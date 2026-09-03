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
};

const char* to_string(Role r);

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

// Heltec V3 PRG / BOOT button. Active LOW, external pull-up on the board.
//
// NOT declared in the vendor variant pins_arduino.h (which stops at the LoRa and
// OLED pins), so unlike every value in board_config.h this one is not transcribed
// from the board definition. GPIO 0 is the ESP32-S3 BOOT strapping pin and the
// button wired to it on this board.
//
// CONFIRMED ON HARDWARE 2026-08-31: reset followed by a PRG press inside the window
// selects RESPONDER on both boards, and no press yields INITIATOR. Confirmed
// behaviourally rather than against the schematic, which is the stronger check of
// the two - what matters is that the button reaches this GPIO, and it does.
inline constexpr int kPinPrgButton = 0;

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
//
// NOT on the PRG button. PRG already means RESPONDER and that meaning is the one the
// operator uses untethered, in the field, on the walking board - overloading it with
// a double-press or a hold would put the survey one mistimed thumb away from the
// walk. A survey run instead starts either from the console with 'v', or by pressing
// PRG a second time on a board that is ALREADY showing the role prompt having been
// put into it deliberately - which is to say, it does not, and 'v' is the only
// selector.
//
// That leaves the far-end survey needing a laptop, which is precisely the problem
// survey.h's NVS blob exists to solve: select the survey at the house where the
// laptop is, walk out with the board still running it, press PRG at the far point to
// store, and read it back on return. See the field procedure in HANDOFF.md.
inline constexpr char kSerialSelectSurvey = 'v';

}  // namespace rangetest
