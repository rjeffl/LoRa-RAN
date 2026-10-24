// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// AlarmWatch: FIRE and the hard-shutdown latch on their edges (PRD G-9, G-10, R-3.2.5a;
// spec 8.7, 8.9; D83).

#include <unity.h>

#include "alarm_watch.h"
#include "gate_state.h"
#include "lran/types.h"

using gatelink::AlarmWatch;
using gatelink::EventIds;
using gatelink::GateContext;
using gatelink::kInAlarm;
using gatelink::kInFire;
using gatelink::kInOpen;
using gatelink::Push;
using gatelink::PushList;

void setUp() {}
void tearDown() {}

namespace {

void assert_status(const Push& p, lran::StatusReason reason) {
  TEST_ASSERT_TRUE(p.kind == Push::Kind::Status);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(reason), static_cast<uint8_t>(p.reason));
}

void assert_event(const Push& p, lran::EventType type) {
  TEST_ASSERT_TRUE(p.kind == Push::Kind::Event);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(type), p.event.event_type);
}

}  // namespace

// R-3.5i owns an alarm present at boot; taking it here too would send it twice.
void test_first_read_is_a_baseline() {
  AlarmWatch w;
  EventIds   ids;
  PushList   out;
  TEST_ASSERT_FALSE(w.update(kInFire | kInAlarm, GateContext{}, &ids, &out));
  TEST_ASSERT_EQUAL(0, out.size());
  TEST_ASSERT_FALSE(w.update(kInFire | kInAlarm, GateContext{}, &ids, &out));
  TEST_ASSERT_EQUAL(0, out.size());
}

void test_fire_rising_sends_status_then_event() {
  AlarmWatch w;
  EventIds   ids;
  PushList   out;
  GateContext g;
  g.input_bits = kInFire;
  g.uptime_s   = 42;
  (void)w.update(0, g, &ids, &out);
  TEST_ASSERT_TRUE(w.update(kInFire, g, &ids, &out));
  TEST_ASSERT_EQUAL(2, out.size());
  assert_status(out[0], lran::StatusReason::Fire);
  assert_event(out[1], lran::EventType::FireAsserted);
  TEST_ASSERT_EQUAL_HEX8(kInFire, out[1].event.input_bits);
  TEST_ASSERT_EQUAL_UINT32(42, out[1].event.uptime_s);
  TEST_ASSERT_EQUAL_UINT32(1, out[1].event.event_id);
}

// The STATUS carries input_bits and node_flags bit 5, so the clear is sent; the event is not.
void test_falling_sends_status_only() {
  AlarmWatch w;
  EventIds   ids;
  PushList   rise;
  PushList   fall;
  (void)w.update(0, GateContext{}, &ids, &rise);
  (void)w.update(kInAlarm, GateContext{}, &ids, &rise);
  TEST_ASSERT_TRUE(w.update(0, GateContext{}, &ids, &fall));
  TEST_ASSERT_EQUAL(1, fall.size());
  assert_status(fall[0], lran::StatusReason::HardShutdown);
}

void test_shutdown_rising_sends_status_then_event() {
  AlarmWatch w;
  EventIds   ids;
  PushList   out;
  (void)w.update(0, GateContext{}, &ids, &out);
  TEST_ASSERT_TRUE(w.update(kInAlarm, GateContext{}, &ids, &out));
  TEST_ASSERT_EQUAL(2, out.size());
  assert_status(out[0], lran::StatusReason::HardShutdown);
  assert_event(out[1], lran::EventType::HardShutdown);
}

// spec 8.9 - FIRE is the highest priority, so it queues first when both rise together.
void test_both_rising_queue_fire_first() {
  AlarmWatch w;
  EventIds   ids;
  PushList   out;
  (void)w.update(0, GateContext{}, &ids, &out);
  TEST_ASSERT_TRUE(w.update(kInFire | kInAlarm, GateContext{}, &ids, &out));
  TEST_ASSERT_EQUAL(4, out.size());
  assert_status(out[0], lran::StatusReason::Fire);
  assert_event(out[1], lran::EventType::FireAsserted);
  assert_status(out[2], lran::StatusReason::HardShutdown);
  assert_event(out[3], lran::EventType::HardShutdown);
}

void test_other_inputs_are_ignored() {
  AlarmWatch w;
  EventIds   ids;
  PushList   out;
  (void)w.update(0, GateContext{}, &ids, &out);
  TEST_ASSERT_FALSE(w.update(kInOpen | 0xC0, GateContext{}, &ids, &out));
  TEST_ASSERT_EQUAL(0, out.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_read_is_a_baseline);
  RUN_TEST(test_fire_rising_sends_status_then_event);
  RUN_TEST(test_falling_sends_status_only);
  RUN_TEST(test_shutdown_rising_sends_status_then_event);
  RUN_TEST(test_both_rising_queue_fire_first);
  RUN_TEST(test_other_inputs_are_ignored);
  return UNITY_END();
}
