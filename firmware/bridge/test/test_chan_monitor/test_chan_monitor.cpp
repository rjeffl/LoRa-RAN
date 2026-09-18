// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// M25 - the channel monitor's accumulation and its serial format. chan_monitor.h has the
// reasoning.
//
// WHAT THIS CANNOT COVER. That GET_RSSI_INST answers for the channel rather than for the
// last packet, that lora_task wakes as often as kLoraMaxWaitMs says, or that the skip
// condition really excludes our own receptions. Those need a radio. What is under test is
// the arithmetic a twelve-hour capture is summarised by - and a bucket that reports a
// clean floor because it never sampled is the failure worth catching at a desk.

#include <unity.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "chan_monitor.h"

using namespace bridge;

void setUp() {}
void tearDown() {}

void test_a_fresh_monitor_has_nothing_to_take() {
  ChanMonitor m;
  ChanBucket  b;
  TEST_ASSERT_FALSE(m.take(&b));
  TEST_ASSERT_EQUAL_UINT32(0, m.lost());
}

void test_a_bucket_closes_only_after_the_nominal_length() {
  ChanMonitor m;
  ChanBucket  b;
  for (uint32_t t = 0; t < kChanBucketMs; t += 10) m.sample(-1140, t);
  TEST_ASSERT_FALSE(m.take(&b));  // still open

  m.sample(-1140, kChanBucketMs);
  TEST_ASSERT_TRUE(m.take(&b));
  TEST_ASSERT_EQUAL_UINT32(0, b.seq);
  TEST_ASSERT_EQUAL_UINT32(kChanBucketMs, b.dur_ms);
}

void test_peak_floor_and_mean_describe_the_samples() {
  ChanMonitor m;
  m.sample(-1160, 0);
  m.sample(-1000, 10);   // an excursion
  m.sample(-1160, 20);
  m.sample(-1160, kChanBucketMs);

  ChanBucket b;
  TEST_ASSERT_TRUE(m.take(&b));
  TEST_ASSERT_EQUAL_UINT32(3, b.samples);
  TEST_ASSERT_EQUAL_INT16(-1000, b.peak);
  TEST_ASSERT_EQUAL_INT16(-1160, b.floor);
  TEST_ASSERT_EQUAL_INT16(-1106, b.mean());  // (-1160 -1000 -1160) / 3
}

void test_the_occupied_count_uses_the_threshold() {
  ChanMonitor m;
  m.sample(kOccupiedDbm10 - 1, 0);  // below
  m.sample(kOccupiedDbm10, 10);     // at - counts
  m.sample(kOccupiedDbm10 + 1, 20); // above - counts
  m.sample(-1160, kChanBucketMs);

  ChanBucket b;
  TEST_ASSERT_TRUE(m.take(&b));
  TEST_ASSERT_EQUAL_UINT32(2, b.above);
}

void test_a_skipped_opportunity_is_counted_and_never_sampled() {
  // THE POINT: a bucket that could not look must not read as a bucket that looked and
  // found nothing. An occupancy figure whose denominator is unstated is not a figure.
  ChanMonitor m;
  for (int i = 0; i < 50; ++i) m.skip(static_cast<uint32_t>(i * 10));
  m.sample(-1160, kChanBucketMs);

  ChanBucket b;
  TEST_ASSERT_TRUE(m.take(&b));
  TEST_ASSERT_EQUAL_UINT32(50, b.skipped);
  TEST_ASSERT_EQUAL_UINT32(0, b.samples);
  TEST_ASSERT_FALSE(b.has_reading());
  TEST_ASSERT_EQUAL_INT16(kNoReading, b.peak);
  TEST_ASSERT_EQUAL_INT16(kNoReading, b.mean());
}

