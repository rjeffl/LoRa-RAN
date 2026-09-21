// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-32 - the node half of the configuration path. Spec 7.4, 7.4.1; D57.
//
// WHAT THIS COVERS. The three rules spec 7.4.1 makes easy to get wrong and expensive to
// get wrong quietly: the bridge accepts MORE THAN ONE CONFIG_ACK bearing a given `seq`,
// it closes on the message whose MORE_FOLLOWS is CLEAR rather than on the first, and it
// publishes nothing from an answer that never completed. Also spec 7.4's rule that a
// missing CONFIG_ACK is `unknown` and is resolved by readback rather than retransmission.
//
// WHAT IT CANNOT. That a node actually marks MORE_FOLLOWS, or that the readback a POLL
// bit 1 provokes arrives at all. Those are the simnode's and the bench's.

#include <unity.h>

#include <cstdio>
#include <cstring>

#include "config_path.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

ConfigJob job_for(lran::NodeId dst, lran::ConfigOp op = ConfigOp::Set) {
  ConfigJob j;
  j.dst        = dst;
  j.op         = op;
  j.config.op  = op;
  j.config.count = 1;
  schema::entry_pack(&j.config.entries[0], 0x0100, PType::U8, 16);
  j.name_count = 1;
  std::snprintf(j.names[0], sizeof(j.names[0]), "dedup_cache_depth");
  return j;
}

schema::NodeConfigAckV1 ack_with(uint8_t count, bool more, uint16_t first_id = 0x0100) {
  schema::NodeConfigAckV1 a;
  a.op             = ConfigOp::GetAll;
  a.more_follows   = more;
  a.persist_status = PersistStatus::Persisted;
  a.count          = count;
  for (uint8_t i = 0; i < count; ++i) {
    schema::entry_pack(&a.entries[i], static_cast<uint16_t>(first_id + i), ParamStatus::Ok,
                       PType::U8, 8);
  }
  return a;
}

// Drives the path to the point where the CONFIG is on the air.
void send_config(ConfigPath& path, uint32_t now) {
  const ConfigStep step = path.next(now);
  TEST_ASSERT_TRUE(step.action == ConfigAction::SendConfig);
  path.on_sent(now);
}

}  // namespace

// ---------------------------------------------------------------------------
// The ordinary round trip
// ---------------------------------------------------------------------------

void test_a_config_goes_out_and_its_ack_resolves_it() {
  ConfigPath path;
  TEST_ASSERT_TRUE(path.submit(job_for(0xF1), 0xAABBCCDD, 7, 1000));
  TEST_ASSERT_TRUE(path.busy());

  const ConfigStep out = path.next(1000);
  TEST_ASSERT_TRUE(out.action == ConfigAction::SendConfig);
  TEST_ASSERT_EQUAL_UINT8(0xF1, out.dst);
  TEST_ASSERT_EQUAL_UINT16(7, out.seq);
  TEST_ASSERT_EQUAL_UINT32(0xAABBCCDD, out.ctx_id);
  path.on_sent(1000);

  path.on_config_ack(0xF1, ack_with(2, /*more=*/false), /*ack_seq=*/7, 1500);

  const ConfigStep done = path.next(1500);
  TEST_ASSERT_TRUE(done.action == ConfigAction::Resolve);
  TEST_ASSERT_TRUE(done.op_outcome == ConfigOutcome::Acked);
  TEST_ASSERT_EQUAL_UINT32(2, done.result_count);
  TEST_ASSERT_TRUE(done.updates_state);
  TEST_ASSERT_FALSE(path.busy());
  TEST_ASSERT_EQUAL_UINT32(1, path.stats().acked);
}

void test_one_transaction_in_flight() {
  ConfigPath path;
  TEST_ASSERT_TRUE(path.submit(job_for(0xF1), 0, 1, 0));
  TEST_ASSERT_FALSE(path.submit(job_for(0xF2), 0, 2, 0));
  TEST_ASSERT_EQUAL_UINT32(1, path.stats().refused_busy);
}

