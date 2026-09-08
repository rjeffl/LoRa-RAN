// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Serial-number arithmetic. Spec 5.4, 10.5.

#pragma once

#include "lran/types.h"

namespace lran {

// RFC 1982 comparison over the 16-bit sequence space. NEVER use a plain > on a Seq.
//
// spec 10.5 - `seq` wraps modulo 2^16. A plain comparison presents in the field as
// "the gate stopped responding to commands after about two months and a reboot fixed
// it", because every subsequent command sits below the high-water mark.
//
// RFC 1982 leaves the exactly-half-space case (|a - b| == 0x8000) undefined. This
// returns false there: an ambiguous distance is not evidence of newness, and on a
// link where seq resets to 1 on every reboot (spec 10.2) the case is unreachable in
// practice.
inline bool seq_newer(Seq a, Seq b) {
  const uint16_t d = static_cast<uint16_t>(a - b);
  return d != 0 && d < 0x8000;
}

inline Seq seq_next(Seq s) { return static_cast<Seq>(s + 1); }

}  // namespace lran
