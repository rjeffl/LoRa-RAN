// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// P2 - framing, CRC and the spec 14 receive ladder.

#include <unity.h>

#include <cstring>

#include "lran/lran.h"
#include "refimpl_mac.h"

using namespace lran;

namespace {

const uint8_t kKey[kNodeKeyLen] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

refimpl::RefMac g_mac;

Header poll_header() {
  Header h;
  h.ver    = kProtoVer;
  h.type   = MsgType::Poll;
  h.src    = kNodeBridge;
  h.dst    = kNodeGateLink;
  h.seq    = 0x1234;
  h.ctx_id = 0x89ABCDEFu;
  h.set_frag(0, 1);
  h.schema = kSchemaNone;
  return h;
}

DecodeCtx node_ctx(Counters* c) {
  DecodeCtx d;
  d.self           = kNodeGateLink;
  d.accept_ver_min = kProtoVer;
  d.accept_ver_max = kProtoVer;
  d.mac            = &g_mac;
  d.node_key       = kKey;
  d.counters       = c;
  return d;
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- spec 3.1: every size derives from LRAN_MAX_FRAME -----------------------

void test_derived_size_constants() {
  TEST_ASSERT_EQUAL_UINT32(222, kMaxFrame);
  TEST_ASSERT_EQUAL_UINT32(16, kHdrLen);
  TEST_ASSERT_EQUAL_UINT32(196, kMaxPayloadAuth);
  TEST_ASSERT_EQUAL_UINT32(204, kMaxPayloadPlain);
  TEST_ASSERT_EQUAL_UINT32(196, kMaxSchemaPayload);
  TEST_ASSERT_EQUAL_UINT32(202, kPingMaxEcho);
  TEST_ASSERT_EQUAL_UINT32(18, kMinFrame);
}

// --- spec 2.1: CRC-16/CCITT-FALSE against the published check value ---------

void test_crc16_known_answer() {
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16_ccitt_false(check, sizeof(check)));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16_ccitt_false(nullptr, 0));  // init value
}

// --- spec 4.1: explicit little-endian ---------------------------------------

void test_bytewriter_little_endian() {
  uint8_t buf[16] = {};
  ByteWriter w(buf, sizeof(buf));
  w.u16(0x1234);
  w.u32(0x89ABCDEFu);
  w.i16(-2);
  TEST_ASSERT_TRUE(w.ok());
  TEST_ASSERT_EQUAL_HEX8(0x34, buf[0]);
  TEST_ASSERT_EQUAL_HEX8(0x12, buf[1]);
  TEST_ASSERT_EQUAL_HEX8(0xEF, buf[2]);
  TEST_ASSERT_EQUAL_HEX8(0xCD, buf[3]);
  TEST_ASSERT_EQUAL_HEX8(0xAB, buf[4]);
  TEST_ASSERT_EQUAL_HEX8(0x89, buf[5]);
  TEST_ASSERT_EQUAL_HEX8(0xFE, buf[6]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, buf[7]);

  ByteReader r(buf, 8);
  TEST_ASSERT_EQUAL_HEX16(0x1234, r.u16());
  TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFu, r.u32());
  TEST_ASSERT_EQUAL_INT16(-2, r.i16());
  TEST_ASSERT_TRUE(r.ok());
}

void test_bytewriter_latches_overrun() {
  uint8_t buf[3] = {};
  ByteWriter w(buf, sizeof(buf));
  TEST_ASSERT_TRUE(w.u16(0xAABB));
  TEST_ASSERT_FALSE(w.u16(0xCCDD));  // only one byte left
  TEST_ASSERT_FALSE(w.ok());
  TEST_ASSERT_FALSE(w.u8(0x01));  // stays latched
}

// --- spec 5: header layout, byte for byte -----------------------------------

