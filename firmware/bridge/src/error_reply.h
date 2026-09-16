// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Spec 14.2 - the ERROR replies the bridge sends BEFORE it has authenticated the sender.
// Task BF-19a; spec 6.5, 8.8, 14, 14.2; Impl Plan 4.3.2.
//
// WHY THIS IS A POLICY OBJECT AND NOT THREE LINES IN THE LADDER. Every ERROR here answers
// a frame whose `src` is a claim, not an identity: each stage 14 defines an ERROR for runs
// before the MAC check, and two of them run before the frame is even parsed past its
// header. Spec 14.2 therefore bounds what an unauthenticated frame can make the bridge do,
// and those bounds are the thing worth testing on a host rather than at the gate:
//
//   1. A REGISTERED SOURCE ONLY. A frame from an address the registry does not hold a key
//      for is discarded at stage 9a and never answered - there is no key to authenticate
//      either direction of the exchange, and answering a stranger is how a receiver
//      becomes a reflector.
//   2. RATE-LIMITED PER SOURCE. Without a limit, one forged frame per ERROR means a
//      transmitter someone else controls, at a rate they choose, on a channel the whole
//      fleet shares. `error_min_interval_ms` (default 1000, root rule 8) is the floor
//      between two ERRORs to one peer.
//
// The reply carries `ctx_id` = 0: the bridge has no context of its own (spec 10.1) and
// the frame that provoked it is unauthenticated, so there is no context to claim. Spec
// 14.2 requires a receiver never to ADOPT that zero (spec 5.5 makes it "unknown"), which
// is a node-side obligation this file cannot enforce and states for the reader.
//
// Arduino-free and I/O-free, like scheduler.h and node_availability.h: the caller decides
// when, this decides whether.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/messages.h"
#include "lran/types.h"
#include "registry.h"

namespace bridge {

// Spec 14.2 - the floor between two ERRORs to one peer. Runtime-settable; BF-23 takes it
// from Home Assistant with the rest of the timing.
inline constexpr uint32_t kErrorMinIntervalDefaultMs = 1000;

// One slot per provisioned node, because a reply is only ever sent to one of them.
inline constexpr size_t kErrorPeers = kNodeCount;

struct ErrorReply {
  bool          send    = false;
  lran::ErrCode code    = lran::ErrCode::BadLength;
  lran::NodeId  dst     = 0;
  lran::Seq     ref_seq = 0;  // spec 6.5 - the offending frame's seq, or 0
};

// Spec 14's "On failure" column, for the stages that name an ERROR. Returns false for a
// discard that spec 14 answers with silence or with a COMMAND_ACK, which is every status
// not listed in the table this function is built from.
//
// BAD_CRC AND BAD_VERSION ARE DELIBERATELY ABSENT. Spec 14 marks both optional and the
// bridge does not send them: a frame that failed CRC has a `src` field that cannot be
// trusted to name its sender at all, so the reply would go to an address chosen by
// corruption. A version this bridge cannot read is BF-22's to answer, with the per-node
// downgrade in hand.
bool error_code_for(lran::Status s, lran::ErrCode* out);

class ErrorReplyPolicy {
 public:
  // Root rule 8. Zero disables the limit, which is for tests; nothing sets it in the
  // firmware.
  void configure(uint32_t min_interval_ms) { min_interval_ms_ = min_interval_ms; }

  // Spec 14.2. `src_registered` is the caller's answer from the registry - this object
  // holds no keys. A decision to send records the time, so the limit counts replies
  // ACTUALLY SENT rather than replies considered.
  ErrorReply decide(lran::Status s, lran::NodeId src, lran::Seq offending_seq,
                    bool src_registered, uint32_t now_ms);

  // Replies suppressed by the rate limit, for the diagnostic publication. Not a spec 14.1
  // counter and not a discard: the frame that provoked it is already counted by the stage
  // that discarded it, and counting the silence again would double it.
  uint32_t suppressed() const { return suppressed_; }

 private:
  struct Peer {
    lran::NodeId src     = 0;
    uint32_t     last_ms = 0;
    bool         used    = false;
  };

  Peer* peer_for(lran::NodeId src, uint32_t now_ms);

  Peer     peers_[kErrorPeers];
  uint32_t min_interval_ms_ = kErrorMinIntervalDefaultMs;
  uint32_t suppressed_      = 0;
};

// Spec 6.5 and 14.2's header. `seq` is the bridge's own, local and advisory (spec 10.2),
// and `ctx_id` is 0. Returns the frame length, or 0 if it would not encode.
size_t build_error_frame(const ErrorReply& reply, lran::Seq seq, uint8_t* buf, size_t cap);

}  // namespace bridge
