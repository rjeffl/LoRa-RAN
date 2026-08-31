// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R6 / R7 - the initiator's CSV.
//
// A formatter rather than a run of Serial.print calls, for one reason: the header and
// the row must not drift apart. A CSV whose header stops matching its columns is
// silently wrong in the worst way - it parses, it plots, and every conclusion drawn
// from it is about the wrong quantity. There is a host test asserting the two have
// the same field count, and it is the most valuable test in this file.
//
// R7 commits these traces to docs/rangetest/data/ as the evidence for D1 and the input
// to W7's airtime regeneration, so the schema here is the one a reader in eighteen
// months has to be able to interpret.
//
// Arduino-free: host tested, and the same code formats a row on target.

#pragma once

#include <cstddef>
#include <cstdint>

#include "phy_params.h"
#include "sweep.h"

namespace rangetest {

// Everything one row reports. Assembled by the caller so the formatter has no opinion
// about where the numbers came from.
struct CsvRow {
  uint16_t position_id = 0;
  uint16_t tp_index    = 0;

  TestPoint      tp{};
  TestPointStats stats{};

  // What the RESPONDER heard at this test point, recovered from its echoes. Distinct
  // from stats.resp_* only in that this is the count, not the signal level.
  //
  // kU16NotAvailable when the responder never reported - which is itself the reading:
  // it means no echo arrived at all, so nothing is known about the downlink.
  uint16_t resp_probes_heard = kU16NotAvailable;
};

// All tenths-of-a-unit columns are emitted as integers with a `10` suffix in the
// name - `init_rssi_mean10` is tenths of a dBm. No decimal points and no floats: the
// values are computed in tenths and printing them as decimals would introduce a
// rounding step between the measurement and the file.
//
// Writes a terminated string. Returns bytes written excluding the terminator, or 0 if
// the buffer is too small - never a truncated line, because a truncated CSV row is a
// row that still parses and is wrong.
size_t csv_header(char* out, size_t cap);
size_t csv_row(const CsvRow& row, char* out, size_t cap);

// Number of comma-separated fields the header declares. Exposed so the row formatter
// can be checked against it rather than the two being compared by eye.
size_t csv_field_count();

// Longest line this formatter can emit, so callers can size a buffer once.
//
// Sized for the HEADER, not the row: 27 columns of names run to 349 characters where
// the widest row of numbers is around 200. The first value here was 320 - chosen for
// the row - and csv_header() correctly refused to emit rather than truncating, which
// is how it was caught. 448 leaves room for a few more columns before this has to be
// revisited, and a host test pins the header against it.
inline constexpr size_t kCsvMaxLine = 448;

}  // namespace rangetest