void test_header_byte_offsets() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0x07};
  Header  h = poll_header();
  h.hdr_flags   = 0;
  h.reserved[0] = 0xAA;  // must be overwritten with zero by encode (spec 4.3)

  EncodeCtx ec;
  TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload, 1, ec, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(19, n);  // spec 19: POLL is 19 bytes

  TEST_ASSERT_EQUAL_HEX8(kProtoVer, buf[0]);            // ver
  TEST_ASSERT_EQUAL_HEX8(0x03, buf[1]);                 // type = POLL
  TEST_ASSERT_EQUAL_HEX8(kNodeBridge, buf[2]);          // src
  TEST_ASSERT_EQUAL_HEX8(kNodeGateLink, buf[3]);        // dst
  TEST_ASSERT_EQUAL_HEX8(0x34, buf[4]);                 // seq lo
  TEST_ASSERT_EQUAL_HEX8(0x12, buf[5]);                 // seq hi
  TEST_ASSERT_EQUAL_HEX8(0xEF, buf[6]);                 // ctx_id[0]
  TEST_ASSERT_EQUAL_HEX8(0xCD, buf[7]);
  TEST_ASSERT_EQUAL_HEX8(0xAB, buf[8]);
  TEST_ASSERT_EQUAL_HEX8(0x89, buf[9]);
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[10]);                // frag: index 0, total 1
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[11]);                // schema
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[12]);                // hdr_flags
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[13]);                // reserved, forced to zero
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[14]);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[15]);
  TEST_ASSERT_EQUAL_HEX8(0x07, buf[16]);                // payload begins at 0x10
}

void test_roundtrip_poll() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {kPollFlagFullStatus};
  EncodeCtx ec;
  TEST_ASSERT_EQUAL(Status::Ok, encode(poll_header(), payload, 1, ec, buf, sizeof(buf), &n));

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_HEX8(kProtoVer, f.hdr.ver);
  TEST_ASSERT_EQUAL(MsgType::Poll, f.hdr.type);
  TEST_ASSERT_EQUAL_HEX16(0x1234, f.hdr.seq);
  TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFu, f.hdr.ctx_id);
  TEST_ASSERT_EQUAL_UINT32(1, f.payload_len);
  TEST_ASSERT_EQUAL_HEX8(kPollFlagFullStatus, f.payload[0]);
  TEST_ASSERT_NULL(f.mac);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// --- spec 14 ladder: every stage reachable and counted ----------------------

void test_stage2_runt() {
  Counters c;
  Frame f;
  uint8_t buf[17] = {};
  TEST_ASSERT_EQUAL(Status::Runt, decode_header(buf, 17, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_runt);
}

// spec 14 stage 2a - a frame longer than kMaxFrame is a counted runtime discard,
// not an assertion: the SX126x PHY hands up as much as 255 bytes and a foreign
// transmitter on the band can produce one at any time.
void test_oversize_frame_is_counted_not_asserted() {
  Counters c;
  Frame f;
  uint8_t buf[255] = {};
  TEST_ASSERT_EQUAL(Status::Oversize, decode_header(buf, sizeof(buf), node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_oversize);
  // Nothing else moved. rx_oversize says "foreign transmitter or misconfigured PHY";
  // rx_bad_length says "a peer's encoder is wrong". Folding the two together at the
  // gate would cost the entire diagnosis (spec 14).
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_bad_length);
  TEST_ASSERT_EQUAL_UINT32(1, c.total_dropped());
}

// The other half of the split: a correctly sized frame whose payload length does not
// match its (type, schema) lands on rx_bad_length and nowhere near rx_oversize.
void test_bad_payload_length_is_not_oversize() {
  Header h = poll_header();
  h.type   = MsgType::Status;
  h.schema = kSchemaGateLinkStatusV1;

  // encode refuses the wrong length too, so the frame is built from a valid 78-byte
  // STATUS with one payload byte trimmed off and the CRC recomputed - which is what a
  // peer with a wrong encoder actually puts on the air.
  uint8_t payload[78] = {};
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  EncodeCtx ec;
  TEST_ASSERT_EQUAL(Status::Ok,
                    encode(h, payload, sizeof(payload), ec, buf, sizeof(buf), &n));

  const size_t shortened = n - 1;
  const uint16_t crc = crc16_ccitt_false(buf, shortened - kCrcLen);
  buf[shortened - 2] = static_cast<uint8_t>(crc & 0xFF);
  buf[shortened - 1] = static_cast<uint8_t>(crc >> 8);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, shortened, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL(Status::BadLength, decode_payload(buf, shortened, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_bad_length);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_oversize);
  TEST_ASSERT_EQUAL_UINT32(1, c.total_dropped());
}

// --- spec 5.6 / 14 stage 5b: the `frag` byte is validated in the header -----

// A declared total of 0 is a malformed header, not a single-frame marker (0x01 is).
void test_stage5b_frag_total_zero() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0};
  EncodeCtx ec;
  TEST_ASSERT_EQUAL(Status::Ok, encode(poll_header(), payload, 1, ec, buf, sizeof(buf), &n));
  buf[10] = 0x00;  // frag: index 0, total 0
  const uint16_t crc = crc16_ccitt_false(buf, n - kCrcLen);
  buf[n - 2] = static_cast<uint8_t>(crc & 0xFF);
  buf[n - 1] = static_cast<uint8_t>(crc >> 8);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::BadFrag, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_bad_frag);
  TEST_ASSERT_EQUAL_UINT32(1, c.total_dropped());
}

