// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Queue message types and the drop accounting. Task BF-11; Impl Plan 5.2.
//
// ARDUINO-FREE, like tasks.h and for the same reason: the accounting is where the
// never-block rule becomes visible or invisible, and it is testable at a desk.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config.h"
#include "lran/frame.h"
#include "lran/types.h"

namespace bridge {

// ---------------------------------------------------------------------------
// The RX queue message: a COMPLETE payload that has passed spec 14 stages 1 to 10.
//
// lora_task runs the ladder and reassembly before queueing (Impl Plan 5.2, BF-16), so
// what crosses this boundary is the header and the whole payload - a single frame's,
// or a reassembled set's - and never raw frame bytes. Changed from raw bytes in BF-16:
// BF-11 had app_task decoding, which contradicted 5.2's table, and a frame the ladder
// rejects has no business costing a queue slot.
//
// A COPY, not a view. lran::Frame and RxDelivery point into buffers that the next
// reception overwrites (lran/frame.h); a payload handed across a task boundary is
// exactly that case, so lora_task copies it in.
//
// ~240 bytes per slot times kRxQueueDepth is ~2 KB of static queue storage. That is the
// price of the rule and it is worth paying.
// ---------------------------------------------------------------------------
struct RxMessage {
  lran::Header hdr{};
  uint8_t      payload[lran::kMaxPayloadPlain] = {0};  // spec 11.2's largest set cap
  size_t       payload_len                     = 0;

  // How many frames the payload arrived in. `hdr.frag` describes only the last one.
  uint8_t fragments = 1;

  // spec 9.4 step 3 passed. Always false today: every authenticated type is
  // bridge -> node (spec 9.2), and the ladder refuses one it cannot verify.
  bool mac_verified = false;

  // Radio metadata, from the driver rather than from the frame. Kept alongside
  // because it is per-reception and is gone by the time app_task looks. For a
  // reassembled set these describe the fragment that completed it.
  int16_t rssi_dbm  = 0;
  int8_t  snr_db    = 0;

  // millis() at reception of the completing frame. The bridge's own clock, used for
  // staleness; nothing on the wire carries a timestamp.
  uint32_t rx_millis = 0;

  // BF-27's dummy publish made this message on the serial console; no radio heard it and
  // no ladder passed it. app_task hands it to the publication policy alone: it must not
  // teach the registry a ctx_id, answer a poll or move a node's availability, because no
  // node sent it (dummy.h).
  bool dummy = false;
};

// ---------------------------------------------------------------------------
// The TX queue message. A frame already encoded by its originator, because
// encoding needs the node key and lora_task is not where the registry lives.
// ---------------------------------------------------------------------------
struct TxMessage {
  uint8_t     bytes[lran::kMaxFrame] = {0};
  size_t      len                    = 0;
  lran::NodeId dst                   = 0;  // diagnostics and the raw frame log
};

// ---------------------------------------------------------------------------
// The inbound command queue - BF-18. mqtt_task parses `lran/<node>/cmd/<action>/set`
// and queues one of these; sched_task runs it against the command path.
//
// WHY A QUEUE AND NOT A DIRECT CALL. The parse happens inside PubSubClient's
// callback, on mqtt_task, and the command path belongs to sched_task - which is
// where every airtime decision already lives. A direct call would need the
// scheduler's lock taken from inside a broker callback, and the rule that lock has
// kept since BF-17 is that it is never held across a queue send or a registry call.
//
// CommandRequest is in command.h, with the state machine that consumes it.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Queues and their overflow accounting.
// ---------------------------------------------------------------------------

enum class QueueId : uint8_t {
  Rx = 0,   // lora_task -> app_task
  Publish,  // app_task  -> mqtt_task
  Tx,       // anything  -> lora_task
  Log,      // anything  -> log_task
  Command,  // mqtt_task -> sched_task (BF-18)
  Config,   // mqtt_task -> sched_task (BF-32)
  kCount,
};

inline constexpr size_t kQueueCount = static_cast<size_t>(QueueId::kCount);

// What a full queue does. Every queue here is DropNewest today.
//
// WHY DROP RATHER THAN BLOCK. Blocking on a full queue is how a slow consumer
// reaches back and stops a producer, and the producer that must never be stopped is
// lora_task (Impl Plan 5.2, PRD 1.3 property 2). A frame arriving while the broker
// is reconnecting has to be received; it does not have to be published promptly.
//
// WHY NEWEST RATHER THAN OLDEST. Dropping the oldest is defensible for state, which
// is idempotent and where newest wins - but it is wrong for events, which are not
// interchangeable and drive email and SMS (PRD, Impl Plan 6.3). One policy that is
// wrong for events beats two policies chosen per call site. BF-24 needed no refinement:
// a state document the queue refuses is not recorded as published, so the node's next
// frame queues a fresh one (publish.h). Nor did BF-25: an event the queue refuses is not
// recorded as published, so a retransmission of it can still get through.
//
// THE REAL ANSWER IS THAT THESE QUEUES DO NOT FILL. A depth reached is a defect
// upstream, which is why every drop is counted and why high_water is reported.
enum class OverflowPolicy : uint8_t {
  DropNewest,
};

struct QueueStat {
  uint32_t sent       = 0;  // accepted into the queue
  uint32_t dropped    = 0;  // refused because it was full
  uint32_t high_water = 0;  // deepest occupancy observed
};

// Per-queue counters, owned by whoever creates the queues.
//
// SEPARATE FROM lran::Counters, DELIBERATELY. Spec 14.1 is a normative registry of
// RECEIVE-LADDER discards and it is what schema 0xF0 carries on the wire; a bridge
// queue overflow is neither. Mixing them would put a bridge-local number into a
// node-health field. These are published as bridge diagnostics instead (spec 16).
//
// ROOT RULE 4 SAYS EVERY DISCARD MAPS TO ONE `Status` AND ONE SPEC 14 STAGE, AND
// THIS ONE DOES NOT. The rule was written for the receive ladder, where it is
// exactly right; a queue overflow happens AFTER a frame has passed the ladder and
// has no wire meaning at all. What the rule is really asking for - that no discard
// is silent - is honoured here by a named counter per queue. Recorded in
// docs/bridge/engineering-log.md rather than resolved locally.
class QueueAccounting {
 public:
  // `depth_after` is the occupancy including the item just accepted.
  void record_sent(QueueId id, size_t depth_after);
  void record_dropped(QueueId id);

  const QueueStat& stat(QueueId id) const;

  // True once anything has ever been dropped. The bridge's own health flag: any
  // non-zero value here means a queue was sized wrong or a consumer stalled, and
  // neither is a condition to discover from a chart six weeks later.
  bool any_dropped() const;

 private:
  QueueStat stats_[kQueueCount];
};

// Static storage bytes a queue needs, for xQueueCreateStatic. Exposed so the
// figure can be asserted at a desk rather than discovered as a boot failure when
// the allocation is silently NULL.
constexpr size_t queue_storage_bytes(size_t depth, size_t item_size) {
  return depth * item_size;
}

}  // namespace bridge
