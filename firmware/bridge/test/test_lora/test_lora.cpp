// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-16 - the radio's data, and the spec 14 receive ladder up to reassembly. BF-15 - the
// ladder refusing a source the registry does not know. Spec 12.3 media access, the other
// decision lora_task makes, moved to lib/lran-link with its tests on 2026-09-14.
//
// WHAT THIS CANNOT COVER. That the SX1262 is configured as radio_config.h says, that DIO1
// wakes lora_task, that a CAD reads the air. Those need frames out and echoes back on a
// board - begin() succeeding proves nothing about a pin map.
//
// Frames here are built by the codec's encoder and taken apart by the same codec's
// decoder, which on its own would test nothing (lran/frame.h). What is under test is the
// LADDER: which stage a frame stops at, which counter moves, and what is delivered. The
// codec's bytes are the W4 vectors' concern.

#include <unity.h>

#include <cstring>

#include "lran/lran.h"
#include "radio_config.h"
#include "rx_ladder.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

constexpr CtxId kCtx = 0x11223344u;

Header make_hdr(MsgType type, NodeId src, NodeId dst, Seq seq, SchemaId schema) {
  Header h;
  h.type   = type;
  h.src    = src;
  h.dst    = dst;
  h.seq    = seq;
  h.ctx_id = kCtx;
  h.schema = schema;
  return h;
}

size_t encode_or_fail(const Header& h, const uint8_t* payload, size_t n,
                      const EncodeCtx& ectx, uint8_t* buf) {
  size_t len = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(encode(h, payload, n, ectx, buf, kMaxFrame, &len)));
  return len;
}

size_t status_frame(NodeId src, NodeId dst, uint8_t* buf) {
  const uint8_t payload[kMaxPayloadPlain] = {0};
  const size_t  n = fixed_payload_len(MsgType::Status, kSchemaGateLinkStatusV1);
  return encode_or_fail(make_hdr(MsgType::Status, src, dst, 7, kSchemaGateLinkStatusV1),
                        payload, n, EncodeCtx{}, buf);
}

// [ping_flags][n][pattern bytes] - spec 6.6.
size_t ping_payload(uint8_t n, uint8_t* out) {
  out[0] = kPingFlagPatternFill;
  out[1] = n;
  for (uint8_t i = 0; i < n; ++i) out[2 + i] = static_cast<uint8_t>(i * 7 + 1);
  return static_cast<size_t>(n) + 2;
}

// A 42-byte PING payload at a 14-byte chunk is a three-fragment set (spec 6.6.2).
constexpr size_t kChunk = 14;

size_t ping_fragment(NodeId src, Seq seq, const uint8_t* payload, size_t n, uint8_t index,
                     uint8_t* buf) {
  size_t       len = 0;
  const Header h   = make_hdr(MsgType::Ping, src, kNodeBridge, seq, kSchemaNone);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(encode_fragment(h, payload, n, index, kChunk,
                                                         EncodeCtx{}, buf, kMaxFrame, &len)));
  return len;
}

// Deterministic stand-in for HMAC-SHA256. Both sides use it, which is all the ladder
// needs: whether a MAC was checked, and what happens when it does not match.
class FakeMac : public IMac {
 public:
  void hmac_sha256_trunc(const uint8_t* key, size_t key_len, const uint8_t* data,
                         size_t data_len, uint8_t out[kMacLen]) override {
    uint32_t acc = 2166136261u;
    for (size_t i = 0; i < key_len; ++i) acc = (acc ^ key[i]) * 16777619u;
    for (size_t i = 0; i < data_len; ++i) acc = (acc ^ data[i]) * 16777619u;
    for (size_t i = 0; i < kMacLen; ++i) out[i] = static_cast<uint8_t>(acc >> ((i % 4) * 8));
  }
};

class TestKeys : public PeerKeys {
 public:
  TestKeys(NodeId id, uint8_t seed) : id_(id) {
    for (size_t i = 0; i < kNodeKeyLen; ++i) key_[i] = static_cast<uint8_t>(seed + i);
  }
  const uint8_t* key_for(NodeId src) const override { return src == id_ ? key_ : nullptr; }
  bool           is_registered(NodeId src) const override { return src == id_; }
  const uint8_t* key() const { return key_; }

 private:
  NodeId  id_;
  uint8_t key_[kNodeKeyLen];
};

