// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Handoff 6 requirement 7 - the PA configuration record.
//
// WHAT THESE TESTS CAN AND CANNOT PROVE. They prove the lookup, the index arithmetic,
// the out-of-range refusal and the format. They CANNOT prove the mirror matches
// RadioLib, because RadioLib's paOptTable is file-static and this environment does not
// build RadioLib at all. That premise has its own check, and it is a tool rather than
// a test for exactly that reason:
//
//     python3 tools/rangetest/check_pa_table.py
//
// The spot values below are transcribed from RadioLib 7.7.1 SX1262.cpp by hand, which
// makes them a second, independent copy of six entries - the same trick tools/vectors
// uses against the codec. If the mirror were edited to match a wrong table, these six
// would have to be edited too, and by someone reading the upstream source again.

#include <unity.h>

#include <cstring>

#include "pa_config.h"
#include "phy_params.h"

using namespace rangetest;

void setUp() {}
void tearDown() {}

namespace {

void expect_entry(int8_t dbm, uint8_t duty, uint8_t hp, int8_t pa_val) {
  const PaConfig c = pa_config_for(dbm, true);
  TEST_ASSERT_TRUE_MESSAGE(c.in_range, "expected an in-range entry");
  TEST_ASSERT_EQUAL_UINT8(duty, c.pa_duty_cycle);
  TEST_ASSERT_EQUAL_UINT8(hp, c.hp_max);
  TEST_ASSERT_EQUAL_INT8(pa_val, c.pa_val);
}

}  // namespace

// Both ends of the table, so an off-by-one in `power + 9` cannot hide in the middle.
void test_table_endpoints() {
  expect_entry(kSx1262MinDbm, 2, 2, -5);   // -9 dBm, entry 0
  expect_entry(kSx1262MaxDbm, 4, 7, 22);   // +22 dBm, entry 31
}

// -4 dBm is the D33 working point (spec 18.2) at the fitted 3.0 dBi antenna, so this
// is the entry every range-test trace from here on will actually carry.
void test_working_point_minus_4_dbm() {
  expect_entry(-4, 1, 2, 3);               // entry 5
}

void test_interior_entries() {
  expect_entry(0, 2, 1, 11);               // entry 9
  expect_entry(14, 1, 4, 20);              // entry 23
  expect_entry(6, 3, 1, 21);               // entry 15 - power is index + kSx1262MinDbm
}

// The clamp should make this unreachable; the guard is what keeps a mirror from
// reading out of bounds while documenting code that refuses first.
void test_out_of_range_reports_no_entry() {
  const PaConfig lo = pa_config_for(kSx1262MinDbm - 1, true);
  TEST_ASSERT_FALSE(lo.in_range);
  const PaConfig hi = pa_config_for(kSx1262MaxDbm + 1, true);
  TEST_ASSERT_FALSE(hi.in_range);

  // And it must not quietly report entry 0 or entry 31 either.
  TEST_ASSERT_EQUAL_INT8(0, lo.pa_val);
  TEST_ASSERT_EQUAL_UINT8(0, lo.pa_duty_cycle);
}

// The datasheet-default branch. Not used today - kPaOptimize is true - but it is the
// other half of the flag being logged, and a record of a flag whose false branch was
// never exercised is a weaker record.
void test_unoptimized_uses_datasheet_defaults() {
  const PaConfig c = pa_config_for(-4, false);
  TEST_ASSERT_TRUE(c.in_range);
  TEST_ASSERT_FALSE(c.optimize);
  TEST_ASSERT_EQUAL_INT8(-4, c.pa_val);            // the requested power itself
  TEST_ASSERT_EQUAL_UINT8(kPaDutyCycleDefault, c.pa_duty_cycle);
  TEST_ASSERT_EQUAL_UINT8(kPaHpMaxDefault, c.hp_max);
}

// kPaOptimize is what the firmware passes and what the boot line reports. If it is
// ever changed, this test is the place that says the change was deliberate.
void test_project_flag_is_radiolib_default() {
  TEST_ASSERT_TRUE_MESSAGE(kPaOptimize,
                           "kPaOptimize no longer matches RadioLib's 1-arg default - "
                           "intended? every prior trace was captured at true");
}

// capture.py collects `^[a-z][a-z0-9_]*=\S*$` lines into the trace header. A space in
// a value, or a line that is not key=value, is silently dropped from the trace - so
// the format is asserted rather than eyeballed.
void test_format_is_capture_parseable() {
  char buf[192];
  const size_t n = format_pa_config(pa_config_for(-4, true), buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN_UINT32(0, n);
  TEST_ASSERT_EQUAL_UINT32(std::strlen(buf), n);

  TEST_ASSERT_NOT_NULL(std::strstr(buf, "pa_optimize=1\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "pa_duty_cycle=1\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "pa_hp_max=2\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "pa_val=3\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "pa_table=RadioLib-7.7.1-paOptTable\n"));

  // No spaces anywhere: one space in one value and capture.py drops that line.
  TEST_ASSERT_NULL(std::strchr(buf, ' '));
  TEST_ASSERT_EQUAL_CHAR('\n', buf[n - 1]);
}

void test_format_reports_absent_entry_explicitly() {
  char buf[192];
  const size_t n = format_pa_config(pa_config_for(kSx1262MaxDbm + 1, true), buf,
                                    sizeof(buf));
  TEST_ASSERT_GREATER_THAN_UINT32(0, n);
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "pa_entry=none\n"));
  // No half-record: the fields that have no value must not be printed at all.
  TEST_ASSERT_NULL(std::strstr(buf, "pa_val="));
  TEST_ASSERT_NULL(std::strchr(buf, ' '));
}

// Refuses to truncate, for format_settings()'s reason.
void test_format_refuses_to_truncate() {
  char buf[8];
  TEST_ASSERT_EQUAL_UINT32(0, format_pa_config(pa_config_for(-4, true), buf,
                                               sizeof(buf)));
  TEST_ASSERT_EQUAL_UINT32(0, format_pa_config(pa_config_for(-4, true), buf, 0));
  TEST_ASSERT_EQUAL_UINT32(0, format_pa_config(pa_config_for(-4, true), nullptr, 64));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_table_endpoints);
  RUN_TEST(test_working_point_minus_4_dbm);
  RUN_TEST(test_interior_entries);
  RUN_TEST(test_out_of_range_reports_no_entry);
  RUN_TEST(test_unoptimized_uses_datasheet_defaults);
  RUN_TEST(test_project_flag_is_radiolib_default);
  RUN_TEST(test_format_is_capture_parseable);
  RUN_TEST(test_format_reports_absent_entry_explicitly);
  RUN_TEST(test_format_refuses_to_truncate);
  return UNITY_END();
}
