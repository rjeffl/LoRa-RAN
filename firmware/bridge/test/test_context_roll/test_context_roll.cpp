// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-34 - the context roll after a bridge restart (spec 10.6, D58, PRD R-3.1h).
//
// THE PROPERTY UNDER TEST is that no node leaves the pending state except through a
// completed roll. A pending node is one whose dedup cache may answer the bridge's next
// command with an earlier command's result, so every path that ends a roll without
// completing it must leave the node pending.
//
// WHAT THIS CANNOT COVER. sched_task's lock, its refusals and the registry writes
// (task_runtime.cpp needs FreeRTOS), and a node actually rolling: test_gatelink covers
// the simnode's half, and the bench run in the engineering log covers the two together.

#include <unity.h>

#include "context_roll.h"
#include "lran/lran.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

constexpr NodeId kNode    = kNodeSim1;
constexpr CtxId  kOldCtx  = 0x11111111u;
constexpr CtxId  kNewCtx  = 0x22222222u;
constexpr Seq    kRollSeq = 1;

msg::CommandAck ack_of(Seq seq, AckResult result) {
  return msg::CommandAck{seq, static_cast<uint8_t>(result), 0};
}

// Heard, submitted and sent once, at t = 1000.
ContextRoll started() {
  ContextRoll r;
  r.on_heard(kNode);
  NodeId due = 0;
  TEST_ASSERT_TRUE(r.next_due(&due));
  TEST_ASSERT_EQUAL_UINT8(kNode, due);
  TEST_ASSERT_TRUE(r.submit(kNode, kOldCtx, kRollSeq, 1000));
  const RollStep st = r.next(1000);
  TEST_ASSERT_EQUAL(RollAction::Send, st.action);
  r.on_sent(1000);
  return r;
}

RollStep resolved(ContextRoll& r, uint32_t now_ms) {
  const RollStep st = r.next(now_ms);
  TEST_ASSERT_EQUAL(RollAction::Resolve, st.action);
  TEST_ASSERT_FALSE(r.busy());
  return st;
}

}  // namespace

void test_every_node_starts_pending_and_none_is_due() {
  ContextRoll r;
  TEST_ASSERT_EQUAL_HEX32(kAllNodesMask, r.pending_mask());
  for (const NodeProvision& p : kNodeTable) TEST_ASSERT_TRUE(r.pending(p.id));
  NodeId due = 0;
  TEST_ASSERT_FALSE(r.next_due(&due));
  TEST_ASSERT_FALSE(r.pending(0x7E));  // not in the table
}

// spec 10.6 bridge step 2 - the roll goes out under the context the node's frame carried.
void test_a_heard_node_is_rolled_under_its_own_context() {
  ContextRoll r;
  r.on_heard(kNode);
  TEST_ASSERT_TRUE(r.submit(kNode, kOldCtx, 7, 1000));
  const RollStep st = r.next(1000);
  TEST_ASSERT_EQUAL(RollAction::Send, st.action);
  TEST_ASSERT_EQUAL_UINT8(kNode, st.dst);
  TEST_ASSERT_EQUAL_HEX32(kOldCtx, st.ctx_id);
  TEST_ASSERT_EQUAL_UINT16(7, st.seq);
  TEST_ASSERT_EQUAL_UINT8(0, st.attempt);
}

// A context of 0 is one not yet learned. A roll under it fails at the node's step 2.
void test_a_roll_is_not_started_without_a_learned_context() {
  ContextRoll r;
  r.on_heard(kNode);
  TEST_ASSERT_FALSE(r.submit(kNode, 0, 1, 1000));
  TEST_ASSERT_FALSE(r.busy());
}

// spec 10.6 bridge step 3.
void test_accepted_completes_the_roll_and_carries_the_new_context() {
  ContextRoll r = started();
  TEST_ASSERT_TRUE(r.on_ack(kNode, ack_of(kRollSeq, AckResult::Accepted), kNewCtx, 1500));
  const RollStep st = resolved(r, 1500);
  TEST_ASSERT_EQUAL(RollOutcome::Rolled, st.outcome);
  TEST_ASSERT_EQUAL_HEX32(kNewCtx, st.ctx_id);
  TEST_ASSERT_FALSE(r.pending(kNode));
  TEST_ASSERT_EQUAL_UINT32(1, r.stats().ctx_rolls);
  TEST_ASSERT_EQUAL_UINT32(0, r.stats().ctx_roll_failed);

  // Rolled once. A later frame does not start another.
  r.on_heard(kNode);
  NodeId due = 0;
  TEST_ASSERT_FALSE(r.next_due(&due));
}

