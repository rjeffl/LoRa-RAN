// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// P5 - fragmentation and reassembly. Spec 11.
//
// The clock is injected, so a five-second timeout is tested in microseconds. A
// reassembly-timeout test that had to wait five real seconds would not get written,
// and so the path would ship untested on hardware with no OTA.

#ifdef ARDUINO
#include <Arduino.h>
#endif

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
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_timeout);

  r.tick(6000);  // exactly at the boundary
  TEST_ASSERT_FALSE(r.active());
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_reassembly_timeout);
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
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_reassembly_timeout);
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
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_reassembly_timeout);
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
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_fragment_overflow);
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
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_fragment_overflow);
}

// spec 11.3 - only one set is held, so a new key displaces the old one. The
// displacement is counted rx_reassembly_abandoned, NOT rx_reassembly_timeout: a timeout
// means the RF path dropped a fragment, an abandonment means the receiver is
// undersized or a peer is interleaving sets. Two diagnoses, two counters. Never a
// silent discard.
void test_displaced_set_counts_abandoned_not_timeout() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {1, 2};
  r.accept(make_fragment(a, 2, 0, 2), 0);

  Frame other = make_fragment(a, 2, 0, 2);
  other.hdr.seq = 0x9999;  // a different set from the same peer
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(other, 1));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_reassembly_abandoned);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_timeout);
  TEST_ASSERT_EQUAL_HEX16(0x9999, r.seq());
  TEST_ASSERT_FALSE(r.complete());

  // Both are drops, so both reach schema 0xF0's rx_dropped.
  TEST_ASSERT_EQUAL_UINT32(1, c.total_dropped());
}

// The other half of the split: a set nobody displaced, that simply ran out of time.
void test_expired_set_counts_timeout_not_abandoned() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {1, 2};
  r.accept(make_fragment(a, 2, 0, 2), 1000);
  r.tick(1000 + kDefaultFragTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_reassembly_timeout);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_abandoned);
  TEST_ASSERT_FALSE(r.active());
}

// spec 11 - all fragments of a set share the total.
void test_inconsistent_total_rejected() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {1, 2};
  r.accept(make_fragment(a, 2, 0, 3), 0);
  Frame f = make_fragment(a, 2, 1, 2);  // same key, different total
  TEST_ASSERT_EQUAL(Status::FragmentOverflow, r.accept(f, 0));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_fragment_overflow);
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

// --- F4 / spec 11.1: sender-side fragmentation is deterministic -------------

// Every fragment except the highest index carries the SAME payload length. The rule
// buys the receiver nothing - spec 11.2 accepts any placement - it buys W4
// everything: without it the Python generator and this codec can both be conformant
// and still emit different bytes for the same payload.
void test_fragment_sizes_uniform_except_last() {
  uint8_t payload[202];
  for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<uint8_t>(i);

  // spec 6.6.2 - a 202-byte echo with frag_chunk = 14 is the full 15-fragment set.
  TEST_ASSERT_EQUAL_UINT8(15, fragment_count(sizeof(payload), 14));

  Header h;
  h.type = MsgType::Ping;
  h.src  = kNodeBridge;
  h.dst  = kNodeGateLink;
  EncodeCtx ec;

  size_t lens[15];
  for (uint8_t i = 0; i < 15; ++i) {
    uint8_t buf[kMaxFrame];
    size_t  n = 0;
    TEST_ASSERT_EQUAL(Status::Ok,
                      encode_fragment(h, payload, sizeof(payload), i, 14, ec, buf,
                                      sizeof(buf), &n));
    lens[i] = n - kHdrLen - kCrcLen;  // PING never carries a MAC
    // The set's index and total are computed by encode_fragment, not by the caller.
    TEST_ASSERT_EQUAL_UINT8(i, static_cast<uint8_t>(buf[10] >> 4));
    TEST_ASSERT_EQUAL_UINT8(15, static_cast<uint8_t>(buf[10] & 0x0F));
  }
  for (uint8_t i = 0; i < 14; ++i) TEST_ASSERT_EQUAL_UINT32(14, lens[i]);
  TEST_ASSERT_EQUAL_UINT32(202 - 14 * 14, lens[14]);  // the remainder, 6 bytes
}

