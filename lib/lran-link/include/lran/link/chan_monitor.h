// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// What else is on the channel, sampled continuously and summarised per second (M25).
// Bridge engineering log, 2026-09-17.
//
// TWO FIRMWARES RUN IT. The bridge samples from lora_task between its own frames, and
// firmware/chan-capture/ samples from a receiver that never transmits. Moved here from the
// bridge on 2026-09-19 so that captures from the two are summarised by one implementation:
// D1 frequency change brief 5 compares them bucket for bucket, and two copies of this
// arithmetic are how two receivers come to disagree about the same second.
//
// ARDUINO-FREE AND HOST-TESTED, like rx_wake.h, rx_deaf.h and frame_log.h: the
// accumulation is arithmetic, and arithmetic that decides what a twelve-hour capture
// says is worth testing at a desk.
//
// WHY THIS EXISTS. D33's third standing condition is that the ambient survey finds no
// co-channel occupant, and Decision Register 3.4 names `cad_backoffs` as the instrument
// that keeps the channel under observation after D1. It cannot do that job. A LoRa CAD
// detects a LoRa preamble AT THE CONFIGURED SPREADING FACTOR, so it is blind to the
// Z-Wave and Insteon FSK on this property at any signal level, and to LoRa at another
// SF. This samples raw RSSI instead, which is modulation-agnostic: it sees anything that
// puts energy in the channel.
//
// WHY A LONG RUN RATHER THAN A BETTER INSTRUMENT. M20's survey caught the YoLink hub at
// -54 dBm against a -113.8 dBm mean in the same bin - a 60 dB peak-to-mean, meaning it is
// on the air for a small fraction of samples. M20 gave each bin 653 samples. lora_task
// wakes every kLoraMaxWaitMs (10 ms), so this yields ~100 samples a second: a six-hour
// capture is ~2.2 million samples on the one channel that matters, four orders of
// magnitude more observation than the survey could spend there. That turns "did we happen
// to catch it" into a duty-cycle measurement.
//
// WHAT IT CANNOT SEE. A burst shorter than the ~10 ms sampling period is caught only in
// proportion to its duty cycle, not reliably on each occurrence. Over hours that is a
// statistical statement rather than a miss - a 0.1 % duty cycle emitter still lands
// thousands of hits in six hours - but a SINGLE short burst can pass unrecorded, so this
// bounds any claim that a particular gap in the frame log had a quiet channel.
//
// ITS CLOCK IS millis(), THE SAME CLOCK frame_log.h STAMPS. A lost frame and a channel
// excursion go on one timeline instead of being argued about separately.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace lran {
namespace link {

// Tenths of a dBm, matching the survey campaign's `peak_dbm10` columns so the two
// datasets can be read with the same habits (docs/rangetest/data/).
using Dbm10 = int16_t;

// No sample yet. Outside any real RSSI - the SX1262 reports about -130 dBm at worst -
// so it can never be confused with a reading (root rule 6).
inline constexpr Dbm10 kNoReading = INT16_MIN;

// The RSSI encoding's own rail: RadioLib returns rssiRaw / -2.0, so a raw 0xFF reads
// exactly -127.5 dBm. IT IS NOT A MEASUREMENT. It appears in the first buckets after
// boot, before the receiver has settled, and a single one of them drags a window's
// floor_min 12 dB below the campaign floor - the kind of number that gets copied into a
// document before anyone asks what produced it. Counted as a skip, which is what it is:
// an opportunity that yielded no reading.
inline constexpr Dbm10 kRssiRailDbm10 = -1275;

// Counted as "something was here". Six dB above the campaign-wide -116 dBm floor, which
// is the level M20 treated as the noise floor at every site. THE ANALYSIS CAN
// RE-THRESHOLD: peak, floor and mean are all recorded per bucket, so this count is a
// convenience and not the only occupancy figure the capture supports.
inline constexpr Dbm10 kOccupiedDbm10 = -1100;

// One second of channel, summarised.
struct ChanBucket {
  // Monotonic across the run. SURVIVES THE millis() WRAP, which a twelve-hour capture
  // will not reach but a multi-day one will; ordering must not depend on the timestamp.
  uint32_t seq = 0;

