// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-19a - spec 14.2's ERROR replies, and the two bounds that keep an unauthenticated
// frame from turning the bridge into somebody else's transmitter.

#include <unity.h>

#include <cstring>

#include "error_reply.h"
#include "lran/codec.h"
#include "lran/config.h"

using namespace bridge;
using lran::ErrCode;
using lran::NodeId;
using lran::Status;

namespace {

constexpr NodeId kPeer  = lran::kNodeGateLink;
constexpr NodeId kPeer2 = lran::kNodeWellLink;

}  // namespace

// --- the mapping, spec 14's "On failure" column -----------------------------

void test_every_stage_that_names_an_error_maps_to_its_wire_code() {
  struct Row {
    Status  s;
    ErrCode code;
  };
  // spec 14: BAD_LENGTH deliberately answers three stages with three counters. The wire
  // answer and the diagnosis are different questions (spec 5.6, 11.4).
  static const Row kRows[] = {
      {Status::UnknownHdrExt, ErrCode::UnknownHdrExt},
      {Status::BadFrag, ErrCode::BadLength},
      {Status::UnknownType, ErrCode::UnknownType},
      {Status::UnknownSchema, ErrCode::UnknownSchema},
      {Status::BadLength, ErrCode::BadLength},
      {Status::NotFragmentable, ErrCode::BadLength},
      {Status::ReassemblyTimeout, ErrCode::ReassemblyTimeout},
      {Status::FragmentOverflow, ErrCode::FragmentOverflow},
  };
  for (const Row& r : kRows) {
    ErrCode got = ErrCode::CtxMismatch;
    TEST_ASSERT_TRUE_MESSAGE(error_code_for(r.s, &got), lran::to_string(r.s));
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(r.code), static_cast<int>(got),
                                  lran::to_string(r.s));
  }
}

// The silences, each for its own reason. A row moving out of this list is a decision, not
// a refactor: every one of them sends a frame in answer to an unauthenticated header.
void test_the_stages_that_answer_with_silence_stay_silent() {
  static const Status kSilent[] = {
      Status::Ok,
      Status::Runt,           // stage 2  - no readable src
      Status::Oversize,       // stage 2a - no readable src
      Status::BadCrc,         // stage 3  - spec 14 optional, not built
      Status::BadVersion,     // stage 4  - spec 14 optional, BF-22's to answer
      Status::NotAddressed,   // stage 5  - not ours to answer
      Status::UnknownSrc,     // stage 9a - spec 14.2, never answer a stranger
      Status::RejectedMac,    // stage 9  - COMMAND_ACK, BF-18
      Status::RejectedCtx,    // stage 9  - COMMAND_ACK, BF-18
      Status::RejectedSeq,    // stage 11 - COMMAND_ACK, BF-18
      Status::DuplicateCached,
      Status::FragLate,       // normal traffic, not a fault
  };
  for (Status s : kSilent) {
    ErrCode got = ErrCode::CtxMismatch;
    TEST_ASSERT_FALSE_MESSAGE(error_code_for(s, &got), lran::to_string(s));
  }
}

// --- bound 1: a registered source only --------------------------------------

void test_a_source_the_registry_does_not_know_is_never_answered() {
  ErrorReplyPolicy p;
  const ErrorReply r = p.decide(Status::UnknownType, 0x7E, 5, /*src_registered=*/false, 1000);
  TEST_ASSERT_FALSE(r.send);
  // Not rate-limiting - refusal. Counting it as suppressed would report a stranger's
  // traffic as the bridge holding its tongue about a real peer.
  TEST_ASSERT_EQUAL_UINT32(0, p.suppressed());
}

// spec 14.2 - an unregistered flood must not evict a real peer's timestamp, which is why
// the registration check runs before the table is touched.
void test_unregistered_traffic_takes_no_slot_in_the_table() {
  ErrorReplyPolicy p;
  for (uint32_t i = 0; i < 100; ++i) {
    (void)p.decide(Status::UnknownType, static_cast<NodeId>(0x10 + (i % 64)), 1, false, i);
  }
  const ErrorReply r = p.decide(Status::UnknownType, kPeer, 7, true, 1000);
  TEST_ASSERT_TRUE(r.send);
  TEST_ASSERT_EQUAL_UINT8(kPeer, r.dst);
}

// --- bound 2: rate-limited per source ---------------------------------------

void test_a_first_reply_to_a_peer_is_never_rate_limited() {
  ErrorReplyPolicy p;
  // now_ms below the default interval: a naive "now - last >= interval" against a
  // zero-initialised table refuses the first reply of every boot for a second.
  const ErrorReply r = p.decide(Status::UnknownType, kPeer, 1, true, 5);
  TEST_ASSERT_TRUE(r.send);
}

void test_a_second_reply_inside_the_window_is_suppressed_and_counted() {
  ErrorReplyPolicy p;
  TEST_ASSERT_TRUE(p.decide(Status::UnknownType, kPeer, 1, true, 10000).send);
  TEST_ASSERT_FALSE(p.decide(Status::UnknownType, kPeer, 2, true, 10001).send);
  TEST_ASSERT_FALSE(p.decide(Status::BadLength, kPeer, 3, true, 10999).send);
  TEST_ASSERT_EQUAL_UINT32(2, p.suppressed());

  // The window is a floor, not a lockout.
  TEST_ASSERT_TRUE(p.decide(Status::UnknownType, kPeer, 4, true, 11000).send);
  TEST_ASSERT_EQUAL_UINT32(2, p.suppressed());
}

