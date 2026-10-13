// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL3 - input injection scripts (PRD R-5.4b; GateLink Impl Plan 6.6).

#include <unity.h>

#include <cstdio>
#include <cstring>

#include "gate_state.h"
#include "inject.h"

using namespace gatelink;

void setUp() {}
void tearDown() {}

void test_first_step_applies_at_once_then_spacing() {
  InjectScript s;
  const char*  bad = nullptr;
  TEST_ASSERT_TRUE(s.parse("+E +S", &bad));
  uint8_t mask = 0, value = 0;
  s.start(1000);
  TEST_ASSERT_TRUE(s.service(1000, 500, &mask, &value));
  TEST_ASSERT_EQUAL_HEX8(kInExit, mask);
  TEST_ASSERT_EQUAL_HEX8(kInExit, value);
  TEST_ASSERT_TRUE(s.service(1499, 500, &mask, &value));
  TEST_ASSERT_FALSE(s.service(1500, 500, &mask, &value));
  TEST_ASSERT_EQUAL_HEX8(kInExit | kInSafety, value);
}

void test_explicit_wait_and_release() {
  InjectScript s;
  const char*  bad = nullptr;
  TEST_ASSERT_TRUE(s.parse(inject_canned("exit"), &bad));
  uint8_t mask = 0, value = 0;
  s.start(0);
  s.service(0, 1000, &mask, &value);
  s.service(29999, 1000, &mask, &value);
  TEST_ASSERT_EQUAL_HEX8(kInExit, value);
  s.service(30000, 1000, &mask, &value);
  TEST_ASSERT_EQUAL_HEX8(kInExit | kInSafety, value);
  TEST_ASSERT_FALSE(s.service(36000, 1000, &mask, &value));
  TEST_ASSERT_EQUAL_HEX8(0, value);
  TEST_ASSERT_EQUAL_HEX8(kInExit | kInSafety, mask);  // still injected, released
}

void test_comma_joins_one_step() {
  InjectScript s;
  const char*  bad = nullptr;
  TEST_ASSERT_TRUE(s.parse("+O,+M @100 +O,-M", &bad));
  TEST_ASSERT_EQUAL(2, s.steps());
  uint8_t mask = 0, value = 0;
  s.start(0);
  s.service(0, 1000, &mask, &value);
  TEST_ASSERT_EQUAL_HEX8(kInOpen | kInMoving, value);
  s.service(100, 1000, &mask, &value);
  TEST_ASSERT_EQUAL_HEX8(kInOpen, value);
}

// A late service applies every step due, in order.
void test_late_service_catches_up() {
  InjectScript s;
  const char*  bad = nullptr;
  TEST_ASSERT_TRUE(s.parse("+S @100 -S @100 +E", &bad));
  uint8_t mask = 0, value = 0;
  s.start(0);
  TEST_ASSERT_FALSE(s.service(5000, 1000, &mask, &value));
  TEST_ASSERT_EQUAL_HEX8(kInExit, value);
}

void test_bad_tokens_are_refused() {
  InjectScript s;
  const char*  bad = nullptr;
  TEST_ASSERT_FALSE(s.parse("+S +X", &bad));
  TEST_ASSERT_EQUAL_STRING("+X", bad);
  TEST_ASSERT_FALSE(s.parse("@abc +S", &bad));
  TEST_ASSERT_FALSE(s.parse("+S,", &bad));
  TEST_ASSERT_FALSE(s.parse("", &bad));
  TEST_ASSERT_EQUAL(0, s.steps());
}

void test_every_canned_script_parses() {
  char names[96];
  std::snprintf(names, sizeof(names), "%s", inject_canned_names());
  for (char* n = std::strtok(names, " "); n != nullptr; n = std::strtok(nullptr, " ")) {
    InjectScript s;
    const char*  bad    = nullptr;
    const char*  script = inject_canned(n);
    TEST_ASSERT_NOT_NULL_MESSAGE(script, n);
    TEST_ASSERT_TRUE_MESSAGE(s.parse(script, &bad), n);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_step_applies_at_once_then_spacing);
  RUN_TEST(test_explicit_wait_and_release);
  RUN_TEST(test_comma_joins_one_step);
  RUN_TEST(test_late_service_catches_up);
  RUN_TEST(test_bad_tokens_are_refused);
  RUN_TEST(test_every_canned_script_parses);
  return UNITY_END();
}
