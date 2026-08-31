// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R4 - the test point, the sweep plan, and what the sweep measures.
//
// Arduino-free and radio-free on purpose. Everything here is enumeration and
// arithmetic, so the sweep's shape, its duration and its statistics are host tested;
// main.cpp owns the radio and the clock. A sweep whose PER arithmetic is only
// exercised by walking 500 ft is a sweep whose PER arithmetic is not exercised.

#pragma once

#include <cstddef>
#include <cstdint>

#include "phy_params.h"
#include "sentinels.h"

namespace rangetest {

// ---------------------------------------------------------------------------
// The plan. Axes, not a hand-written list of points.
// ---------------------------------------------------------------------------

struct SweepPlan {
  const uint32_t* freqs_hz    = nullptr;  size_t n_freq    = 0;
  const uint8_t*  sfs         = nullptr;  size_t n_sf      = 0;
  const uint8_t*  cr_denoms   = nullptr;  size_t n_cr      = 0;
  const int8_t*   powers_dbm  = nullptr;  size_t n_power   = 0;
  const uint8_t*  payloads    = nullptr;  size_t n_payload = 0;

  uint16_t probes_per_point = 8;

  // Time allowed for an echo before the probe is scored lost. Computed from airtime
  // rather than fixed: the round trip is ~20x longer at SF12 than at SF7, and a
  // single constant is either far too slow at SF7 or scores every SF12 probe lost.
  uint16_t echo_timeout_margin_ms = 250;

  // Quiet time between probes. Not politeness - spec 12.3 notes the channel has
  // third-party occupants, and back-to-back probes at SF12 would hold it for whole
  // seconds at a time.
  uint16_t inter_probe_gap_ms = 50;
};

// Total test points: the product of the axes.
size_t sweep_point_count(const SweepPlan& plan);

// Materializes point `index`, applying the D33 clamp to its power.
//
// Axis order, fastest varying first: payload, power, CR, SF, frequency. Radio
// reconfiguration is cheap, so the ordering is chosen for the CSV reader instead -
// rows group naturally by radio configuration, and the slowest axis (SF, which
// dominates airtime) changes least often so a partial sweep still spans the
// interesting range of payload and power.
//
// Returns false if `index` is out of range, or if the D33 ceiling for this antenna
// is below the SX1262 floor - in which case there is no legal power and the caller
// must not transmit (task guardrail 3).
bool sweep_point_at(const SweepPlan& plan, size_t index, int16_t antenna_gain_dbi10,
                    TestPoint* out);

// Estimated wall-clock duration of one full sweep, in milliseconds.
//
// TWO figures, because the spread between them is the thing the operator needs.
// `worst` assumes every probe times out, which is what actually happens at the far
// positions where the data matters most; `nominal` assumes every echo returns after
// one round trip. On the default plan these differ by roughly a factor of two, so a
// single number would either overstate a good position or understate a dead one.
uint32_t sweep_duration_worst_ms(const SweepPlan& plan, int16_t antenna_gain_dbi10);
uint32_t sweep_duration_nominal_ms(const SweepPlan& plan, int16_t antenna_gain_dbi10);

// Echo deadline for one point: probe airtime + echo airtime + margin.
uint32_t echo_timeout_ms(const SweepPlan& plan, const TestPoint& tp);

// ---------------------------------------------------------------------------
// Radio configurations - what the RESPONDER has to track.
//
// FOUND ON HARDWARE, not in review. The first sweep run produced a perfect 0% PER
// across all eight SF7 points and a flat 100% PER on all sixteen SF9 and SF12 points.
// The initiator retunes for each test point; the responder was staying wherever it
// booted, and two radios on different spreading factors cannot hear each other at
// all. A sweep like that can only ever measure its first radio configuration, and it
// reports the rest as total link failure - which at 500 ft is indistinguishable from
// a real result.
//
// Only freq/SF/CR affect whether a receiver can hear a transmitter. Power and payload
// length do not, so the responder tracks the SIX distinct radio configurations of the
// default plan rather than all twenty-four test points.
// ---------------------------------------------------------------------------

struct RadioConfigKey {
  uint32_t freq_hz  = 0;
  uint8_t  sf        = 0;
  uint8_t  cr_denom  = 0;