void test_the_rssi_rail_is_not_a_reading() {
  // Seen on the board 2026-09-17: the first buckets after boot carried -127.5 dBm, which
  // is 0xFF / -2 and not a measurement. One of them pulls a window's floor 12 dB below
  // the campaign floor.
  ChanMonitor m;
  m.sample(kRssiRailDbm10, 0);
  m.sample(-1160, 10);
  m.sample(-1160, kChanBucketMs);

  ChanBucket b;
  TEST_ASSERT_TRUE(m.take(&b));
  TEST_ASSERT_EQUAL_UINT32(1, b.samples);
  TEST_ASSERT_EQUAL_UINT32(1, b.skipped);
  TEST_ASSERT_EQUAL_INT16(-1160, b.floor);
}

void test_our_own_receptions_are_attributed_to_the_open_bucket() {
  ChanMonitor m;
  m.sample(-1160, 0);
  m.note_own_rx();
  m.note_own_rx();
  m.sample(-1160, kChanBucketMs);

  ChanBucket b;
  TEST_ASSERT_TRUE(m.take(&b));
  TEST_ASSERT_EQUAL_UINT32(2, b.own_rx);
}

void test_buckets_are_numbered_consecutively() {
  ChanMonitor m;
  for (uint32_t k = 0; k <= 3; ++k) m.sample(-1150, k * kChanBucketMs);

  ChanBucket b;
  for (uint32_t k = 0; k < 3; ++k) {
    TEST_ASSERT_TRUE(m.take(&b));
    TEST_ASSERT_EQUAL_UINT32(k, b.seq);
  }
}

void test_a_long_gap_closes_one_bucket_not_many() {
  // lora_task is not a metronome, and an OTA or a long transmit can stall it. The bucket
  // reports the duration it actually covered rather than pretending to be 1000 ms.
  ChanMonitor m;
  m.sample(-1150, 0);
  m.sample(-1150, 7 * kChanBucketMs);

  ChanBucket b;
  TEST_ASSERT_TRUE(m.take(&b));
  TEST_ASSERT_EQUAL_UINT32(7 * kChanBucketMs, b.dur_ms);
  TEST_ASSERT_FALSE(m.take(&b));
}

void test_the_bucket_clock_survives_the_millis_wrap() {
  ChanMonitor m;
  const uint32_t near_wrap = UINT32_MAX - (kChanBucketMs / 2);
  m.sample(-1150, near_wrap);
  m.sample(-1150, near_wrap + kChanBucketMs);  // wraps through zero

  ChanBucket b;
  TEST_ASSERT_TRUE(m.take(&b));
  TEST_ASSERT_EQUAL_UINT32(kChanBucketMs, b.dur_ms);
}

void test_an_overwritten_bucket_is_counted() {
  ChanMonitor m;
  for (uint32_t k = 0; k <= kChanSlots + 3; ++k) m.sample(-1150, k * kChanBucketMs);

  ChanBucket b;
  TEST_ASSERT_TRUE(m.take(&b));
  TEST_ASSERT_EQUAL_UINT32(3, m.lost());
  TEST_ASSERT_EQUAL_UINT32(3, b.seq);  // the oldest that still exists
}

void test_a_line_is_csv_and_tagged() {
  // The capture file is the deliverable and shares a port with frame_log's FRAME lines,
  // so the tag has to be there and the rest has to be machine-readable.
  ChanMonitor m;
  m.sample(-1160, 0);
  m.sample(-1000, 10);
  m.note_own_rx();
  m.sample(-1160, kChanBucketMs);

  ChanBucket b;
  TEST_ASSERT_TRUE(m.take(&b));

  char         line[160];
  const size_t n = render_chan(b, line, sizeof(line));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_size_t(n, std::strlen(line));

  const std::string s(line);
  TEST_ASSERT_EQUAL_size_t(0, s.find("CHAN,"));
  TEST_ASSERT_EQUAL_INT(10, static_cast<int>(std::count(s.begin(), s.end(), ',')));
  TEST_ASSERT_TRUE(s.find("-1000") != std::string::npos);  // the peak
}

void test_a_line_is_refused_rather_than_truncated() {
  ChanBucket b;
  char       line[12];
  TEST_ASSERT_EQUAL_size_t(0, render_chan(b, line, sizeof(line)));
  TEST_ASSERT_EQUAL_size_t(0, std::strlen(line));
}

