// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Relay pulse timing and input debounce. GL1; GateLink Impl Plan 4.4, 5.2.
//
// Every comparison of two times is a signed difference, so a millisecond counter that wraps
// at 2^32 (49.7 days) moves a deadline by nothing.

#include "gate_io.h"

namespace gatelink {
namespace {

bool reached(uint32_t now_ms, uint32_t at_ms) {
  return static_cast<int32_t>(now_ms - at_ms) >= 0;
}

}  // namespace

PulseResult RelayPulser::start(uint8_t relay, uint32_t width_ms, uint32_t spacing_ms,
                               uint32_t now_ms) {
  if (relay >= kRelayCount) return PulseResult::BadRelay;
  if (width_ms == 0) return PulseResult::BadWidth;
  if (active_) return PulseResult::Busy;
  // R-3.1.2b - the gap runs from the last trailing edge to this leading edge, whichever
  // relay either one drove.
  if (ended_ && !reached(now_ms, end_ms_ + spacing_ms)) return PulseResult::TooSoon;
  active_ = true;
  relay_  = relay;
  end_ms_ = now_ms + width_ms;
  return PulseResult::Started;
}

bool RelayPulser::update(uint32_t now_ms) {
  if (!active_ || !reached(now_ms, end_ms_)) return false;
  active_ = false;
  ended_  = true;
  // The spacing runs from the edge as scheduled, not as observed: a late wake must not
  // also shorten the gap that follows it.
  return true;
}

uint32_t RelayPulser::ms_to_edge(uint32_t now_ms) const {
  if (!active_) return UINT32_MAX;
  if (reached(now_ms, end_ms_)) return 0;
  return end_ms_ - now_ms;
}

bool CommandSequencer::begin(const RelaySequence& seq, uint32_t now_ms) {
  if (state_ != State::Idle) return false;
  relay_[0]      = seq.first;
  relay_[1]      = seq.second;
  step_          = 0;
  not_before_ms_ = now_ms;
  ended_         = false;
  state_         = State::Waiting;
  return true;
}

bool CommandSequencer::service(RelayPulser& pulser, uint32_t width_ms, uint32_t spacing_ms,
                               uint32_t settle_ms, uint32_t now_ms) {
  if (state_ == State::Pulsing) {
    // Nothing else can start a pulse while this one runs, so an empty mask is its edge.
    if (pulser.mask() != 0) return false;
    if (step_ == 1 || relay_[1] == kNoRelay) {
      state_ = State::Idle;
      ended_ = true;
      end_   = SequenceEnd::Done;
      return false;
    }
    step_          = 1;
    not_before_ms_ = now_ms + settle_ms;
    state_         = State::Waiting;
  }
  if (state_ != State::Waiting || !reached(now_ms, not_before_ms_)) return false;

  switch (pulser.start(relay_[step_], width_ms, spacing_ms, now_ms)) {
    case PulseResult::Started:
      state_ = State::Pulsing;
      return true;
    case PulseResult::Busy:
    case PulseResult::TooSoon:
      return false;  // a later pass
    case PulseResult::BadRelay:
    case PulseResult::BadWidth:
      break;
  }
  state_ = State::Idle;
  ended_ = true;
  end_   = SequenceEnd::Refused;
  return false;
}

bool CommandSequencer::take_end(SequenceEnd* end) {
  if (!ended_) return false;
  ended_ = false;
  *end   = end_;
  return true;
}

uint8_t Debouncer::update(uint8_t raw, uint8_t samples) {
  if (!primed_) {
    primed_ = true;
    stable_ = raw;
    return stable_;
  }
  if (samples == 0) samples = 1;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    const uint8_t m = static_cast<uint8_t>(1u << bit);
    if ((raw & m) == (stable_ & m)) {
      count_[bit] = 0;
      continue;
    }
    if (++count_[bit] >= samples) {
      stable_ ^= m;
      count_[bit] = 0;
    }
  }
  return stable_;
}

}  // namespace gatelink
