// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// P5 - fragmentation and reassembly. Spec 11.
//
// The clock is injected, so a five-second timeout is tested in microseconds. A
// reassembly-timeout test that had to wait five real seconds would not get written,
// and so the path would ship untested on hardware with no OTA.

#include <unity.h>

#include "lran/lran.h"

using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

Frame make_fragment(const uint8_t* data, size_t len, uint8_t index, uint8_t total,
                    MsgType type = MsgType::Ping, SchemaId schema = kSchemaNone) {
  Frame f;
  f.hdr.type   = type;
  f.hdr.src    = kNodeGateLink;
  f.hdr.dst    = kNodeBridge;
  f.hdr.seq    = 0x4242;
  f.hdr.ctx_id = 0xCAFEBABEu;
  f.hdr.schema = schema;
  f.hdr.set_frag(index, total);
  f.payload     = data;
  f.payload_len = len;
  return f;
}

}  // namespace

void test_frag_nibble_packing() {
  Header h;
  h.set_frag(3, 5);
  TEST_ASSERT_EQUAL_HEX8(0x35, h.frag);
  TEST_ASSERT_EQUAL_UINT8(3, h.frag_index());
  TEST_ASSERT_EQUAL_UINT8(5, h.frag_total());
  h.set_frag(0, 1);
  TEST_ASSERT_EQUAL_HEX8(0x01, h.frag);  // spec 5.6 - a single unfragmented frame
}

void test_single_frame_completes_immediately() {
  Counters c;
  Reassembler r(&c);
  const uint8_t data[] = {1, 2, 3, 4};
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(data, 4, 0, 1), 0));
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL_UINT32(4, r.len());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(data, r.data(), 4);
}

void test_two_fragments_in_order() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {0x10, 0x11, 0x12};
  const uint8_t b[] = {0x20, 0x21};
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(a, 3, 0, 2), 100));
  TEST_ASSERT_FALSE(r.complete());
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(b, 2, 1, 2), 110));
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL_UINT32(5, r.len());
  const uint8_t want[] = {0x10, 0x11, 0x12, 0x20, 0x21};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, r.data(), 5);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// Fragments are concatenated in INDEX order regardless of arrival order, and the
// pieces need not be equal sizes.
void test_two_fragments_reversed_and_uneven() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {0xAA};
  const uint8_t b[] = {0xB0, 0xB1, 0xB2, 0xB3};
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(b, 4, 1, 2), 0));
  TEST_ASSERT_FALSE(r.complete());
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(a, 1, 0, 2), 0));
  TEST_ASSERT_TRUE(r.complete());
  const uint8_t want[] = {0xAA, 0xB0, 0xB1, 0xB2, 0xB3};
  TEST_ASSERT_EQUAL_UINT32(5, r.len());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, r.data(), 5);
}

// spec 11 - the 15-fragment maximum, delivered fully reversed.
void test_fifteen_fragments_reversed() {
  Counters c;
  Reassembler r(&c);
  uint8_t chunk[15][4];
  for (int i = 0; i < 15; ++i) {
    for (int j = 0; j < 4; ++j) chunk[i][j] = static_cast<uint8_t>(i * 4 + j);
  }
  uint32_t now = 1000;
  for (int i = 14; i >= 0; --i) {
    // Time only ever moves forward. Feeding a decreasing clock would underflow the
    // unsigned age arithmetic and expire the set on every fragment - which is what
    // the wrap test at test_timeout_survives_millis_wrap exercises deliberately.
    now += 10;
    TEST_ASSERT_EQUAL(Status::Ok,
                      r.accept(make_fragment(chunk[i], 4, static_cast<uint8_t>(i), 15),
                               now));
    TEST_ASSERT_EQUAL(i == 0, r.complete());
  }
  TEST_ASSERT_EQUAL_UINT32(60, r.len());
  for (uint8_t i = 0; i < 60; ++i) TEST_ASSERT_EQUAL_HEX8(i, r.data()[i]);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// spec 12.3 - a retransmit after a CAD backoff is ordinary traffic, not an error.
void test_duplicate_fragment_is_idempotent() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {1, 2};
  const uint8_t b[] = {3, 4};
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(a, 2, 0, 2), 0));
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(a, 2, 0, 2), 5));
  TEST_ASSERT_FALSE(r.complete());
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(b, 2, 1, 2), 10));
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL_UINT32(4, r.len());
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// spec 11 - incomplete sets expire after frag_reassembly_timeout_ms and are counted.
void test_reassembly_timeout_expires_and_counts() {
  Counters c;
  Reassembler r(&c);
  TEST_ASSERT_EQUAL_UINT32(5000, r.timeout_ms());  // spec 11 default

  const uint8_t a[] = {1, 2};
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(a, 2, 0, 2), 1000));
  TEST_ASSERT_TRUE(r.active());

  r.tick(5999);
  TEST_ASSERT_TRUE(r.active());
  TEST_ASSERT_EQUAL_UINT32(0, c.reassembly_timeout);

  r.tick(6000);  // exactly at the boundary
  TEST_ASSERT_FALSE(r.active());
  TEST_ASSERT_EQUAL_UINT32(1, c.reassembly_timeout);
  TEST_ASSERT_FALSE(r.complete());
}

