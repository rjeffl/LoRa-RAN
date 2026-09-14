// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-3, BF-5 - the protocol engine: ROLE_RANGE and ROLE_HEALTH, per-identity receive.
//
// The "bridge" in these tests is the library's codec with self = 0x00, which is exactly
// what the bridge's receive ladder calls. What is under test is the simnode's behaviour -
// who answers, with what, from which context and seq space - not the codec's bytes.
//
// WHAT THIS CANNOT COVER. That any of it reaches the air. radio.cpp needs a board.

#include <unity.h>

#include <cstring>

#include "identity.h"
#include "lran/lran.h"
#include "node.h"
#include "refimpl_mac.h"
#include "test_key.h"

using namespace simnode;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefKdf g_kdf;
refimpl::RefMac g_mac;

uint32_t g_next_random = 0x5000;
uint32_t counting_random() { return ++g_next_random; }

class CaptureSink final : public Sink {
 public:
  void line(const char* text) override {
    std::strncpy(last_, text, sizeof(last_) - 1);
    ++lines_;
  }
  bool last_contains(const char* s) const { return std::strstr(last_, s) != nullptr; }
  int  lines() const { return lines_; }

 private:
  char last_[256] = {0};
  int  lines_     = 0;
};

// One simnode board: its table, outbox, engine and log.
struct Board {
  IdentityTable ids;
  Outbox        out;
  CaptureSink   log;
  Node          node{&ids, &out, &g_mac, &log};
  Board() { ids.init(lran_test::kTestMasterKey, &g_kdf, counting_random); }
};

size_t bridge_encode(const Header& h, const uint8_t* payload, size_t n, NodeId peer_for_key,
                     const Board& b, uint8_t* buf) {
  EncodeCtx ectx;
  ectx.mac      = &g_mac;
  ectx.node_key = b.ids.find(peer_for_key)->key;
  size_t len    = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(encode(h, payload, n, ectx, buf, kMaxFrame, &len)));
  return len;
}

size_t poll_to(NodeId dst, const Board& b, uint8_t* buf, uint8_t ver = kProtoVer) {
  Header h;
  h.ver    = ver;
  h.type   = MsgType::Poll;
  h.src    = kNodeBridge;
  h.dst    = dst;
  h.seq    = 3;
  h.ctx_id = b.ids.find(dst) != nullptr ? b.ids.find(dst)->ctx_id : 0;
  const uint8_t payload[1] = {0};
  return bridge_encode(h, payload, 1, dst, b, buf);
}

// Decodes one outgoing frame as the bridge's ladder would, through stage 9.
Frame bridge_decode(const OutFrame& f, NodeId src, const Board& b) {
  DecodeCtx ctx;
  ctx.self           = kNodeBridge;
  ctx.accept_ver_min = 1;
  ctx.accept_ver_max = kProtoVer;
  ctx.mac            = &g_mac;
  ctx.node_key       = b.ids.find(src)->key;
  Frame out;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_header(f.bytes, f.len, ctx, &out)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_payload(f.bytes, f.len, ctx, &out)));
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// POLL -> schema 0xF0
// ---------------------------------------------------------------------------

// spec 6.4 / 7.5 - STATUS 0xF0 from the polled identity's own context and seq space,
// marked synthetic through health_flags bit 0.
void test_a_poll_is_answered_with_node_health() {
  Board b;
  b.ids.add(kNodeSim0, Role::Health);
  const Identity* e = b.ids.find(kNodeSim0);

  uint8_t buf[kMaxFrame];
  size_t  len = poll_to(kNodeSim0, b, buf);
  b.node.on_rx(buf, len, -42, 95, 61000);

  TEST_ASSERT_EQUAL_size_t(1, b.out.size());
  OutFrame f;
  b.out.pop(&f);
  const Frame got = bridge_decode(f, kNodeSim0, b);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MsgType::Status), static_cast<int>(got.hdr.type));
  TEST_ASSERT_EQUAL_UINT8(kSchemaNodeHealthV1, got.hdr.schema);
  TEST_ASSERT_EQUAL_UINT8(kNodeSim0, got.hdr.src);
  TEST_ASSERT_EQUAL_UINT8(kNodeBridge, got.hdr.dst);
  TEST_ASSERT_EQUAL_UINT32(e->ctx_id, got.hdr.ctx_id);
  TEST_ASSERT_EQUAL_UINT16(1, got.hdr.seq);

  schema::NodeHealthV1 h;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::deserialize(got.payload, got.payload_len, &h)));
  TEST_ASSERT_EQUAL_UINT32(61, h.uptime_s);
  TEST_ASSERT_EQUAL_UINT16(kU16NotAvailable, h.boot_count);
  TEST_ASSERT_EQUAL_UINT16(1, h.rx_frames);
  // Frames sent before this one. The health payload is built before it is queued, so a
  // 0xF0 never counts itself - the next one will.
  TEST_ASSERT_EQUAL_UINT16(0, h.tx_frames);
  TEST_ASSERT_EQUAL_INT16(-42, h.last_rssi_dbm);
  TEST_ASSERT_EQUAL_INT16(95, h.last_snr_db10);
  TEST_ASSERT_EQUAL_UINT8(kProtoVer, h.proto_ver);
  TEST_ASSERT_EQUAL_UINT8(schema::kHealthFlagDebugActive, h.health_flags);

  // The next answer continues the same identity's seq space.
  len = poll_to(kNodeSim0, b, buf);
  b.node.on_rx(buf, len, -42, 95, 62000);
  b.out.pop(&f);
  TEST_ASSERT_EQUAL_UINT16(2, bridge_decode(f, kNodeSim0, b).hdr.seq);
}