// Registers every source and holds no key. The stage tests below are about the ladder, not
// the registry; test_registry covers the registry, and the tests at the end of this file
// cover the ladder refusing what the registry does not know.
class AnySource : public PeerKeys {
 public:
  const uint8_t* key_for(NodeId) const override { return nullptr; }
  bool           is_registered(NodeId) const override { return true; }
};

AnySource g_any;

// An authenticated COMMAND addressed to the bridge. No real node sends one - every
// authenticated type is bridge -> node (spec 9.2) - which is exactly why the bridge must
// refuse one it cannot verify rather than pass it on.
size_t command_frame(IMac* mac, const uint8_t* key, uint8_t* buf) {
  uint8_t payload[8] = {0};
  payload[0]         = static_cast<uint8_t>(Cmd::Open);
  EncodeCtx ectx;
  ectx.mac      = mac;
  ectx.node_key = key;
  return encode_or_fail(make_hdr(MsgType::Command, kNodeGateLink, kNodeBridge, 1, kSchemaNone),
                        payload, fixed_payload_len(MsgType::Command, kSchemaNone), ectx, buf);
}

}  // namespace

// ---------------------------------------------------------------------------
// radio_config.h
// ---------------------------------------------------------------------------

// Impl Plan 10.8.1's LRAN_PROFILE_HELTEC, value for value.
void test_heltec_pins_match_impl_plan_10_8_1() {
  TEST_ASSERT_EQUAL_INT8(8, kHeltecV3Radio.nss);
  TEST_ASSERT_EQUAL_INT8(12, kHeltecV3Radio.rst);
  TEST_ASSERT_EQUAL_INT8(13, kHeltecV3Radio.busy);
  TEST_ASSERT_EQUAL_INT8(14, kHeltecV3Radio.dio1);
  TEST_ASSERT_EQUAL_INT8(9, kHeltecV3Radio.sck);
  TEST_ASSERT_EQUAL_INT8(11, kHeltecV3Radio.miso);
  TEST_ASSERT_EQUAL_INT8(10, kHeltecV3Radio.mosi);
  TEST_ASSERT_EQUAL_INT8(kPinNone, kHeltecV3Radio.rf_sw);
}

// The two settings that fail silently on this board.
void test_heltec_tcxo_and_rf_switch_are_set() {
  TEST_ASSERT_EQUAL_UINT16(1800, kHeltecV3Radio.tcxo_mv);
  TEST_ASSERT_TRUE(kHeltecV3Radio.dio2_as_rf_switch);
}

// spec 12.1 - D1's working point, closed 2026-09-10.
void test_phy_is_the_d1_working_point() {
  TEST_ASSERT_EQUAL_UINT32(917400000u, kPhy.freq_hz);
  TEST_ASSERT_EQUAL_UINT16(1250, kPhy.bw_khz10);
  TEST_ASSERT_EQUAL_UINT8(9, kPhy.sf);
  TEST_ASSERT_EQUAL_UINT8(5, kPhy.cr_denom);
  TEST_ASSERT_EQUAL_INT8(-4, kPhy.conducted_dbm);
  TEST_ASSERT_EQUAL_UINT8(30, kPhy.antenna_gain_dbi10);
  TEST_ASSERT_EQUAL_UINT8(0x12, kPhy.sync_word);
}

void test_eirp_is_within_the_d33_ceiling() {
  TEST_ASSERT_TRUE(kPhy.conducted_dbm * 10 + kPhy.antenna_gain_dbi10 <= kEirpCeilingDbm10);
}

// The collision check must be able to fail, or the static_assert beside the table
// proves nothing.
void test_pin_conflict_check_detects_a_collision() {
  TEST_ASSERT_FALSE(has_pin_conflict(kHeltecV3Radio, kHeltecV3Ui));
  RadioPins clash = kHeltecV3Radio;
  clash.busy      = kHeltecV3Ui.sda;
  TEST_ASSERT_TRUE(has_pin_conflict(clash, kHeltecV3Ui));
}

// ---------------------------------------------------------------------------
// rx_ladder.h - spec 14 stages 1 to 10
// ---------------------------------------------------------------------------

