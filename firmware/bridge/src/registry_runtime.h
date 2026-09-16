// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The registry as the tasks see it: one instance, its lock, and the platform HMAC and
// HKDF. Task BF-15; Impl Plan 4.2, 5.2.
//
// registry.h is the logic and is host-tested. This file adds only what cannot run on the
// host - mbedTLS and a FreeRTOS mutex - so it stays small.

#pragma once

#include <cstdint>

#include "lran/frame.h"
#include "lran/mac.h"
#include "registry.h"

namespace bridge {

// Derives every node's key and hands lora_task its PeerKeys and IMac. Call once from
// setup(), BEFORE start_tasks(): lora_task reads the keys without a lock, which is safe
// only if they are complete before it runs. False if the mutex could not be created.
bool registry_begin(const uint8_t master[lran::kMasterKeyLen]);

// What a node is. Lock-free, any task.
const NodeInfo* registry_find(lran::NodeId id);
size_t          registry_size();
const NodeInfo& registry_info_at(size_t i);

// What the bridge has learned, under the lock. NOT FROM lora_task, which must never wait.
Observed registry_observe(const lran::Header& hdr, int16_t rssi_dbm, int8_t snr_db,
                          uint32_t now_ms);

// A copy, so the caller never holds a pointer into state another task is writing.
bool registry_state(lran::NodeId id, NodeState* out);

}  // namespace bridge
