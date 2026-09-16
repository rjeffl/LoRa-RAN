// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-7 - the patch-after-encode primitive, Bridge Impl Plan 10.5.2.
//
// The first group is the independent witness. /tools/vectors/generate.py built its
// negative vectors the way 10.6 rule 1 asks the simnode to: a correct frame, edited, then
// resealed. That code never read this library. Each test here starts from lran::encode(),
// applies the same edit through FramePatch, and must reproduce the committed vector byte
// for byte. Payload bytes are taken from the vector itself because the payload is not
// what is under test; the header, the MAC and the trailer are.
//
// Three of the 21 negative vectors are absent: command_ctx_mismatch and
// command_signed_with_wrong_node_key are correct frames that encode() emits directly,
// and frag_index_equals_total follows the same path as frag_index_ge_total.

#include <unity.h>

#include <cstring>

#include "lran/lran.h"
#include "lran/sim/frame_patch.h"
#include "refimpl_mac.h"
#include "test_key.h"
#include "vectors_data.h"

using namespace lran;
using namespace lran::sim;

void setUp() {}
void tearDown() {}

namespace {

// Values from tools/vectors/generate.py.
constexpr NodeId   kBridge   = 0x00;
constexpr NodeId   kGateLink = 0x01;
constexpr NodeId   kWellLink = 0x02;
constexpr uint32_t kGateCtx  = 0x89ABCDEF;

refimpl::RefMac g_mac;
refimpl::RefKdf g_kdf;
uint8_t         g_gate_key[kNodeKeyLen];

const lran_vectors::NegVec& vec(const char* name) {
  for (size_t i = 0; i < lran_vectors::kNegativeCount; ++i) {
    if (strcmp(lran_vectors::kNegative[i].name, name) == 0) return lran_vectors::kNegative[i];
  }
  TEST_FAIL_MESSAGE(name);
  return lran_vectors::kNegative[0];
}

// The vector's payload region, for frames with no MAC and an intact header.
const uint8_t* vec_payload(const lran_vectors::NegVec& v) { return v.frame + kHdrLen; }

void expect_vector(const char* name, const FramePatch& fp) {
  const lran_vectors::NegVec& v = vec(name);
  TEST_ASSERT_NOT_NULL_MESSAGE(fp.frame(), name);
  TEST_ASSERT_EQUAL_UINT_MESSAGE(v.frame_len, fp.len(), name);
  TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(v.frame, fp.frame(), v.frame_len, name);
}

Header hdr(MsgType type, NodeId src, NodeId dst, Seq seq, SchemaId schema = kSchemaNone) {
  Header h;
  h.type   = type;
  h.src    = src;
  h.dst    = dst;
  h.seq    = seq;
  h.ctx_id = kGateCtx;
  h.schema = schema;
  return h;
}

// generate.py's good_poll(): POLL bridge -> GateLink, poll_flags 0x01.
void good_poll(FramePatch* fp, Seq seq) {
  const uint8_t p[] = {0x01};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(fp->encode(hdr(MsgType::Poll, kBridge, kGateLink, seq),
                                                    p, sizeof(p), EncodeCtx{})));
}

EncodeCtx gate_ctx() {
  EncodeCtx c;
  c.mac      = &g_mac;
  c.node_key = g_gate_key;
  return c;
}

void ok(PatchStatus s) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PatchStatus::Ok), static_cast<int>(s));
}

void is(PatchStatus want, PatchStatus got) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(want), static_cast<int>(got));
}

}  // namespace

// --- the W4 witness ------------------------------------------------------------------

void test_w4_header_byte_patches_on_a_poll() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));

  good_poll(&fp, 4672);
  ok(fp.set_header(HdrByte::Ver, 3));
  ok(fp.seal(Seal::Crc));
  expect_vector("unknown_ver_3", fp);

  good_poll(&fp, 4673);
  ok(fp.set_header(HdrByte::Dst, kWellLink));
  ok(fp.seal(Seal::Crc));
  expect_vector("wrong_dst_not_addressed", fp);

  good_poll(&fp, 4674);
  ok(fp.set_header(HdrByte::HdrFlags, kHdrFlagCriticalExt));
  ok(fp.seal(Seal::Crc));
  expect_vector("critical_ext_unimplemented", fp);

  good_poll(&fp, 4675);
  ok(fp.set_frag(0, 0));  // frag_zero
  ok(fp.seal(Seal::Crc));
  expect_vector("frag_total_zero", fp);
}

