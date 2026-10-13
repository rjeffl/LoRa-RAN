// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL3 - gate state, hold source and movement cause (PRD R-3.1.4a-b; GateLink Impl Plan 6.2,
// 6.3).

#include <unity.h>

#include "gate_state.h"

using namespace gatelink;
using lran::GateState;
using lran::HoldSource;
using lran::MovementCause;

namespace {

constexpr uint8_t kClosed    = 0;
constexpr uint8_t kMoving    = kInMoving;
constexpr uint8_t kCountdown = kInOpen | kInMoving;
constexpr uint8_t kHeld      = kInOpen;

GateParams params() { return GateParams{}; }

// Feeds `inputs` every 100 ms, as app_task does, from `*t` for `ms`.
uint8_t run(GateTracker& g, uint8_t inputs, uint32_t* t, uint32_t ms) {
  uint8_t changed = 0;
  for (uint32_t end = *t + ms; *t < end; *t += 100) changed |= g.update(inputs, *t, params());
  return changed;
}

// Closed, then a full open to the given resting inputs.
void open_to(GateTracker& g, uint8_t rest, uint32_t* t) {
  run(g, kClosed, t, 1000);
  run(g, kMoving, t, 15000);
  run(g, rest, t, 3000);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_states_follow_in1_in2() {
  GateTracker g;
  uint32_t    t = 0;
  TEST_ASSERT_EQUAL(GateState::Unknown, g.state());
  TEST_ASSERT_EQUAL_HEX8(kChangedState, g.update(kClosed, t, params()));
  TEST_ASSERT_EQUAL(GateState::Closed, g.state());
  t += 100;
  g.update(kMoving, t, params());
  TEST_ASSERT_EQUAL(GateState::Moving, g.state());
  t += 100;
  g.update(kCountdown, t, params());
  TEST_ASSERT_EQUAL(GateState::OpenCountdown, g.state());
  TEST_ASSERT_FALSE(g.held());
}

// R-3.1.4b - 1/0 is OPEN_HELD only after hold_confirm_ms.
void test_hold_waits_for_confirmation() {
  GateTracker g;
  uint32_t    t = 0;
  run(g, kCountdown, &t, 500);
  TEST_ASSERT_EQUAL(0, run(g, kHeld, &t, 1900));
  TEST_ASSERT_EQUAL(GateState::OpenCountdown, g.state());
  TEST_ASSERT_EQUAL_HEX8(kChangedState | kChangedHold, run(g, kHeld, &t, 200));
  TEST_ASSERT_EQUAL(GateState::OpenHeld, g.state());
  TEST_ASSERT_TRUE(g.held());
}

// R-3.1.4b - the brief 1/0 at the start of a close never becomes a hold.
void test_transient_one_zero_is_absorbed() {
  GateTracker g;
  uint32_t    t = 0;
  open_to(g, kCountdown, &t);
  TEST_ASSERT_EQUAL(0, run(g, kHeld, &t, 600));
  run(g, kCountdown, &t, 300);
  TEST_ASSERT_EQUAL(GateState::OpenCountdown, g.state());
  TEST_ASSERT_FALSE(g.held());
}

// Impl Plan 6.2 - a hold there at boot is UNKNOWN.
void test_hold_at_boot_is_unknown() {
  GateTracker g;
  uint32_t    t = 0;
  TEST_ASSERT_EQUAL_HEX8(kChangedState | kChangedHold, run(g, kHeld, &t, 2100));
  TEST_ASSERT_EQUAL(HoldSource::Unknown, g.hold_source());
  TEST_ASSERT_EQUAL_HEX8(0x01 | (0x04 << 1), g.hold_byte());
}

void test_k1_hold_is_lran() {
  GateTracker g;
  uint32_t    t = 0;
  run(g, kClosed, &t, 1000);
  g.note_pulse(kRelayOpenLock, t);
  run(g, kMoving, &t, 15000);
  run(g, kHeld, &t, 2500);
  TEST_ASSERT_EQUAL(HoldSource::Lran, g.hold_source());
  TEST_ASSERT_EQUAL(MovementCause::LranCommand, g.cause());
}

// K1 on a gate already counting down holds it where it stands.
void test_k1_on_open_gate_is_lran() {
  GateTracker g;
  uint32_t    t = 0;
  open_to(g, kCountdown, &t);
  g.note_pulse(kRelayOpenLock, t);
  run(g, kCountdown, &t, 300);
  run(g, kHeld, &t, 2500);
  TEST_ASSERT_EQUAL(HoldSource::Lran, g.hold_source());
}

void test_manual_hold_and_its_cause() {
  GateTracker g;
  uint32_t    t = 0;
  open_to(g, kHeld, &t);
  TEST_ASSERT_EQUAL(HoldSource::Manual, g.hold_source());
  TEST_ASSERT_EQUAL(MovementCause::ManualHold, g.cause());
}

// A K1 that moved nothing must not claim a manual hold made later.
void test_stale_k1_does_not_claim_a_later_hold() {
  GateTracker g;
  uint32_t    t = 0;
  run(g, kClosed, &t, 1000);
  g.note_pulse(kRelayOpenLock, t);
  run(g, kClosed, &t, 20000);
  run(g, kMoving, &t, 15000);
  run(g, kHeld, &t, 2500);
  TEST_ASSERT_EQUAL(HoldSource::Manual, g.hold_source());
}

void test_k2_releases_the_claim() {
  GateTracker g;
  uint32_t    t = 0;
  open_to(g, kCountdown, &t);
  g.note_pulse(kRelayOpenLock, t);
  run(g, kCountdown, &t, 200);
  g.note_pulse(kRelayUnlock, t);
  run(g, kHeld, &t, 2500);
  TEST_ASSERT_EQUAL(HoldSource::Manual, g.hold_source());
}

// FIRE is a placeholder under D77; the path stays for a revision whose IN5 reads.
void test_fire_hold_is_keypad_fire() {
  GateTracker g;
  uint32_t    t = 0;
  run(g, kClosed, &t, 1000);
  run(g, kClosed | kInFire, &t, 500);
  run(g, kMoving | kInFire, &t, 15000);
  run(g, kHeld | kInFire, &t, 2500);
  TEST_ASSERT_EQUAL(HoldSource::KeypadFire, g.hold_source());
  TEST_ASSERT_EQUAL(MovementCause::KeypadFire, g.cause());
}

void test_release_reports_hold_change() {
  GateTracker g;
  uint32_t    t = 0;
  open_to(g, kHeld, &t);
  TEST_ASSERT_EQUAL_HEX8(kChangedState | kChangedHold, run(g, kCountdown, &t, 100));
  TEST_ASSERT_EQUAL(HoldSource::None, g.hold_source());
  TEST_ASSERT_EQUAL_HEX8(0x00, g.hold_byte());
}

void test_exit_edge_before_opening_is_the_wand() {
  GateTracker g;
  uint32_t    t = 0;
  run(g, kClosed, &t, 1000);
  run(g, kClosed | kInExit, &t, 1500);
  run(g, kMoving | kInExit, &t, 15000);
  TEST_ASSERT_EQUAL(MovementCause::ExitWand, g.cause());
}

void test_external_momentary_open() {
  GateTracker g;
  uint32_t    t = 0;
  open_to(g, kCountdown, &t);
  TEST_ASSERT_EQUAL(MovementCause::ExternalMomentary, g.cause());
}

void test_k3_open_is_lran_command() {
  GateTracker g;
  uint32_t    t = 0;
  run(g, kClosed, &t, 1000);
  g.note_pulse(kRelayOpen, t);
  run(g, kMoving, &t, 15000);
  run(g, kCountdown, &t, 1000);
  TEST_ASSERT_EQUAL(MovementCause::LranCommand, g.cause());
  TEST_ASSERT_FALSE(g.held());
}

// An EXIT edge long before the opening is not its cause.
void test_old_exit_edge_is_not_the_cause() {
  GateTracker g;
  uint32_t    t = 0;
  run(g, kClosed | kInExit, &t, 500);
  run(g, kClosed, &t, 1000);
  run(g, kClosed | kInExit, &t, 500);
  run(g, kClosed, &t, 20000);
  run(g, kMoving, &t, 15000);
  run(g, kCountdown, &t, 500);
  TEST_ASSERT_EQUAL(MovementCause::ExternalMomentary, g.cause());
}

void test_alarm_is_fault() {
  GateTracker g;
  uint32_t    t = 0;
  run(g, kClosed | kInAlarm, &t, 200);
  TEST_ASSERT_EQUAL(GateState::Fault, g.state());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_states_follow_in1_in2);
  RUN_TEST(test_hold_waits_for_confirmation);
  RUN_TEST(test_transient_one_zero_is_absorbed);
  RUN_TEST(test_hold_at_boot_is_unknown);
  RUN_TEST(test_k1_hold_is_lran);
  RUN_TEST(test_k1_on_open_gate_is_lran);
  RUN_TEST(test_manual_hold_and_its_cause);
  RUN_TEST(test_stale_k1_does_not_claim_a_later_hold);
  RUN_TEST(test_k2_releases_the_claim);
  RUN_TEST(test_fire_hold_is_keypad_fire);
  RUN_TEST(test_release_reports_hold_change);
  RUN_TEST(test_exit_edge_before_opening_is_the_wand);
  RUN_TEST(test_external_momentary_open);
  RUN_TEST(test_k3_open_is_lran_command);
  RUN_TEST(test_old_exit_edge_is_not_the_cause);
  RUN_TEST(test_alarm_is_fault);
  return UNITY_END();
}