void test_an_ack_from_another_node_is_ignored() {
  ConfigPath path;
  (void)path.submit(job_for(0xF1), 0, 7, 0);
  send_config(path, 0);
  path.on_config_ack(0xF2, ack_with(1, false), 7, 100);
  TEST_ASSERT_TRUE(path.next(100).action == ConfigAction::None);
  TEST_ASSERT_EQUAL_UINT32(1, path.stats().ack_ignored);
}

// spec 9.2 - a solicited answer correlates by `seq`.
void test_an_ack_bearing_another_seq_is_ignored() {
  ConfigPath path;
  (void)path.submit(job_for(0xF1), 0, 7, 0);
  send_config(path, 0);
  path.on_config_ack(0xF1, ack_with(1, false), /*ack_seq=*/8, 100);
  TEST_ASSERT_TRUE(path.next(100).action == ConfigAction::None);
  TEST_ASSERT_EQUAL_UINT32(1, path.stats().ack_ignored);
}

// ---------------------------------------------------------------------------
// Spec 7.4.1 - the split answer. The three rules this file exists for.
// ---------------------------------------------------------------------------

// THE BRIDGE MUST ACCEPT MORE THAN ONE CONFIG_ACK BEARING A GIVEN `seq`. A bridge that
// closed on the first strands the rest of the answer and reports a configuration it did
// not finish reading.
void test_an_answer_in_three_messages_completes_on_the_last() {
  ConfigPath path;
  (void)path.submit(job_for(0xF1, ConfigOp::GetAll), 0, 7, 0);
  send_config(path, 0);

  path.on_config_ack(0xF1, ack_with(4, /*more=*/true, 0x0100), 7, 100);
  TEST_ASSERT_TRUE(path.next(150).action == ConfigAction::None);  // not yet

  path.on_config_ack(0xF1, ack_with(4, /*more=*/true, 0x0110), 7, 200);
  TEST_ASSERT_TRUE(path.next(250).action == ConfigAction::None);  // still not

  path.on_config_ack(0xF1, ack_with(2, /*more=*/false, 0x0120), 7, 300);

  const ConfigStep done = path.next(300);
  TEST_ASSERT_TRUE(done.action == ConfigAction::Resolve);
  TEST_ASSERT_TRUE(done.op_outcome == ConfigOutcome::Acked);
  // Every message's results, in the order they arrived.
  TEST_ASSERT_EQUAL_UINT32(10, done.result_count);
  TEST_ASSERT_EQUAL_UINT16(0x0100, done.results[0].param_id);
  TEST_ASSERT_EQUAL_UINT16(0x0120, done.results[8].param_id);
}

// A bridge that closes on the first message is the defect this asserts against.
void test_a_marked_message_does_not_resolve_anything() {
  ConfigPath path;
  (void)path.submit(job_for(0xF1, ConfigOp::GetAll), 0, 7, 0);
  send_config(path, 0);
  path.on_config_ack(0xF1, ack_with(4, /*more=*/true), 7, 100);
  TEST_ASSERT_TRUE(path.busy());
  TEST_ASSERT_TRUE(path.next(100).action == ConfigAction::None);
  TEST_ASSERT_EQUAL_UINT32(0, path.stats().acked);
}

// Spec 7.4.1 bounds one answer at kMaxConfigAckMessages, so a bridge staging one has a
// termination condition that does not depend on the node being correct.
void test_a_node_that_never_stops_marking_is_bounded() {
  ConfigPath path;
  (void)path.submit(job_for(0xF1, ConfigOp::GetAll), 0, 7, 0);
  send_config(path, 0);
  for (int i = 0; i < 8; ++i) {
    path.on_config_ack(0xF1, ack_with(1, /*more=*/true), 7, 100 + i);
  }
  TEST_ASSERT_TRUE(path.stats().staging_overflow > 0);
  TEST_ASSERT_TRUE(path.busy());  // it ends on the timeout, not on the node's say-so
}

