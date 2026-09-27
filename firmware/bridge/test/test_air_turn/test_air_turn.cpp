// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The poll clash - one exchange on the air at a time (air_turn.h; PRD R-3.1d).
//
// THE PROPERTY UNDER TEST: no two exchanges wait for their answers at the same time. With
// a scheduled poll or any other exchange outstanding, no other exchange starts; with
// anything in flight or waiting, no scheduled poll starts. And R-5.3d: an OTA upload
// starts only when nothing is in flight or waiting, and no exchange starts during one.
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
  TEST_ASSERT_TRUE(air_idle(a));
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
                                 &AirTurn::config_busy, &AirTurn::phy_blocks_traffic,
                                 &AirTurn::hex_busy, &AirTurn::echo_busy};
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

// sched_config() once asked about neither the command nor the roll, and they did not ask
// about the CONFIG, so a command and a CONFIG could share one node's seq space in flight.
void test_each_exchange_holds_every_other() {
  bool AirTurn::*const busy[] = {&AirTurn::command_busy, &AirTurn::roll_busy,
                                 &AirTurn::config_busy, &AirTurn::phy_blocks_traffic,
                                 &AirTurn::hex_busy, &AirTurn::echo_busy};
  for (bool AirTurn::*const field : busy) {
    AirTurn a;
    a.*field = true;
    TEST_ASSERT_FALSE(exchange_may_start(a));
  }
}

// A request waiting in its queue holds a poll, not an exchange: it IS the next exchange.
void test_a_waiting_request_holds_no_exchange() {
  AirTurn a;
  a.request_waiting = true;
  TEST_ASSERT_TRUE(exchange_may_start(a));
}

// TODO(BF-18)'s case, 2026-09-26: a command between its retries leaves the radio idle, and
// ota_task asked about the scheduled poll alone. Every exchange, and every waiting
// request, now holds an upload.
void test_every_exchange_and_waiting_request_holds_ota() {
  bool AirTurn::*const busy[] = {&AirTurn::poll_outstanding, &AirTurn::command_busy,
                                 &AirTurn::roll_busy,        &AirTurn::config_busy,
                                 &AirTurn::phy_blocks_traffic, &AirTurn::hex_busy,
                                 &AirTurn::echo_busy,          &AirTurn::request_waiting};
  for (bool AirTurn::*const field : busy) {
    AirTurn a;
    a.*field = true;
    TEST_ASSERT_FALSE(air_idle(a));
  }
}

// The other half: an upload ends in a reboot, so nothing starts that it would cut off.
void test_an_upload_holds_every_start() {
  AirTurn a;
  a.ota_in_progress = true;
  TEST_ASSERT_FALSE(poll_may_start(a));
  TEST_ASSERT_FALSE(exchange_may_start(a));
}

}  // namespace

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_an_idle_air_lets_either_start);
  RUN_TEST(test_an_outstanding_poll_holds_every_other_exchange);
  RUN_TEST(test_each_other_exchange_holds_a_scheduled_poll);
  RUN_TEST(test_a_waiting_request_goes_before_a_due_poll);
  RUN_TEST(test_each_exchange_holds_every_other);
  RUN_TEST(test_a_waiting_request_holds_no_exchange);
  RUN_TEST(test_every_exchange_and_waiting_request_holds_ota);
  RUN_TEST(test_an_upload_holds_every_start);
  return UNITY_END();
}
