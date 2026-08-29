// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Fragment reassembly. Spec 11.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config.h"
#include "lran/counters.h"
#include "lran/frame.h"
#include "lran/types.h"

namespace lran {

// Reassembles one fragment set, keyed on (src, ctx_id, seq, schema) per spec 11.
//
// ONE in-flight set, not a pool. Sixteen bits of got_mask_ covers the 15-fragment
// maximum with a bit to spare, and a second concurrent set from the same peer is a
// protocol violation rather than a case to support. A bridge talking to several
// nodes holds one Reassembler per peer.
//
// Fragments are concatenated in INDEX order; the reassembled length is the sum of
// the fragment payload lengths. No fragment-size convention is imposed on the
// sender, which is what lets an out-of-order set reassemble without the receiver
// having to know where fragment 0 ended.
//
// Cost of that generality: fragments land in a staging buffer in arrival order and
// are permuted into index order on completion, so the object holds two payload
// buffers. On a node with 512 KB of SRAM that is the right trade against imposing a
// uniform-chunk rule the specification does not state.
class Reassembler {
 public:
  explicit Reassembler(Counters* counters = nullptr) : counters_(counters) {}

  // spec 11 - default frag_reassembly_timeout_ms is 5000. Runtime-settable because
  // no timing constant may be fixed at compile time in a node that cannot be
  // reflashed without a walk to the gate.
  void set_timeout_ms(uint32_t ms) { timeout_ms_ = ms; }
  uint32_t timeout_ms() const { return timeout_ms_; }

  // now_ms is PASSED IN. This class never calls millis(): a timeout test that had to
  // wait five real seconds would not get written, and so the path would not be
  // tested.
  //
  // A frag_total of 1 is accepted as a complete single-frame set, so a caller can
  // route every frame through here without branching.
  Status accept(const Frame& f, uint32_t now_ms);

  // Expires a stale set. Call from the receive loop's periodic tick, not only on
  // arrival - otherwise a set whose remaining fragments never arrive is never
  // counted, and the peer's silence looks like nothing happened.
  void tick(uint32_t now_ms);

  bool complete() const { return complete_; }
  const uint8_t* data() const { return out_; }
  size_t len() const { return out_len_; }

  // Key of the set currently held, valid while active() or complete().
  NodeId   src() const { return src_; }
  CtxId    ctx_id() const { return ctx_id_; }
  Seq      seq() const { return seq_; }
  SchemaId schema() const { return schema_; }
  MsgType  type() const { return type_; }

  bool active() const { return active_; }
  void reset();

 private:
  void begin(const Frame& f, uint32_t now_ms);
  bool same_set(const Frame& f) const;
  void assemble();

  Counters* counters_;
  uint32_t  timeout_ms_ = kDefaultFragTimeoutMs;

  bool     active_   = false;
  bool     complete_ = false;
  uint32_t start_ms_ = 0;

  NodeId   src_    = 0;
  CtxId    ctx_id_ = 0;
  Seq      seq_    = 0;
  SchemaId schema_ = kSchemaNone;
  MsgType  type_   = MsgType::Poll;
  uint8_t  total_  = 0;

  uint16_t got_mask_ = 0;  // one bit per fragment index

  uint8_t  stage_[kMaxPayloadPlain];  // fragment bytes, arrival order
  uint16_t stage_used_ = 0;
  uint16_t frag_off_[kMaxFragments] = {};
  uint16_t frag_len_[kMaxFragments] = {};

  uint8_t  out_[kMaxPayloadPlain];
  size_t   out_len_ = 0;
};

}  // namespace lran