// Fragmentation is a pure function of (payload, type, chunk): the same input twice
// produces identical bytes. This is the property the W4 vectors are compared under.
void test_fragmentation_is_deterministic() {
  uint8_t payload[100];
  for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<uint8_t>(i * 7);

  Header h;
  h.type = MsgType::Ping;
  h.src  = kNodeBridge;
  h.dst  = kNodeGateLink;
  h.seq  = 0x1234;
  EncodeCtx ec;

  const uint8_t total = fragment_count(sizeof(payload), 30);
  TEST_ASSERT_EQUAL_UINT8(4, total);  // ceil(100 / 30)
  for (uint8_t i = 0; i < total; ++i) {
    uint8_t a[kMaxFrame], b[kMaxFrame];
    size_t  na = 0, nb = 0;
    TEST_ASSERT_EQUAL(Status::Ok, encode_fragment(h, payload, sizeof(payload), i, 30,
                                                  ec, a, sizeof(a), &na));
    TEST_ASSERT_EQUAL(Status::Ok, encode_fragment(h, payload, sizeof(payload), i, 30,
                                                  ec, b, sizeof(b), &nb));
    TEST_ASSERT_EQUAL_UINT32(na, nb);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(a, b, na);
  }
}

// spec 11.1 - the default chunk is the maximum payload for the type, so a payload
// that fits one frame produces exactly one fragment.
void test_default_chunk_is_the_type_maximum() {
  TEST_ASSERT_EQUAL_UINT32(kMaxPayloadAuth, default_frag_chunk(MsgType::Config));
  TEST_ASSERT_EQUAL_UINT32(kMaxPayloadPlain, default_frag_chunk(MsgType::Ping));
  TEST_ASSERT_EQUAL_UINT8(1, fragment_count(0, kMaxPayloadPlain));    // empty is one
  TEST_ASSERT_EQUAL_UINT8(1, fragment_count(204, kMaxPayloadPlain));
  TEST_ASSERT_EQUAL_UINT8(2, fragment_count(205, kMaxPayloadPlain));
  TEST_ASSERT_EQUAL_UINT8(15, fragment_count(15, 1));
  TEST_ASSERT_EQUAL_UINT8(0, fragment_count(16, 1));  // past the 15-fragment ceiling
  TEST_ASSERT_EQUAL_UINT8(0, fragment_count(10, 0));  // a chunk of 0 is meaningless
}

// --- F5 / spec 11.2: a duplicate index overwrites ---------------------------

// Retransmission after a CAD backoff and RF echo both produce a duplicate index. It
// is not an error and not a discard: the stored fragment is overwritten and
// rx_frag_duplicate counts the overwrite.
void test_duplicate_fragment_index_overwrites() {
  Counters c;
  Reassembler r(&c);
  const uint8_t first[]  = {0xAA, 0xAA};
  const uint8_t second[] = {0xBB, 0xBB};
  const uint8_t tail[]   = {0xCC};

  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(first, 2, 0, 2), 0));
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(second, 2, 0, 2), 1));
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(tail, 1, 1, 2), 2));
  TEST_ASSERT_TRUE(r.complete());

  const uint8_t want[] = {0xBB, 0xBB, 0xCC};  // the LATER copy wins
  TEST_ASSERT_EQUAL_UINT32(3, r.len());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, r.data(), 3);

  TEST_ASSERT_EQUAL_UINT32(1, c.rx_frag_duplicate);
  // spec 11.2 / 14 - an overwrite is not a discard, so it is NOT summed into
  // rx_dropped. Summing it would inflate the drop count every time the RF path
  // echoed a fragment that was then used successfully.
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// A duplicate of DIFFERING length still overwrites: the peer has contradicted itself
// about a fragment it already sent, and the later bytes win. The reassembled length
// follows the new fragment, not the old one.
void test_duplicate_fragment_of_different_length_overwrites() {
  Counters c;
  Reassembler r(&c);
  const uint8_t first[]  = {0x11, 0x22, 0x33};
  const uint8_t second[] = {0x44};
  const uint8_t tail[]   = {0x99};

  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(first, 3, 0, 2), 0));
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(second, 1, 0, 2), 1));
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(tail, 1, 1, 2), 2));
  TEST_ASSERT_TRUE(r.complete());

  const uint8_t want[] = {0x44, 0x99};
  TEST_ASSERT_EQUAL_UINT32(2, r.len());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, r.data(), 2);
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_frag_duplicate);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// --- F7 / spec 11.4: HEX_REQ and HEX_RSP are single-frame in v1 -------------