// ---------------------------------------------------------------------------
// Spec 7.4 - a lost CONFIG_ACK
// ---------------------------------------------------------------------------

// NEVER FAILURE, AND NEVER A RETRANSMISSION. A configuration write is not idempotently
// repeatable, and repeating one cannot tell you whether the first took effect.
void test_no_ack_inside_the_timeout_is_unknown_and_then_a_readback() {
  ConfigPath path;
  path.set_ack_timeout_ms(1000);
  (void)path.submit(job_for(0xF1), 0, 7, 0);
  send_config(path, 0);

  TEST_ASSERT_TRUE(path.next(500).action == ConfigAction::None);

  const ConfigStep unknown = path.next(1000);
  TEST_ASSERT_TRUE(unknown.action == ConfigAction::Resolve);
  TEST_ASSERT_TRUE(unknown.op_outcome == ConfigOutcome::Unknown);
  TEST_ASSERT_TRUE(unknown.persist == AckPersist::Unknown);
  TEST_ASSERT_EQUAL_UINT32(0, unknown.result_count);
  TEST_ASSERT_FALSE(unknown.updates_state);

  // Then, and only then, the bridge asks the node what it is running.
  const ConfigStep readback = path.next(1000);
  TEST_ASSERT_TRUE(readback.action == ConfigAction::RequestReadback);
  TEST_ASSERT_EQUAL_UINT8(0xF1, readback.dst);
  TEST_ASSERT_EQUAL_UINT32(1, path.stats().unknown);
  TEST_ASSERT_EQUAL_UINT32(1, path.stats().sent);  // the CONFIG, and no second one
}

// The readback correlates to no request (D45), so its `seq` comes from the node's status
// space and must NOT be matched against the CONFIG's.
void test_an_unsolicited_readback_is_not_matched_by_seq() {
  ConfigPath path;
  path.set_ack_timeout_ms(1000);
  (void)path.submit(job_for(0xF1), 0, 7, 0);
  send_config(path, 0);
  (void)path.next(1000);  // Resolve: unknown
  const ConfigStep readback = path.next(1000);
  TEST_ASSERT_TRUE(readback.action == ConfigAction::RequestReadback);
  path.on_sent(1000);

  // A status-space seq, nothing like the request's 7.
  path.on_config_ack(0xF1, ack_with(3, /*more=*/false), /*ack_seq=*/412, 1500);

  const ConfigStep done = path.next(1500);
  TEST_ASSERT_TRUE(done.action == ConfigAction::Resolve);
  TEST_ASSERT_TRUE(done.op_outcome == ConfigOutcome::ReadbackOk);
  TEST_ASSERT_EQUAL_UINT32(3, done.result_count);
  TEST_ASSERT_TRUE(done.updates_state);
  // Spec 16.7.3 - the op published is the ANSWER's, and a readback answers GET_ALL.
  TEST_ASSERT_TRUE(done.op == ConfigOp::GetAll);
  TEST_ASSERT_EQUAL_UINT32(1, path.stats().readbacks_completed);
}

// ---------------------------------------------------------------------------
// Spec 7.4.1's abandon rule, and the retained topic it protects
// ---------------------------------------------------------------------------

// THE TIMEOUT RUNS FROM THE FIRST MESSAGE OF THE ANSWER, not from the request. A node
// several seconds into a long answer must not be cut off by a clock that started earlier.
void test_the_readback_timeout_runs_from_the_first_message() {
  ConfigPath path;
  path.set_ack_timeout_ms(1000);
  path.set_readback_timeout_ms(5000);
  (void)path.submit(job_for(0xF1), 0, 7, 0);
  send_config(path, 0);
  (void)path.next(1000);
  (void)path.next(1000);
  path.on_sent(1000);

  // The first message arrives late, and the window opens there.
  path.on_config_ack(0xF1, ack_with(1, /*more=*/true), 400, 4000);
  TEST_ASSERT_TRUE(path.next(8000).action == ConfigAction::None);  // 4 s into the answer

  path.on_config_ack(0xF1, ack_with(1, /*more=*/false), 401, 8500);
  TEST_ASSERT_TRUE(path.next(8500).op_outcome == ConfigOutcome::ReadbackOk);
}

