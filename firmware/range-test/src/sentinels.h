// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// spec 4.6 / repo rule 6 - sentinels, never zero, for "not available".
//
// Its own header because more than one thing here needs it and neither owns it: the
// bench frame carries the sentinel on the wire, and the sweep's statistics return it
// for a series with no samples. Putting it in either would make the other depend on a
// module it has no business knowing about.
//
// Restated locally rather than taken from lran/types.h because branches 2 and 3 link
// no /lib/ (task R4 - the sweep uses raw frames). The VALUE must match spec 4.6, and
// does; only the declaration is duplicated.

#pragma once

#include <cstdint>

namespace rangetest {

// A consumer must be able to tell "0.0 dB" from "no reading".
inline constexpr int16_t  kI16NotAvailable = INT16_MIN;
inline constexpr uint16_t kU16NotAvailable = UINT16_MAX;

}  // namespace rangetest
