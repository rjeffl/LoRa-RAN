// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// spec 12.3 media access. Task BF-16; see media_access.h.

#include "media_access.h"

namespace bridge {

void MediaAccess::start(uint32_t now_ms) {
  state_         = State::Waiting;
  wait_start_ms_ = now_ms;
  wait_ms_       = 0;
  backoffs_      = 0;
  forced_        = false;
}

TxStep MediaAccess::step(uint32_t now_ms) const {
  switch (state_) {
    case State::Idle:
      return TxStep::Idle;
    case State::Ready:
      return TxStep::Transmit;
    case State::Waiting:
      return static_cast<uint32_t>(now_ms - wait_start_ms_) >= wait_ms_ ? TxStep::Cad
                                                                      : TxStep::Wait;
  }
  return TxStep::Idle;
}

TxStep MediaAccess::on_cad(CadResult result, uint32_t now_ms, uint32_t random_value,
                           lran::Counters* counters) {
  if (state_ == State::Idle) {
    return TxStep::Idle;
  }

  if (result == CadResult::Free) {
    state_ = State::Ready;
    return TxStep::Transmit;
  }

  if (backoffs_ >= cfg_.cad_retries) {
    // spec 12.3 - "then transmit regardless". Not a backoff, so not counted as one.
    forced_ = true;
    state_  = State::Ready;
    return TxStep::Transmit;
  }

  ++backoffs_;
  if (result == CadResult::Busy) {
    if (counters != nullptr) ++counters->cad_backoffs;
  } else {
    ++cad_errors_;
  }

  wait_ms_       = cfg_.backoff_max_ms == 0 ? 0 : random_value % cfg_.backoff_max_ms;
  wait_start_ms_ = now_ms;
  state_         = State::Waiting;
  return TxStep::Wait;
}

void MediaAccess::finish() { state_ = State::Idle; }

}  // namespace bridge
