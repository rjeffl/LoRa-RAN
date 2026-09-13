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
#include "lran/types.h"

namespace bridge {

// ---------------------------------------------------------------------------
// The RX queue message.
//
// A COPY, not a view. lran::Frame points into the buffer it was decoded from and
// "MUST NOT outlive the buffer" (lran/frame.h); a frame handed across a task
// boundary is exactly that case, and the library says the copy is the caller's
// responsibility. So lora_task copies the bytes in and app_task decodes from its
// own copy - the alternative is a Frame whose payload pointer aims at whatever the
// radio has received since.
//
// 222 bytes per slot times kRxQueueDepth is ~1.8 KB of static queue storage. That
// is the price of the rule and it is worth paying.
// ---------------------------------------------------------------------------
struct RxMessage {
  uint8_t bytes[lran::kMaxFrame] = {0};
  size_t  len                    = 0;

  // Radio metadata, from the driver rather than from the frame. Kept alongside
  // because it is per-reception and is gone by the time app_task looks.
  int16_t rssi_dbm  = 0;
  int8_t  snr_db    = 0;

  // millis() at reception. The bridge's own clock, used for staleness and for the
  // reassembly window; nothing on the wire carries a timestamp.
  uint32_t rx_millis = 0;
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
// Queues and their overflow accounting.
// ---------------------------------------------------------------------------

enum class QueueId : uint8_t {
  Rx = 0,   // lora_task -> app_task
  Publish,  // app_task  -> mqtt_task
  Tx,       // anything  -> lora_task
  Log,      // anything  -> log_task
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
// wrong for events beats two policies chosen per call site, and BF-24/BF-25 own the
// per-class refinement with the publication policy in hand.
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
