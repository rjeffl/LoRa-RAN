// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-13 - the verdict on a freshly flashed image, and R-5.3d's deferral.
//
// WHAT THIS COVERS. The decision: when an image in PENDING_VERIFY is kept, when it
// is rolled back, and when nothing is decided at all. That decision is what V-B9
// tests on a bench; testing it here first means the bench tests the MECHANISM -
// bootloader, otadata, the extern "C" override - rather than the arithmetic.
//
// WHAT IT CANNOT. That the bootloader actually rolls back, that ArduinoOTA writes
// the other slot, that verifyRollbackLater() is the symbol the core calls. Those
// are V-B9, and V-B9 is not met until it has been run on the bridge board.

#include <unity.h>

#include "ota_policy.h"

using namespace bridge;

void setUp() {}
void tearDown() {}

namespace {

OtaHealth healthy_at(uint32_t uptime_ms) {
  OtaHealth h;
  h.uptime_ms      = uptime_ms;
  h.tasks_started  = true;
  h.mqtt_connected = true;
  return h;
}

}  // namespace

// A USB-flashed image, or one already verified, has nothing to decide - however
// unhealthy it looks. USB is the recovery path; it must never roll itself back.
void test_an_image_not_pending_is_never_rolled_back() {
  OtaHealth dead;  // no tasks, no broker
  dead.uptime_ms = 0xFFFFFFFFu;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaVerdict::Nothing),
                        static_cast<int>(ota_verdict(OtaImageState::NotPending, dead)));
}

// Healthy but young is not proven. The image that connects and then falls over on
// its first discovery publish must not be blessed in its first seconds.
void test_healthy_but_young_waits() {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(OtaVerdict::Wait),
      static_cast<int>(ota_verdict(OtaImageState::PendingVerify, healthy_at(1000))));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaVerdict::Wait),
                        static_cast<int>(ota_verdict(OtaImageState::PendingVerify,
                                                     healthy_at(kOtaMinUptimeMs - 1))));
}

void test_healthy_and_old_enough_is_kept() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaVerdict::MarkValid),
                        static_cast<int>(ota_verdict(OtaImageState::PendingVerify,
                                                     healthy_at(kOtaMinUptimeMs))));
}

// THE CASE ARDUINO'S DEFAULT GETS WRONG. An image that boots and never reaches the
// broker would be marked valid by the core before setup() ran. Here it rolls back at
// the deadline - and a bridge that cannot reach the LAN could not be OTA'd back.
void test_an_image_that_never_reaches_the_broker_rolls_back() {
  OtaHealth h;
  h.tasks_started  = true;
  h.mqtt_connected = false;

  h.uptime_ms = kOtaVerifyDeadlineMs - 1;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaVerdict::Wait),
                        static_cast<int>(ota_verdict(OtaImageState::PendingVerify, h)));

  h.uptime_ms = kOtaVerifyDeadlineMs;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaVerdict::RollBack),
                        static_cast<int>(ota_verdict(OtaImageState::PendingVerify, h)));
}

// Tasks that failed to start are unhealthy even with a broker.
void test_an_image_whose_tasks_did_not_start_rolls_back() {
  OtaHealth h = healthy_at(kOtaVerifyDeadlineMs);
  h.tasks_started = false;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaVerdict::RollBack),
                        static_cast<int>(ota_verdict(OtaImageState::PendingVerify, h)));
}

// Healthy on the very tick the deadline passes: kept, not thrown away on a
// technicality.
void test_healthy_at_the_deadline_is_kept() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaVerdict::MarkValid),
                        static_cast<int>(ota_verdict(OtaImageState::PendingVerify,
                                                     healthy_at(kOtaVerifyDeadlineMs))));
}

// The deadline leaves room for a slow AP plus the broker's 30 s capped backoff, so
// a good image on a sluggish network is not rolled back.
void test_the_deadline_outlasts_a_slow_reconnect() {
  TEST_ASSERT_TRUE(kOtaVerifyDeadlineMs - kOtaMinUptimeMs >= 60000u);
}

// R-5.3d: an upload is not accepted while a LoRa transaction is outstanding, nor
// without a network to receive it over.
void test_ota_waits_for_lora_idle_and_wifi() {
  TEST_ASSERT_TRUE(ota_may_start(true, true));
  TEST_ASSERT_FALSE(ota_may_start(true, false));
  TEST_ASSERT_FALSE(ota_may_start(false, true));
}

// The names are what the version topic and V-B9's procedure read.
void test_state_and_verdict_names_are_stable_tokens() {
  TEST_ASSERT_EQUAL_STRING("pending_verify", ota_state_name(OtaImageState::PendingVerify));
  TEST_ASSERT_EQUAL_STRING("not_pending", ota_state_name(OtaImageState::NotPending));
  TEST_ASSERT_EQUAL_STRING("roll_back", ota_verdict_name(OtaVerdict::RollBack));
  TEST_ASSERT_EQUAL_STRING("mark_valid", ota_verdict_name(OtaVerdict::MarkValid));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_an_image_not_pending_is_never_rolled_back);
  RUN_TEST(test_healthy_but_young_waits);
  RUN_TEST(test_healthy_and_old_enough_is_kept);
  RUN_TEST(test_an_image_that_never_reaches_the_broker_rolls_back);
  RUN_TEST(test_an_image_whose_tasks_did_not_start_rolls_back);
  RUN_TEST(test_healthy_at_the_deadline_is_kept);
  RUN_TEST(test_the_deadline_outlasts_a_slow_reconnect);
  RUN_TEST(test_ota_waits_for_lora_idle_and_wifi);
  RUN_TEST(test_state_and_verdict_names_are_stable_tokens);
  return UNITY_END();
}
