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
#include "lran/messages.h"
#include "lran/schema/node_config_v1.h"
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

// BF-17 - sched_task counts an unanswered poll.
bool registry_note_poll_missed(lran::NodeId id);

// A copy, so the caller never holds a pointer into state another task is writing.
bool registry_state(lran::NodeId id, NodeState* out);

// --- BF-18, the command path. --------------------------------------------------

// spec 10.2 - takes this node's next command seq and advances it. False when
// unregistered. A RETRY DOES NOT CALL THIS (root rule 2).
bool registry_take_cmd_seq(lran::NodeId id, lran::Seq* out);

// spec 10.3 step 2 - adopt a ctx_id from a REJECTED_CTX and reset cmd_seq to 1.
bool registry_adopt_ctx(lran::NodeId id, lran::CtxId ctx);

// R-3.1f - a frame from `id` was refused at spec 14 stage 4 carrying `ver` (BF-22).
bool registry_note_unsupported_version(lran::NodeId id, uint8_t ver);

// BF-23 - sched_task applies a node's `poll_interval_s` from the configuration store.
bool registry_set_poll_interval(lran::NodeId id, uint16_t interval_s);

// Builds an authenticated COMMAND to `dst` with that node's derived key (spec 9.2).
// Returns the frame length, or 0 - for an unregistered node as well as an encode
// failure, because a node with no key has no command that can reach it.
//
// THE KEY DOES NOT LEAVE THIS FILE. command.cpp builds the frame from an EncodeCtx
// the caller supplies, which is what keeps it host-testable; this is the one place
// that fills the EncodeCtx in, so no task holds a pointer to key material.
size_t registry_build_command(lran::NodeId dst, lran::CtxId ctx, lran::Seq seq, uint8_t ver,
                              const lran::msg::Command& cmd, uint8_t* buf, size_t cap);

// The same, for an authenticated CONFIG (spec 7.4, 9.2). BF-32. Returns the frame length,
// or 0 - for an unregistered node as well as an encode failure, because a node with no key
// has no configuration that can reach it.
size_t registry_build_config(lran::NodeId dst, lran::CtxId ctx, lran::Seq seq, uint8_t ver,
                             const lran::schema::NodeConfigV1& cfg, uint8_t* buf,
                             size_t cap);

}  // namespace bridge