void test_a_status_frame_is_delivered_whole() {
  Counters   c;
  RxLadder   ladder(&c);
  ladder.set_auth(nullptr, &g_any);
  uint8_t    buf[kMaxFrame];
  RxDelivery d;

  const size_t len = status_frame(kNodeGateLink, kNodeBridge, buf);
  TEST_ASSERT_TRUE(ladder.accept(buf, len, 0, &d));
  TEST_ASSERT_EQUAL_UINT8(kNodeGateLink, d.hdr.src);
  TEST_ASSERT_EQUAL_UINT8(kSchemaGateLinkStatusV1, d.hdr.schema);
  TEST_ASSERT_EQUAL_size_t(fixed_payload_len(MsgType::Status, kSchemaGateLinkStatusV1),
                           d.payload_len);
  TEST_ASSERT_EQUAL_UINT8(1, d.fragments);
  TEST_ASSERT_TRUE(d.payload == buf + kHdrLen);
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_frames);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
  TEST_ASSERT_FALSE(ladder.any_set_active());
}

// Stage 1 is the driver's, and it is rx_crc_err - never rx_bad_crc.
void test_a_phy_crc_error_is_counted_as_stage_1() {
  Counters c;
  RxLadder ladder(&c);
  ladder.on_phy_crc_error();
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_crc_err);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_bad_crc);
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_frames);
}

void test_a_runt_is_counted_and_not_delivered() {
  Counters      c;
  RxLadder      ladder(&c);
  const uint8_t buf[5] = {2, 4, 1, 0, 0};
  RxDelivery    d;
  TEST_ASSERT_FALSE(ladder.accept(buf, sizeof(buf), 0, &d));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_runt);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Runt), static_cast<int>(ladder.last_status()));
}

// Stage 2a. The SX1262 accepts up to 255 bytes, so a foreign transmitter can hand the
// bridge a frame longer than LRAN_MAX_FRAME, and it has its own counter.
void test_an_oversize_frame_is_counted_and_not_delivered() {
  Counters      c;
  RxLadder      ladder(&c);
  const uint8_t buf[255] = {0};
  RxDelivery    d;
  TEST_ASSERT_FALSE(ladder.accept(buf, sizeof(buf), 0, &d));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_oversize);
}

void test_a_frame_for_another_node_is_counted_and_not_delivered() {
  Counters     c;
  RxLadder     ladder(&c);
  uint8_t      buf[kMaxFrame];
  RxDelivery   d;
  const size_t len = status_frame(kNodeSim0, kNodeGateLink, buf);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 0, &d));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_not_addressed);
}

// spec 11.2 - concatenation in index order, whatever the arrival order.
void test_a_fragmented_ping_reassembles_out_of_order() {
  Counters   c;
  RxLadder   ladder(&c);
  ladder.set_auth(nullptr, &g_any);
  uint8_t    payload[kMaxPayloadPlain];
  const size_t n = ping_payload(40, payload);
  uint8_t    buf[kMaxFrame];
  RxDelivery d;

  size_t len = ping_fragment(kNodeSim0, 9, payload, n, 2, buf);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 0, &d));
  TEST_ASSERT_TRUE(ladder.any_set_active());
  len = ping_fragment(kNodeSim0, 9, payload, n, 0, buf);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 1, &d));
  len = ping_fragment(kNodeSim0, 9, payload, n, 1, buf);
  TEST_ASSERT_TRUE(ladder.accept(buf, len, 2, &d));

  TEST_ASSERT_EQUAL_size_t(n, d.payload_len);
  TEST_ASSERT_EQUAL_MEMORY(payload, d.payload, n);
  TEST_ASSERT_EQUAL_UINT8(3, d.fragments);
  TEST_ASSERT_EQUAL_UINT8(kNodeSim0, d.hdr.src);
  TEST_ASSERT_FALSE(ladder.any_set_active());
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// spec 11.2 - the v0.6 finding. A peer's periodic STATUS arriving mid-set is delivered
// and leaves that peer's set untouched.
void test_a_single_frame_mid_set_leaves_the_set_intact() {
  Counters   c;
  RxLadder   ladder(&c);
  ladder.set_auth(nullptr, &g_any);
  uint8_t    payload[kMaxPayloadPlain];
  const size_t n = ping_payload(40, payload);
  uint8_t    buf[kMaxFrame];
  RxDelivery d;

  size_t len = ping_fragment(kNodeSim0, 9, payload, n, 0, buf);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 0, &d));

  len = status_frame(kNodeSim0, kNodeBridge, buf);
  TEST_ASSERT_TRUE(ladder.accept(buf, len, 1, &d));
  TEST_ASSERT_TRUE(ladder.any_set_active());

  len = ping_fragment(kNodeSim0, 9, payload, n, 1, buf);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 2, &d));
  len = ping_fragment(kNodeSim0, 9, payload, n, 2, buf);
  TEST_ASSERT_TRUE(ladder.accept(buf, len, 3, &d));
  TEST_ASSERT_EQUAL_MEMORY(payload, d.payload, n);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_abandoned);
}

