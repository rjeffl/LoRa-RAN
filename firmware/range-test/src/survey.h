// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R8 / M20 - the ambient RSSI survey.
//
// spec 12.1 forbids D1 fixing a frequency until this has run AT BOTH ENDS. It is also
// D33's third standing condition. Nothing here transmits: the survey listens, and the
// radio's output power is never engaged in this mode.
//
// Arduino-free and radio-free, like sweep.h and for the same reason - the bin plan,
// the per-bin statistics, the CSV schema and the NVS blob are all things that can be
// silently wrong, and none of them needs a radio to be exercised. main.cpp owns the
// SX1262 and the clock.
//
// WHAT THIS MEASURES WELL, AND WHAT IT DOES NOT
//
// One radio listening to one 125 kHz slice at a time cannot observe 130 bins at once.
// Each bin is therefore sampled roughly 1/130 of the time, which means:
//
//   - The NOISE FLOOR per bin is measured well. It is stationary, so any pass sees
//     it, and the mean over hundreds of passes is a solid figure. This is the number
//     that sets the node's margin, and it is what spec 12.1 actually asks for.
//   - OCCUPANCY is detected only PROBABILISTICALLY. A YoLink sensor keying up for
//     100 ms every five minutes will be missed by most runs. A peak well above the
//     floor is real evidence of an occupant; the ABSENCE of one is NOT evidence of an
//     empty bin, and no run length short of hours makes it so.
//
// That asymmetry is why the peak column is peak-HOLD across every pass and why the
// run duration is the operator's to choose (rule 8). It is also why the trace records
// `passes` and `samples` per bin: a reader has to be able to see how hard the survey
// looked before believing that it found nothing.

#pragma once

#include <cstddef>
#include <cstdint>

#include "sentinels.h"
#include "sweep.h"   // Series - the same min/max/mean accumulator the sweep uses

namespace rangetest {

// ---------------------------------------------------------------------------
// The sites. R8 asks for the bridge location and "the most distant node location";
// the property has SEVEN places that matter, so the survey stores one run per site
// rather than one run.
//
// This is what makes the campaign a single trip. Without it the operator would have
// to walk back to the laptop after every site to read a run out before the next one
// overwrote it - seven walks instead of one loop.
//
// Site 0 is the bridge (spec 12.1's first required location). The rest are the node
// target locations, in the order a loop of the property naturally visits them; the
// order is not load-bearing, but the NAMES are - a trace saying "site 3" and nothing
// else is a trace nobody can place in eighteen months.
// ---------------------------------------------------------------------------

inline constexpr size_t kSurveySiteCount = 7;

// Short, lower-case, no spaces or commas: these go in a CSV column.
const char* survey_site_name(size_t site);

// ---------------------------------------------------------------------------
// The bin plan. R8: 902-928 MHz in 200 kHz steps, 130 bins.
// ---------------------------------------------------------------------------

inline constexpr uint32_t kSurveyStartHz = 902000000UL;

// 200 kHz because it is the LoRaWAN US915 uplink channel spacing. The step is chosen
// so a LoRaWAN-shaped occupancy pattern lines up with the bins and is recognisable on
// sight - which settles whether the site's existing equipment is band-plan or
// proprietary, a question no datasheet has answered (M20).
inline constexpr uint32_t kSurveyStepHz = 200000UL;

inline constexpr size_t kSurveyBinCount = 130;

// Bin 0 sits at the bottom band edge and bin 129 at 927.8 MHz. There is deliberately
// no bin at 928.0: that is the top edge itself, and a 125 kHz receiver centred on it
// would be listening half outside the band.
static_assert(kSurveyStartHz + (kSurveyBinCount - 1) * kSurveyStepHz == 927800000UL,
              "R8 bin plan no longer spans 902.0-927.8 MHz");

uint32_t survey_bin_freq_hz(size_t bin_index);

// Nearest bin to a frequency, for reporting where a candidate channel would sit.
// Returns kSurveyBinCount if the frequency is outside the plan.
size_t survey_bin_of(uint32_t freq_hz);

// ---------------------------------------------------------------------------
// Timing. Rule 8: nothing timing-related is fixed at compile time.
// ---------------------------------------------------------------------------

struct SurveyPlan {
  uint32_t start_hz = kSurveyStartHz;
  uint32_t step_hz  = kSurveyStepHz;
  uint16_t bins     = kSurveyBinCount;

