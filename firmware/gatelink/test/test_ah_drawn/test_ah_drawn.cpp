// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// AhDrawn: PRD R-6.4c's count, spec 7.2.10.

#include <unity.h>

#include <initializer_list>

#include "ah_drawn.h"
#include "lran/types.h"

using gatelink::AhDrawn;
using gatelink::BmsSnapshot;
using gatelink::MpptSnapshot;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kPollS = 300;

MpptSnapshot charging() {
  MpptSnapshot m;
  m.mppt_flags   = 0;
  m.charge_state = gatelink::kCsFloat;
  m.batt_ma      = 500;
  return m;
}

MpptSnapshot idle() {
  MpptSnapshot m;
  m.mppt_flags   = 0;
  m.charge_state = 0;
  m.batt_ma      = 0;
  return m;
}

BmsSnapshot read(uint32_t dah, uint32_t at_ms) {
  BmsSnapshot b;
  b.have               = true;
  b.data.remaining_dAh = dah;
  b.read_ms            = at_ms;
  return b;
}

}  // namespace

// spec 7.2.10 - a reset never restarts the count from 0.
void test_no_reference_reads_unavailable() {
  AhDrawn a;
  TEST_ASSERT_EQUAL_UINT16(lran::kU16NotAvailable, a.update(idle(), BmsSnapshot{}, 1000, kPollS));
  TEST_ASSERT_EQUAL_UINT16(lran::kU16NotAvailable, a.update(idle(), read(500, 2000), 3000, kPollS));
}

void test_reads_zero_while_charging_and_falls_after() {
  AhDrawn a;
  TEST_ASSERT_EQUAL_UINT16(0, a.update(charging(), read(1000, 1000), 1000, kPollS));
  TEST_ASSERT_EQUAL_UINT16(0, a.update(idle(), read(1000, 1000), 2000, kPollS));
  TEST_ASSERT_EQUAL_UINT16(5, a.update(idle(), read(995, 300000), 300000, kPollS));
  TEST_ASSERT_EQUAL_UINT16(12, a.update(idle(), read(988, 600000), 600000, kPollS));
}

// The BMS rising without charging seen, such as a recalibration, floors at 0.
void test_floors_at_zero() {
  AhDrawn a;
  a.update(charging(), read(1000, 1000), 1000, kPollS);
  TEST_ASSERT_EQUAL_UINT16(0, a.update(idle(), read(1010, 300000), 300000, kPollS));
}

void test_each_charging_period_moves_the_reference() {
  AhDrawn a;
  a.update(charging(), read(1000, 1000), 1000, kPollS);
  a.update(idle(), read(900, 300000), 300000, kPollS);
  a.update(charging(), read(950, 600000), 600000, kPollS);
  TEST_ASSERT_EQUAL_UINT16(10, a.update(idle(), read(940, 900000), 900000, kPollS));
}

// A brief charge between two polls keeps the night's reference.
void test_brief_charge_keeps_the_reference() {
  AhDrawn a;
  a.update(charging(), read(1000, 1000), 1000, kPollS);
  a.update(idle(), read(990, 300000), 300000, kPollS);
  TEST_ASSERT_EQUAL_UINT16(0, a.update(charging(), read(990, 300000), 310000, kPollS));
  TEST_ASSERT_EQUAL_UINT16(10, a.update(idle(), read(990, 300000), 320000, kPollS));
}

// Charging the BMS never saw makes the reference meaningless.
void test_long_charge_with_no_read_drops_the_reference() {
  AhDrawn a;
  a.update(charging(), read(1000, 1000), 1000, kPollS);
  const BmsSnapshot old = read(1000, 1000);
  TEST_ASSERT_EQUAL_UINT16(0, a.update(charging(), old, 2 * kPollS * 1000 + 5000, kPollS));
  TEST_ASSERT_EQUAL_UINT16(lran::kU16NotAvailable, a.update(idle(), old, 2 * kPollS * 1000 + 6000, kPollS));
}

// R-6.4c: charger state alone is not charging, and a stale MPPT says nothing.
void test_charging_active_needs_state_current_and_a_fresh_mppt() {
  MpptSnapshot no_current = charging();
  no_current.batt_ma      = 0;
  MpptSnapshot stale      = charging();
  stale.mppt_flags        = gatelink::kMpptFlagStale;
  MpptSnapshot off        = charging();
  off.charge_state        = 0;
  for (const MpptSnapshot& m : {no_current, stale, off}) {
    AhDrawn a;
    TEST_ASSERT_EQUAL_UINT16(lran::kU16NotAvailable, a.update(m, read(1000, 1000), 1000, kPollS));
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_reference_reads_unavailable);
  RUN_TEST(test_reads_zero_while_charging_and_falls_after);
  RUN_TEST(test_floors_at_zero);
  RUN_TEST(test_each_charging_period_moves_the_reference);
  RUN_TEST(test_brief_charge_keeps_the_reference);
  RUN_TEST(test_long_charge_with_no_read_drops_the_reference);
  RUN_TEST(test_charging_active_needs_state_current_and_a_fresh_mppt);
  return UNITY_END();
}
