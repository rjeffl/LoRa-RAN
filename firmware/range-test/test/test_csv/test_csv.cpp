// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Host tests for the R6/R7 CSV schema.

#include <unity.h>

#include <cstring>

#include "csv.h"

using namespace rangetest;

void setUp() {}
void tearDown() {}

namespace {

size_t count_fields(const char* s) {
  size_t n = 1;
  for (const char* p = s; *p != '\0'; ++p) {
    if (*p == ',') ++n;
  }
  return n;
}

CsvRow sample() {
  CsvRow r{};
  r.position_id = 3;
  r.tp_index    = 11;

  r.tp.freq_hz                  = 915000000UL;
  r.tp.sf                       = 9;
  r.tp.cr_denom                 = 5;
  r.tp.payload_len              = 64;
  r.tp.power.conducted_dbm      = -3;
  r.tp.power.antenna_gain_dbi10 = 20;

  r.stats.probes_sent     = 8;
  r.stats.echoes_received = 7;
  r.resp_probes_heard     = 8;
  r.stats.init_rssi_dbm10.add(-450);
  r.stats.init_rssi_dbm10.add(-460);
  r.stats.init_snr_db10.add(112);
  r.stats.resp_rssi_dbm10.add(-461);
  r.stats.resp_snr_db10.add(110);
  return r;
}

}  // namespace

// THE TEST THIS FILE EXISTS FOR. A header that stops matching its columns produces a
// file that parses, plots, and misattributes every value - and nothing downstream can
// detect it. Adding a column to one and not the other fails here.
static void test_header_and_row_have_the_same_field_count() {
  char h[kCsvMaxLine];
  char r[kCsvMaxLine];
  TEST_ASSERT_GREATER_THAN_size_t(0, csv_header(h, sizeof(h)));
  TEST_ASSERT_GREATER_THAN_size_t(0, csv_row(sample(), r, sizeof(r)));

  TEST_ASSERT_EQUAL_size_t(count_fields(h), count_fields(r));
  TEST_ASSERT_EQUAL_size_t(csv_field_count(), count_fields(r));
}

// Empty and full rows must have the same shape, or a partially-filled test point
// shifts every column after it.
static void test_an_empty_row_has_the_same_field_count() {
  char r[kCsvMaxLine];
  CsvRow empty{};
  TEST_ASSERT_GREATER_THAN_size_t(0, csv_row(empty, r, sizeof(r)));
  TEST_ASSERT_EQUAL_size_t(csv_field_count(), count_fields(r));
}

// D33 standing condition 1: the ceiling is EIRP, so conducted power and antenna gain
// are separate columns and a reader can recompute the EIRP and audit it.
static void test_conducted_power_and_antenna_gain_are_separate_columns() {
  char h[kCsvMaxLine];
  csv_header(h, sizeof(h));
  TEST_ASSERT_NOT_NULL(std::strstr(h, "conducted_dbm"));
  TEST_ASSERT_NOT_NULL(std::strstr(h, "antenna_gain_dbi10"));

  char r[kCsvMaxLine];
  csv_row(sample(), r, sizeof(r));
  TEST_ASSERT_NOT_NULL(std::strstr(r, ",-3,20,"));   // adjacent, in that order
}

// R6 asks for mean/min/max of RSSI and SNR. Both directions, all three statistics.
static void test_all_four_series_report_mean_min_and_max() {
  char h[kCsvMaxLine];
  csv_header(h, sizeof(h));
  const char* want[] = {
      "init_rssi_mean10", "init_rssi_min10", "init_rssi_max10",
      "init_snr_mean10",  "init_snr_min10",  "init_snr_max10",
      "resp_rssi_mean10", "resp_rssi_min10", "resp_rssi_max10",
      "resp_snr_mean10",  "resp_snr_min10",  "resp_snr_max10",
  };
  for (const char* w : want) TEST_ASSERT_NOT_NULL(std::strstr(h, w));
}