// Each identity hears every frame. The one addressed answers; the other counts it
// rx_not_addressed, as a second board in the room would.
void test_each_identity_hears_every_frame_and_only_the_addressed_one_answers() {
  Board b;
  b.ids.add(kNodeSim0, Role::Range);
  b.ids.add(kNodeSim2, Role::Health);

  uint8_t      buf[kMaxFrame];
  const size_t len = poll_to(kNodeSim2, b, buf);
  b.node.on_rx(buf, len, -50, 0, 1000);

  TEST_ASSERT_EQUAL_size_t(1, b.out.size());
  OutFrame f;
  b.out.pop(&f);
  TEST_ASSERT_EQUAL_UINT8(kNodeSim2, bridge_decode(f, kNodeSim2, b).hdr.src);

  TEST_ASSERT_EQUAL_UINT32(1, b.ids.find(kNodeSim0)->counters.rx_not_addressed);
  TEST_ASSERT_EQUAL_UINT32(1, b.ids.find(kNodeSim0)->counters.rx_frames);
  TEST_ASSERT_EQUAL_UINT32(0, b.ids.find(kNodeSim2)->counters.rx_not_addressed);
}

// Impl Plan 10.3 - a disabled identity stops answering, which is how the availability
// watchdog is tested without power-cycling anything. It hears nothing either.
void test_a_disabled_identity_is_silent() {
  Board b;
  b.ids.add(kNodeSim0, Role::Health);
  b.ids.find(kNodeSim0)->enabled = false;

  uint8_t      buf[kMaxFrame];
  const size_t len = poll_to(kNodeSim0, b, buf);
  b.node.on_rx(buf, len, -50, 0, 1000);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
  TEST_ASSERT_EQUAL_UINT32(0, b.ids.find(kNodeSim0)->counters.rx_frames);
}

// V-B10 - an identity announcing N-1 speaks N-1: it answers in N-1 and refuses N.
void test_an_identity_speaks_the_version_it_announces() {
  Board b;
  b.ids.add(kNodeSim1, Role::Health);
  b.ids.find(kNodeSim1)->proto_ver = kProtoVer - 1;

  uint8_t buf[kMaxFrame];
  size_t  len = poll_to(kNodeSim1, b, buf, kProtoVer);
  b.node.on_rx(buf, len, -50, 0, 1000);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
  TEST_ASSERT_EQUAL_UINT32(1, b.ids.find(kNodeSim1)->counters.rx_bad_ver);

  len = poll_to(kNodeSim1, b, buf, kProtoVer - 1);
  b.node.on_rx(buf, len, -50, 0, 1000);
  TEST_ASSERT_EQUAL_size_t(1, b.out.size());
  OutFrame f;
  b.out.pop(&f);
  TEST_ASSERT_EQUAL_UINT8(kProtoVer - 1, bridge_decode(f, kNodeSim1, b).hdr.ver);
}

// ---------------------------------------------------------------------------
// PING - spec 6.6, 17.3
// ---------------------------------------------------------------------------

namespace {

size_t ping_frames_from_bridge(NodeId dst, Seq seq, uint8_t n, size_t chunk, const Board& b,
                               uint8_t frames[][kMaxFrame], size_t lens[]) {
  uint8_t payload[kMaxPayloadPlain];
  payload[0] = kPingFlagPatternFill;
  payload[1] = n;
  msg::ping_fill_pattern(seq, payload + 2, n);
  const size_t plen = 2 + static_cast<size_t>(n);

  Header h;
  h.type   = MsgType::Ping;
  h.src    = kNodeBridge;
  h.dst    = dst;
  h.seq    = seq;
  h.ctx_id = 0x0BADF00Du;
  EncodeCtx ectx;
  ectx.mac      = &g_mac;
  ectx.node_key = b.ids.find(dst)->key;

  const uint8_t total = chunk == 0 ? 1 : fragment_count(plen, chunk);
  for (uint8_t i = 0; i < total; ++i) {
    const Status st = chunk == 0
                          ? encode(h, payload, plen, ectx, frames[i], kMaxFrame, &lens[i])
                          : encode_fragment(h, payload, plen, i, chunk, ectx, frames[i],
                                            kMaxFrame, &lens[i]);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok), static_cast<int>(st));
  }
  return total;
}

}  // namespace

