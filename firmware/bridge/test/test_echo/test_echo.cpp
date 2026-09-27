// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-27's RF echo - the bridge's PING responder (echo.h; spec 6.6, 17.3).
//
// A PING is encoded as the simnode's `ping` command encodes it, passed through the
// bridge's own RxLadder, offered to PingEcho, and the echo's frames are decoded and
// reassembled as a node would. So the chunk the ladder records is the one the echo
// splits at, end to end, rather than a number the test supplies.

#include <unity.h>

#include "echo.h"
#include "lran/codec.h"
#include "lran/counters.h"
#include "lran/messages.h"
#include "lran/reassembly.h"
#include "lran/types.h"
#include "rx_ladder.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

constexpr NodeId kPeer = 0xF0;
constexpr CtxId  kCtx  = 0x0EC40EC4;
constexpr Seq    kSeq  = 0x2A;

struct BenchOnly final : PeerKeys {
  const uint8_t* key_for(NodeId) const override { return nullptr; }
  bool is_registered(NodeId src) const override { return src == kPeer; }
};

Header ping_header() {
  Header h;
  h.ver    = kProtoVer;
  h.type   = MsgType::Ping;
  h.src    = kPeer;
  h.dst    = kNodeBridge;
  h.seq    = kSeq;
  h.ctx_id = kCtx;
  h.schema = kSchemaNone;
  return h;
}

// [ping_flags][n][data], PATTERN_FILL set.
size_t ping_payload(uint8_t n, uint8_t* out) {
  out[0] = kPingFlagPatternFill;
  out[1] = n;
  msg::ping_fill_pattern(kSeq, out + 2, n);
  return 2u + n;
}

// The simnode's send: one frame, or the set split at `chunk`. Each frame goes through
// the ladder; returns true with `d` filled once a payload completes.
bool send_through_ladder(RxLadder& ladder, const uint8_t* payload, size_t len, uint8_t chunk,
                         RxDelivery* d) {
  const Header    h = ping_header();
  const EncodeCtx ectx;
  uint8_t         buf[kMaxFrame];
  size_t          n = 0;
  if (chunk == 0) {
    TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload, len, ectx, buf, sizeof(buf), &n));
    return ladder.accept(buf, n, 1000, d);
  }
  const uint8_t total = fragment_count(len, chunk);
  bool          done  = false;
  for (uint8_t i = 0; i < total; ++i) {
    TEST_ASSERT_EQUAL(Status::Ok,
                      encode_fragment(h, payload, len, i, chunk, ectx, buf, sizeof(buf), &n));
    done = ladder.accept(buf, n, 1000 + i, d);
  }
  return done;
}

// What the node sees: each echo frame decoded at kPeer and reassembled.
struct NodeSide {
  Counters    counters;
  Reassembler reassembler{&counters};
  uint8_t     frames = 0;
  Header      last{};
  uint8_t     max_payload = 0;

  bool take(const uint8_t* buf, size_t len) {
    DecodeCtx ctx;
    ctx.self = kPeer;
    Frame f;
    TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, len, ctx, &f));
    TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, len, ctx, &f));
    ++frames;
    last = f.hdr;
    if (f.payload_len > max_payload) max_payload = static_cast<uint8_t>(f.payload_len);
    if (f.hdr.frag_total() == 1) return true;
    TEST_ASSERT_EQUAL(Status::Ok, reassembler.accept(f, 2000 + frames));
    return reassembler.complete();
  }
};

void drain(PingEcho& echo, NodeSide* node) {
  uint8_t buf[kMaxFrame];
  while (echo.frames_left()) {
    const size_t n = echo.encode_next(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    node->take(buf, n);
    echo.advance();
  }
  echo.finish();
}

}  // namespace

// ---------------------------------------------------------------------------

