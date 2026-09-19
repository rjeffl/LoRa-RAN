// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// See receiver.h. The bring-up follows firmware/simnode/src/radio.cpp, which is the
// reference for the RadioLib traps named below, less everything that sends.

#include "receiver.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

#include <cmath>
#include <new>

namespace chancap {
namespace {

// Driver objects in static storage (root rule 3). RadioLib's SPIClass Module constructor
// allocates its HAL on the heap; the HAL is built here and passed in instead.
SPIClass g_spi(FSPI);

alignas(ArduinoHal) uint8_t g_hal_storage[sizeof(ArduinoHal)];
alignas(Module) uint8_t     g_module_storage[sizeof(Module)];
alignas(SX1262) uint8_t     g_radio_storage[sizeof(SX1262)];
SX1262*                     g_radio = nullptr;

lran::link::RadioPins g_pins{};
lran::link::PhyConfig g_phy{};
uint32_t              g_freq_hz = 0;

ReceiverStats g_stats;
bool          g_ready           = false;
uint32_t      g_begin_failed_ms = 0;

volatile bool g_dio1 = false;
void IRAM_ATTR on_dio1() { g_dio1 = true; }

constexpr uint32_t kBeginRetryMs = 10000;

uint8_t g_rx_buf[256];  // read and discarded: the frame is evidence of a sender, not data

// spec 12.1 and 12.2, at this image's frequency. Every call checked but one, which returns
// void. begin() takes a power argument and configures the PA with it; nothing here ever
// starts a transmission, so the PA is configured and never keyed.
int16_t radio_begin() {
  int16_t st = g_radio->begin(
      /* freq            */ static_cast<float>(g_freq_hz) / 1000000.0f,
      /* bw              */ static_cast<float>(g_phy.bw_khz10) / 10.0f,
      /* sf              */ g_phy.sf,
      /* cr              */ g_phy.cr_denom,
      /* syncWord        */ g_phy.sync_word,
      /* power           */ g_phy.conducted_dbm,
      /* preambleLength  */ g_phy.preamble_symbols,
      /* tcxoVoltage     */ static_cast<float>(g_pins.tcxo_mv) / 1000.0f,
      /* useRegulatorLDO */ false);
  if (st != RADIOLIB_ERR_NONE) return st;

  // The RF switch matters on receive as much as on transmit: with the wrong path selected
  // the receiver hears a dead end and the capture reads as an unusually quiet channel.
  if (g_pins.dio2_as_rf_switch) {
    st = g_radio->setDio2AsRfSwitch(true);
    if (st != RADIOLIB_ERR_NONE) return st;
  }
  // THE ONE UNCHECKED CALL: setRfSwitchPins(rxEn, txEn) returns void in 7.7.1. On the Wio
  // Kit rf_sw is the receive enable; the Heltec has no such line.
  if (g_pins.rf_sw != lran::link::kPinNone) {
    g_radio->setRfSwitchPins(static_cast<uint32_t>(g_pins.rf_sw), RADIOLIB_NC);
  }

  // spec 2.1. The same header and CRC settings as the fleet, so an LRAN frame on the
  // channel decodes and is counted rather than read only as energy.
  st = g_radio->explicitHeader();
  if (st != RADIOLIB_ERR_NONE) return st;
  st = g_radio->setCRC(2);
  if (st != RADIOLIB_ERR_NONE) return st;

  g_radio->setDio1Action(on_dio1);
  return g_radio->startReceive();  // continuous: RADIOLIB_SX126X_RX_TIMEOUT_INF
}

void try_begin(uint32_t now_ms) {
  const int16_t st = radio_begin();
  g_stats.last_begin_status = st;
  if (st != RADIOLIB_ERR_NONE) {
    g_ready           = false;
    g_begin_failed_ms = now_ms;
    ++g_stats.begin_failures;
    return;
  }
  g_dio1  = false;
  g_ready = true;
}

}  // namespace

void receiver_start(const lran::link::RadioPins& pins, const lran::link::PhyConfig& phy,
                    uint32_t freq_hz, uint32_t now_ms) {
  g_pins    = pins;
  g_phy     = phy;
  g_freq_hz = freq_hz;

  g_spi.begin(pins.sck, pins.miso, pins.mosi, pins.nss);
  ArduinoHal* hal    = new (g_hal_storage) ArduinoHal(g_spi);
  Module*     module = new (g_module_storage)
      Module(hal, static_cast<uint32_t>(pins.nss), static_cast<uint32_t>(pins.dio1),
             static_cast<uint32_t>(pins.rst), static_cast<uint32_t>(pins.busy));
  g_radio = new (g_radio_storage) SX1262(module);

  try_begin(now_ms);
}

bool receiver_service(uint32_t now_ms) {
  if (g_radio == nullptr) return false;
  if (!g_ready) {
    if (now_ms - g_begin_failed_ms >= kBeginRetryMs) try_begin(now_ms);
    return false;
  }
  if (!g_dio1) return false;
  g_dio1 = false;

  // RadioLib's default receive mask routes RX_DONE alone to DIO1, and the SX1262 raises
  // RX_DONE on a CRC failure as well. readData() reads the buffer and clears the IRQ
  // register without leaving receive (RadioLib 7.7.1, SX126x.cpp), which is what lets DIO1
  // fire for the next frame. NEVER ASK getPacketLength() WHETHER A PACKET ARRIVED - it holds
  // the last length.
  const int16_t st = g_radio->readData(g_rx_buf, 0);
  if (st == RADIOLIB_ERR_CRC_MISMATCH) {
    ++g_stats.frames_bad_crc;
  } else if (st == RADIOLIB_ERR_NONE) {
    ++g_stats.frames_heard;
  }
  return true;
}

bool receiver_ready() { return g_ready; }

int16_t receiver_rssi_dbm10() {
  // getRSSI(false) is GET_RSSI_INST - one short SPI read, no wait (RadioLib 7.7.1), the
  // same call the bridge's sampler makes. Half-dB resolution, recorded in tenths.
  return static_cast<int16_t>(std::lround(g_radio->getRSSI(false) * 10.0f));
}

const ReceiverStats& receiver_stats() { return g_stats; }

}  // namespace chancap
