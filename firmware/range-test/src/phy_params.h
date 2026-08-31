// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R3 - the spec 12.1 fixed settings, the swept test point, and the D33 power clamp.
//
// Arduino-free on purpose: the clamp is the one piece of this firmware that must be
// right before anything is transmitted, and it is pure arithmetic, so it is host
// tested (test/test_phy_params). A clamp that can only be exercised on target is a
// clamp first exercised by putting power on the air.
//
// THIS FILE IS THE ONE PLACE THIS BINARY DEPARTS FROM spec 12.1's "LoRa PHY
// parameters are not runtime-configurable" rule. Task guardrail 4: that is the point
// of a sweep and it is confined here. Nothing that reads a PHY parameter at runtime
// may migrate into node firmware.

#pragma once

#include <cstddef>
#include <cstdint>

namespace rangetest {

// ---------------------------------------------------------------------------
// spec 12.1 - fixed, not swept.
// ---------------------------------------------------------------------------

inline constexpr uint16_t kSyncWord      = 0x1424;  // SX126x private, not LoRaWAN
inline constexpr bool     kExplicitHeader = true;   // required by spec 2.1
inline constexpr bool     kCrcEnabled     = true;   // required by spec 2.1

// spec 12.1 gives BW 125 kHz as the starting point. Held here rather than in the
// swept set: R3 lists frequency, SF, CR, TX power and payload size as swept, and BW
// is not among them.
inline constexpr uint16_t kBandwidthKhz10 = 1250;  // tenths of a kHz -> 125.0 kHz

// Not fixed by spec 12.1. RadioLib's default is 8 symbols; stated explicitly so the
// settings dump records it and a later change is visible in a diff. spec 17.1
// reserves asymmetric preamble for duty-cycled nodes - out of scope for pass 1.
inline constexpr uint16_t kPreambleSymbols = 8;

// ---------------------------------------------------------------------------
// D33 / spec 18.1 - the power ceiling, in tenths of a dB throughout.
//
// Integer tenths rather than float so the clamp is exact and host-testable. A
// rounding error here is a transmission above the ceiling.
// ---------------------------------------------------------------------------

// spec 18.1: the 15.249 provisions permit roughly 0.75 mW EIRP, about -1 dBm.
inline constexpr int16_t kEirpCeilingDbm10 = -10;  // -1.0 dBm EIRP

// SX1262 output range, the values RadioLib accepts for setOutputPower().
inline constexpr int8_t kSx1262MinDbm = -9;
inline constexpr int8_t kSx1262MaxDbm = 22;

// D33 standing condition 1 and task R4: conducted power and antenna gain are
// RECORDED SEPARATELY. The ceiling is EIRP; a single combined figure cannot be
// audited later. This is a struct, not two loose numbers, so they cannot drift apart
// between the clamp and the CSV.
struct PowerPoint {
  int8_t  conducted_dbm    = kSx1262MinDbm;
  int16_t antenna_gain_dbi10 = 0;  // tenths of a dBi, as measured/nameplate
};

enum class ClampResult : uint8_t {
  Ok = 0,          // requested power is at or below the ceiling; used unchanged
  Clamped,         // requested exceeded the ceiling; reduced to it
  BelowRadioFloor, // the ceiling itself is below the SX1262 minimum - see below
};

// Returns the greatest conducted power that keeps EIRP at or below the D33 ceiling,
// given the antenna gain in tenths of a dBi.
//
// Rounds DOWN (toward -inf), never to nearest: rounding a half-dB up is a
// transmission above the ceiling, which is the one direction that matters.
int8_t conducted_ceiling_dbm(int16_t antenna_gain_dbi10);

// Clamps `requested` against the D33 ceiling and the SX1262's own range.
//
// BelowRadioFloor is a real, reportable outcome rather than a silent floor: with a
// high-gain antenna the EIRP ceiling can sit below -9 dBm, and the radio then cannot
// legally transmit at all on that antenna. Task guardrail 3 says the clamp is in
// code, not in operator discipline; repo rule 4 says nothing is discarded silently.
// The caller reports it and does not transmit.
ClampResult clamp_conducted(int8_t requested_dbm, int16_t antenna_gain_dbi10,
                            PowerPoint* out);

// ---------------------------------------------------------------------------
// R4 - the test point. A sweep visits these; spec 12.1's fixed settings do not vary.
// ---------------------------------------------------------------------------

struct TestPoint {
  uint32_t   freq_hz      = 0;
  uint8_t    sf           = 0;   // 7..12
  uint8_t    cr_denom     = 5;   // 4/N, N in 5..8
  PowerPoint power{};
  uint8_t    payload_len  = 0;
};

// PROVISIONAL STARTING POINT - NOT A DECISION.
//
// D1 is open and bounded (Decision Register 2.1). The frequency in particular SHALL
// NOT be fixed until M20's ambient survey has run at both ends (spec 12.1), which is
// task R8 and has not happened. 915.0 MHz is the band centre and a placeholder for
// getting two boards to talk on the bench, nothing more.
//
// SF7 / CR 4:5 is the fastest configuration and the one spec 18.1's link budget is
// computed at, so it is where a sweep starts having the most margin in hand.
inline constexpr uint32_t kProvisionalFreqHz = 915000000UL;
inline constexpr uint8_t  kProvisionalSf      = 7;
inline constexpr uint8_t  kProvisionalCrDenom = 5;

// Renders the active configuration as text, one `key=value` per line, into a
// caller-owned buffer. Returns the number of bytes written, excluding the
// terminator, or 0 if the buffer is too small.
//
// R3's acceptance criterion: a settings dump on the serial console at boot, so a CSV
// can be correlated with the configuration that produced it. Host-tested, because a
// dump that silently truncates correlates a CSV with a lie.
size_t format_settings(const TestPoint& tp, const char* board_name,
                       char* out, size_t cap);

}  // namespace rangetest
