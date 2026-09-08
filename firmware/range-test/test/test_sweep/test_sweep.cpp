// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Host tests for the R4 sweep: enumeration, the D33 clamp on every point, the
// round-trip PER arithmetic, and the duration estimate.

#include <unity.h>

#include "airtime.h"
#include "sweep.h"

using namespace rangetest;

void setUp() {}
void tearDown() {}

static constexpr int16_t kGain2dBi = 20;

// ---------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------

static void test_point_count_is_the_product_of_the_axes() {
  const SweepPlan p = default_plan();
  TEST_ASSERT_EQUAL_size_t(
      kDefaultFreqCount * kDefaultSfCount * kDefaultCrCount *
          kDefaultPowerCount * kDefaultPayloadCount,
      sweep_point_count(p));
}

static void test_an_empty_axis_yields_no_points() {
  SweepPlan p = default_plan();
  p.n_sf = 0;
  TEST_ASSERT_EQUAL_size_t(0, sweep_point_count(p));
  TestPoint tp{};
  TEST_ASSERT_FALSE(sweep_point_at(p, 0, kGain2dBi, &tp));
}

static void test_index_past_the_end_is_refused() {
  const SweepPlan p = default_plan();
  TestPoint tp{};
  TEST_ASSERT_FALSE(sweep_point_at(p, sweep_point_count(p), kGain2dBi, &tp));
}

// Payload varies fastest, then power, then CR, then SF (see sweep.h).
static void test_axis_order_is_payload_fastest() {
  const SweepPlan p = default_plan();
  TestPoint a{}, b{};
  TEST_ASSERT_TRUE(sweep_point_at(p, 0, kGain2dBi, &a));
  TEST_ASSERT_TRUE(sweep_point_at(p, 1, kGain2dBi, &b));

  TEST_ASSERT_NOT_EQUAL(a.payload_len, b.payload_len);
  TEST_ASSERT_EQUAL_UINT8(a.sf, b.sf);
  TEST_ASSERT_EQUAL_UINT8(a.cr_denom, b.cr_denom);
}

// SF is the slowest axis, so a partial sweep still spans payload and power.
static void test_sf_is_the_slowest_varying_axis() {
  const SweepPlan p = default_plan();
  const size_t per_sf = kDefaultPayloadCount * kDefaultPowerCount * kDefaultCrCount;

  TestPoint first{}, last_of_block{}, next_block{};
  TEST_ASSERT_TRUE(sweep_point_at(p, 0, kGain2dBi, &first));
  TEST_ASSERT_TRUE(sweep_point_at(p, per_sf - 1, kGain2dBi, &last_of_block));
  TEST_ASSERT_TRUE(sweep_point_at(p, per_sf, kGain2dBi, &next_block));

  TEST_ASSERT_EQUAL_UINT8(first.sf, last_of_block.sf);
  TEST_ASSERT_NOT_EQUAL(first.sf, next_block.sf);
}

static void test_every_point_is_visited_exactly_once() {
  const SweepPlan p = default_plan();
  const size_t    n = sweep_point_count(p);
  TEST_ASSERT_LESS_OR_EQUAL_size_t(64, n);   // guard: the bitmap below assumes it

  uint64_t seen = 0;
  for (size_t i = 0; i < n; ++i) {
    TestPoint tp{};
    TEST_ASSERT_TRUE(sweep_point_at(p, i, kGain2dBi, &tp));

    size_t sf_i = 0, cr_i = 0, pw_i = 0, pl_i = 0;
    for (size_t k = 0; k < kDefaultSfCount; ++k)      if (kDefaultSfs[k] == tp.sf) sf_i = k;
    for (size_t k = 0; k < kDefaultCrCount; ++k)      if (kDefaultCrDenoms[k] == tp.cr_denom) cr_i = k;
    for (size_t k = 0; k < kDefaultPayloadCount; ++k) if (kDefaultPayloads[k] == tp.payload_len) pl_i = k;
    // Power is clamped, so identify it by rank rather than by requested value.
    pw_i = (tp.power.conducted_dbm == kSx1262MinDbm) ? 0 : 1;

    const size_t slot = ((sf_i * kDefaultCrCount + cr_i) * kDefaultPowerCount + pw_i)
                            * kDefaultPayloadCount + pl_i;
    TEST_ASSERT_EQUAL_UINT64(0, seen & (1ULL << slot));  // not seen before
    seen |= (1ULL << slot);
  }
  TEST_ASSERT_EQUAL_UINT64((n == 64) ? ~0ULL : ((1ULL << n) - 1), seen);
}