// spec 10.6 bridge step 4 - the lost-ACK case. The roll is complete, not a resync.
void test_rejected_ctx_completes_the_roll() {
  ContextRoll r = started();
  TEST_ASSERT_TRUE(r.on_ack(kNode, ack_of(kRollSeq, AckResult::RejectedCtx), kNewCtx, 1500));
  const RollStep st = resolved(r, 1500);
  TEST_ASSERT_EQUAL(RollOutcome::Rolled, st.outcome);
  TEST_ASSERT_EQUAL_HEX32(kNewCtx, st.ctx_id);
  TEST_ASSERT_FALSE(r.pending(kNode));
  TEST_ASSERT_EQUAL_UINT32(1, r.stats().ctx_rolls);
  TEST_ASSERT_EQUAL_UINT32(1, r.stats().by_rejected_ctx);
}

// spec 10.6 bridge step 5 and root rule 2 - the retry after a BUSY carries the same seq.
void test_actuator_busy_is_retried_under_the_same_seq() {
  ContextRoll r = started();
  TEST_ASSERT_TRUE(r.on_ack(kNode, ack_of(kRollSeq, AckResult::ActuatorBusy), kOldCtx, 1500));
  TEST_ASSERT_TRUE(r.busy());
  TEST_ASSERT_EQUAL(RollAction::None, r.next(1500 + r.ack_timeout_ms() - 1).action);

  const RollStep st = r.next(1500 + r.ack_timeout_ms());
  TEST_ASSERT_EQUAL(RollAction::Send, st.action);
  TEST_ASSERT_EQUAL_UINT16(kRollSeq, st.seq);
  TEST_ASSERT_EQUAL_HEX32(kOldCtx, st.ctx_id);
  TEST_ASSERT_EQUAL_UINT8(1, st.attempt);
  TEST_ASSERT_EQUAL_UINT32(1, r.stats().busy);
  TEST_ASSERT_TRUE(r.pending(kNode));
}

// spec 10.6 bridge step 6 - out of retries. Counted, still pending, and not due again
// until the node is heard.
void test_exhaustion_fails_and_leaves_the_node_pending() {
  ContextRoll r = started();
  uint32_t    t = 1000;
  for (uint8_t a = 1; a <= r.retries(); ++a) {
    t += r.ack_timeout_ms() + kCommandRetryBackoffMs;
    const RollStep st = r.next(t);
    TEST_ASSERT_EQUAL(RollAction::Send, st.action);
    TEST_ASSERT_EQUAL_UINT16(kRollSeq, st.seq);
    r.on_sent(t);
  }
  t += r.ack_timeout_ms() + kCommandRetryBackoffMs;
  const RollStep st = resolved(r, t);
  TEST_ASSERT_EQUAL(RollOutcome::Failed, st.outcome);
  TEST_ASSERT_TRUE(st.no_ack);
  TEST_ASSERT_TRUE(r.pending(kNode));
  TEST_ASSERT_EQUAL_UINT32(1, r.stats().ctx_roll_failed);
  TEST_ASSERT_EQUAL_UINT32(1u + r.retries(), r.stats().sent);

  NodeId due = 0;
  TEST_ASSERT_FALSE(r.next_due(&due));
  r.on_heard(kNode);
  TEST_ASSERT_TRUE(r.next_due(&due));
}

// spec 10.6 bridge step 6 - REJECTED_UNKNOWN_CMD is a fault, and there is no fallback to
// commanding the node without a roll.
void test_any_other_answer_fails_and_leaves_the_node_pending() {
  const AckResult kOthers[] = {AckResult::RejectedUnknownCmd, AckResult::RejectedArg,
                               AckResult::RejectedMac, AckResult::DuplicateCached,
                               AckResult::RejectedSeq};
  for (AckResult a : kOthers) {
    ContextRoll r = started();
    TEST_ASSERT_TRUE(r.on_ack(kNode, ack_of(kRollSeq, a), kOldCtx, 1500));
    const RollStep st = resolved(r, 1500);
    TEST_ASSERT_EQUAL(RollOutcome::Failed, st.outcome);
    TEST_ASSERT_FALSE(st.no_ack);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(a), st.result);
    TEST_ASSERT_TRUE(r.pending(kNode));
    TEST_ASSERT_EQUAL_UINT32(1, r.stats().ctx_roll_failed);
  }
}

// The ACK that answers a roll is itself a heard frame, and app_task reports it as heard
// before it reports the ACK. That must not queue a second roll to the same node.
void test_hearing_the_node_in_flight_does_not_queue_another_roll() {
  ContextRoll r = started();
  r.on_heard(kNode);
  TEST_ASSERT_TRUE(r.on_ack(kNode, ack_of(kRollSeq, AckResult::RejectedUnknownCmd), kOldCtx,
                            1500));
  resolved(r, 1500);
  NodeId due = 0;
  TEST_ASSERT_FALSE(r.next_due(&due));
}

