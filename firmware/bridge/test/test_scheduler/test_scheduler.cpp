// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-17 - the poll scheduler (Impl Plan 6.1; PRD R-3.1d).
//
// THE PROPERTY UNDER TEST is R-3.1d: never more than one outstanding poll across the fleet,
// however many nodes are due. The rest - who is polled, when the next poll falls, when a
// silence counts - is what makes that property hold without starving anyone.
//
// WHAT THIS CANNOT COVER. sched_task's lock and queue send (task_runtime.cpp needs FreeRTOS),
// and a node answering on air. B3's bench run is where a simnode answers these polls.

#include <unity.h>

#include "lran/lran.h"
#include "refimpl_mac.h"
#include "registry.h"
#include "scheduler.h"
#include "test_key.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint16_t kInterval = kPollIntervalDefaultS;

void expect(const PollStep& st, PollAction action, NodeId node = 0) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(action), static_cast<int>(st.action));
  if (action != PollAction::None) TEST_ASSERT_EQUAL_HEX8(node, st.node);
}

// Sends the poll next() asks for, as sched_task would.
NodeId poll_now(PollScheduler& s, uint32_t now, uint16_t interval = kInterval) {
  const PollStep st = s.next(now, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PollAction::Poll), static_cast<int>(st.action));
  s.on_sent(st.node, interval, now);
  return st.node;
}

}  // namespace

// Production rows are polled from the first tick, in table order; bench rows are not.
void test_production_rows_are_polled_from_boot_and_bench_rows_are_not() {
  PollScheduler s;
  TEST_ASSERT_TRUE(s.enrolled(kNodeGateLink));
  TEST_ASSERT_TRUE(s.enrolled(kNodeWellLink));
  TEST_ASSERT_FALSE(s.enrolled(kNodeSim0));

  TEST_ASSERT_EQUAL_HEX8(kNodeGateLink, poll_now(s, 0));
  s.on_heard(kNodeGateLink, 100);
  TEST_ASSERT_EQUAL_HEX8(kNodeWellLink, poll_now(s, 200));
  s.on_heard(kNodeWellLink, 300);
  expect(s.next(400, true), PollAction::None);  // no simnode has spoken
}

// R-3.1d. Both production nodes are due at boot; the second waits for the first to resolve.
void test_one_outstanding_poll_across_the_fleet() {
  PollScheduler s;
  poll_now(s, 0);
  TEST_ASSERT_TRUE(s.outstanding());
  for (uint32_t t = 1000; t < kPollReplyTimeoutDefaultMs; t += 1000) {
    expect(s.next(t, true), PollAction::None);
  }
  s.on_heard(kNodeWellLink, 5000);  // a frame from another node answers nothing
  TEST_ASSERT_TRUE(s.outstanding());
  s.on_heard(kNodeGateLink, 5500);
  TEST_ASSERT_FALSE(s.outstanding());
  expect(s.next(6000, true), PollAction::Poll, kNodeWellLink);
  TEST_ASSERT_EQUAL_UINT32(1, s.stats().answered);
}

// An unanswered poll is reported once, when its window closes, and frees the fleet.
void test_a_silent_node_is_missed_when_the_window_closes() {
  PollScheduler s;
  poll_now(s, 1000);
  expect(s.next(1000 + kPollReplyTimeoutDefaultMs - 1, true), PollAction::None);
  expect(s.next(1000 + kPollReplyTimeoutDefaultMs, true), PollAction::Missed, kNodeGateLink);
  TEST_ASSERT_FALSE(s.outstanding());
  expect(s.next(1000 + kPollReplyTimeoutDefaultMs, true), PollAction::Poll, kNodeWellLink);
  TEST_ASSERT_EQUAL_UINT32(1, s.stats().missed);
}

// The next poll falls one interval after the send, not after the due time.
void test_the_next_poll_is_one_interval_after_the_send() {
  PollScheduler s;
  s.set_reply_timeout_ms(500);
  poll_now(s, 0);
  s.on_heard(kNodeGateLink, 100);
  poll_now(s, 7000);  // WellLink, sent late, behind GateLink
  s.on_heard(kNodeWellLink, 7100);

  expect(s.next(59999, true), PollAction::None);
  expect(s.next(60000, true), PollAction::Poll, kNodeGateLink);
  s.on_sent(kNodeGateLink, kInterval, 60000);
  s.on_heard(kNodeGateLink, 60100);
  expect(s.next(66999, true), PollAction::None);
  expect(s.next(67000, true), PollAction::Poll, kNodeWellLink);
}

// Impl Plan 6.1 - a push resets missed_polls but does not move the schedule.
void test_a_push_does_not_move_the_schedule() {
  PollScheduler s;
  s.set_reply_timeout_ms(500);
  poll_now(s, 0);
  s.on_heard(kNodeGateLink, 100);
  poll_now(s, 200);
  s.on_heard(kNodeWellLink, 300);

  s.on_heard(kNodeGateLink, 30000);  // an unsolicited push, mid-interval
  expect(s.next(59999, true), PollAction::None);
  expect(s.next(60000, true), PollAction::Poll, kNodeGateLink);
}

