// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "lran/command_gate.h"

#include "lran/seq.h"

namespace lran {

void CommandGate::set_cache_depth(uint8_t n) {
  if (n < 1) n = 1;
  if (n > kDedupCacheCapacity) n = kDedupCacheCapacity;
  depth_ = n;
  while (count_ > depth_) evict_oldest();
}

GateResult CommandGate::check(Seq seq) {
  // spec 9.4 step 4 - the dedup cache first.
  const int slot = find(seq);
  if (slot >= 0) {
    if (in_flight_ & (1u << slot)) {
      // spec 10.4 (v0.11) - no result exists to resend. Answering REJECTED_SEQ here
      // would have the bridge tell Home Assistant "rejected" about a command the gate
      // is carrying out; silence makes the bridge retry, and that retry lands after
      // record() and receives the real result.
      bump(Status::DuplicateInFlight);
      return {Verdict::InFlight, Status::DuplicateInFlight, AckResult::Accepted, 0};
    }
    bump(Status::DuplicateCached);
    const Entry& e = entries_[slot];
    return {Verdict::ReturnCached, Status::DuplicateCached, e.result, e.detail};
  }

  // spec 9.4 step 5 - strictly newer, by serial-number arithmetic (spec 10.5).
  if (!seq_newer(seq, high_water_)) {
    bump(Status::RejectedSeq);
    return {Verdict::Reject, Status::RejectedSeq, AckResult::Accepted, 0};
  }

  // spec 9.4 step 6, state half - BEFORE the caller dispatches. The mark and the
  // in-flight entry are both in place by the time the caller sees Execute, so no
  // retry can find this seq unclaimed however long execution takes.
  high_water_ = seq;
  if (count_ == depth_) evict_oldest();
  const uint8_t s = static_cast<uint8_t>((head_ + count_) % kDedupCacheCapacity);
  entries_[s] = {seq, AckResult::Accepted, 0};
  in_flight_ |= (1u << s);
  ++count_;
  return {Verdict::Execute, Status::Ok, AckResult::Accepted, 0};
}

bool CommandGate::record(Seq seq, AckResult result, uint8_t detail) {
  const int slot = find(seq);
  if (slot < 0 || !(in_flight_ & (1u << slot))) return false;
  entries_[slot].result = result;
  entries_[slot].detail = detail;
  in_flight_ &= ~(1u << slot);
  return true;
}

void CommandGate::reset_context(CtxId new_ctx) {
  ctx_id_     = new_ctx;
  high_water_ = 0;
  head_       = 0;
  count_      = 0;
  in_flight_  = 0;
}

int CommandGate::find(Seq seq) const {
  for (uint8_t i = 0; i < count_; ++i) {
    const uint8_t s = static_cast<uint8_t>((head_ + i) % kDedupCacheCapacity);
    if (entries_[s].seq == seq) return s;
  }
  return -1;
}

void CommandGate::evict_oldest() {
  in_flight_ &= ~(1u << head_);
  head_ = static_cast<uint8_t>((head_ + 1) % kDedupCacheCapacity);
  --count_;
}

}  // namespace lran