// spec 11.2 - an index at or past the declared total keeps FRAGMENT_OVERFLOW. The
// two conditions have different diagnoses and must not share a counter.
void test_stage5b_index_past_total() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0};
  EncodeCtx ec;
  TEST_ASSERT_EQUAL(Status::Ok, encode(poll_header(), payload, 1, ec, buf, sizeof(buf), &n));
  buf[10] = 0xF1;  // frag: index 15, total 1
  const uint16_t crc = crc16_ccitt_false(buf, n - kCrcLen);
  buf[n - 2] = static_cast<uint8_t>(crc & 0xFF);
  buf[n - 1] = static_cast<uint8_t>(crc >> 8);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::FragmentOverflow, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.fragment_overflow);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_bad_frag);
  TEST_ASSERT_EQUAL_UINT32(1, c.total_dropped());
}

// The control: frag = 0x01 is a valid single unfragmented frame and moves nothing.
void test_stage5b_single_frame_frag_is_valid() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0};
  EncodeCtx ec;
  TEST_ASSERT_EQUAL(Status::Ok, encode(poll_header(), payload, 1, ec, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[10]);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

void test_stage3_bad_crc() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0};
  EncodeCtx ec;
  encode(poll_header(), payload, 1, ec, buf, sizeof(buf), &n);
  buf[16] ^= 0xFF;  // corrupt the payload, leave the CRC alone

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::BadCrc, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_bad_crc);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_crc_err);  // the PHY counter is not the codec's
}

void test_stage4_bad_version() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0};
  Header h = poll_header();
  h.ver = 99;
  EncodeCtx ec;
  encode(h, payload, 1, ec, buf, sizeof(buf), &n);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::BadVersion, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_bad_ver);
}

void test_bridge_accepts_n_minus_1() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0};
  Header h = poll_header();
  h.ver = kProtoVer - 1;  // spec 13.1
  h.dst = kNodeBridge;
  EncodeCtx ec;
  encode(h, payload, 1, ec, buf, sizeof(buf), &n);

  Counters c;
  DecodeCtx d = node_ctx(&c);
  d.self           = kNodeBridge;
  d.accept_ver_min = kProtoVer - 1;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, d, &f));
}

void test_stage5_not_addressed() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0};
  Header h = poll_header();
  h.dst = kNodeWellLink;
  EncodeCtx ec;
  encode(h, payload, 1, ec, buf, sizeof(buf), &n);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::NotAddressed, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_not_addressed);
}

void test_broadcast_is_addressed() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0};
  Header h = poll_header();
  h.dst = kNodeBroadcast;
  EncodeCtx ec;
  encode(h, payload, 1, ec, buf, sizeof(buf), &n);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, node_ctx(&c), &f));
}

void test_stage5a_critical_ext() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0};
  Header h = poll_header();
  h.hdr_flags = kHdrFlagCriticalExt;
  EncodeCtx ec;
  encode(h, payload, 1, ec, buf, sizeof(buf), &n);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::UnknownHdrExt, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_unknown_hdr_ext);
}

// spec 4.3 / 5.9 - the row most likely to be skipped, because its expected result is
// that nothing happens. It is what protects the header extension space.
void test_forward_compat_reserved_bits_ignored() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0};
  EncodeCtx ec;
  encode(poll_header(), payload, 1, ec, buf, sizeof(buf), &n);

  // Set hdr_flags bits 6:0 and all three reserved bytes, as a future sender using
  // optional extensions would, then repair the CRC.
  buf[12] = 0x7F;
  buf[13] = 0xDE;
  buf[14] = 0xAD;
  buf[15] = 0xBE;
  const uint16_t crc = crc16_ccitt_false(buf, n - kCrcLen);
  buf[n - 2] = static_cast<uint8_t>(crc & 0xFF);
  buf[n - 1] = static_cast<uint8_t>(crc >> 8);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_HEX8(0x7F, f.hdr.hdr_flags);
  TEST_ASSERT_EQUAL_HEX8(0xDE, f.hdr.reserved[0]);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

