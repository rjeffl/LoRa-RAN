// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R9 - the W9 bench runs. See w9.h.

#include "w9.h"

namespace rangetest {

const char* to_string(W9Run r) {
  switch (r) {
    case W9Run::MaxFrame:   return "max-frame";
    case W9Run::Fragmented: return "fragmented";
  }
  return "?";
}

W9Plan w9_plan(W9Run run, uint16_t pings) {
  W9Plan p;
  p.run    = run;
  p.echo_n = kW9EchoBytes;
  p.pings  = pings;

  // spec 6.6 - the payload is [ping_flags][n][data:n].
  p.payload_len = static_cast<uint16_t>(lran::msg::kPingHdrLen + p.echo_n);

  if (run == W9Run::Fragmented) {
    p.frag_chunk   = kW9FragChunk;
    p.expect_frags = lran::fragment_count(p.payload_len, p.frag_chunk);
  } else {
    p.frag_chunk   = 0;
    p.expect_frags = 1;
  }
  return p;
}

lran::Header w9_header(lran::Seq seq, lran::NodeId src, lran::NodeId dst) {
  lran::Header h;
  h.ver    = lran::kProtoVer;
  h.type   = lran::MsgType::Ping;
  h.src    = src;
  h.dst    = dst;
  h.seq    = seq;

  // spec 9.2 - PING is unauthenticated, so there is no context to bind it to and
  // no MAC to key. ctx_id stays 0, which is also what tells a reader of a capture
  // that this frame never went through the spec 9.4 authenticated path.
  h.ctx_id = 0;
  h.schema = lran::kSchemaNone;

  h.frag      = 0x01;  // spec 5.6 - a single unfragmented frame
  h.hdr_flags = 0;
  return h;
}

lran::Status w9_build_ping(lran::Seq seq, uint8_t n, uint8_t* out, size_t cap,
                           size_t* out_len) {
  if (n > lran::kPingMaxEcho) return lran::Status::BadLength;
  if (out == nullptr || out_len == nullptr) return lran::Status::BadLength;
  if (cap < lran::msg::kPingHdrLen + static_cast<size_t>(n)) return lran::Status::BadLength;

  // The pattern is generated straight into the caller's buffer at the echo offset,
  // so nothing here needs a scratch buffer of its own (repo rule 3).
  uint8_t* data = out + lran::msg::kPingHdrLen;
  lran::msg::ping_fill_pattern(seq, data, n);

  lran::msg::Ping p;
  p.ping_flags = 0x01;  // spec 6.6.3 - PATTERN_FILL
  p.n          = n;
  p.data       = data;

  return lran::msg::serialize(p, out, cap, out_len);
}

W9EchoCheck w9_check_echo(lran::Seq seq, uint8_t sent_n, const uint8_t* payload,
                          size_t len) {
  W9EchoCheck c;

  lran::msg::Ping got;
  c.status = lran::msg::deserialize(payload, len, &got);
  if (c.status != lran::Status::Ok) return c;

  c.n = got.n;

  // spec 6.6 - the responder echoes `n` bytes verbatim and preserves ping_flags.
  // A short echo is not a partial success: it means the far end rebuilt the payload
  // rather than echoing it, which is the fault this run exists to catch.
  if (got.n != sent_n) {
    c.status = lran::Status::BadLength;
    return c;
  }

  // spec 6.6 - PATTERN_FILL preserved, and spec 5.9 - the reserved bits still zero.
  c.flags_ok = (got.ping_flags == 0x01);

  // status stays Ok: the codec accepted this frame and it was right to. Whether the
  // BYTES are correct is a separate question and pattern_ok is its answer.
  c.pattern_ok = lran::msg::ping_check_pattern(seq, got.data, got.n, &c.first_bad);
  return c;
}

size_t w9_echo_chunk(uint16_t largest_frag_payload, uint8_t frag_total) {
  if (frag_total <= 1) return 0;  // arrived as a single frame; echo the same way
  return largest_frag_payload;
}

void w9_stats_reset(W9Stats* s) {
  if (s != nullptr) *s = W9Stats{};
}

bool w9_run_passed(const W9Stats& s) {
  if (s.pings_sent == 0) return false;
  return s.echoes_ok == s.pings_sent && s.pattern_faults == 0 &&
         s.decode_faults == 0 && s.reassembly_fails == 0 && s.echo_timeouts == 0;
}

W9AirtimeCheck w9_airtime_check(uint32_t frame_airtime_ms, uint32_t backoff_max_ms) {
  W9AirtimeCheck c;
  c.frame_airtime_ms = frame_airtime_ms;
  c.backoff_max_ms   = backoff_max_ms;

  // A backoff window shorter than one frame's airtime cannot outlast the frame it
  // backed off for: the retry lands while the channel is still occupied by the same
  // transmission, and CAD fires again. spec 12.3's defaults were chosen against an
  // empty channel and a 222-byte frame at a high SF is where that assumption is
  // worth re-testing (R9).
  c.backoff_covers = (backoff_max_ms >= frame_airtime_ms);
  return c;
}

}  // namespace rangetest