// An ACK for another node, another seq, or with nothing in flight belongs to the command
// path, and must reach it uncounted here.
void test_an_ack_that_is_not_the_rolls_is_left_for_the_command_path() {
  ContextRoll idle;
  TEST_ASSERT_FALSE(idle.on_ack(kNode, ack_of(kRollSeq, AckResult::Accepted), kNewCtx, 1500));

  ContextRoll r = started();
  TEST_ASSERT_FALSE(r.on_ack(kNodeSim2, ack_of(kRollSeq, AckResult::Accepted), kNewCtx, 1500));
  TEST_ASSERT_FALSE(r.on_ack(kNode, ack_of(kRollSeq + 1, AckResult::Accepted), kNewCtx, 1500));
  TEST_ASSERT_TRUE(r.busy());
  TEST_ASSERT_TRUE(r.pending(kNode));
}

// One roll at a time, and the rest wait their turn in table order.
void test_one_roll_in_flight_and_the_next_is_due_after_it() {
  ContextRoll r = started();
  r.on_heard(kNodeSim2);
  TEST_ASSERT_FALSE(r.submit(kNodeSim2, kOldCtx, 1, 1100));
  TEST_ASSERT_TRUE(r.on_ack(kNode, ack_of(kRollSeq, AckResult::Accepted), kNewCtx, 1500));
  resolved(r, 1500);
  NodeId due = 0;
  TEST_ASSERT_TRUE(r.next_due(&due));
  TEST_ASSERT_EQUAL_UINT8(kNodeSim2, due);
  TEST_ASSERT_TRUE(r.submit(kNodeSim2, kOldCtx, 1, 1600));
  TEST_ASSERT_FALSE(r.pending(kNode));
  TEST_ASSERT_TRUE(r.pending(kNodeSim2));
}

// Root rule 8 - the roll waits on the command path's two levers, set at runtime.
void test_the_timeout_and_retry_count_are_runtime_settable() {
  ContextRoll r;
  TEST_ASSERT_EQUAL_UINT32(kCommandAckTimeoutDefaultMs, r.ack_timeout_ms());
  TEST_ASSERT_EQUAL_UINT8(kCommandRetriesDefault, r.retries());
  r.set_ack_timeout_ms(8000);
  r.set_retries(0);
  r.on_heard(kNode);
  TEST_ASSERT_TRUE(r.submit(kNode, kOldCtx, 1, 1000));
  r.next(1000);
  r.on_sent(1000);
  TEST_ASSERT_EQUAL(RollAction::None, r.next(8999).action);
  const RollStep st = resolved(r, 9000);
  TEST_ASSERT_EQUAL(RollOutcome::Failed, st.outcome);
}

// millis() wraps at ~49.7 days, and the bridge runs for years.
void test_the_window_survives_the_millis_wrap() {
  ContextRoll r;
  r.on_heard(kNode);
  const uint32_t t0 = 0xFFFFFF00u;
  TEST_ASSERT_TRUE(r.submit(kNode, kOldCtx, 1, t0));
  r.next(t0);
  r.on_sent(t0);
  TEST_ASSERT_EQUAL(RollAction::None, r.next(t0 + 1000).action);
  TEST_ASSERT_EQUAL(RollAction::Send, r.next(t0 + r.ack_timeout_ms()).action);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_node_starts_pending_and_none_is_due);
  RUN_TEST(test_a_heard_node_is_rolled_under_its_own_context);
  RUN_TEST(test_a_roll_is_not_started_without_a_learned_context);
  RUN_TEST(test_accepted_completes_the_roll_and_carries_the_new_context);
  RUN_TEST(test_rejected_ctx_completes_the_roll);
  RUN_TEST(test_actuator_busy_is_retried_under_the_same_seq);
  RUN_TEST(test_exhaustion_fails_and_leaves_the_node_pending);
  RUN_TEST(test_any_other_answer_fails_and_leaves_the_node_pending);
  RUN_TEST(test_hearing_the_node_in_flight_does_not_queue_another_roll);
  RUN_TEST(test_an_ack_that_is_not_the_rolls_is_left_for_the_command_path);
  RUN_TEST(test_one_roll_in_flight_and_the_next_is_due_after_it);
  RUN_TEST(test_the_timeout_and_retry_count_are_runtime_settable);
  RUN_TEST(test_the_window_survives_the_millis_wrap);
  return UNITY_END();
}
