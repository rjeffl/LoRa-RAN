// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "lran/link/rx_arrival.h"

namespace lran {
namespace link {

namespace {

// Unsigned, so a millis() wrap between two reads does not make a flag look ancient.
uint32_t elapsed(uint32_t now_ms, uint32_t since_ms) { return now_ms - since_ms; }

}  // namespace

uint32_t preamble_to_header_ms(const PhyConfig& phy) {
  // Counted in quarter symbols, because the sync interval is 4.25 symbols (spec 15.1):
  // the preamble, 17 for the sync, 32 for the header's 8 symbols, 4 for rounding.
  const uint64_t quarters = static_cast<uint64_t>(phy.preamble_symbols) * 4u + 17u + 32u + 4u;
  // One symbol is 2^SF / BW seconds; bw_khz10 is BW in units of 100 Hz.
  const uint64_t sym_us = ((1ull << phy.sf) * 10000ull) / phy.bw_khz10;
  const uint64_t us     = quarters * sym_us / 4u;
  return static_cast<uint32_t>((us + 999u) / 1000u);
}

uint32_t burst_holdoff_ms(const PhyConfig& phy) { return 2u * preamble_to_header_ms(phy); }

RxArrivalState RxArrival::observe(bool preamble, bool header, uint32_t now_ms) {
  if (header) {
    if (!header_seen_) {
      header_seen_    = true;
      header_seen_ms_ = now_ms;
    }
    return elapsed(now_ms, header_seen_ms_) < frame_ms_ ? RxArrivalState::Arriving
                                                        : RxArrivalState::Stale;
  }
  header_seen_ = false;
  if (preamble) {
    if (!preamble_seen_) {
      preamble_seen_    = true;
      preamble_seen_ms_ = now_ms;
    }
    return elapsed(now_ms, preamble_seen_ms_) < preamble_ms_ ? RxArrivalState::Arriving
                                                             : RxArrivalState::Stale;
  }
  preamble_seen_ = false;
  return RxArrivalState::Idle;
}

bool RxArrival::holding_off(uint32_t now_ms) const {
  return reception_ended_ && elapsed(now_ms, reception_end_ms_) < holdoff_ms_;
}

bool RxArrival::arriving(uint32_t now_ms) const {
  if (header_seen_) return elapsed(now_ms, header_seen_ms_) < frame_ms_;
  if (preamble_seen_) return elapsed(now_ms, preamble_seen_ms_) < preamble_ms_;
  return false;
}

}  // namespace link
}  // namespace lran