void test_fragmented_hex_req_rejected() {
  TEST_ASSERT_FALSE(type_is_fragmentable(MsgType::HexReq));
  TEST_ASSERT_FALSE(type_is_fragmentable(MsgType::HexRsp));
  TEST_ASSERT_FALSE(type_is_fragmentable(MsgType::Command));
  TEST_ASSERT_TRUE(type_is_fragmentable(MsgType::Config));
  TEST_ASSERT_TRUE(type_is_fragmentable(MsgType::Ping));

  // Sender: a refusal to violate spec 11.4, distinct from NotImplemented (a gap in
  // this library) and from MissingMac (a misconfiguration).
  uint8_t payload[8] = {0, 6, ':', '7', 'F', '0', 'E', 'D'};
  Header h;
  h.type = MsgType::HexReq;
  h.src  = kNodeBridge;
  h.dst  = kNodeGateLink;
  h.set_frag(0, 2);
  EncodeCtx ec;
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  TEST_ASSERT_EQUAL(Status::NotFragmentable,
                    encode(h, payload, sizeof(payload), ec, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(0, n);

  h.type = MsgType::HexRsp;
  TEST_ASSERT_EQUAL(Status::NotFragmentable,
                    encode(h, payload, sizeof(payload), ec, buf, sizeof(buf), &n));

  // encode_fragment refuses the same case before laying out any bytes.
  uint8_t big[300] = {};
  h.type = MsgType::HexReq;
  TEST_ASSERT_EQUAL(Status::NotFragmentable,
                    encode_fragment(h, big, 250, 0, 128, ec, buf, sizeof(buf), &n));

  // Receiver: a fragmented HEX set never reaches the reassembly buffers.
  Counters c;
  Reassembler r(&c);
  // spec 14 stage 8a - the wire answer is ERROR(BAD_LENGTH) per spec 11.4, but the
  // counter is rx_not_fragmentable. "A peer fragmented a type that may not be
  // fragmented" and "a peer's encoder got a length wrong" have different fixes, and
  // at the gate the counter is the whole diagnosis.
  Frame f = make_fragment(payload, 8, 0, 2, MsgType::HexReq);
  TEST_ASSERT_EQUAL(Status::NotFragmentable, r.accept(f, 0));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_not_fragmentable);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_bad_length);
  TEST_ASSERT_FALSE(r.active());
}


// --- L1 / spec 11.2: a fragment matching the last completed set -------------

namespace {
// Delivers a complete 2-fragment set with the given seq. Returns the reassembler's
// state ready for the next case.
void deliver_set(Reassembler* r, Seq seq, const uint8_t* a, const uint8_t* b,
                 uint32_t* now) {
  Frame f0 = make_fragment(a, 2, 0, 2);
  Frame f1 = make_fragment(b, 2, 1, 2);
  f0.hdr.seq = seq;
  f1.hdr.seq = seq;
  *now += 10;
  r->accept(f0, *now);
  *now += 10;
  r->accept(f1, *now);
}
}  // namespace

