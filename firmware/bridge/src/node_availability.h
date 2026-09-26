// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The per-node availability watchdog. Task BF-20; PRD R-3.4a-d; spec 16.5, 16.6; Impl
// Plan 4.2a, 6.1; V-B3.
//
// ARDUINO-FREE, AND IT HOLDS NO LOCK. sched_task copies each node's registry state and
// hands it to evaluate() once a tick; the watchdog says what changed and what still needs
// publishing. The publish itself is task_runtime.cpp's.
//
// THE RULE (spec 16.5): `offline` after missed_poll_threshold consecutive unanswered polls,
// `online` on any valid frame. Nothing else moves a node - not time since last_seen, not a
// threshold changed at runtime. A node that has neither answered nor missed enough polls
// since boot is Unknown and is NOT published: whatever the broker retained from before the
// reboot stands until the node proves it either way.
//
// WHO IS WATCHED mirrors the poll scheduler (D61): a deployed node from the tick its lever
// is applied, any other node once the bridge has heard it. A simnode that is not on the
// bench, or a node not yet in the field, is not reported offline and is not in spec
// 12.4.1's fleet.

#pragma once

#include <cstddef>
#include <cstdint>

#include "registry.h"

namespace bridge {

// PRD R-3.4b, spec 16.5. Runtime-settable (root rule 8); BF-23 takes it from Home Assistant.
inline constexpr uint16_t kMissedPollThresholdDefault = 3;

enum class Availability : uint8_t { Unknown, Online, Offline };

// spec 16.1's payload tokens. nullptr for Unknown, which is never published.
const char* availability_payload(Availability a);

struct AvailabilityChange {
  bool         changed = false;
  Availability from    = Availability::Unknown;
  Availability to      = Availability::Unknown;
};

class AvailabilityWatchdog {
 public:
  // Rows are registry rows: index i is kNodeTable[i].
  AvailabilityWatchdog() = default;

  // A threshold of 0 would mark every node offline without a poll; it is held to 1.
  void     set_threshold(uint16_t polls);
  uint16_t threshold() const { return threshold_; }

  // Judges row i from a copy of its registry state. A change also marks the row pending.
  // `deployed` is the node's D61 lever; once watched, a row stays watched this boot.
  AvailabilityChange evaluate(size_t i, bool deployed, const NodeState& s);

  Availability state(size_t i) const;
  bool         watched(size_t i) const;

  // Pending: the retained topic does not yet carry this row's state. Cleared only once the
  // publication is queued, so a full queue is retried on the next tick.
  bool pending(size_t i) const;
  void clear_pending(size_t i);

  // Every known row pending again - after a broker connect, which may have lost the
  // retained state (spec 16.5), or once bench publication is switched on (BF-26).
  void mark_known_pending();

  // For the status page (R-4.1c): online rows out of watched rows.
  uint8_t online_count() const;
  uint8_t watched_count() const;

 private:
  struct Row {
    Availability state        = Availability::Unknown;
    bool         watched      = false;
    bool         pending      = false;
    bool         seen_any     = false;  // frames_at is meaningful
    uint32_t     frames_at    = 0;      // NodeState::frames_heard at the last evaluate()
  };

  uint16_t threshold_ = kMissedPollThresholdDefault;
  Row      rows_[kNodeCount];
};

// spec 16.6 - a bench node's availability and diagnostics are published only while
// simnode_diag_enable is set. The node is still judged and counted; only the publication is
// gated (Impl Plan 4.2a).
inline bool bench_publication_allowed(const NodeInfo& info, bool simnode_diag_enable) {
  return !info.is_bench || simnode_diag_enable;
}

// What row's retained availability topic should carry, or nullptr for nothing. Unknown
// publishes nothing. A bench node with the flag clear publishes nothing either, except
// `offline` when the flag clears (`clearing`), whatever the row's state: the caller sets
// `clearing` only for a topic that may hold an `online` (bench_withdrawal_owed()). Its
// discovery entities stay in HA's registry, and `offline` is how they say that nothing
// updates them. Spec 16.6 allows that one publication and no other while the flag is
// clear.
const char* availability_publication(const NodeInfo& info, Availability state,
                                     bool simnode_diag_enable, bool clearing);

// One bit per bench address from 0xF0 (spec 5.3), or 0 for any other address. The
// bridge's NVS keeps a mask of these (nvs_persist.h).
uint16_t bench_bit(lran::NodeId id);

// spec 16.6 - whether clearing the flag owes this row an `offline`: a bench row this boot
// has judged, or one whose `online` the last boot left retained (`retained_online`, the
// NVS mask). A flag set `applied_not_persisted` comes back clear after a reboot, with every
// state Unknown, and the mask is the only record of what the broker still holds. A bench
// row in neither gets nothing, because nothing on its topic needs withdrawing.
bool bench_withdrawal_owed(const NodeInfo& info, Availability state,
                           uint16_t retained_online);

}  // namespace bridge
