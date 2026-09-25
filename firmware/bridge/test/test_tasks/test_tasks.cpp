// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-11 - the task table's invariants, and the queue drop accounting.
//
// WHAT THIS COVERS AND WHAT IT CANNOT. Impl Plan 5.2 states its rules in prose:
// lora_task is highest and never blocks, publication is queued rather than inline.
// The half that is DATA - priorities, cores, depths, and what a full queue does -
// is asserted here. The half that is FreeRTOS behaviour is not: no host test can
// show that a real xQueueSend from a real ISR context did not block.
//
// So the never-block rule is defended in three places, deliberately, because one
// would not hold: this file (the table and the accounting), the absence of any
// blocking send in task_runtime.h (there is no API to misuse), and
// tools/checks/lora_task_never_blocks.py (a blocking primitive appearing on the
// LoRa path fails the checks).

#include <unity.h>

#include "lran/wire.h"
#include "queues.h"
#include "tasks.h"

using namespace bridge;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

void test_lora_outranks_every_other_task() {
  TEST_ASSERT_TRUE(lora_is_strictly_highest());
}

void test_log_is_beneath_every_other_task() {
  TEST_ASSERT_TRUE(log_is_strictly_lowest());
}

// A task below the Arduino loop task (priority 1) is starved by a `loop()` that
// never yields, which is the default shape of an Arduino sketch.
void test_nothing_sits_below_the_arduino_loop() {
  TEST_ASSERT_TRUE(all_priorities_above_arduino_loop());
}

// The names are what a panic backtrace prints. Two tasks sharing one is a crash
// report that names the wrong task.
void test_task_names_are_unique() {
  TEST_ASSERT_TRUE(task_names_are_unique());
}

// Every row is filled in. A zero stack is a task that cannot start; an empty name
// is a backtrace with a hole in it.
void test_every_row_is_populated() {
  for (size_t i = 0; i < kTaskCount; ++i) {
    const TaskSpec& s = task_table()[i];
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(i), static_cast<uint8_t>(s.id));
    TEST_ASSERT_NOT_NULL(s.name);
    TEST_ASSERT_TRUE(s.name[0] != '\0');
    TEST_ASSERT_TRUE(s.stack_bytes > 0);
  }
}

// lora_task is pinned away from core 0, where the WiFi and lwIP stacks run. This is
// the structural half of the same asymmetry PRD 4.4 records at the radio level, and
// M22 is the measurement that would say it is not enough.
void test_lora_is_pinned_off_the_wifi_core() {
  TEST_ASSERT_EQUAL_INT(kCore1, task_spec(TaskId::Lora).core);
  TEST_ASSERT_EQUAL_INT(kCore0, task_spec(TaskId::Mqtt).core);
}

// The three periodic tasks have periods and the queue-driven ones do not. A task
// with both is usually a task doing two jobs.
void test_periods_match_the_trigger_column() {
  TEST_ASSERT_EQUAL_UINT32(1000, task_spec(TaskId::Sched).period_ms);
  TEST_ASSERT_EQUAL_UINT32(100, task_spec(TaskId::Mqtt).period_ms);
  TEST_ASSERT_EQUAL_UINT32(500, task_spec(TaskId::Ui).period_ms);
  TEST_ASSERT_EQUAL_UINT32(0, task_spec(TaskId::App).period_ms);
  TEST_ASSERT_EQUAL_UINT32(0, task_spec(TaskId::Lora).period_ms);
}

// ---------------------------------------------------------------------------
// The queues
// ---------------------------------------------------------------------------

// Publication is queued and fans out - one status frame becomes many topics - so
// the publish queue is the deep one. The TX queue is shallow because the bridge
// serializes polls fleet-wide (6.1, R-3.1d): depth there would mean something
// upstream stopped honouring that.
void test_queue_depths_reflect_their_jobs() {
  TEST_ASSERT_TRUE(kPublishQueueDepth > kRxQueueDepth);
  TEST_ASSERT_TRUE(kRxQueueDepth > kTxQueueDepth);
}