// BF-23 - a changed interval counts from the last poll, so a shorter one takes effect
// without waiting out the longer one, and a longer one pushes the next poll out.
void test_a_changed_interval_counts_from_the_last_poll() {
  PollScheduler s;
  s.set_reply_timeout_ms(500);
  poll_now(s, 0, 3600);
  s.on_heard(kNodeGateLink, 100);
  poll_now(s, 200, 60);
  s.on_heard(kNodeWellLink, 300);

  s.retime(kNodeGateLink, 30);
  expect(s.next(29999, true), PollAction::None);
  expect(s.next(30000, true), PollAction::Poll, kNodeGateLink);
  s.on_sent(kNodeGateLink, 30, 30000);
  s.on_heard(kNodeGateLink, 30100);
  s.retime(kNodeGateLink, 3600);  // out of the way of the next check

  s.retime(kNodeWellLink, 120);
  expect(s.next(120199, true), PollAction::None);
  expect(s.next(120200, true), PollAction::Poll, kNodeWellLink);
}

// A row never polled is due at once, and a retime leaves it so.
void test_a_retime_before_the_first_poll_leaves_the_row_due() {
  PollScheduler s;
  s.retime(kNodeGateLink, 3600);
  expect(s.next(0, true), PollAction::Poll, kNodeGateLink);
}

// Decided with the operator 2026-09-14: a bench row joins once heard, and is due at once.
void test_a_bench_node_joins_the_schedule_once_heard() {
  PollScheduler s;
  s.set_reply_timeout_ms(500);
  poll_now(s, 0);
  s.on_heard(kNodeSim1, 50);  // a simnode push while GateLink's poll is outstanding
  TEST_ASSERT_TRUE(s.enrolled(kNodeSim1));
  s.on_heard(kNodeGateLink, 100);

  // Neither WellLink nor f1 has been polled, so both are maximally due; table order decides.
  expect(s.next(200, true), PollAction::Poll, kNodeWellLink);
  s.on_sent(kNodeWellLink, kInterval, 200);
  s.on_heard(kNodeWellLink, 300);
  expect(s.next(400, true), PollAction::Poll, kNodeSim1);
}

// B3a records poll-to-answer times (Impl Plan 6.1.1). Only the frame that answers the
// outstanding poll returns a time, measured from on_sent() and correct across the millis() wrap.
void test_on_heard_returns_the_poll_to_answer_time_only_for_an_answer() {
  PollScheduler s;
  const uint32_t t0 = UINT32_MAX - 200;
  poll_now(s, t0);
  TEST_ASSERT_EQUAL_UINT32(PollScheduler::kNotAnAnswer, s.on_heard(kNodeWellLink, t0 + 100));
  TEST_ASSERT_EQUAL_UINT32(700, s.on_heard(kNodeGateLink, t0 + 700));
  TEST_ASSERT_EQUAL_UINT32(PollScheduler::kNotAnAnswer, s.on_heard(kNodeGateLink, t0 + 900));
  TEST_ASSERT_EQUAL_UINT32(PollScheduler::kNotAnAnswer, s.on_heard(0x7E, t0 + 950));  // not a row
}

// An upload about to restart the bridge holds new polls, but a closing window is still counted.
void test_an_ota_upload_holds_new_polls_but_still_counts_a_miss() {
  PollScheduler s;
  poll_now(s, 0);
  expect(s.next(kPollReplyTimeoutDefaultMs, false), PollAction::Missed, kNodeGateLink);
  expect(s.next(kPollReplyTimeoutDefaultMs, false), PollAction::None);
  expect(s.next(kPollReplyTimeoutDefaultMs, true), PollAction::Poll, kNodeWellLink);
}

// millis() wraps at ~49.7 days; neither the window nor the interval may break across it.
void test_the_schedule_survives_the_millis_wrap() {
  PollScheduler  s;
  const uint32_t t0 = UINT32_MAX - 2000;
  poll_now(s, t0);
  expect(s.next(t0 + 5000, true), PollAction::None);  // wrapped, 5 s elapsed
  s.on_heard(kNodeGateLink, t0 + 5000);
  poll_now(s, t0 + 6000);
  s.on_heard(kNodeWellLink, t0 + 6100);
  expect(s.next(t0 + 59999, true), PollAction::None);
  expect(s.next(t0 + 60000, true), PollAction::Poll, kNodeGateLink);
}

// A bench node first heard after ~24.8 days of uptime must still come due.
void test_a_row_enrolled_late_in_uptime_is_polled() {
  PollScheduler  s;
  s.set_reply_timeout_ms(100);
  const uint32_t late = 0x90000000u;  // past half the millis() range
  poll_now(s, late);
  s.on_heard(kNodeGateLink, late + 10);
  poll_now(s, late + 20);
  s.on_heard(kNodeWellLink, late + 30);
  s.on_heard(kNodeSim2, late + 40);
  expect(s.next(late + 50, true), PollAction::Poll, kNodeSim2);
}

