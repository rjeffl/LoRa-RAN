// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The bridge board's SX1262 pin map, checked against its panel. Task BF-16; spec 12.2,
// Impl Plan 10.8.1, R-4.1b.
//
// ARDUINO-FREE AND DRIVER-FREE, so the values are host-tested and a pin collision is a
// compile error. lora_link.cpp is the only file that turns kPinNone into RADIOLIB_NC.
//
// The struct shape and the fixed PHY moved to lib/lran-link's radio_config.h on
// 2026-09-14, when the simnode became the second firmware to need them; the bridge names
// them through the using declarations below. What stays here is what belongs to this
// board: its pin map and the check against its own panel.
//
// PROVENANCE. The pin values are the range test's kHeltecV3, transcribed there from the
// vendor variant pins_arduino.h at framework-arduinoespressif32 3.20017.241212 and
// confirmed over the air in range-test pass 1 and B1a. The VALUES carry over; the code
// does not - firmware/range-test/CLAUDE.md forbids migrating its sources into node
// firmware, so the struct is written here rather than included from there. Impl Plan
// 10.8.1 is the one home for the values: correct them there first.

#pragma once

#include <cstddef>
#include <cstdint>

#include "board_ui.h"
#include "lran/link/radio_config.h"

namespace bridge {

using lran::link::kEirpCeilingDbm10;
using lran::link::kPhy;
using lran::link::kPinNone;
using lran::link::PhyConfig;
using lran::link::RadioPins;

// Heltec WiFi LoRa 32 V3 - the bridge board.
//
// GPIO 14 IS DIO1. The vendor header calls it `DIO0`, the SX127x name; on the SX1262
// that line is DIO1 and it is what RadioLib wants as its IRQ pin.
//
// TWO SETTINGS HERE FAIL SILENTLY: the radio initialises, reports a successful
// transmit, and puts nothing on the air. tcxo_mv 1800 - the V3 has a TCXO, and the
// wrong voltage presents as a radio that will not calibrate. dio2_as_rf_switch - the
// V3 switches its RF path from DIO2, and without it the PA never reaches the antenna.
inline constexpr RadioPins kHeltecV3Radio = {
    /* nss               */ 8,
    /* rst               */ 12,
    /* busy              */ 13,
    /* dio1              */ 14,
    /* sck               */ 9,
    /* miso              */ 11,
    /* mosi              */ 10,
    /* rf_sw             */ kPinNone,
    /* tcxo_mv           */ 1800,
    /* dio2_as_rf_switch */ true,
};

// Every GPIO the bridge drives, radio and panel, in one list. Two peripherals on one
// pin means one of them does not work and the symptom lands on the other subsystem.
// kPinNone never collides with anything, including itself.
constexpr bool has_pin_conflict(const RadioPins& r, const BoardUiConfig& u) {
  const int8_t pins[] = {r.nss, r.rst,   r.busy, r.dio1,  r.sck, r.miso,
                         r.mosi, r.rf_sw, u.sda,  u.scl,  u.rst, u.vext};
  const size_t n = sizeof(pins) / sizeof(pins[0]);
  for (size_t i = 0; i < n; ++i) {
    if (pins[i] == kPinNone) continue;
    for (size_t j = i + 1; j < n; ++j) {
      if (pins[j] != kPinNone && pins[i] == pins[j]) return true;
    }
  }
  return false;
}

static_assert(!has_pin_conflict(kHeltecV3Radio, kHeltecV3Ui),
              "Heltec V3 - radio and panel share a GPIO");

}  // namespace bridge
