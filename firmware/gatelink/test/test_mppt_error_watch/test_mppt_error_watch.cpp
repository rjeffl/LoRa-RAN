// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// MpptErrorWatch: PRD R-3.5b; spec 8.9; D79, D86.

#include <unity.h>

#include "lran/types.h"
#include "mppt_error_watch.h"

using gatelink::EventIds;
using gatelink::GateContext;
using gatelink::MpptErrorWatch;
using gatelink::MpptSnapshot;
using gatelink::Push;
using gatelink::PushList;

void setUp() {}
void tearDown() {}

namespace {

MpptSnapshot with_err(uint8_t err) {
  MpptSnapshot m;
  m.mppt_flags = 0;
  m.mppt_err   = err;
  return m;
}

void assert_pushed(const PushList& out, uint16_t detail) {
  TEST_ASSERT_EQUAL_size_t(2, out.size());
  TEST_ASSERT_TRUE(out[0].kind == Push::Kind::Status);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lran::StatusReason::MpptError),
                          static_cast<uint8_t>(out[0].reason));
  TEST_ASSERT_TRUE(out[1].kind == Push::Kind::Event);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lran::EventType::MpptError), out[1].event.event_type);
  TEST_ASSERT_EQUAL_UINT16(detail, out[1].event.detail);
}

}  // namespace

void test_no_error_sends_nothing() {
  MpptErrorWatch w;
  EventIds       ids;
  PushList       out;
  TEST_ASSERT_FALSE(w.update(with_err(0), GateContext{}, &ids, &out));
  TEST_ASSERT_EQUAL_size_t(0, out.size());
}

// An error present at boot has no re-send to cover it.
void test_error_at_first_read_is_sent() {
  MpptErrorWatch w;
  EventIds       ids;
  PushList       out;
  TEST_ASSERT_TRUE(w.update(with_err(17), GateContext{}, &ids, &out));
  assert_pushed(out, 17);
}

void test_held_error_sends_once_and_clear_sends_zero() {
  MpptErrorWatch w;
  EventIds       ids;
  PushList       first;
  w.update(with_err(17), GateContext{}, &ids, &first);
  PushList held;
  TEST_ASSERT_FALSE(w.update(with_err(17), GateContext{}, &ids, &held));
  TEST_ASSERT_EQUAL_size_t(0, held.size());
  PushList clear;
  TEST_ASSERT_TRUE(w.update(with_err(0), GateContext{}, &ids, &clear));
  assert_pushed(clear, 0);
}

void test_a_different_error_is_sent() {
  MpptErrorWatch w;
  EventIds       ids;
  PushList       first;
  w.update(with_err(17), GateContext{}, &ids, &first);
  PushList next;
  TEST_ASSERT_TRUE(w.update(with_err(2), GateContext{}, &ids, &next));
  assert_pushed(next, 2);
}

// D79 - 0xFF is not an error, and must not read as a change either.
void test_not_available_and_stale_change_nothing() {
  MpptErrorWatch w;
  EventIds       ids;
  PushList       first;
  w.update(with_err(17), GateContext{}, &ids, &first);
  PushList out;
  TEST_ASSERT_FALSE(w.update(with_err(gatelink::kCodeNotAvailable), GateContext{}, &ids, &out));
  MpptSnapshot stale = with_err(0);
  stale.mppt_flags   = gatelink::kMpptFlagStale;
  TEST_ASSERT_FALSE(w.update(stale, GateContext{}, &ids, &out));
  TEST_ASSERT_EQUAL_size_t(0, out.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_error_sends_nothing);
  RUN_TEST(test_error_at_first_read_is_sent);
  RUN_TEST(test_held_error_sends_once_and_clear_sends_zero);
  RUN_TEST(test_a_different_error_is_sent);
  RUN_TEST(test_not_available_and_stale_change_nothing);
  return UNITY_END();
}
