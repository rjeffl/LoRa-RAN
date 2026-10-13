// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL3 - direction classification, partial traversals and the held-open alert (PRD 3.2.2-
// 3.2.4, R-5.4b's long cases; spec 7.2.5, 7.3).

#include <unity.h>

#include "detect.h"
#include "gate_state.h"

using namespace gatelink;
using lran::Direction;
using lran::EventType;
using lran::HoldSource;

namespace {

constexpr uint8_t kS = kInSafety;
constexpr uint8_t kE = kInExit;

struct Rig {
  EventIds     ids;
  Detector     d{&ids};
  DetectParams p;
  GateContext  g;
  uint32_t     t = 0;
  // Every push since the last take().
  Push   log[64];
  size_t n = 0;

  void run(uint8_t inputs, uint32_t ms) {
    for (uint32_t end = t + ms; t < end; t += 100) {
      PushList out;
      d.update(inputs, t, g, p, &out);
      for (size_t i = 0; i < out.size() && n < 64; ++i) log[n++] = out[i];
    }
  }
  size_t events() const {
    size_t k = 0;
    for (size_t i = 0; i < n; ++i) k += log[i].kind == Push::Kind::Event;
    return k;
  }
  const Push* event(size_t which) const {
    for (size_t i = 0; i < n; ++i) {
      if (log[i].kind == Push::Kind::Event && which-- == 0) return &log[i];
    }
    return nullptr;
  }
};

void assert_detection(const Rig& r, EventType type, Direction dir) {
  TEST_ASSERT_EQUAL(2, r.events());
  const Push* first = r.event(0);
  const Push* last  = r.event(1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(type), first->event.event_type);
  TEST_ASSERT_EQUAL_UINT8(0, first->event.event_flags);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Direction::Undetermined), first->event.direction);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(type), last->event.event_type);
  TEST_ASSERT_EQUAL_UINT8(1, last->event.event_flags);
  TEST_ASSERT_EQUAL_UINT32(first->event.event_id, last->event.event_id);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(dir), last->event.direction);
}

}  // namespace

void setUp() {}
void tearDown() {}

// A departing vehicle: EXIT, a 30 s wait for the gate, then SAFETY (PRD 3.2.2 property 1).
void test_exit_with_a_30_s_gap() {
  Rig r;
  r.run(0, 1000);
  r.run(kE, 30000);
  TEST_ASSERT_EQUAL_HEX8(0x04 | 0x02, r.d.detect_flags(kE));  // mid-sequence
  r.run(kE | kS, 2000);
  r.run(kS, 4000);
  r.run(0, 1000);
  assert_detection(r, EventType::VehicleDetected, Direction::Exit);
  TEST_ASSERT_EQUAL(Direction::Exit, r.d.last_direction());
  TEST_ASSERT_EQUAL_UINT32(7, r.d.last_traversal_age_s(r.t));  // from SAFETY's rise
}

void test_entry_is_safety_then_exit() {
  Rig r;
  r.run(0, 500);
  r.run(kS, 3000);
  r.run(kS | kE, 1000);
  r.run(kE, 1000);
  r.run(0, 500);
  assert_detection(r, EventType::VehicleDetected, Direction::Entry);
}

void test_simultaneous_edges_are_undetermined() {
  Rig r;
  r.run(0, 500);
  r.run(kS | kE, 1000);
  r.run(0, 500);
  assert_detection(r, EventType::VehicleDetected, Direction::Undetermined);
  TEST_ASSERT_NOT_EQUAL(UINT32_MAX, r.d.last_traversal_age_s(r.t));
}

// R-3.2.3d - a vehicle pulls up to the wand and reverses. The idle reset resolves it, and
// it is not a traversal.
void test_partial_at_the_wand_resolves_on_idle() {
  Rig r;
  r.run(0, 500);
  r.run(kE, 5000);
  r.run(0, 9900);
  TEST_ASSERT_EQUAL(1, r.events());
  r.run(0, 200);
  assert_detection(r, EventType::VehicleDetected, Direction::Undetermined);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, r.d.last_traversal_age_s(r.t));
}

// R-3.2.3d - a vehicle stops on the loops. The window resolves it, and the next real
// traversal classifies cleanly.
void test_partial_on_the_loops_does_not_corrupt_the_next() {
  Rig r;
  r.run(0, 500);
  r.run(kS, 70000);
  TEST_ASSERT_EQUAL(2, r.events());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Direction::Undetermined), r.event(1)->event.direction);
  r.run(kS | kE, 1000);  // the tail of the same vehicle starts nothing
  r.run(0, 2000);
  TEST_ASSERT_EQUAL(2, r.events());
  r.run(kE, 20000);
  r.run(kS, 3000);
  r.run(0, 500);
  TEST_ASSERT_EQUAL(4, r.events());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Direction::Exit), r.event(3)->event.direction);
}

