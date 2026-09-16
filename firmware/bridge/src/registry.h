// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The per-node registry. Task BF-15; Impl Plan 4.2; PRD R-3.1b, R-3.1c; spec 5.3, 9.1,
// 10.1, 10.2, 11.3.
//
// ARDUINO-FREE, AND IT HOLDS NO LOCK. An entry has two halves with different rules:
//
//   WHAT A NODE IS - id, type, key, is_bench. Written once by load(), before
//   start_tasks(), and never again. lora_task reads it through PeerKeys without a lock,
//   which is safe only because nothing writes it after the tasks start.
//
//   WHAT THE BRIDGE HAS LEARNED - ctx_id, cmd_seq, last_seen, radio metadata. More than one
//   task writes it, so every access goes through registry_runtime.h's mutex. lora_task
//   never touches this half: it would have to wait for the lock.
//
// NO GATE KNOWLEDGE. A node is a row in kNodeTable. If adding one needs a change anywhere
// but that row, a decoder and a discovery template, the abstraction has leaked (Impl Plan
// 4.2, BG-2). The four bench identities are ordinary rows for the same reason.

#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>

#include "lran/frame.h"
#include "lran/mac.h"
#include "lran/types.h"
#include "rx_ladder.h"

namespace bridge {

// Selects the decoder and the discovery template set (Impl Plan 4.2). Nothing else may
// branch on it.
enum class NodeType : uint8_t { GateLink, WellLink, Simnode };

struct NodeProvision {
  lran::NodeId id;
  NodeType     type;
};

// Impl Plan 4.2 - the registry is loaded from build configuration at boot. Spec 5.3 owns
// the addresses; this table only says which of them this bridge speaks to.
//
// WellLink is provisioned although no WellLink exists: spec 5.3 reserves its address and
// its key derives today, so commissioning one is a node flash and not a bridge change.
inline constexpr NodeProvision kNodeTable[] = {
    {lran::kNodeGateLink, NodeType::GateLink},
    {lran::kNodeWellLink, NodeType::WellLink},
    {lran::kNodeSim0, NodeType::Simnode},
    {lran::kNodeSim1, NodeType::Simnode},
    {lran::kNodeSim2, NodeType::Simnode},
    {lran::kNodeSim3, NodeType::Simnode},
};
inline constexpr size_t kNodeCount = sizeof(kNodeTable) / sizeof(kNodeTable[0]);

// No duplicate, and neither the bridge's own address nor broadcast. A duplicate would
// shadow a row; the other two would give the bridge a key for an address no node owns.
constexpr bool node_table_valid(const NodeProvision* table, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (table[i].id == lran::kNodeBridge || table[i].id == lran::kNodeBroadcast) return false;
    for (size_t j = 0; j < i; ++j) {
      if (table[j].id == table[i].id) return false;
    }
  }
  return true;
}
static_assert(node_table_valid(kNodeTable, kNodeCount), "kNodeTable: duplicate or reserved id");

// spec 11.3 - at least one reassembly set per provisioned node. A row added past this
// fails the build rather than making two nodes share a slot.
static_assert(kNodeCount <= kReassemblySlots, "spec 11.3 - a reassembly slot per node");

// Root rule 6 - sentinels, not zero, for what has not been heard.
inline constexpr int16_t  kRssiUnknown     = INT16_MIN;
inline constexpr int8_t   kSnrUnknown      = INT8_MIN;
inline constexpr uint8_t  kVerUnknown      = UINT8_MAX;
inline constexpr uint16_t kPollIntervalDefaultS = 60;  // Impl Plan 6.1

struct NodeInfo {
  lran::NodeId id       = 0;
  NodeType     type     = NodeType::GateLink;
  bool         is_bench = false;  // derived from the address, spec 5.3; gates publication only
};

struct NodeState {
  // spec 10.1 - the node's boot context. 0 means not yet learned: a node never sends 0.
  lran::CtxId ctx_id = 0;

  // spec 10.2 - the bridge's command seq for this node, reset to 1 on a new ctx_id.
  // Advanced by take_cmd_seq(), with spec 10.5's wrap (BF-18).
  lran::Seq cmd_seq = 1;

  // last_seen_ms means nothing until `heard`; millis() can legitimately be 0.
  bool     heard        = false;
  uint32_t last_seen_ms = 0;

  // Every frame observe() recorded, wrapping. The availability watchdog (BF-20) compares it
  // between ticks to see that a frame arrived: last_seen_ms cannot say so, because
  // missed_polls may have climbed again since the frame reset it.
  uint32_t frames_heard = 0;

  // Counted by the poll scheduler (BF-17), cleared by any valid frame (Impl Plan 6.1), read
  // by the availability watchdog (BF-20). Saturates.
  uint16_t missed_polls = 0;

  // TODO(BF-22): the downgrade decision reads this. Recorded, not yet acted on.
  uint8_t proto_ver = kVerUnknown;

  // The reception that last updated this entry, from the driver (RxMessage).
  int16_t rssi_dbm = kRssiUnknown;
  int8_t  snr_db   = kSnrUnknown;

  // TODO(BF-23): set from Home Assistant. The default until then.
  uint16_t poll_interval_s = kPollIntervalDefaultS;
};

// What a received frame did to its node's entry.
enum class Observed : uint8_t {
  UnregisteredNode,  // no entry; nothing changed
  Heard,             // entry updated, context unchanged
  NewContext,        // entry updated, ctx_id learned and cmd_seq reset (spec 10.1, 10.2)
};

class Registry final : public PeerKeys {
 public:
  Registry();

  // Derives every node's key (spec 9.1) and resets what has been learned. Call once,
  // before start_tasks(). The master key is read here and not kept (R-3.1c).
  void load(const uint8_t master[lran::kMasterKeyLen], lran::IKdf* kdf);

  // --- What a node is. Lock-free, any task, lora_task included. --------------------

  // PeerKeys. Before load() nothing is registered, so every frame is refused.
  const uint8_t* key_for(lran::NodeId src) const override;
  bool           is_registered(lran::NodeId src) const override;

  const NodeInfo* find(lran::NodeId id) const;  // nullptr when unregistered
  size_t          size() const { return kNodeCount; }
  const NodeInfo& info_at(size_t i) const { return entries_[i].info; }

  // --- What the bridge has learned. The caller holds the lock. ---------------------

  // Records one reception from hdr.src. Call only for a frame the ladder delivered.
  Observed observe(const lran::Header& hdr, int16_t rssi_dbm, int8_t snr_db, uint32_t now_ms);

  // A poll to `id` went unanswered (BF-17). False when unregistered.
  bool note_poll_missed(lran::NodeId id);

  // spec 10.2 - hands out this node's next command seq and advances it, with spec
  // 10.5's wrap. BF-18. False when unregistered. A RETRY DOES NOT CALL THIS: root
  // rule 2 reuses the seq of the attempt it repeats.
  bool take_cmd_seq(lran::NodeId id, lran::Seq* out);

  // spec 10.3 step 2 - adopt a ctx_id from a REJECTED_CTX and reset cmd_seq to 1.
  // BF-18. False when unregistered.
  bool adopt_ctx(lran::NodeId id, lran::CtxId ctx);

  const NodeState* state(lran::NodeId id) const;  // nullptr when unregistered

 private:
  struct Entry {
    NodeInfo  info;
    uint8_t   key[lran::kNodeKeyLen] = {0};
    NodeState state;
  };

  int index_of(lran::NodeId id) const;

  Entry entries_[kNodeCount];
  bool  loaded_ = false;
};

}  // namespace bridge