// The limit is per source, so one noisy peer cannot silence another's diagnosis.
void test_the_limit_is_per_peer() {
  ErrorReplyPolicy p;
  TEST_ASSERT_TRUE(p.decide(Status::UnknownType, kPeer, 1, true, 10000).send);
  TEST_ASSERT_FALSE(p.decide(Status::UnknownType, kPeer, 2, true, 10100).send);
  TEST_ASSERT_TRUE(p.decide(Status::UnknownType, kPeer2, 1, true, 10100).send);
}

void test_the_interval_is_runtime_settable() {
  ErrorReplyPolicy p;
  p.configure(100);
  TEST_ASSERT_TRUE(p.decide(Status::UnknownType, kPeer, 1, true, 10000).send);
  TEST_ASSERT_FALSE(p.decide(Status::UnknownType, kPeer, 2, true, 10050).send);
  TEST_ASSERT_TRUE(p.decide(Status::UnknownType, kPeer, 3, true, 10100).send);
}

// A millis() rollover must read as a long age, not as a peer locked out for 24.8 days.
void test_the_window_survives_a_millis_rollover() {
  ErrorReplyPolicy p;
  const uint32_t before = UINT32_MAX - 500;
  TEST_ASSERT_TRUE(p.decide(Status::UnknownType, kPeer, 1, true, before).send);
  TEST_ASSERT_FALSE(p.decide(Status::UnknownType, kPeer, 2, true, before + 100).send);
  TEST_ASSERT_TRUE(p.decide(Status::UnknownType, kPeer, 3, true, before + 1500).send);
}

// --- the frame ---------------------------------------------------------------

void test_the_reply_frame_carries_spec_14_2_s_header() {
  ErrorReplyPolicy p;
  const ErrorReply r = p.decide(Status::UnknownSchema, kPeer, 0x1234, true, 1000);
  TEST_ASSERT_TRUE(r.send);

  uint8_t      buf[lran::kMaxFrame];
  const size_t len = build_error_frame(r, 9, buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, len);

  lran::Frame     f;
  lran::DecodeCtx ctx;
  ctx.self = kPeer;  // decode as the node being answered
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(lran::decode_header(buf, len, ctx, &f)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(lran::MsgType::Error), static_cast<int>(f.hdr.type));
  TEST_ASSERT_EQUAL_UINT8(lran::kNodeBridge, f.hdr.src);
  TEST_ASSERT_EQUAL_UINT8(kPeer, f.hdr.dst);
  TEST_ASSERT_EQUAL_UINT16(9, f.hdr.seq);
  // spec 14.2 - unknown, and a node must not adopt it (spec 5.5).
  TEST_ASSERT_EQUAL_UINT32(0, f.hdr.ctx_id);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(lran::decode_payload(buf, len, ctx, &f)));
  TEST_ASSERT_EQUAL_UINT(lran::msg::kErrorLen, f.payload_len);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ErrCode::UnknownSchema), f.payload[0]);
  TEST_ASSERT_EQUAL_UINT8(0, f.payload[1]);                    // detail
  TEST_ASSERT_EQUAL_UINT8(0x34, f.payload[2]);                 // ref_seq, little-endian
  TEST_ASSERT_EQUAL_UINT8(0x12, f.payload[3]);
}

// spec 9.2 - ERROR is unauthenticated, so the frame is the plain 22 bytes and carries no
// MAC. A reply that needed a key could not answer the frames this exists to answer.
void test_the_reply_carries_no_mac() {
  ErrorReplyPolicy p;
  const ErrorReply r = p.decide(Status::UnknownType, kPeer, 1, true, 1000);
  uint8_t          buf[lran::kMaxFrame];
  const size_t     len = build_error_frame(r, 1, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT(lran::kHdrLen + lran::msg::kErrorLen + lran::kCrcLen, len);
}

void test_a_decision_not_to_send_builds_nothing() {
  ErrorReply r;  // send = false
  uint8_t    buf[lran::kMaxFrame];
  TEST_ASSERT_EQUAL_UINT(0, build_error_frame(r, 1, buf, sizeof(buf)));
}

void test_a_buffer_too_small_builds_nothing() {
  ErrorReplyPolicy p;
  const ErrorReply r = p.decide(Status::UnknownType, kPeer, 1, true, 1000);
  uint8_t          small[8];
  TEST_ASSERT_EQUAL_UINT(0, build_error_frame(r, 1, small, sizeof(small)));
  TEST_ASSERT_EQUAL_UINT(0, build_error_frame(r, 1, nullptr, lran::kMaxFrame));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_stage_that_names_an_error_maps_to_its_wire_code);
  RUN_TEST(test_the_stages_that_answer_with_silence_stay_silent);
  RUN_TEST(test_a_source_the_registry_does_not_know_is_never_answered);
  RUN_TEST(test_unregistered_traffic_takes_no_slot_in_the_table);
  RUN_TEST(test_a_first_reply_to_a_peer_is_never_rate_limited);
  RUN_TEST(test_a_second_reply_inside_the_window_is_suppressed_and_counted);
  RUN_TEST(test_the_limit_is_per_peer);
  RUN_TEST(test_the_interval_is_runtime_settable);
  RUN_TEST(test_the_window_survives_a_millis_rollover);
  RUN_TEST(test_the_reply_frame_carries_spec_14_2_s_header);
  RUN_TEST(test_the_reply_carries_no_mac);
  RUN_TEST(test_a_decision_not_to_send_builds_nothing);
  RUN_TEST(test_a_buffer_too_small_builds_nothing);
  return UNITY_END();
}
