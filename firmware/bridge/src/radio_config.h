// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The SX1262's pin map and the fixed PHY, as data. Task BF-16; spec 12.1, 12.2,
// Impl Plan 10.8.1, R-4.1b.
//
// ARDUINO-FREE AND DRIVER-FREE, so the values are host-tested and a pin collision is a
// compile error. lora_link.cpp is the only file that turns kPinNone into RADIOLIB_NC.
//
// PROVENANCE. The pin values are the range test's kHeltecV3, transcribed there from the
// vendor variant pins_arduino.h at framework-arduinoespressif32 3.20017.241212 and
// confirmed over the air in range-test pass 1 and B1a. The VALUES carry over; the code
// does not - firmware/range-test/CLAUDE.md forbids migrating its sources into node
// firmware, so the struct is written here rather than included from there. Impl Plan
// 10.8.1 is the one home for the values: correct them there first.
//
// ONE DEVIATION FROM 10.8.1's SKETCH: TCXO voltage is millivolts, not a float. The range
// test found a float that prints as "1.8" and compares unequal to 1.8f is a debugging
// session nobody needs; the bridge takes the same answer for the same reason.

#pragma once

#include <cstddef>
#include <cstdint>

#include "board_ui.h"

namespace bridge {

// RADIOLIB_NC is -1. Spelled locally so this header pulls in no driver.
inline constexpr int8_t kPinNone = -1;

// spec 12.2 - the pin map, TCXO reference voltage and RF-switch mode are supplied by
// configuration at construction, never compiled into the driver.
struct RadioPins {
  int8_t nss;
  int8_t rst;
  int8_t busy;
  int8_t dio1;

  int8_t sck;
  int8_t miso;
  int8_t mosi;

  // kPinNone when DIO2 alone drives the RF switch. Present on every board so the
  // driver needs no #ifdef; the Wio-SX1262 carries a real one (Impl Plan 10.8.1).
  int8_t rf_sw;

  uint16_t tcxo_mv;
  bool     dio2_as_rf_switch;
};

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

// spec 12.1 - the working point D1 fixed on 2026-09-10 (Decision Register 3.4).
//
// NOT RUNTIME-CONFIGURABLE, and deliberately beside the pin map rather than in the
// HA-visible configuration: changing the PHY from Home Assistant changes the link the
// change travels over, and a node left on the other side of that is a walk to the gate.
struct PhyConfig {
  uint32_t freq_hz;
  uint16_t bw_khz10;  // tenths of a kHz: 1250 is 125.0 kHz
  uint8_t  sf;
  uint8_t  cr_denom;  // 5 is CR 4/5

  // Root rule 10 - conducted power and antenna gain are recorded SEPARATELY. The D33
  // ceiling is EIRP, and a combined figure cannot be audited.
  int8_t  conducted_dbm;
  uint8_t antenna_gain_dbi10;  // 30 is 3.0 dBi, Bridge PRD R-4.3a.1's stick

  // spec 12.1's "Private (0x12 / SX126x 0x1424)" are one value at two layers. RadioLib
  // takes the one-byte form and expands it into the SX126x's register pair.
  uint8_t sync_word;
  uint8_t preamble_symbols;  // spec 15.1 computes airtime on 8
};

inline constexpr PhyConfig kPhy = {
    /* freq_hz            */ 917400000u,
    /* bw_khz10           */ 1250,
    /* sf                 */ 9,
    /* cr_denom           */ 5,
    /* conducted_dbm      */ -4,
    /* antenna_gain_dbi10 */ 30,
    /* sync_word          */ 0x12,
    /* preamble_symbols   */ 8,
};

// D33 / spec 18.2 - at or below -1 dBm EIRP. Integer tenths of a dB, so a rounding
// direction cannot put a half-dB above the ceiling. A change to either term that
// breaks the ceiling is a compile error here, not a finding on a spectrum analyser.
inline constexpr int kEirpCeilingDbm10 = -10;
static_assert(kPhy.conducted_dbm * 10 + kPhy.antenna_gain_dbi10 <= kEirpCeilingDbm10,
              "D33 - conducted power plus antenna gain exceeds the EIRP ceiling");

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
