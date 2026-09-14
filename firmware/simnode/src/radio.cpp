// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-2; see radio.h.
//
// The state machine follows the bridge's lora_link.cpp (BF-16), which is the reference for
// every RadioLib trap named below. It is a second driver rather than a shared one because
// the two differ in what surrounds the radio - FreeRTOS queues and a task notification on
// the bridge, a polled loop here - and the policy they share, spec 12.3, is already one
// implementation in lib/lran-link.

#include "radio.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <esp_system.h>

#include <cmath>
#include <new>

#include "lran/link/media_access.h"

namespace simnode {
namespace {

using lran::link::CadResult;
using lran::link::MediaAccess;
using lran::link::TxStep;

// Driver objects in static storage (root rule 3). RadioLib's SPIClass Module constructor
// allocates its HAL on the heap; the HAL is built here and passed in instead.
SPIClass g_spi(FSPI);

alignas(ArduinoHal) uint8_t g_hal_storage[sizeof(ArduinoHal)];
alignas(Module) uint8_t     g_module_storage[sizeof(Module)];
alignas(SX1262) uint8_t     g_radio_storage[sizeof(SX1262)];
SX1262*                     g_radio = nullptr;

lran::link::RadioPins g_pins{};
lran::link::PhyConfig g_phy{};
Sink*                 g_log = nullptr;

RadioStats  g_stats;
MediaAccess g_access;

volatile bool g_dio1 = false;
void IRAM_ATTR on_dio1() { g_dio1 = true; }

enum class Mode : uint8_t { Down, Receive, Cad, Transmit };
Mode     g_mode          = Mode::Down;
bool     g_ready         = false;
uint32_t g_mode_start_ms = 0;
uint32_t g_tx_timeout_ms = 0;

OutFrame g_tx;
bool     g_have_tx = false;

uint8_t g_rx_buf[256];  // the SX1262 accepts 255 bytes; spec 14 stage 2a must see them

uint32_t g_begin_failed_ms  = 0;
uint32_t g_last_irq_read_ms = 0;
bool     g_header_seen      = false;
uint32_t g_header_seen_ms   = 0;

constexpr uint32_t kBeginRetryMs      = 10000;
constexpr uint32_t kCadTimeoutMs      = 500;   // a few 4.1 ms symbols at SF9 / 125 kHz
constexpr uint32_t kIrqReadMs         = 1000;  // HEADER_ERR never reaches DIO1
constexpr uint32_t kRxInProgressMaxMs = 1500;  // spec 15.1 - the longest frame at SF9 is 1107 ms

uint32_t elapsed(uint32_t now_ms, uint32_t since_ms) { return now_ms - since_ms; }

int16_t snr_db10(float snr) {
  const long v = std::lround(snr * 10.0f);
  if (v < INT16_MIN + 1) return INT16_MIN + 1;  // INT16_MIN is the "not available" sentinel
  if (v > INT16_MAX) return INT16_MAX;
  return static_cast<int16_t>(v);
}

// spec 12.1 and 12.2. Every call checked but one, which returns void.
int16_t radio_begin() {
  int16_t st = g_radio->begin(
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

  // Explicit `optimize`, as the bridge and the range test do: begin() hardcodes it.
  st = g_radio->setOutputPower(g_phy.conducted_dbm, true);
  if (st != RADIOLIB_ERR_NONE) return st;

  if (g_pins.dio2_as_rf_switch) {
    st = g_radio->setDio2AsRfSwitch(true);
    if (st != RADIOLIB_ERR_NONE) return st;
  }

  // THE ONE UNCHECKED CALL: setRfSwitchPins(rxEn, txEn) returns void in 7.7.1. The Wio Kit
  // needs it as well as DIO2; the Heltec has no RF_SW line and skips it.
  if (g_pins.rf_sw != lran::link::kPinNone) {
    g_radio->setRfSwitchPins(static_cast<uint32_t>(g_pins.rf_sw), RADIOLIB_NC);
  }

  st = g_radio->explicitHeader();  // spec 2.1
  if (st != RADIOLIB_ERR_NONE) return st;
  st = g_radio->setCRC(2);
  if (st != RADIOLIB_ERR_NONE) return st;

  g_radio->setDio1Action(on_dio1);
  return g_radio->startReceive();
}

void end_tx() {
  g_access.finish();
  g_have_tx = false;
}

void radio_failed(int16_t status, uint32_t now_ms) {
  g_ready                   = false;
  g_mode                    = Mode::Down;
  g_begin_failed_ms         = now_ms;
  g_stats.last_begin_status = status;
  ++g_stats.begin_failures;
  if (g_have_tx) end_tx();
  sink_printf(g_log, "radio: down, RadioLib status %d - retrying in %u s", static_cast<int>(status),
              static_cast<unsigned>(kBeginRetryMs / 1000));
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
  g_mode                    = Mode::Receive;
  g_ready                   = true;

  // From the values the radio was configured with, so it cannot drift from kPhy. Conducted
  // power and antenna gain stay separate (root rule 10).
  sink_printf(g_log,
              "radio: up - %lu Hz, SF%u, BW %u.%u kHz, CR 4/%u, %d dBm conducted, %u.%u dBi antenna",
              static_cast<unsigned long>(g_phy.freq_hz), static_cast<unsigned>(g_phy.sf),
              static_cast<unsigned>(g_phy.bw_khz10 / 10), static_cast<unsigned>(g_phy.bw_khz10 % 10),
              static_cast<unsigned>(g_phy.cr_denom), static_cast<int>(g_phy.conducted_dbm),
              static_cast<unsigned>(g_phy.antenna_gain_dbi10 / 10),
              static_cast<unsigned>(g_phy.antenna_gain_dbi10 % 10));
}

void start_receive(uint32_t now_ms) {
  g_dio1        = false;  // TX_DONE and CAD_DONE share DIO1 with RX_DONE
  g_header_seen = false;
  const int16_t st = g_radio->startReceive();
  if (st != RADIOLIB_ERR_NONE) {
    radio_failed(st, now_ms);
    return;
  }
  g_mode = Mode::Receive;
}

void service_receive(Node* node, uint32_t now_ms) {
  if (!g_dio1 && elapsed(now_ms, g_last_irq_read_ms) < kIrqReadMs) return;
  g_dio1             = false;
  g_last_irq_read_ms = now_ms;

  const uint32_t irq = g_radio->getIrqFlags();
  if ((irq & RADIOLIB_SX126X_IRQ_RX_DONE) == 0) {
    // spec 14 stage 1, the header half: a LoRa header failing its own CRC raises no RX_DONE.
    if ((irq & RADIOLIB_SX126X_IRQ_HEADER_ERR) != 0) {
      node->on_phy_crc_error();
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
  g_header_seen      = false;

  if (st == RADIOLIB_ERR_CRC_MISMATCH) {
    node->on_phy_crc_error();
    return;
  }
  if (st != RADIOLIB_ERR_NONE) {
    ++g_stats.rx_driver_errors;
    return;
  }
  node->on_rx(g_rx_buf, len, static_cast<int16_t>(std::lround(rssi)), snr_db10(snr), now_ms);
}

TxStep report_cad(Node* node, CadResult result, uint32_t now_ms) {
  const TxStep next  = g_access.on_cad(result, now_ms, esp_random(), node->radio_counters());
  g_stats.cad_errors = g_access.cad_errors();
  return next;
}

void start_transmit(uint32_t now_ms) {
  if (g_access.forced()) ++g_stats.tx_forced;
  const int16_t st = g_radio->startTransmit(g_tx.bytes, g_tx.len);
  if (st != RADIOLIB_ERR_NONE) {
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

void start_cad(Node* node, uint32_t now_ms) {
  const uint32_t irq = g_radio->getIrqFlags();
  if ((irq & RADIOLIB_SX126X_IRQ_RX_DONE) != 0) {
    g_dio1 = true;  // read the waiting frame first
    return;
  }
  if (reception_in_progress(irq, now_ms)) {
    // A CAD would take the radio out of receive and destroy the arriving frame.
    ++g_stats.cad_deferred;
    if (report_cad(node, CadResult::Busy, now_ms) == TxStep::Transmit) start_transmit(now_ms);
    return;
  }
  if (g_header_seen) {
    start_receive(now_ms);
    if (g_mode != Mode::Receive) return;
  }
  const int16_t st = g_radio->startChannelScan();
  if (st != RADIOLIB_ERR_NONE) {
    if (report_cad(node, CadResult::Error, now_ms) == TxStep::Transmit) {
      start_transmit(now_ms);
    } else {
      start_receive(now_ms);
    }
    return;
  }
  g_mode          = Mode::Cad;
  g_mode_start_ms = now_ms;
}

void service_cad(Node* node, uint32_t now_ms) {
  const uint32_t irq = g_radio->getIrqFlags();
  CadResult      result;
  if ((irq & RADIOLIB_SX126X_IRQ_CAD_DONE) != 0) {
    result = (irq & RADIOLIB_SX126X_IRQ_CAD_DETECTED) != 0 ? CadResult::Busy : CadResult::Free;
  } else if (elapsed(now_ms, g_mode_start_ms) >= kCadTimeoutMs) {
    result = CadResult::Error;
  } else {
    return;
  }
  if (report_cad(node, result, now_ms) == TxStep::Transmit) {
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

void service_tx(Node* node, Outbox* outbox, uint32_t now_ms) {
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
      start_cad(node, now_ms);
      return;
    case TxStep::Transmit:
      start_transmit(now_ms);
      return;
  }
}

}  // namespace

void radio_start(const lran::link::RadioPins& pins, const lran::link::PhyConfig& phy, Sink* log) {
  g_pins = pins;
  g_phy  = phy;
  g_log  = log;

  g_spi.begin(pins.sck, pins.miso, pins.mosi, pins.nss);
  ArduinoHal* hal    = new (g_hal_storage) ArduinoHal(g_spi);
  Module*     module = new (g_module_storage)
      Module(hal, static_cast<uint32_t>(pins.nss), static_cast<uint32_t>(pins.dio1),
             static_cast<uint32_t>(pins.rst), static_cast<uint32_t>(pins.busy));
  g_radio = new (g_radio_storage) SX1262(module);

  try_begin(millis());
}

void radio_service(Node* node, Outbox* outbox, uint32_t now_ms) {
  if (g_radio == nullptr) return;
  switch (g_mode) {
    case Mode::Down:
      if (elapsed(now_ms, g_begin_failed_ms) >= kBeginRetryMs) try_begin(now_ms);
      break;
    case Mode::Receive:
      service_receive(node, now_ms);
      if (g_mode == Mode::Receive) service_tx(node, outbox, now_ms);
      break;
    case Mode::Cad:
      service_cad(node, now_ms);
      break;
    case Mode::Transmit:
      service_transmit(now_ms);
      break;
  }
}

bool radio_ready() { return g_ready; }

const RadioStats& radio_stats() { return g_stats; }

}  // namespace simnode