// A late echo is discarded, counted, and does NOT open a set.
void test_late_fragment_after_completion_counted() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {0x10, 0x11};
  const uint8_t b[] = {0x20, 0x21};
  uint32_t now = 1000;

  deliver_set(&r, 0x4242, a, b, &now);
  TEST_ASSERT_TRUE(r.complete());

  // The echo: index 0 of the set that just completed.
  Frame late = make_fragment(a, 2, 0, 2);
  late.hdr.seq = 0x4242;
  now += 10;
  TEST_ASSERT_EQUAL(Status::FragLate, r.accept(late, now));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_frag_late);
  TEST_ASSERT_FALSE(r.active());  // no set was opened

  // spec 14.1 - a late fragment is normal traffic, not a drop.
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());

  // The completed payload is still intact: an echo must not destroy it.
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL_UINT32(4, r.len());

  // ...and a genuinely new set is accepted immediately afterwards.
  const uint8_t c0[] = {0x30, 0x31};
  const uint8_t c1[] = {0x40, 0x41};
  deliver_set(&r, 0x4243, c0, c1, &now);
  TEST_ASSERT_TRUE(r.complete());
  const uint8_t want[] = {0x30, 0x31, 0x40, 0x41};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, r.data(), 4);
}

// THE CASE THE RULE EXISTS FOR. Under the old behaviour the echo opened a set that
// could never complete, held the slot for frag_reassembly_timeout_ms, blocked the
// next set behind it, and then reported rx_reassembly_timeout - a counter naming a
// fault that did not occur.
void test_late_fragment_does_not_block_slot() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {0x01, 0x02};
  const uint8_t b[] = {0x03, 0x04};
  uint32_t now = 1000;

  deliver_set(&r, 0x0501, a, b, &now);

  Frame late = make_fragment(b, 2, 1, 2);
  late.hdr.seq = 0x0501;
  now += 10;
  TEST_ASSERT_EQUAL(Status::FragLate, r.accept(late, now));

  // The new set completes WITHOUT waiting out any timeout: the clock advances by far
  // less than frag_reassembly_timeout_ms across the whole exchange.
  const uint8_t d0[] = {0xAA, 0xBB};
  const uint8_t d1[] = {0xCC, 0xDD};
  deliver_set(&r, 0x0502, d0, d1, &now);
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_LESS_THAN_UINT32(kDefaultFragTimeoutMs, now - 1000);

  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_timeout);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_abandoned);
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_frag_late);
}

// Only COMPLETION retains a key. A set that expired never completed, so a fragment
// carrying its key is a legitimate retry and must be allowed to open a fresh set.
void test_timed_out_set_does_not_retain_key() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {0x77, 0x88};

  Frame f0 = make_fragment(a, 2, 0, 2);
  f0.hdr.seq = 0x1234;
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(f0, 1000));
  r.tick(1000 + kDefaultFragTimeoutMs);
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_reassembly_timeout);
  TEST_ASSERT_FALSE(r.active());

  // Same key again - a retry, not a late echo.
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(f0, 1000 + kDefaultFragTimeoutMs + 10));
  TEST_ASSERT_TRUE(r.active());
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_frag_late);
}

// The retained key holds ONE set. spec 11.2 - displaced by the next completion.
void test_retained_key_displaced_by_next_completion() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {0x01, 0x02};
  const uint8_t b[] = {0x03, 0x04};
  uint32_t now = 1000;

  deliver_set(&r, 0xA000, a, b, &now);  // set A
  deliver_set(&r, 0xB000, a, b, &now);  // set B displaces A's key
  TEST_ASSERT_TRUE(r.complete());

  // A fragment of A is no longer recognised as late, so it opens a new set.
  Frame old_a = make_fragment(a, 2, 0, 2);
  old_a.hdr.seq = 0xA000;
  now += 10;
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(old_a, now));
  TEST_ASSERT_TRUE(r.active());
  TEST_ASSERT_EQUAL_HEX16(0xA000, r.seq());
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_frag_late);

  // B's key is still the retained one, so B's echo is still recognised as late -
  // even though a different set (A) is now live. The retained-key check sits after
  // the live-set check and before starting anything new (spec 11.2).
  Frame echo_b = make_fragment(b, 2, 1, 2);
  echo_b.hdr.seq = 0xB000;
  now += 10;
  TEST_ASSERT_EQUAL(Status::FragLate, r.accept(echo_b, now));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_frag_late);
  TEST_ASSERT_TRUE(r.active());              // A's set survived the echo
  TEST_ASSERT_EQUAL_HEX16(0xA000, r.seq());
}

