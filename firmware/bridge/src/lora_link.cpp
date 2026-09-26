// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The SX1262 and lora_task's loop. Task BF-16; see lora_link.h.

#include "lora_link.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cmath>
#include <new>

#include "lran/link/chan_monitor.h"
#include "frame_log.h"
#include "queues.h"
#include "rx_deaf.h"
#include "rx_wake.h"
#include "task_runtime.h"

namespace bridge {
namespace {

// ---------------------------------------------------------------------------
// The driver objects, in static storage (root rule 3).
//
// RadioLib's Module(cs, irq, rst, gpio, SPIClass&) constructor allocates its HAL with
// `new ArduinoHal` (Module.cpp, 7.7.1). The HAL is built here instead and passed to the
// constructor that takes one, so nothing on the radio path reaches the heap. All three
// are constructed once: a retried begin() re-initialises the chip, not the objects.
// ---------------------------------------------------------------------------

// FSPI is the ESP32-S3's general-purpose SPI controller; on this chip HSPI serves the
// flash and PSRAM.
SPIClass g_spi(FSPI);

alignas(ArduinoHal) uint8_t g_hal_storage[sizeof(ArduinoHal)];
alignas(Module) uint8_t     g_module_storage[sizeof(Module)];
alignas(SX1262) uint8_t     g_radio_storage[sizeof(SX1262)];
SX1262*                     g_radio = nullptr;

RadioPins g_pins{};

// BF-27 - Impl Plan 6.6's raw frame log. Written from lora_task only and drained by
// log_task, which is what frame_log.h's single-producer claim rests on.
//
// ALWAYS RECORDING, WITH NO RUNTIME LEVER TO TURN IT OFF. A lever would need the
// HA-visible configuration path, which does not exist and whose route is an open
// operator decision (docs/bridge/HANDOFF.md); building one here would decide it by
// default. The cost this leaves running is a 24-byte store per frame, bounded by the
// frame rate the link already carries - the DRAIN is where the cost would be, and that
// is log_task's, which is the lowest-priority task in the bridge (tasks.h).
FrameLog g_frame_log;

// M25 - what else is on 917.4 MHz. Sampled from lora_task, drained by log_task, same as
// the frame log and for the same reasons (chan_monitor.h).
ChanMonitor g_chan;
PhyConfig g_phy{};

// spec 12.4.1 - a retune asked for by sched_task. Guarded by g_diag_mux, which already
// protects the one other cross-task hand-off in this file.
PhyConfig             g_phy_next{};
uint32_t              g_phy_requested = 0;
std::atomic<uint32_t> g_phy_applied{0};

// The last ticketed frame to leave lora_task, and when. Guarded by g_diag_mux, like the
// retune request above. lora_tx_finished() reads them.
uint32_t g_tx_done_ticket = 0;
uint32_t g_tx_done_ms     = 0;

lran::Counters g_counters;
LoraStats      g_stats;
RxLadder       g_ladder(&g_counters);
MediaAccess    g_access;

// The copy other tasks read (lora_diag_snapshot). Written only by lora_task, under the mux.
constexpr uint32_t kDiagSnapshotMs = 1000;
portMUX_TYPE       g_diag_mux      = portMUX_INITIALIZER_UNLOCKED;
lran::Counters     g_diag_counters;
LoraStats          g_diag_stats;
uint32_t           g_diag_copied_ms   = 0;
uint32_t           g_diag_err_suppressed = 0;

// BF-19a, spec 14.2. The policy decides whether an ERROR may be sent; this task builds it
// and queues it like any other frame, so it takes its turn at media access behind whatever
// is already waiting. g_error_seq is the bridge's own sequence space: local and advisory
// (spec 10.2), and it advances no high-water mark anywhere.
ErrorReplyPolicy   g_error_policy;
lran::Seq          g_error_seq = 1;

// Read by other tasks: ota_task for R-5.3d and the image verdict.
// R-3.1f (BF-22) - the last (src, ver) refused at stage 4, packed into one word so the
// write is a single atomic store and needs no lock on lora_task's side. 0 means nothing
// pending; sched_task takes it and clears it. A second refusal before the first is
// collected overwrites it, which is correct: the counter rx_bad_ver is the count, and
// this is only the identity of the skew.
std::atomic<uint16_t> g_bad_ver_seen{0};

std::atomic<bool> g_ready{false};
std::atomic<bool> g_idle{true};

// Written by the ISR. Plain volatile rather than std::atomic, because the ISR runs from
// IRAM while the flash cache may be disabled - an OTA flash write disables it - and an
// out-of-line atomic store would be a call into flash.
TaskHandle_t  g_task = nullptr;
volatile bool g_dio1 = false;

// rx_deaf.h owns these, so that which modes are deaf is one documented fact with a host
// test. The alias keeps every call site below reading Mode::Receive.
using Mode = RadioMode;
Mode     g_mode          = Mode::Down;
uint32_t g_mode_start_ms = 0;
uint32_t g_tx_timeout_ms = 0;

TxMessage g_tx;
bool      g_have_tx = false;

// Static rather than on lora_task's stack, which is 8 KB in all (tasks.cpp). 256 bytes
// because the SX1262 accepts a 255-byte packet, and spec 14 stage 2a has to see the real
// length of one to count it.
uint8_t   g_rx_buf[256];
RxMessage g_rx_msg;

uint32_t g_begin_failed_ms = 0;
uint32_t g_last_irq_read_ms = 0;
bool     g_header_seen      = false;
uint32_t g_header_seen_ms   = 0;

constexpr uint32_t kBeginRetryMs = 10000;

// A CAD at SF9 / BW 125 kHz spans a few 4.1 ms symbols. Half a second is a radio that is
// not going to answer, not a slow one.
constexpr uint32_t kCadTimeoutMs = 500;

// Belt and braces for a lost DIO1 wake: while receiving, the IRQ register is read at least
// this often, at the cost of one SPI transaction a second.
//
// IT IS NOT ONLY BELT AND BRACES, AND THIS INTERVAL IS UNDER MEASUREMENT (engineering log,
// 2026-09-17). DIO1 is a level output read on its rising edge, so a frame arriving while
// the previous RX_DONE is still set raises no edge and this read is the ONLY thing that
// finds it - which makes this interval the deadline a frame has to beat. rx_wake.h holds
// the mechanism; g_stats.rx_no_interrupt counts how often it happens.
//
// It is also the only path that can find HEADER_ERR, which the DIO1 mask excludes. Do not
// remove this read.
constexpr uint32_t kIrqReadMs = 1000;

// spec 15.1 - the longest frame at SF9 is 1107 ms. A valid header seen longer ago than
// this with no RX_DONE behind it is a reception that died, not one still arriving.
constexpr uint32_t kRxInProgressMaxMs = 1500;

void IRAM_ATTR on_dio1() {
  g_dio1 = true;
  if (g_task == nullptr) return;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(g_task, &woken);
  if (woken == pdTRUE) portYIELD_FROM_ISR();
}

// Unsigned subtraction: correct across a millis() wrap.
uint32_t elapsed(uint32_t now_ms, uint32_t since_ms) { return now_ms - since_ms; }

int8_t snr_to_i8(float snr) {
  const long v = std::lround(snr);
  if (v < INT8_MIN) return INT8_MIN;
  if (v > INT8_MAX) return INT8_MAX;
  return static_cast<int8_t>(v);
}

// spec 12.1 and 12.2. Every call is checked, and the two settings that fail silently on
// the Heltec - TCXO voltage and DIO2 as RF switch - are both applied here.
int16_t radio_begin() {
  const int16_t st = g_radio->begin(
      /* freq            */ static_cast<float>(g_phy.freq_hz) / 1000000.0f,
      /* bw              */ static_cast<float>(g_phy.bw_khz10) / 10.0f,
      /* sf              */ g_phy.sf,
      /* cr              */ g_phy.cr_denom,
      /* syncWord        */ g_phy.sync_word,
      /* power           */ g_phy.conducted_dbm,
      /* preambleLength  */ g_phy.preamble_symbols,
      /* tcxoVoltage     */ static_cast<float>(g_pins.tcxo_mv) / 1000.0f,
      /* useRegulatorLDO */ false);
  if (st != RADIOLIB_ERR_NONE) return st;

  // Explicit `optimize`: begin() sets the power through the one-argument overload, which
  // hardcodes it. Both are true today; this call is what makes it the bridge's decision
  // rather than a library default (firmware/range-test's PA configuration reasoning).
  int16_t r = g_radio->setOutputPower(g_phy.conducted_dbm, true);
  if (r != RADIOLIB_ERR_NONE) return r;

  if (g_pins.dio2_as_rf_switch) {
    r = g_radio->setDio2AsRfSwitch(true);
    if (r != RADIOLIB_ERR_NONE) return r;
  }

  // THE ONE UNCHECKED RADIO CALL: setRfSwitchPins(rxEn, txEn) returns void in 7.7.1. The
  // Heltec has no RF_SW line and skips it; a Wio profile would not.
  if (g_pins.rf_sw != kPinNone) {
    g_radio->setRfSwitchPins(static_cast<uint32_t>(g_pins.rf_sw), RADIOLIB_NC);
  }

  // spec 2.1 requires explicit header and CRC on. RadioLib's defaults agree today; a later
  // version is free not to.
  r = g_radio->explicitHeader();
  if (r != RADIOLIB_ERR_NONE) return r;
  r = g_radio->setCRC(2);
  if (r != RADIOLIB_ERR_NONE) return r;

  g_radio->setDio1Action(on_dio1);
  return g_radio->startReceive();
}

// THE ONE PLACE g_mode CHANGES. Closing the deaf interval here rather than at each call
// site makes it structurally impossible to leave Cad or Transmit without accounting for
// the time the radio spent there (rx_deaf.h) - a new transition would have to bypass this
// function to escape it. A mode that was hearing adds zero, so no caller has to ask.
void enter_mode(Mode next, uint32_t now_ms) {
  const uint32_t add = deaf_elapsed(g_mode, g_mode_start_ms, now_ms);
  g_stats.rx_deaf_ms = add > UINT32_MAX - g_stats.rx_deaf_ms ? UINT32_MAX
                                                             : g_stats.rx_deaf_ms + add;
  g_mode             = next;
  g_mode_start_ms    = now_ms;
}

// A frame left lora_task, on air or not. Every path that lets go of g_tx calls this, so a
// reply window waiting on it opens even when the frame never went out.
void note_tx_done(uint32_t now_ms) {
  if (g_tx.ticket == 0) return;
  portENTER_CRITICAL(&g_diag_mux);
  g_tx_done_ticket = g_tx.ticket;
  g_tx_done_ms     = now_ms;
  portEXIT_CRITICAL(&g_diag_mux);
}

void end_tx(uint32_t now_ms) {
  g_access.finish();
  g_have_tx = false;
  note_tx_done(now_ms);
}

void radio_failed(int16_t status, uint32_t now_ms) {
  g_ready            = false;
  enter_mode(Mode::Down, now_ms);
  g_begin_failed_ms  = now_ms;
  g_stats.last_begin_status = status;
  ++g_stats.begin_failures;
  if (g_have_tx) {
    ++g_stats.tx_dropped_no_radio;
    end_tx(now_ms);
  }
  // A line every 10 s from a dead radio is loud on purpose; it is the only symptom a
  // field log would otherwise show.
  log_printf(LogLevel::Error, "LoRa: radio down, RadioLib status %d - retrying in %lu s\n",
             static_cast<int>(status), static_cast<unsigned long>(kBeginRetryMs / 1000));
}

void try_begin(uint32_t now_ms) {
  const int16_t st = radio_begin();
  if (st != RADIOLIB_ERR_NONE) {
    radio_failed(st, now_ms);
    return;
  }
  g_stats.last_begin_status = st;
  g_dio1                    = false;
  g_header_seen             = false;
  enter_mode(Mode::Receive, now_ms);
  g_ready                   = true;

  // What the radio was actually configured with, from the values it was configured from.
  // The banner's PHY line is a string; this one cannot drift from kPhy. Conducted power
  // and antenna gain stay separate (root rule 10).
  log_printf(LogLevel::Info,
             "LoRa: radio up - %lu Hz, SF%u, BW %u.%u kHz, CR 4/%u, %d dBm conducted, "
             "%u.%u dBi antenna\n",
             static_cast<unsigned long>(g_phy.freq_hz), static_cast<unsigned>(g_phy.sf),
             static_cast<unsigned>(g_phy.bw_khz10 / 10),
             static_cast<unsigned>(g_phy.bw_khz10 % 10),
             static_cast<unsigned>(g_phy.cr_denom), static_cast<int>(g_phy.conducted_dbm),
             static_cast<unsigned>(g_phy.antenna_gain_dbi10 / 10),
             static_cast<unsigned>(g_phy.antenna_gain_dbi10 % 10));

  // THE FALSIFIER FOR lora_task's STACK SIZE (tasks.cpp). Taken after begin() and the
  // log_printf above, which formats on this stack: the two deepest things this task does.
  // Bytes never touched since the task started; a small number here is the warning a
  // stack overflow never gives.
  log_printf(LogLevel::Info, "LoRa: stack high-water %u bytes free\n",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

void start_receive(uint32_t now_ms) {
  // TX_DONE and CAD_DONE arrive on the same DIO1 line as RX_DONE. The wake they left is
  // dropped here, the one point it is known not to be a packet.
  g_dio1        = false;
  g_header_seen = false;
  const int16_t st = g_radio->startReceive();
  if (st != RADIOLIB_ERR_NONE) {
    radio_failed(st, now_ms);
    return;
  }
  enter_mode(Mode::Receive, now_ms);
}

void queue_delivery(const RxDelivery& d, float rssi, float snr, uint32_t now_ms) {
  // RxLadder caps every payload at spec 11.2's largest set, so this cannot trigger. It is
  // checked rather than trusted because the alternative on failure is a truncated payload,
  // and nothing in this firmware truncates (firmware/bridge/CLAUDE.md).
  if (d.payload_len > sizeof(g_rx_msg.payload)) {
    ++g_stats.rx_driver_errors;
    return;
  }
  g_rx_msg.hdr = d.hdr;
  for (size_t i = 0; i < d.payload_len; ++i) g_rx_msg.payload[i] = d.payload[i];
  g_rx_msg.payload_len  = d.payload_len;
  g_rx_msg.fragments    = d.fragments;
  g_rx_msg.mac_verified = d.mac_verified;
  g_rx_msg.rssi_dbm     = static_cast<int16_t>(std::lround(rssi));
  g_rx_msg.snr_db       = snr_to_i8(snr);
  g_rx_msg.rx_millis    = now_ms;

  // Zero ticks. A full RX queue is counted inside send_rx, never waited on.
  (void)send_rx(g_rx_msg);
}

// BF-19a - spec 14.2. Consults the policy, and on a yes builds the frame and posts it to
// the TX queue with no wait, exactly as a poll is posted. A queue that refuses it has
// already counted the drop; the frame that provoked the ERROR is counted by its own stage
// either way, so nothing here is lost silently.
void reply_error(lran::Status s, lran::NodeId src, lran::Seq seq, uint32_t now_ms) {
  const bool registered = g_ladder.registered(src);
  const ErrorReply reply = g_error_policy.decide(s, src, seq, registered, now_ms);
  if (!reply.send) return;

  TxMessage tx;
  tx.dst = reply.dst;
  tx.len = build_error_frame(reply, g_error_seq++, tx.bytes, sizeof(tx.bytes));
  if (tx.len == 0) return;
  (void)send_tx(tx);
}

// BF-27 - one record for a reception. Impl Plan 6.6.
//
// THE LADDER'S `last_*` ARE ONLY READ WHEN THE LADDER RAN. On a header error, a PHY CRC
// failure or a driver error, accept() was never called and those accessors still hold
// the PREVIOUS frame's header - reading them would attribute a loss to whichever node
// happened to transmit last, which is worse than recording nothing.
//
// RSSI AND SNR ARE ONLY READ WHERE THEY MEAN THIS FRAME. getRSSI() answers for the last
// completed reception and is not cleared, the same trap getPacketLength() sets; on a
// header error there was no completed reception, so the record carries no radio
// metadata rather than the previous frame's.
void log_rx(RxOutcome outcome, uint32_t now_ms, bool ladder_ran, bool radio_meta,
            float rssi, float snr) {
  FrameLogEntry e;
  e.ms      = now_ms;
  e.deaf_ms = g_stats.rx_deaf_ms;
  e.dir     = static_cast<uint8_t>(FrameDir::Rx);
  e.rx      = static_cast<uint8_t>(outcome);

  if (ladder_ran) {
    e.status = static_cast<uint8_t>(g_ladder.last_status());
    e.peer   = g_ladder.last_src();
    e.seq    = g_ladder.last_seq();
    e.type   = g_ladder.last_type();
    e.schema = g_ladder.last_schema();
    e.frag   = g_ladder.last_frag();
  } else {
    e.status = kStatusNotRun;
  }

  if (radio_meta) {
    e.rssi_dbm = static_cast<int16_t>(std::lround(rssi));
    e.snr_db   = snr_to_i8(snr);
  }

  g_frame_log.record(e);
}

void service_receive(uint32_t now_ms) {
  const RxWake wake = rx_wake(g_dio1, now_ms, g_last_irq_read_ms, kIrqReadMs);
  if (wake == RxWake::Skip) return;
  g_dio1             = false;
  g_last_irq_read_ms = now_ms;

  const uint32_t irq = g_radio->getIrqFlags();

  // Kept, rather than switched on directly: BF-27's record needs to say which of the
  // two delivering cases this was, and re-deriving it below would be a second place for
  // the same answer to be got wrong.
  const RxPass pass = rx_pass(wake, (irq & RADIOLIB_SX126X_IRQ_RX_DONE) != 0,
                              (irq & RADIOLIB_SX126X_IRQ_HEADER_ERR) != 0);

  switch (pass) {
    case RxPass::HeaderError:
      // spec 14 stage 1, the header half. A LoRa header that fails its own CRC raises no
      // RX_DONE and never reaches DIO1, so it is found by the timed read or not at all.
      // Counted as the PHY CRC error it is, and receive is restarted to clear the register.
      g_ladder.on_phy_crc_error();
      log_rx(RxOutcome::HeaderError, now_ms, false, false, 0.0f, 0.0f);
      start_receive(now_ms);
      return;
    case RxPass::WakeEmpty:
      ++g_stats.rx_wake_empty;
      return;
    case RxPass::Nothing:
      return;
    case RxPass::Orphan:
      // No edge ever arrived for this frame; the timed read found it. rx_wake.h has the
      // mechanism. Counted before the read, so the count survives a read that then fails.
      ++g_stats.rx_no_interrupt;
      break;
    case RxPass::Packet:
      break;
  }

  // NEVER ASK getPacketLength() WHETHER A PACKET ARRIVED. It holds the last packet's length
  // and is not cleared by reading it; a poll built on it re-reports one frame forever
  // (firmware/range-test/CLAUDE.md, 2026-08-31). RX_DONE above says one arrived; this only
  // asks how big it is.
  size_t len = g_radio->getPacketLength();
  if (len > sizeof(g_rx_buf)) len = sizeof(g_rx_buf);

  const int16_t st   = g_radio->readData(g_rx_buf, len);  // clears the IRQ register
  const float   rssi = g_radio->getRSSI();
  const float   snr  = g_radio->getSNR();
  g_header_seen      = false;

  if (st == RADIOLIB_ERR_CRC_MISMATCH) {
    g_ladder.on_phy_crc_error();  // spec 14 stage 1
    // The reception COMPLETED - rssi and snr describe this frame, and a corrupt frame's
    // signal level is the measurement that separates an RF story from a software one.
    log_rx(RxOutcome::PhyCrc, now_ms, false, true, rssi, snr);
    return;
  }
  if (st != RADIOLIB_ERR_NONE) {
    ++g_stats.rx_driver_errors;
    log_rx(RxOutcome::DriverError, now_ms, false, false, 0.0f, 0.0f);
    return;
  }

  RxDelivery d;
  if (g_ladder.accept(g_rx_buf, len, now_ms, &d)) {
    queue_delivery(d, rssi, snr, now_ms);
  } else {
    // spec 14.2 (BF-19a). A stage that names no ERROR, a source the registry does not
    // know, or a frame too short to have a readable `src` all leave decide() at no.
    reply_error(g_ladder.last_status(), g_ladder.last_src(), g_ladder.last_seq(), now_ms);

    // R-3.1f (BF-22). A frame discarded at spec 14 stage 4 never reaches the registry,
    // so the version that caused it would be lost and the node would simply fall silent.
    // Published here as one word for sched_task to collect, because lora_task MUST NOT
    // call registry_runtime - that waits on a mutex, and this task never waits.
    if (g_ladder.last_status() == lran::Status::BadVersion && g_ladder.last_src() != 0) {
      g_bad_ver_seen = static_cast<uint16_t>(
          (static_cast<uint16_t>(g_ladder.last_src()) << 8) | g_ladder.last_ver());
    }
  }
  // BF-27 - Impl Plan 6.6. After the branch, so `last_status()` is the ladder's verdict
  // on THIS frame: Ok for a delivered payload, and for an incomplete reassembly set too
  // (rx_ladder.h), which is why `frag` is in the record.
  log_rx(pass == RxPass::Orphan ? RxOutcome::Orphan : RxOutcome::Packet, now_ms, true,
         true, rssi, snr);
  g_chan.note_own_rx();  // M25 - so a busy bucket is not read as a quiet channel
}

TxStep report_cad(CadResult result, uint32_t now_ms) {
  const TxStep next = g_access.on_cad(result, now_ms, esp_random(), &g_counters);
  g_stats.cad_errors = g_access.cad_errors();
  return next;
}

void start_transmit(uint32_t now_ms) {
  if (g_access.forced()) ++g_stats.tx_forced;

  const int16_t st = g_radio->startTransmit(g_tx.bytes, g_tx.len);
  if (st != RADIOLIB_ERR_NONE) {
    ++g_stats.tx_errors;
    end_tx(now_ms);
    start_receive(now_ms);
    return;
  }

  // RadioLib's own blocking transmit() allows 5 ms plus five times the time on air
  // (getTimeOnAir() is in microseconds). The same deadline, without the busy wait.
  g_tx_timeout_ms =
      5 + static_cast<uint32_t>((g_radio->getTimeOnAir(g_tx.len) * 5) / 1000);
  enter_mode(Mode::Transmit, now_ms);

  // BF-27 - Impl Plan 6.6's "in and out". RECORDED AFTER enter_mode(), so `deaf_ms`
  // carries the CAD that preceded this transmission rather than leaving it to the next
  // record: a bridge action and the deafness it cost belong to the same line.
  //
  // Read off the encoded frame rather than from a header the caller keeps, because
  // lora_task has no decoded view of what it is sending - the originator encoded it
  // (queues.h) and the bytes are the only thing that crossed.
  {
    FrameLogEntry e;
    e.ms      = now_ms;
    e.deaf_ms = g_stats.rx_deaf_ms;
    e.dir     = static_cast<uint8_t>(FrameDir::Tx);
    e.rx      = static_cast<uint8_t>(RxOutcome::Transmitted);
    e.status  = static_cast<uint8_t>(lran::Status::Ok);
    e.peer    = g_tx.dst;
    if (g_tx.len >= lran::kHdrLen) {
      // spec 5.2, 5.4, 5.6, 5.7 - explicit offsets, little-endian (root rule 1).
      e.type   = g_tx.bytes[1];
      e.seq    = static_cast<lran::Seq>(static_cast<uint16_t>(g_tx.bytes[4]) |
                                     (static_cast<uint16_t>(g_tx.bytes[5]) << 8));
      e.frag   = g_tx.bytes[10];
      e.schema = g_tx.bytes[11];
    }
    g_frame_log.record(e);
  }
}

// A frame is arriving when the radio has seen a valid header and no RX_DONE yet. Only a
// recent header counts: one older than the longest frame is a reception that died, and a
// stale flag would otherwise defer every CAD after it.
bool reception_in_progress(uint32_t irq, uint32_t now_ms) {
  if ((irq & RADIOLIB_SX126X_IRQ_HEADER_VALID) == 0) {
    g_header_seen = false;
    return false;
  }
  if (!g_header_seen) {
    g_header_seen    = true;
    g_header_seen_ms = now_ms;
    return true;
  }
  return elapsed(now_ms, g_header_seen_ms) < kRxInProgressMaxMs;
}

void start_cad(uint32_t now_ms) {
  const uint32_t irq = g_radio->getIrqFlags();

  // A frame is already waiting to be read. Read it on the next pass, then CAD.
  if ((irq & RADIOLIB_SX126X_IRQ_RX_DONE) != 0) {
    g_dio1 = true;
    return;
  }

  if (reception_in_progress(irq, now_ms)) {
    // The channel is busy by definition - busy with a frame this radio is receiving. A CAD
    // would take the radio out of receive and destroy that frame. Reported as the busy CAD
    // it stands in for, and the radio stays in receive.
    ++g_stats.cad_deferred;
    if (report_cad(CadResult::Busy, now_ms) == TxStep::Transmit) start_transmit(now_ms);
    return;
  }
  if (g_header_seen) {
    // The header outlived the longest frame: that reception died. Clear the register.
    start_receive(now_ms);
    if (g_mode != Mode::Receive) return;
  }

  const int16_t st = g_radio->startChannelScan();
  if (st != RADIOLIB_ERR_NONE) {
    if (report_cad(CadResult::Error, now_ms) == TxStep::Transmit) {
      start_transmit(now_ms);
    } else {
      start_receive(now_ms);
    }
    return;
  }
  enter_mode(Mode::Cad, now_ms);
}

void service_cad(uint32_t now_ms) {
  const uint32_t irq = g_radio->getIrqFlags();

  CadResult result;
  if ((irq & RADIOLIB_SX126X_IRQ_CAD_DONE) != 0) {
    result = (irq & RADIOLIB_SX126X_IRQ_CAD_DETECTED) != 0 ? CadResult::Busy
                                                           : CadResult::Free;
    // spec 12.3 counts a busy CAD and nothing else, so this outcome was invisible.
    if (result == CadResult::Free) ++g_stats.cad_free;
  } else if (elapsed(now_ms, g_mode_start_ms) >= kCadTimeoutMs) {
    result = CadResult::Error;
  } else {
    return;
  }

  if (report_cad(result, now_ms) == TxStep::Transmit) {
    start_transmit(now_ms);  // straight from CAD to TX: no receive window to lose the channel in
  } else {
    start_receive(now_ms);
  }
}

void service_transmit(uint32_t now_ms) {
  const uint32_t irq = g_radio->getIrqFlags();
  if ((irq & RADIOLIB_SX126X_IRQ_TX_DONE) != 0) {
    ++g_counters.tx_frames;
  } else if (elapsed(now_ms, g_mode_start_ms) >= g_tx_timeout_ms) {
    ++g_stats.tx_timeouts;
  } else {
    return;
  }
  (void)g_radio->finishTransmit();  // clears the IRQ register and returns to standby
  end_tx(now_ms);
  start_receive(now_ms);
}

void service_tx(uint32_t now_ms) {
  if (!g_have_tx) {
    if (!take_tx(&g_tx)) return;
    g_have_tx = true;
    g_access.start(now_ms);
  }

  switch (g_access.step(now_ms)) {
    case TxStep::Idle:
      g_have_tx = false;
      note_tx_done(now_ms);
      return;
    case TxStep::Wait:
      return;  // backing off, and still receiving
    case TxStep::Cad:
      start_cad(now_ms);
      return;
    case TxStep::Transmit:
      start_transmit(now_ms);
      return;
  }
}

}  // namespace

void lora_start(const RadioPins& pins, const PhyConfig& phy) {
  g_task = xTaskGetCurrentTaskHandle();
  g_pins = pins;
  g_phy  = phy;

  g_spi.begin(pins.sck, pins.miso, pins.mosi, pins.nss);
  ArduinoHal* hal    = new (g_hal_storage) ArduinoHal(g_spi);
  Module*     module = new (g_module_storage)
      Module(hal, static_cast<uint32_t>(pins.nss), static_cast<uint32_t>(pins.dio1),
             static_cast<uint32_t>(pins.rst), static_cast<uint32_t>(pins.busy));
  g_radio = new (g_radio_storage) SX1262(module);

  try_begin(millis());
}

// M25 - one channel sample per lora_task wake. chan_monitor.h has the reasoning.
//
// ONLY WHILE THE RADIO IS IN RECEIVE AND NOTHING OF OURS IS ARRIVING. GET_RSSI_INST
// answers for whatever the receiver is hearing now, so a reading taken during our own
// reception measures the simnode a metre away and a reading taken in transmit or CAD
// measures nothing at all. Both are counted as skips rather than dropped, because an
// occupancy figure whose denominator is unstated is not a figure.
//
// ~100 SAMPLES A SECOND, set by kLoraMaxWaitMs and deliberately not raised. Shortening
// that wait to sample faster would change lora_task's duty cycle, and this instrument
// exists to measure the channel rather than to perturb the thing it is measuring.
void sample_channel(uint32_t now_ms) {
  // READ, NEVER reception_in_progress(): that function needs the IRQ register, which is
  // another SPI transaction, and it MUTATES g_header_seen - calling it here would drive
  // the CAD deferral logic from the sampler. The same staleness bound is applied, so a
  // header that never completed stops suppressing samples instead of suppressing them
  // for good.
  const bool ours_arriving =
      g_header_seen && elapsed(now_ms, g_header_seen_ms) < kRxInProgressMaxMs;

  if (g_mode != Mode::Receive || ours_arriving) {
    g_chan.skip(now_ms);
    return;
  }
  // getRSSI(false) is the GET_RSSI_INST command - one short SPI read, no wait
  // (RadioLib 7.7.1, SX126x.cpp). Half-dB resolution, recorded in tenths.
  g_chan.sample(static_cast<Dbm10>(std::lround(g_radio->getRSSI(false) * 10.0f)), now_ms);
}

// spec 12.4.1 step 5. radio_begin() reconfigures every setting from g_phy, so a retune
// is the path the radio came up on, with its checks and its log line. A retune that fails
// leaves the radio down and retried every 10 s on the new settings, and the fleet
// machine's deadline then reverts them.
void service_retune(uint32_t now_ms) {
  const bool ours_arriving =
      g_header_seen && elapsed(now_ms, g_header_seen_ms) < kRxInProgressMaxMs;
  if (g_have_tx || ours_arriving || (g_mode != Mode::Receive && g_mode != Mode::Down)) return;

  uint32_t ticket = 0;
  portENTER_CRITICAL(&g_diag_mux);
  if (g_phy_requested != g_phy_applied.load()) {
    ticket = g_phy_requested;
    g_phy  = g_phy_next;
  }
  portEXIT_CRITICAL(&g_diag_mux);
  if (ticket == 0) return;

  try_begin(now_ms);
  g_phy_applied = ticket;
}

void lora_service(uint32_t now_ms) {
  if (g_radio == nullptr) return;

  service_retune(now_ms);
  sample_channel(now_ms);

  switch (g_mode) {
    case Mode::Down:
      // Nothing can be sent. Draining keeps producers' drop counters honest: a frame left
      // queued would go out long after its poll or command had timed out upstream.
      while (take_tx(&g_tx)) {
        ++g_stats.tx_dropped_no_radio;
        note_tx_done(now_ms);
      }
      if (elapsed(now_ms, g_begin_failed_ms) >= kBeginRetryMs) try_begin(now_ms);
      break;
    case Mode::Receive:
      service_receive(now_ms);
      if (g_mode == Mode::Receive) service_tx(now_ms);
      break;
    case Mode::Cad:
      service_cad(now_ms);
      break;
    case Mode::Transmit:
      service_transmit(now_ms);
      break;
  }

  // spec 14 stage 10 - a set whose remaining fragments never arrived is answered from the
  // tick, not from an arrival, because the peer's silence is the whole event (spec 11.2).
  ExpiredSet   expired[kReassemblySlots];
  const size_t n = g_ladder.tick(now_ms, expired, kReassemblySlots);
  for (size_t i = 0; i < n && i < kReassemblySlots; ++i) {
    reply_error(lran::Status::ReassemblyTimeout, expired[i].src, expired[i].seq, now_ms);
  }

  // BF-19. Another task reading g_counters field by field could see rx_dropped's parts
  // from two moments. A spinlock, not a mutex: lora_task never waits on another task, and
  // this holds the other core off for one ~150-byte copy, once a second.
  if (elapsed(now_ms, g_diag_copied_ms) >= kDiagSnapshotMs) {
    g_diag_copied_ms = now_ms;
    portENTER_CRITICAL(&g_diag_mux);
    g_diag_counters       = g_counters;
    g_diag_stats          = g_stats;
    g_diag_err_suppressed = g_error_policy.suppressed();
    portEXIT_CRITICAL(&g_diag_mux);
  }

  g_idle = (g_mode == Mode::Receive || g_mode == Mode::Down) && !g_have_tx &&
           !g_ladder.any_set_active();
}

void lora_wait(uint32_t max_wait_ms) {
  // Bounded, and woken by DIO1. Not a queue wait and not a network wait - the two things
  // lora_task must never do (Impl Plan 5.2, tools/checks/lora_task_never_blocks.py).
  (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(max_wait_ms));
}

void lora_configure(const MediaAccessConfig& access, uint32_t frag_timeout_ms) {
  g_access.set_config(access);
  g_ladder.set_frag_timeout_ms(frag_timeout_ms);
}

uint32_t lora_request_phy(const PhyConfig& phy) {
  portENTER_CRITICAL(&g_diag_mux);
  g_phy_next      = phy;
  g_phy_requested = g_phy_requested + 1 == 0 ? 1 : g_phy_requested + 1;
  const uint32_t ticket = g_phy_requested;
  portEXIT_CRITICAL(&g_diag_mux);
  return ticket;
}

bool lora_phy_applied(uint32_t ticket) { return g_phy_applied.load() == ticket; }

bool lora_tx_finished(uint32_t ticket, uint32_t* done_ms) {
  portENTER_CRITICAL(&g_diag_mux);
  const uint32_t t  = g_tx_done_ticket;
  const uint32_t ms = g_tx_done_ms;
  portEXIT_CRITICAL(&g_diag_mux);
  // Wrap-safe, like every millis() comparison here: tickets wrap too, skipping 0.
  if (t == 0 || static_cast<int32_t>(t - ticket) < 0) return false;
  *done_ms = ms;
  return true;
}

void lora_configure_errors(uint32_t min_interval_ms) {
  g_error_policy.configure(min_interval_ms);
}

uint32_t lora_errors_suppressed() {
  portENTER_CRITICAL(&g_diag_mux);
  const uint32_t v = g_diag_err_suppressed;
  portEXIT_CRITICAL(&g_diag_mux);
  return v;
}

bool lora_take_bad_version(lran::NodeId* src, uint8_t* ver) {
  const uint16_t v = g_bad_ver_seen.exchange(0);
  if (v == 0) return false;
  *src = static_cast<lran::NodeId>(v >> 8);
  *ver = static_cast<uint8_t>(v & 0xFF);
  return true;
}

void lora_set_auth(lran::IMac* mac, const PeerKeys* keys) { g_ladder.set_auth(mac, keys); }

bool lora_radio_ready() { return g_ready; }

bool lora_idle() { return g_idle; }

bool lora_take_frame_log(FrameLogEntry* out) { return g_frame_log.read_next(out); }

bool lora_take_chan(ChanBucket* out) { return g_chan.take(out); }

uint32_t lora_chan_lost() { return g_chan.lost(); }

uint32_t lora_frame_log_lost() { return g_frame_log.lost(); }

void lora_diag_snapshot(lran::Counters* counters, LoraStats* stats) {
  portENTER_CRITICAL(&g_diag_mux);
  if (counters != nullptr) *counters = g_diag_counters;
  if (stats != nullptr) *stats = g_diag_stats;
  portEXIT_CRITICAL(&g_diag_mux);
}

}  // namespace bridge
