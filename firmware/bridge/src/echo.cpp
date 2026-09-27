// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The bridge's PING responder. BF-27; PRD R-5.4a; spec 6.6, 17.3. echo.h has the design.

#include "echo.h"

#include "lran/codec.h"
#include "lran/messages.h"
#include "lran/types.h"

namespace bridge {

EchoOffer PingEcho::offer(const lran::Header& ping, const uint8_t* payload, size_t len,
                          uint8_t frag_chunk) {
  if (active_) {
    ++stats_.busy;
    return EchoOffer::Busy;
  }
  lran::msg::Ping p;
  if (payload == nullptr || len > sizeof(payload_) ||
      lran::msg::deserialize(payload, len, &p) != lran::Status::Ok) {
    ++stats_.malformed;
    return EchoOffer::Malformed;
  }

  // A fresh header rather than a copy of the PING's: the received `frag` describes only
  // the fragment that completed the set, and encode() writes the echo's own.
  reply_        = lran::Header{};
  reply_.ver    = ping.ver;
  reply_.type   = lran::MsgType::Ping;
  reply_.src    = lran::kNodeBridge;
  reply_.dst    = ping.src;
  reply_.seq    = ping.seq;
  reply_.ctx_id = ping.ctx_id;
  reply_.schema = lran::kSchemaNone;

  // The payload is echoed as received, reserved ping_flags bits included (spec 6.6).
  for (size_t i = 0; i < len; ++i) payload_[i] = payload[i];
  len_ = len;

  // A chunk that covers the whole payload is one frame. A split fragment_count() cannot
  // make, more than 15 fragments, cannot come from a set the ladder reassembled.
  chunk_ = frag_chunk != 0 && frag_chunk < len ? frag_chunk : 0;
  total_ = chunk_ == 0 ? 1 : lran::fragment_count(len, chunk_);
  if (total_ == 0) {
    ++stats_.malformed;
    return EchoOffer::Malformed;
  }
  next_   = 0;
  active_ = true;
  ++stats_.answered;
  return EchoOffer::Accepted;
}

size_t PingEcho::encode_next(uint8_t* buf, size_t cap) const {
  if (!frames_left()) return 0;
  const lran::EncodeCtx ectx;  // spec 9.2 - PING carries no MAC
  size_t                n = 0;
  const lran::Status    st =
      chunk_ == 0 ? lran::encode(reply_, payload_, len_, ectx, buf, cap, &n)
                  : lran::encode_fragment(reply_, payload_, len_, next_, chunk_, ectx, buf, cap, &n);
  return st == lran::Status::Ok ? n : 0;
}

void PingEcho::advance() {
  if (!frames_left()) return;
  ++next_;
  ++stats_.frames;
}

}  // namespace bridge