void test_w4_runt_and_bad_crc() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));

  good_poll(&fp, 4660);
  ok(fp.truncate_body(15));
  ok(fp.seal(Seal::Crc));
  expect_vector("runt_17_bytes", fp);

  good_poll(&fp, 4671);
  ok(fp.flip_crc(1, 0xFF));
  expect_vector("bad_app_crc16", fp);
}

// `oversize`: a 205-byte PING payload that encode() refuses, reached by growing a 204-byte
// one. The vector's `n` is 203, so the grown frame's `n` byte is patched to match.
void test_w4_oversize_by_growing_a_maximum_ping() {
  const lran_vectors::NegVec& v = vec("oversize_223_bytes");
  uint8_t start[kMaxPayloadPlain];
  memcpy(start, vec_payload(v), sizeof(start));
  start[1] = static_cast<uint8_t>(kPingMaxEcho);

  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(fp.encode(hdr(MsgType::Ping, kBridge, kGateLink, 4670),
                                                   start, sizeof(start), EncodeCtx{})));
  ok(fp.set_payload(1, 203));
  ok(fp.resize_payload(205, vec_payload(v)[204]));
  ok(fp.seal(Seal::Crc));
  expect_vector("oversize_223_bytes", fp);
}

void test_w4_frag_index_past_total() {
  const lran_vectors::NegVec& v = vec("frag_index_ge_total");
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));
  // encode() accepts a PING fragment; the index is the only thing patched.
  Header h = hdr(MsgType::Ping, kBridge, kGateLink, 4676);
  h.set_frag(0, 2);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(fp.encode(h, vec_payload(v), 14, EncodeCtx{})));
  ok(fp.set_header(HdrByte::Frag, 0x53));
  ok(fp.seal(Seal::Crc));
  expect_vector("frag_index_ge_total", fp);
}

void test_w4_type_patches_drop_the_payload() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));

  good_poll(&fp, 4678);
  ok(fp.set_header(HdrByte::Type, 0x0C));
  ok(fp.resize_payload(0, 0));
  ok(fp.seal(Seal::Crc));
  expect_vector("unknown_type_0x0c", fp);

  good_poll(&fp, 4685);
  ok(fp.set_header(HdrByte::Type, 0x00));
  ok(fp.resize_payload(0, 0));
  ok(fp.seal(Seal::Crc));
  expect_vector("reserved_type_0x00", fp);
}

void test_w4_schema_and_type_pairing() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));

  // An EVENT 0x11 frame re-typed as STATUS.
  const lran_vectors::NegVec& pair = vec("status_carrying_event_schema");
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(Status::Ok),
      static_cast<int>(fp.encode(hdr(MsgType::Event, kGateLink, kBridge, 4679,
                                     kSchemaGateLinkEventV1),
                                 vec_payload(pair), 16, EncodeCtx{})));
  ok(fp.set_header(HdrByte::Type, static_cast<uint8_t>(MsgType::Status)));
  ok(fp.seal(Seal::Crc));
  expect_vector("status_carrying_event_schema", fp);

  const lran_vectors::NegVec& unk = vec("unknown_schema_0x99");
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(Status::Ok),
      static_cast<int>(fp.encode(hdr(MsgType::Status, kGateLink, kBridge, 4680,
                                     kSchemaNodeHealthV1),
                                 vec_payload(unk), 20, EncodeCtx{})));
  ok(fp.set_header(HdrByte::Schema, 0x99));
  ok(fp.seal(Seal::Crc));
  expect_vector("unknown_schema_0x99", fp);
}

void test_w4_bad_length_both_directions() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));

  good_poll(&fp, 4681);
  ok(fp.resize_payload(2, 0x00));
  ok(fp.seal(Seal::Crc));
  expect_vector("poll_payload_two_bytes", fp);

  // The vector keeps a 78-byte payload's first 76 bytes. The last two are never on the
  // wire, so any value starts the encode.
  const lran_vectors::NegVec& v = vec("status_0x10_76_bytes");
  uint8_t full[78] = {};
  memcpy(full, vec_payload(v), 76);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(Status::Ok),
      static_cast<int>(fp.encode(hdr(MsgType::Status, kGateLink, kBridge, 4682,
                                     kSchemaGateLinkStatusV1),
                                 full, sizeof(full), EncodeCtx{})));
  ok(fp.resize_payload(76, 0));
  ok(fp.seal(Seal::Crc));
  expect_vector("status_0x10_76_bytes", fp);
}