// Across GateLink's interval, a longer-late row wins over a less-late one.
void test_the_most_overdue_row_goes_first() {
  PollScheduler s;
  s.set_reply_timeout_ms(100);
  poll_now(s, 0);                      // GateLink, next due 60 000
  s.on_heard(kNodeGateLink, 10);
  poll_now(s, 20, 30);                 // WellLink, next due 30 020
  s.on_heard(kNodeWellLink, 30);
  poll_now(s, 60000);                  // at 60 000 both are due; WellLink is later by ~30 s
  TEST_ASSERT_EQUAL_HEX8(kNodeWellLink, s.outstanding_node());
}

void test_a_zero_interval_is_held_to_one_second() {
  PollScheduler s;
  s.set_reply_timeout_ms(100);
  poll_now(s, 0, 0);
  s.on_heard(kNodeGateLink, 10);
  poll_now(s, 20);
  s.on_heard(kNodeWellLink, 30);
  expect(s.next(999, true), PollAction::None);
  expect(s.next(1000, true), PollAction::Poll, kNodeGateLink);
}

void test_poll_seqs_advance() {
  PollScheduler s;
  TEST_ASSERT_EQUAL_UINT16(1, s.take_poll_seq());
  TEST_ASSERT_EQUAL_UINT16(2, s.take_poll_seq());
}

// The frame a node receives: POLL, full status, its own context, no MAC.
void test_the_poll_frame_is_what_a_node_decodes() {
  uint8_t      buf[kMaxFrame];
  const size_t len = build_poll_frame(kNodeSim1, 0x12345678u, 42, kProtoVer, buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, len);

  DecodeCtx ctx;
  ctx.self          = kNodeSim1;
  ctx.expect_ctx_id = 0x12345678u;
  Frame f;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok), static_cast<int>(decode_header(buf, len, ctx, &f)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok), static_cast<int>(decode_payload(buf, len, ctx, &f)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::Poll), static_cast<uint8_t>(f.hdr.type));
  TEST_ASSERT_EQUAL_HEX8(kNodeBridge, f.hdr.src);
  TEST_ASSERT_EQUAL_UINT16(42, f.hdr.seq);
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, f.hdr.ctx_id);
  TEST_ASSERT_EQUAL_size_t(1, f.payload_len);
  TEST_ASSERT_EQUAL_HEX8(kPollFlagFullStatus, f.payload[0]);
  TEST_ASSERT_NULL(f.mac);
}

// The registry half: a miss counts, saturates, and any valid frame clears it.
void test_missed_polls_count_and_any_frame_clears_them() {
  refimpl::RefKdf kdf;
  Registry        r;
  r.load(lran_test::kTestMasterKey, &kdf);
  TEST_ASSERT_TRUE(r.note_poll_missed(kNodeGateLink));
  TEST_ASSERT_TRUE(r.note_poll_missed(kNodeGateLink));
  TEST_ASSERT_EQUAL_UINT16(2, r.state(kNodeGateLink)->missed_polls);
  TEST_ASSERT_FALSE(r.note_poll_missed(0x42));

  Header h;
  h.type   = MsgType::Status;
  h.src    = kNodeGateLink;
  h.dst    = kNodeBridge;
  h.ctx_id = 7;
  r.observe(h, -80, 5, 1000);
  TEST_ASSERT_EQUAL_UINT16(0, r.state(kNodeGateLink)->missed_polls);

  for (uint32_t i = 0; i < 70000u; ++i) r.note_poll_missed(kNodeWellLink);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, r.state(kNodeWellLink)->missed_polls);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_production_rows_are_polled_from_boot_and_bench_rows_are_not);
  RUN_TEST(test_one_outstanding_poll_across_the_fleet);
  RUN_TEST(test_a_silent_node_is_missed_when_the_window_closes);
  RUN_TEST(test_the_next_poll_is_one_interval_after_the_send);
  RUN_TEST(test_a_push_does_not_move_the_schedule);
  RUN_TEST(test_a_bench_node_joins_the_schedule_once_heard);
  RUN_TEST(test_on_heard_returns_the_poll_to_answer_time_only_for_an_answer);
  RUN_TEST(test_an_ota_upload_holds_new_polls_but_still_counts_a_miss);
  RUN_TEST(test_the_schedule_survives_the_millis_wrap);
  RUN_TEST(test_a_row_enrolled_late_in_uptime_is_polled);
  RUN_TEST(test_the_most_overdue_row_goes_first);
  RUN_TEST(test_a_zero_interval_is_held_to_one_second);
  RUN_TEST(test_a_changed_interval_counts_from_the_last_poll);
  RUN_TEST(test_a_retime_before_the_first_poll_leaves_the_row_due);
  RUN_TEST(test_poll_seqs_advance);
  RUN_TEST(test_the_poll_frame_is_what_a_node_decodes);
  RUN_TEST(test_missed_polls_count_and_any_frame_clears_them);
  return UNITY_END();
}
