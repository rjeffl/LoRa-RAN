// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R3 - see phy_params.h. No Arduino, no RadioLib: host tested.

#include "phy_params.h"

#include <cstdio>

namespace rangetest {
namespace {

// Floor division toward negative infinity. C++ integer division truncates toward
// zero, so -35 / 10 is -3 where the floor is -4. Rounding a fractional dB up is a
// transmission above the D33 ceiling; this is the reason the clamp is host tested.
int16_t floor_div10(int16_t v) {
  return static_cast<int16_t>((v >= 0) ? (v / 10) : -(((-v) + 9) / 10));
}

}  // namespace

int8_t conducted_ceiling_dbm(int16_t antenna_gain_dbi10) {
  // spec 18.1: EIRP = conducted + antenna gain, so the conducted ceiling is the EIRP
  // ceiling less the gain. With the fitted 3.0 dBi antenna: -1.0 - 3.0 = -4.0 dBm
  // conducted, which is spec 18.2's working point and what the 2026-09-04 walk ran at.
  //
  // No feedline term, deliberately. Spec 18.2 / M21 findings 7.2 assume 0 dB of feedline
  // loss on the COMPLIANCE side, so omitting it here is that assumption expressed in
  // code. GateLink's real path crosses two bulkheads and loses 0.5-1.5 dB; crediting that
  // would raise the permitted conducted power. Do not "fix" this.
  const int16_t conducted_dbm10 =
      static_cast<int16_t>(kEirpCeilingDbm10 - antenna_gain_dbi10);

  const int16_t floored = floor_div10(conducted_dbm10);

  if (floored > kSx1262MaxDbm) return kSx1262MaxDbm;
  // Deliberately NOT floored up to kSx1262MinDbm here. A ceiling below the radio's
  // minimum is information the caller has to act on, and clamp_conducted reports it.
  if (floored < -128) return -128;
  return static_cast<int8_t>(floored);
}

ClampResult clamp_conducted(int8_t requested_dbm, int16_t antenna_gain_dbi10,
                            PowerPoint* out) {
  const int8_t ceiling = conducted_ceiling_dbm(antenna_gain_dbi10);

  if (out != nullptr) {
    out->antenna_gain_dbi10 = antenna_gain_dbi10;
  }

  // The ceiling is below anything the SX1262 can emit. Not a clamp - a refusal.
  // Task guardrail 3: the cap is enforced in code, and the honest answer here is
  // that this antenna cannot be used at this ceiling, not a quiet -9 dBm.
  if (ceiling < kSx1262MinDbm) {
    if (out != nullptr) out->conducted_dbm = kSx1262MinDbm;
    return ClampResult::BelowRadioFloor;
  }

  int8_t      value  = requested_dbm;
  ClampResult result = ClampResult::Ok;

  if (value > ceiling) {
    value  = ceiling;
    result = ClampResult::Clamped;
  }
  // The sweep starts at the bottom of the SX1262's range (task guardrail 3); a
  // request below it is a caller bug, corrected rather than passed to the driver.
  if (value < kSx1262MinDbm) {
    value = kSx1262MinDbm;
  }

  if (out != nullptr) out->conducted_dbm = value;
  return result;
}

size_t format_settings(const TestPoint& tp, const char* board_name,
                       char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;

  // snprintf returns what it WOULD have written. Anything at or over cap means the
  // dump was truncated, and a truncated settings dump correlates a CSV with a
  // configuration that was not the one used - so it returns 0 rather than a short
  // string (R3).
  const int n = std::snprintf(
      out, cap,
      "board=%s\n"
      "freq_hz=%lu\n"
      "sf=%u\n"
      "bw_khz=%u.%u\n"
      "cr=4/%u\n"
      "conducted_dbm=%d\n"
      "antenna_gain_dbi=%d.%d\n"
      "eirp_ceiling_dbm=%d.%d\n"
      "sync_word=0x%04X\n"
      "explicit_header=%d\n"
      "crc=%d\n"
      "preamble_symbols=%u\n"
      "payload_len=%u\n",
      (board_name != nullptr) ? board_name : "?",
      static_cast<unsigned long>(tp.freq_hz),
      static_cast<unsigned>(tp.sf),
      static_cast<unsigned>(kBandwidthKhz10 / 10),
      static_cast<unsigned>(kBandwidthKhz10 % 10),
      static_cast<unsigned>(tp.cr_denom),
      static_cast<int>(tp.power.conducted_dbm),
      static_cast<int>(tp.power.antenna_gain_dbi10 / 10),
      static_cast<int>((tp.power.antenna_gain_dbi10 < 0
                            ? -tp.power.antenna_gain_dbi10
                            : tp.power.antenna_gain_dbi10) % 10),
      static_cast<int>(kEirpCeilingDbm10 / 10),
      static_cast<int>((kEirpCeilingDbm10 < 0 ? -kEirpCeilingDbm10
                                              : kEirpCeilingDbm10) % 10),
      static_cast<unsigned>(kSyncWord),
      kExplicitHeader ? 1 : 0,
      kCrcEnabled ? 1 : 0,
      static_cast<unsigned>(kPreambleSymbols),
      static_cast<unsigned>(tp.payload_len));

  if (n < 0) return 0;
  if (static_cast<size_t>(n) >= cap) return 0;
  return static_cast<size_t>(n);
}

}  // namespace rangetest
