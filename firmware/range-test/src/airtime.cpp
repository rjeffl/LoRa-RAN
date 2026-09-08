// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "airtime.h"

namespace rangetest {
namespace {

// Symbol period in nanoseconds. 2^SF / BW.
//
// Nanoseconds because SF12 at 125 kHz is 32.768 ms and SF7 is 1.024 ms; microseconds
// would round the short ones and there is no reason to accept that when a uint64
// intermediate is free.
uint64_t symbol_period_ns(const LoraParams& p) {
  // bw_khz10 is tenths of a kHz, so bandwidth in Hz is bw_khz10 * 100.
  const uint64_t bw_hz = static_cast<uint64_t>(p.bw_khz10) * 100ULL;
  if (bw_hz == 0) return 0;
  return (static_cast<uint64_t>(1ULL << p.sf) * 1000000000ULL) / bw_hz;
}

}  // namespace

bool low_data_rate_optimize(const LoraParams& p) {
  // >= 16 ms. The SX126x datasheet condition, and RadioLib's autoLDRO uses the same
  // threshold - see the header for why agreeing with the driver is not optional.
  return symbol_period_ns(p) >= 16000000ULL;
}

uint32_t airtime_us(const LoraParams& p, uint16_t payload_len) {
  if (p.sf < 5 || p.sf > 12) return 0;
  if (p.cr_denom < 5 || p.cr_denom > 8) return 0;

  const uint64_t t_sym_ns = symbol_period_ns(p);
  if (t_sym_ns == 0) return 0;

  // spec 15.1's correction: the preamble term is (n_pre + 4.25) symbols, not n_pre.
  // v0.2 of the specification omitted the 4.25-symbol sync interval and was ~4% low
  // as a result. Written as quarters to keep the 0.25 exact in integer arithmetic.
  const uint64_t preamble_quarter_syms =
      static_cast<uint64_t>(p.preamble_symbols) * 4ULL + 17ULL;  // (n + 4.25) * 4
  const uint64_t t_preamble_ns = (preamble_quarter_syms * t_sym_ns) / 4ULL;

  const int de = low_data_rate_optimize(p) ? 1 : 0;
  const int ih = p.explicit_header ? 0 : 1;
  const int crc = p.crc ? 1 : 0;

  // ceil( (8*PL - 4*SF + 28 + 16*CRC - 20*IH) / (4*(SF - 2*DE)) )
  const int32_t num = 8 * static_cast<int32_t>(payload_len)
                    - 4 * static_cast<int32_t>(p.sf)
                    + 28 + 16 * crc - 20 * ih;
  const int32_t den = 4 * (static_cast<int32_t>(p.sf) - 2 * de);
  if (den <= 0) return 0;

  int32_t blocks = 0;
  if (num > 0) blocks = (num + den - 1) / den;  // ceil, num > 0 so this is exact

  const int32_t cr = static_cast<int32_t>(p.cr_denom) - 4;  // 4/5 -> 1 ... 4/8 -> 4
  int32_t payload_syms = blocks * (cr + 4);
  if (payload_syms < 0) payload_syms = 0;

  // The fixed 8 symbols are outside the max(...,0) in the Semtech formula.
  const uint64_t n_payload_syms = 8ULL + static_cast<uint64_t>(payload_syms);
  const uint64_t t_payload_ns   = n_payload_syms * t_sym_ns;

  const uint64_t total_ns = t_preamble_ns + t_payload_ns;
  return static_cast<uint32_t>((total_ns + 500ULL) / 1000ULL);  // ns -> us, rounded
}

uint32_t airtime_ms(const LoraParams& p, uint16_t payload_len) {
  const uint32_t us = airtime_us(p, payload_len);
  return (us + 500U) / 1000U;
}

}  // namespace rangetest
