// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The per-node availability watchdog. Task BF-20.

#include "node_availability.h"

#include "net_policy.h"

namespace bridge {

const char* availability_payload(Availability a) {
  switch (a) {
    case Availability::Online:  return kPayloadOnline;
    case Availability::Offline: return kPayloadOffline;
    case Availability::Unknown: break;
  }
  return nullptr;
}

const char* availability_publication(const NodeInfo& info, Availability state,
                                     bool simnode_diag_enable, bool clearing) {
  if (state == Availability::Unknown) return nullptr;
  if (!bench_publication_allowed(info, simnode_diag_enable)) {
    return clearing ? kPayloadOffline : nullptr;
  }
  return availability_payload(state);
}

void AvailabilityWatchdog::set_threshold(uint16_t polls) {
  threshold_ = polls == 0 ? 1 : polls;
}

AvailabilityChange AvailabilityWatchdog::evaluate(size_t i, bool deployed,
                                                  const NodeState& s) {
  AvailabilityChange out;
  if (i >= kNodeCount) return out;
  Row& r = rows_[i];

  // A frame since the last tick. frames_heard, not missed_polls == 0: a frame followed by a
  // miss inside one tick leaves missed_polls at 1 and would hide the frame.
  const bool frame = s.heard && (!r.seen_any || s.frames_heard != r.frames_at);
  if (s.heard) {
    r.seen_any  = true;
    r.frames_at = s.frames_heard;
  }
  r.watched = r.watched || deployed || s.heard;

  Availability next = r.state;
  if (frame) next = Availability::Online;
  // After the frame, deliberately: observe() cleared missed_polls, so a count at the
  // threshold now was reached by polls sent after that frame, and is the newer fact.
  if (s.missed_polls >= threshold_) next = Availability::Offline;

  if (next != r.state) {
    out.changed = true;
    out.from    = r.state;
    out.to      = next;
    r.state     = next;
    r.pending   = true;
  }
  return out;
}

Availability AvailabilityWatchdog::state(size_t i) const {
  return i < kNodeCount ? rows_[i].state : Availability::Unknown;
}

bool AvailabilityWatchdog::watched(size_t i) const {
  return i < kNodeCount && rows_[i].watched;
}

bool AvailabilityWatchdog::pending(size_t i) const {
  return i < kNodeCount && rows_[i].pending;
}

void AvailabilityWatchdog::clear_pending(size_t i) {
  if (i < kNodeCount) rows_[i].pending = false;
}

void AvailabilityWatchdog::mark_known_pending() {
  for (Row& r : rows_) {
    if (r.state != Availability::Unknown) r.pending = true;
  }
}

uint8_t AvailabilityWatchdog::online_count() const {
  uint8_t n = 0;
  for (const Row& r : rows_) {
    if (r.watched && r.state == Availability::Online) ++n;
  }
  return n;
}

uint8_t AvailabilityWatchdog::watched_count() const {
  uint8_t n = 0;
  for (const Row& r : rows_) {
    if (r.watched) ++n;
  }
  return n;
}

}  // namespace bridge