  // Time spent listening on one bin, per pass. SHORT ON PURPOSE. The occupants worth
  // catching are rare and brief, so the probability of overlapping one is driven by
  // the number of passes, not by the length of any single dwell - and a long dwell
  // buys fewer passes for the same wall-clock minute.
  uint16_t dwell_ms = 30;

  // Discarded after entering RX on a new frequency. The SX1262's RSSI reads low and
  // then climbs while its AGC settles, so the first samples after a retune are not
  // measurements of the band - they are measurements of the receiver waking up. They
  // would drag every bin's mean down by the same amount, which is the worst kind of
  // error: it looks like a clean, quiet band.
  uint16_t settle_ms = 4;

  // Gap between RSSI reads within one dwell.
  uint16_t sample_interval_ms = 3;
};

// Samples one dwell will take, after the settle time is deducted. Zero is a real and
// reportable answer - it means the plan's settle time swallows its dwell.
uint16_t survey_samples_per_dwell(const SurveyPlan& plan);

// Wall-clock milliseconds for one full pass over every bin, excluding retune time.
uint32_t survey_pass_duration_ms(const SurveyPlan& plan);

// ---------------------------------------------------------------------------
// The accumulator.
// ---------------------------------------------------------------------------

// Series::count is a uint16_t, so a long run would wrap it and produce a mean over a
// denominator that is not the number of samples taken. Capped instead, with the
// overflow COUNTED rather than absorbed (repo rule 4): a bin that stopped accepting
// samples says so in its own column, and the mean it reports is still a true mean of
// the samples it did accept.
inline constexpr uint16_t kSurveyMaxSamplesPerBin = 60000;

struct SurveyBin {
  Series   rssi_dbm10;   // min is the floor, max is the peak hold, mean is the mean
  uint32_t dropped = 0;  // samples refused after the cap
};

class Survey {
 public:
  void reset();

  // Folds one RSSI reading into `bin_index`. Out-of-range indices are ignored.
  void add_sample(size_t bin_index, int16_t rssi_dbm10);

  // A pass over every bin finished. Counted separately from samples because it is the
  // figure that says how many independent chances the survey had to catch a brief
  // transmission.
  void note_pass() { ++passes_; }

  uint32_t passes() const { return passes_; }
  const SurveyBin& bin(size_t i) const;

  // Bins that took at least one sample. Less than `bins` means the run was stopped
  // part-way through a pass, which is normal and which the trace must not hide.
  size_t bins_sampled() const;

  // The quietest bin by MEAN, and the loudest by PEAK. Both are what the operator
  // wants on a 128x64 display, and both are what a D1 justification opens with.
  // Return kSurveyBinCount when nothing has been sampled.
  size_t quietest_bin() const;
  size_t loudest_bin() const;

  // ---- persistence ------------------------------------------------------
  //
  // R8 runs at the MOST DISTANT NODE LOCATION, which is where the walking end goes
  // and where there is no laptop. Without this the far-end trace costs a second trip
  // with a laptop in hand. Same shape as PositionLog's blob (R6) and for the same
  // reasons: flat, little-endian, field by field, never a memcpy of the struct
  // (repo rule 1), because host tooling reads it back.
  //
  // Frequency is NOT stored per bin - it is derivable from the index and the plan,
  // and storing it twice invites the two to disagree. The plan's start and step ARE
  // stored, so a blob captured under a different plan is detectable rather than
  // silently re-interpreted under the current one.

  // magic(4) version(2) bins(2) start_hz(4) step_hz(4) passes(4)
  static constexpr size_t kBlobHeaderLen = 20;
  static constexpr size_t kBlobEntryLen  = 12;
  static constexpr size_t kBlobMaxLen =
      kBlobHeaderLen + kSurveyBinCount * kBlobEntryLen;

  size_t serialize(const SurveyPlan& plan, uint8_t* out, size_t cap) const;

  // Replaces the contents and reports the plan the blob was captured under. Returns
  // false on bad magic, version or length, leaving the survey cleared rather than
  // half-loaded.
  bool deserialize(const uint8_t* in, size_t len, SurveyPlan* out_plan);

