// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Queue drop accounting. Task BF-11.

#include "queues.h"

namespace bridge {

void QueueAccounting::record_sent(QueueId id, size_t depth_after) {
  QueueStat& s = stats_[static_cast<size_t>(id)];
  ++s.sent;
  const uint32_t depth = static_cast<uint32_t>(depth_after);
  if (depth > s.high_water) {
    s.high_water = depth;
  }
}

void QueueAccounting::record_dropped(QueueId id) {
  ++stats_[static_cast<size_t>(id)].dropped;
}

const QueueStat& QueueAccounting::stat(QueueId id) const {
  return stats_[static_cast<size_t>(id)];
}

bool QueueAccounting::any_dropped() const {
  for (size_t i = 0; i < kQueueCount; ++i) {
    if (stats_[i].dropped != 0) {
      return true;
    }
  }
  return false;
}

}  // namespace bridge