void test_w4_frag_on_unfragmentable_types() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));

  const lran_vectors::NegVec& hex = vec("fragmented_hex_req");
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(Status::Ok),
      static_cast<int>(fp.encode(hdr(MsgType::HexReq, kBridge, kGateLink, 4683),
                                 vec_payload(hex), 12, EncodeCtx{})));
  TEST_ASSERT_FALSE(fp.has_mac());  // a Get: the nibble is 7
  TEST_ASSERT_EQUAL_UINT(12, fp.payload_len());
  ok(fp.set_frag(0, 2));
  ok(fp.seal(Seal::Crc));
  expect_vector("fragmented_hex_req", fp);

  // `frag_command`: the generator signed the patched header, so the MAC is valid and
  // stage 8a must refuse the frame before stage 9 would accept it.
  const lran_vectors::NegVec& cmd = vec("fragmented_command");
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(Status::Ok),
      static_cast<int>(fp.encode(hdr(MsgType::Command, kBridge, kGateLink, 4684),
                                 vec_payload(cmd), 4, gate_ctx())));
  ok(fp.set_frag(0, 2));
  ok(fp.seal(Seal::MacAndCrc));
  expect_vector("fragmented_command", fp);
}

void test_w4_mac_patches() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));

  const lran_vectors::NegVec& forged = vec("command_corrupt_mac");
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(Status::Ok),
      static_cast<int>(fp.encode(hdr(MsgType::Command, kBridge, kGateLink, 6),
                                 vec_payload(forged), 4, gate_ctx())));
  ok(fp.flip_mac(kMacLen - 1, 0xFF));
  ok(fp.seal(Seal::Crc));  // Seal::MacAndCrc would repair the flip
  expect_vector("command_corrupt_mac", fp);

  const lran_vectors::NegVec& unsigned_cmd = vec("command_with_no_mac");
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(Status::Ok),
      static_cast<int>(fp.encode(hdr(MsgType::Command, kBridge, kGateLink, 9),
                                 vec_payload(unsigned_cmd), 4, gate_ctx())));
  ok(fp.strip_mac());
  ok(fp.seal(Seal::Crc));
  expect_vector("command_with_no_mac", fp);
}

// --- the offsets, proven against the codec -------------------------------------------

// HdrByte is the one piece of layout this library states. Each offset is patched in a
// real frame and read back through decode_header(); a wrong offset changes a different
// field and fails here.
void test_every_header_offset_matches_decode_header() {
  struct Row { HdrByte field; uint8_t value; };
  const Row rows[] = {
      {HdrByte::Ver, 1},       {HdrByte::Type, 0x04},     {HdrByte::Src, 0x33},
      {HdrByte::Dst, 0x44},    {HdrByte::Frag, 0x13},     {HdrByte::Schema, 0xF0},
      {HdrByte::HdrFlags, 0x7F}, {HdrByte::Reserved0, 0xA1}, {HdrByte::Reserved1, 0xB2},
      {HdrByte::Reserved2, 0xC3},
  };
  for (const Row& row : rows) {
    uint8_t buf[kPhyMaxFrame];
    FramePatch fp(buf, sizeof(buf));
    good_poll(&fp, 1);
    ok(fp.set_header(row.field, row.value));
    ok(fp.seal(Seal::Crc));

    Header before = hdr(MsgType::Poll, kBridge, kGateLink, 1);
    DecodeCtx dc;
    dc.self           = (row.field == HdrByte::Dst) ? row.value : kGateLink;
    dc.accept_ver_min = 0;
    dc.accept_ver_max = 0xFF;
    Frame f;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(decode_header(fp.frame(), fp.len(), dc, &f)));

    // Apply the same change to the Header the frame was built from; nothing else moves.
    switch (row.field) {
      case HdrByte::Ver:       before.ver = row.value; break;
      case HdrByte::Type:      before.type = static_cast<MsgType>(row.value); break;
      case HdrByte::Src:       before.src = row.value; break;
      case HdrByte::Dst:       before.dst = row.value; break;
      case HdrByte::Frag:      before.frag = row.value; break;
      case HdrByte::Schema:    before.schema = row.value; break;
      case HdrByte::HdrFlags:  before.hdr_flags = row.value; break;
      case HdrByte::Reserved0: before.reserved[0] = row.value; break;
      case HdrByte::Reserved1: before.reserved[1] = row.value; break;
      case HdrByte::Reserved2: before.reserved[2] = row.value; break;
    }
    TEST_ASSERT_EQUAL_UINT8(before.ver, f.hdr.ver);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(before.type), static_cast<uint8_t>(f.hdr.type));
    TEST_ASSERT_EQUAL_UINT8(before.src, f.hdr.src);
    TEST_ASSERT_EQUAL_UINT8(before.dst, f.hdr.dst);
    TEST_ASSERT_EQUAL_UINT16(before.seq, f.hdr.seq);
    TEST_ASSERT_EQUAL_UINT32(before.ctx_id, f.hdr.ctx_id);
    TEST_ASSERT_EQUAL_UINT8(before.frag, f.hdr.frag);
    TEST_ASSERT_EQUAL_UINT8(before.schema, f.hdr.schema);
    TEST_ASSERT_EQUAL_UINT8(before.hdr_flags, f.hdr.hdr_flags);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before.reserved, f.hdr.reserved, 3);
  }
}

