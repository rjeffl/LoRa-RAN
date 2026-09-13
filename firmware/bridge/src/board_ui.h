// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The Heltec V3's OLED, as data. Task BF-14.
//
// INJECTED, NOT #DEFINED - spec 12.2's reasoning for the radio, applied to the other
// peripheral, and the same shape firmware/range-test/ settled on in pass 2. The bridge
// has one board, so one instance; the struct is what keeps a second board a config
// addition rather than an edit to the renderer.
//
// PROVENANCE. Values from the vendor variant pins_arduino.h (SDA_OLED 17, SCL_OLED 18,
// RST_OLED 21, Vext 36), confirmed on this board by /wattcycle-reader and by the range
// test (firmware/range-test/src/board_config.h). The VALUES carry over; the code does
// not - firmware/range-test/CLAUDE.md forbids migrating its sources into node firmware.
//
// No collision with the bridge's radio pins (Impl Plan 10.8.1: nss 8, rst 12, busy 13,
// dio1 14, sck 9, miso 11, mosi 10). BF-16 adds those; when it does, the range test's
// has_pin_conflict() pattern belongs here too.

#pragma once

#include <cstdint>

namespace bridge {

struct BoardUiConfig {
  uint8_t addr;             // SSD1306 I2C address
  int8_t  sda;
  int8_t  scl;
  int8_t  rst;              // panel reset line
  int8_t  vext;             // Vext enable, ACTIVE LOW - the panel is powered through it
  bool    flip_vertically;  // the V3 mounts its panel rotated; verified on hardware
};

inline constexpr BoardUiConfig kHeltecV3Ui = {
    /* addr            */ 0x3C,
    /* sda             */ 17,
    /* scl             */ 18,
    /* rst             */ 21,
    /* vext            */ 36,
    /* flip_vertically */ true,
};

}  // namespace bridge
