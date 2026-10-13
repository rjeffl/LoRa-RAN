// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL3 - gate state, hold state and its source, and movement cause, derived from the
// debounced inputs (PRD R-3.1.4a-c; GateLink Impl Plan 6.2, 6.3; spec 8.3-8.5).
//
// ARDUINO-FREE, time passed in. app_task is the only caller.
//
// OBSERVED, NOT REMEMBERED. The state is a function of IN1 and IN2 alone. What the tracker
// remembers is only what attributes a change: GateLink's own K1 and K3 pulses, the last
// EXIT and FIRE edges, and the state before a hold. A reboot loses those, which is why a hold
// observed at boot reads UNKNOWN rather than a guess (Impl Plan 6.2).

#pragma once

#include <cstdint>

#include "lran/types.h"

namespace gatelink {

// spec 7.2.1 - input_bits, as io_task's debounced inputs carry them.
inline constexpr uint8_t kInOpen   = 1u << 0;  // IN1, OUT1 programmed OPEN
inline constexpr uint8_t kInMoving = 1u << 1;  // IN2, OUT2 programmed MOVING
inline constexpr uint8_t kInSafety = 1u << 2;  // IN3
inline constexpr uint8_t kInExit   = 1u << 3;  // IN4
inline constexpr uint8_t kInFire   = 1u << 4;  // IN5, a placeholder under D77
inline constexpr uint8_t kInAlarm  = 1u << 5;  // IN6, a placeholder under D77

// The relays, as io_task numbers them (Impl Plan 3.1).
inline constexpr uint8_t kRelayOpenLock = 0;  // K1
inline constexpr uint8_t kRelayUnlock   = 1;  // K2
inline constexpr uint8_t kRelayOpen     = 2;  // K3

struct GateParams {
  uint32_t hold_confirm_ms = 2000;   // lran-config 0x1012, PRD R-3.1.4b
  uint32_t cause_window_ms = 10000;  // lran-config 0x1023, Impl Plan 6.3
};

// What update() reports changed, so app_task knows which STATUS to send (spec 8.7).
inline constexpr uint8_t kChangedState = 1u << 0;
inline constexpr uint8_t kChangedHold  = 1u << 1;

class GateTracker {
 public:
  // One debounced read. Returns kChanged* bits.
  uint8_t update(uint8_t inputs, uint32_t now_ms, const GateParams& p);

  // io_task started a pulse on `relay` (0-3 for K1-K4).
  void note_pulse(uint8_t relay, uint32_t now_ms);

  lran::GateState     state() const { return state_; }
  bool                held() const { return held_; }
  lran::HoldSource    hold_source() const { return source_; }
  lran::MovementCause cause() const { return cause_; }
  // spec 7.2.1's `hold` byte: bit 0 held_open, bits 3:1 hold_source.
  uint8_t hold_byte() const {
    return static_cast<uint8_t>((held_ ? 1u : 0u) | (static_cast<uint8_t>(source_) << 1));
  }

 private:
  static bool within(uint32_t now, uint32_t then, uint32_t window) { return now - then <= window; }
  void        begin_opening(uint32_t now, const GateParams& p);

  bool            primed_ = false;
  uint8_t         prev_inputs_ = 0;
  lran::GateState state_    = lran::GateState::Unknown;
  lran::GateState observed_ = lran::GateState::Unknown;  // before the hold confirmation
  bool            hold_candidate_ = false;
  uint32_t        hold_since_ms_  = 0;

  bool             held_   = false;
  lran::HoldSource source_ = lran::HoldSource::None;
  // The derived state the gate left for OPEN_HELD. UNKNOWN means the hold was there at boot.
  lran::GateState  before_hold_ = lran::GateState::Unknown;

  lran::MovementCause cause_         = lran::MovementCause::Unknown;
  bool                cause_pending_ = false;  // opened with no attributable trigger yet

  // Impl Plan 6.3 - the triggers an opening is attributed to.
  bool     exit_seen_  = false;
  uint32_t exit_ms_    = 0;
  bool     fire_seen_  = false;
  uint32_t fire_ms_    = 0;
  bool     open_pulse_seen_ = false;  // K1 or K3
  uint32_t open_pulse_ms_   = 0;

  // Impl Plan 6.2 - a K1 pulse claims the next hold. The claim lapses when the gate closes,
  // when K2 releases, or when nothing moves within cause_window_ms of the pulse: a K1 that
  // moved nothing must not claim a manual hold made later.
  bool     lran_claim_       = false;
  bool     lran_claim_moved_ = false;
  uint32_t lran_claim_ms_    = 0;
  bool     fire_claim_       = false;  // a FIRE edge since the gate last closed
};

}  // namespace gatelink
