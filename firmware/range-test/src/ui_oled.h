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

  // R1/R8 - drawn WHILE PRG IS HELD during the selection window, naming the role a
  // release would choose right now. The walking board reaches SURVEY only through this
  // gesture (role.h), and a hold whose effect is invisible until release is how an
  // operator ends up at a site with a board in the wrong mode.
  void show_role_hold(uint32_t held_ms, bool survey);

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

  // R6 - nothing heard for a while. A display frozen on its last good reading makes
  // "walked out of range" and "the board has crashed" look identical, and on a walk
  // that is the difference between carrying on and turning back. The age is what
  // separates them: it keeps counting, so the display is visibly alive.
  void show_stale(Role r, uint16_t position_id, uint32_t silent_ms,
                  float last_rssi_dbm, bool ever_heard);

  // R8 - the ambient survey. Different quantities entirely: there is no far end, no
  // position and no link, so show_link() has nothing to say here.
  //
  // The operator standing at the far point with no laptop needs exactly three things:
  // that it is still running (the pass count, which keeps climbing), WHICH SITE the
  // run will be stored under, and whether the last PRG press actually stored it.
  // Getting the site wrong is the failure that survives the walk home: the numbers
  // are right and they are filed under the wrong place on the property.
  void show_survey(const char* site_name, uint32_t passes, uint32_t freq_hz_now,
                   uint32_t loudest_freq_hz, int16_t loudest_peak_dbm10,
                   bool saved);

  // R11 - HELD, the phase in which the survey is deliberately measuring nothing.
  //
  // Inverted bar for the same reason show_sweep_done has one: the operator is glancing
  // at a hand-shaded panel in sunlight, and "walking, not measuring" versus "measuring"
  // must survive a glance that reads no words at all. Getting this wrong in the
  // scanning direction contaminates the run; getting it wrong in the held direction
  // wastes a five-minute dwell that measured nothing.
  //
  // The site name is the site the NEXT dwell will be filed under, and the counter says
  // how far through the campaign the operator is - the two things worth knowing while
  // walking with no laptop.
  void show_survey_held(const char* site_name, size_t site_index, size_t site_count,
                        bool saved);

  // R5/R10 - THE WALKING OPERATOR'S GO SIGNAL.
  //
  // Shown on the RESPONDER when the initiator's armed beacon says the sweep for this
  // position has finished. The operator is several hundred feet from the initiator's
  // console and its OLED; without this they are counting minutes and guessing, and a
  // guess that is early puts half a sweep at one position under the label of another.
  //
  // Deliberately unlike show_link: same panel, completely different picture, so it
  // reads at a glance in the sun rather than needing a number to be compared.
  void show_sweep_done(uint16_t position_id, float last_rssi_dbm);

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
