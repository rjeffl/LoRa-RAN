// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R9 host tests. The fragmentation and pattern paths are exercised here against the
// real codec, so a fault is found at the desk rather than at the far end of a walk.

#include <unity.h>

#include <cstring>

#include "lran/codec.h"
#include "lran/reassembly.h"
#include "w9.h"

using namespace rangetest;

void setUp() {}
void tearDown() {}

// --- the plan -------------------------------------------------------------

// spec 6.6.1 - the whole point of run 1 is that the frame is exactly LRAN_MAX_FRAME.
void test_max_frame_run_is_exactly_222_bytes() {
  const W9Plan p = w9_plan(W9Run::MaxFrame);
  TEST_ASSERT_EQUAL_UINT8(202, p.echo_n);
  TEST_ASSERT_EQUAL_UINT16(204, p.payload_len);
  TEST_ASSERT_EQUAL_UINT8(1, p.expect_frags);
  TEST_ASSERT_EQUAL_size_t(0, p.frag_chunk);

  // PING carries no MAC (spec 9.2), so the frame is header + payload + CRC.
  TEST_ASSERT_EQUAL_size_t(lran::kMaxFrame, lran::frame_len(p.payload_len, false));
}

// spec 6.6.2 - frag_chunk 14 on a 202-byte echo is the FULL 15-fragment set. The
// task text names both numbers, and 15 is kMaxFragments, so this is the ceiling.
void test_fragmented_run_is_the_full_fifteen_fragment_set() {
  const W9Plan p = w9_plan(W9Run::Fragmented);
  TEST_ASSERT_EQUAL_size_t(14, p.frag_chunk);
  TEST_ASSERT_EQUAL_UINT8(15, p.expect_frags);
  TEST_ASSERT_EQUAL_UINT8(lran::kMaxFragments, p.expect_frags);
}

// --- building a PING ------------------------------------------------------

void test_build_ping_sets_pattern_fill_and_the_pattern() {
  uint8_t buf[lran::kMaxPayloadPlain];
  size_t  len = 0;
  TEST_ASSERT_EQUAL(lran::Status::Ok,
                    w9_build_ping(0x1234, kW9EchoBytes, buf, sizeof(buf), &len));
  TEST_ASSERT_EQUAL_size_t(204, len);
  TEST_ASSERT_EQUAL_UINT8(0x01, buf[0]);  // spec 6.6.3 PATTERN_FILL
  TEST_ASSERT_EQUAL_UINT8(202, buf[1]);

  // spec 6.6.3 - data[i] = (seq & 0xFF) + i
  TEST_ASSERT_EQUAL_UINT8(0x34, buf[2]);
  TEST_ASSERT_EQUAL_UINT8(0x35, buf[3]);
  TEST_ASSERT_TRUE(lran::msg::ping_check_pattern(0x1234, buf + 2, 202, nullptr));
}

void test_build_ping_refuses_an_echo_over_the_cap() {
  uint8_t buf[lran::kMaxPayloadPlain];
  size_t  len = 0;
  // n is a uint8 and the cap is 202, so 203 is representable and must be refused.
  TEST_ASSERT_EQUAL(lran::Status::BadLength,
                    w9_build_ping(1, 203, buf, sizeof(buf), &len));
}

void test_build_ping_refuses_a_buffer_it_would_overrun() {
  uint8_t small[16];
  size_t  len = 0;
  TEST_ASSERT_EQUAL(lran::Status::BadLength,
                    w9_build_ping(1, kW9EchoBytes, small, sizeof(small), &len));
}

// --- checking an echo -----------------------------------------------------

void test_a_clean_echo_passes() {
  uint8_t buf[lran::kMaxPayloadPlain];
  size_t  len = 0;
  w9_build_ping(0x0042, kW9EchoBytes, buf, sizeof(buf), &len);

  const W9EchoCheck c = w9_check_echo(0x0042, kW9EchoBytes, buf, len);
  TEST_ASSERT_TRUE(c.ok());
  TEST_ASSERT_EQUAL_UINT8(202, c.n);
}

