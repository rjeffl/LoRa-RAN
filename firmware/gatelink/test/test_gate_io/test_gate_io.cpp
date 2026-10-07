// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL1 - relay pulse timing and input debounce; GL3 - the command sequencer (GateLink Impl
// Plan 4.4, 5.2, 6.1).

#include <unity.h>

#include "gate_io.h"

using namespace gatelink;

void setUp() {}
void tearDown() {}

void test_pulse_holds_its_width_and_then_ends() {
  RelayPulser p;
  TEST_ASSERT_EQUAL(PulseResult::Started, p.start(2, 500, 500, 1000));
  TEST_ASSERT_EQUAL_HEX8(0x04, p.mask());
  TEST_ASSERT_EQUAL_UINT32(500, p.ms_to_edge(1000));
  TEST_ASSERT_FALSE(p.update(1499));
  TEST_ASSERT_EQUAL_HEX8(0x04, p.mask());
  TEST_ASSERT_TRUE(p.update(1500));
  TEST_ASSERT_EQUAL_HEX8(0x00, p.mask());
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, p.ms_to_edge(1500));
}

// A late wake ends the pulse at once; ms_to_edge never goes negative.
void test_late_wake_ends_at_once() {
  RelayPulser p;
  p.start(0, 500, 500, 0);
  TEST_ASSERT_EQUAL_UINT32(0, p.ms_to_edge(520));
  TEST_ASSERT_TRUE(p.update(520));
}

void test_second_pulse_is_refused_while_one_runs() {
  RelayPulser p;
  p.start(0, 500, 500, 0);
  TEST_ASSERT_EQUAL(PulseResult::Busy, p.start(1, 500, 500, 100));
  TEST_ASSERT_EQUAL_HEX8(0x01, p.mask());
}

// R-3.1.2b - the gap runs from the scheduled trailing edge, whichever relay comes next.
void test_spacing_runs_from_the_trailing_edge() {
  RelayPulser p;
  p.start(0, 500, 500, 0);
  p.update(530);  // woken 30 ms late
  TEST_ASSERT_EQUAL(PulseResult::TooSoon, p.start(3, 500, 500, 999));
  TEST_ASSERT_EQUAL(PulseResult::Started, p.start(3, 500, 500, 1000));
  TEST_ASSERT_EQUAL_HEX8(0x08, p.mask());
}

void test_bad_relay_and_width_are_refused() {
  RelayPulser p;
  TEST_ASSERT_EQUAL(PulseResult::BadRelay, p.start(4, 500, 500, 0));
  TEST_ASSERT_EQUAL(PulseResult::BadWidth, p.start(0, 0, 500, 0));
  TEST_ASSERT_EQUAL_HEX8(0x00, p.mask());
}

// The millisecond counter wraps at 49.7 days; a pulse across the wrap keeps its width.
void test_pulse_across_counter_wrap() {
  RelayPulser p;
  const uint32_t t0 = UINT32_MAX - 99;
  p.start(1, 500, 500, t0);
  TEST_ASSERT_EQUAL_UINT32(500, p.ms_to_edge(t0));
  TEST_ASSERT_FALSE(p.update(t0 + 499));
  TEST_ASSERT_TRUE(p.update(t0 + 500));
}

void test_first_read_is_stable() {
  Debouncer d;
  TEST_ASSERT_EQUAL_HEX8(0x05, d.update(0x05, 2));
}

void test_change_needs_consecutive_agreeing_reads() {
  Debouncer d;
  d.update(0x00, 2);
  TEST_ASSERT_EQUAL_HEX8(0x00, d.update(0x01, 2));
  TEST_ASSERT_EQUAL_HEX8(0x01, d.update(0x01, 2));
}

// A one-read glitch is rejected, and the count starts again after it.
void test_glitch_resets_the_count() {
  Debouncer d;
  d.update(0x00, 3);
  d.update(0x02, 3);
  d.update(0x02, 3);
  d.update(0x00, 3);  // glitch back
  d.update(0x02, 3);
  TEST_ASSERT_EQUAL_HEX8(0x00, d.update(0x02, 3));
  TEST_ASSERT_EQUAL_HEX8(0x02, d.update(0x02, 3));
}

// Bits debounce independently.
void test_bits_are_independent() {
  Debouncer d;
  d.update(0x00, 2);
  d.update(0x01, 2);
  d.update(0x81, 2);  // bit 0 settles, bit 7 has one read
  TEST_ASSERT_EQUAL_HEX8(0x01, d.stable());
  TEST_ASSERT_EQUAL_HEX8(0x81, d.update(0x81, 2));
}

void test_samples_of_one_follow_every_read() {
  Debouncer d;
  d.update(0x00, 1);
  TEST_ASSERT_EQUAL_HEX8(0x10, d.update(0x10, 1));
}