// ---------------------------------------------------------------------------
// D33 - the plan's own table is not exempt from the clamp
// ---------------------------------------------------------------------------

// kDefaultPowersDbm asks for the SX1262 MAXIMUM at its high end precisely so the
// clamp is what decides the answer. If a point ever emerged at +22 dBm the cap would
// have been bypassed.
static void test_no_point_exceeds_the_d33_ceiling() {
  const SweepPlan p = default_plan();
  const int8_t ceiling = conducted_ceiling_dbm(kGain2dBi);

  for (size_t i = 0; i < sweep_point_count(p); ++i) {
    TestPoint tp{};
    TEST_ASSERT_TRUE(sweep_point_at(p, i, kGain2dBi, &tp));
    TEST_ASSERT_LESS_OR_EQUAL_INT8(ceiling, tp.power.conducted_dbm);
    TEST_ASSERT_GREATER_OR_EQUAL_INT8(kSx1262MinDbm, tp.power.conducted_dbm);
  }
}

static void test_sweep_starts_at_the_bottom_of_the_radios_range() {
  const SweepPlan p = default_plan();
  TestPoint tp{};
  TEST_ASSERT_TRUE(sweep_point_at(p, 0, kGain2dBi, &tp));
  TEST_ASSERT_EQUAL_INT8(kSx1262MinDbm, tp.power.conducted_dbm);
}

static void test_antenna_gain_is_carried_into_every_point() {
  const SweepPlan p = default_plan();
  for (size_t i = 0; i < sweep_point_count(p); ++i) {
    TestPoint tp{};
    TEST_ASSERT_TRUE(sweep_point_at(p, i, kGain2dBi, &tp));
    TEST_ASSERT_EQUAL_INT16(kGain2dBi, tp.power.antenna_gain_dbi10);
  }
}

// A 10 dBi antenna puts the ceiling under the radio floor: no legal power exists, so
// the point is refused rather than silently produced at -9 dBm.
static void test_impossible_antenna_refuses_every_point() {
  const SweepPlan p = default_plan();
  TestPoint tp{};
  TEST_ASSERT_FALSE(sweep_point_at(p, 0, /* 10.0 dBi */ 100, &tp));
}

// ---------------------------------------------------------------------------
// Round-trip PER - R4's primary metric
// ---------------------------------------------------------------------------

static void test_per_is_zero_when_every_probe_is_echoed() {
  TestPointStats s{};
  s.probes_sent = 8; s.echoes_received = 8;
  TEST_ASSERT_EQUAL_UINT16(0, s.per_pct100());
}

static void test_per_is_full_when_nothing_comes_back() {
  TestPointStats s{};
  s.probes_sent = 8; s.echoes_received = 0;
  TEST_ASSERT_EQUAL_UINT16(10000, s.per_pct100());
}

static void test_per_is_exact_for_awkward_fractions() {
  TestPointStats s{};
  s.probes_sent = 3; s.echoes_received = 2;   // 1/3 = 33.33%
  TEST_ASSERT_EQUAL_UINT16(3333, s.per_pct100());

  s.probes_sent = 8; s.echoes_received = 7;   // 1/8 = 12.50%
  TEST_ASSERT_EQUAL_UINT16(1250, s.per_pct100());
}

// "Nothing sent" is not "no loss". A test point that never ran must not read as a
// perfect one in the CSV.
static void test_no_probes_is_not_reported_as_zero_loss() {
  TestPointStats s{};
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, s.per_pct100());
  TEST_ASSERT_NOT_EQUAL(0, s.per_pct100());
}

// Duplicates would otherwise produce a negative loss. The poll() defect fixed on the
// R2 bench produced exactly this shape, so it is pinned.
static void test_more_echoes_than_probes_clamps_to_zero_not_underflow() {
  TestPointStats s{};
  s.probes_sent = 4; s.echoes_received = 9;
  TEST_ASSERT_EQUAL_UINT16(0, s.per_pct100());
}

// ---------------------------------------------------------------------------
// Series
// ---------------------------------------------------------------------------