  bool operator==(const RadioConfigKey& o) const {
    return freq_hz == o.freq_hz && sf == o.sf && cr_denom == o.cr_denom;
  }
};

// Distinct radio configurations: freq x SF x CR. Power and payload are excluded
// deliberately - see above.
size_t sweep_config_count(const SweepPlan& plan);

bool sweep_config_at(const SweepPlan& plan, size_t config_index, RadioConfigKey* out);

// Which radio configuration a given test point runs on.
//
// This is the mapping that lets a responder hearing `tp_index` know what to tune to,
// and it must agree with sweep_point_at()'s axis order or the responder will chase
// the wrong configuration.
bool sweep_config_index_of(const SweepPlan& plan, size_t tp_index, size_t* out);

// How long a responder should dwell on one configuration while hunting for the
// initiator, and how long it should wait before deciding it has lost them.
//
// Both are derived from the worst probe period AT THAT CONFIGURATION. A fixed value
// cannot work: one probe period is 0.5 s at SF7 and 8.4 s at SF12, so anything short
// enough to be responsive at SF7 abandons SF12 mid-probe.
uint32_t sweep_config_dwell_ms(const SweepPlan& plan, const RadioConfigKey& cfg);

// Uncounted probes to send when the sweep moves to a new radio configuration.
//
// MEASURED, not guessed. On the bench the first test point after each configuration
// change lost probes - 2 of 8 entering SF9, 1 of 8 entering SF12 - and the counts
// matched dwell(previous config) / probe_period(new config) exactly. That is the
// responder's reacquisition time being charged to the link as packet loss, on 5 of
// 24 points, in the metric that feeds D1.
//
// The initiator knows the plan, so it knows how long the responder will take to
// notice: one dwell at the configuration just left. These probes are transmitted
// normally and simply not counted, which lets the responder find the new
// configuration before the statistics start.
//
// Returns 0 when the configuration has not changed - the common case, since four
// consecutive test points share one configuration in the default plan.
uint16_t sweep_warmup_probes(const SweepPlan& plan, bool config_changed,
                             size_t prev_config, const TestPoint& next_tp);

// ---------------------------------------------------------------------------
// What one test point measures.
// ---------------------------------------------------------------------------

// A running min/max/mean over tenths, with an explicit count so "no samples" is
// distinguishable from "a sample that happened to be zero" (repo rule 6).
struct Series {
  int32_t  sum   = 0;
  int16_t  min   = 0;
  int16_t  max   = 0;
  uint16_t count = 0;

  void add(int16_t v);
  bool empty() const { return count == 0; }

  // Mean in tenths, rounded to nearest, away from zero. kI16NotAvailable when empty.
  int16_t mean() const;
};

// R4's primary metric is ROUND-TRIP PER, deliberately: a command that gets no
// COMMAND_ACK has failed regardless of which leg dropped it. It conflates the two
// directions, and `resp_*` is how the initiator's own CSV still says something about
// the downlink - the responder's local log (R6) is the fuller answer.
struct TestPointStats {
  uint16_t probes_sent     = 0;
  uint16_t echoes_received = 0;

  // Measured BY THE INITIATOR on the returning echo - the uplink leg.
  Series init_rssi_dbm10;
  Series init_snr_db10;

  // Measured BY THE RESPONDER on the outgoing probe, carried back inside the echo -
  // the downlink leg.
  Series resp_rssi_dbm10;
  Series resp_snr_db10;

  // Frames that arrived but failed the PHY CRC (spec 14 stage 1). Counted, never
  // silently dropped - the one discard path that cannot be produced at a desk.
  uint16_t phy_crc_errors = 0;

  // Frames that parsed but were not ours, or whose filler was corrupt.
  uint16_t foreign_frames  = 0;
  uint16_t filler_mismatch = 0;

  // R6 - the responder's own tally for this test point, carried back in the echo.
  // kU16NotAvailable while no echo has arrived, which is itself the reading: nothing
  // is known about the downlink until the responder has managed to say something.
  uint16_t resp_heard = kU16NotAvailable;

  void reset() { *this = TestPointStats{}; }

  // Round-trip packet error rate in hundredths of a percent (0..10000), so it can be
  // reported exactly without floating point. UINT16_MAX when nothing was sent.
  uint16_t per_pct100() const;
};

// ---------------------------------------------------------------------------
// The default plan. PROVISIONAL, and the frequency axis especially so.
// ---------------------------------------------------------------------------

// ONE frequency, not a sweep of them. spec 12.1 forbids D1 fixing a frequency before
// M20's ambient survey has run at both ends, and sweeping frequencies before knowing
// where the occupants are would produce numbers nobody can interpret. The axis exists
// in the plan because R4 defines the test point that way and because R8 will fill it.
extern const uint32_t kDefaultFreqsHz[];
extern const size_t   kDefaultFreqCount;

// SF7 is spec 18.1's link-budget case; SF9 is the one 15.2 keeps citing as affordable;
// SF12 is the far end. Three points span the range without paying for six.
extern const uint8_t kDefaultSfs[];
extern const size_t  kDefaultSfCount;

// 4/5 and 4/8 - the extremes. The middle two are interpolation.
extern const uint8_t kDefaultCrDenoms[];
extern const size_t  kDefaultCrCount;

// The sweep starts at the bottom of the SX1262's range and climbs only on failure
// (task guardrail 3). Both entries pass through the D33 clamp; the high one is
// requested at the SX1262 maximum precisely so the clamp is what decides it.
extern const int8_t kDefaultPowersDbm[];
extern const size_t kDefaultPowerCount;

// Small and large. 222 is not here - that is W9's job (R9), against the real codec.
extern const uint8_t kDefaultPayloads[];
extern const size_t  kDefaultPayloadCount;

SweepPlan default_plan();

}  // namespace rangetest