// --- L2 / spec 11.2: single-frame frames are untouched by the rule ----------

// A retransmitted unfragmented CONFIG_ACK must not be eaten as a late fragment: it
// never joined a set in the first place.
void test_single_frame_retransmit_not_late() {
  Counters c;
  Reassembler r(&c);
  const uint8_t p[] = {0x01, 0x00, 0x00};

  Frame f = make_fragment(p, 3, 0, 1, MsgType::ConfigAck, kSchemaNodeConfigV1);
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(f, 100));
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(f, 110));  // the retransmit
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_frag_late);
  TEST_ASSERT_EQUAL_UINT32(3, r.len());
}

// G2 / spec 11.3, INVERTED IN v0.6 - this assertion previously read
// `TEST_ASSERT_EQUAL_UINT32(1, c.rx_reassembly_abandoned)`. Counting the
// displacement was v0.5's fix for a silent discard and was the right immediate move;
// spec 11.2 now says the single frame had no business in the slot at all, so there
// is no displacement left to count. Kept in place rather than deleted: a test that
// flips is easier to miss than a test that is missing.
//
// After this, rx_reassembly_abandoned has exactly ONE caller - one set displacing
// another with no slot free - which is what makes it worth distinguishing from
// rx_reassembly_timeout: a timeout means the RF path dropped a fragment, an
// abandonment means the receiver is undersized or a peer is interleaving sets. A
// STATUS arriving on schedule is neither.
void test_single_frame_does_not_abandon_live_set() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {0x01, 0x02};
  Frame f0 = make_fragment(a, 2, 0, 2);
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(f0, 100));
  TEST_ASSERT_TRUE(r.active());

  const uint8_t p[] = {0x01};
  Frame single = make_fragment(p, 1, 0, 1, MsgType::Poll);
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(single, 110));
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_abandoned);

  // The set is untouched: still live, still holding the same key, and no counter of
  // any kind moved.
  TEST_ASSERT_TRUE(r.active());
  TEST_ASSERT_EQUAL_HEX16(0x4242, r.seq());
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_frag_duplicate);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_frag_late);
}