static void test_series_tracks_min_max_and_mean() {
  Series s{};
  s.add(-490); s.add(-510); s.add(-500);
  TEST_ASSERT_EQUAL_UINT16(3, s.count);
  TEST_ASSERT_EQUAL_INT16(-510, s.min);
  TEST_ASSERT_EQUAL_INT16(-490, s.max);
  TEST_ASSERT_EQUAL_INT16(-500, s.mean());
}

static void test_empty_series_reports_not_available_not_zero() {
  Series s{};
  TEST_ASSERT_TRUE(s.empty());
  TEST_ASSERT_EQUAL_INT16(kI16NotAvailable, s.mean());
  TEST_ASSERT_NOT_EQUAL(0, s.mean());
}

// Truncation toward zero would report -49.5 dBm as -49, biasing every negative mean
// optimistically - and RSSI is always negative here.
static void test_negative_mean_rounds_away_from_zero() {
  Series s{};
  s.add(-49); s.add(-50);        // mean -49.5
  TEST_ASSERT_EQUAL_INT16(-50, s.mean());
}

static void test_a_single_zero_sample_is_a_reading() {
  Series s{};
  s.add(0);
  TEST_ASSERT_FALSE(s.empty());
  TEST_ASSERT_EQUAL_INT16(0, s.mean());
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

// A fixed echo timeout is either far too slow at SF7 or scores every SF12 probe lost.
static void test_echo_timeout_scales_with_spreading_factor() {
  const SweepPlan p = default_plan();
  TestPoint fast{}, slow{};
  fast.sf = 7;  fast.cr_denom = 5; fast.payload_len = 64;
  slow.sf = 12; slow.cr_denom = 5; slow.payload_len = 64;
  TEST_ASSERT_GREATER_THAN_UINT32(echo_timeout_ms(p, fast) * 5,
                                  echo_timeout_ms(p, slow));
}

static void test_echo_timeout_covers_both_legs_plus_margin() {
  SweepPlan p = default_plan();
  p.echo_timeout_margin_ms = 100;
  TestPoint tp{};
  tp.sf = 9; tp.cr_denom = 5; tp.payload_len = 64;

  LoraParams lp{}; lp.sf = 9; lp.cr_denom = 5;
  TEST_ASSERT_EQUAL_UINT32(2 * airtime_ms(lp, 64) + 100, echo_timeout_ms(p, tp));
}

// The figure the operator plans a walk around. Assert it is sane rather than exact:
// the point is that it is computed, not guessed.
static void test_default_sweep_duration_is_bounded_and_nonzero() {
  const uint32_t ms = sweep_duration_worst_ms(default_plan(), kGain2dBi);
  TEST_ASSERT_GREATER_THAN_UINT32(0, ms);
  TEST_ASSERT_LESS_THAN_UINT32(15UL * 60UL * 1000UL, ms);  // under 15 minutes
}

// The spread is the point. A dead position costs materially more than a good one, and
// collapsing the two into one figure would mislead in whichever direction it rounded.
static void test_worst_case_exceeds_nominal() {
  const SweepPlan p = default_plan();
  TEST_ASSERT_GREATER_THAN_UINT32(sweep_duration_nominal_ms(p, kGain2dBi),
                                  sweep_duration_worst_ms(p, kGain2dBi));
}

static void test_more_probes_costs_proportionally_more_time() {
  SweepPlan a = default_plan(); a.probes_per_point = 4;
  SweepPlan b = default_plan(); b.probes_per_point = 8;
  TEST_ASSERT_EQUAL_UINT32(2 * sweep_duration_worst_ms(a, kGain2dBi),
                           sweep_duration_worst_ms(b, kGain2dBi));
}

// ---------------------------------------------------------------------------
// Radio configurations - the responder-follows fix
// ---------------------------------------------------------------------------

// Power and payload do not affect whether a receiver can hear a transmitter, so the
// responder tracks six configurations rather than twenty-four test points.
static void test_config_count_excludes_power_and_payload() {
  const SweepPlan p = default_plan();
  TEST_ASSERT_EQUAL_size_t(kDefaultFreqCount * kDefaultSfCount * kDefaultCrCount,
                           sweep_config_count(p));
  TEST_ASSERT_LESS_THAN_size_t(sweep_point_count(p), sweep_config_count(p));
}

// THE BUG THIS FIXES. Every test point must map to the configuration it actually
// transmits on; if this mapping disagreed with sweep_point_at()'s axis order the
// responder would chase the wrong configuration and hear nothing - which is exactly
// the flat 100% PER the first hardware sweep produced from SF9 onward.
static void test_every_point_maps_to_the_config_it_transmits_on() {
  const SweepPlan p = default_plan();
  for (size_t i = 0; i < sweep_point_count(p); ++i) {
    TestPoint tp{};
    TEST_ASSERT_TRUE(sweep_point_at(p, i, kGain2dBi, &tp));

    size_t ci = 0;
    TEST_ASSERT_TRUE(sweep_config_index_of(p, i, &ci));

    RadioConfigKey cfg{};
    TEST_ASSERT_TRUE(sweep_config_at(p, ci, &cfg));

    TEST_ASSERT_EQUAL_UINT32(tp.freq_hz,  cfg.freq_hz);
    TEST_ASSERT_EQUAL_UINT8 (tp.sf,       cfg.sf);
    TEST_ASSERT_EQUAL_UINT8 (tp.cr_denom, cfg.cr_denom);
  }
}

// Consecutive points sharing a configuration must not make the responder retune -
// that is the common case and where most probes would otherwise be lost.
static void test_points_differing_only_in_payload_share_a_config() {
  const SweepPlan p = default_plan();
  size_t a = 0, b = 0;
  TEST_ASSERT_TRUE(sweep_config_index_of(p, 0, &a));
  TEST_ASSERT_TRUE(sweep_config_index_of(p, 1, &b));   // payload axis only
  TEST_ASSERT_EQUAL_size_t(a, b);
}

static void test_every_config_is_reachable_and_distinct() {
  const SweepPlan p = default_plan();
  const size_t    n = sweep_config_count(p);
  for (size_t i = 0; i < n; ++i) {
    RadioConfigKey ci{};
    TEST_ASSERT_TRUE(sweep_config_at(p, i, &ci));
    for (size_t j = i + 1; j < n; ++j) {
      RadioConfigKey cj{};
      TEST_ASSERT_TRUE(sweep_config_at(p, j, &cj));
      TEST_ASSERT_FALSE(ci == cj);
    }
  }
  RadioConfigKey past{};
  TEST_ASSERT_FALSE(sweep_config_at(p, n, &past));
}

// A fixed dwell cannot work: one probe period is ~0.5 s at SF7 and ~8.4 s at SF12, so
// anything responsive at SF7 abandons SF12 mid-probe.
static void test_dwell_scales_with_spreading_factor() {
  const SweepPlan p = default_plan();
  RadioConfigKey fast{kProvisionalFreqHz, 7, 5};
  RadioConfigKey slow{kProvisionalFreqHz, 12, 5};
  TEST_ASSERT_GREATER_THAN_UINT32(sweep_config_dwell_ms(p, fast) * 5,
                                  sweep_config_dwell_ms(p, slow));
}

// A responder arriving just after a probe started must still be there for the next
// one, or it abandons a configuration that is working.
static void test_dwell_covers_at_least_two_probe_periods() {
  const SweepPlan p = default_plan();
  RadioConfigKey cfg{kProvisionalFreqHz, 9, 5};

  TestPoint tp{};
  tp.sf = 9; tp.cr_denom = 5; tp.payload_len = 64;   // largest in the default plan
  const uint32_t one_probe = echo_timeout_ms(p, tp) + p.inter_probe_gap_ms;

  TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2 * one_probe, sweep_config_dwell_ms(p, cfg));
}

