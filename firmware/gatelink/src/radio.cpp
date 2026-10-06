// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The carrier's SX1262. GL1; GateLink Impl Plan 5.2; spec 12.1, 12.2.

#include "radio.h"

#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>

#include <new>

#include "board_profile.h"
#include "lran/link/radio_config.h"
#include "spi_bus.h"

namespace gatelink {
namespace {

using lran::link::kPhy;

// readRegister() is protected in RadioLib 7.7.1, and the bring-up image's bus test read
// the sync word through it (bringup.cpp).
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

// spec 12.1 and 12.2, in the order the bring-up image's radio_configure() proved.
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
  // begin() leaves DIO1 a plain input, which clears the pull-down that expansion board
  // 7.1.1 asks for (engineering log, 2026-10-05).
  pinMode(p.dio1, INPUT_PULLDOWN);
  return RADIOLIB_ERR_NONE;
}

}  // namespace

int16_t radio_begin() {
  const auto& p = kCarrierRadio;
  if (g_radio == nullptr) {
    ArduinoHal* hal    = new (g_hal_storage) ArduinoHal(SPI);
    Module*     module = new (g_module_storage)
        Module(hal, static_cast<uint32_t>(p.nss), static_cast<uint32_t>(p.dio1),
               static_cast<uint32_t>(p.rst), static_cast<uint32_t>(p.busy));
    g_radio = new (g_radio_storage) ProbeRadio(module);
  }
  SpiLock lock;
  return configure();
}

bool radio_probe() {
  if (g_radio == nullptr) return false;
  uint8_t sync[2] = {};
  SpiLock lock;
  const int16_t st = g_radio->readRegister(RADIOLIB_SX126X_REG_LORA_SYNC_WORD_MSB, sync, 2);
  return st == RADIOLIB_ERR_NONE && sync[0] == 0x14 && sync[1] == 0x24;
}

}  // namespace gatelink
