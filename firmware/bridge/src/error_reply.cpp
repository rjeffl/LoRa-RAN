// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "error_reply.h"

#include "lran/codec.h"

namespace bridge {

// spec 14's "On failure" column. The mapping is not one-to-one and must not be made to
// look like it: BAD_LENGTH answers three different stages, each with its own counter,
// because the WIRE answer and the DIAGNOSIS are different questions (spec 11.4, 5.6).
bool error_code_for(lran::Status s, lran::ErrCode* out) {
  if (out == nullptr) return false;
  switch (s) {
    case lran::Status::UnknownHdrExt:
      *out = lran::ErrCode::UnknownHdrExt;  // stage 5a, spec 5.8
      return true;
    case lran::Status::BadFrag:
      *out = lran::ErrCode::BadLength;  // stage 5b - a `frag` total of 0, spec 5.6
      return true;
    case lran::Status::UnknownType:
      *out = lran::ErrCode::UnknownType;  // stage 6
      return true;
    case lran::Status::UnknownSchema:
      *out = lran::ErrCode::UnknownSchema;  // stage 7
      return true;
    case lran::Status::BadLength:
      *out = lran::ErrCode::BadLength;  // stage 8
      return true;
    case lran::Status::NotFragmentable:
      *out = lran::ErrCode::BadLength;  // stage 8a, spec 11.4
      return true;
    case lran::Status::ReassemblyTimeout:
      *out = lran::ErrCode::ReassemblyTimeout;  // stage 10, spec 11.2
      return true;
    case lran::Status::FragmentOverflow:
      *out = lran::ErrCode::FragmentOverflow;  // stage 10, spec 11.2
      return true;

    // Silence, and each for its own reason. Stages 1, 2 and 2a have no `src` worth
    // reading; stage 5 means the frame was not ours to answer; stage 9a is spec 14.2's
    // rule about strangers; stage 9 and stage 11 answer with a COMMAND_ACK (spec 9.4),
    // which is BF-18's to send. rx_frag_duplicate and rx_frag_late are normal traffic.
    // BadCrc and BadVersion are spec 14's two optional ERRORs and are not built - see
    // the header.
    default:
      return false;
  }
}

ErrorReplyPolicy::Peer* ErrorReplyPolicy::peer_for(lran::NodeId src, uint32_t now_ms) {
  for (Peer& p : peers_) {
    if (p.used && p.src == src) return &p;
  }
  for (Peer& p : peers_) {
    if (!p.used) {
      p.used    = true;
      p.src     = src;
      p.last_ms = now_ms - min_interval_ms_ - 1;  // a first reply is never rate-limited
      return &p;
    }
  }
  // Unreachable while the table holds one slot per provisioned node and a reply only ever
  // goes to a provisioned node. Kept because the alternative is a null dereference if
  // either of those two facts ever stops being true.
  return nullptr;
}

ErrorReply ErrorReplyPolicy::decide(lran::Status s, lran::NodeId src, lran::Seq offending_seq,
                                    bool src_registered, uint32_t now_ms) {
  ErrorReply reply;

  lran::ErrCode code = lran::ErrCode::BadLength;
  if (!error_code_for(s, &code)) return reply;

  // spec 14.2, bound 1. Checked before the rate limit so a stranger never takes a slot in
  // the table, which would let unregistered traffic evict a real peer's timestamp.
  if (!src_registered) return reply;

  Peer* p = peer_for(src, now_ms);
  if (p == nullptr) return reply;

  // spec 14.2, bound 2. Wrap-safe: the difference is taken as a signed interval, so a
  // millis() rollover reads as a large positive age rather than a peer locked out for
  // 24.8 days.
  if (min_interval_ms_ > 0) {
    const uint32_t since = now_ms - p->last_ms;
    if (since < min_interval_ms_) {
      ++suppressed_;
      return reply;
    }
  }

  p->last_ms    = now_ms;
  reply.send    = true;
  reply.code    = code;
  reply.dst     = src;
  reply.ref_seq = offending_seq;
  return reply;
}

size_t build_error_frame(const ErrorReply& reply, lran::Seq seq, uint8_t* buf, size_t cap) {
  if (!reply.send || buf == nullptr) return 0;

  lran::Header h;
  h.ver  = lran::kProtoVer;
  h.type = lran::MsgType::Error;
  h.src  = lran::kNodeBridge;
  h.dst  = reply.dst;
  h.seq  = seq;
  // spec 14.2 - 0 is "unknown" (spec 5.5). The bridge has no context of its own and the
  // frame being answered is unauthenticated, so there is none to claim. A node must not
  // adopt it.
  h.ctx_id = 0;
  h.schema = lran::kSchemaNone;

  lran::msg::Error e;
  e.err_code = static_cast<uint8_t>(reply.code);
  e.detail   = 0;
  e.ref_seq  = reply.ref_seq;

  uint8_t payload[lran::msg::kErrorLen];
  size_t  plen = 0;
  if (lran::msg::serialize(e, payload, sizeof(payload), &plen) != lran::Status::Ok) return 0;

  lran::EncodeCtx ectx;  // spec 9.2 - ERROR carries no MAC
  size_t          len = 0;
  return lran::encode(h, payload, plen, ectx, buf, cap, &len) == lran::Status::Ok ? len : 0;
}

}  // namespace bridge
