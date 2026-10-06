// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// L6 - the task table's invariants (GateLink Impl Plan 5.2).
//
// The table is data, so a host test can fail on it. Whether FreeRTOS honours it on the
// board is a bench question, and tools/checks/io_task_never_blocks.py covers the one rule
// that lives in io_task's code rather than in this table.

#include <unity.h>

#include "tasks.h"

using namespace gatelink;

void setUp() {}
void tearDown() {}

// R-5.2a - a relay pulse whose trailing edge is late is a command of the wrong length.
void test_io_outranks_every_other_task() { TEST_ASSERT_TRUE(io_is_strictly_highest()); }

void test_log_is_beneath_every_other_task() { TEST_ASSERT_TRUE(log_is_strictly_lowest()); }

void test_nothing_sits_below_the_arduino_loop() {
  TEST_ASSERT_TRUE(all_priorities_above_arduino_loop());
}

void test_task_names_are_unique() { TEST_ASSERT_TRUE(task_names_are_unique()); }

// A zero stack cannot start, and an empty name leaves a hole in a backtrace.
void test_every_row_is_populated() {
  for (size_t i = 0; i < kTaskCount; ++i) {
    const TaskSpec& s = task_table()[i];
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(i), static_cast<uint8_t>(s.id));
    TEST_ASSERT_NOT_NULL(s.name);
    TEST_ASSERT_TRUE(s.name[0] != '\0');
    TEST_ASSERT_TRUE(s.stack_bytes >= 2048);
  }
}

// Root rule 8. io_task's and bms_task's periods are parameters, so the table names the row
// rather than a number, and the row must exist.
void test_period_params_exist() { TEST_ASSERT_TRUE(period_params_exist()); }

void test_io_period_is_input_poll_ms() {
  TEST_ASSERT_EQUAL_HEX16(0x1010, task_spec(TaskId::Io).period_param);
  TEST_ASSERT_EQUAL_UINT32(100, default_period_ms(task_spec(TaskId::Io)));
}

// io_task reads these four rows by id. A renumbered row would hand it another parameter's
// default; Impl Plan 4.4 states the values.
void test_io_param_defaults() {
  TEST_ASSERT_EQUAL_UINT32(500, param_default(kParamRelayPulseMs));
  TEST_ASSERT_EQUAL_UINT32(500, param_default(kParamRelayMinSpacingMs));
  TEST_ASSERT_EQUAL_UINT32(100, param_default(kParamInputPollMs));
  TEST_ASSERT_EQUAL_UINT32(2, param_default(kParamInputDebounceSamples));
  TEST_ASSERT_EQUAL_UINT32(0, param_default(0x10FF));
}

// bms_poll_s is in seconds; the period comes out in milliseconds.
void test_bms_period_is_bms_poll_s_scaled() {
  TEST_ASSERT_EQUAL_HEX16(0x1040, task_spec(TaskId::Bms).period_param);
  TEST_ASSERT_EQUAL_UINT32(300000, default_period_ms(task_spec(TaskId::Bms)));
}

// A task has a fixed tick or a period parameter, never both: two sources for one period
// is one of them being ignored.
void test_no_task_has_two_period_sources() {
  for (size_t i = 0; i < kTaskCount; ++i) {
    const TaskSpec& s = task_table()[i];
    TEST_ASSERT_FALSE(s.period_ms != 0 && s.period_param != 0);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_io_outranks_every_other_task);
  RUN_TEST(test_log_is_beneath_every_other_task);
  RUN_TEST(test_nothing_sits_below_the_arduino_loop);
  RUN_TEST(test_task_names_are_unique);
  RUN_TEST(test_every_row_is_populated);
  RUN_TEST(test_period_params_exist);
  RUN_TEST(test_io_period_is_input_poll_ms);
  RUN_TEST(test_io_param_defaults);
  RUN_TEST(test_bms_period_is_bms_poll_s_scaled);
  RUN_TEST(test_no_task_has_two_period_sources);
  return UNITY_END();
}