// AN INCOMPLETE ANSWER PUBLISHES NOTHING. A retained config/state holding half of this
// answer and half of the last one cannot be read back apart afterwards.
void test_an_abandoned_answer_carries_no_results() {
  ConfigPath path;
  path.set_ack_timeout_ms(1000);
  path.set_readback_timeout_ms(2000);
  (void)path.submit(job_for(0xF1), 0, 7, 0);
  send_config(path, 0);
  (void)path.next(1000);
  (void)path.next(1000);
  path.on_sent(1000);

  path.on_config_ack(0xF1, ack_with(4, /*more=*/true), 400, 1500);
  const ConfigStep gone = path.next(4000);
  TEST_ASSERT_TRUE(gone.action == ConfigAction::Resolve);
  TEST_ASSERT_TRUE(gone.op_outcome == ConfigOutcome::Abandoned);
  TEST_ASSERT_EQUAL_UINT32(0, gone.result_count);
  TEST_ASSERT_FALSE(gone.updates_state);
  TEST_ASSERT_EQUAL_UINT32(1, path.stats().config_readback_abandoned);
  // ONE Resolve per transaction. A second would publish a config/ack carrying no results
  // and no explanation, for a set the operator already had an answer to.
  TEST_ASSERT_FALSE(path.busy());
  TEST_ASSERT_TRUE(path.next(5000).action == ConfigAction::None);
}

// A node that answers the poll with nothing at all still ends, on the window the poll
// opened. Otherwise one silent node holds the fleet's only transaction slot forever.
void test_a_readback_that_never_starts_still_ends() {
  ConfigPath path;
  path.set_ack_timeout_ms(1000);
  path.set_readback_timeout_ms(2000);
  (void)path.submit(job_for(0xF1), 0, 7, 0);
  send_config(path, 0);
  (void)path.next(1000);
  (void)path.next(1000);
  path.on_sent(1000);

  TEST_ASSERT_TRUE(path.next(2500).action == ConfigAction::None);
  const ConfigStep gone = path.next(3000);
  TEST_ASSERT_TRUE(gone.op_outcome == ConfigOutcome::Abandoned);
  TEST_ASSERT_FALSE(path.busy());
}

void test_persist_status_comes_from_the_answer() {
  ConfigPath path;
  (void)path.submit(job_for(0xF1), 0, 7, 0);
  send_config(path, 0);

  schema::NodeConfigAckV1 a = ack_with(1, false);
  a.persist_status          = PersistStatus::AppliedNotPersisted;
  path.on_config_ack(0xF1, a, 7, 100);

  TEST_ASSERT_TRUE(path.next(100).persist == AckPersist::AppliedNotPersisted);
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_a_config_goes_out_and_its_ack_resolves_it);
  RUN_TEST(test_one_transaction_in_flight);
  RUN_TEST(test_an_ack_from_another_node_is_ignored);
  RUN_TEST(test_an_ack_bearing_another_seq_is_ignored);

  RUN_TEST(test_an_answer_in_three_messages_completes_on_the_last);
  RUN_TEST(test_a_marked_message_does_not_resolve_anything);
  RUN_TEST(test_a_node_that_never_stops_marking_is_bounded);

  RUN_TEST(test_no_ack_inside_the_timeout_is_unknown_and_then_a_readback);
  RUN_TEST(test_an_unsolicited_readback_is_not_matched_by_seq);

  RUN_TEST(test_the_readback_timeout_runs_from_the_first_message);
  RUN_TEST(test_an_abandoned_answer_carries_no_results);
  RUN_TEST(test_a_readback_that_never_starts_still_ends);
  RUN_TEST(test_persist_status_comes_from_the_answer);

  return UNITY_END();
}
