// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// R4 - LoRa time-on-air.
//
// The sweep needs this for two things: telling the operator how long a position will
// take before they stand still for it, and sizing the echo timeout from the actual
// round trip rather than from a guess that is wrong by 20x between SF7 and SF12.
//
// Arduino-free, and host tested against the twenty-one figures in spec 15.1. That
// table carries W7 - "recompute once D1 fixes SF/BW/CR" - so an implementation that
// reproduces it exactly is the instrument W7 will regenerate it with, not just a
// convenience for the sweep.

#pragma once

#include <cstdint>

#include "phy_params.h"

namespace rangetest {

struct LoraParams {
  uint8_t  sf              = 7;
  uint16_t bw_khz10        = kBandwidthKhz10;
  uint8_t  cr_denom        = 5;   // 4/N, N in 5..8
  uint16_t preamble_symbols = kPreambleSymbols;
  bool     explicit_header = kExplicitHeader;
  bool     crc             = kCrcEnabled;
};

// Time on air in microseconds for a payload of `payload_len` bytes.
//
// The Semtech formula. Returns 0 for parameters outside the SX1262's range rather
// than a plausible-looking number computed from nonsense.
uint32_t airtime_us(const LoraParams& p, uint16_t payload_len);

// Rounded to the nearest millisecond, which is how spec 15.1 tabulates it.
uint32_t airtime_ms(const LoraParams& p, uint16_t payload_len);

// True when the low data rate optimization is engaged.
//
// Not a free choice: the SX1262 requires LDRO when the symbol period is long, and
// RadioLib's default autoLDRO applies the same >= 16 ms rule. If this disagreed with
// the driver, every SF11/SF12 airtime figure would be wrong - and those are exactly
// the ones long enough for the error to matter.
bool low_data_rate_optimize(const LoraParams& p);

}  // namespace rangetest