// An empty series has no min or max. Printing its zero-initialized ones would invent
// a -0.0 dBm reading, which is not a plausible RSSI and would be believed anyway.
static void test_empty_series_emits_sentinels_not_zeros() {
  CsvRow r{};
  r.stats.probes_sent = 8;   // sent, nothing came back
  char buf[kCsvMaxLine];
  TEST_ASSERT_GREATER_THAN_size_t(0, csv_row(r, buf, sizeof(buf)));

  // -32768 three times over for init_rssi mean/min/max.
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "-32768,-32768,-32768"));
  // And a total loss reads as 100.00%, not as 0.
  TEST_ASSERT_NOT_NULL(std::strstr(buf, ",10000,"));
}

// The disambiguation R4 gives up. When no echo ever arrives the responder's count is
// unknown, and "unknown" must not read as "heard nothing" - the difference is exactly
// whether the downlink or the uplink failed.
static void test_unknown_responder_count_is_a_sentinel_not_zero() {
  CsvRow r{};
  r.stats.probes_sent = 8;
  TEST_ASSERT_EQUAL_UINT16(kU16NotAvailable, r.resp_probes_heard);

  char buf[kCsvMaxLine];
  csv_row(r, buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "65535"));
}

// A truncated CSV row still parses and is wrong. Emit nothing instead.
static void test_row_refuses_to_truncate() {
  char small[24];
  TEST_ASSERT_EQUAL_size_t(0, csv_row(sample(), small, sizeof(small)));
  TEST_ASSERT_EQUAL_size_t(0, csv_header(small, sizeof(small)));
}

// The header is the long line, not the row. kCsvMaxLine is sized for it, and this is
// what stops a future column silently pushing it over.
static void test_header_fits_the_advertised_buffer() {
  char buf[kCsvMaxLine];
  const size_t n = csv_header(buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN_size_t(0, n);
  TEST_ASSERT_LESS_THAN_size_t(kCsvMaxLine, n);

  // And it really is the longer of the two, so sizing for it is sufficient.
  char row[kCsvMaxLine];
  TEST_ASSERT_GREATER_THAN_size_t(csv_row(sample(), row, sizeof(row)), n);
}

static void test_row_fits_the_advertised_buffer() {
  CsvRow r = sample();
  // Widest plausible values in every column.
  r.position_id = 65535;
  r.tp_index    = 65535;
  r.tp.freq_hz  = 928000000UL;
  r.stats.probes_sent = 65535;
  r.stats.echoes_received = 65535;
  r.stats.phy_crc_errors = 65535;
  r.stats.foreign_frames = 65535;
  r.stats.filler_mismatch = 65535;
  r.resp_probes_heard = 65535;

  char buf[kCsvMaxLine];
  const size_t n = csv_row(r, buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN_size_t(0, n);
  TEST_ASSERT_LESS_THAN_size_t(kCsvMaxLine, n);
}

static void test_row_begins_with_position_then_test_point() {
  char buf[kCsvMaxLine];
  csv_row(sample(), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_INT(0, std::strncmp(buf, "3,11,915000000,9,5,-3,20,64,", 28));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_header_and_row_have_the_same_field_count);
  RUN_TEST(test_an_empty_row_has_the_same_field_count);
  RUN_TEST(test_conducted_power_and_antenna_gain_are_separate_columns);
  RUN_TEST(test_all_four_series_report_mean_min_and_max);
  RUN_TEST(test_empty_series_emits_sentinels_not_zeros);
  RUN_TEST(test_unknown_responder_count_is_a_sentinel_not_zero);
  RUN_TEST(test_row_refuses_to_truncate);
  RUN_TEST(test_header_fits_the_advertised_buffer);
  RUN_TEST(test_row_fits_the_advertised_buffer);
  RUN_TEST(test_row_begins_with_position_then_test_point);
  return UNITY_END();
}