void test_stage6_unknown_type() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  const uint8_t payload[1] = {0};
  EncodeCtx ec;
  encode(poll_header(), payload, 1, ec, buf, sizeof(buf), &n);
  buf[1] = 0x0C;  // spec 6 - reserved type
  const uint16_t crc = crc16_ccitt_false(buf, n - kCrcLen);
  buf[n - 2] = static_cast<uint8_t>(crc & 0xFF);
  buf[n - 1] = static_cast<uint8_t>(crc >> 8);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::UnknownType, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_unknown_type);
}

void test_stage7_unknown_schema() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  uint8_t payload[78] = {};
  Header h = poll_header();
  h.type   = MsgType::Status;
  h.schema = kSchemaGateLinkStatusV1;
  EncodeCtx ec;
  TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload, sizeof(payload), ec, buf, sizeof(buf), &n));
  buf[11] = 0x20;  // WellLink status - reserved, not defined
  const uint16_t crc = crc16_ccitt_false(buf, n - kCrcLen);
  buf[n - 2] = static_cast<uint8_t>(crc & 0xFF);
  buf[n - 1] = static_cast<uint8_t>(crc >> 8);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL(Status::UnknownSchema, decode_payload(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_unknown_schema);
}

// spec 7.1 - pairing, not just membership: 0x11 is an EVENT schema.
void test_schema_type_pairing_is_validated() {
  TEST_ASSERT_FALSE(schema_is_known(MsgType::Status, kSchemaGateLinkEventV1));
  TEST_ASSERT_TRUE(schema_is_known(MsgType::Event, kSchemaGateLinkEventV1));
  TEST_ASSERT_TRUE(schema_is_known(MsgType::Status, kSchemaSimnodeStatusV1));
  // spec 5.7 - ignored entirely for types that carry no schema.
  TEST_ASSERT_TRUE(schema_is_known(MsgType::Poll, 0x77));
}

void test_stage8_bad_length() {
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  uint8_t payload[77] = {};  // one short of the 78-byte schema 0x10
  Header h = poll_header();
  h.type   = MsgType::Status;
  h.schema = kSchemaGateLinkStatusV1;
  EncodeCtx ec;
  // encode refuses it too
  TEST_ASSERT_EQUAL(Status::BadLength,
                    encode(h, payload, sizeof(payload), ec, buf, sizeof(buf), &n));

  // Build the short frame by hand to exercise the decode side.
  uint8_t raw[kMaxFrame] = {};
  raw[0] = kProtoVer; raw[1] = 0x04; raw[2] = kNodeBridge; raw[3] = kNodeGateLink;
  raw[10] = 0x01; raw[11] = kSchemaGateLinkStatusV1;
  const size_t total = kHdrLen + 77 + kCrcLen;
  const uint16_t crc = crc16_ccitt_false(raw, total - kCrcLen);
  raw[total - 2] = static_cast<uint8_t>(crc & 0xFF);
  raw[total - 1] = static_cast<uint8_t>(crc >> 8);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(raw, total, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL(Status::BadLength, decode_payload(raw, total, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_bad_length);
}

// --- spec 6.6.1: a maximum PING is exactly LRAN_MAX_FRAME --------------------

void test_max_ping_is_222_bytes() {
  uint8_t echo[kPingMaxEcho];
  msg::ping_fill_pattern(0x1234, echo, sizeof(echo));

  uint8_t payload[kMaxPayloadPlain];
  size_t  plen = 0;
  msg::Ping p;
  p.ping_flags = kPingFlagPatternFill;
  p.n          = static_cast<uint8_t>(sizeof(echo));
  p.data       = echo;
  TEST_ASSERT_EQUAL(Status::Ok, msg::serialize(p, payload, sizeof(payload), &plen));
  TEST_ASSERT_EQUAL_UINT32(204, plen);

  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  Header h = poll_header();
  h.type = MsgType::Ping;
  EncodeCtx ec;
  TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload, plen, ec, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(kMaxFrame, n);  // spec 6.6.1

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, node_ctx(&c), &f));

  msg::Ping back;
  TEST_ASSERT_EQUAL(Status::Ok, msg::deserialize(f.payload, f.payload_len, &back));
  TEST_ASSERT_EQUAL_UINT8(kPingMaxEcho, back.n);
  size_t bad = 0;
  TEST_ASSERT_TRUE(msg::ping_check_pattern(0x1234, back.data, back.n, &bad));
}

