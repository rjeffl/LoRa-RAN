// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The diagnostic documents. Task BF-19; spec 14.1, 16.1, 16.2; PRD R-4.4b, 5.1 item 7.
//
// ARDUINO-FREE. sched_task gathers the numbers; everything that decides what the JSON
// says - key names, sentinels, whether it fits - is here and has host tests.
//
// SPEC 16.2 NAMES `lran/<node>/diag/state` BUT DEFINES NO PAYLOAD, as it defines none for
// lran/bridge/version (BF-13). These documents are the bridge's choice and the gap is in
// the engineering log. Two rules hold them to the specification anyway:
//
//   The spec 14.1 names are the keys, spelled from lran::kCounterRegistry and nowhere else.
//   A sentinel is `null`, never a number (root rule 6; B4's "sentinels not published as
//   numbers").
//
// WHERE EACH COUNTER GOES (decided with the operator, 2026-09-14). The discard counters are
// the BRIDGE's, on lran/bridge/diag/state: most discards happen before the MAC check, where
// the `src` byte may be corrupt or forged, so charging one to a node could blame GateLink
// for a stranger's frame. How spec 14.1's "per node by the bridge" attributes a pre-MAC
// discard is raised for v0.12. A node's own document carries its link: RSSI, SNR,
// last_seen, missed_polls, proto_ver.

#pragma once

#include <cstddef>
#include <cstdint>

#include "command.h"
#include "lora_stats.h"
#include "lran/counters.h"
#include "queues.h"
#include "registry.h"

namespace bridge {

// No document gives a diagnostic cadence. One default poll interval: a counter moving
// between two publications is visible within one poll of the frame that moved it.
// Runtime-settable (root rule 8); BF-23 takes it from Home Assistant.
inline constexpr uint16_t kDiagPublishIntervalDefaultS = 60;

// lran/bridge/diag/state - every spec 14.1 counter in kCounterRegistry order, then
// `rx_dropped` (spec 14.1's sum) and `rx_frames`. `rx_unknown_src` is a registry row like
// any other since BF-15a (spec 14 stage 9a); it was published beside them until then.
// Returns the length written, or 0 with out[0] = '\0' when `cap` is too small.
size_t diag_rx_json(const lran::Counters& c, char* out, size_t cap);

// What lran/bridge/diag/radio/state carries: the radio's own diagnostics and the queues'.
struct RadioDiag {
  LoraStats stats;
  uint32_t  tx_frames    = 0;
  uint32_t  cad_backoffs = 0;
  // BF-19a - ERRORs spec 14.2's rate limit withheld. The bridge's own, like the queue
  // statistics beside it: it is not a discard and spec 14.1 does not name it. A number
  // that climbs here means a peer is producing faults faster than one a second.
  uint32_t  errors_suppressed = 0;
  QueueStat queues[kQueueCount];
};
size_t diag_radio_json(const RadioDiag& r, char* out, size_t cap);

// lran/<node>/diag/state - the node's link as the bridge last heard it. `last_seen_s` is an
// age in seconds at `now_ms`, wrap-safe; the bridge has no wall clock to stamp it with.
size_t diag_node_json(const NodeState& s, uint32_t now_ms, char* out, size_t cap);

// `lran/bridge/diag/cmd/state` - the command path's accounting (BF-18).
//
// WHY THESE ARE PUBLISHED AND NOT MERELY LOGGED. The first bench run of BF-18 had to
// read a serial cable to answer "did that command resync?", because the ACK topic
// reports the OUTCOME and a resync is invisible in it - spec 10.3's retry succeeds and
// publishes `acked` exactly like a command that never resynced. `resyncs` is the
// number that separates a healthy link from one whose node is rebooting under it.
size_t diag_command_json(const CommandStats& s, char* out, size_t cap);

}  // namespace bridge
