// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// M25's channel occupancy accumulator. chan_monitor.h has the reasoning.

#include "chan_monitor.h"

#include <cstdio>

namespace bridge {
namespace {

uint32_t saturating_add(uint32_t a, uint32_t b) {
  return b > UINT32_MAX - a ? UINT32_MAX : a + b;
}

}  // namespace

void ChanMonitor::advance(uint32_t now_ms) {
  if (!started_) {
    open_          = ChanBucket{};
    open_.seq      = next_seq_;
    open_.start_ms = now_ms;
    started_       = true;
    return;
  }
  // Unsigned subtraction, wrap-correct - the same arithmetic rx_deaf.h relies on.
  if (now_ms - open_.start_ms >= kChanBucketMs) publish(now_ms);
}

void ChanMonitor::publish(uint32_t now_ms) {
  open_.dur_ms = now_ms - open_.start_ms;

  const uint32_t w           = write_.load(std::memory_order_relaxed);
  slots_[w % kChanSlots]     = open_;
  write_.store(w + 1, std::memory_order_release);

  ++next_seq_;
  open_          = ChanBucket{};
  open_.seq      = next_seq_;
  open_.start_ms = now_ms;
}

void ChanMonitor::sample(Dbm10 rssi, uint32_t now_ms) {
  advance(now_ms);

  // The encoding's rail is not a reading (chan_monitor.h). Counted as a skip rather than
  // dropped silently, so the denominator still accounts for every opportunity.
  if (rssi <= kRssiRailDbm10) {
    ++open_.skipped;
    return;
  }

  ++open_.samples;
  open_.sum += rssi;
  if (rssi >= kOccupiedDbm10) ++open_.above;

  // kNoReading is INT16_MIN, below every real RSSI, so the first sample sets both ends
  // without a separate "is this the first" flag.
  if (open_.peak == kNoReading || rssi > open_.peak) open_.peak = rssi;
  if (open_.floor == kNoReading || rssi < open_.floor) open_.floor = rssi;
}

void ChanMonitor::skip(uint32_t now_ms) {
  advance(now_ms);
  ++open_.skipped;
}

void ChanMonitor::note_own_rx() {
  // No advance(): this is attributed to whatever bucket is open, and a reception that
  // lands between two buckets belongs to either equally.
  if (started_) ++open_.own_rx;
}

bool ChanMonitor::take(ChanBucket* out) {
  for (size_t attempt = 0; attempt < kChanSlots; ++attempt) {
    const uint32_t w = write_.load(std::memory_order_acquire);
    uint32_t       r = read_.load(std::memory_order_relaxed);
    if (r == w) return false;

    const uint32_t span = w - r;
    if (span > kChanSlots) {
      lost_.store(saturating_add(lost_.load(std::memory_order_relaxed), span - kChanSlots),
                  std::memory_order_relaxed);
      r = w - kChanSlots;
    }

    const ChanBucket copy = slots_[r % kChanSlots];
    read_.store(r + 1, std::memory_order_relaxed);

    // The torn-slot check frame_log.h explains: a bucket carries its own sequence, so a
    // slot the producer overwrote mid-copy no longer answers the number asked for.
    if (copy.seq == r) {
      *out = copy;
      return true;
    }
    lost_.store(saturating_add(lost_.load(std::memory_order_relaxed), 1),
                std::memory_order_relaxed);
  }
  return false;
}

void ChanRollupper::add(const ChanBucket& b) {
  if (cur_.buckets == 0) cur_.from_seq = b.seq;
  cur_.to_seq = b.seq;
  ++cur_.buckets;

  cur_.span_ms += b.dur_ms;
  cur_.samples += b.samples;
  cur_.skipped += b.skipped;
  cur_.above += b.above;
  cur_.own_rx += b.own_rx;
  if (chan_notable(b)) ++cur_.notable;

  // A bucket that sampled nothing contributes NO floor and NO peak. Letting it through
  // as kNoReading would drag the minimum to INT16_MIN and report a -3276.8 dBm floor;
  // counting it as blind is what keeps "unobserved" apart from "quiet".
  if (b.samples == 0) {
    ++cur_.blind;
    return;
  }

  if (b.peak != kNoReading && (cur_.peak == kNoReading || b.peak > cur_.peak)) {
    cur_.peak = b.peak;
  }
  if (b.floor != kNoReading) {
    if (cur_.floor_min == kNoReading || b.floor < cur_.floor_min) cur_.floor_min = b.floor;
    if (cur_.floor_max == kNoReading || b.floor > cur_.floor_max) cur_.floor_max = b.floor;
    cur_.floor_sum += b.floor;
    ++cur_.floor_n;
  }
}

bool ChanRollupper::due() const {
  return cur_.buckets >= (first_ ? kChanFirstRollupBuckets : kChanRollupBuckets);
}

bool ChanRollupper::take(ChanRollup* out) {
  if (cur_.buckets == 0) return false;
  *out   = cur_;
  cur_   = ChanRollup{};
  first_ = false;
  return true;
}

size_t render_chan(const ChanBucket& b, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;

  const int n = std::snprintf(
      out, cap, "CHAN,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%d,%d,%d",
      static_cast<unsigned long>(b.seq), static_cast<unsigned long>(b.start_ms),
      static_cast<unsigned long>(b.dur_ms), static_cast<unsigned long>(b.samples),
      static_cast<unsigned long>(b.skipped), static_cast<unsigned long>(b.above),
      static_cast<unsigned long>(b.own_rx), static_cast<int>(b.peak),
      static_cast<int>(b.floor), static_cast<int>(b.mean()));

  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t render_chan_rollup(const ChanRollup& r, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;

  const int n = std::snprintf(
      out, cap, "CHANSUM,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%d,%d,%d,%d",
      static_cast<unsigned long>(r.from_seq), static_cast<unsigned long>(r.to_seq),
      static_cast<unsigned long>(r.buckets), static_cast<unsigned long>(r.span_ms),
      static_cast<unsigned long>(r.samples), static_cast<unsigned long>(r.skipped),
      static_cast<unsigned long>(r.above), static_cast<unsigned long>(r.own_rx),
      static_cast<unsigned long>(r.notable), static_cast<unsigned long>(r.blind),
      static_cast<int>(r.peak), static_cast<int>(r.floor_min),
      static_cast<int>(r.floor_max), static_cast<int>(r.floor_mean()));

  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t render_chan_boot(const char* git, uint32_t freq_hz, uint8_t sf, uint16_t bw_khz10,
                        uint32_t wait_ms, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;

  const int n = std::snprintf(
      out, cap, "CHAN-BOOT,%s,%lu,%u,%u,%lu,%d,%lu", git == nullptr ? "unknown" : git,
      static_cast<unsigned long>(freq_hz), static_cast<unsigned>(sf),
      static_cast<unsigned>(bw_khz10), static_cast<unsigned long>(kChanBucketMs),
      static_cast<int>(kOccupiedDbm10), static_cast<unsigned long>(wait_ms));

  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

}  // namespace bridge