// spec 6.6.3 - the offset is the finding. A count of failures would not distinguish
// a marginal path from a reassembly fault; the offset does.
void test_a_corrupt_byte_is_reported_at_its_offset() {
  uint8_t buf[lran::kMaxPayloadPlain];
  size_t  len = 0;
  w9_build_ping(0x0042, kW9EchoBytes, buf, sizeof(buf), &len);

  buf[2 + 168] ^= 0xFF;  // deep inside what would be fragment 12 of the set

  const W9EchoCheck c = w9_check_echo(0x0042, kW9EchoBytes, buf, len);
  TEST_ASSERT_FALSE(c.ok());
  TEST_ASSERT_EQUAL(lran::Status::Ok, c.status);  // it DECODED fine - spec 14 saw nothing
  TEST_ASSERT_FALSE(c.pattern_ok);
  TEST_ASSERT_EQUAL_size_t(168, c.first_bad);
}

// spec 6.6 - the responder echoes `n` verbatim. A short echo means the far end
// rebuilt the payload instead of echoing it, which is a finding, not a near miss.
void test_a_short_echo_is_a_length_fault_not_a_pattern_fault() {
  uint8_t buf[lran::kMaxPayloadPlain];
  size_t  len = 0;
  w9_build_ping(7, 64, buf, sizeof(buf), &len);

  const W9EchoCheck c = w9_check_echo(7, kW9EchoBytes, buf, len);
  TEST_ASSERT_FALSE(c.ok());
  TEST_ASSERT_EQUAL(lran::Status::BadLength, c.status);
}

// spec 6.6 - ping_flags is preserved by the responder. An echo that dropped
// PATTERN_FILL still carries the right bytes, so only the flag check catches it.
void test_a_dropped_pattern_fill_flag_is_caught() {
  uint8_t buf[lran::kMaxPayloadPlain];
  size_t  len = 0;
  w9_build_ping(9, 32, buf, sizeof(buf), &len);
  buf[0] = 0x00;

  const W9EchoCheck c = w9_check_echo(9, 32, buf, len);
  TEST_ASSERT_FALSE(c.ok());
  TEST_ASSERT_TRUE(c.pattern_ok);   // the bytes are fine
  TEST_ASSERT_FALSE(c.flags_ok);    // the flag is not
}

// The pattern is keyed on seq (spec 6.6.3), so an echo of the WRONG ping - a stale
// one still in flight - fails even though it is a perfectly formed PING.
void test_an_echo_of_a_different_seq_fails_the_pattern() {
  uint8_t buf[lran::kMaxPayloadPlain];
  size_t  len = 0;
  w9_build_ping(100, 32, buf, sizeof(buf), &len);

  const W9EchoCheck c = w9_check_echo(101, 32, buf, len);
  TEST_ASSERT_FALSE(c.ok());
  TEST_ASSERT_FALSE(c.pattern_ok);
  TEST_ASSERT_EQUAL_size_t(0, c.first_bad);
}

// --- the whole fragmented round trip, against the real codec --------------

// This is run 2 end to end at the desk: build, fragment, reassemble, check. If the
// 15-fragment path is broken, it is broken here and not 500 ft from the house.
void test_fragmented_ping_survives_a_full_round_trip() {
  const W9Plan p = w9_plan(W9Run::Fragmented);
  const lran::Seq seq = 0x00AB;

  uint8_t payload[lran::kMaxPayloadPlain];
  size_t  payload_len = 0;
  TEST_ASSERT_EQUAL(lran::Status::Ok,
                    w9_build_ping(seq, p.echo_n, payload, sizeof(payload), &payload_len));

  const lran::Header hdr = w9_header(seq, kW9Initiator, kW9Responder);
  lran::EncodeCtx enc;  // spec 9.2 - PING carries no MAC, so no key material
  lran::DecodeCtx dec;
  dec.self = kW9Responder;

  lran::Reassembler re;
  uint16_t largest_frag = 0;

  for (uint8_t i = 0; i < p.expect_frags; ++i) {
    uint8_t frame[lran::kMaxFrame];
    size_t  frame_len = 0;
    TEST_ASSERT_EQUAL(lran::Status::Ok,
                      lran::encode_fragment(hdr, payload, payload_len, i, p.frag_chunk,
                                            enc, frame, sizeof(frame), &frame_len));

    lran::Frame f;
    TEST_ASSERT_EQUAL(lran::Status::Ok, lran::decode_header(frame, frame_len, dec, &f));
    TEST_ASSERT_EQUAL(lran::Status::Ok, lran::decode_payload(frame, frame_len, dec, &f));
    TEST_ASSERT_EQUAL_UINT8(p.expect_frags, f.hdr.frag_total());
    TEST_ASSERT_EQUAL_UINT8(i, f.hdr.frag_index());

    if (f.payload_len > largest_frag) largest_frag = static_cast<uint16_t>(f.payload_len);
    TEST_ASSERT_EQUAL(lran::Status::Ok, re.accept(f, 1000));
  }

  TEST_ASSERT_TRUE(re.complete());
  TEST_ASSERT_EQUAL_size_t(payload_len, re.len());

  const W9EchoCheck c = w9_check_echo(seq, p.echo_n, re.data(), re.len());
  TEST_ASSERT_TRUE(c.ok());

  // The responder infers the chunk to echo with from the set it received.
  TEST_ASSERT_EQUAL_size_t(kW9FragChunk, w9_echo_chunk(largest_frag, p.expect_frags));
}

