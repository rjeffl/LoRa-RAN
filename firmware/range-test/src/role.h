// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R1 - role selection.
//
// One binary, two roles. The role is shown on the OLED at startup and is NOT
// persisted: a power cycle re-asks (task R1).

#pragma once

#include <cstdint>

namespace rangetest {

enum class Role : uint8_t {
  Initiator,  // fixed end. Tethered to the field laptop, drives the sweep, owns the
              // CSV. Sits at the house or the gate and does not move.
  Responder,  // walking end. Untethered, echoes probes, shows live link quality.
  Survey,     // R8 / M20 - ambient RSSI scan. LISTENS ONLY; never transmits.

  // R9 / W9 - the protocol bench, spec 6.6. Real PING frames through the real codec,
  // which is the one thing the sweep deliberately does not do: the sweep measures the
  // RADIO LINK with its own raw frame, W9 measures the PROTOCOL.
  W9Initiator,
  W9Responder,
};

const char* to_string(Role r);

// True for the two R9 modes. They share the radio with the sweep and nothing else,
// so several places have to exclude them together rather than one at a time.
constexpr bool w9_role(Role r) {
  return r == Role::W9Initiator || r == Role::W9Responder;
}

// ---------------------------------------------------------------------------
// A DEVIATION FROM THE TASK TEXT, AND WHY.
//
// R1 says the role is "selected by holding the PRG button at boot". On this board
// that cannot work as written. The Heltec V3's PRG button is the ESP32-S3 BOOT
// strapping pin (GPIO 0), and GPIO 0 held low THROUGH RESET puts the chip into the
// ROM serial downloader - the application never runs, so it never gets to read the
// button. The instruction as written selects "download mode", not "responder".
//
// Implemented instead as a selection WINDOW immediately after the application
// starts: the OLED shows a countdown, and pressing PRG at any point during the
// window selects RESPONDER. No press means INITIATOR.
//
// This keeps every property R1 actually asked for - one binary, two roles, no
// persistence, role on the OLED, no laptop needed at the walking end - and it is
// arguably better for the field: the walking unit is selected by a deliberate press
// with the display confirming it, rather than by a hold whose effect is invisible
// until the radio does or does not start.
//
// INITIATOR is the no-press default on purpose. It is the tethered end, so if the
// default is ever wrong the operator is sitting at the laptop that can see it.
// ---------------------------------------------------------------------------

// PASS 2: THE BUTTON PIN MOVED TO `BoardUiConfig::role_button` (board_config.h).
//
// It was `kPinPrgButton = 0` here while there was one board. The XIAO deliberately
// does NOT use GPIO 0 - the reasoning above is exactly why. On the Heltec the strapping
// pin is survivable because the selection happens in a window AFTER boot, but there is
// no reason to point a second board at the download-mode strap when it has a plain GPIO
// (21, on top of the Wio) available.
//
// Active LOW with a pull-up on BOTH boards, so nothing in this file or in the window
// logic below needs a polarity concept. A third board that inverts it gets a field in
// BoardUiConfig and an entry in the log - not an #ifdef at the read site.

inline constexpr uint32_t kRoleSelectWindowMs = 3000;

// A SECOND selector, for the case the button cannot serve: both boards tethered to
// one build machine during bench bring-up (R2's acceptance criterion). Sending 'i'
// or 'r' on the serial console during the same window picks the role directly.
//
// Not a replacement for the button - the walking end is untethered by definition and
// PRG is the only selector it has. This exists because R2's gate is worked with both
// boards on a desk, and because R1 itself anticipates the field machine being
// connected to the responder "if the need arises". Bridge Impl Plan 11.2's original
// design selected modes by serial keypress, so the mechanism is not alien to it.
//
// Deliberately inside the same window rather than a persistent console command: the
// role must still not be persisted, and a power cycle must still re-ask (R1).
inline constexpr char kSerialSelectInitiator = 'i';
inline constexpr char kSerialSelectResponder = 'r';

// R8 - the third mode, on the same binary per the task text.
inline constexpr char kSerialSelectSurvey = 'v';

// R9 - the two W9 modes, SERIAL ONLY, and deliberately so.
//
// The survey needed a button because the walking board is untethered by definition
// (see below). W9 is the opposite case: the task puts it on the bench "while the
// boards are out", both ends are reachable from a console, and the run's output is a
// per-fragment fault report that only means anything on a console anyway.
//
// Adding a fourth and fifth PRG gesture would put the protocol bench one mistimed
// thumb away from the walk, on a board whose default role TRANSMITS - which is the
// same class of mistake the survey's hold gesture was introduced to fix. A mode that
// does not need a gesture does not get one.
inline constexpr char kSerialSelectW9Initiator = 'w';
inline constexpr char kSerialSelectW9Responder = 'x';

// PRG ALSO SELECTS SURVEY, BY HOLDING IT. A tap is RESPONDER; a hold past
// kPrgSurveyHoldMs is SURVEY.
//
// THIS WAS ORIGINALLY SERIAL-ONLY, AND THAT WAS A FIELD-BLOCKING BUG.
//
// The reasoning was that PRG already means RESPONDER, so overloading it puts the
// survey one mistimed thumb away from the walk; select the survey at the house, where
// the laptop is, and walk out with the board still running it.
//
// That silently assumed the board stays powered. It does not: this Heltec has no
// battery fitted, so moving from the laptop to a power bank is a POWER CYCLE, and the
// role is deliberately not persisted (R1 - a power cycle re-asks). The board therefore
// came back up as INITIATOR, the no-press default - which in this firmware is the mode
// that TRANSMITS. The survey campaign as documented could not be run at all, and its
// failure mode was a board quietly putting power on the air at every site.
//
// A hold is the right gesture rather than a persisted flag: it keeps R1's "not
// persisted, a power cycle re-asks" intact, it needs no laptop, and it cannot be hit
// by accident during the walk because the walk's own gesture is a tap on a board that
// is not in the role window. The OLED names the role that will be chosen while the
// button is still down, so the operator sees SURVEY before releasing.
inline constexpr uint32_t kPrgSurveyHoldMs = 1500;

}  // namespace rangetest
