// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R1 / R6 - the onboard SSD1306.
//
// The bring-up sequence (Vext, then reset pulse, then I2C, then probe) and the pin
// numbers are lifted from /wattcycle-reader/src/BmsDisplay.cpp, where they are
// verified on this exact board. Task "read this first": leverage wattcycle-reader
// code elements for access to the Heltec OLED. Re-deriving a working Vext sequence
// would be the definition of reinventing.
//
// R6 wants text large enough to read outdoors at arm's length in sunlight, which is
// why the link figures use the 24 px font and the labels do not.

#pragma once

#include <SSD1306Wire.h>

#include <cstdint>

#include "role.h"

namespace rangetest {

// Vendor variant pins_arduino.h: SDA_OLED 17, SCL_OLED 18, RST_OLED 21, Vext 36.
// Identical to the values wattcycle-reader confirmed on hardware.
inline constexpr int     kPinOledSda = 17;
inline constexpr int     kPinOledScl = 18;
inline constexpr int     kPinOledRst = 21;
inline constexpr int     kPinVext    = 36;  // ACTIVE LOW: LOW = Vext ON
inline constexpr uint8_t kOledAddr   = 0x3c;

class Ui {
 public:
  Ui();

  // Powers Vext, pulses the OLED reset, brings up I2C, probes 0x3C. Returns false if
  // the panel does not ACK - reported rather than guessed at, because a dark display
  // on this board is usually Vext and not the driver.
  bool begin();

  // R1 - the role selection window. Shows the countdown and what a press will do.
  void show_role_prompt(uint32_t ms_remaining);

  // R1 - the role, shown at startup.
  void show_role(Role r, const char* board_name);

  void show_message(const char* line1, const char* line2);

  // R5 - the initiator between sweeps, and the responder between positions. The
  // operator is at the far end looking at the walking unit, so "what position am I,
  // and did the last sweep finish" has to be readable without the laptop.
  void show_armed(Role r, uint16_t position_id, uint16_t sweeps_done);

  // R1 / R6 - live link quality. Shown by BOTH roles: R1 asks the initiator to echo
  // working status and live link quality, and R6 asks the responder for the same
  // figures, so one renderer serves both.
  void show_link(Role r, float rssi_dbm, float snr_db, uint16_t position_id,
                 uint32_t ok_count, uint32_t total_count);

  bool ok() const { return ok_; }

 private:
  // The two boards are physically identical and their USB bridges even report the
  // same serial string, so the display is the ONLY thing that says which is which.
  // Drawn as an inverted tag rather than plain text: it survives being glanced at
  // across a bench and, later, at arm's length in sunlight on a fence post.
  void draw_role_badge(Role r);

  SSD1306Wire display_;
  bool        ok_ = false;
};

}  // namespace rangetest