// spec 6.6.3 - the pattern localizes corruption to an offset.
void test_ping_pattern_reports_offset() {
  uint8_t echo[32];
  msg::ping_fill_pattern(0x00FF, echo, sizeof(echo));
  TEST_ASSERT_EQUAL_HEX8(0xFF, echo[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, echo[1]);  // wraps within the byte
  echo[13] ^= 0x01;
  size_t bad = 0;
  TEST_ASSERT_FALSE(msg::ping_check_pattern(0x00FF, echo, sizeof(echo), &bad));
  TEST_ASSERT_EQUAL_UINT32(13, bad);
}

void test_ping_zero_length_payload() {
  uint8_t payload[2] = {0, 0};
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  Header h = poll_header();
  h.type = MsgType::Ping;
  EncodeCtx ec;
  TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload, 2, ec, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(20, n);
  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, node_ctx(&c), &f));
}

// --- spec 5.4 / 10.5: RFC 1982 -----------------------------------------------

void test_seq_newer_across_wrap() {
  TEST_ASSERT_TRUE(seq_newer(2, 1));
  TEST_ASSERT_FALSE(seq_newer(1, 2));
  TEST_ASSERT_FALSE(seq_newer(5, 5));
  // The failure a plain > produces: 0x0001 must read as newer than 0xFFFF.
  TEST_ASSERT_TRUE(seq_newer(0x0001, 0xFFFF));
  TEST_ASSERT_FALSE(seq_newer(0xFFFF, 0x0001));
  TEST_ASSERT_TRUE(seq_newer(0x0000, 0xFFFF));
  TEST_ASSERT_TRUE(seq_newer(0x7FFF, 0x0000));
  TEST_ASSERT_FALSE(seq_newer(0x8000, 0x0000));  // exactly half - ambiguous, not newer
  TEST_ASSERT_EQUAL_HEX16(0x0000, seq_next(0xFFFF));

  // Exhaustive sweep of the neighbourhood of the wrap.
  for (uint32_t d = 1; d < 1000; ++d) {
    const Seq base = 0xFFF0;
    const Seq newer = static_cast<Seq>(base + d);
    TEST_ASSERT_TRUE(seq_newer(newer, base));
    TEST_ASSERT_FALSE(seq_newer(base, newer));
  }
}

// --- spec 14: every Status maps to exactly one counter -----------------------

void test_counter_mapping_is_total() {
  Counters c;
  c.bump(Status::Runt);
  c.bump(Status::Oversize);
  c.bump(Status::BadCrc);
  c.bump(Status::BadVersion);
  c.bump(Status::NotAddressed);
  c.bump(Status::UnknownHdrExt);
  c.bump(Status::BadFrag);
  c.bump(Status::UnknownType);
  c.bump(Status::UnknownSchema);
  c.bump(Status::BadLength);
  c.bump(Status::NotFragmentable);
  c.bump(Status::ReassemblyTimeout);
  c.bump(Status::FragmentOverflow);
  c.bump(Status::BadMac);
  c.bump(Status::CtxMismatch);
  TEST_ASSERT_EQUAL_UINT32(15, c.total_dropped());

  // Not wire conditions: no spec 14 stage owns them, so they are not drops.
  c.bump(Status::Ok);
  c.bump(Status::BufferTooSmall);
  c.bump(Status::MissingMac);
  c.bump(Status::NotImplemented);
  TEST_ASSERT_EQUAL_UINT32(15, c.total_dropped());

  // spec 14 stage 1 belongs to the radio driver but still counts as a drop.
  c.rx_crc_err = 5;
  TEST_ASSERT_EQUAL_UINT32(20, c.total_dropped());
}

void test_status_strings_present() {
  TEST_ASSERT_EQUAL_STRING("Ok", to_string(Status::Ok));
  TEST_ASSERT_EQUAL_STRING("BadMac", to_string(Status::BadMac));
  TEST_ASSERT_EQUAL_STRING("UnknownHdrExt", to_string(Status::UnknownHdrExt));
}

// --- spec 6.2-6.6, 7.6: fixed-shape payloads ---------------------------------