// ---------------------------------------------------------------------------
// Warmup probes - removing the reacquisition bias
// ---------------------------------------------------------------------------

// Four consecutive points share a configuration in the default plan, so the common
// case must cost nothing.
static void test_no_warmup_when_the_config_is_unchanged() {
  const SweepPlan p = default_plan();
  TestPoint tp{};
  TEST_ASSERT_TRUE(sweep_point_at(p, 1, kGain2dBi, &tp));
  TEST_ASSERT_EQUAL_UINT16(0, sweep_warmup_probes(p, false, 0, tp));
}

static void test_warmup_is_requested_on_a_config_change() {
  const SweepPlan p = default_plan();
  TestPoint tp{};
  TEST_ASSERT_TRUE(sweep_point_at(p, 8, kGain2dBi, &tp));   // first SF9 point
  TEST_ASSERT_GREATER_THAN_UINT16(0, sweep_warmup_probes(p, true, 1, tp));
}

// Leaving a slow configuration means a longer wait for the responder to notice, so
// more warmup probes are needed at the fast configuration that follows.
static void test_leaving_a_slow_config_needs_more_warmup() {
  const SweepPlan p = default_plan();
  TestPoint fast_next{};
  TEST_ASSERT_TRUE(sweep_point_at(p, 0, kGain2dBi, &fast_next));  // SF7

  size_t sf7_cfg = 0, sf12_cfg = 0;
  TEST_ASSERT_TRUE(sweep_config_index_of(p, 0,  &sf7_cfg));
  TEST_ASSERT_TRUE(sweep_config_index_of(p, 20, &sf12_cfg));

  TEST_ASSERT_GREATER_THAN_UINT16(sweep_warmup_probes(p, true, sf7_cfg, fast_next),
                                  sweep_warmup_probes(p, true, sf12_cfg, fast_next));
}