void test_the_boot_header_says_what_produced_the_file() {
  char         line[160];
  const size_t n = render_chan_boot("abc1234", 917400000u, 9, 1250, 10, line, sizeof(line));
  TEST_ASSERT_TRUE(n > 0);

  const std::string s(line);
  TEST_ASSERT_EQUAL_size_t(0, s.find("CHAN-BOOT,"));
  TEST_ASSERT_TRUE(s.find("abc1234") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("917400000") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("1250") != std::string::npos);
}

void test_a_boot_header_without_a_git_field_still_parses() {
  char line[160];
  TEST_ASSERT_TRUE(render_chan_boot(nullptr, 917400000u, 9, 1250, 10, line, sizeof(line)) > 0);
  TEST_ASSERT_TRUE(std::string(line).find("unknown") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The rollup - what keeps the capture small without losing the denominator
// ---------------------------------------------------------------------------

ChanBucket made(uint32_t seq, uint32_t samples, uint32_t above, Dbm10 peak, Dbm10 flr) {
  ChanBucket b;
  b.seq     = seq;
  b.dur_ms  = 1000;
  b.samples = samples;
  b.above   = above;
  b.peak    = peak;
  b.floor   = flr;
  b.sum     = static_cast<int32_t>(samples) * flr;
  return b;
}

void test_only_a_bucket_that_saw_something_is_notable() {
  TEST_ASSERT_FALSE(chan_notable(made(0, 100, 0, -1150, -1160)));
  TEST_ASSERT_TRUE(chan_notable(made(1, 100, 1, -1000, -1160)));
}

void test_the_rollup_keeps_the_denominator_the_quiet_buckets_hold() {
  // THE WHOLE POINT. Occupancy is above/samples, and the samples live in the buckets
  // that are never written out individually.
  ChanRollupper r;
  for (uint32_t i = 0; i < kChanFirstRollupBuckets; ++i) {
    r.add(made(i, 100, i == 3 ? 5 : 0, i == 3 ? -1000 : -1150, -1160));
  }
  TEST_ASSERT_TRUE(r.due());

  ChanRollup out;
  TEST_ASSERT_TRUE(r.take(&out));
  TEST_ASSERT_EQUAL_UINT32(kChanFirstRollupBuckets, out.buckets);
  TEST_ASSERT_EQUAL_UINT32(100 * kChanFirstRollupBuckets, out.samples);
  TEST_ASSERT_EQUAL_UINT32(5, out.above);
  TEST_ASSERT_EQUAL_UINT32(1, out.notable);
  TEST_ASSERT_EQUAL_INT16(-1000, out.peak);
  TEST_ASSERT_EQUAL_INT16(-1160, out.floor_mean());
}

void test_the_first_rollup_is_short_so_a_capture_proves_itself_quickly() {
  ChanRollupper r;
  for (uint32_t i = 0; i < kChanFirstRollupBuckets; ++i) r.add(made(i, 100, 0, -1150, -1160));
  TEST_ASSERT_TRUE(r.due());

  ChanRollup out;
  TEST_ASSERT_TRUE(r.take(&out));

  // The second needs the full window.
  for (uint32_t i = 0; i < kChanFirstRollupBuckets; ++i) r.add(made(i, 100, 0, -1150, -1160));
  TEST_ASSERT_FALSE(r.due());
}

void test_a_blind_bucket_contributes_no_floor_and_is_counted() {
  // Letting kNoReading through would report a floor of -3276.8 dBm, which is the kind of
  // number that gets copied into a document before anyone looks at it.
  ChanRollupper r;
  ChanBucket    blind;
  blind.seq     = 0;
  blind.dur_ms  = 1000;
  blind.skipped = 100;  // samples stays 0
  r.add(blind);
  r.add(made(1, 100, 0, -1150, -1160));

  ChanRollup out;
  TEST_ASSERT_TRUE(r.take(&out));
  TEST_ASSERT_EQUAL_UINT32(2, out.buckets);
  TEST_ASSERT_EQUAL_UINT32(1, out.blind);
  TEST_ASSERT_EQUAL_UINT32(100, out.samples);
  TEST_ASSERT_EQUAL_INT16(-1160, out.floor_min);
  TEST_ASSERT_EQUAL_INT16(-1160, out.floor_mean());
}

void test_an_all_blind_window_reports_no_floor_rather_than_a_wrong_one() {
  ChanRollupper r;
  for (uint32_t i = 0; i < 3; ++i) {
    ChanBucket b;
    b.seq     = i;
    b.dur_ms  = 1000;
    b.skipped = 100;
    r.add(b);
  }
  ChanRollup out;
  TEST_ASSERT_TRUE(r.take(&out));
  TEST_ASSERT_EQUAL_UINT32(3, out.blind);
  TEST_ASSERT_EQUAL_INT16(kNoReading, out.floor_mean());
  TEST_ASSERT_EQUAL_INT16(kNoReading, out.peak);
}

void test_taking_a_rollup_resets_it() {
  ChanRollupper r;
  r.add(made(0, 100, 0, -1150, -1160));

  ChanRollup out;
  TEST_ASSERT_TRUE(r.take(&out));
  TEST_ASSERT_FALSE(r.take(&out));
}

void test_a_rollup_line_is_csv_and_tagged() {
  ChanRollupper r;
  r.add(made(7, 100, 2, -540, -1160));

  ChanRollup out;
  TEST_ASSERT_TRUE(r.take(&out));

  char         line[192];
  const size_t n = render_chan_rollup(out, line, sizeof(line));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_size_t(n, std::strlen(line));

  const std::string s(line);
  TEST_ASSERT_EQUAL_size_t(0, s.find("CHANSUM,"));
  TEST_ASSERT_EQUAL_INT(14, static_cast<int>(std::count(s.begin(), s.end(), ',')));
  TEST_ASSERT_TRUE(s.find("-540") != std::string::npos);
}

void test_a_rollup_line_is_refused_rather_than_truncated() {
  ChanRollup r;
  char       line[16];
  TEST_ASSERT_EQUAL_size_t(0, render_chan_rollup(r, line, sizeof(line)));
  TEST_ASSERT_EQUAL_size_t(0, std::strlen(line));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_fresh_monitor_has_nothing_to_take);
  RUN_TEST(test_a_bucket_closes_only_after_the_nominal_length);
  RUN_TEST(test_peak_floor_and_mean_describe_the_samples);
  RUN_TEST(test_the_occupied_count_uses_the_threshold);
  RUN_TEST(test_a_skipped_opportunity_is_counted_and_never_sampled);
  RUN_TEST(test_the_rssi_rail_is_not_a_reading);
  RUN_TEST(test_our_own_receptions_are_attributed_to_the_open_bucket);
  RUN_TEST(test_buckets_are_numbered_consecutively);
  RUN_TEST(test_a_long_gap_closes_one_bucket_not_many);
  RUN_TEST(test_the_bucket_clock_survives_the_millis_wrap);
  RUN_TEST(test_an_overwritten_bucket_is_counted);
  RUN_TEST(test_a_line_is_csv_and_tagged);
  RUN_TEST(test_a_line_is_refused_rather_than_truncated);
  RUN_TEST(test_the_boot_header_says_what_produced_the_file);
  RUN_TEST(test_a_boot_header_without_a_git_field_still_parses);

  RUN_TEST(test_only_a_bucket_that_saw_something_is_notable);
  RUN_TEST(test_the_rollup_keeps_the_denominator_the_quiet_buckets_hold);
  RUN_TEST(test_the_first_rollup_is_short_so_a_capture_proves_itself_quickly);
  RUN_TEST(test_a_blind_bucket_contributes_no_floor_and_is_counted);
  RUN_TEST(test_an_all_blind_window_reports_no_floor_rather_than_a_wrong_one);
  RUN_TEST(test_taking_a_rollup_resets_it);
  RUN_TEST(test_a_rollup_line_is_csv_and_tagged);
  RUN_TEST(test_a_rollup_line_is_refused_rather_than_truncated);
  return UNITY_END();
}