// --- the catalogue frames the vectors do not cover -----------------------------------

// 10.5 `oversize` says the PHY hands up 255 bytes. FramePatch reaches exactly that, and
// the codec counts it at stage 2a.
void test_oversize_reaches_the_phy_maximum_and_stops_there() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));
  good_poll(&fp, 2);
  const size_t max_payload = kPhyMaxFrame - kHdrLen - kCrcLen;
  is(PatchStatus::OutOfRange, fp.resize_payload(max_payload + 1, 0));
  ok(fp.resize_payload(max_payload, 0xAA));
  ok(fp.seal(Seal::Crc));
  TEST_ASSERT_EQUAL_UINT(kPhyMaxFrame, fp.len());

  Counters  c;
  DecodeCtx dc;
  dc.self     = kGateLink;
  dc.counters = &c;
  Frame f;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Oversize),
                        static_cast<int>(decode_header(fp.frame(), fp.len(), dc, &f)));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_oversize);
}

// 10.5 `hdr_rsv`: non-zero reserved bytes on an authenticated frame, resigned, must be
// accepted - the forward-compatibility rule (spec 4.3).
void test_reserved_bytes_resigned_pass_verification() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));
  const uint8_t cmd[] = {0x01, 0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(fp.encode(hdr(MsgType::Command, kBridge, kGateLink, 3),
                                                   cmd, sizeof(cmd), gate_ctx())));
  ok(fp.set_header(HdrByte::Reserved1, 0x5A));
  ok(fp.seal(Seal::MacAndCrc));

  DecodeCtx dc;
  dc.self          = kGateLink;
  dc.mac           = &g_mac;
  dc.node_key      = g_gate_key;
  dc.expect_ctx_id = kGateCtx;
  Frame f;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_header(fp.frame(), fp.len(), dc, &f)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_payload(fp.frame(), fp.len(), dc, &f)));
  TEST_ASSERT_TRUE(f.mac_verified);
  TEST_ASSERT_EQUAL_UINT8(0x5A, f.hdr.reserved[1]);
}

// A MAC that moved with a resized payload is stale until resigned: Seal::Crc leaves it
// failing at stage 9, Seal::MacAndCrc makes it verify. Only the length is wrong then.
void test_resize_moves_the_mac_and_resigning_is_the_callers_choice() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));
  const uint8_t cmd[] = {0x01, 0x00, 0x00, 0x00};
  const Header  h     = hdr(MsgType::Command, kBridge, kGateLink, 4);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(fp.encode(h, cmd, sizeof(cmd), gate_ctx())));
  uint8_t mac[kMacLen];
  memcpy(mac, buf + kHdrLen + 4, kMacLen);

  ok(fp.resize_payload(5, 0xEE));
  TEST_ASSERT_EQUAL_UINT(5, fp.payload_len());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, buf + kHdrLen + 5, kMacLen);
  TEST_ASSERT_EQUAL_UINT8(0xEE, buf[kHdrLen + 4]);

  ok(fp.seal(Seal::MacAndCrc));
  uint8_t want[kMacLen];
  g_mac.hmac_sha256_trunc(g_gate_key, kNodeKeyLen, fp.frame(), kHdrLen + 5, want);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, fp.frame() + kHdrLen + 5, kMacLen);
}

// --- seal discipline -----------------------------------------------------------------

