// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The listen-only receiver's frequency rules and its console grammar. capture_config.h has
// the reasoning.
//
// WHAT THIS CANNOT COVER. That NVS keeps the value across a reset, that the radio tunes to
// what CHAN-BOOT says, or that the image never transmits. The first two need a board;
// tools/checks/chan_capture_never_transmits.py is the tripwire for the third.

#include <unity.h>

#include <cstring>

#include "capture_config.h"
#include "lran/link/radio_config.h"

using namespace chancap;

void setUp() {}
void tearDown() {}

void test_the_band_edges_are_half_a_channel_inside_902_and_928() {
  TEST_ASSERT_TRUE(freq_in_band(902062500u));
  TEST_ASSERT_FALSE(freq_in_band(902062499u));
  TEST_ASSERT_TRUE(freq_in_band(927937500u));
  TEST_ASSERT_FALSE(freq_in_band(927937501u));
}

void test_every_candidate_in_the_brief_is_in_the_band() {
  TEST_ASSERT_TRUE(freq_in_band(917200000u));
  TEST_ASSERT_TRUE(freq_in_band(917400000u));
  TEST_ASSERT_TRUE(freq_in_band(917600000u));
  TEST_ASSERT_TRUE(freq_in_band(lran::link::kPhy.freq_hz));
}

void test_nothing_stored_uses_the_default() {
  const FreqChoice c = choose_freq(false, 0, 917400000u);
  TEST_ASSERT_EQUAL_UINT32(917400000u, c.hz);
  TEST_ASSERT_EQUAL(FreqSource::Default, c.source);
}

void test_a_stored_value_in_the_band_is_used() {
  const FreqChoice c = choose_freq(true, 917600000u, 917400000u);
  TEST_ASSERT_EQUAL_UINT32(917600000u, c.hz);
  TEST_ASSERT_EQUAL(FreqSource::Stored, c.source);
}

// NVS that held something else, or a value written by a build with other limits, must not
// tune the receiver outside the band - and must say that it fell back, not pass as default.
void test_a_stored_value_outside_the_band_falls_back_and_says_so() {
  const FreqChoice c = choose_freq(true, 868100000u, 917400000u);
  TEST_ASSERT_EQUAL_UINT32(917400000u, c.hz);
  TEST_ASSERT_EQUAL(FreqSource::Rejected, c.source);
  TEST_ASSERT_NOT_EQUAL(0, strcmp(freq_source_name(FreqSource::Rejected),
                                  freq_source_name(FreqSource::Default)));
}

void test_freq_alone_shows_the_frequency() {
  TEST_ASSERT_EQUAL(CommandKind::ShowFreq, parse_command("freq").kind);
  TEST_ASSERT_EQUAL(CommandKind::ShowFreq, parse_command("  freq \r\n").kind);
}

void test_freq_with_hertz_sets_it() {
  const Command c = parse_command("freq 917200000\r\n");
  TEST_ASSERT_EQUAL(CommandKind::SetFreq, c.kind);
  TEST_ASSERT_EQUAL_UINT32(917200000u, c.hz);
}

// Each of these is a plausible typo that a lenient parser would turn into a frequency.
void test_freq_refuses_anything_but_whole_hertz_in_the_band() {
  TEST_ASSERT_EQUAL(CommandKind::BadFreq, parse_command("freq 917.2").kind);
  TEST_ASSERT_EQUAL(CommandKind::BadFreq, parse_command("freq 917200000Hz").kind);
  TEST_ASSERT_EQUAL(CommandKind::BadFreq, parse_command("freq -917200000").kind);
  TEST_ASSERT_EQUAL(CommandKind::BadFreq, parse_command("freq 9172000000").kind);
  TEST_ASSERT_EQUAL(CommandKind::BadFreq, parse_command("freq 99999999999").kind);
  TEST_ASSERT_EQUAL(CommandKind::BadFreq, parse_command("freq 917200").kind);
  TEST_ASSERT_EQUAL(CommandKind::BadFreq, parse_command("freq 917200000 917400000").kind);
}

void test_radio_and_restart_take_no_argument() {
  TEST_ASSERT_EQUAL(CommandKind::Radio, parse_command("radio\r\n").kind);
  TEST_ASSERT_EQUAL(CommandKind::Restart, parse_command(" restart ").kind);
  TEST_ASSERT_EQUAL(CommandKind::Unknown, parse_command("radio now").kind);
  TEST_ASSERT_EQUAL(CommandKind::Unknown, parse_command("restart 1").kind);
  TEST_ASSERT_EQUAL(CommandKind::Unknown, parse_command("radios").kind);
}

void test_blank_help_and_unknown_lines() {
  TEST_ASSERT_EQUAL(CommandKind::Empty, parse_command("").kind);
  TEST_ASSERT_EQUAL(CommandKind::Empty, parse_command(" \r\n").kind);
  TEST_ASSERT_EQUAL(CommandKind::Empty, parse_command(nullptr).kind);
  TEST_ASSERT_EQUAL(CommandKind::Help, parse_command("help").kind);
  TEST_ASSERT_EQUAL(CommandKind::Unknown, parse_command("frequency 917200000").kind);
  TEST_ASSERT_EQUAL(CommandKind::Unknown, parse_command("tx").kind);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_band_edges_are_half_a_channel_inside_902_and_928);
  RUN_TEST(test_every_candidate_in_the_brief_is_in_the_band);
  RUN_TEST(test_nothing_stored_uses_the_default);
  RUN_TEST(test_a_stored_value_in_the_band_is_used);
  RUN_TEST(test_a_stored_value_outside_the_band_falls_back_and_says_so);
  RUN_TEST(test_freq_alone_shows_the_frequency);
  RUN_TEST(test_freq_with_hertz_sets_it);
  RUN_TEST(test_freq_refuses_anything_but_whole_hertz_in_the_band);
  RUN_TEST(test_radio_and_restart_take_no_argument);
  RUN_TEST(test_blank_help_and_unknown_lines);
  return UNITY_END();
}
