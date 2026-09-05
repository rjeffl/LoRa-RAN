// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Host tests for the R8 / M20 ambient survey: the bin plan, the peak-hold
// statistics, the CSV schema and the NVS blob.
//
// None of this needs a radio, and all of it can be silently wrong. A survey whose bin
// plan is off by one step puts every occupant next to the wrong channel in a D1
// argument, and nothing on the board would say so.

#include <unity.h>

#include <cstring>

#include "survey.h"

using namespace rangetest;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// The bin plan - R8's 902-928 MHz in 200 kHz steps, 130 bins.
// ---------------------------------------------------------------------------

static void test_bin_plan_spans_the_band_in_200khz_steps() {
  TEST_ASSERT_EQUAL_size_t(130, kSurveyBinCount);
  TEST_ASSERT_EQUAL_UINT32(902000000UL, survey_bin_freq_hz(0));
  TEST_ASSERT_EQUAL_UINT32(902200000UL, survey_bin_freq_hz(1));
  TEST_ASSERT_EQUAL_UINT32(915000000UL, survey_bin_freq_hz(65));
  // The top bin is 927.8, not 928.0: a 125 kHz receiver centred on the band edge
  // would be listening half outside the band.
  TEST_ASSERT_EQUAL_UINT32(927800000UL, survey_bin_freq_hz(kSurveyBinCount - 1));
}

static void test_out_of_range_bin_index_is_not_a_frequency() {
  TEST_ASSERT_EQUAL_UINT32(0, survey_bin_freq_hz(kSurveyBinCount));
  TEST_ASSERT_EQUAL_UINT32(0, survey_bin_freq_hz(9999));
}

// The provisional 915.0 MHz starting point has to land ON a bin, or the survey can
// never say anything about the frequency the sweep has been running at.
static void test_the_provisional_frequency_lands_on_a_bin() {
  const size_t bin = survey_bin_of(915000000UL);
  TEST_ASSERT_EQUAL_size_t(65, bin);
  TEST_ASSERT_EQUAL_UINT32(915000000UL, survey_bin_freq_hz(bin));
}

static void test_bin_lookup_rounds_to_nearest_not_down() {
  // 914.9 belongs to the 915.0 bin. Truncating would file it under 914.8 and put a
  // noise figure beside the wrong channel.
  TEST_ASSERT_EQUAL_size_t(65, survey_bin_of(914900000UL));
  TEST_ASSERT_EQUAL_size_t(65, survey_bin_of(915090000UL));
  TEST_ASSERT_EQUAL_size_t(66, survey_bin_of(915150000UL));
}