void test_a_patched_frame_is_unreadable_until_sealed() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));
  TEST_ASSERT_NULL(fp.frame());
  is(PatchStatus::NoFrame, fp.set_header(HdrByte::Ver, 1));
  is(PatchStatus::NoFrame, fp.seal(Seal::Crc));

  good_poll(&fp, 5);
  TEST_ASSERT_NOT_NULL(fp.frame());  // encode() output is already sealed
  ok(fp.set_header(HdrByte::Dst, 0x02));
  TEST_ASSERT_NULL(fp.frame());
  TEST_ASSERT_EQUAL_UINT(0, fp.len());
  is(PatchStatus::NotSealed, fp.flip_crc(0, 0xFF));
  ok(fp.seal(Seal::Crc));
  TEST_ASSERT_NOT_NULL(fp.frame());
}

void test_refusals_name_the_reason() {
  uint8_t buf[kPhyMaxFrame];
  FramePatch fp(buf, sizeof(buf));

  good_poll(&fp, 6);
  is(PatchStatus::NoMac, fp.strip_mac());
  is(PatchStatus::NoMac, fp.flip_mac(0, 1));
  is(PatchStatus::NoMac, fp.seal(Seal::MacAndCrc));
  is(PatchStatus::OutOfRange, fp.set_payload(1, 0));  // POLL is one byte
  is(PatchStatus::OutOfRange, fp.flip_crc(2, 1));

  // A COMMAND encoded with a key, then resealed through a context with none.
  const uint8_t cmd[] = {0x01, 0x00, 0x00, 0x00};
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(fp.encode(hdr(MsgType::Command, kBridge, kGateLink, 7),
                                                   cmd, sizeof(cmd), gate_ctx())));
  is(PatchStatus::OutOfRange, fp.flip_mac(kMacLen, 1));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::MissingMac),
                        static_cast<int>(fp.encode(hdr(MsgType::Command, kBridge, kGateLink, 7),
                                                   cmd, sizeof(cmd), EncodeCtx{})));
  TEST_ASSERT_NULL(fp.frame());
  is(PatchStatus::NoFrame, fp.set_frag(0, 2));

  // After truncation there is no payload or MAC to address.
  good_poll(&fp, 8);
  ok(fp.truncate_body(10));
  is(PatchStatus::Truncated, fp.set_payload(0, 0));
  is(PatchStatus::Truncated, fp.resize_payload(1, 0));
  is(PatchStatus::Truncated, fp.seal(Seal::MacAndCrc));
  is(PatchStatus::OutOfRange, fp.set_header(HdrByte::Frag, 0));
  ok(fp.set_header(HdrByte::Dst, 0x02));
  ok(fp.seal(Seal::Crc));
  TEST_ASSERT_EQUAL_UINT(12, fp.len());
}

// A buffer smaller than kPhyMaxFrame caps growth at the buffer.
void test_growth_is_capped_by_a_small_buffer() {
  uint8_t buf[40];
  FramePatch fp(buf, sizeof(buf));
  good_poll(&fp, 9);
  is(PatchStatus::OutOfRange, fp.resize_payload(40 - kHdrLen - kCrcLen + 1, 0));
  ok(fp.resize_payload(40 - kHdrLen - kCrcLen, 0));
}

int main() {
  g_kdf.derive_node_key(lran_test::kTestMasterKey, kGateLink, g_gate_key);

  UNITY_BEGIN();
  RUN_TEST(test_w4_header_byte_patches_on_a_poll);
  RUN_TEST(test_w4_runt_and_bad_crc);
  RUN_TEST(test_w4_oversize_by_growing_a_maximum_ping);
  RUN_TEST(test_w4_frag_index_past_total);
  RUN_TEST(test_w4_type_patches_drop_the_payload);
  RUN_TEST(test_w4_schema_and_type_pairing);
  RUN_TEST(test_w4_bad_length_both_directions);
  RUN_TEST(test_w4_frag_on_unfragmentable_types);
  RUN_TEST(test_w4_mac_patches);
  RUN_TEST(test_every_header_offset_matches_decode_header);
  RUN_TEST(test_oversize_reaches_the_phy_maximum_and_stops_there);
  RUN_TEST(test_reserved_bytes_resigned_pass_verification);
  RUN_TEST(test_resize_moves_the_mac_and_resigning_is_the_callers_choice);
  RUN_TEST(test_a_patched_frame_is_unreadable_until_sealed);
  RUN_TEST(test_refusals_name_the_reason);
  RUN_TEST(test_growth_is_capped_by_a_small_buffer);
  return UNITY_END();
}