// Repo rule 8 - no timing constant is fixed at compile time in a node that cannot be
// reflashed without a walk to the gate.
void test_timeout_is_runtime_configurable() {
  Counters c;
  Reassembler r(&c);
  r.set_timeout_ms(250);
  const uint8_t a[] = {1};
  r.accept(make_fragment(a, 1, 0, 2), 0);
  r.tick(249);
  TEST_ASSERT_TRUE(r.active());
  r.tick(250);
  TEST_ASSERT_FALSE(r.active());
  TEST_ASSERT_EQUAL_UINT32(1, c.reassembly_timeout);
}

// The millisecond counter wraps through zero after ~49 days of uptime. Unsigned
// subtraction means an in-flight set must not be resurrected by the wrap.
void test_timeout_survives_millis_wrap() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {1};
  const uint32_t near_wrap = 0xFFFFF000u;
  r.accept(make_fragment(a, 1, 0, 2), near_wrap);
  r.tick(near_wrap + 4999);
  TEST_ASSERT_TRUE(r.active());
  r.tick(near_wrap + 5000);  // wrapped past zero
  TEST_ASSERT_FALSE(r.active());
  TEST_ASSERT_EQUAL_UINT32(1, c.reassembly_timeout);
}

// spec 11 - a set exceeding LRAN_MAX_SCHEMA_PAYLOAD on reassembly is discarded with
// ERROR(FRAGMENT_OVERFLOW). Checked at the fragment that overruns.
void test_fragment_overflow_on_schema_cap() {
  Counters c;
  Reassembler r(&c);
  uint8_t big[150] = {};
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(big, 150, 0, 2, MsgType::Status,
                                                       kSchemaGateLinkStatusV1), 0));
  // 150 + 150 = 300 > 196
  TEST_ASSERT_EQUAL(Status::FragmentOverflow,
                    r.accept(make_fragment(big, 150, 1, 2, MsgType::Status,
                                           kSchemaGateLinkStatusV1), 1));
  TEST_ASSERT_EQUAL_UINT32(1, c.fragment_overflow);
  TEST_ASSERT_FALSE(r.active());
}

// spec 11 - PING is capped at LRAN_MAX_PAYLOAD_PLAIN rather than the schema cap,
// since it carries no schema and never carries a MAC.
void test_ping_uses_the_plain_cap() {
  TEST_ASSERT_EQUAL_UINT32(kMaxPayloadPlain, reassembly_cap(MsgType::Ping));
  TEST_ASSERT_EQUAL_UINT32(kMaxSchemaPayload, reassembly_cap(MsgType::Status));

  Counters c;
  Reassembler r(&c);
  uint8_t big[102] = {};
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(big, 102, 0, 2), 0));
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(big, 102, 1, 2), 0));
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL_UINT32(204, r.len());  // exactly the cap
}

// spec 5.6 / 14 stage 5b - the two malformed `frag` bytes have different diagnoses
// and land on different counters. A total of 0 means the sender's framing is broken;
// an index at or past the total means one fragment has nowhere to land.
void test_bad_fragment_nibbles_rejected() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {1};
  Frame f = make_fragment(a, 1, 0, 1);
  f.hdr.frag = 0x00;  // total = 0
  TEST_ASSERT_EQUAL(Status::BadFrag, r.accept(f, 0));
  f.hdr.frag = 0x32;  // index 3 of 2
  TEST_ASSERT_EQUAL(Status::FragmentOverflow, r.accept(f, 0));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_bad_frag);
  TEST_ASSERT_EQUAL_UINT32(1, c.fragment_overflow);
}

