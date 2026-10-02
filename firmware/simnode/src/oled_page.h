// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// What the simnode's OLED says, built as text. Task BF-9; Impl Plan 10.6 (ui.cpp: identity
// table, last frame, fault armed) and rule 2.
//
// ARDUINO-FREE, so the page is host-tested. ui.cpp draws rows; everything that decides what
// the rows ARE - which fault is armed, how much of it is left, when a name must be cut - is
// here. The bridge's status page (BF-14) is the pattern, and the range test's lesson behind
// it holds: a character budget checked at a desk is cheaper than a truncation found on a
// panel.
//
// WHY ARMED FAULTS ARE DRAWN INVERTED. 10.6 rule 2: a simnode left in a fault mode looks
// exactly like a broken bridge. The row that says so must be the thing the eye lands on, not
// one line among five.

#pragma once

#include <cstddef>
#include <cstdint>

#include "bms_emu.h"
#include "fault.h"
#include "identity.h"
#include "node.h"

namespace simnode {

// Row 0 is the last frame; rows 1-4 are the identity slots (kMaxIdentities).
inline constexpr size_t kPageRows = 1 + kMaxIdentities;

struct PageRow {
  char left[24]  = {0};
  char right[8]  = {0};  // right-aligned: an age, a count, or "off"
  bool invert    = false;
};

struct PageLines {
  PageRow rows[kPageRows];
};

// ArialMT_Plain_10 on a 128 px panel fits about 21 mixed characters - the bridge's measured
// budget (status_page.h), same panel, same driver version. A row's left and right halves
// share it, with one space between.
inline constexpr size_t kRowBudget = 21;

struct IdentityView {
  bool         used       = false;
  lran::NodeId id         = 0;
  Role         role       = Role::Range;
  bool         enabled    = true;
  const char*  fault      = nullptr;  // exact console token, or nullptr when nothing is armed
  uint16_t     fault_left = 0;        // injections still to fire, or answers still to withhold
};

// The BMS emulator (GateLink task L7). It has no identity, so it gets its own view.
struct BmsView {
  bool        on         = false;
  bool        linked     = false;  // a central is connected and has sent the handshake
  const char* fault      = nullptr;  // exact console token, or nullptr when nothing is armed
  uint32_t    fault_left = 0;
};

struct PageSnapshot {
  bool         radio_up = false;
  uint32_t     now_ms   = 0;
  LastRx       last;
  IdentityView ids[kMaxIdentities];
  BmsView      bms;
};

// Reads the table, the injector and the node. `silent` lives on the identity rather than in
// the injector (fault.h), so both places are read.
PageSnapshot take_snapshot(const IdentityTable& ids, const FaultInjector& faults, const Node& node,
                           bool radio_up, uint32_t now_ms);

// Fills `s->bms` from the emulator. A separate call, because the emulator is the board's.
void add_bms(PageSnapshot* s, bool on, const BmsEmu& emu);

// The BMS row takes the bottom row while the emulator is on. With all four identity slots
// in use it displaces the fourth only while a BMS fault is armed, so an armed fault is
// always on the panel (Impl Plan 10.6 rule 2).
PageLines build_page(const PageSnapshot& s);

// Four characters at most, so the last-frame row fits with an RSSI and an age.
const char* short_type_name(lran::MsgType t);

// "9s", "59m", "99h", ">99h". Ages keep moving, so a frozen panel is distinguishable from a
// quiet channel.
void format_age(uint32_t ms, char* out, size_t cap);

}  // namespace simnode
