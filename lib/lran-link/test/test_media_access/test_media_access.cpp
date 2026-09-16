// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Spec 12.3 media access. Written for the bridge by BF-16 in firmware/bridge's test_lora,
// and moved here unchanged with the code on 2026-09-14.

#include <unity.h>

#include "lran/counters.h"
#include "lran/link/media_access.h"

using namespace lran;
using namespace lran::link;

void setUp() {}
void tearDown() {}

void test_a_free_channel_transmits_at_once() {
  Counters    c;
  MediaAccess ma;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Idle), static_cast<int>(ma.step(0)));

  ma.start(1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Cad), static_cast<int>(ma.step(1000)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Transmit),
                        static_cast<int>(ma.on_cad(CadResult::Free, 1000, 0, &c)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Transmit), static_cast<int>(ma.step(1001)));
  TEST_ASSERT_FALSE(ma.forced());
  TEST_ASSERT_EQUAL_UINT32(0, c.cad_backoffs);

  ma.finish();
  TEST_ASSERT_FALSE(ma.pending());
}

// A busy channel backs off inside [0, backoff_max_ms), counts, and asks for a fresh CAD
// when the backoff has run.
void test_a_busy_channel_backs_off_and_counts() {
  Counters    c;
  MediaAccess ma;
  ma.start(1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Wait),
                        static_cast<int>(ma.on_cad(CadResult::Busy, 1000, 700, &c)));
  TEST_ASSERT_EQUAL_UINT32(1, c.cad_backoffs);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Wait), static_cast<int>(ma.step(1699)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Cad), static_cast<int>(ma.step(1700)));

  // The window is taken modulo backoff_max_ms, so no random value escapes it.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Wait),
                        static_cast<int>(ma.on_cad(CadResult::Busy, 2000, 1500 + 200, &c)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Wait), static_cast<int>(ma.step(2199)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Cad), static_cast<int>(ma.step(2200)));
}

// spec 12.3 - "then transmit regardless". Five backoffs at the default, and the sixth
// busy CAD transmits; the forced transmission is not a backoff and is not counted as one.
void test_retries_exhaust_then_transmit_regardless() {
  Counters    c;
  MediaAccess ma;
  ma.start(0);
  uint32_t now = 0;
  for (int i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Wait),
                          static_cast<int>(ma.on_cad(CadResult::Busy, now, 10, &c)));
    now += 10;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Cad), static_cast<int>(ma.step(now)));
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Transmit),
                        static_cast<int>(ma.on_cad(CadResult::Busy, now, 10, &c)));
  TEST_ASSERT_TRUE(ma.forced());
  TEST_ASSERT_EQUAL_UINT8(5, ma.backoffs());
  TEST_ASSERT_EQUAL_UINT32(5, c.cad_backoffs);
}

// A CAD that fails outright delays like a busy one and stays bounded, but is not
// reported as congestion.
void test_a_failed_cad_is_bounded_and_not_counted_as_busy() {
  Counters    c;
  MediaAccess ma;
  ma.start(0);
  for (int i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Wait),
                          static_cast<int>(ma.on_cad(CadResult::Error, 0, 0, &c)));
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Transmit),
                        static_cast<int>(ma.on_cad(CadResult::Error, 0, 0, &c)));
  TEST_ASSERT_EQUAL_UINT32(0, c.cad_backoffs);
  TEST_ASSERT_EQUAL_UINT32(5, ma.cad_errors());
}

// Root rule 8 - both numbers are runtime-configurable.
void test_media_access_config_is_runtime_settable() {
  Counters          c;
  MediaAccess       ma;
  MediaAccessConfig cfg;
  cfg.cad_retries    = 0;
  cfg.backoff_max_ms = 0;
  ma.set_config(cfg);

  ma.start(0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Transmit),
                        static_cast<int>(ma.on_cad(CadResult::Busy, 0, 999, &c)));
  TEST_ASSERT_TRUE(ma.forced());

  cfg.cad_retries = 1;
  ma.set_config(cfg);
  ma.start(0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Wait),
                        static_cast<int>(ma.on_cad(CadResult::Busy, 0, 999, &c)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Cad), static_cast<int>(ma.step(0)));
}

// A millis() wrap in the middle of a backoff neither releases the frame early nor holds
// it for 49 days.
void test_a_backoff_survives_a_millis_wrap() {
  Counters       c;
  MediaAccess    ma;
  const uint32_t t0 = 0xFFFFFF00u;
  ma.start(t0);
  ma.on_cad(CadResult::Busy, t0, 500, &c);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Wait),
                        static_cast<int>(ma.step(static_cast<uint32_t>(t0 + 499))));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Cad),
                        static_cast<int>(ma.step(static_cast<uint32_t>(t0 + 500))));
}

// Each frame gets its own retries.
void test_a_new_frame_starts_with_fresh_retries() {
  Counters    c;
  MediaAccess ma;
  ma.start(0);
  ma.on_cad(CadResult::Busy, 0, 0, &c);
  ma.on_cad(CadResult::Busy, 0, 0, &c);
  ma.finish();

  ma.start(100);
  TEST_ASSERT_EQUAL_UINT8(0, ma.backoffs());
  TEST_ASSERT_FALSE(ma.forced());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TxStep::Idle),
                        static_cast<int>([&] {
                          MediaAccess idle;
                          return idle.on_cad(CadResult::Busy, 0, 0, &c);
                        }()));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_free_channel_transmits_at_once);
  RUN_TEST(test_a_busy_channel_backs_off_and_counts);
  RUN_TEST(test_retries_exhaust_then_transmit_regardless);
  RUN_TEST(test_a_failed_cad_is_bounded_and_not_counted_as_busy);
  RUN_TEST(test_media_access_config_is_runtime_settable);
  RUN_TEST(test_a_backoff_survives_a_millis_wrap);
  RUN_TEST(test_a_new_frame_starts_with_fresh_retries);
  return UNITY_END();
}