// GL3 - a one-pulse command ends at its trailing edge, not at its start (Impl Plan 5.2).
void test_sequence_of_one_ends_at_the_trailing_edge() {
  RelayPulser      p;
  CommandSequencer q;
  SequenceEnd      end;
  TEST_ASSERT_TRUE(q.begin({2, kNoRelay}, 0));
  TEST_ASSERT_TRUE(q.service(p, 500, 500, 500, 0));
  TEST_ASSERT_EQUAL_HEX8(0x04, p.mask());
  TEST_ASSERT_FALSE(q.take_end(&end));
  p.update(500);
  q.service(p, 500, 500, 500, 500);
  TEST_ASSERT_TRUE(q.take_end(&end));
  TEST_ASSERT_EQUAL(SequenceEnd::Done, end);
  TEST_ASSERT_FALSE(q.running());
  TEST_ASSERT_FALSE(q.take_end(&end));  // once
}

// PRD R-3.1.2c - K2, unlock_settle_ms from its trailing edge, then K4.
void test_immediate_close_waits_the_settle() {
  RelayPulser      p;
  CommandSequencer q;
  SequenceEnd      end;
  q.begin({1, 3}, 0);
  q.service(p, 500, 100, 700, 0);
  TEST_ASSERT_EQUAL_HEX8(0x02, p.mask());
  p.update(500);
  TEST_ASSERT_FALSE(q.service(p, 500, 100, 700, 500));
  TEST_ASSERT_FALSE(q.service(p, 500, 100, 700, 1199));
  TEST_ASSERT_TRUE(q.service(p, 500, 100, 700, 1200));
  TEST_ASSERT_EQUAL_HEX8(0x08, p.mask());
  TEST_ASSERT_FALSE(q.take_end(&end));
  p.update(1700);
  q.service(p, 500, 100, 700, 1700);
  TEST_ASSERT_TRUE(q.take_end(&end));
  TEST_ASSERT_EQUAL(SequenceEnd::Done, end);
}

// R-3.1.2b - spacing delays a command; it never refuses one.
void test_spacing_delays_a_command() {
  RelayPulser      p;
  CommandSequencer q;
  p.start(0, 500, 500, 0);  // a bench pulse
  p.update(500);
  q.begin({2, kNoRelay}, 600);
  TEST_ASSERT_FALSE(q.service(p, 500, 500, 500, 600));
  TEST_ASSERT_TRUE(q.running());
  TEST_ASSERT_TRUE(q.service(p, 500, 500, 500, 1000));
}

void test_a_pulse_in_progress_delays_a_command() {
  RelayPulser      p;
  CommandSequencer q;
  p.start(0, 500, 0, 0);
  q.begin({2, kNoRelay}, 100);
  TEST_ASSERT_FALSE(q.service(p, 500, 0, 500, 100));
  p.update(500);
  TEST_ASSERT_TRUE(q.service(p, 500, 0, 500, 500));
  TEST_ASSERT_EQUAL_HEX8(0x04, p.mask());
}

void test_bad_step_is_refused_and_ends() {
  RelayPulser      p;
  CommandSequencer q;
  SequenceEnd      end;
  q.begin({7, kNoRelay}, 0);
  TEST_ASSERT_FALSE(q.service(p, 500, 500, 500, 0));
  TEST_ASSERT_TRUE(q.take_end(&end));
  TEST_ASSERT_EQUAL(SequenceEnd::Refused, end);
  TEST_ASSERT_EQUAL_HEX8(0x00, p.mask());
}

void test_second_sequence_is_refused_while_one_runs() {
  CommandSequencer q;
  TEST_ASSERT_TRUE(q.begin({0, kNoRelay}, 0));
  TEST_ASSERT_FALSE(q.begin({1, kNoRelay}, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pulse_holds_its_width_and_then_ends);
  RUN_TEST(test_late_wake_ends_at_once);
  RUN_TEST(test_second_pulse_is_refused_while_one_runs);
  RUN_TEST(test_spacing_runs_from_the_trailing_edge);
  RUN_TEST(test_bad_relay_and_width_are_refused);
  RUN_TEST(test_pulse_across_counter_wrap);
  RUN_TEST(test_first_read_is_stable);
  RUN_TEST(test_change_needs_consecutive_agreeing_reads);
  RUN_TEST(test_glitch_resets_the_count);
  RUN_TEST(test_bits_are_independent);
  RUN_TEST(test_samples_of_one_follow_every_read);
  RUN_TEST(test_sequence_of_one_ends_at_the_trailing_edge);
  RUN_TEST(test_immediate_close_waits_the_settle);
  RUN_TEST(test_spacing_delays_a_command);
  RUN_TEST(test_a_pulse_in_progress_delays_a_command);
  RUN_TEST(test_bad_step_is_refused_and_ends);
  RUN_TEST(test_second_sequence_is_refused_while_one_runs);
  return UNITY_END();
}