// BF-38 - events have a queue of their own, so a full state queue cannot refuse one.
// Counted apart, so a dropped event is never hidden inside the state queue's count.
void test_events_have_their_own_queue_and_count() {
  TEST_ASSERT_TRUE(kEventQueueDepth > 0);
  QueueAccounting a;
  for (size_t i = 0; i < kPublishQueueDepth; ++i) a.record_sent(QueueId::Publish, i + 1);
  a.record_dropped(QueueId::Publish);
  a.record_sent(QueueId::Event, 1);
  TEST_ASSERT_EQUAL_UINT32(0, a.stat(QueueId::Event).dropped);
  TEST_ASSERT_EQUAL_UINT32(1, a.stat(QueueId::Event).sent);
  TEST_ASSERT_EQUAL_UINT32(1, a.stat(QueueId::Publish).dropped);
}

// Static storage arithmetic, asserted at a desk rather than discovered as a NULL
// queue handle at boot.
void test_queue_storage_is_depth_times_item() {
  TEST_ASSERT_EQUAL_size_t(kRxQueueDepth * sizeof(RxMessage),
                           queue_storage_bytes(kRxQueueDepth, sizeof(RxMessage)));
}

// The RX message is a COPY of a complete payload, not a view into the radio's buffer or
// the reassembler's - both are overwritten by the next reception, and a queue is exactly
// the boundary a view would not survive (lran/frame.h). BF-16 changed it from raw frame
// bytes; it must still hold the largest set spec 11.2 allows.
void test_rx_message_carries_a_complete_payload_by_value() {
  RxMessage m;
  TEST_ASSERT_EQUAL_size_t(lran::kMaxPayloadPlain, sizeof(m.payload));
  TEST_ASSERT_TRUE(sizeof(m.payload) >= lran::reassembly_cap(lran::MsgType::Ping));
  TEST_ASSERT_EQUAL_size_t(0, m.payload_len);
  TEST_ASSERT_FALSE(m.mac_verified);
}

void test_accounting_starts_clean() {
  QueueAccounting a;
  TEST_ASSERT_FALSE(a.any_dropped());
  TEST_ASSERT_EQUAL_UINT32(0, a.stat(QueueId::Rx).sent);
  TEST_ASSERT_EQUAL_UINT32(0, a.stat(QueueId::Rx).high_water);
}

// A drop is never silent (root rule 4's intent, on a path with no spec 14 stage of
// its own - see queues.h).
void test_a_drop_is_counted_and_raises_the_health_flag() {
  QueueAccounting a;
  a.record_dropped(QueueId::Publish);

  TEST_ASSERT_EQUAL_UINT32(1, a.stat(QueueId::Publish).dropped);
  TEST_ASSERT_TRUE(a.any_dropped());

  // And it stays local to the queue that dropped.
  TEST_ASSERT_EQUAL_UINT32(0, a.stat(QueueId::Rx).dropped);
}

// high_water is the number that says a depth was nearly reached BEFORE anything is
// lost. Monotonic, so a burst three weeks ago is still visible.
void test_high_water_holds_the_deepest_occupancy() {
  QueueAccounting a;
  a.record_sent(QueueId::Rx, 1);
  a.record_sent(QueueId::Rx, 5);
  a.record_sent(QueueId::Rx, 2);

  TEST_ASSERT_EQUAL_UINT32(3, a.stat(QueueId::Rx).sent);
  TEST_ASSERT_EQUAL_UINT32(5, a.stat(QueueId::Rx).high_water);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_lora_outranks_every_other_task);
  RUN_TEST(test_log_is_beneath_every_other_task);
  RUN_TEST(test_nothing_sits_below_the_arduino_loop);
  RUN_TEST(test_task_names_are_unique);
  RUN_TEST(test_every_row_is_populated);
  RUN_TEST(test_lora_is_pinned_off_the_wifi_core);
  RUN_TEST(test_periods_match_the_trigger_column);
  RUN_TEST(test_queue_depths_reflect_their_jobs);
  RUN_TEST(test_queue_storage_is_depth_times_item);
  RUN_TEST(test_rx_message_carries_a_complete_payload_by_value);
  RUN_TEST(test_accounting_starts_clean);
  RUN_TEST(test_a_drop_is_counted_and_raises_the_health_flag);
  RUN_TEST(test_high_water_holds_the_deepest_occupancy);
  RUN_TEST(test_events_have_their_own_queue_and_count);
  return UNITY_END();
}