// Swap src and dst, preserve seq, flags and bytes; the echo carries this node's own ctx.
void test_role_range_echoes_a_ping() {
  Board b;
  b.ids.add(kNodeSim0, Role::Range);

  uint8_t frames[1][kMaxFrame];
  size_t  lens[1];
  ping_frames_from_bridge(kNodeSim0, 777, 202, 0, b, frames, lens);
  TEST_ASSERT_EQUAL_size_t(kMaxFrame, lens[0]);  // spec 6.6.1 - the full-size frame
  b.node.on_rx(frames[0], lens[0], -30, 80, 500);

  TEST_ASSERT_EQUAL_size_t(1, b.out.size());
  OutFrame f;
  b.out.pop(&f);
  TEST_ASSERT_EQUAL_size_t(kMaxFrame, f.len);
  const Frame got = bridge_decode(f, kNodeSim0, b);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MsgType::Ping), static_cast<int>(got.hdr.type));
  TEST_ASSERT_EQUAL_UINT8(kNodeSim0, got.hdr.src);
  TEST_ASSERT_EQUAL_UINT8(kNodeBridge, got.hdr.dst);
  TEST_ASSERT_EQUAL_UINT16(777, got.hdr.seq);
  TEST_ASSERT_EQUAL_UINT32(b.ids.find(kNodeSim0)->ctx_id, got.hdr.ctx_id);

  msg::Ping p;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(msg::deserialize(got.payload, got.payload_len, &p)));
  TEST_ASSERT_EQUAL_UINT8(202, p.n);
  TEST_ASSERT_EQUAL_UINT8(kPingFlagPatternFill, p.ping_flags);
  TEST_ASSERT_TRUE(msg::ping_check_pattern(777, p.data, p.n, nullptr));
}

// spec 6.6.2 - reassemble, then re-fragment the echo at the initiator's chunk.
void test_a_fragmented_ping_is_echoed_at_the_same_chunk() {
  Board b;
  b.ids.add(kNodeSim0, Role::Range);

  uint8_t      frames[kMaxFragments][kMaxFrame];
  size_t       lens[kMaxFragments];
  const size_t total = ping_frames_from_bridge(kNodeSim0, 12, 40, 14, b, frames, lens);
  TEST_ASSERT_EQUAL_size_t(3, total);

  // Out of order, as the air may deliver them.
  b.node.on_rx(frames[2], lens[2], -30, 80, 100);
  b.node.on_rx(frames[0], lens[0], -30, 80, 200);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
  b.node.on_rx(frames[1], lens[1], -30, 80, 300);
  TEST_ASSERT_EQUAL_size_t(3, b.out.size());

  Counters    c;
  Reassembler r(&c);
  OutFrame    f;
  while (b.out.pop(&f)) {
    const Frame got = bridge_decode(f, kNodeSim0, b);
    TEST_ASSERT_EQUAL_UINT8(3, got.hdr.frag_total());
    TEST_ASSERT_TRUE(got.payload_len <= 14);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok), static_cast<int>(r.accept(got, 400)));
  }
  TEST_ASSERT_TRUE(r.complete());
  msg::Ping p;
  msg::deserialize(r.data(), r.len(), &p);
  TEST_ASSERT_EQUAL_UINT8(40, p.n);
  TEST_ASSERT_TRUE(msg::ping_check_pattern(12, p.data, p.n, nullptr));
}

// Impl Plan 10.2 - ROLE_HEALTH answers 0xF0 only.
void test_role_health_does_not_echo_a_ping() {
  Board b;
  b.ids.add(kNodeSim2, Role::Health);
  uint8_t frames[1][kMaxFrame];
  size_t  lens[1];
  ping_frames_from_bridge(kNodeSim2, 5, 10, 0, b, frames, lens);
  b.node.on_rx(frames[0], lens[0], -30, 80, 500);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
  TEST_ASSERT_EQUAL_UINT32(1, b.ids.find(kNodeSim2)->unhandled);
}