// spec 11.3 - one set per peer. Two peers interleaving never abandon each other.
void test_two_peers_interleave_without_abandoning() {
  Counters   c;
  RxLadder   ladder(&c);
  ladder.set_auth(nullptr, &g_any);
  uint8_t    payload[kMaxPayloadPlain];
  const size_t n = ping_payload(40, payload);
  uint8_t    buf[kMaxFrame];
  RxDelivery d;
  int        delivered = 0;

  const NodeId peers[] = {kNodeSim0, kNodeSim1};
  for (uint8_t index = 0; index < 3; ++index) {
    for (NodeId src : peers) {
      const size_t len = ping_fragment(src, 5, payload, n, index, buf);
      if (ladder.accept(buf, len, index, &d)) {
        ++delivered;
        TEST_ASSERT_EQUAL_MEMORY(payload, d.payload, n);
      }
    }
  }
  TEST_ASSERT_EQUAL_INT(2, delivered);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_abandoned);
}

// Nine peers with live sets and eight slots: the least recently used set is displaced,
// and the displacement is counted as an abandonment, never as a timeout.
void test_slot_exhaustion_abandons_the_oldest_set_and_counts() {
  Counters   c;
  RxLadder   ladder(&c);
  ladder.set_auth(nullptr, &g_any);
  uint8_t    payload[kMaxPayloadPlain];
  const size_t n = ping_payload(40, payload);
  uint8_t    buf[kMaxFrame];
  RxDelivery d;

  for (uint8_t i = 0; i < kReassemblySlots; ++i) {
    const size_t len = ping_fragment(static_cast<NodeId>(0x10 + i), 1, payload, n, 0, buf);
    TEST_ASSERT_FALSE(ladder.accept(buf, len, i, &d));
  }
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_abandoned);

  const size_t len = ping_fragment(0x30, 1, payload, n, 0, buf);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 100, &d));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_reassembly_abandoned);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_timeout);
}

// spec 11.2 - tick() expires a set whose remaining fragments never arrive.
void test_an_incomplete_set_times_out_on_tick() {
  Counters   c;
  RxLadder   ladder(&c);
  ladder.set_auth(nullptr, &g_any);
  uint8_t    payload[kMaxPayloadPlain];
  const size_t n = ping_payload(40, payload);
  uint8_t    buf[kMaxFrame];
  RxDelivery d;

  const size_t len = ping_fragment(kNodeSim2, 3, payload, n, 0, buf);
  ladder.accept(buf, len, 1000, &d);

  ladder.tick(1000 + kDefaultFragTimeoutMs - 1);
  TEST_ASSERT_TRUE(ladder.any_set_active());
  ladder.tick(1000 + kDefaultFragTimeoutMs);
  TEST_ASSERT_FALSE(ladder.any_set_active());
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_reassembly_timeout);
}

// Root rule 8.
void test_the_reassembly_timeout_is_runtime_settable() {
  Counters   c;
  RxLadder   ladder(&c);
  ladder.set_auth(nullptr, &g_any);
  uint8_t    payload[kMaxPayloadPlain];
  const size_t n = ping_payload(40, payload);
  uint8_t    buf[kMaxFrame];
  RxDelivery d;

  ladder.set_frag_timeout_ms(100);
  const size_t len = ping_fragment(kNodeSim2, 3, payload, n, 0, buf);
  ladder.accept(buf, len, 0, &d);
  ladder.tick(100);
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_reassembly_timeout);
}

// spec 11.2 - an echo of a completed set's fragment is late, counted outside
// rx_dropped, and never delivered a second time.
void test_a_late_fragment_is_not_delivered_twice() {
  Counters   c;
  RxLadder   ladder(&c);
  ladder.set_auth(nullptr, &g_any);
  uint8_t    payload[kMaxPayloadPlain];
  const size_t n = ping_payload(40, payload);
  uint8_t    buf[kMaxFrame];
  RxDelivery d;

  for (uint8_t index = 0; index < 3; ++index) {
    const size_t len = ping_fragment(kNodeSim3, 4, payload, n, index, buf);
    ladder.accept(buf, len, index, &d);
  }
  const size_t len = ping_fragment(kNodeSim3, 4, payload, n, 1, buf);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 10, &d));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_frag_late);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// THE HOLE THE CODEC LEAVES. With no key the codec returns Ok and mac_verified false; the
