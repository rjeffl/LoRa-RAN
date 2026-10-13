// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "gate_state.h"

namespace gatelink {

using lran::GateState;
using lran::HoldSource;
using lran::MovementCause;

namespace {

// spec 8.3 - the four states of R-3.1.4a, and FAULT while the hard-shutdown latch is asserted.
GateState observe(uint8_t inputs) {
  if ((inputs & kInAlarm) != 0) return GateState::Fault;
  const bool open   = (inputs & kInOpen) != 0;
  const bool moving = (inputs & kInMoving) != 0;
  if (!open) return moving ? GateState::Moving : GateState::Closed;
  return moving ? GateState::OpenCountdown : GateState::OpenHeld;
}

}  // namespace

void GateTracker::note_pulse(uint8_t relay, uint32_t now_ms) {
  if (relay == kRelayOpenLock || relay == kRelayOpen) {
    open_pulse_seen_ = true;
    open_pulse_ms_   = now_ms;
  }
  if (relay == kRelayOpenLock) {
    lran_claim_       = true;
    lran_claim_moved_ = false;
    lran_claim_ms_    = now_ms;
  } else if (relay == kRelayUnlock) {
    lran_claim_ = false;
  }
}

// Impl Plan 6.3 - in the table's order. A cause that cannot be named yet waits for the gate
// to settle open, where MOVING tells a hold from a momentary open.
void GateTracker::begin_opening(uint32_t now, const GateParams& p) {
  cause_pending_ = false;
  if (exit_seen_ && within(now, exit_ms_, p.cause_window_ms)) {
    cause_ = MovementCause::ExitWand;
  } else if (open_pulse_seen_ && within(now, open_pulse_ms_, p.cause_window_ms)) {
    cause_ = MovementCause::LranCommand;
  } else if (fire_seen_ && within(now, fire_ms_, p.cause_window_ms)) {
    cause_ = MovementCause::KeypadFire;
  } else {
    cause_         = MovementCause::Unknown;
    cause_pending_ = true;
  }
}

uint8_t GateTracker::update(uint8_t inputs, uint32_t now_ms, const GateParams& p) {
  const uint8_t rising = primed_ ? static_cast<uint8_t>(inputs & ~prev_inputs_) : 0;
  prev_inputs_         = inputs;
  primed_              = true;
  if ((rising & kInExit) != 0) {
    exit_seen_ = true;
    exit_ms_   = now_ms;
  }
  if ((rising & kInFire) != 0) {
    fire_seen_  = true;
    fire_ms_    = now_ms;
    fire_claim_ = true;
  }

  const GateState seen = observe(inputs);
  if (seen != observed_) {
    observed_ = seen;
    if (lran_claim_) lran_claim_moved_ = true;
  }
  if (lran_claim_ && !lran_claim_moved_ && !within(now_ms, lran_claim_ms_, p.cause_window_ms)) {
    lran_claim_ = false;
  }

  // R-3.1.4b - 1/0 is not a hold until it has held for hold_confirm_ms. Until then the state
  // stays what it was, which absorbs the brief 1/1 -> 1/0 -> 1/1 at the start of a close.
  GateState next = seen;
  if (seen == GateState::OpenHeld) {
    if (!hold_candidate_) {
      hold_candidate_ = true;
      hold_since_ms_  = now_ms;
    }
    if (now_ms - hold_since_ms_ < p.hold_confirm_ms) next = state_;
  } else {
    hold_candidate_ = false;
  }

  uint8_t changed = 0;
  if (next != state_) {
    const GateState prev = state_;
    state_               = next;
    changed |= kChangedState;
    if (prev == GateState::Closed) begin_opening(now_ms, p);
    if (next == GateState::OpenHeld) before_hold_ = prev;
    if (cause_pending_ && (next == GateState::OpenHeld || next == GateState::OpenCountdown)) {
      cause_pending_ = false;
      cause_         = next == GateState::OpenHeld ? MovementCause::ManualHold
                                                   : MovementCause::ExternalMomentary;
    }
    if (next == GateState::Closed) {
      cause_pending_ = false;
      lran_claim_    = false;
      fire_claim_    = false;
    }
  }

  const bool held_now = state_ == GateState::OpenHeld;
  if (held_now != held_) {
    held_ = held_now;
    changed |= kChangedHold;
    if (!held_) {
      source_ = HoldSource::None;
    } else if (before_hold_ == GateState::Unknown) {
      source_ = HoldSource::Unknown;  // there at boot; Impl Plan 6.2
    } else if (lran_claim_) {
      source_ = HoldSource::Lran;
    } else if (fire_claim_) {
      source_ = HoldSource::KeypadFire;
    } else {
      source_ = HoldSource::Manual;
    }
    lran_claim_ = false;
  }
  return changed;
}

}  // namespace gatelink