// G1 / spec 11.2 - the whole round trip. A single-frame frame may not begin, join,
// displace or expire a set, even one sharing (src, ctx_id, schema) with it, AND it
// must still be delivered: the fix for the displacement is a delivery path that
// bypasses the slot, not a discard.
//
// The case is not exotic. On the bridge this is a node's periodic STATUS or an
// asynchronous EVENT arriving while that same node's fragmented set is still in flight.
// The set is a PING: spec v0.12 made CONFIG_ACK single-frame (11.4, D38), and PING is the
// only fragmentable type left.
void test_single_frame_does_not_disturb_live_set() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {0x10, 0x11, 0x12};
  const uint8_t b[] = {0x20, 0x21};

  // A live multi-fragment set, one fragment short.
  Frame f0 = make_fragment(a, 3, 0, 2);
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(f0, 100));
  TEST_ASSERT_TRUE(r.active());
  TEST_ASSERT_FALSE(r.complete());

  // The interloper: same peer, same ctx_id, different conversation. It is delivered
  // to the caller in full.
  const uint8_t s[] = {0xF0, 0xF1, 0xF2, 0xF3};
  Frame single = make_fragment(s, 4, 0, 1, MsgType::Status, kSchemaNodeHealthV1);
  single.hdr.seq = 0x0007;
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(single, 110));
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL_UINT32(4, r.len());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(s, r.data(), 4);

  // ...and the set behind it is still live, still on its own key.
  TEST_ASSERT_TRUE(r.active());
  TEST_ASSERT_EQUAL_HEX16(0x4242, r.seq());
  TEST_ASSERT_EQUAL(MsgType::Ping, r.type());

  // The set completes normally and reassembles to the bytes it would have without
  // the interloper.
  Frame f1 = make_fragment(b, 2, 1, 2);
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(f1, 120));
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_FALSE(r.active());
  const uint8_t want[] = {0x10, 0x11, 0x12, 0x20, 0x21};
  TEST_ASSERT_EQUAL_UINT32(5, r.len());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, r.data(), 5);

  // No reassembly counter moved, in either direction: not abandoned, not timed out,
  // not overflowed, and not a duplicate or a late fragment either.
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_abandoned);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_timeout);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_fragment_overflow);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_frag_duplicate);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_frag_late);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// spec 11.2 - the "expire" half of the rule, which is the one an implementation gets
// wrong by accident: a single frame that runs the slot's clock forward ages out a
// set it is not part of. tick() from the receive loop is what expires sets.
void test_single_frame_does_not_expire_live_set() {
  Counters c;
  Reassembler r(&c);
  const uint8_t a[] = {0x01, 0x02};
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(make_fragment(a, 2, 0, 2), 1000));

  const uint8_t p[] = {0x01};
  Frame single = make_fragment(p, 1, 0, 1, MsgType::Poll);
  single.hdr.seq = 0x0009;
  // Well past frag_reassembly_timeout_ms. The set is stale, but this frame is not
  // what may say so.
  TEST_ASSERT_EQUAL(Status::Ok, r.accept(single, 1000 + kDefaultFragTimeoutMs + 1));
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_timeout);
  TEST_ASSERT_TRUE(r.active());

  // The receive loop's tick is what expires it, and then it is counted exactly once.
  r.tick(1000 + kDefaultFragTimeoutMs + 2);
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_reassembly_timeout);
  TEST_ASSERT_FALSE(r.active());
}

int run_all() {
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
  RUN_TEST(test_displaced_set_counts_abandoned_not_timeout);
  RUN_TEST(test_expired_set_counts_timeout_not_abandoned);
  RUN_TEST(test_inconsistent_total_rejected);
  RUN_TEST(test_fragmented_ping_end_to_end);
  RUN_TEST(test_fragment_sizes_uniform_except_last);
  RUN_TEST(test_fragmentation_is_deterministic);
  RUN_TEST(test_default_chunk_is_the_type_maximum);
  RUN_TEST(test_duplicate_fragment_index_overwrites);
  RUN_TEST(test_duplicate_fragment_of_different_length_overwrites);
  RUN_TEST(test_fragmented_hex_req_rejected);
  RUN_TEST(test_late_fragment_after_completion_counted);
  RUN_TEST(test_late_fragment_does_not_block_slot);
  RUN_TEST(test_timed_out_set_does_not_retain_key);
  RUN_TEST(test_retained_key_displaced_by_next_completion);
  RUN_TEST(test_single_frame_retransmit_not_late);
  RUN_TEST(test_single_frame_does_not_abandon_live_set);
  RUN_TEST(test_single_frame_does_not_disturb_live_set);
  RUN_TEST(test_single_frame_does_not_expire_live_set);
  return UNITY_END();
}

// PlatformIO runs the same suites on the host and on the ESP32-S3. The host entry
// point is main(); Arduino's is setup()/loop(). Unity's own setUp/tearDown are
// distinct names and do not collide.
#ifdef ARDUINO
void setup() {
  // The USB-serial link needs a moment before the first report, or the opening
  // lines are lost and a passing run looks like a hang.
  delay(2000);
  run_all();
}
void loop() {}
#else
int main() { return run_all(); }
#endif