// ladder must refuse the frame and count it, not deliver an unverified COMMAND.
void test_an_authenticated_frame_without_keys_is_refused() {
  Counters     c;
  RxLadder     ladder(&c);
  FakeMac      mac;
  TestKeys     keys(kNodeGateLink, 0x40);
  uint8_t      buf[kMaxFrame];
  RxDelivery   d;
  const size_t len = command_frame(&mac, keys.key(), buf);

  TEST_ASSERT_FALSE(ladder.accept(buf, len, 0, &d));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_rejected_mac);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::RejectedMac),
                        static_cast<int>(ladder.last_status()));

  // A key with no MAC implementation is still an unverified frame.
  ladder.set_auth(nullptr, &keys);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 0, &d));
  TEST_ASSERT_EQUAL_UINT32(2, c.rx_rejected_mac);
}

void test_an_authenticated_frame_with_the_wrong_key_is_refused() {
  Counters     c;
  RxLadder     ladder(&c);
  FakeMac      mac;
  TestKeys     sender(kNodeGateLink, 0x40);
  TestKeys     other(kNodeGateLink, 0x90);
  uint8_t      buf[kMaxFrame];
  RxDelivery   d;
  const size_t len = command_frame(&mac, sender.key(), buf);

  ladder.set_auth(&mac, &other);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 0, &d));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_rejected_mac);
}

void test_an_authenticated_frame_with_its_key_is_delivered_verified() {
  Counters     c;
  RxLadder     ladder(&c);
  FakeMac      mac;
  TestKeys     keys(kNodeGateLink, 0x40);
  uint8_t      buf[kMaxFrame];
  RxDelivery   d;
  const size_t len = command_frame(&mac, keys.key(), buf);

  ladder.set_auth(&mac, &keys);
  TEST_ASSERT_TRUE(ladder.accept(buf, len, 0, &d));
  TEST_ASSERT_TRUE(d.mac_verified);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// ---------------------------------------------------------------------------
// rx_ladder.h - sources the registry does not know (BF-15)
// ---------------------------------------------------------------------------

// Counted as a bridge diagnostic and kept out of spec 14.1's counters: spec 14 has no stage
// for it, and rx_dropped must not move for a discard the specification does not define.
void test_an_unregistered_source_is_refused_and_counted() {
  Counters     c;
  RxLadder     ladder(&c);
  TestKeys     keys(kNodeGateLink, 0x40);
  uint8_t      buf[kMaxFrame];
  RxDelivery   d;
  ladder.set_auth(nullptr, &keys);

  size_t len = status_frame(kNodeGateLink, kNodeBridge, buf);
  TEST_ASSERT_TRUE(ladder.accept(buf, len, 0, &d));
  TEST_ASSERT_FALSE(ladder.last_unregistered_src());

  len = status_frame(kNodeWellLink, kNodeBridge, buf);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 1, &d));
  TEST_ASSERT_TRUE(ladder.last_unregistered_src());
  TEST_ASSERT_EQUAL_UINT32(1, ladder.unregistered_src());
  TEST_ASSERT_EQUAL_UINT32(2, c.rx_frames);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());

  // The flag describes the last frame only.
  len = status_frame(kNodeGateLink, kNodeBridge, buf);
  TEST_ASSERT_TRUE(ladder.accept(buf, len, 2, &d));
  TEST_ASSERT_FALSE(ladder.last_unregistered_src());
}

// Before registration is set, nothing is registered.
void test_a_ladder_with_no_registry_refuses_every_source() {
  Counters     c;
  RxLadder     ladder(&c);
  uint8_t      buf[kMaxFrame];
  RxDelivery   d;
  const size_t len = status_frame(kNodeGateLink, kNodeBridge, buf);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 0, &d));
  TEST_ASSERT_EQUAL_UINT32(1, ladder.unregistered_src());
}