// spec 6.6 - src and dst swap; seq, ctx_id and the payload come back unchanged.
void test_single_frame_ping_comes_back_turned_round() {
  Counters  c;
  RxLadder  ladder(&c);
  BenchOnly keys;
  ladder.set_auth(nullptr, &keys);

  uint8_t    payload[kMaxPayloadPlain];
  const size_t len = ping_payload(40, payload);
  RxDelivery d;
  TEST_ASSERT_TRUE(send_through_ladder(ladder, payload, len, 0, &d));
  TEST_ASSERT_EQUAL_UINT8(0, d.frag_chunk);

  PingEcho echo;
  TEST_ASSERT_EQUAL(EchoOffer::Accepted, echo.offer(d.hdr, d.payload, d.payload_len, d.frag_chunk));
  TEST_ASSERT_EQUAL_UINT8(1, echo.total());

  NodeSide node;
  uint8_t  buf[kMaxFrame];
  const size_t n = echo.encode_next(buf, sizeof(buf));
  TEST_ASSERT_TRUE(node.take(buf, n));
  TEST_ASSERT_EQUAL_UINT8(kNodeBridge, node.last.src);
  TEST_ASSERT_EQUAL_UINT8(kPeer, node.last.dst);
  TEST_ASSERT_EQUAL_UINT8(kSeq, node.last.seq);
  TEST_ASSERT_EQUAL_UINT32(kCtx, node.last.ctx_id);
  TEST_ASSERT_EQUAL(MsgType::Ping, node.last.type);

  Frame f;
  DecodeCtx ctx;
  ctx.self = kPeer;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, ctx, &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, ctx, &f));
  TEST_ASSERT_EQUAL_size_t(len, f.payload_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, f.payload, len);
}

// spec 6.6.1 - a maximum PING, 202 echo bytes, fills a 222-byte frame both ways.
void test_maximum_ping_fills_the_frame_both_ways() {
  Counters  c;
  RxLadder  ladder(&c);
  BenchOnly keys;
  ladder.set_auth(nullptr, &keys);

  uint8_t      payload[kMaxPayloadPlain];
  const size_t len = ping_payload(kPingMaxEcho, payload);
  RxDelivery   d;
  TEST_ASSERT_TRUE(send_through_ladder(ladder, payload, len, 0, &d));

  PingEcho echo;
  TEST_ASSERT_EQUAL(EchoOffer::Accepted, echo.offer(d.hdr, d.payload, d.payload_len, d.frag_chunk));
  uint8_t buf[kMaxFrame];
  TEST_ASSERT_EQUAL_size_t(kMaxFrame, echo.encode_next(buf, sizeof(buf)));
}

// spec 6.6.2 - the full 15-fragment set: 202 echo bytes at a chunk of 14. The ladder
// records the chunk, the echo splits at it, and the node reassembles the payload intact.
void test_fragmented_ping_is_refragmented_at_the_initiators_chunk() {
  Counters  c;
  RxLadder  ladder(&c);
  BenchOnly keys;
  ladder.set_auth(nullptr, &keys);

  uint8_t      payload[kMaxPayloadPlain];
  const size_t len = ping_payload(kPingMaxEcho, payload);
  RxDelivery   d;
  TEST_ASSERT_TRUE(send_through_ladder(ladder, payload, len, 14, &d));
  TEST_ASSERT_EQUAL_UINT8(15, d.fragments);
  TEST_ASSERT_EQUAL_UINT8(14, d.frag_chunk);

  PingEcho echo;
  TEST_ASSERT_EQUAL(EchoOffer::Accepted, echo.offer(d.hdr, d.payload, d.payload_len, d.frag_chunk));
  TEST_ASSERT_EQUAL_UINT8(15, echo.total());

  NodeSide node;
  drain(echo, &node);
  TEST_ASSERT_EQUAL_UINT8(15, node.frames);
  TEST_ASSERT_EQUAL_UINT8(14, node.max_payload);
  TEST_ASSERT_TRUE(node.reassembler.complete());
  TEST_ASSERT_EQUAL_size_t(len, node.reassembler.len());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, node.reassembler.data(), len);
  TEST_ASSERT_EQUAL_UINT32(15, echo.stats().frames);

  size_t first_bad = 0;
  TEST_ASSERT_TRUE(msg::ping_check_pattern(kSeq, node.reassembler.data() + 2, kPingMaxEcho,
                                           &first_bad));
}

