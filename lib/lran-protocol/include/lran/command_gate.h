// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Replay and dedup gate. Spec 9.4 steps 4-6 (state half), 10.4. D34, as amended
// 2026-09-11 (LRAN-P8-CommandGate-Brief).

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/counters.h"
#include "lran/types.h"

namespace lran {

// spec 10.4 - dedup_cache_depth ranges 1..32, runtime-settable (root rule 8). Static
// allocation (root rule 3) means the storage is sized for the top of the range, not
// the default: 32 entries of 4 bytes, 128 B per peer.
inline constexpr uint8_t kDedupCacheCapacity     = 32;
inline constexpr uint8_t kDefaultDedupCacheDepth = 8;

enum class Verdict : uint8_t {
  Execute,       // dispatch it, then record() the result
  ReturnCached,  // answer COMMAND_ACK(DUPLICATE_CACHED) with cached_result
  InFlight,      // the first copy is still executing - send NOTHING (spec 10.4)
  Reject,        // answer COMMAND_ACK(REJECTED_SEQ)
};

struct GateResult {
  Verdict   verdict;
  Status    status;         // Ok | DuplicateCached | DuplicateInFlight | RejectedSeq
  AckResult cached_result;  // valid ONLY when verdict == ReturnCached
  uint8_t   cached_detail;  // likewise
};

// One instance per peer, called once per COMPLETED set, immediately after the
// Reassembler, on authenticated types only. Status `seq` is advisory and MUST NOT
// reject (spec 10.2), so status frames never come here.
//
// The gate returns a verdict and executes nothing. Dispatch - and what to do about a
// refusal - stays with the caller (D34).
//
// NO PRECONDITION ON THREADING. check() advances the high-water mark and marks the
// entry in flight before it returns Execute, so a retry that arrives while another
// task is still executing the first copy is answered InFlight and can never be
// answered Execute. This is the property D34's original form lacked: it advanced the
// mark in record(), after execution, and a retry landing in between passed both
// checks and pulsed the relay a second time.
class CommandGate {
 public:
  explicit CommandGate(Counters* counters = nullptr) : counters_(counters) {}

  // spec 10.4 - default 8. Clamped to 1..kDedupCacheCapacity. Shrinking evicts the
  // oldest entries; growing keeps what is there.
  void    set_cache_depth(uint8_t n);
  uint8_t cache_depth() const { return depth_; }

  // spec 9.4 steps 4-6, in that order. Step 4 BEFORE step 5, and the order is
  // load-bearing: a retry carries seq == high_water, which step 5 rejects, so
  // checking seq first would answer REJECTED_SEQ to a frame owed the cached ACK.
  //
  // On Execute this has ALREADY done step 6's state half: the high-water mark is at
  // `seq` and an in-flight entry holds its place. The command's `seq` is consumed
  // whether or not execution succeeds, which is correct - seq is attacker-visible and
  // must not be reusable - and a failure is recorded as the result like any other.
  //
  // Bumps the counter for every verdict except Execute.
  GateResult check(Seq seq);

  // Stores the result of executing `seq`, which the application is about to ACK. A
  // retry after this returns ReturnCached with exactly this result and detail.
  //
  // Returns false, and changes nothing, if no in-flight entry for `seq` is held:
  // record() without check(), a second record() for the same seq, or an in-flight
  // entry evicted before its execution finished (more than cache_depth() newer
  // commands accepted meanwhile, or the depth shrunk). The last case is safe - a
  // retry of an evicted seq sits at or below the high-water mark and step 5 rejects
  // it - but the caller should log it: the bridge will read REJECTED_SEQ for a
  // command that ran.
  bool record(Seq seq, AckResult result, uint8_t detail);

  // spec 10.1, 10.3 - a new context invalidates every cached entry and the mark,
  // because both are keyed within a context and a reboot changes it. The cache is
  // RAM-only and lost on reboot for the same reason (spec 10.4).
  void  reset_context(CtxId new_ctx);
  CtxId ctx_id() const { return ctx_id_; }

  // spec 9.4 step 5's reference. Starts at 0 in each context: the bridge's command
  // seq starts at 1 (spec 10.2), which is newer than 0.
  Seq high_water() const { return high_water_; }

 private:
  struct Entry {
    Seq       seq;
    AckResult result;
    uint8_t   detail;
  };

  // Ring slot of the entry holding `seq`, or -1.
  int  find(Seq seq) const;
  void evict_oldest();
  void bump(Status s) {
    if (counters_ != nullptr) counters_->bump(s);
  }

  Counters* counters_;
  CtxId     ctx_id_     = 0;
  Seq       high_water_ = 0;
  uint8_t   depth_      = kDefaultDedupCacheDepth;

  // Oldest-first ring. head_ is the oldest slot; count_ never exceeds depth_.
  Entry    entries_[kDedupCacheCapacity] = {};
  uint32_t in_flight_ = 0;  // one bit per SLOT - an entry awaiting record()
  uint8_t  head_      = 0;
  uint8_t  count_     = 0;

  static_assert(kDedupCacheCapacity <= 32, "in_flight_ holds one bit per slot");
};

}  // namespace lran