// Only one set is held. A new key abandons the old one, and the abandonment is
// counted - never a silent discard.
void test_new_set_abandons_incomplete_one() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {1, 2};
  r.accept(make_fragment(a, 2, 0, 2), 0);

  Frame other = make_fragment(a, 2, 0, 2);
  other.hdr.seq = 0x9999;  // a different set from the same peer
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(other, 1));
  TEST_ASSERT_EQUAL_UINT32(1, c.reassembly_timeout);
  TEST_ASSERT_EQUAL_HEX16(0x9999, r.seq());
  TEST_ASSERT_FALSE(r.complete());
}

// spec 11 - all fragments of a set share the total.
void test_inconsistent_total_rejected() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {1, 2};
  r.accept(make_fragment(a, 2, 0, 3), 0);
  Frame f = make_fragment(a, 2, 1, 2);  // same key, different total
  TEST_ASSERT_EQUAL(Status::FragmentOverflow, r.accept(f, 0));
  TEST_ASSERT_EQUAL_UINT32(1, c.fragment_overflow);
}

// End to end: a PING payload split, encoded, decoded and reassembled, with the
// spec 6.6.3 pattern verified across the join.
void test_fragmented_ping_end_to_end() {
  const Seq seq = 0x00AB;
  uint8_t echo[120];
  msg::ping_fill_pattern(seq, echo, sizeof(echo));

  uint8_t payload[kMaxPayloadPlain];
  size_t  plen = 0;
  msg::Ping p{kPingFlagPatternFill, static_cast<uint8_t>(sizeof(echo)), echo};
  TEST_ASSERT_EQUAL(Status::Ok, msg::serialize(p, payload, sizeof(payload), &plen));

  Counters      c;
  Reassembler   r(&c);
  const size_t  split = 50;
  const size_t  parts[2][2] = {{0, split}, {split, plen - split}};

  DecodeCtx d;
  d.self     = kNodeBridge;
  d.counters = &c;
  EncodeCtx ec;

  for (int i = 1; i >= 0; --i) {  // deliver reversed
    Header h;
    h.type   = MsgType::Ping;
    h.src    = kNodeGateLink;
    h.dst    = kNodeBridge;
    h.seq    = seq;
    h.ctx_id = 0x12345678u;
    h.set_frag(static_cast<uint8_t>(i), 2);

    uint8_t buf[kMaxFrame];
    size_t  n = 0;
    TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload + parts[i][0], parts[i][1], ec,
                                         buf, sizeof(buf), &n));
    Frame f;
    TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, d, &f));
    TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, d, &f));
    TEST_ASSERT_EQUAL(Status::Ok, r.accept(f, 500));
  }

  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL_UINT32(plen, r.len());

  msg::Ping back;
  TEST_ASSERT_EQUAL(Status::Ok, msg::deserialize(r.data(), r.len(), &back));
  TEST_ASSERT_EQUAL_UINT8(sizeof(echo), back.n);
  size_t bad = 0;
  TEST_ASSERT_TRUE(msg::ping_check_pattern(seq, back.data, back.n, &bad));
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_frag_nibble_packing);
  RUN_TEST(test_single_frame_completes_immediately);
  RUN_TEST(test_two_fragments_in_order);
  RUN_TEST(test_two_fragments_reversed_and_uneven);
  RUN_TEST(test_fifteen_fragments_reversed);
  RUN_TEST(test_duplicate_fragment_is_idempotent);
  RUN_TEST(test_reassembly_timeout_expires_and_counts);
  RUN_TEST(test_timeout_is_runtime_configurable);
  RUN_TEST(test_timeout_survives_millis_wrap);
  RUN_TEST(test_fragment_overflow_on_schema_cap);
  RUN_TEST(test_ping_uses_the_plain_cap);
  RUN_TEST(test_bad_fragment_nibbles_rejected);
  RUN_TEST(test_new_set_abandons_incomplete_one);
  RUN_TEST(test_inconsistent_total_rejected);
  RUN_TEST(test_fragmented_ping_end_to_end);
  return UNITY_END();
}
