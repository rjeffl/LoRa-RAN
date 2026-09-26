// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The CAD guard's view of an arriving frame. Spec 12.3; rx_arrival.h has the loss it
// closes, a BOOT event destroyed by a CAD between its preamble and its header.

#include <unity.h>

#include "lran/link/radio_config.h"
#include "lran/link/rx_arrival.h"

using namespace lran::link;

namespace {

constexpr uint32_t kPreambleMs = 88;
constexpr uint32_t kFrameMs    = 1500;
constexpr uint32_t kHoldoffMs  = 176;

RxArrival make() {
  RxArrival a;
  a.set_bounds(kPreambleMs, kFrameMs, kHoldoffMs);
  return a;
}

int as_int(RxArrivalState s) { return static_cast<int>(s); }

}  // namespace

void setUp() {}
void tearDown() {}

// 8 preamble symbols + 4.25 sync + 8 header + 1 rounding = 21.25 symbols of 4096 us.
void test_the_bound_at_d1_phy_is_88_ms() {
  TEST_ASSERT_EQUAL_UINT32(88, preamble_to_header_ms(kPhy));
}

// Each SF step doubles the symbol, so a PHY change moves the bound with it.
void test_the_bound_follows_the_phy() {
  PhyConfig p = kPhy;
  p.sf        = 12;
  TEST_ASSERT_EQUAL_UINT32(697, preamble_to_header_ms(p));  // 21.25 x 32.768 ms
  p.sf       = 7;
  p.bw_khz10 = 2500;
  TEST_ASSERT_EQUAL_UINT32(11, preamble_to_header_ms(p));   // 21.25 x 0.512 ms
}

// Twice the preamble-to-header time, so it scales with the PHY too.
void test_the_holdoff_at_d1_phy_is_176_ms() {
  TEST_ASSERT_EQUAL_UINT32(176, burst_holdoff_ms(kPhy));
  PhyConfig p = kPhy;
  p.sf        = 12;
  TEST_ASSERT_EQUAL_UINT32(1394, burst_holdoff_ms(p));
}

// The loss the preamble guard left: a BOOT event starting 39 ms after its status.
void test_no_cad_straight_after_a_reception() {
  RxArrival a = make();
  TEST_ASSERT_FALSE(a.holding_off(1000));  // nothing received yet
  a.note_reception_end(1000);
  TEST_ASSERT_TRUE(a.holding_off(1000));
  TEST_ASSERT_TRUE(a.holding_off(1039));
  TEST_ASSERT_TRUE(a.holding_off(1175));
  TEST_ASSERT_FALSE(a.holding_off(1176));
}

// Restarting receive clears the register, not the time since the last frame ended.
void test_reset_keeps_the_holdoff() {
  RxArrival a = make();
  a.note_reception_end(1000);
  a.reset();
  TEST_ASSERT_TRUE(a.holding_off(1100));
}

void test_the_holdoff_survives_a_millis_wrap() {
  RxArrival a = make();
  a.note_reception_end(0xFFFFFFF0u);
  TEST_ASSERT_TRUE(a.holding_off(0x20));
  TEST_ASSERT_FALSE(a.holding_off(0xFFFFFFF0u + kHoldoffMs));
}

void test_no_flag_is_idle() {
  RxArrival a = make();
  TEST_ASSERT_EQUAL_INT(as_int(RxArrivalState::Idle), as_int(a.observe(false, false, 1000)));
  TEST_ASSERT_FALSE(a.arriving(1000));
}

// The case the header-only guard missed.
void test_a_fresh_preamble_is_arriving() {
  RxArrival a = make();
  TEST_ASSERT_EQUAL_INT(as_int(RxArrivalState::Arriving), as_int(a.observe(true, false, 1000)));
  TEST_ASSERT_EQUAL_INT(as_int(RxArrivalState::Arriving), as_int(a.observe(true, false, 1087)));
  TEST_ASSERT_TRUE(a.arriving(1087));
}

// A preamble with no header behind it is a false detection or a dead reception.
void test_a_preamble_past_its_bound_is_stale() {
  RxArrival a = make();
  (void)a.observe(true, false, 1000);
  TEST_ASSERT_EQUAL_INT(as_int(RxArrivalState::Stale), as_int(a.observe(true, false, 1088)));
  TEST_ASSERT_FALSE(a.arriving(1088));
}

// Once the header is valid, the frame's bound applies, counted from the header.
void test_a_header_extends_the_preamble() {
  RxArrival a = make();
  (void)a.observe(true, false, 1000);
  TEST_ASSERT_EQUAL_INT(as_int(RxArrivalState::Arriving), as_int(a.observe(true, true, 1080)));
  TEST_ASSERT_EQUAL_INT(as_int(RxArrivalState::Arriving), as_int(a.observe(true, true, 2579)));
  TEST_ASSERT_TRUE(a.arriving(2579));
  TEST_ASSERT_EQUAL_INT(as_int(RxArrivalState::Stale), as_int(a.observe(true, true, 2580)));
}

void test_reset_forgets_both_flags() {
  RxArrival a = make();
  (void)a.observe(true, true, 1000);
  a.reset();
  TEST_ASSERT_FALSE(a.arriving(1001));
  // A new reception is timed from its own first sighting, not the old one.
  TEST_ASSERT_EQUAL_INT(as_int(RxArrivalState::Arriving), as_int(a.observe(true, false, 5000)));
}

// A flag first read late is trusted from that read, so lateness only lengthens it.
void test_age_counts_from_the_first_read() {
  RxArrival a = make();
  (void)a.observe(true, false, 1000);
  (void)a.observe(true, false, 1050);
  TEST_ASSERT_EQUAL_INT(as_int(RxArrivalState::Stale), as_int(a.observe(true, false, 1090)));
}

void test_a_millis_wrap_is_not_ancient() {
  RxArrival a = make();
  (void)a.observe(true, false, 0xFFFFFFF0u);
  TEST_ASSERT_EQUAL_INT(as_int(RxArrivalState::Arriving), as_int(a.observe(true, false, 0x20)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_bound_at_d1_phy_is_88_ms);
  RUN_TEST(test_the_bound_follows_the_phy);
  RUN_TEST(test_the_holdoff_at_d1_phy_is_176_ms);
  RUN_TEST(test_no_cad_straight_after_a_reception);
  RUN_TEST(test_reset_keeps_the_holdoff);
  RUN_TEST(test_the_holdoff_survives_a_millis_wrap);
  RUN_TEST(test_no_flag_is_idle);
  RUN_TEST(test_a_fresh_preamble_is_arriving);
  RUN_TEST(test_a_preamble_past_its_bound_is_stale);
  RUN_TEST(test_a_header_extends_the_preamble);
  RUN_TEST(test_reset_forgets_both_flags);
  RUN_TEST(test_age_counts_from_the_first_read);
  RUN_TEST(test_a_millis_wrap_is_not_ancient);
  return UNITY_END();
}
