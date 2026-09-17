// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The per-node registry. Task BF-15; see registry.h.

#include "registry.h"

namespace bridge {

Registry::Registry() {
  for (size_t i = 0; i < kNodeCount; ++i) {
    entries_[i].info.id       = kNodeTable[i].id;
    entries_[i].info.type     = kNodeTable[i].type;
    entries_[i].info.is_bench = lran::is_bench_node(kNodeTable[i].id);
  }
}

void Registry::load(const uint8_t master[lran::kMasterKeyLen], lran::IKdf* kdf) {
  for (Entry& e : entries_) {
    // spec 9.1 - the key follows the address in the frame, so each identity on a
    // multi-identity bench board gets its own, exactly as a separate board would.
    kdf->derive_node_key(master, e.info.id, e.key);
    e.state = NodeState{};
  }
  loaded_ = true;
}

int Registry::index_of(lran::NodeId id) const {
  if (!loaded_) return -1;
  for (size_t i = 0; i < kNodeCount; ++i) {
    if (entries_[i].info.id == id) return static_cast<int>(i);
  }
  return -1;
}

const uint8_t* Registry::key_for(lran::NodeId src) const {
  const int i = index_of(src);
  return i < 0 ? nullptr : entries_[i].key;
}

bool Registry::is_registered(lran::NodeId src) const { return index_of(src) >= 0; }

const NodeInfo* Registry::find(lran::NodeId id) const {
  const int i = index_of(id);
  return i < 0 ? nullptr : &entries_[i].info;
}

const NodeState* Registry::state(lran::NodeId id) const {
  const int i = index_of(id);
  return i < 0 ? nullptr : &entries_[i].state;
}

Observed Registry::observe(const lran::Header& hdr, int16_t rssi_dbm, int8_t snr_db,
                           uint32_t now_ms) {
  const int i = index_of(hdr.src);
  if (i < 0) return Observed::UnregisteredNode;

  NodeState& s   = entries_[i].state;
  s.heard        = true;
  s.last_seen_ms = now_ms;
  ++s.frames_heard;
  s.proto_ver    = hdr.ver;
  // R-3.1f - this frame was decoded, so whatever skew was recorded is over (BF-22).
  s.unsupported_ver = 0;
  s.rssi_dbm     = rssi_dbm;
  s.snr_db       = snr_db;
  // Impl Plan 6.1 - any valid frame from the node, a push included, resets the count.
  s.missed_polls = 0;

  // spec 10.1 - learned from any received frame. A zero ctx_id is not a context, and
  // adopting it would make the bridge address its next command to no context at all.
  if (hdr.ctx_id == 0 || hdr.ctx_id == s.ctx_id) return Observed::Heard;

  // spec 10.2 - a new context means the node rebooted, and its command seq space with it.
  s.ctx_id  = hdr.ctx_id;
  s.cmd_seq = 1;
  return Observed::NewContext;
}

// spec 10.2, 10.5 - the bridge's command seq for this node, and the only place it
// advances. The wrap lives with the counter rather than in command.cpp, so there is
// one place to read when asking what the next seq will be.
//
// SKIPS 0 ON WRAP. A `seq` of 0 is what an entry carries before any command has been
// sent and what a fresh context resets toward; handing 0 out after 0xFFFF would make
// the first command of the ~45th day indistinguishable from an uninitialized one.
bool Registry::take_cmd_seq(lran::NodeId id, lran::Seq* out) {
  const int i = index_of(id);
  if (i < 0) return false;
  lran::Seq& s = entries_[i].state.cmd_seq;
  if (out != nullptr) *out = s;
  const lran::Seq n = static_cast<lran::Seq>(s + 1);
  s                 = (n == 0) ? 1 : n;
  return true;
}

// spec 10.3 step 2 - the resync. Adopt the ctx_id a REJECTED_CTX carried and reset the
// command seq to 1, so the NEXT command starts in the node's current context.
//
// SEPARATE FROM observe(), which learns a context from a frame that arrived. This one
// is driven by the command path's decision to resync, and it runs whether or not the
// ACK that carried the ctx_id has reached observe() yet - the two are on different
// tasks and neither ordering may be assumed.
bool Registry::adopt_ctx(lran::NodeId id, lran::CtxId ctx) {
  const int i = index_of(id);
  if (i < 0) return false;
  entries_[i].state.ctx_id  = ctx;
  entries_[i].state.cmd_seq = 1;
  return true;
}

bool Registry::note_unsupported_version(lran::NodeId id, uint8_t ver) {
  const int i = index_of(id);
  if (i < 0) return false;
  entries_[i].state.unsupported_ver = ver;
  return true;
}

bool Registry::note_poll_missed(lran::NodeId id) {
  const int i = index_of(id);
  if (i < 0) return false;
  uint16_t& m = entries_[i].state.missed_polls;
  if (m < UINT16_MAX) ++m;
  return true;
}

}  // namespace bridge
