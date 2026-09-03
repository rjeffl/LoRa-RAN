// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R8 - see survey.h.

#include "survey.h"

#include <cstdio>
#include <cstring>

namespace rangetest {
namespace {

// THE SCHEMA, as one string, for the reason csv.cpp gives: header order and row order
// are written next to each other and a column cannot be added to one alone.
const char kSurveyHeader[] =
    "site_index,site_name,bin_index,freq_hz,passes,samples,"
    "peak_dbm10,mean_dbm10,floor_dbm10,dropped";

// Named, not numbered. Order matches a loop of the property; the names are what a
// reader needs and the indices are only how NVS keys them.
const char* const kSiteNames[kSurveySiteCount] = {
    "bridge-house",      // 0 - spec 12.1's first required location
    "gatelink-gate",     // 1 - GateLink, the gate controller
    "weather-island",    // 2 - front island, the existing weather station
    "welllink-well",     // 3 - WellLink
    "irrigation-pump",   // 4 - future pump control and monitoring
    "hopyard-lower",     // 5 - future remote weather station
    "propane-tank",      // 6 - future propane level monitor
};

constexpr uint32_t kBlobMagic   = 0x4C525338UL;  // "LRS8" - LRAN Range Survey, R8
constexpr uint16_t kBlobVersion = 1;

void put_u16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void put_u32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// Two's complement on the wire, assembled explicitly. Repo rule 1 is about structs,
// but a signed field written by casting through a union has the same problem.
void put_i16(uint8_t* p, int16_t v) { put_u16(p, static_cast<uint16_t>(v)); }

uint16_t get_u16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

uint32_t get_u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

int16_t get_i16(const uint8_t* p) { return static_cast<int16_t>(get_u16(p)); }

// An empty Series has no min or max; printing its zero-initialized ones would invent
// a reading of 0.0 dBm, which in this trace would be a bin apparently 100 dB hotter
// than its neighbours (spec 4.6 / repo rule 6).
int16_t series_min(const Series& s) { return s.empty() ? kI16NotAvailable : s.min; }
int16_t series_max(const Series& s) { return s.empty() ? kI16NotAvailable : s.max; }

}  // namespace

// ---------------------------------------------------------------------------
// Bin plan
// ---------------------------------------------------------------------------

const char* survey_site_name(size_t site) {
  if (site >= kSurveySiteCount) return "unknown";
  return kSiteNames[site];
}

uint32_t survey_bin_freq_hz(size_t bin_index) {
  if (bin_index >= kSurveyBinCount) return 0;
  return kSurveyStartHz + static_cast<uint32_t>(bin_index) * kSurveyStepHz;
}

size_t survey_bin_of(uint32_t freq_hz) {
  if (freq_hz < kSurveyStartHz) return kSurveyBinCount;
  const uint32_t offset = freq_hz - kSurveyStartHz;
  // Round to nearest bin rather than truncating: a candidate at 914.9 MHz belongs to
  // the 915.0 bin, and reporting it against 914.8 would put the noise figure next to
  // the wrong channel in a D1 argument.
  const uint32_t bin = (offset + kSurveyStepHz / 2) / kSurveyStepHz;
  if (bin >= kSurveyBinCount) return kSurveyBinCount;
  return static_cast<size_t>(bin);
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

uint16_t survey_samples_per_dwell(const SurveyPlan& plan) {
  if (plan.dwell_ms <= plan.settle_ms) return 0;
  const uint16_t listening = static_cast<uint16_t>(plan.dwell_ms - plan.settle_ms);
  const uint16_t interval  = plan.sample_interval_ms > 0 ? plan.sample_interval_ms : 1;
  // The sample at t=0 of the listening window counts, hence the +1.
  return static_cast<uint16_t>(listening / interval + 1);
}

uint32_t survey_pass_duration_ms(const SurveyPlan& plan) {
  return static_cast<uint32_t>(plan.bins) * plan.dwell_ms;
}

// ---------------------------------------------------------------------------
// Accumulator
// ---------------------------------------------------------------------------

void Survey::reset() {
  for (size_t i = 0; i < kSurveyBinCount; ++i) bins_[i] = SurveyBin{};
  passes_ = 0;
}

void Survey::add_sample(size_t bin_index, int16_t rssi_dbm10) {
  if (bin_index >= kSurveyBinCount) return;
  SurveyBin& b = bins_[bin_index];
  if (b.rssi_dbm10.count >= kSurveyMaxSamplesPerBin) {
    ++b.dropped;   // counted, never silently absorbed
    return;
  }
  b.rssi_dbm10.add(rssi_dbm10);
}

const SurveyBin& Survey::bin(size_t i) const {
  static const SurveyBin kEmpty{};
  if (i >= kSurveyBinCount) return kEmpty;
  return bins_[i];
}

size_t Survey::bins_sampled() const {
  size_t n = 0;
  for (size_t i = 0; i < kSurveyBinCount; ++i) {
    if (!bins_[i].rssi_dbm10.empty()) ++n;
  }
  return n;
}

size_t Survey::quietest_bin() const {
  size_t  best = kSurveyBinCount;
  int16_t best_mean = 0;
  for (size_t i = 0; i < kSurveyBinCount; ++i) {
    if (bins_[i].rssi_dbm10.empty()) continue;
    const int16_t m = bins_[i].rssi_dbm10.mean();
    if (best == kSurveyBinCount || m < best_mean) {
      best = i;
      best_mean = m;
    }
  }
  return best;
}

size_t Survey::loudest_bin() const {
  size_t  best = kSurveyBinCount;
  int16_t best_peak = 0;
  for (size_t i = 0; i < kSurveyBinCount; ++i) {
    if (bins_[i].rssi_dbm10.empty()) continue;
    const int16_t p = bins_[i].rssi_dbm10.max;
    if (best == kSurveyBinCount || p > best_peak) {
      best = i;
      best_peak = p;
    }
  }
  return best;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

size_t Survey::serialize(const SurveyPlan& plan, uint8_t* out, size_t cap) const {
  if (out == nullptr || cap < kBlobMaxLen) return 0;

  put_u32(out + 0, kBlobMagic);
  put_u16(out + 4, kBlobVersion);
  put_u16(out + 6, static_cast<uint16_t>(kSurveyBinCount));
  put_u32(out + 8, plan.start_hz);
  put_u32(out + 12, plan.step_hz);
  // One number for the whole run, so it belongs in the header and not in every bin.
  put_u32(out + 16, passes_);

  uint8_t* p = out + kBlobHeaderLen;
  for (size_t i = 0; i < kSurveyBinCount; ++i) {
    const SurveyBin& b = bins_[i];
    put_u32(p + 0, static_cast<uint32_t>(b.rssi_dbm10.sum));
    put_i16(p + 4, b.rssi_dbm10.min);
    put_i16(p + 6, b.rssi_dbm10.max);
    put_u16(p + 8, b.rssi_dbm10.count);
    // Truncated to 16 bits deliberately: `dropped` only becomes non-zero after 60000
    // samples in one bin, and a run that overflows this too has other problems.
    put_u16(p + 10, b.dropped > 0xFFFFu ? 0xFFFFu
                                        : static_cast<uint16_t>(b.dropped));
    p += kBlobEntryLen;
  }

  return kBlobMaxLen;
}

bool Survey::deserialize(const uint8_t* in, size_t len, SurveyPlan* out_plan) {
  reset();
  if (in == nullptr || len < kBlobMaxLen) return false;
  if (get_u32(in + 0) != kBlobMagic) return false;
  if (get_u16(in + 4) != kBlobVersion) return false;
  if (get_u16(in + 6) != static_cast<uint16_t>(kSurveyBinCount)) return false;

  if (out_plan != nullptr) {
    out_plan->start_hz = get_u32(in + 8);
    out_plan->step_hz  = get_u32(in + 12);
    out_plan->bins     = static_cast<uint16_t>(kSurveyBinCount);
  }
  passes_ = get_u32(in + 16);

  const uint8_t* p = in + kBlobHeaderLen;
  for (size_t i = 0; i < kSurveyBinCount; ++i) {
    SurveyBin& b = bins_[i];
    b.rssi_dbm10.sum   = static_cast<int32_t>(get_u32(p + 0));
    b.rssi_dbm10.min   = get_i16(p + 4);
    b.rssi_dbm10.max   = get_i16(p + 6);
    b.rssi_dbm10.count = get_u16(p + 8);
    b.dropped          = get_u16(p + 10);
    p += kBlobEntryLen;
  }
  return true;
}

// ---------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------

size_t survey_csv_field_count() {
  size_t n = 1;
  for (const char* q = kSurveyHeader; *q != '\0'; ++q) {
    if (*q == ',') ++n;
  }
  return n;
}

size_t survey_csv_header(char* out, size_t cap) {
  if (out == nullptr) return 0;
  const size_t n = std::strlen(kSurveyHeader);
  if (n + 1 > cap) return 0;
  std::memcpy(out, kSurveyHeader, n + 1);
  return n;
}

size_t survey_csv_row(const SurveyCsvRow& row, char* out, size_t cap) {
  if (out == nullptr || row.bin == nullptr) return 0;

  const Series& s = row.bin->rssi_dbm10;
  const int n = std::snprintf(
      out, cap,
      "%u,%s,%u,%lu,%lu,%u,%d,%d,%d,%lu",
      static_cast<unsigned>(row.site),
      survey_site_name(row.site),
      static_cast<unsigned>(row.bin_index),
      static_cast<unsigned long>(row.freq_hz),
      static_cast<unsigned long>(row.passes),
      static_cast<unsigned>(s.count),
      static_cast<int>(series_max(s)),     // peak hold
      static_cast<int>(s.mean()),
      static_cast<int>(series_min(s)),     // floor
      static_cast<unsigned long>(row.bin->dropped));

  // Never a truncated line: a short CSV row still parses and is wrong.
  if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
  return static_cast<size_t>(n);
}

}  // namespace rangetest
