// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// See pa_config.h. No Arduino, no RadioLib: host tested.

#include "pa_config.h"

#include <cstdio>

#include "phy_params.h"

namespace rangetest {
namespace {

// MIRROR OF RadioLib 7.7.1 SX1262.cpp `paOptTable`, in its own order.
//
// Do not reorder, reformat into columns, or "tidy" the values: check_pa_table.py
// parses BOTH this table and RadioLib's, and the diff is only meaningful while the
// two are the same 32 entries in the same order. Upstream derived them by
// measurement (RadioLib issue 1628, radiolib-org/power-tests), so there is no formula
// to compute them from and no way to spot a wrong entry by eye.
//
// Indexed by `conducted_dbm + 9`: entry 0 is -9 dBm, entry 31 is +22 dBm.
struct TableEntry {
  uint8_t pa_duty_cycle;
  uint8_t hp_max;
  int8_t  pa_val;
};

constexpr TableEntry kPaOptTable[32] = {
    {2, 2, -5},  {2, 1, 0},   {1, 1, 3},   {1, 2, 0},   {1, 1, 6},   {1, 2, 3},
    {2, 2, 2},   {4, 1, 6},   {1, 1, 11},  {2, 1, 11},  {1, 1, 14},  {2, 1, 14},
    {1, 1, 20},  {1, 1, 22},  {2, 2, 11},  {3, 1, 21},  {1, 2, 17},  {4, 2, 13},
    {1, 2, 20},  {1, 2, 22},  {2, 2, 21},  {3, 2, 21},  {1, 4, 19},  {1, 4, 20},
    {3, 3, 20},  {2, 5, 19},  {1, 6, 22},  {2, 5, 22},  {3, 5, 22},  {3, 6, 22},
    {4, 6, 22},  {4, 7, 22},
};

// The table spans the SX1262's whole output range, one entry per dB. If either bound
// ever moves, the index arithmetic below is wrong before any test notices.
static_assert(kSx1262MaxDbm - kSx1262MinDbm + 1 ==
                  static_cast<int>(sizeof(kPaOptTable) / sizeof(kPaOptTable[0])),
              "paOptTable must cover kSx1262MinDbm..kSx1262MaxDbm, one entry per dB");
static_assert(kSx1262MinDbm == -9,
              "the table index is `power + 9` in RadioLib; it is derived from "
              "kSx1262MinDbm here and the two must agree");

}  // namespace

PaConfig pa_config_for(int8_t conducted_dbm, bool optimize) {
  PaConfig cfg;
  cfg.optimize = optimize;

  // RadioLib's checkOutputPower() rejects out-of-range BEFORE the table is indexed,
  // so the honest report is "no entry", not entry 0. Repo rule 6 in spirit: a
  // consumer must be able to tell an unavailable reading from a real one.
  if (conducted_dbm < kSx1262MinDbm || conducted_dbm > kSx1262MaxDbm) {
    cfg.in_range = false;
    return cfg;
  }
  cfg.in_range = true;

  if (!optimize) {
    // SX1262.cpp's non-optimized branch, written out rather than referenced: paVal is
    // the requested power itself and the two PA constants are literals.
    cfg.pa_val        = conducted_dbm;
    cfg.pa_duty_cycle = kPaDutyCycleDefault;
    cfg.hp_max        = kPaHpMaxDefault;
    return cfg;
  }

  const TableEntry& e = kPaOptTable[conducted_dbm - kSx1262MinDbm];
  cfg.pa_val        = e.pa_val;
  cfg.pa_duty_cycle = e.pa_duty_cycle;
  cfg.hp_max        = e.hp_max;
  return cfg;
}

size_t format_pa_config(const PaConfig& cfg, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;

  // `pa_entry=none` rather than an omitted block. A missing line reads as an older
  // firmware; an explicit `none` reads as a power the driver would have refused,
  // which is a different fault and one worth seeing in a trace.
  const int n =
      cfg.in_range
          ? std::snprintf(out, cap,
                          "pa_optimize=%d\n"
                          "pa_duty_cycle=%u\n"
                          "pa_hp_max=%u\n"
                          "pa_val=%d\n"
                          "pa_table=RadioLib-%s-paOptTable\n",
                          cfg.optimize ? 1 : 0,
                          static_cast<unsigned>(cfg.pa_duty_cycle),
                          static_cast<unsigned>(cfg.hp_max),
                          static_cast<int>(cfg.pa_val),
                          kPaTableRadioLibVersion)
          : std::snprintf(out, cap,
                          "pa_optimize=%d\n"
                          "pa_entry=none\n"
                          "pa_table=RadioLib-%s-paOptTable\n",
                          cfg.optimize ? 1 : 0, kPaTableRadioLibVersion);

  if (n < 0) return 0;
  // Refuses to truncate, for format_settings()'s reason: a partial record correlates
  // a trace with a configuration that was not the one used.
  if (static_cast<size_t>(n) >= cap) return 0;
  return static_cast<size_t>(n);
}

}  // namespace rangetest
