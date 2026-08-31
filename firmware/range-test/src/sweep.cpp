// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "sweep.h"

#include "airtime.h"
#include "bench_frame.h"

namespace rangetest {

// ---------------------------------------------------------------------------
// Default axes
// ---------------------------------------------------------------------------

const uint32_t kDefaultFreqsHz[]  = {kProvisionalFreqHz};
const size_t   kDefaultFreqCount  = sizeof(kDefaultFreqsHz) / sizeof(kDefaultFreqsHz[0]);

const uint8_t kDefaultSfs[]       = {7, 9, 12};
const size_t  kDefaultSfCount     = sizeof(kDefaultSfs) / sizeof(kDefaultSfs[0]);

const uint8_t kDefaultCrDenoms[]  = {5, 8};
const size_t  kDefaultCrCount     = sizeof(kDefaultCrDenoms) / sizeof(kDefaultCrDenoms[0]);

const int8_t  kDefaultPowersDbm[] = {kSx1262MinDbm, kSx1262MaxDbm};
const size_t  kDefaultPowerCount  = sizeof(kDefaultPowersDbm) / sizeof(kDefaultPowersDbm[0]);

const uint8_t kDefaultPayloads[]  = {static_cast<uint8_t>(kBenchHeaderLen), 64};
const size_t  kDefaultPayloadCount = sizeof(kDefaultPayloads) / sizeof(kDefaultPayloads[0]);

SweepPlan default_plan() {
  SweepPlan p{};
  p.freqs_hz  = kDefaultFreqsHz;   p.n_freq    = kDefaultFreqCount;
  p.sfs       = kDefaultSfs;       p.n_sf      = kDefaultSfCount;
  p.cr_denoms = kDefaultCrDenoms;  p.n_cr      = kDefaultCrCount;
  p.powers_dbm = kDefaultPowersDbm; p.n_power  = kDefaultPowerCount;
  p.payloads  = kDefaultPayloads;  p.n_payload = kDefaultPayloadCount;
  return p;
}

// ---------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------

size_t sweep_point_count(const SweepPlan& p) {
  if (p.n_freq == 0 || p.n_sf == 0 || p.n_cr == 0 || p.n_power == 0 ||
      p.n_payload == 0) {
    return 0;
  }
  return p.n_freq * p.n_sf * p.n_cr * p.n_power * p.n_payload;
}

bool sweep_point_at(const SweepPlan& p, size_t index, int16_t antenna_gain_dbi10,
                    TestPoint* out) {
  if (out == nullptr) return false;
  const size_t total = sweep_point_count(p);
  if (total == 0 || index >= total) return false;

  // Fastest axis first - see the header for why this order and not another.
  size_t rest = index;
  const size_t i_payload = rest % p.n_payload; rest /= p.n_payload;
  const size_t i_power   = rest % p.n_power;   rest /= p.n_power;
  const size_t i_cr      = rest % p.n_cr;      rest /= p.n_cr;
  const size_t i_sf      = rest % p.n_sf;      rest /= p.n_sf;
  const size_t i_freq    = rest % p.n_freq;

  TestPoint tp{};
  tp.freq_hz     = p.freqs_hz[i_freq];
  tp.sf          = p.sfs[i_sf];
  tp.cr_denom    = p.cr_denoms[i_cr];
  tp.payload_len = p.payloads[i_payload];

  // Every power goes through the D33 clamp. There is no other route to an output
  // power in this firmware, and the plan's own table is not exempt from it.
  const ClampResult clamp =
      clamp_conducted(p.powers_dbm[i_power], antenna_gain_dbi10, &tp.power);
  if (clamp == ClampResult::BelowRadioFloor) return false;

  *out = tp;
  return true;
}

uint32_t echo_timeout_ms(const SweepPlan& p, const TestPoint& tp) {
  LoraParams lp{};
  lp.sf       = tp.sf;
  lp.cr_denom = tp.cr_denom;

  // Both legs carry the same payload length: the responder echoes a frame of the same
  // size, so the round trip is two airtimes of the same shape.
  const uint32_t one_way = airtime_ms(lp, tp.payload_len);
  return 2U * one_way + p.echo_timeout_margin_ms;
}

namespace {

uint32_t duration_ms(const SweepPlan& p, int16_t gain_dbi10, bool worst) {
  const size_t total = sweep_point_count(p);
  uint32_t     ms    = 0;

  for (size_t i = 0; i < total; ++i) {
    TestPoint tp{};
    if (!sweep_point_at(p, i, gain_dbi10, &tp)) continue;

    uint32_t per_probe;
    if (worst) {
      // Every probe runs out its deadline. This is the far-position case.
      per_probe = echo_timeout_ms(p, tp) + p.inter_probe_gap_ms;
    } else {
      // Every echo returns after one round trip of airtime.
      LoraParams lp{};
      lp.sf       = tp.sf;
      lp.cr_denom = tp.cr_denom;
      per_probe   = 2U * airtime_ms(lp, tp.payload_len) + p.inter_probe_gap_ms;
    }
    ms += per_probe * p.probes_per_point;
  }
  return ms;
}

}  // namespace

uint32_t sweep_duration_worst_ms(const SweepPlan& p, int16_t gain_dbi10) {
  return duration_ms(p, gain_dbi10, true);
}

uint32_t sweep_duration_nominal_ms(const SweepPlan& p, int16_t gain_dbi10) {
  return duration_ms(p, gain_dbi10, false);
}

// ---------------------------------------------------------------------------
// Radio configurations
// ---------------------------------------------------------------------------

size_t sweep_config_count(const SweepPlan& p) {
  if (p.n_freq == 0 || p.n_sf == 0 || p.n_cr == 0) return 0;
  return p.n_freq * p.n_sf * p.n_cr;
}

bool sweep_config_at(const SweepPlan& p, size_t config_index, RadioConfigKey* out) {
  if (out == nullptr) return false;
  const size_t total = sweep_config_count(p);
  if (total == 0 || config_index >= total) return false;

  // Same nesting as sweep_point_at, with the power and payload axes removed: CR
  // fastest, then SF, then frequency. Keeping the order identical is what makes
  // sweep_config_index_of() a division rather than a search.
  size_t rest = config_index;
  const size_t i_cr   = rest % p.n_cr;  rest /= p.n_cr;
  const size_t i_sf   = rest % p.n_sf;  rest /= p.n_sf;
  const size_t i_freq = rest % p.n_freq;

  out->freq_hz  = p.freqs_hz[i_freq];
  out->sf       = p.sfs[i_sf];
  out->cr_denom = p.cr_denoms[i_cr];
  return true;
}

bool sweep_config_index_of(const SweepPlan& p, size_t tp_index, size_t* out) {
  if (out == nullptr) return false;
  if (tp_index >= sweep_point_count(p)) return false;

  // Strip the two fastest axes - payload and power - and what remains is exactly the
  // configuration index. This is why the two enumerations must share an axis order.
  *out = tp_index / (p.n_payload * p.n_power);
  return true;
}

uint32_t sweep_config_dwell_ms(const SweepPlan& p, const RadioConfigKey& cfg) {
  // The worst probe period at this configuration: the largest payload in the plan,
  // its full echo deadline, plus the inter-probe gap.
  uint8_t max_payload = 0;
  for (size_t i = 0; i < p.n_payload; ++i) {
    if (p.payloads[i] > max_payload) max_payload = p.payloads[i];
  }

  TestPoint tp{};
  tp.sf          = cfg.sf;
  tp.cr_denom    = cfg.cr_denom;
  tp.payload_len = max_payload;

  // Two probe periods. One is too tight - a responder arriving just after a probe
  // started would leave before the next one, and never hear a configuration that is
  // working perfectly well.
  return 2U * (echo_timeout_ms(p, tp) + p.inter_probe_gap_ms);
}

uint16_t sweep_warmup_probes(const SweepPlan& p, bool config_changed,
                             size_t prev_config, const TestPoint& next_tp) {
  if (!config_changed) return 0;

  RadioConfigKey prev{};
  if (!sweep_config_at(p, prev_config, &prev)) return 0;

  const uint32_t dwell    = sweep_config_dwell_ms(p, prev);
  const uint32_t probe_ms = echo_timeout_ms(p, next_tp) + p.inter_probe_gap_ms;
  if (probe_ms == 0) return 0;

  // Round up, then one more. The responder's dwell may have started at any point
  // inside a probe period, so covering exactly the dwell can still land a counted
  // probe in the gap before it retunes.
  const uint32_t n = (dwell + probe_ms - 1) / probe_ms + 1;

  // Bounded. A warmup longer than the measurement it protects would be a worse
  // trade than the bias it removes - at SF12 each probe is seconds.
  const uint32_t kMaxWarmup = 6;
  return static_cast<uint16_t>(n > kMaxWarmup ? kMaxWarmup : n);
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

void Series::add(int16_t v) {
  if (count == 0) {
    min = v;
    max = v;
  } else {
    if (v < min) min = v;
    if (v > max) max = v;
  }
  sum += v;
  ++count;
}

int16_t Series::mean() const {
  if (count == 0) return kI16NotAvailable;
  // Round to nearest, away from zero, so a mean of -49.5 reports as -50 rather than
  // being quietly truncated toward zero the way integer division would do it.
  const int32_t n = static_cast<int32_t>(count);
  const int32_t r = (sum >= 0) ? (sum + n / 2) / n : (sum - n / 2) / n;
  return static_cast<int16_t>(r);
}

uint16_t TestPointStats::per_pct100() const {
  if (probes_sent == 0) return UINT16_MAX;  // no samples, not "0% loss"

  const uint32_t lost = (echoes_received >= probes_sent)
                            ? 0U
                            : static_cast<uint32_t>(probes_sent - echoes_received);
  return static_cast<uint16_t>((lost * 10000UL + probes_sent / 2) / probes_sent);
}

}  // namespace rangetest