// Two boards: F0 on one pings F2 on the other, F2 echoes, and F0 reports the echo and does
// not echo it back - two simnodes must never ping-pong.
void test_two_boards_complete_a_fragmented_ping_round_trip() {
  Board a;
  Board z;
  a.ids.add(kNodeSim0, Role::Range);
  z.ids.add(kNodeSim2, Role::Range);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(PingResult::Ok),
                        static_cast<int>(a.node.ping(kNodeSim0, 60, true, 20, kNodeSim2, 1000)));
  TEST_ASSERT_EQUAL_size_t(4, a.out.size());  // 62 bytes at a 20-byte chunk
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PingResult::Pending),
                        static_cast<int>(a.node.ping(kNodeSim0, 1, false, 0, kNodeSim2, 1001)));

  OutFrame f;
  while (a.out.pop(&f)) z.node.on_rx(f.bytes, f.len, -20, 100, 1100);
  TEST_ASSERT_EQUAL_size_t(4, z.out.size());

  while (z.out.pop(&f)) a.node.on_rx(f.bytes, f.len, -21, 99, 1900);
  TEST_ASSERT_EQUAL_size_t(0, a.out.size());
  TEST_ASSERT_TRUE(a.log.last_contains("echo ok"));
  TEST_ASSERT_TRUE(a.log.last_contains("900 ms"));
  TEST_ASSERT_FALSE(a.ids.find(kNodeSim0)->ping.active);
}

void test_a_ping_with_no_echo_is_reported_on_tick() {
  Board a;
  a.ids.add(kNodeSim0, Role::Range);
  a.node.set_ping_timeout_ms(5000);
  a.node.ping(kNodeSim0, 8, false, 0, kNodeBridge, 1000);
  a.node.tick(5999);
  TEST_ASSERT_TRUE(a.ids.find(kNodeSim0)->ping.active);
  a.node.tick(6000);
  TEST_ASSERT_FALSE(a.ids.find(kNodeSim0)->ping.active);
  TEST_ASSERT_TRUE(a.log.last_contains("no echo"));
}

void test_ping_arguments_are_checked() {
  Board a;
  a.ids.add(kNodeSim0, Role::Range);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PingResult::NoIdentity),
                        static_cast<int>(a.node.ping(kNodeSim1, 8, false, 0, 0, 0)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PingResult::BadLength),
                        static_cast<int>(a.node.ping(kNodeSim0, 203, false, 0, 0, 0)));
  // 204 bytes at a 13-byte chunk is 16 fragments - one past spec 11's ceiling.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PingResult::BadChunk),
                        static_cast<int>(a.node.ping(kNodeSim0, 202, false, 13, 0, 0)));
  a.ids.find(kNodeSim0)->enabled = false;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PingResult::Disabled),
                        static_cast<int>(a.node.ping(kNodeSim0, 8, false, 0, 0, 0)));
  TEST_ASSERT_EQUAL_size_t(0, a.out.size());
}

// A set that would not fit is not half-queued: its missing tail would read as RF loss.
void test_a_frame_set_that_does_not_fit_is_not_queued_at_all() {
  Board a;
  a.ids.add(kNodeSim0, Role::Range);
  a.ids.add(kNodeSim1, Role::Range);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PingResult::Ok),
                        static_cast<int>(a.node.ping(kNodeSim0, 202, true, 14, 0, 0)));
  TEST_ASSERT_EQUAL_size_t(15, a.out.size());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PingResult::OutboxFull),
                        static_cast<int>(a.node.ping(kNodeSim1, 40, true, 14, 0, 0)));
  TEST_ASSERT_EQUAL_size_t(15, a.out.size());
}

void test_a_phy_crc_error_is_heard_by_every_enabled_identity() {
  Board b;
  b.ids.add(kNodeSim0, Role::Range);
  b.ids.add(kNodeSim1, Role::Range);
  b.ids.find(kNodeSim1)->enabled = false;
  b.node.on_phy_crc_error();
  TEST_ASSERT_EQUAL_UINT32(1, b.ids.find(kNodeSim0)->counters.rx_crc_err);
  TEST_ASSERT_EQUAL_UINT32(0, b.ids.find(kNodeSim1)->counters.rx_crc_err);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_poll_is_answered_with_node_health);
  RUN_TEST(test_each_identity_hears_every_frame_and_only_the_addressed_one_answers);
  RUN_TEST(test_a_disabled_identity_is_silent);
  RUN_TEST(test_an_identity_speaks_the_version_it_announces);
  RUN_TEST(test_role_range_echoes_a_ping);
  RUN_TEST(test_a_fragmented_ping_is_echoed_at_the_same_chunk);
  RUN_TEST(test_role_health_does_not_echo_a_ping);
  RUN_TEST(test_two_boards_complete_a_fragmented_ping_round_trip);
  RUN_TEST(test_a_ping_with_no_echo_is_reported_on_tick);
  RUN_TEST(test_ping_arguments_are_checked);
  RUN_TEST(test_a_frame_set_that_does_not_fit_is_not_queued_at_all);
  RUN_TEST(test_a_phy_crc_error_is_heard_by_every_enabled_identity);
  return UNITY_END();
}