void test_message_payload_roundtrips() {
  uint8_t buf[16];
  size_t  n = 0;

  msg::Command cmd{static_cast<uint8_t>(Cmd::HoldOpen), 0x00, 0xBEEF};
  TEST_ASSERT_EQUAL(Status::Ok, msg::serialize(cmd, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(msg::kCommandLen, n);
  TEST_ASSERT_EQUAL_HEX8(0xEF, buf[2]);  // arg2 little-endian
  TEST_ASSERT_EQUAL_HEX8(0xBE, buf[3]);
  msg::Command cmd_back;
  TEST_ASSERT_EQUAL(Status::Ok, msg::deserialize(buf, n, &cmd_back));
  TEST_ASSERT_EQUAL_HEX16(0xBEEF, cmd_back.arg2);

  msg::CommandAck ack{0x1234, static_cast<uint8_t>(AckResult::DuplicateCached), 9};
  TEST_ASSERT_EQUAL(Status::Ok, msg::serialize(ack, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(msg::kCommandAckLen, n);
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[4]);  // reserved written zero
  TEST_ASSERT_EQUAL_HEX8(0x00, buf[5]);

  msg::Error err{static_cast<uint8_t>(ErrCode::UnknownHdrExt), 1, 0x4321};
  TEST_ASSERT_EQUAL(Status::Ok, msg::serialize(err, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(msg::kErrorLen, n);
  msg::Error err_back;
  TEST_ASSERT_EQUAL(Status::Ok, msg::deserialize(buf, n, &err_back));
  TEST_ASSERT_EQUAL_HEX16(0x4321, err_back.ref_seq);
}

// spec 7.6 - HEX_RSP is 2 + n. The spec 6 and spec 19 "3 + n" figures are an erratum.
void test_hex_rsp_is_two_plus_n() {
  const uint8_t hex[] = {':', '7', 'F', '0', 'E', 'D'};
  uint8_t payload[64];
  size_t  plen = 0;
  msg::HexRsp rsp;
  rsp.status = static_cast<uint8_t>(HexStatus::Ok);
  rsp.n      = sizeof(hex);
  rsp.hex    = hex;
  TEST_ASSERT_EQUAL(Status::Ok, msg::serialize(rsp, payload, sizeof(payload), &plen));
  TEST_ASSERT_EQUAL_UINT32(2 + sizeof(hex), plen);

  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  Header h = poll_header();
  h.type = MsgType::HexRsp;
  EncodeCtx ec;
  TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload, plen, ec, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(kHdrLen + 2 + sizeof(hex) + kCrcLen, n);
}

// spec 6 / 7.6 / 19 - HEX_RSP is status(1) + n(1) + hex[n] = 2 + N, giving a
// 20 + N byte frame. v0.3's §6 said 3 + N and its §19 said 21 + N; v0.4 corrects both
// to match §7.6, which was right. Asserted as an exact frame length rather than only
// round-tripped so an off-by-one in EITHER direction fails loudly.
void test_hex_rsp_frame_length_is_twenty_plus_n() {
  struct Case { uint8_t n; };
  const Case cases[] = {{0}, {10}};

  for (const Case& tc : cases) {
    uint8_t hex[16] = {};
    for (uint8_t i = 0; i < tc.n; ++i) hex[i] = static_cast<uint8_t>('0' + (i % 10));

    uint8_t payload[64];
    size_t  plen = 0;
    msg::HexRsp rsp;
    rsp.status = static_cast<uint8_t>(HexStatus::Ok);
    rsp.n      = tc.n;
    rsp.hex    = hex;
    TEST_ASSERT_EQUAL(Status::Ok, msg::serialize(rsp, payload, sizeof(payload), &plen));
    TEST_ASSERT_EQUAL_UINT32(2u + tc.n, plen);  // NOT 3 + n

    uint8_t buf[kMaxFrame];
    size_t  n = 0;
    Header h = poll_header();
    h.type = MsgType::HexRsp;
    EncodeCtx ec;
    TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload, plen, ec, buf, sizeof(buf), &n));
    TEST_ASSERT_EQUAL_UINT32(20u + tc.n, n);  // NOT 21 + n

    // ...and the decoder agrees on the same boundary.
    Counters c;
    Frame f;
    TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, node_ctx(&c), &f));
    TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, node_ctx(&c), &f));
    TEST_ASSERT_EQUAL_UINT32(2u + tc.n, f.payload_len);
    TEST_ASSERT_NULL(f.mac);
    TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
  }
}

