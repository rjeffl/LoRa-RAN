// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Handoff 6 requirement 7 / M21 findings 7.5 - the PA configuration actually applied,
// as a record a trace can carry.
//
// WHY THIS FILE EXISTS AT ALL. RadioLib's SX1262::setOutputPower(power) forwards to
// setOutputPower(power, true), and the `true` selects paDutyCycle/hpMax/paVal from a
// 32-entry table indexed by `power + 9`. Neither the flag nor the entry is observable:
// `paOptTable` is file-static in SX1262.cpp, and the SX1262's PA config is written by
// the SetPaConfig COMMAND, not to a register that can be read back. So a trace could
// say what power was requested and never what PA configuration produced it.
//
// M21 findings 7.5 budgets ~3 dB between a requested power and the power at the
// connector, and 7.6 reads the sanity check against that budget. A divergence is only
// a defect to find rather than noise to absorb if the configuration is on the record.
//
// THIS IS A MIRROR, AND A MIRROR IS A LOAD-BEARING PREMISE. The premise is "these 32
// entries are RadioLib 7.7.1's paOptTable". Per root CLAUDE.md, the check that would
// falsify it is named and runnable, not asserted in prose:
//
//     python3 tools/rangetest/check_pa_table.py
//
// It parses the pinned RadioLib source in .pio/libdeps and diffs it against the table
// in pa_config.cpp. Run it after any RadioLib version change. The version is pinned in
// every platformio.ini (repo rule 9), which is what makes a mirror defensible at all -
// but a pin is a promise about the future and this is the check on it.
//
// NOTHING HERE CONFIGURES THE RADIO. It reports what the driver will do with a given
// power, so the report is host-testable; radio_link.cpp still calls RadioLib. Two
// code paths computing the same thing is the cost of RadioLib not exposing its own,
// and it is why check_pa_table.py exists.

#pragma once

#include <cstddef>
#include <cstdint>

namespace rangetest {

// The RadioLib version this table was mirrored from. Printed at boot, so a trace
// records which driver's PA table its numbers describe rather than leaving a reader
// to guess from the commit date.
inline constexpr char kPaTableRadioLibVersion[] = "7.7.1";

// What RadioLib's one-argument SX1262::setOutputPower() passes for `optimize`.
//
// Named rather than left implicit, and passed EXPLICITLY at the call sites in
// radio_link.cpp. The flag changes the emitted power, so "whatever the overload
// defaults to" is a compliance-relevant decision made by a library default. It is
// this project's decision now, and it is logged.
inline constexpr bool kPaOptimize = true;

// The datasheet-default PA configuration, used when `optimize` is false. RadioLib
// writes these two constants literally (SX1262.cpp), and paVal is then the requested
// power itself.
inline constexpr uint8_t kPaDutyCycleDefault = 0x04;
inline constexpr uint8_t kPaHpMaxDefault     = 0x07;

struct PaConfig {
  int8_t  pa_val        = 0;
  uint8_t pa_duty_cycle = 0;
  uint8_t hp_max        = 0;
  bool    optimize      = kPaOptimize;

  // False when `conducted_dbm` is outside the SX1262's -9..+22 range. RadioLib's
  // checkOutputPower() rejects such a value BEFORE indexing the table, so there is no
  // entry to report - and a mirror that indexed anyway would read out of bounds while
  // documenting the code that does not. The clamp in phy_params.cpp is what keeps
  // this from arising; this field is the guard, not the plan.
  bool    in_range      = false;
};

// The configuration RadioLib will apply for `conducted_dbm` at this `optimize`
// setting. Pure arithmetic and a table lookup - no radio, no Arduino.
PaConfig pa_config_for(int8_t conducted_dbm, bool optimize);

// Renders `cfg` as `key=value` lines into a caller-owned buffer, the same shape
// format_settings() uses. Returns bytes written excluding the terminator, or 0 if the
// buffer is too small - it refuses to truncate for the reason format_settings does.
//
// THE FORMAT IS LOAD-BEARING FOR capture.py: `^[a-z][a-z0-9_]*=\S*$` lines emitted
// before the CSV header are collected into the trace's header block. No spaces in a
// value, and no line that is not key=value.
size_t format_pa_config(const PaConfig& cfg, char* out, size_t cap);

}  // namespace rangetest