  uint32_t start_ms = 0;
  uint32_t dur_ms   = 0;  // actual, not nominal - lora_task is not a metronome

  // Samples taken with the radio in receive and no frame of ours arriving.
  uint32_t samples = 0;

  // Opportunities that were NOT sampled: the radio was transmitting, doing a CAD, or
  // down, or one of our own frames was mid-flight. REPORTED RATHER THAN HIDDEN - a
  // bucket that was mostly skipped saw mostly nothing, and an occupancy figure that does
  // not say so is a number with an unstated denominator.
  uint32_t skipped = 0;

  uint32_t above  = 0;  // samples at or above kOccupiedDbm10
  uint32_t own_rx = 0;  // our own receptions completed in this bucket

  Dbm10 peak  = kNoReading;
  Dbm10 floor = kNoReading;

  // Tenths-dBm summed, for the mean. A dBm mean is not a power mean and is not meant to
  // be: peak is what answers "was something here", and the mean tracks the floor's drift.
  int32_t sum = 0;

  bool has_reading() const { return samples > 0; }
  Dbm10 mean() const {
    return samples == 0 ? kNoReading : static_cast<Dbm10>(sum / static_cast<int32_t>(samples));
  }
};

// Eight seconds of backlog against a drain that runs ten times a second. The ring exists
// so the serial writer can never stall the sampler, not because it is expected to fill.
// On the bridge those are log_task and lora_task.
inline constexpr size_t kChanSlots = 8;

// Nominal bucket length. Compile-time, and root rule 8 does not reach it: the rule is
// about a node that cannot be reflashed without a walk to the gate, and both firmwares
// that run this are reflashable in the house. A runtime lever would need the HA-visible
// configuration path, whose route is an open operator decision - see docs/bridge/HANDOFF.md.
inline constexpr uint32_t kChanBucketMs = 1000;

// Single-producer (the sampler), single-consumer (the serial writer). The ring and the
// torn-slot check are the bridge's frame_log.h's, for the reasons given there.
class ChanMonitor {
 public:
  // One RSSI reading, from the sampler. Closes and publishes a bucket when the nominal
  // length has elapsed.
  void sample(Dbm10 rssi, uint32_t now_ms);

  // An opportunity that could not be sampled, and why it could not is already known to
  // the caller: the radio was not in receive, or one of our frames was arriving.
  void skip(uint32_t now_ms);

  // One of our own frames completed. Attributed to the open bucket.
  void note_own_rx();

  // Consumer side. False when no bucket has closed.
  bool take(ChanBucket* out);

  // Buckets the ring overwrote before log_task drained them. Saturating.
  uint32_t lost() const { return lost_.load(std::memory_order_relaxed); }

 private:
  void advance(uint32_t now_ms);
  void publish(uint32_t now_ms);

