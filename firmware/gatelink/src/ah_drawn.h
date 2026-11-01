// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// PRD R-6.4c's Ah drawn since charging was last active, sent as ah_drawn_dah in schema 0x13
// (spec 7.2.10, D82).
//
// ARDUINO-FREE, so the native suite drives it. app_task owns one and feeds it each pass.
//
// NO CURRENT IS INTEGRATED. The count is the fall in the BMS's own remaining capacity, in
// its 0.1 Ah steps, because neither current GateLink reads can carry its 44 mA standing draw
// (R-6.4c). That the capacity falls under such a draw is unmeasured; R-6.4c names the check.
//
// THE REFERENCE LIVES IN RAM, so a reset reads UINT16_MAX until charging is next active with
// a BMS read. Restarting from 0 would understate the reserve used (spec 7.2.10).

#pragma once

#include <cstdint>

#include "bms_link.h"
#include "ved_link.h"

namespace gatelink {

class AhDrawn {
 public:
  // One pass. Returns ah_drawn_dah: 0 while charging is active, UINT16_MAX with no reference,
  // otherwise the fall since the reference, floored at 0 and saturating at 0xFFFE.
  uint16_t update(const MpptSnapshot& m, const BmsSnapshot& b, uint32_t now_ms,
                  uint32_t bms_poll_s);

 private:
  bool     have_ref_  = false;
  uint32_t ref_dah_   = 0;  // remaining capacity at the last read while charging was active
  uint32_t last_dah_  = 0;  // remaining capacity at the latest read
  bool     seen_read_ = false;
  uint32_t read_ms_   = 0;  // the read last_dah_ came from
};

}  // namespace gatelink