 private:
  SurveyBin bins_[kSurveyBinCount]{};
  uint32_t  passes_ = 0;
};

// ---------------------------------------------------------------------------
// R11 - the campaign cursor and its HOLD state.
//
// WHY THIS EXISTS. Before R11 the scan never stopped: storing a site reset the
// accumulator and resumed sampling immediately, so everything the radio heard while
// the operator WALKED to the next site was folded into that site's run. Peak-hold
// never forgets, so one burst heard in transit was attributed permanently to the site
// being walked to. The 2026-09-05 campaign was collected that way and its peak column
// is caveated in the trace because of it.
//
// There is no press pattern that avoids this - pressing on arrival rather than on
// departure only moves the contamination to the site just left. It needs a state in
// which the radio is not accumulating, which is what this is.
//
// THE CYCLE IS TWO PRESSES PER SITE. Arrive, press to start the dwell; when the dwell
// is done, press to store and advance, which returns to HELD. The walk happens in
// HELD and is not measured. Boot comes up HELD, because the operator is not standing
// at site 0 when the board boots.
//
// This type owns the cursor and the phase and NOTHING else. Storing to NVS can fail -
// the partition is small and fails by short write - and a cursor that advanced over a
// site that was not written is a site silently lost. So the caller performs the store
// and reports the outcome back through note_stored(); the cursor advances only on a
// success. main.cpp keeps NVS and the radio, and this stays testable on the host.
// ---------------------------------------------------------------------------

enum class SurveyPhase : uint8_t {
  Held,      // not accumulating. Walking, or waiting to be told to start.
  Running,   // dwelling on this site, folding samples into the run
};

// What a PRG press means right now. The caller looks at this to decide what work to
// do; nothing here performs it.
enum class SurveyPress : uint8_t {
  StartDwell,   // Held -> Running. Clear the accumulator and begin measuring.
  StoreSite,    // Running -> store the run, then report back via note_stored()
};

class SurveyCampaign {
 public:
  size_t      site()  const { return site_; }
  SurveyPhase phase() const { return phase_; }
  bool        held()  const { return phase_ == SurveyPhase::Held; }

  // True when the cursor is on the last site, so a store there has nowhere to
  // advance to. The run is still stored; the cursor simply stays put.
  bool on_last_site() const { return site_ + 1 >= kSurveySiteCount; }

  SurveyPress classify_press() const {
    return held() ? SurveyPress::StartDwell : SurveyPress::StoreSite;
  }

  // Held -> Running. The caller clears the accumulator; this only moves the phase.
  void note_started() { phase_ = SurveyPhase::Running; }

  // The caller attempted the store. On success the cursor advances (unless it is
  // already on the last site) and the phase returns to Held, so the walk to the next
  // site is not measured. On FAILURE nothing moves: the run stays in memory, still
  // accumulating, and the operator can press again or read it out over serial.
  // Returns true when the cursor advanced.
  bool note_stored(bool ok) {
    if (!ok) return false;
    phase_ = SurveyPhase::Held;
    if (on_last_site()) return false;
    ++site_;
    return true;
  }

  // Cursor corrections ('n' and 'b'), which store nothing. Both drop to Held: the
  // accumulator is cleared under them, and resuming a dwell is the operator's call.
  bool next_site() {
    if (on_last_site()) return false;
    ++site_;
    phase_ = SurveyPhase::Held;
    return true;
  }
  bool prev_site() {
    if (site_ == 0) return false;
    --site_;
    phase_ = SurveyPhase::Held;
    return true;
  }

  // Restoring the persisted cursor at boot. Always lands in Held - see above.
  void restore_site(size_t site) {
    site_  = (site < kSurveySiteCount) ? site : kSurveySiteCount - 1;
    phase_ = SurveyPhase::Held;
  }

  void reset() {
    site_  = 0;
    phase_ = SurveyPhase::Held;
  }

 private:
  size_t      site_  = 0;
  SurveyPhase phase_ = SurveyPhase::Held;
};

// ---------------------------------------------------------------------------
// R8's CSV. A SECOND schema, not an extension of R7's.
//
// The task allows either. Two schemas because the two traces have nothing in common
// but their directory: R7's row is a test point at a position with a link at the far
// end, R8's is a frequency bin with no far end at all. Widening one schema to cover
// both would give every survey row twenty empty sweep columns and vice versa, and a
// reader could not tell a real sentinel from a column that never applied.
// ---------------------------------------------------------------------------

struct SurveyCsvRow {
  // The site travels in every row so ONE file can hold the whole campaign. Seven
  // separate files would have to be correlated by filename, and a filename is not
  // evidence.
  size_t           site      = 0;
  size_t           bin_index = 0;
  uint32_t         freq_hz   = 0;
  uint32_t         passes    = 0;
  const SurveyBin* bin       = nullptr;
};

size_t survey_csv_header(char* out, size_t cap);
size_t survey_csv_row(const SurveyCsvRow& row, char* out, size_t cap);
size_t survey_csv_field_count();

inline constexpr size_t kSurveyCsvMaxLine = 160;

}  // namespace rangetest
