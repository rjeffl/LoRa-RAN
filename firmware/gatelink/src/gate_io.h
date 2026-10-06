// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Relay pulse timing and input debounce, with time passed in. GL1; GateLink Impl Plan 4.4,
// 5.2, 5.3.
//
// ARDUINO-FREE, so the native suite can drive time. io_task owns one RelayPulser and one
// Debouncer and is their only caller; nothing here locks.

#pragma once

#include <cstdint>

namespace gatelink {

// K1-K4 (Impl Plan 3.1), as bits 0-3 of the relay mask.
inline constexpr uint8_t kRelayCount = 4;

enum class PulseResult : uint8_t {
  Started,
  BadRelay,     // not K1-K4
  BadWidth,     // zero. The Store holds relay_pulse_ms to its range; this catches the rest
  Busy,         // a pulse is in progress
  TooSoon,      // inside relay_min_spacing_ms of the last trailing edge (R-3.1.2b)
};

// One pulse at a time. A gate command that needs two relays, such as K2 then K4 on an
// immediate close, is a sequence the caller spaces (unlock_settle_ms), not two at once.
class RelayPulser {
 public:
  // width_ms and spacing_ms come from the Store at the call, so a changed parameter takes
  // effect on the next pulse (root rule 8).
  PulseResult start(uint8_t relay, uint32_t width_ms, uint32_t spacing_ms, uint32_t now_ms);

  // Ends the pulse once its width has passed. Returns true when the mask changed, which is
  // the caller's cue to write the expander.
  bool update(uint32_t now_ms);

  // The relays that should be energized now. Bit n is K(n+1).
  uint8_t mask() const { return active_ ? static_cast<uint8_t>(1u << relay_) : 0; }

  // Milliseconds until the trailing edge, or UINT32_MAX with no pulse in progress. io_task
  // wakes at the edge rather than at its next poll: at input_poll_ms = 100 a poll-timed edge
  // would be up to 100 ms late, and GL1 asks for +-10 ms.
  uint32_t ms_to_edge(uint32_t now_ms) const;

 private:
  bool     active_ = false;
  bool     ended_  = false;  // a pulse has ended, so spacing applies to the next
  uint8_t  relay_  = 0;
  uint32_t end_ms_ = 0;      // the trailing edge, scheduled or past
};

// Eight inputs, as one mask. A bit changes only after `samples` consecutive raw reads
// agree on its new value (input_debounce_samples; Impl Plan 4.4).
class Debouncer {
 public:
  // The first read is taken as stable: at boot there is no earlier state to debounce from.
  uint8_t update(uint8_t raw, uint8_t samples);
  uint8_t stable() const { return stable_; }

 private:
  bool    primed_ = false;
  uint8_t stable_ = 0;
  uint8_t count_[8] = {};
};

}  // namespace gatelink