// The wand de-asserting and re-asserting inside the window is one detection (PRD 3.2.2
// property 4, M10).
void test_wand_retrigger_is_one_detection() {
  Rig r;
  r.run(0, 500);
  r.run(kE, 3000);
  r.run(0, 2000);
  r.run(kE, 3000);
  r.run(kS, 2000);
  r.run(0, 500);
  assert_detection(r, EventType::VehicleDetected, Direction::Exit);
}

// R-3.2.4b-c - the first edge alerts, for each of the four hold sources, with no delay.
void test_held_open_alert_on_first_edge_for_every_source() {
  const HoldSource sources[] = {HoldSource::Lran, HoldSource::Manual, HoldSource::KeypadFire,
                                HoldSource::Unknown};
  for (HoldSource src : sources) {
    Rig r;
    r.g.held        = true;
    r.g.hold_source = src;
    r.g.gate_state  = lran::GateState::OpenHeld;
    r.run(0, 100);
    r.run(kS, 100);
    TEST_ASSERT_EQUAL(1, r.events());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventType::VehicleWhileHeldOpen),
                            r.event(0)->event.event_type);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(src), r.event(0)->event.hold_source);
    TEST_ASSERT_EQUAL_HEX8(0x01 | 0x04 | 0x08, r.d.detect_flags(kS));
    r.run(kS | kE, 500);
    r.run(0, 500);
    assert_detection(r, EventType::VehicleWhileHeldOpen, Direction::Entry);
  }
}

// R-3.2.4b - a hold that arrives mid-detection alerts at the next edge.
void test_hold_mid_detection_alerts_at_next_edge() {
  Rig r;
  r.run(0, 100);
  r.run(kE, 1000);
  r.g.held        = true;
  r.g.hold_source = HoldSource::Manual;
  r.run(kE | kS, 500);
  // The classifying edge resolves first, then nothing is left to alert on.
  TEST_ASSERT_EQUAL(2, r.events());
  r.run(kS, 500);
  r.run(kS | kE, 200);  // a new edge while settling, under the hold
  TEST_ASSERT_EQUAL(3, r.events());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventType::VehicleWhileHeldOpen),
                          r.event(2)->event.event_type);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Direction::Exit), r.event(2)->event.direction);
}

// R-3.2.3e - at 0 every detection while held alerts.
void test_every_detection_alerts_at_repeat_zero() {
  Rig r;
  r.g.held = true;
  r.run(0, 100);
  for (int i = 0; i < 3; ++i) {
    r.run(kS, 500);
    r.run(0, 11000);
  }
  TEST_ASSERT_EQUAL(6, r.events());
  for (size_t i = 0; i < 6; ++i) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventType::VehicleWhileHeldOpen),
                            r.event(i)->event.event_type);
  }
  TEST_ASSERT_NOT_EQUAL(r.event(0)->event.event_id, r.event(2)->event.event_id);
}

void test_repeat_limit_suppresses_and_flags() {
  Rig r;
  r.g.held           = true;
  r.p.alert_repeat_s = 60;
  r.run(0, 100);
  r.run(kS, 500);
  r.run(0, 11000);
  r.run(kS, 500);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventType::VehicleDetected),
                          r.event(2)->event.event_type);
  TEST_ASSERT_EQUAL_HEX8(0x10, r.d.detect_flags(kS) & 0x18);
  r.run(0, 11000);
  r.run(0, 40000);
  r.run(kS, 500);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventType::VehicleWhileHeldOpen),
                          r.event(4)->event.event_type);
}

void test_status_follows_resolution() {
  Rig r;
  r.run(0, 100);
  r.run(kS | kE, 100);
  size_t statuses = 0;
  for (size_t i = 0; i < r.n; ++i) {
    if (r.log[i].kind == Push::Kind::Status) {
      ++statuses;
      TEST_ASSERT_EQUAL(lran::StatusReason::VehicleDetected, r.log[i].reason);
    }
  }
  TEST_ASSERT_EQUAL(1, statuses);
}

void test_asserted_at_boot_is_not_an_edge() {
  Rig r;
  r.run(kS, 1000);
  TEST_ASSERT_EQUAL(0, r.events());
  TEST_ASSERT_EQUAL(Direction::None, r.d.last_direction());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_exit_with_a_30_s_gap);
  RUN_TEST(test_entry_is_safety_then_exit);
  RUN_TEST(test_simultaneous_edges_are_undetermined);
  RUN_TEST(test_partial_at_the_wand_resolves_on_idle);
  RUN_TEST(test_partial_on_the_loops_does_not_corrupt_the_next);
  RUN_TEST(test_wand_retrigger_is_one_detection);
  RUN_TEST(test_held_open_alert_on_first_edge_for_every_source);
  RUN_TEST(test_hold_mid_detection_alerts_at_next_edge);
  RUN_TEST(test_every_detection_alerts_at_repeat_zero);
  RUN_TEST(test_repeat_limit_suppresses_and_flags);
  RUN_TEST(test_status_follows_resolution);
  RUN_TEST(test_asserted_at_boot_is_not_an_edge);
  return UNITY_END();
}
