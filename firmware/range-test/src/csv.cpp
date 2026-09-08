// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "csv.h"

#include <cstdio>
#include <cstring>

namespace rangetest {
namespace {

// THE SCHEMA. One string, so header order and row order are written next to each
// other and a column cannot be added to one without the other being obviously wrong.
//
// D33 standing condition 1 is why conducted_dbm and antenna_gain_dbi10 are two
// columns: the ceiling is EIRP, and a combined figure cannot be audited later.
const char kHeader[] =
    "position,tp_index,freq_hz,sf,cr_denom,conducted_dbm,antenna_gain_dbi10,"
    "payload_len,"
    "probes_sent,echoes_recv,resp_heard,per_pct100,"
    "init_rssi_mean10,init_rssi_min10,init_rssi_max10,"
    "init_snr_mean10,init_snr_min10,init_snr_max10,"
    "resp_rssi_mean10,resp_rssi_min10,resp_rssi_max10,"
    "resp_snr_mean10,resp_snr_min10,resp_snr_max10,"
    "phy_crc_err,foreign,filler_err";

// An empty Series has no min or max, and printing its zero-initialized ones would
// invent readings. The sentinel says "no samples" (spec 4.6 / repo rule 6).
int16_t series_min(const Series& s) { return s.empty() ? kI16NotAvailable : s.min; }
int16_t series_max(const Series& s) { return s.empty() ? kI16NotAvailable : s.max; }

}  // namespace

size_t csv_field_count() {
  size_t n = 1;
  for (const char* p = kHeader; *p != '\0'; ++p) {
    if (*p == ',') ++n;
  }
  return n;
}

size_t csv_header(char* out, size_t cap) {
  if (out == nullptr) return 0;
  const size_t n = std::strlen(kHeader);
  if (n + 1 > cap) return 0;
  std::memcpy(out, kHeader, n + 1);
  return n;
}

size_t csv_row(const CsvRow& r, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;

  const int n = std::snprintf(
      out, cap,
      "%u,%u,%lu,%u,%u,%d,%d,%u,"
      "%u,%u,%u,%u,"
      "%d,%d,%d,"
      "%d,%d,%d,"
      "%d,%d,%d,"
      "%d,%d,%d,"
      "%u,%u,%u",
      static_cast<unsigned>(r.position_id),
      static_cast<unsigned>(r.tp_index),
      static_cast<unsigned long>(r.tp.freq_hz),
      static_cast<unsigned>(r.tp.sf),
      static_cast<unsigned>(r.tp.cr_denom),
      static_cast<int>(r.tp.power.conducted_dbm),
      static_cast<int>(r.tp.power.antenna_gain_dbi10),
      static_cast<unsigned>(r.tp.payload_len),

      static_cast<unsigned>(r.stats.probes_sent),
      static_cast<unsigned>(r.stats.echoes_received),
      static_cast<unsigned>(r.resp_probes_heard),
      static_cast<unsigned>(r.stats.per_pct100()),

      static_cast<int>(r.stats.init_rssi_dbm10.mean()),
      static_cast<int>(series_min(r.stats.init_rssi_dbm10)),
      static_cast<int>(series_max(r.stats.init_rssi_dbm10)),

      static_cast<int>(r.stats.init_snr_db10.mean()),
      static_cast<int>(series_min(r.stats.init_snr_db10)),
      static_cast<int>(series_max(r.stats.init_snr_db10)),

      static_cast<int>(r.stats.resp_rssi_dbm10.mean()),
      static_cast<int>(series_min(r.stats.resp_rssi_dbm10)),
      static_cast<int>(series_max(r.stats.resp_rssi_dbm10)),

      static_cast<int>(r.stats.resp_snr_db10.mean()),
      static_cast<int>(series_min(r.stats.resp_snr_db10)),
      static_cast<int>(series_max(r.stats.resp_snr_db10)),

      static_cast<unsigned>(r.stats.phy_crc_errors),
      static_cast<unsigned>(r.stats.foreign_frames),
      static_cast<unsigned>(r.stats.filler_mismatch));

  if (n < 0) return 0;
  if (static_cast<size_t>(n) >= cap) return 0;   // never a truncated row
  return static_cast<size_t>(n);
}

}  // namespace rangetest