// spec 11 - fragments may arrive out of order, and the reassembler permutes them
// back into index order. Delivering the set backwards is the cheapest way to prove
// the initiator's check is not silently passing on arrival order.
void test_fragments_arriving_backwards_still_reassemble() {
  const W9Plan p = w9_plan(W9Run::Fragmented);
  const lran::Seq seq = 0x0F0F;

  uint8_t payload[lran::kMaxPayloadPlain];
  size_t  payload_len = 0;
  w9_build_ping(seq, p.echo_n, payload, sizeof(payload), &payload_len);

  const lran::Header hdr = w9_header(seq, kW9Initiator, kW9Responder);
  lran::EncodeCtx enc;
  lran::DecodeCtx dec;
  dec.self = kW9Responder;

  lran::Reassembler re;
  for (int i = p.expect_frags - 1; i >= 0; --i) {
    uint8_t frame[lran::kMaxFrame];
    size_t  frame_len = 0;
    lran::encode_fragment(hdr, payload, payload_len, static_cast<uint8_t>(i),
                          p.frag_chunk, enc, frame, sizeof(frame), &frame_len);
    lran::Frame f;
    lran::decode_header(frame, frame_len, dec, &f);
    lran::decode_payload(frame, frame_len, dec, &f);
    // now_ms is held constant: the reassembly timeout is not what is under test, and
    // a monotonic clock is a PRECONDITION of accept().
    re.accept(f, 1000);
  }

  TEST_ASSERT_TRUE(re.complete());
  const W9EchoCheck c = w9_check_echo(seq, p.echo_n, re.data(), re.len());
  TEST_ASSERT_TRUE(c.ok());
}

// spec 11.2 - a fragment of an already-completed set is a LATE fragment. R9 asks
// whether this link produces them at all; this proves the firmware can tell one from
// a fresh set when it does, so a clean "we saw none" means something.
void test_a_repeated_fragment_after_completion_reports_frag_late() {
  const W9Plan p = w9_plan(W9Run::Fragmented);
  const lran::Seq seq = 0x0055;

  uint8_t payload[lran::kMaxPayloadPlain];
  size_t  payload_len = 0;
  w9_build_ping(seq, p.echo_n, payload, sizeof(payload), &payload_len);

  const lran::Header hdr = w9_header(seq, kW9Initiator, kW9Responder);
  lran::EncodeCtx enc;
  lran::DecodeCtx dec;
  dec.self = kW9Responder;

  uint8_t last_frame[lran::kMaxFrame];
  size_t  last_len = 0;

  lran::Reassembler re;
  for (uint8_t i = 0; i < p.expect_frags; ++i) {
    uint8_t frame[lran::kMaxFrame];
    size_t  frame_len = 0;
    lran::encode_fragment(hdr, payload, payload_len, i, p.frag_chunk, enc, frame,
                          sizeof(frame), &frame_len);
    lran::Frame f;
    lran::decode_header(frame, frame_len, dec, &f);
    lran::decode_payload(frame, frame_len, dec, &f);
    re.accept(f, 1000);
    if (i + 1 == p.expect_frags) {
      memcpy(last_frame, frame, frame_len);
      last_len = frame_len;
    }
  }
  TEST_ASSERT_TRUE(re.complete());

  lran::Frame echo;
  lran::decode_header(last_frame, last_len, dec, &echo);
  lran::decode_payload(last_frame, last_len, dec, &echo);
  TEST_ASSERT_EQUAL(lran::Status::FragLate, re.accept(echo, 1100));
}

