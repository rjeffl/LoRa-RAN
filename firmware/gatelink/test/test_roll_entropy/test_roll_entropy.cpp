// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// RollEntropy: spec 10.1, D87.

#include <unity.h>

#include "roll_entropy.h"

using gatelink::RollEntropy;

void setUp() {}
void tearDown() {}

void test_empty_takes_zero() {
  RollEntropy e;
  TEST_ASSERT_EQUAL_UINT32(0, e.take());
}

// spec 10.1 - a word is spent once, in order.
void test_each_word_is_spent_once() {
  RollEntropy e;
  TEST_ASSERT_TRUE(e.put(11));
  TEST_ASSERT_TRUE(e.put(22));
  TEST_ASSERT_EQUAL_UINT32(11, e.take());
  TEST_ASSERT_EQUAL_UINT32(22, e.take());
  TEST_ASSERT_EQUAL_UINT32(0, e.take());
}

void test_full_keeps_what_it_holds() {
  RollEntropy e;
  for (uint32_t i = 1; i <= RollEntropy::kCap; ++i) TEST_ASSERT_TRUE(e.put(i));
  TEST_ASSERT_FALSE(e.put(99));
  TEST_ASSERT_EQUAL_UINT32(1, e.take());
  TEST_ASSERT_EQUAL_size_t(RollEntropy::kCap - 1, e.size());
}

void test_zero_is_not_a_word() {
  RollEntropy e;
  TEST_ASSERT_FALSE(e.put(0));
  TEST_ASSERT_EQUAL_size_t(0, e.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_takes_zero);
  RUN_TEST(test_each_word_is_spent_once);
  RUN_TEST(test_full_keeps_what_it_holds);
  RUN_TEST(test_zero_is_not_a_word);
  return UNITY_END();
}