// A warmup longer than the measurement it protects is a worse trade than the bias.
static void test_warmup_is_bounded() {
  const SweepPlan p = default_plan();
  for (size_t i = 0; i < sweep_point_count(p); ++i) {
    TestPoint tp{};
    TEST_ASSERT_TRUE(sweep_point_at(p, i, kGain2dBi, &tp));
    for (size_t c = 0; c < sweep_config_count(p); ++c) {
      TEST_ASSERT_LESS_OR_EQUAL_UINT16(6, sweep_warmup_probes(p, true, c, tp));
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_point_count_is_the_product_of_the_axes);
  RUN_TEST(test_an_empty_axis_yields_no_points);
  RUN_TEST(test_index_past_the_end_is_refused);
  RUN_TEST(test_axis_order_is_payload_fastest);
  RUN_TEST(test_sf_is_the_slowest_varying_axis);
  RUN_TEST(test_every_point_is_visited_exactly_once);

  RUN_TEST(test_no_point_exceeds_the_d33_ceiling);
  RUN_TEST(test_sweep_starts_at_the_bottom_of_the_radios_range);
  RUN_TEST(test_antenna_gain_is_carried_into_every_point);
  RUN_TEST(test_impossible_antenna_refuses_every_point);

  RUN_TEST(test_per_is_zero_when_every_probe_is_echoed);
  RUN_TEST(test_per_is_full_when_nothing_comes_back);
  RUN_TEST(test_per_is_exact_for_awkward_fractions);
  RUN_TEST(test_no_probes_is_not_reported_as_zero_loss);
  RUN_TEST(test_more_echoes_than_probes_clamps_to_zero_not_underflow);

  RUN_TEST(test_series_tracks_min_max_and_mean);
  RUN_TEST(test_empty_series_reports_not_available_not_zero);
  RUN_TEST(test_negative_mean_rounds_away_from_zero);
  RUN_TEST(test_a_single_zero_sample_is_a_reading);

  RUN_TEST(test_echo_timeout_scales_with_spreading_factor);
  RUN_TEST(test_echo_timeout_covers_both_legs_plus_margin);
  RUN_TEST(test_default_sweep_duration_is_bounded_and_nonzero);
  RUN_TEST(test_worst_case_exceeds_nominal);
  RUN_TEST(test_more_probes_costs_proportionally_more_time);

  RUN_TEST(test_config_count_excludes_power_and_payload);
  RUN_TEST(test_every_point_maps_to_the_config_it_transmits_on);
  RUN_TEST(test_points_differing_only_in_payload_share_a_config);
  RUN_TEST(test_every_config_is_reachable_and_distinct);
  RUN_TEST(test_dwell_scales_with_spreading_factor);
  RUN_TEST(test_dwell_covers_at_least_two_probe_periods);

  RUN_TEST(test_no_warmup_when_the_config_is_unchanged);
  RUN_TEST(test_warmup_is_requested_on_a_config_change);
  RUN_TEST(test_leaving_a_slow_config_needs_more_warmup);
  RUN_TEST(test_warmup_is_bounded);
  return UNITY_END();
}
