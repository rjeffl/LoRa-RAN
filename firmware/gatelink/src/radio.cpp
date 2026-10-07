// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The carrier's SX1262. GL1, GL3; see radio.h.

#include "radio.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cmath>
#include <new>

#include "board_profile.h"
#include "lran/link/media_access.h"
#include "lran/link/radio_config.h"
#include "lran/link/rx_arrival.h"
#include "spi_bus.h"

namespace gatelink {
namespace {

using lran::link::CadResult;
using lran::link::kPhy;
using lran::link::MediaAccess;
using lran::link::TxStep;
using lran::node::OutFrame;

// readRegister() is protected in RadioLib 7.7.1, and GL1's bus test reads the sync word
// through it.
class ProbeRadio : public SX1262 {
 public:
  using SX1262::SX1262;
  using SX126x::readRegister;
};

// Static storage, as the bring-up image keeps it (root rule 3). The HAL takes the global
// SPI object, which board_begin() started on the carrier's pins and SD.begin() also uses.
alignas(ArduinoHal) uint8_t g_hal_storage[sizeof(ArduinoHal)];
alignas(Module) uint8_t     g_module_storage[sizeof(Module)];
alignas(ProbeRadio) uint8_t g_radio_storage[sizeof(ProbeRadio)];
ProbeRadio*                 g_radio = nullptr;

TaskHandle_t  g_task = nullptr;
volatile bool g_dio1 = false;

// RX_DONE, CAD_DONE and TX_DONE share DIO1. The flag and the notification only; the IRQ
// register is read on lora_task, under the SpiLock.
void IRAM_ATTR on_dio1() {
  g_dio1             = true;
  BaseType_t woken   = pdFALSE;
  if (g_task != nullptr) vTaskNotifyGiveFromISR(g_task, &woken);
  portYIELD_FROM_ISR(woken);
}

RadioStats  g_stats;
MediaAccess g_access;

enum class Mode : uint8_t { Down, Receive, Cad, Transmit };
Mode     g_mode          = Mode::Down;
uint32_t g_mode_start_ms = 0;
uint32_t g_tx_timeout_ms = 0;

OutFrame g_tx;
bool     g_have_tx = false;

uint8_t g_rx_buf[256];  // the SX1262 accepts 255 bytes; spec 14 stage 2a must see them

uint32_t              g_begin_failed_ms  = 0;
uint32_t              g_last_irq_read_ms = 0;
lran::link::RxArrival g_arrival;  // the CAD guard's view of an arriving frame

constexpr uint32_t kBeginRetryMs      = 10000;
constexpr uint32_t kCadTimeoutMs      = 500;   // a few 4.1 ms symbols at SF9 / 125 kHz
constexpr uint32_t kIrqReadMs         = 1000;  // HEADER_ERR never reaches DIO1
constexpr uint32_t kRxInProgressMaxMs = 1500;  // spec 15.1 - the longest frame at SF9 is 1107 ms

// PREAMBLE_DETECTED added to RadioLib's receive flags, so the CAD guard sees a frame from
// its preamble on (rx_arrival.h). Flags only: DIO1 stays on RX_DONE alone.
constexpr RadioLibIrqFlags_t kRxIrqFlags =
    RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED);

uint32_t elapsed(uint32_t now_ms, uint32_t since_ms) { return now_ms - since_ms; }

void note_error(const char* at, int16_t status) {
  g_stats.last_error_at = at;
  g_stats.last_error    = status;
}

int16_t snr_db10(float snr) {
  const long v = std::lround(snr * 10.0f);
  if (v < INT16_MIN + 1) return INT16_MIN + 1;  // INT16_MIN is the "not available" sentinel
  if (v > INT16_MAX) return INT16_MAX;
  return static_cast<int16_t>(v);
}

int16_t start_receive_radio() {
  return g_radio->startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF, kRxIrqFlags,
                               RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
}

lran::link::RxArrivalState observe_arrival(uint32_t irq, uint32_t now_ms) {
  return g_arrival.observe((irq & RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED) != 0,
                           (irq & RADIOLIB_SX126X_IRQ_HEADER_VALID) != 0, now_ms);
}

// spec 12.1 and 12.2, in the order the bring-up image's radio_configure() proved. The
// caller holds the SpiLock.
int16_t configure() {
  const auto& p = kCarrierRadio;
  int16_t st = g_radio->begin(static_cast<float>(kPhy.freq_hz) / 1000000.0f,
                              static_cast<float>(kPhy.bw_khz10) / 10.0f, kPhy.sf, kPhy.cr_denom,
                              kPhy.sync_word, kPhy.conducted_dbm, kPhy.preamble_symbols,
                              static_cast<float>(p.tcxo_mv) / 1000.0f, false);
  if (st != RADIOLIB_ERR_NONE) return st;
  st = g_radio->setOutputPower(kPhy.conducted_dbm, true);
  if (st != RADIOLIB_ERR_NONE) return st;
  st = g_radio->setDio2AsRfSwitch(p.dio2_as_rf_switch);
  if (st != RADIOLIB_ERR_NONE) return st;
  g_radio->setRfSwitchPins(static_cast<uint32_t>(p.rf_sw), RADIOLIB_NC);  // returns void
  st = g_radio->explicitHeader();
  if (st != RADIOLIB_ERR_NONE) return st;
  st = g_radio->setCRC(2);
  if (st != RADIOLIB_ERR_NONE) return st;
  g_radio->setDio1Action(on_dio1);
  // begin() leaves DIO1 a plain input, which clears the pull-down that expansion board
  // 7.1.1 asks for (engineering log, 2026-10-05).
  pinMode(p.dio1, INPUT_PULLDOWN);
  return start_receive_radio();
}

void end_tx() {
  g_access.finish();
  g_have_tx = false;
}

void radio_failed(int16_t status, uint32_t now_ms) {
  g_mode                    = Mode::Down;
  g_begin_failed_ms         = now_ms;
  g_stats.last_begin_status = status;
  ++g_stats.begin_failures;
  if (g_have_tx) end_tx();
}

int16_t try_begin(uint32_t now_ms) {
  const int16_t st = configure();
  if (st != RADIOLIB_ERR_NONE) {
    note_error("begin", st);
    radio_failed(st, now_ms);
    return st;
  }
  g_stats.last_begin_status = st;
  g_dio1                    = false;
  g_arrival.reset();
  g_arrival.set_bounds(lran::link::preamble_to_header_ms(kPhy), kRxInProgressMaxMs,
                       lran::link::burst_holdoff_ms(kPhy));
  g_mode = Mode::Receive;
  return st;
}

void start_receive(uint32_t now_ms) {
  g_dio1 = false;  // TX_DONE and CAD_DONE share DIO1 with RX_DONE
  g_arrival.reset();
  const int16_t st = start_receive_radio();
  if (st != RADIOLIB_ERR_NONE) {
    note_error("startReceive", st);
    radio_failed(st, now_ms);
    return;
  }
  g_mode = Mode::Receive;
}

void service_receive(RadioClient* client, uint32_t now_ms) {
  if (!g_dio1 && elapsed(now_ms, g_last_irq_read_ms) < kIrqReadMs) return;
  g_dio1             = false;
  g_last_irq_read_ms = now_ms;

  const uint32_t irq = g_radio->getIrqFlags();
  (void)observe_arrival(irq, now_ms);  // dated here, so a dead one is stale when a CAD asks
  if ((irq & RADIOLIB_SX126X_IRQ_RX_DONE) == 0) {
    // spec 14 stage 1, the header half: a LoRa header failing its own CRC raises no RX_DONE.
    if ((irq & RADIOLIB_SX126X_IRQ_HEADER_ERR) != 0) {
      client->on_phy_crc_error(now_ms);
      g_arrival.note_reception_end(now_ms);
      start_receive(now_ms);
    }
    return;
  }

  // NEVER ASK getPacketLength() WHETHER A PACKET ARRIVED - it holds the last length. RX_DONE
  // above says one arrived; this only asks its size.
  size_t len = g_radio->getPacketLength();
  if (len > sizeof(g_rx_buf)) len = sizeof(g_rx_buf);

  const int16_t st   = g_radio->readData(g_rx_buf, len);  // clears the IRQ register
  const float   rssi = g_radio->getRSSI();
  const float   snr  = g_radio->getSNR();
  g_arrival.reset();
  g_arrival.note_reception_end(now_ms);  // the next frame of a burst may be starting

  if (st == RADIOLIB_ERR_CRC_MISMATCH) {
    client->on_phy_crc_error(now_ms);
    return;
  }
  if (st != RADIOLIB_ERR_NONE) {
    ++g_stats.rx_driver_errors;
    return;
  }
  ++g_stats.rx_frames;
  client->on_frame(g_rx_buf, len, static_cast<int16_t>(std::lround(rssi)), snr_db10(snr), now_ms);
}

TxStep report_cad(RadioClient* client, CadResult result, uint32_t now_ms) {
  const TxStep next  = g_access.on_cad(result, now_ms, esp_random(), client->counters());
  g_stats.cad_errors = g_access.cad_errors();
  return next;
}

void start_transmit(uint32_t now_ms) {
  if (g_access.forced()) ++g_stats.tx_forced;
  const int16_t st = g_radio->startTransmit(g_tx.bytes, g_tx.len);
  if (st != RADIOLIB_ERR_NONE) {
    note_error("startTransmit", st);
    ++g_stats.tx_errors;
    end_tx();
    start_receive(now_ms);
    return;
  }
  // RadioLib's own blocking transmit() allows 5 ms plus five times the time on air.
  g_tx_timeout_ms = 5 + static_cast<uint32_t>((g_radio->getTimeOnAir(g_tx.len) * 5) / 1000);
  g_mode          = Mode::Transmit;
  g_mode_start_ms = now_ms;
}

void start_cad(RadioClient* client, uint32_t now_ms) {
  // Too soon after a reception for a burst's next frame to show: ask again next pass.
  if (g_arrival.holding_off(now_ms)) return;

  const uint32_t irq = g_radio->getIrqFlags();
  if ((irq & RADIOLIB_SX126X_IRQ_RX_DONE) != 0) {
    g_dio1 = true;  // read the waiting frame first
    return;
  }
  const lran::link::RxArrivalState arrival = observe_arrival(irq, now_ms);
  if (arrival == lran::link::RxArrivalState::Arriving) {
    // A CAD would take the radio out of receive and destroy the arriving frame, from its
    // preamble on.
    ++g_stats.cad_deferred;
    if (report_cad(client, CadResult::Busy, now_ms) == TxStep::Transmit) start_transmit(now_ms);
    return;
  }
  if (arrival == lran::link::RxArrivalState::Stale) {
    // A reception that died. Clear the register and CAD on a later pass, so a preamble
    // hidden behind the sticky flag can raise it again.
    start_receive(now_ms);
    return;
  }
  const int16_t st = g_radio->startChannelScan();
  if (st != RADIOLIB_ERR_NONE) {
    note_error("startChannelScan", st);
    if (report_cad(client, CadResult::Error, now_ms) == TxStep::Transmit) {
      start_transmit(now_ms);
    } else {
      start_receive(now_ms);
    }
    return;
  }
  g_mode          = Mode::Cad;
  g_mode_start_ms = now_ms;
}

void service_cad(RadioClient* client, uint32_t now_ms) {
  const uint32_t irq = g_radio->getIrqFlags();
  CadResult      result;
  if ((irq & RADIOLIB_SX126X_IRQ_CAD_DONE) != 0) {
    result = (irq & RADIOLIB_SX126X_IRQ_CAD_DETECTED) != 0 ? CadResult::Busy : CadResult::Free;
  } else if (elapsed(now_ms, g_mode_start_ms) >= kCadTimeoutMs) {
    result = CadResult::Error;
  } else {
    return;
  }
  if (report_cad(client, result, now_ms) == TxStep::Transmit) {
    start_transmit(now_ms);
  } else {
    start_receive(now_ms);
  }
}

void service_transmit(uint32_t now_ms) {
  const uint32_t irq = g_radio->getIrqFlags();
  if ((irq & RADIOLIB_SX126X_IRQ_TX_DONE) != 0) {
    ++g_stats.tx_frames;
  } else if (elapsed(now_ms, g_mode_start_ms) >= g_tx_timeout_ms) {
    ++g_stats.tx_timeouts;
  } else {
    return;
  }
  (void)g_radio->finishTransmit();
  end_tx();
  start_receive(now_ms);
}

// TODO(GL5): take the LoRa/BLE interlock before the CAD (R-4.3h, Impl Plan 5.2). bms_task
// is a stub until then, so nothing holds it.
void service_tx(RadioClient* client, lran::node::Outbox* outbox, uint32_t now_ms) {
  if (!g_have_tx) {
    if (!outbox->pop(&g_tx)) return;
    g_have_tx = true;
    g_access.start(now_ms);
  }
  switch (g_access.step(now_ms)) {
    case TxStep::Idle:
      g_have_tx = false;
      return;
    case TxStep::Wait:
      return;
    case TxStep::Cad:
      start_cad(client, now_ms);
      return;
    case TxStep::Transmit:
      start_transmit(now_ms);
      return;
  }
}

}  // namespace