  ChanBucket            open_{};
  bool                  started_ = false;
  uint32_t              next_seq_ = 0;
  ChanBucket            slots_[kChanSlots];
  std::atomic<uint32_t> write_{0};
  std::atomic<uint32_t> read_{0};
  std::atomic<uint32_t> lost_{0};
};

// ---------------------------------------------------------------------------
// What actually reaches the capture file.
//
// A BUCKET A SECOND FOR TWELVE HOURS IS 43 200 LINES, and almost all of them say the
// channel was empty. Writing only the interesting ones is the obvious saving and it is
// WRONG ON ITS OWN, because occupancy is `above / samples` and the samples live in the
// quiet buckets. Drop them and the capture loses its denominator, the floor's drift over
// the night, and the difference between "quiet" and "not looking" - which is the one
// distinction this instrument exists to keep (see `skipped`).
//
// So two tiers, and between them they cost about 1.7 % of the naive volume:
//
//   CHAN     one line per bucket that SAW something, in full detail.
//   CHANSUM  one line per kChanRollupBuckets, carrying the denominator, the floor and
//            the skip accounting for every bucket in the window, loud or not.
//
// A quiet twelve hours is then ~720 lines rather than 43 200, and nothing the analysis
// needs has been thrown away.
// ---------------------------------------------------------------------------

// True when a bucket is worth a line of its own.
inline bool chan_notable(const ChanBucket& b) { return b.above > 0; }

// Buckets per rollup line. 60 at a 1 s bucket is one line a minute.
inline constexpr uint32_t kChanRollupBuckets = 60;

// The first rollup is short, so a capture proves itself in ten seconds instead of
// leaving the operator watching a silent port for a minute wondering which thing broke.
inline constexpr uint32_t kChanFirstRollupBuckets = 10;

// Every bucket in a window, loud or not.
struct ChanRollup {
  uint32_t from_seq = 0;
  uint32_t to_seq   = 0;
  uint32_t buckets  = 0;
  uint32_t span_ms  = 0;

  uint32_t samples   = 0;
  uint32_t skipped   = 0;
  uint32_t above     = 0;
  uint32_t own_rx    = 0;
  uint32_t notable   = 0;  // buckets that also got their own CHAN line
  uint32_t blind     = 0;  // buckets that sampled nothing at all

  Dbm10 peak      = kNoReading;
  Dbm10 floor_min = kNoReading;
  Dbm10 floor_max = kNoReading;

  int32_t  floor_sum = 0;  // mean of the per-bucket floors; a median needs a sort
  uint32_t floor_n   = 0;

  Dbm10 floor_mean() const {
    return floor_n == 0 ? kNoReading
                        : static_cast<Dbm10>(floor_sum / static_cast<int32_t>(floor_n));
  }
};

// Accumulates buckets into rollups. Lives on log_task, so it needs no synchronisation:
// the policy of what to write belongs with the writer, not with the sampler.
class ChanRollupper {
 public:
  void add(const ChanBucket& b);

  // True when enough buckets have accumulated to emit one.
  bool due() const;

  // Fills *out and resets. False when nothing has accumulated.
  bool take(ChanRollup* out);

 private:
  ChanRollup cur_{};
  bool       first_ = true;
};

// One bucket as a serial line, NUL-terminated. Returns the length, or 0 when it would
// not fit - refused, never truncated.
//
// THE CAPTURE FILE IS THE DELIVERABLE, so the line is CSV with a `CHAN` tag rather than
// prose: a six-hour run is read by a tool months later, on a machine that has none of
// this context, and it shares the port with frame_log.h's `FRAME` lines.
//
//   CHAN,seq,start_ms,dur_ms,samples,skipped,above,own_rx,peak_dbm10,floor_dbm10,mean_dbm10
size_t render_chan(const ChanBucket& b, char* out, size_t cap);

// One rollup as a serial line. Same contract.
//
//   CHANSUM,from_seq,to_seq,buckets,span_ms,samples,skipped,above,own_rx,notable,blind,
//           peak_dbm10,floor_min_dbm10,floor_max_dbm10,floor_mean_dbm10
size_t render_chan_rollup(const ChanRollup& r, char* out, size_t cap);

// The header every capture opens with, so the file says what produced it without a
// commit message or a lab notebook beside it. Returns the length, or 0.
//
//   CHAN-BOOT,<git>,<freq_hz>,<sf>,<bw_khz10>,<bucket_ms>,<occupied_dbm10>,<wait_ms>
size_t render_chan_boot(const char* git, uint32_t freq_hz, uint8_t sf, uint16_t bw_khz10,
                        uint32_t wait_ms, char* out, size_t cap);

}  // namespace link
}  // namespace lran