// --- spec 7.6 / 9.2: the HEX_REQ authentication rule -------------------------

void test_hex_req_write_class_detection() {
  // Get (0x7) is read-only and needs no MAC.
  const uint8_t get[] = {0x00, 6, ':', '7', 'F', '0', 'E', 'D'};
  TEST_ASSERT_FALSE(hex_req_is_write_class(get, sizeof(get)));
  // Set (0x8) writes MPPT config - a battery-damage path under LiFePO4.
  const uint8_t set[] = {0x00, 6, ':', '8', 'F', '0', 'E', 'D'};
  TEST_ASSERT_TRUE(hex_req_is_write_class(set, sizeof(set)));
  // Restart (0x6).
  const uint8_t restart[] = {0x00, 2, ':', '6'};
  TEST_ASSERT_TRUE(hex_req_is_write_class(restart, sizeof(restart)));
  // The declared flag is not trusted - only the nibble decides.
  const uint8_t lying[] = {0x00, 6, ':', '7', 'F', '0', 'E', 'D'};
  TEST_ASSERT_FALSE(hex_req_is_write_class(lying, sizeof(lying)));
}

void test_hex_req_write_without_mac_is_rejected() {
  const uint8_t hex[] = {':', '8', 'F', '0', 'E', 'D'};
  uint8_t payload[64];
  size_t  plen = 0;
  msg::HexReq req;
  req.flags = 0;  // deliberately not declaring write-class
  req.n     = sizeof(hex);
  req.hex   = hex;
  msg::serialize(req, payload, sizeof(payload), &plen);

  // Hand-build the frame without a MAC (encode would have added one).
  uint8_t raw[kMaxFrame] = {};
  raw[0] = kProtoVer; raw[1] = 0x08; raw[2] = kNodeBridge; raw[3] = kNodeGateLink;
  raw[10] = 0x01;
  for (size_t i = 0; i < plen; ++i) raw[kHdrLen + i] = payload[i];
  const size_t total = kHdrLen + plen + kCrcLen;
  const uint16_t crc = crc16_ccitt_false(raw, total - kCrcLen);
  raw[total - 2] = static_cast<uint8_t>(crc & 0xFF);
  raw[total - 1] = static_cast<uint8_t>(crc >> 8);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(raw, total, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL(Status::BadMac, decode_payload(raw, total, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_bad_mac);
}

void test_hex_req_read_needs_no_mac() {
  const uint8_t hex[] = {':', '7', 'F', '0', 'E', 'D'};
  uint8_t payload[64];
  size_t  plen = 0;
  msg::HexReq req{0, sizeof(hex), hex};
  msg::serialize(req, payload, sizeof(payload), &plen);

  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  Header h = poll_header();
  h.type = MsgType::HexReq;
  EncodeCtx ec;  // no IMac needed: a read carries no MAC
  TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload, plen, ec, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(kHdrLen + plen + kCrcLen, n);

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_NULL(f.mac);
}

// --- spec 7.4: variable CONFIG payloads are validated structurally -----------

void test_config_payload_structure_validated() {
  schema::GateLinkConfigV1 cfg;
  cfg.op    = ConfigOp::Set;
  cfg.count = 2;
  schema::entry_pack(&cfg.entries[0], 0x0001, PType::U16, 300);
  schema::entry_pack(&cfg.entries[1], 0x0003, PType::U8, 5);

  uint8_t payload[kMaxSchemaPayload];
  size_t  plen = 0;
  TEST_ASSERT_EQUAL(Status::Ok, schema::serialize(cfg, payload, sizeof(payload), &plen));
  TEST_ASSERT_EQUAL_UINT32(2 + (4 + 2) + (4 + 1), plen);

  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  Header h = poll_header();
  h.type   = MsgType::Config;
  h.schema = kSchemaGateLinkConfigV1;
  EncodeCtx ec;
  ec.mac = &g_mac;
  ec.node_key = kKey;
  TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload, plen, ec, buf, sizeof(buf), &n));

  Counters c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, node_ctx(&c), &f));
  TEST_ASSERT_NOT_NULL(f.mac);  // spec 9.2 - CONFIG is authenticated

  schema::GateLinkConfigV1 back;
  TEST_ASSERT_EQUAL(Status::Ok, schema::deserialize(f.payload, f.payload_len, &back));
  TEST_ASSERT_EQUAL_UINT8(2, back.count);
  TEST_ASSERT_EQUAL_HEX16(0x0001, back.entries[0].param_id);
  TEST_ASSERT_EQUAL_UINT32(300, schema::entry_raw(back.entries[0].value,
                                                  back.entries[0].len));

  // A count that overruns the declared payload is rejected, not partly parsed.
  uint8_t bad[8] = {static_cast<uint8_t>(ConfigOp::Set), 2, 0x01, 0x00, 0x02, 0x02,
                    0x2C, 0x01};
  schema::GateLinkConfigV1 junk;
  TEST_ASSERT_EQUAL(Status::BadLength, schema::deserialize(bad, sizeof(bad), &junk));
}

