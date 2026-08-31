// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R2 - the injected radio pin map (spec 12.2).
//
// Deliberately free of every Arduino and RadioLib header so the map is host
// testable. `kPinNone` stands in for RADIOLIB_NC; radio_link.cpp is the only
// file that translates one to the other. A pin map that can only be compiled
// for the target is a pin map no host test can check.

#pragma once

#include <cstdint>

namespace rangetest {

// RADIOLIB_NC is -1. Spelled locally so this header pulls in no driver.
inline constexpr int8_t kPinNone = -1;

// spec 12.2 - the pin map, TCXO reference voltage and RF-switch mode are supplied
// by configuration at construction, never compiled in. Pass 1 has one board type,
// which makes the seam look like ceremony; it is the reason pass 2 is a config
// addition rather than a rewrite (task R2), and four firmwares depend on it.
//
// TCXO voltage is TENTHS OF A VOLT, not a float. The value is compared and printed
// in the settings dump (R3) and a float that prints as "1.8" but compares unequal to
// 1.8f is a debugging session nobody needs. spec 12.2's own figure is 1.8 V.
struct BoardRadioConfig {
  const char* name;

  int8_t nss;
  int8_t rst;
  int8_t busy;
  int8_t dio1;

  int8_t sck;
  int8_t miso;
  int8_t mosi;

  // kPinNone when DIO2 alone drives the RF switch. Present rather than absent so the
  // struct shape is identical across profiles and the driver needs no #ifdef
  // (Bridge Impl Plan 10.8.1).
  int8_t rf_sw;

  uint16_t tcxo_mv;
  bool     dio2_as_rf_switch;
};

// Heltec WiFi LoRa 32 V3 - SX1262 on a dedicated internal SPI bus.
//
// PIN PROVENANCE, because task R2 requires it and "from memory or a forum post" is
// what it rules out. Every GPIO below is transcribed from the vendor board
// definition shipped with the Arduino core:
//
//   ~/.platformio/packages/framework-arduinoespressif32/
//       variants/heltec_wifi_lora_32_V3/pins_arduino.h
//
// read at framework-arduinoespressif32 3.20017.241212+sha.dcc1105b, which is the
// version espressif32@6.13.0 resolves - the platform this project pins. Recorded
// because "the vendor definition" is only a checkable claim with a version on it.
//
//   SS = 8   SCK = 9   MISO = 11   MOSI = 10   RST_LoRa = 12   BUSY_LoRa = 13
//
// They agree, value for value, with Bridge Impl Plan 10.8.1's LRAN_PROFILE_HELTEC
// entry, which described itself as "the community-standard V3 assignment" and
// derived rather than transcribed. It is now confirmed against the vendor variant.
//
// ONE NAMING TRAP. The variant header calls GPIO 14 `DIO0`, which is the SX127x
// name; on the SX1262 that line is DIO1 and it is what RadioLib wants as its IRQ
// pin. The number is right and the label is legacy - do not "correct" it to a
// different GPIO on the strength of the name.
//
// The two silent-failure settings (task R2, spec 12.2) are here on the first commit
// rather than added after an afternoon of debugging a radio that reports a
// successful transmit and puts nothing on the air:
//   - tcxo_mv 1800. The V3 uses a TCXO, not a crystal. Wrong value presents as a
//     radio that will not calibrate, not as an error.
//   - dio2_as_rf_switch true. The V3 switches its RF path from DIO2; without it the
//     PA is never connected to the antenna.
inline constexpr BoardRadioConfig kHeltecV3 = {
    /* name              */ "heltec_wifi_lora_32_V3",
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

// PASS 2 SLOT - XIAO ESP32S3 + Wio-SX1262. NOT POPULATED AND NOT BUILT.
//
// Deliberately left as a declaration rather than filled in from Bridge Impl Plan
// 10.8.1's XIAO column, because that column says of itself: "Ring out the XIAO
// column against the module on arrival and correct this table in place." Copying
// unrung values here would create a second uncorrected copy, which is the exact
// propagation 10.8.1 warns about.
//
// Pass 2 populates this from `gatelink-expansion-board.md` rev 0.3 and rings it out.
// If pass 2 needs more than this one struct, R2 was built wrong and that belongs in
// the engineering log rather than being absorbed quietly (task, Pass 2 section).
//
// TODO(R2-pass2): populate from gatelink-expansion-board rev 0.3, after ring-out.

}  // namespace rangetest