// --- the responder's echo chunk -------------------------------------------

void test_a_single_frame_ping_is_echoed_unfragmented() {
  TEST_ASSERT_EQUAL_size_t(0, w9_echo_chunk(204, 1));
}

// spec 11.1 - every fragment but the last carries the same length, so the largest
// received fragment IS the sender's chunk.
void test_the_echo_chunk_mirrors_the_received_set() {
  TEST_ASSERT_EQUAL_size_t(14, w9_echo_chunk(14, 15));
  TEST_ASSERT_EQUAL_size_t(64, w9_echo_chunk(64, 4));
}

// --- stats ----------------------------------------------------------------

void test_a_run_passes_only_when_every_ping_came_back_clean() {
  W9Stats s;
  w9_stats_reset(&s);
  TEST_ASSERT_FALSE(w9_run_passed(s));  // nothing sent is not a pass

  s.pings_sent = 32;
  s.echoes_ok  = 32;
  TEST_ASSERT_TRUE(w9_run_passed(s));

  s.pattern_faults = 1;
  TEST_ASSERT_FALSE(w9_run_passed(s));
}

void test_one_lost_echo_fails_the_run() {
  W9Stats s;
  s.pings_sent   = 32;
  s.echoes_ok    = 31;
  s.echo_timeouts = 1;
  TEST_ASSERT_FALSE(w9_run_passed(s));
}

// --- the airtime check R9 asks for ----------------------------------------

// spec 15.1 - a 222-byte frame at SF9 is over a second of channel occupancy, and
// spec 12.3's backoff defaults were chosen against an empty channel.
void test_a_backoff_window_shorter_than_one_frame_does_not_cover_it() {
  const W9AirtimeCheck c = w9_airtime_check(1100, 500);
  TEST_ASSERT_FALSE(c.backoff_covers);
}

void test_a_backoff_window_at_least_one_frame_long_covers_it() {
  const W9AirtimeCheck c = w9_airtime_check(200, 200);
  TEST_ASSERT_TRUE(c.backoff_covers);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_max_frame_run_is_exactly_222_bytes);
  RUN_TEST(test_fragmented_run_is_the_full_fifteen_fragment_set);
  RUN_TEST(test_build_ping_sets_pattern_fill_and_the_pattern);
  RUN_TEST(test_build_ping_refuses_an_echo_over_the_cap);
  RUN_TEST(test_build_ping_refuses_a_buffer_it_would_overrun);
  RUN_TEST(test_a_clean_echo_passes);
  RUN_TEST(test_a_corrupt_byte_is_reported_at_its_offset);
  RUN_TEST(test_a_short_echo_is_a_length_fault_not_a_pattern_fault);
  RUN_TEST(test_a_dropped_pattern_fill_flag_is_caught);
  RUN_TEST(test_an_echo_of_a_different_seq_fails_the_pattern);
  RUN_TEST(test_fragmented_ping_survives_a_full_round_trip);
  RUN_TEST(test_fragments_arriving_backwards_still_reassemble);
  RUN_TEST(test_a_repeated_fragment_after_completion_reports_frag_late);
  RUN_TEST(test_a_single_frame_ping_is_echoed_unfragmented);
  RUN_TEST(test_the_echo_chunk_mirrors_the_received_set);
  RUN_TEST(test_a_run_passes_only_when_every_ping_came_back_clean);
  RUN_TEST(test_one_lost_echo_fails_the_run);
  RUN_TEST(test_a_backoff_window_shorter_than_one_frame_does_not_cover_it);
  RUN_TEST(test_a_backoff_window_at_least_one_frame_long_covers_it);
  return UNITY_END();
}