// --- encode-side guards ------------------------------------------------------

// spec 9.2 - "An encoder MUST NOT emit an authenticated type without a MAC." The
// refusal is unchanged from P1-P5; only the reporting is. MissingMac says the caller
// wired the library up wrong or shipped without key material, which NotImplemented
// (a genuine library gap) could not be distinguished from in a field log.
void test_encode_refuses_unauthenticated_command() {
  msg::Command cmd{static_cast<uint8_t>(Cmd::Open), 0, 0};
  uint8_t payload[4];
  size_t  plen = 0;
  msg::serialize(cmd, payload, sizeof(payload), &plen);

  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  Header h = poll_header();
  h.type = MsgType::Command;
  EncodeCtx ec;  // no IMac
  TEST_ASSERT_EQUAL(Status::MissingMac,
                    encode(h, payload, plen, ec, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(0, n);  // and no frame on the wire

  // An IMac with no key is the same misconfiguration by another route.
  ec.mac = &g_mac;
  TEST_ASSERT_EQUAL(Status::MissingMac,
                    encode(h, payload, plen, ec, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(0, n);
}

void test_encode_buffer_too_small() {
  const uint8_t payload[1] = {0};
  uint8_t buf[18];
  size_t  n = 0;
  EncodeCtx ec;
  TEST_ASSERT_EQUAL(Status::BufferTooSmall,
                    encode(poll_header(), payload, 1, ec, buf, sizeof(buf), &n));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_derived_size_constants);
  RUN_TEST(test_crc16_known_answer);
  RUN_TEST(test_bytewriter_little_endian);
  RUN_TEST(test_bytewriter_latches_overrun);
  RUN_TEST(test_header_byte_offsets);
  RUN_TEST(test_roundtrip_poll);
  RUN_TEST(test_stage2_runt);
  RUN_TEST(test_oversize_frame_is_counted_not_asserted);
  RUN_TEST(test_bad_payload_length_is_not_oversize);
  RUN_TEST(test_stage5b_frag_total_zero);
  RUN_TEST(test_stage5b_index_past_total);
  RUN_TEST(test_stage5b_single_frame_frag_is_valid);
  RUN_TEST(test_stage3_bad_crc);
  RUN_TEST(test_stage4_bad_version);
  RUN_TEST(test_bridge_accepts_n_minus_1);
  RUN_TEST(test_stage5_not_addressed);
  RUN_TEST(test_broadcast_is_addressed);
  RUN_TEST(test_stage5a_critical_ext);
  RUN_TEST(test_forward_compat_reserved_bits_ignored);
  RUN_TEST(test_stage6_unknown_type);
  RUN_TEST(test_stage7_unknown_schema);
  RUN_TEST(test_schema_type_pairing_is_validated);
  RUN_TEST(test_stage8_bad_length);
  RUN_TEST(test_max_ping_is_222_bytes);
  RUN_TEST(test_ping_pattern_reports_offset);
  RUN_TEST(test_ping_zero_length_payload);
  RUN_TEST(test_seq_newer_across_wrap);
  RUN_TEST(test_counter_mapping_is_total);
  RUN_TEST(test_status_strings_present);
  RUN_TEST(test_message_payload_roundtrips);
  RUN_TEST(test_hex_rsp_is_two_plus_n);
  RUN_TEST(test_hex_rsp_frame_length_is_twenty_plus_n);
  RUN_TEST(test_hex_req_write_class_detection);
  RUN_TEST(test_hex_req_write_without_mac_is_rejected);
  RUN_TEST(test_hex_req_read_needs_no_mac);
  RUN_TEST(test_config_payload_structure_validated);
  RUN_TEST(test_encode_refuses_unauthenticated_command);
  RUN_TEST(test_encode_buffer_too_small);
  return UNITY_END();
}