int16_t radio_begin(void* task) {
  const auto& p = kCarrierRadio;
  g_task        = static_cast<TaskHandle_t>(task);
  if (g_radio == nullptr) {
    ArduinoHal* hal    = new (g_hal_storage) ArduinoHal(SPI);
    Module*     module = new (g_module_storage)
        Module(hal, static_cast<uint32_t>(p.nss), static_cast<uint32_t>(p.dio1),
               static_cast<uint32_t>(p.rst), static_cast<uint32_t>(p.busy));
    g_radio = new (g_radio_storage) ProbeRadio(module);
  }
  SpiLock lock;
  return try_begin(millis());
}

void radio_service(RadioClient* client, lran::node::Outbox* outbox, uint32_t now_ms) {
  if (g_radio == nullptr) return;
  SpiLock lock;
  switch (g_mode) {
    case Mode::Down:
      if (elapsed(now_ms, g_begin_failed_ms) >= kBeginRetryMs) (void)try_begin(now_ms);
      break;
    case Mode::Receive:
      service_receive(client, now_ms);
      if (g_mode == Mode::Receive) service_tx(client, outbox, now_ms);
      break;
    case Mode::Cad:
      service_cad(client, now_ms);
      break;
    case Mode::Transmit:
      service_transmit(now_ms);
      break;
  }
}

bool radio_tx_active() { return g_have_tx || g_mode == Mode::Cad || g_mode == Mode::Transmit; }

bool radio_tx_idle() { return !radio_tx_active(); }

bool radio_ready() { return g_mode != Mode::Down; }

const RadioStats& radio_stats() { return g_stats; }

bool radio_probe() {
  if (g_radio == nullptr || g_mode == Mode::Down) return false;
  uint8_t sync[2] = {};
  SpiLock lock;
  const int16_t st = g_radio->readRegister(RADIOLIB_SX126X_REG_LORA_SYNC_WORD_MSB, sync, 2);
  return st == RADIOLIB_ERR_NONE && sync[0] == 0x14 && sync[1] == 0x24;
}

}  // namespace gatelink
