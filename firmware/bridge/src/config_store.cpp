// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-32; spec 16.7.1.

#include "config_store.h"

#include <cstring>
#include <new>

namespace bridge {
namespace {

using lran::config::Owner;
using lran::config::ParamDef;

bool name_is(const ParamDef& d, const char* name) {
  return d.name != nullptr && std::strcmp(d.name, name) == 0;
}

// spec 7.4, D55 - one unit of the row's own ptype, which is what Store::apply accepts.
// A value the parser could not read never reaches here; it is answered `type_mismatch`
// by name before a row is looked up at all.
lran::schema::ConfigEntry entry_for(const ParamDef& d, lran::config::Value v) {
  lran::schema::ConfigEntry e;
  uint32_t                  raw = 0;
  switch (lran::config::ptype_width(d.type)) {
    case 1: raw = static_cast<uint32_t>(v) & 0xFFu; break;
    case 2: raw = static_cast<uint32_t>(v) & 0xFFFFu; break;
    default: raw = static_cast<uint32_t>(v); break;
  }
  lran::schema::entry_pack(&e, d.id, d.type, raw);
  return e;
}

void copy_name(char* out, size_t cap, const char* name) {
  if (out == nullptr || cap == 0) return;
  out[0] = '\0';
  if (name == nullptr) return;
  std::strncpy(out, name, cap - 1);
  out[cap - 1] = '\0';
}

ConfigResult unknown_result(const char* name) {
  ConfigResult r;
  copy_name(r.name, sizeof(r.name), name);
  r.status    = ResultStatus::UnknownParam;
  r.has_value = false;
  return r;
}

}  // namespace

const ParamDef* find_param(ConfigScope scope, const char* name) {
  if (name == nullptr) return nullptr;
  if (scope == ConfigScope::Bridge) {
    for (size_t i = 0; i < lran::config::kBridgeParamCount; ++i) {
      const ParamDef& d = lran::config::kBridgeParams[i];
      if (d.owner == Owner::BridgeGlobal && name_is(d, name)) return &d;
    }
    return nullptr;
  }
  // Node scope: the bridge's per-node rows and the node's own. They share no name today
  // and a collision between the two blocks would be a table defect rather than a lookup
  // question, so the order here is not a precedence rule.
  for (size_t i = 0; i < lran::config::kBridgeParamCount; ++i) {
    const ParamDef& d = lran::config::kBridgeParams[i];
    if (d.owner == Owner::BridgePerNode && name_is(d, name)) return &d;
  }
  for (size_t i = 0; i < lran::config::kNodeCommonParamCount; ++i) {
    const ParamDef& d = lran::config::kNodeCommonParams[i];
    if (name_is(d, name)) return &d;
  }
  return nullptr;
}

size_t scope_rows(ConfigScope scope, const ParamDef** out, size_t cap) {
  size_t n = 0;
  if (scope == ConfigScope::Bridge) {
    for (size_t i = 0; i < lran::config::kBridgeParamCount && n < cap; ++i) {
      const ParamDef& d = lran::config::kBridgeParams[i];
      if (d.owner == Owner::BridgeGlobal) out[n++] = &d;
    }
    return n;
  }
  for (size_t i = 0; i < lran::config::kBridgeParamCount && n < cap; ++i) {
    const ParamDef& d = lran::config::kBridgeParams[i];
    if (d.owner == Owner::BridgePerNode) out[n++] = &d;
  }
  for (size_t i = 0; i < lran::config::kNodeCommonParamCount && n < cap; ++i) {
    out[n++] = &lran::config::kNodeCommonParams[i];
  }
  return n;
}

// ---------------------------------------------------------------------------

ConfigStore::ConfigStore() {}

void ConfigStore::begin(lran::config::Persist* global, lran::config::Persist* const* per_node,
                        size_t n) {
  // SUB-RANGES OF kBridgeParams, NOT COPIES. config_store.h's static_asserts are what
  // keeps that correct; see the comment there for what an inserted row would do.
  bridge_table_ = lran::config::Table{};
  (void)bridge_table_.add_block(&lran::config::kBridgeParams[kBridgeGlobalFirst],
                                kBridgeGlobalCount);

  // The bridge's HALF of a node topic: the per-node rows only. A node's own rows are the
  // node's to apply, and the bridge holds no store for them - it holds the last readback,
  // which is a different thing and lives with the readback.
  node_table_ = lran::config::Table{};
  (void)node_table_.add_block(&lran::config::kBridgeParams[kBridgePerNodeFirst],
                              kBridgePerNodeCount);

  bridge_store_ = new (bridge_storage_) lran::config::Store(bridge_table_, global);
  for (size_t i = 0; i < kNodeCount; ++i) {
    lran::config::Persist* p = (per_node != nullptr && i < n) ? per_node[i] : nullptr;
    node_stores_[i] = new (node_storage_[i]) lran::config::Store(node_table_, p);
  }
}

lran::config::Store* ConfigStore::store_for(lran::NodeId node) {
  for (size_t i = 0; i < kNodeCount; ++i) {
    if (kNodeTable[i].id == node) return node_stores_[i];
  }
  return nullptr;
}

const lran::config::Store* ConfigStore::store_for(lran::NodeId node) const {
  for (size_t i = 0; i < kNodeCount; ++i) {
    if (kNodeTable[i].id == node) return node_stores_[i];
  }
  return nullptr;
}

size_t ConfigStore::apply(ConfigScope scope, lran::NodeId node, const ConfigSetRequest& req,
                          ConfigResult* results, size_t cap, AckPersist* persist,
                          ConfigSetRequest* node_half) {
  size_t n         = 0;
  bool   any_bridge = false;
  bool   all_saved  = true;
  bool   any_applied = false;

  if (node_half != nullptr) {
    *node_half    = ConfigSetRequest{};
    node_half->op = lran::ConfigOp::Set;
  }

  for (size_t i = 0; i < req.count && n < cap; ++i) {
    const ConfigSetEntry& in = req.entries[i];
    const ParamDef*       d  = find_param(scope, in.name);

    if (d == nullptr) {
      results[n++] = unknown_result(in.name);
      continue;
    }

    // spec 16.7.2 - a value the parser could not read is this entry's problem and no
    // other's. It is answered by name and the rest of the set still applies.
    if (!in.value_readable) {
      ConfigResult r;
      copy_name(r.name, sizeof(r.name), in.name);
      r.status    = ResultStatus::TypeMismatch;
      r.has_value = false;
      results[n++] = r;
      continue;
    }

    // The node's half is not applied here and not answered here. It is collected for the
    // caller to send as a CONFIG, and its results arrive with the CONFIG_ACK.
    if (d->owner == Owner::Node) {
      if (node_half != nullptr && node_half->count < kMaxConfigSetEntries) {
        node_half->entries[node_half->count++] = in;
      }
      continue;
    }

    lran::config::Store* store =
        d->owner == Owner::BridgeGlobal ? bridge_store_ : store_for(node);
    if (store == nullptr) {
      // A per-node row aimed at a node the registry does not carry. Answered rather than
      // dropped: the name exists, the target does not.
      results[n++] = unknown_result(in.name);
      continue;
    }

    bool applied   = false;
    bool persisted = false;
    const lran::schema::ConfigAckEntry out =
        store->apply(entry_for(*d, in.value), &applied, &persisted);

    ConfigResult r;
    copy_name(r.name, sizeof(r.name), d->name);
    r.status = result_status_of(out.status);
    if (out.len > 0) {
      r.has_value = true;
      r.value     = lran::schema::entry_signed(out.value, out.len, out.ptype);
    }
    results[n++] = r;

    any_bridge = true;
    if (applied) {
      any_applied = true;
      if (!persisted) all_saved = false;
    }
  }

  if (persist != nullptr) {
    // spec 8.11 - honest about the store. A set whose bridge half applied nothing at all
    // is `not_applied`; one that applied and saved everything is `persisted`.
    if (!any_bridge || !any_applied) {
      *persist = AckPersist::NotApplied;
    } else {
      *persist = all_saved ? AckPersist::Persisted : AckPersist::AppliedNotPersisted;
    }
  }
  return n;
}

size_t ConfigStore::restore_defaults(ConfigScope scope, lran::NodeId node,
                                     ConfigResult* results, size_t cap,
                                     AckPersist* persist) {
  lran::config::Store* store =
      scope == ConfigScope::Bridge ? bridge_store_ : store_for(node);
  if (store != nullptr) {
    // D52 - the return says whether the nonvolatile copy was cleared too. The RAM
    // overrides are gone either way, which is why read_all() below reports the defaults
    // whatever this answered.
    (void)store->restore_defaults();
  }
  return read_all(scope, node, results, cap, persist);
}

size_t ConfigStore::read_all(ConfigScope scope, lran::NodeId node, ConfigResult* results,
                             size_t cap, AckPersist* persist) const {
  const ParamDef* rows[lran::config::kMaxTableParams];
  const size_t    total = scope_rows(scope, rows, lran::config::kMaxTableParams);

  size_t n = 0;
  for (size_t i = 0; i < total && n < cap; ++i) {
    const ParamDef& d = *rows[i];
    // A node's own row has no value here until a readback has brought one back. Spec
    // 16.7.4 makes that `null` rather than the table's default, because the two are
    // different statements about what the node is running.
    if (d.owner == Owner::Node) continue;

    const lran::config::Store* store =
        d.owner == Owner::BridgeGlobal ? bridge_store_ : store_for(node);
    if (store == nullptr) continue;

    ConfigResult r;
    copy_name(r.name, sizeof(r.name), d.name);
    r.status    = ResultStatus::Ok;
    r.has_value = true;
    r.value     = store->effective(d.id);
    results[n++] = r;
  }

  if (persist != nullptr) {
    const lran::config::Store* store =
        scope == ConfigScope::Bridge ? bridge_store_ : store_for(node);
    *persist = store == nullptr ? AckPersist::NotApplied
                                : ack_persist_of(store->read_persist_status());
  }
  return n;
}

size_t ConfigStore::state(ConfigScope scope, lran::NodeId node, ConfigStateEntry* out,
                          size_t cap) const {
  const ParamDef* rows[lran::config::kMaxTableParams];
  const size_t    total = scope_rows(scope, rows, lran::config::kMaxTableParams);

  size_t n = 0;
  for (size_t i = 0; i < total && n < cap; ++i) {
    const ParamDef& d = *rows[i];
    ConfigStateEntry e;
    copy_name(e.name, sizeof(e.name), d.name);

    if (d.owner == Owner::Node) {
      // Spec 16.7.4 - never read back, so null. The readback fills these in.
      e.has_value = false;
      out[n++]    = e;
      continue;
    }

    const lran::config::Store* store =
        d.owner == Owner::BridgeGlobal ? bridge_store_ : store_for(node);
    if (store == nullptr) continue;

    e.has_value   = true;
    e.value       = store->effective(d.id);
    e.is_override = store->is_override(d.id);
    out[n++]      = e;
  }
  return n;
}

lran::config::Value ConfigStore::global_value(uint16_t id) const {
  return bridge_store_ != nullptr ? bridge_store_->effective(id) : 0;
}

lran::config::Value ConfigStore::node_value(lran::NodeId node, uint16_t id) const {
  const lran::config::Store* store = store_for(node);
  if (store != nullptr) return store->effective(id);
  const ParamDef* d = node_table_.find(id);
  return d != nullptr ? d->def : 0;
}

}  // namespace bridge