// The stages spec 14 defines still run for an unregistered sender, and still count.
void test_an_unregistered_source_still_counts_the_earlier_stages() {
  Counters     c;
  RxLadder     ladder(&c);
  TestKeys     keys(kNodeGateLink, 0x40);
  uint8_t      buf[kMaxFrame];
  RxDelivery   d;
  ladder.set_auth(nullptr, &keys);

  const size_t len = status_frame(kNodeWellLink, kNodeSim0, buf);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 0, &d));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_not_addressed);
  TEST_ASSERT_EQUAL_UINT32(0, ladder.unregistered_src());
}

// spec 11.3 - THE REASON THE CHECK SITS BEFORE STAGE 10. Every slot holds a registered
// peer's live set, and an unprovisioned transmitter's fragment neither takes a slot nor
// displaces one of them.
void test_an_unregistered_fragment_takes_no_slot_and_displaces_nothing() {
  // Registers 0x10 .. 0x10 + kReassemblySlots - 1, and nothing else.
  class SlotPeers : public PeerKeys {
   public:
    const uint8_t* key_for(NodeId) const override { return nullptr; }
    bool           is_registered(NodeId src) const override {
      return src >= 0x10 && static_cast<size_t>(src) < 0x10 + kReassemblySlots;
    }
  } peers;

  Counters   c;
  RxLadder   ladder(&c);
  uint8_t    payload[kMaxPayloadPlain];
  const size_t n = ping_payload(40, payload);
  uint8_t    buf[kMaxFrame];
  RxDelivery d;
  ladder.set_auth(nullptr, &peers);

  for (uint8_t i = 0; i < kReassemblySlots; ++i) {
    const size_t len = ping_fragment(static_cast<NodeId>(0x10 + i), 1, payload, n, 0, buf);
    TEST_ASSERT_FALSE(ladder.accept(buf, len, i, &d));
  }

  size_t len = ping_fragment(0x30, 1, payload, n, 0, buf);
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 100, &d));
  TEST_ASSERT_TRUE(ladder.last_unregistered_src());
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_abandoned);

  // Every registered set is still live and still completes.
  int delivered = 0;
  for (uint8_t index = 1; index < 3; ++index) {
    for (uint8_t i = 0; i < kReassemblySlots; ++i) {
      len = ping_fragment(static_cast<NodeId>(0x10 + i), 1, payload, n, index, buf);
      if (ladder.accept(buf, len, 200, &d)) ++delivered;
    }
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(kReassemblySlots), delivered);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_heltec_pins_match_impl_plan_10_8_1);
  RUN_TEST(test_heltec_tcxo_and_rf_switch_are_set);
  RUN_TEST(test_phy_is_the_d1_working_point);
  RUN_TEST(test_eirp_is_within_the_d33_ceiling);
  RUN_TEST(test_pin_conflict_check_detects_a_collision);

  RUN_TEST(test_a_status_frame_is_delivered_whole);
  RUN_TEST(test_a_phy_crc_error_is_counted_as_stage_1);
  RUN_TEST(test_a_runt_is_counted_and_not_delivered);
  RUN_TEST(test_an_oversize_frame_is_counted_and_not_delivered);
  RUN_TEST(test_a_frame_for_another_node_is_counted_and_not_delivered);
  RUN_TEST(test_a_fragmented_ping_reassembles_out_of_order);
  RUN_TEST(test_a_single_frame_mid_set_leaves_the_set_intact);
  RUN_TEST(test_two_peers_interleave_without_abandoning);
  RUN_TEST(test_slot_exhaustion_abandons_the_oldest_set_and_counts);
  RUN_TEST(test_an_incomplete_set_times_out_on_tick);
  RUN_TEST(test_the_reassembly_timeout_is_runtime_settable);
  RUN_TEST(test_a_late_fragment_is_not_delivered_twice);
  RUN_TEST(test_an_authenticated_frame_without_keys_is_refused);
  RUN_TEST(test_an_authenticated_frame_with_the_wrong_key_is_refused);
  RUN_TEST(test_an_authenticated_frame_with_its_key_is_delivered_verified);

  RUN_TEST(test_an_unregistered_source_is_refused_and_counted);
  RUN_TEST(test_a_ladder_with_no_registry_refuses_every_source);
  RUN_TEST(test_an_unregistered_source_still_counts_the_earlier_stages);
  RUN_TEST(test_an_unregistered_fragment_takes_no_slot_and_displaces_nothing);
  return UNITY_END();
}
