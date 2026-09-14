// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-3; see identity.h.

#include "identity.h"

#include <cstring>

namespace simnode {

const char* role_name(Role r) {
  switch (r) {
    case Role::Range:    return "ROLE_RANGE";
    case Role::Health:   return "ROLE_HEALTH";
    case Role::GateLink: return "ROLE_GATELINK";
    case Role::Fault:    return "ROLE_FAULT";
  }
  return "ROLE_?";
}

bool parse_role(const char* token, Role* out) {
  if (token == nullptr || out == nullptr) return false;
  const Role all[] = {Role::Range, Role::Health, Role::GateLink, Role::Fault};
  for (Role r : all) {
    if (std::strcmp(token, role_name(r)) == 0) {
      *out = r;
      return true;
    }
  }
  return false;
}

void IdentityTable::init(const uint8_t master[lran::kMasterKeyLen], lran::IKdf* kdf,
                         RandomFn random) {
  std::memcpy(master_, master, lran::kMasterKeyLen);
  kdf_    = kdf;
  random_ = random;
  for (Identity& e : slots_) clear(e);
}

void IdentityTable::clear(Identity& e) {
  e.used      = false;
  e.id        = 0;
  e.role      = Role::Range;
  e.enabled   = true;
  e.proto_ver = lran::kProtoVer;
  std::memset(e.key, 0, sizeof(e.key));
  e.ctx_id = 0;
  e.tx_seq = 1;
  e.counters.reset();
  e.gate.reset_context(0);
  e.reassembler.reset();
  e.reassembler.forget_completed();
  e.rx_chunk      = 0;
  e.heard         = false;
  e.last_rssi_dbm = lran::kI16NotAvailable;
  e.last_snr_db10 = lran::kI16NotAvailable;
  e.unhandled     = 0;
  e.ping          = PendingPing{};
}

lran::CtxId IdentityTable::random_ctx() {
  // spec 10.1 - non-zero; 0 means "unknown" on the wire. Bounded, so an RNG that returns
  // zero forever produces a fixed context rather than a hung board.
  for (int i = 0; i < 16; ++i) {
    const lran::CtxId v = random_ != nullptr ? random_() : 0;
    if (v != 0) return v;
  }
  return 1;
}

AddResult IdentityTable::add(lran::NodeId id, Role role) {
  if (kdf_ == nullptr) return AddResult::NotReady;
  if (!is_simnode_id(id)) return AddResult::BadId;
  if (find(id) != nullptr) return AddResult::Exists;

  for (Identity& e : slots_) {
    if (e.used) continue;
    clear(e);
    e.used = true;
    e.id   = id;
    e.role = role;
    kdf_->derive_node_key(master_, id, e.key);
    e.ctx_id = random_ctx();
    e.gate.reset_context(e.ctx_id);
    return AddResult::Ok;
  }
  return AddResult::Full;
}

bool IdentityTable::remove(lran::NodeId id) {
  Identity* e = find(id);
  if (e == nullptr) return false;
  clear(*e);
  return true;
}

Identity* IdentityTable::find(lran::NodeId id) {
  for (Identity& e : slots_) {
    if (e.used && e.id == id) return &e;
  }
  return nullptr;
}

const Identity* IdentityTable::find(lran::NodeId id) const {
  for (const Identity& e : slots_) {
    if (e.used && e.id == id) return &e;
  }
  return nullptr;
}

size_t IdentityTable::count() const {
  size_t n = 0;
  for (const Identity& e : slots_) {
    if (e.used) ++n;
  }
  return n;
}

bool IdentityTable::new_context(lran::NodeId id) {
  Identity* e = find(id);
  if (e == nullptr) return false;
  e->ctx_id = random_ctx();
  e->gate.reset_context(e->ctx_id);
  e->tx_seq = 1;
  e->reassembler.reset();
  e->reassembler.forget_completed();
  e->rx_chunk = 0;
  e->ping     = PendingPing{};
  return true;
}

}  // namespace simnode