static void test_frequencies_outside_the_plan_have_no_bin() {
  TEST_ASSERT_EQUAL_size_t(kSurveyBinCount, survey_bin_of(901000000UL));
  TEST_ASSERT_EQUAL_size_t(kSurveyBinCount, survey_bin_of(928000000UL));
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

static void test_samples_per_dwell_excludes_the_settle_time() {
  SurveyPlan p{};
  p.dwell_ms = 30;
  p.settle_ms = 4;
  p.sample_interval_ms = 3;
  // 26 ms of listening at 3 ms intervals, plus the sample at t=0.
  TEST_ASSERT_EQUAL_UINT16(26 / 3 + 1, survey_samples_per_dwell(p));
}

// A plan whose settle time swallows its dwell takes no samples at all. Zero is the
// honest answer; silently borrowing time from the settle would report a floor
// measured while the AGC was still climbing.
static void test_a_dwell_shorter_than_its_settle_takes_no_samples() {
  SurveyPlan p{};
  p.dwell_ms = 4;
  p.settle_ms = 4;
  TEST_ASSERT_EQUAL_UINT16(0, survey_samples_per_dwell(p));

  p.dwell_ms = 2;
  TEST_ASSERT_EQUAL_UINT16(0, survey_samples_per_dwell(p));
}

static void test_pass_duration_is_every_bin_dwelled_once() {
  SurveyPlan p{};
  p.dwell_ms = 30;
  p.bins = kSurveyBinCount;
  TEST_ASSERT_EQUAL_UINT32(130UL * 30UL, survey_pass_duration_ms(p));
}

// ---------------------------------------------------------------------------
// The accumulator
// ---------------------------------------------------------------------------

static void test_peak_is_held_and_mean_is_a_mean() {
  Survey s;
  s.add_sample(10, -1200);
  s.add_sample(10, -900);    // the peak
  s.add_sample(10, -1200);

  const SurveyBin& b = s.bin(10);
  TEST_ASSERT_EQUAL_INT16(-900,  b.rssi_dbm10.max);   // peak hold
  TEST_ASSERT_EQUAL_INT16(-1200, b.rssi_dbm10.min);   // floor
  TEST_ASSERT_EQUAL_INT16(-1100, b.rssi_dbm10.mean());
  TEST_ASSERT_EQUAL_UINT16(3, b.rssi_dbm10.count);
}

// A peak survives every later quiet pass. That is the entire detection mechanism for
// a brief transmission: it is seen once, in one pass, and must not be averaged away.
static void test_a_single_loud_sample_survives_many_quiet_ones() {
  Survey s;
  s.add_sample(3, -400);
  for (int i = 0; i < 5000; ++i) s.add_sample(3, -1250);
  TEST_ASSERT_EQUAL_INT16(-400, s.bin(3).rssi_dbm10.max);
}

static void test_bins_are_independent() {
  Survey s;
  s.add_sample(0, -1000);
  s.add_sample(129, -500);
  TEST_ASSERT_EQUAL_INT16(-1000, s.bin(0).rssi_dbm10.mean());
  TEST_ASSERT_EQUAL_INT16(-500, s.bin(129).rssi_dbm10.mean());
  TEST_ASSERT_TRUE(s.bin(1).rssi_dbm10.empty());
}

static void test_out_of_range_samples_are_ignored_not_wrapped() {
  Survey s;
  s.add_sample(kSurveyBinCount, -500);
  s.add_sample(9999, -500);
  TEST_ASSERT_EQUAL_size_t(0, s.bins_sampled());
}

// Series::count is 16 bits. Beyond the cap the sample is REFUSED and counted, never
// folded in over a wrapped denominator - a mean divided by the wrong count is a
// number that looks entirely plausible and is wrong.
static void test_samples_past_the_cap_are_counted_as_dropped() {
  Survey s;
  for (uint32_t i = 0; i < kSurveyMaxSamplesPerBin; ++i) s.add_sample(7, -1000);
  TEST_ASSERT_EQUAL_UINT16(kSurveyMaxSamplesPerBin, s.bin(7).rssi_dbm10.count);
  TEST_ASSERT_EQUAL_UINT32(0, s.bin(7).dropped);

  s.add_sample(7, -100);   // would have been a new peak
  TEST_ASSERT_EQUAL_UINT16(kSurveyMaxSamplesPerBin, s.bin(7).rssi_dbm10.count);
  TEST_ASSERT_EQUAL_UINT32(1, s.bin(7).dropped);
  TEST_ASSERT_EQUAL_INT16(-1000, s.bin(7).rssi_dbm10.max);   // refused, not folded in
  TEST_ASSERT_EQUAL_INT16(-1000, s.bin(7).rssi_dbm10.mean());
}

static void test_bins_sampled_reports_a_partial_pass() {
  Survey s;
  for (size_t i = 0; i < 40; ++i) s.add_sample(i, -1100);
  TEST_ASSERT_EQUAL_size_t(40, s.bins_sampled());
}

static void test_quietest_and_loudest_ignore_unsampled_bins() {
  Survey s;
  TEST_ASSERT_EQUAL_size_t(kSurveyBinCount, s.quietest_bin());
  TEST_ASSERT_EQUAL_size_t(kSurveyBinCount, s.loudest_bin());

  s.add_sample(20, -1150);
  s.add_sample(21, -1050);
  s.add_sample(22, -1250);   // quietest by mean
  s.add_sample(21, -300);    // loudest by peak, in a bin whose mean is middling

  TEST_ASSERT_EQUAL_size_t(22, s.quietest_bin());
  TEST_ASSERT_EQUAL_size_t(21, s.loudest_bin());
}

static void test_reset_clears_bins_and_passes() {
  Survey s;
  s.add_sample(5, -1000);
  s.note_pass();
  s.reset();
  TEST_ASSERT_EQUAL_UINT32(0, s.passes());
  TEST_ASSERT_EQUAL_size_t(0, s.bins_sampled());
}

// ---------------------------------------------------------------------------
// The NVS blob - what makes the far-end trace a one-person job
// ---------------------------------------------------------------------------

static void test_blob_round_trip_preserves_every_bin() {
  Survey a;
  for (size_t i = 0; i < kSurveyBinCount; ++i) {
    a.add_sample(i, static_cast<int16_t>(-1000 - static_cast<int>(i)));
    a.add_sample(i, static_cast<int16_t>(-500 - static_cast<int>(i)));
  }
  for (int i = 0; i < 37; ++i) a.note_pass();

  SurveyPlan plan{};
  plan.dwell_ms = 40;
  uint8_t blob[Survey::kBlobMaxLen];
  const size_t n = a.serialize(plan, blob, sizeof(blob));
  TEST_ASSERT_EQUAL_size_t(Survey::kBlobMaxLen, n);

  Survey b;
  SurveyPlan back{};
  TEST_ASSERT_TRUE(b.deserialize(blob, n, &back));
  TEST_ASSERT_EQUAL_UINT32(37, b.passes());
  TEST_ASSERT_EQUAL_UINT32(kSurveyStartHz, back.start_hz);
  TEST_ASSERT_EQUAL_UINT32(kSurveyStepHz, back.step_hz);

  for (size_t i = 0; i < kSurveyBinCount; ++i) {
    TEST_ASSERT_EQUAL_INT16(a.bin(i).rssi_dbm10.min, b.bin(i).rssi_dbm10.min);
    TEST_ASSERT_EQUAL_INT16(a.bin(i).rssi_dbm10.max, b.bin(i).rssi_dbm10.max);
    TEST_ASSERT_EQUAL_UINT16(a.bin(i).rssi_dbm10.count, b.bin(i).rssi_dbm10.count);
    TEST_ASSERT_EQUAL_INT16(a.bin(i).rssi_dbm10.mean(), b.bin(i).rssi_dbm10.mean());
  }
}

// The blob is negative numbers almost everywhere: RSSI sums are large and negative,
// and a sign that does not survive NVS turns a -110 dBm floor into a +110 dBm one.
static void test_blob_preserves_negative_sums_and_minima() {
  Survey a;
  for (int i = 0; i < 1000; ++i) a.add_sample(64, -1187);

  SurveyPlan plan{};
  uint8_t blob[Survey::kBlobMaxLen];
  TEST_ASSERT_NOT_EQUAL(0, a.serialize(plan, blob, sizeof(blob)));

  Survey b;
  TEST_ASSERT_TRUE(b.deserialize(blob, sizeof(blob), nullptr));
  TEST_ASSERT_EQUAL_INT16(-1187, b.bin(64).rssi_dbm10.mean());
  TEST_ASSERT_EQUAL_INT16(-1187, b.bin(64).rssi_dbm10.min);
  TEST_ASSERT_EQUAL_INT32(-1187 * 1000, b.bin(64).rssi_dbm10.sum);
}

static void test_serialize_refuses_a_short_buffer() {
  Survey s;
  SurveyPlan plan{};
  uint8_t small[16];
  TEST_ASSERT_EQUAL_size_t(0, s.serialize(plan, small, sizeof(small)));
}

static void test_corrupt_blob_leaves_the_survey_cleared() {
  Survey s;
  s.add_sample(2, -900);
  s.note_pass();

  uint8_t blob[Survey::kBlobMaxLen];
  std::memset(blob, 0xAB, sizeof(blob));      // bad magic
  TEST_ASSERT_FALSE(s.deserialize(blob, sizeof(blob), nullptr));
  TEST_ASSERT_EQUAL_size_t(0, s.bins_sampled());
  TEST_ASSERT_EQUAL_UINT32(0, s.passes());
}

static void test_short_blob_is_rejected() {
  Survey s;
  uint8_t blob[Survey::kBlobMaxLen];
  SurveyPlan plan{};
  s.serialize(plan, blob, sizeof(blob));
  TEST_ASSERT_FALSE(s.deserialize(blob, sizeof(blob) - 1, nullptr));
}

// Repo rule 1: the layout is written field by field, little-endian, never memcpy'd -
// this blob is read back by host tooling built with a different compiler.
static void test_blob_is_explicit_little_endian() {
  Survey s;
  SurveyPlan plan{};
  uint8_t blob[Survey::kBlobMaxLen];
  s.serialize(plan, blob, sizeof(blob));

  TEST_ASSERT_EQUAL_UINT8(0x38, blob[0]);   // magic 0x4C525338, low byte first
  TEST_ASSERT_EQUAL_UINT8(0x53, blob[1]);
  TEST_ASSERT_EQUAL_UINT8(0x52, blob[2]);
  TEST_ASSERT_EQUAL_UINT8(0x4C, blob[3]);
  TEST_ASSERT_EQUAL_UINT8(1,    blob[4]);   // version
  TEST_ASSERT_EQUAL_UINT8(0,    blob[5]);
  TEST_ASSERT_EQUAL_UINT8(130,  blob[6]);   // bin count
  TEST_ASSERT_EQUAL_UINT8(0,    blob[7]);
  // start_hz = 902000000 = 0x35C36D80
  TEST_ASSERT_EQUAL_UINT8(0x80, blob[8]);
  TEST_ASSERT_EQUAL_UINT8(0x6D, blob[9]);
  TEST_ASSERT_EQUAL_UINT8(0xC3, blob[10]);
  TEST_ASSERT_EQUAL_UINT8(0x35, blob[11]);
}

// ---------------------------------------------------------------------------
// The CSV - the thing a reader in eighteen months has to interpret
// ---------------------------------------------------------------------------

static void test_header_and_row_have_the_same_field_count() {
  Survey s;
  s.add_sample(0, -1100);

  SurveyCsvRow row{};
  row.bin_index = 0;
  row.freq_hz = survey_bin_freq_hz(0);
  row.passes = 4;
  row.bin = &s.bin(0);

  char line[kSurveyCsvMaxLine];
  TEST_ASSERT_NOT_EQUAL(0, survey_csv_row(row, line, sizeof(line)));

  size_t fields = 1;
  for (const char* p = line; *p != '\0'; ++p) {
    if (*p == ',') ++fields;
  }
  TEST_ASSERT_EQUAL_size_t(survey_csv_field_count(), fields);
}

static void test_header_fits_the_advertised_line_length() {
  char hdr[kSurveyCsvMaxLine];
  const size_t n = survey_csv_header(hdr, sizeof(hdr));
  TEST_ASSERT_NOT_EQUAL(0, n);
  TEST_ASSERT_LESS_THAN(kSurveyCsvMaxLine, n + 1);
}

static void test_row_reports_peak_mean_and_floor() {
  Survey s;
  s.add_sample(65, -1200);
  s.add_sample(65, -800);
  s.add_sample(65, -1000);

  SurveyCsvRow row{};
  row.site = 1;
  row.bin_index = 65;
  row.freq_hz = survey_bin_freq_hz(65);
  row.passes = 12;
  row.bin = &s.bin(65);

  char line[kSurveyCsvMaxLine];
  TEST_ASSERT_NOT_EQUAL(0, survey_csv_row(row, line, sizeof(line)));
  // bin,freq,passes,samples,peak,mean,floor,dropped
  TEST_ASSERT_EQUAL_STRING("1,gatelink-gate,65,915000000,12,3,-800,-1000,-1200,0",
                           line);
}

// An unsampled bin must report the sentinel, not 0.0 dBm. Zero would be a bin
// apparently 100 dB hotter than its neighbours (spec 4.6 / repo rule 6).
static void test_an_unsampled_bin_reports_sentinels_not_zero() {
  Survey s;
  SurveyCsvRow row{};
  row.bin_index = 5;
  row.freq_hz = survey_bin_freq_hz(5);
  row.passes = 0;
  row.bin = &s.bin(5);

  char line[kSurveyCsvMaxLine];
  TEST_ASSERT_NOT_EQUAL(0, survey_csv_row(row, line, sizeof(line)));
  TEST_ASSERT_EQUAL_STRING(
      "0,bridge-house,5,903000000,0,0,-32768,-32768,-32768,0", line);
}

static void test_row_refuses_a_short_buffer_rather_than_truncating() {
  Survey s;
  s.add_sample(0, -1100);
  SurveyCsvRow row{};
  row.bin_index = 0;
  row.freq_hz = survey_bin_freq_hz(0);
  row.bin = &s.bin(0);

  char tiny[8];
  TEST_ASSERT_EQUAL_size_t(0, survey_csv_row(row, tiny, sizeof(tiny)));
}

static void test_row_without_a_bin_is_refused() {
  SurveyCsvRow row{};
  char line[kSurveyCsvMaxLine];
  TEST_ASSERT_EQUAL_size_t(0, survey_csv_row(row, line, sizeof(line)));
}

// The site names are what makes a trace placeable. A run stored under an index the
// table does not cover must say so rather than reading off the end of it.
static void test_every_site_has_a_name_and_out_of_range_does_not_read_off_the_end() {
  TEST_ASSERT_EQUAL_size_t(7, kSurveySiteCount);
  TEST_ASSERT_EQUAL_STRING("bridge-house", survey_site_name(0));
  TEST_ASSERT_EQUAL_STRING("propane-tank", survey_site_name(kSurveySiteCount - 1));
  for (size_t i = 0; i < kSurveySiteCount; ++i) {
    TEST_ASSERT_NOT_NULL(survey_site_name(i));
    TEST_ASSERT_TRUE(survey_site_name(i)[0] != '\0');
  }
  TEST_ASSERT_EQUAL_STRING("unknown", survey_site_name(kSurveySiteCount));
  TEST_ASSERT_EQUAL_STRING("unknown", survey_site_name(9999));
}

// A site name containing a comma or a space would split a CSV row into the wrong
// number of fields, and the file would still parse.
static void test_site_names_are_csv_safe() {
  for (size_t i = 0; i < kSurveySiteCount; ++i) {
    for (const char* p = survey_site_name(i); *p != '\0'; ++p) {
      TEST_ASSERT_TRUE(*p != ',');
      TEST_ASSERT_TRUE(*p != ' ');
      TEST_ASSERT_TRUE(*p != '\n');
    }
  }
}

// Seven blobs have to fit a 20 kB NVS partition alongside the responder's position
// log. This is the arithmetic that says the campaign can be stored at all; if a
// future bin plan breaks it, it breaks here and not at the sixth site.
static void test_the_whole_campaign_fits_the_nvs_partition() {
  const size_t campaign = kSurveySiteCount * Survey::kBlobMaxLen;
  TEST_ASSERT_EQUAL_size_t(1580, Survey::kBlobMaxLen);
  // 0x5000 = 20480 bytes, of which one 4 kB page is reserved for compaction.
  TEST_ASSERT_LESS_THAN(20480 - 4096, campaign);
}

// ---------------------------------------------------------------------------
// R11 - the campaign cursor and its hold state.
//
// This exists because the 2026-09-05 campaign measured the WALK between sites and
// filed it under the destination. Peak-hold never forgets, so a burst heard in transit
// became a permanent occupant of a site the operator was only walking towards. These
// tests pin the state machine that stops it; the transitions are cheap to get subtly
// wrong and expensive to discover in the field, a week's walk later.
// ---------------------------------------------------------------------------

void test_campaign_boots_held_at_site_zero() {
  SurveyCampaign c;
  // Held, not Running: the operator is not standing at site 0 when the board boots,
  // and a scan that started itself would charge the walk in to the run.
  TEST_ASSERT_TRUE(c.held());
  TEST_ASSERT_EQUAL_UINT32(0, c.site());
}

void test_press_while_held_starts_the_dwell_without_moving_the_cursor() {
  SurveyCampaign c;
  TEST_ASSERT_EQUAL(static_cast<int>(SurveyPress::StartDwell),
                    static_cast<int>(c.classify_press()));
  c.note_started();
  TEST_ASSERT_FALSE(c.held());
  TEST_ASSERT_EQUAL_UINT32(0, c.site());
}

void test_press_while_running_stores_and_returns_to_held_on_the_next_site() {
  SurveyCampaign c;
  c.note_started();
  TEST_ASSERT_EQUAL(static_cast<int>(SurveyPress::StoreSite),
                    static_cast<int>(c.classify_press()));
  TEST_ASSERT_TRUE(c.note_stored(true));      // advanced
  TEST_ASSERT_EQUAL_UINT32(1, c.site());
  // HELD is the whole point: the walk to site 1 must not be measured.
  TEST_ASSERT_TRUE(c.held());
}

void test_a_failed_store_moves_nothing_at_all() {
  SurveyCampaign c;
  c.note_started();
  TEST_ASSERT_FALSE(c.note_stored(false));
  // Cursor put AND still Running. Advancing over a site that was not written is a
  // site silently lost - the NVS partition is small and fails by short write - and
  // dropping to Held would quietly stop measuring a site the operator thinks is live.
  TEST_ASSERT_EQUAL_UINT32(0, c.site());
  TEST_ASSERT_FALSE(c.held());
}

void test_a_retry_after_a_failed_store_still_works() {
  SurveyCampaign c;
  c.note_started();
  c.note_stored(false);
  TEST_ASSERT_EQUAL(static_cast<int>(SurveyPress::StoreSite),
                    static_cast<int>(c.classify_press()));
  TEST_ASSERT_TRUE(c.note_stored(true));
  TEST_ASSERT_EQUAL_UINT32(1, c.site());
  TEST_ASSERT_TRUE(c.held());
}

void test_a_full_campaign_walks_every_site_once() {
  SurveyCampaign c;
  for (size_t i = 0; i < kSurveySiteCount; ++i) {
    TEST_ASSERT_EQUAL_UINT32(i, c.site());
    TEST_ASSERT_TRUE(c.held());
    c.note_started();
    TEST_ASSERT_FALSE(c.held());
    c.note_stored(true);
  }
  // Seven sites stored, cursor parked on the last one.
  TEST_ASSERT_EQUAL_UINT32(kSurveySiteCount - 1, c.site());
  TEST_ASSERT_TRUE(c.held());
}

void test_the_last_site_stores_but_does_not_advance() {
  SurveyCampaign c;
  c.restore_site(kSurveySiteCount - 1);
  c.note_started();
  TEST_ASSERT_TRUE(c.on_last_site());
  // No advance - there is nowhere to go - but the phase still returns to Held so the
  // board is not left silently accumulating over a run it has already stored.
  TEST_ASSERT_FALSE(c.note_stored(true));
  TEST_ASSERT_EQUAL_UINT32(kSurveySiteCount - 1, c.site());
  TEST_ASSERT_TRUE(c.held());
}

void test_cursor_corrections_store_nothing_and_drop_to_held() {
  SurveyCampaign c;
  c.note_started();
  TEST_ASSERT_TRUE(c.next_site());
  TEST_ASSERT_EQUAL_UINT32(1, c.site());
  TEST_ASSERT_TRUE(c.held());

  c.note_started();
  TEST_ASSERT_TRUE(c.prev_site());
  TEST_ASSERT_EQUAL_UINT32(0, c.site());
  TEST_ASSERT_TRUE(c.held());
}

void test_cursor_corrections_stop_at_the_ends() {
  SurveyCampaign c;
  TEST_ASSERT_FALSE(c.prev_site());
  TEST_ASSERT_EQUAL_UINT32(0, c.site());

  c.restore_site(kSurveySiteCount - 1);
  TEST_ASSERT_FALSE(c.next_site());
  TEST_ASSERT_EQUAL_UINT32(kSurveySiteCount - 1, c.site());
}

void test_restoring_a_stale_cursor_is_clamped_not_trusted() {
  SurveyCampaign c;
  // A cursor persisted by a build with a longer site list would otherwise index off
  // the end of the name table.
  c.restore_site(9999);
  TEST_ASSERT_EQUAL_UINT32(kSurveySiteCount - 1, c.site());
}

void test_a_power_cycle_resumes_held() {
  SurveyCampaign c;
  c.note_started();
  // A power cycle happens between sites, with the board in a bag or on a charger.
  // Coming back Running would measure whatever the walk to the next site heard.
  c.restore_site(3);
  TEST_ASSERT_EQUAL_UINT32(3, c.site());
  TEST_ASSERT_TRUE(c.held());
}

void test_reset_returns_to_the_boot_state() {
  SurveyCampaign c;
  c.restore_site(4);
  c.note_started();
  c.reset();
  TEST_ASSERT_EQUAL_UINT32(0, c.site());
  TEST_ASSERT_TRUE(c.held());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_site_has_a_name_and_out_of_range_does_not_read_off_the_end);
  RUN_TEST(test_site_names_are_csv_safe);
  RUN_TEST(test_the_whole_campaign_fits_the_nvs_partition);
  RUN_TEST(test_bin_plan_spans_the_band_in_200khz_steps);
  RUN_TEST(test_out_of_range_bin_index_is_not_a_frequency);
  RUN_TEST(test_the_provisional_frequency_lands_on_a_bin);
  RUN_TEST(test_bin_lookup_rounds_to_nearest_not_down);
  RUN_TEST(test_frequencies_outside_the_plan_have_no_bin);
  RUN_TEST(test_samples_per_dwell_excludes_the_settle_time);
  RUN_TEST(test_a_dwell_shorter_than_its_settle_takes_no_samples);
  RUN_TEST(test_pass_duration_is_every_bin_dwelled_once);
  RUN_TEST(test_peak_is_held_and_mean_is_a_mean);
  RUN_TEST(test_a_single_loud_sample_survives_many_quiet_ones);
  RUN_TEST(test_bins_are_independent);
  RUN_TEST(test_out_of_range_samples_are_ignored_not_wrapped);
  RUN_TEST(test_samples_past_the_cap_are_counted_as_dropped);
  RUN_TEST(test_bins_sampled_reports_a_partial_pass);
  RUN_TEST(test_quietest_and_loudest_ignore_unsampled_bins);
  RUN_TEST(test_reset_clears_bins_and_passes);
  RUN_TEST(test_blob_round_trip_preserves_every_bin);
  RUN_TEST(test_blob_preserves_negative_sums_and_minima);
  RUN_TEST(test_serialize_refuses_a_short_buffer);
  RUN_TEST(test_corrupt_blob_leaves_the_survey_cleared);
  RUN_TEST(test_short_blob_is_rejected);
  RUN_TEST(test_blob_is_explicit_little_endian);
  RUN_TEST(test_header_and_row_have_the_same_field_count);
  RUN_TEST(test_header_fits_the_advertised_line_length);
  RUN_TEST(test_row_reports_peak_mean_and_floor);
  RUN_TEST(test_an_unsampled_bin_reports_sentinels_not_zero);
  RUN_TEST(test_row_refuses_a_short_buffer_rather_than_truncating);
  RUN_TEST(test_row_without_a_bin_is_refused);
  RUN_TEST(test_campaign_boots_held_at_site_zero);
  RUN_TEST(test_press_while_held_starts_the_dwell_without_moving_the_cursor);
  RUN_TEST(test_press_while_running_stores_and_returns_to_held_on_the_next_site);
  RUN_TEST(test_a_failed_store_moves_nothing_at_all);
  RUN_TEST(test_a_retry_after_a_failed_store_still_works);
  RUN_TEST(test_a_full_campaign_walks_every_site_once);
  RUN_TEST(test_the_last_site_stores_but_does_not_advance);
  RUN_TEST(test_cursor_corrections_store_nothing_and_drop_to_held);
  RUN_TEST(test_cursor_corrections_stop_at_the_ends);
  RUN_TEST(test_restoring_a_stale_cursor_is_clamped_not_trusted);
  RUN_TEST(test_a_power_cycle_resumes_held);
  RUN_TEST(test_reset_returns_to_the_boot_state);
  return UNITY_END();
}