// A set that displaces an incomplete one starts its chunk afresh.
void test_a_displaced_set_does_not_lend_its_chunk() {
  Counters  c;
  RxLadder  ladder(&c);
  BenchOnly keys;
  ladder.set_auth(nullptr, &keys);

  uint8_t      payload[kMaxPayloadPlain];
  const size_t len = ping_payload(60, payload);

  // Two fragments of a set at chunk 40, never finished.
  Header       h = ping_header();
  const EncodeCtx ectx;
  uint8_t      buf[kMaxFrame];
  size_t       n = 0;
  RxDelivery   d;
  TEST_ASSERT_EQUAL(Status::Ok, encode_fragment(h, payload, len, 0, 40, ectx, buf, sizeof(buf), &n));
  TEST_ASSERT_FALSE(ladder.accept(buf, n, 1000, &d));

  // A new seq, at chunk 20, completes.
  h.seq = kSeq + 1;
  const uint8_t total = fragment_count(len, 20);
  bool done = false;
  for (uint8_t i = 0; i < total; ++i) {
    TEST_ASSERT_EQUAL(Status::Ok, encode_fragment(h, payload, len, i, 20, ectx, buf, sizeof(buf), &n));
    done = ladder.accept(buf, n, 1100 + i, &d);
  }
  TEST_ASSERT_TRUE(done);
  TEST_ASSERT_EQUAL_UINT8(20, d.frag_chunk);
}

// One echo at a time; a second PING is refused and counted until finish().
void test_a_ping_during_an_echo_is_refused_and_counted() {
  uint8_t      payload[kMaxPayloadPlain];
  const size_t len = ping_payload(10, payload);
  PingEcho     echo;
  TEST_ASSERT_EQUAL(EchoOffer::Accepted, echo.offer(ping_header(), payload, len, 0));
  TEST_ASSERT_TRUE(echo.busy());
  TEST_ASSERT_EQUAL(EchoOffer::Busy, echo.offer(ping_header(), payload, len, 0));
  TEST_ASSERT_EQUAL_UINT32(1, echo.stats().busy);

  echo.advance();
  TEST_ASSERT_FALSE(echo.frames_left());
  TEST_ASSERT_TRUE(echo.busy());  // until the last frame has left lora_task
  echo.finish();
  TEST_ASSERT_EQUAL(EchoOffer::Accepted, echo.offer(ping_header(), payload, len, 0));
  TEST_ASSERT_EQUAL_UINT32(2, echo.stats().answered);
}

// A payload whose n disagrees with its length is not echoed.
void test_a_malformed_ping_is_not_echoed() {
  uint8_t payload[4] = {0, 9, 1, 2};  // n says 9, two bytes follow
  PingEcho echo;
  TEST_ASSERT_EQUAL(EchoOffer::Malformed, echo.offer(ping_header(), payload, sizeof(payload), 0));
  TEST_ASSERT_FALSE(echo.busy());
  TEST_ASSERT_EQUAL_UINT32(1, echo.stats().malformed);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_single_frame_ping_comes_back_turned_round);
  RUN_TEST(test_maximum_ping_fills_the_frame_both_ways);
  RUN_TEST(test_fragmented_ping_is_refragmented_at_the_initiators_chunk);
  RUN_TEST(test_a_displaced_set_does_not_lend_its_chunk);
  RUN_TEST(test_a_ping_during_an_echo_is_refused_and_counted);
  RUN_TEST(test_a_malformed_ping_is_not_echoed);
  return UNITY_END();
}
