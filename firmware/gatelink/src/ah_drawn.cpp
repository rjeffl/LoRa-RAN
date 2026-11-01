// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// PRD R-6.4c; see ah_drawn.h.

#include "ah_drawn.h"

#include "lran/types.h"

namespace gatelink {
namespace {

// Past this many polls without a read, a charging period has outrun the reference. Fewer
// would drop it on one missed window; a dropped reference costs the whole next night.
constexpr uint32_t kRefStalePolls = 2;
constexpr uint32_t kSaturated     = 0xFFFE;  // spec 7.2.10

// R-6.4c: bulk, absorption or float, and VE.Direct battery current above 0.
bool charging_active(const MpptSnapshot& m) {
  return (m.mppt_flags & kMpptFlagStale) == 0 &&
         (m.charge_state == kCsBulk || m.charge_state == kCsAbsorption ||
          m.charge_state == kCsFloat) &&
         m.batt_ma != lran::kI16NotAvailable && m.batt_ma > 0;
}

}  // namespace

uint16_t AhDrawn::update(const MpptSnapshot& m, const BmsSnapshot& b, uint32_t now_ms,
                         uint32_t bms_poll_s) {
  const bool active = charging_active(m);
  const bool fresh  = b.have && (!seen_read_ || b.read_ms != read_ms_);
  if (fresh) {
    seen_read_ = true;
    read_ms_   = b.read_ms;
    last_dah_  = b.data.remaining_dAh;
    if (active) {
      have_ref_ = true;
      ref_dah_  = last_dah_;
    }
  }
  // A cloud's brief charge between two polls must not lose the night's reference, but hours
  // of charging the BMS never saw make it stale.
  if (active && have_ref_ && seen_read_ &&
      now_ms - read_ms_ > kRefStalePolls * bms_poll_s * 1000 + 1) {
    have_ref_ = false;
  }

  if (active) return 0;
  if (!have_ref_) return lran::kU16NotAvailable;
  if (last_dah_ >= ref_dah_) return 0;
  const uint32_t drawn = ref_dah_ - last_dah_;
  return static_cast<uint16_t>(drawn > kSaturated ? kSaturated : drawn);
}

}  // namespace gatelink
