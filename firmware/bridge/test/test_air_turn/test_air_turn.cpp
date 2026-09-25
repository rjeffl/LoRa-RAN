// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The poll clash - one exchange on the air at a time (air_turn.h; PRD R-3.1d).
//
// THE PROPERTY UNDER TEST: a scheduled POLL and any other exchange never wait for their
// answers at the same time. With one scheduled poll outstanding, nothing else starts;
// with anything else in flight or waiting, no scheduled poll starts.
//
// WHAT THIS CANNOT COVER. That sched_task asks these questions at every start, and a node
// answering on air. The bench run in the engineering log is where the fix is proven.

#include <unity.h>

#include "air_turn.h"

using namespace bridge;

void setUp() {}
void tearDown() {}

namespace {

void test_an_idle_air_lets_either_start() {
  const AirTurn a;
  TEST_ASSERT_TRUE(poll_may_start(a));
  TEST_ASSERT_TRUE(exchange_may_start(a));
}

// The 2026-09-24 case: the POLL to f2 went, so the PHY change's SET must wait.
void test_an_outstanding_poll_holds_every_other_exchange() {
  AirTurn a;
  a.poll_outstanding = true;
  TEST_ASSERT_FALSE(exchange_may_start(a));
}

// The reverse case, inferred from the code: a POLL sent while an answer is still due.
void test_each_other_exchange_holds_a_scheduled_poll() {
  bool AirTurn::*const busy[] = {&AirTurn::command_busy, &AirTurn::roll_busy,
                                 &AirTurn::config_busy, &AirTurn::phy_blocks_traffic};
  for (bool AirTurn::*const field : busy) {
    AirTurn a;
    a.*field = true;
    TEST_ASSERT_FALSE(poll_may_start(a));
  }
}

void test_a_waiting_request_goes_before_a_due_poll() {
  AirTurn a;
  a.request_waiting = true;
  TEST_ASSERT_FALSE(poll_may_start(a));
  TEST_ASSERT_TRUE(exchange_may_start(a));
}

// Only the scheduled poll's own state holds an exchange. Whether one exchange may start
// beside another is each path's own busy() check, which this does not replace.
void test_nothing_but_a_poll_holds_an_exchange() {
  AirTurn a;
  a.command_busy       = true;
  a.roll_busy          = true;
  a.config_busy        = true;
  a.phy_blocks_traffic = true;
  a.request_waiting    = true;
  TEST_ASSERT_TRUE(exchange_may_start(a));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_an_idle_air_lets_either_start);
  RUN_TEST(test_an_outstanding_poll_holds_every_other_exchange);
  RUN_TEST(test_each_other_exchange_holds_a_scheduled_poll);
  RUN_TEST(test_a_waiting_request_goes_before_a_due_poll);
  RUN_TEST(test_nothing_but_a_poll_holds_an_exchange);
  return UNITY_END();
}
