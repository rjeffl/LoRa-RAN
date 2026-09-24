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

bool config_set_reaches_node(ConfigScope scope, const ConfigSetRequest& req) {
  if (scope != ConfigScope::Node) return false;
  if (req.op == lran::ConfigOp::GetAll || req.op == lran::ConfigOp::RestoreDefaults) {
    return true;
  }
  for (size_t i = 0; i < req.count; ++i) {
    const ParamDef* d = find_param(scope, req.entries[i].name);
    // A node's PHY row is answered read_only here and never sent (spec 16.7.1).
    if (d != nullptr && d->owner == Owner::Node && d->access != lran::config::Access::Phy &&
        req.entries[i].value_readable) {
      return true;
    }
  }
  return false;
}

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

const ParamDef* find_param_by_id(ConfigScope scope, uint16_t id) {
  const ParamDef* rows[lran::config::kMaxTableParams];
  const size_t    n = scope_rows(scope, rows, lran::config::kMaxTableParams);
  for (size_t i = 0; i < n; ++i) {
    if (rows[i]->id == id) return rows[i];
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

  bridge_store_   = new (bridge_storage_) lran::config::Store(bridge_table_, global);
  global_persist_ = global;
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

    // spec 16.7.1 - a PHY row on a node's topic is answered read_only, carrying the value
    // the bridge last read back from that node, and no CONFIG goes. Only the bridge's
    // topic can move the fleet (spec 12.4).
    if (d->owner == Owner::Node && d->access == lran::config::Access::Phy) {
      ConfigResult r;
      copy_name(r.name, sizeof(r.name), d->name);
      r.status    = ResultStatus::ReadOnly;
      r.has_value = mirror_value(node, d->id, &r.value);
      results[n++] = r;
      continue;
    }

    // The fleet machine's rows. The caller answered them from phy_request().
    if (d->owner == Owner::BridgeGlobal && d->access == lran::config::Access::Phy &&
        phy_trial_enabled_) {
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
      // Spec 16.7.4 - the node's own value, from the last complete readback, or null when
      // none has arrived. Null is a different statement from "equal to the default".
      e.has_value = mirror_value(node, d.id, &e.value);
      // INFERRED, NOT REPORTED. CONFIG_ACK carries no override flag, so an override equal
      // to its default reads `default` here. W15 tracks the gap; GateLink PRD R-5.3e is
      // what would close it.
      if (e.has_value) e.is_override = e.value != d.def;
      out[n++] = e;
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

namespace {

size_t mirror_index(lran::NodeId node) {
  for (size_t i = 0; i < kNodeCount; ++i) {
    if (kNodeTable[i].id == node) return i;
  }
  return kNodeCount;
}

}  // namespace

void ConfigStore::note_readback(lran::NodeId node, const lran::schema::ConfigAckEntry* results,
                                size_t n) {
  const size_t index = mirror_index(node);
  if (index == kNodeCount || results == nullptr) return;

  // REPLACED WHOLE. A GET_ALL answer describes the node's entire table, so a row missing
  // from it is a row the node no longer has - not one to keep from an older answer.
  for (size_t i = 0; i < kMirrorRows; ++i) mirror_[index][i] = Mirror{};
  note_set_results(node, results, n);
}

void ConfigStore::note_set_results(lran::NodeId node,
                                   const lran::schema::ConfigAckEntry* results, size_t n) {
  const size_t index = mirror_index(node);
  if (index == kNodeCount || results == nullptr) return;

  for (size_t i = 0; i < n; ++i) {
    const lran::schema::ConfigAckEntry& e = results[i];
    // spec 8.12 - a result that is not Ok reports no effective value to mirror.
    if (e.status != lran::ParamStatus::Ok || e.len == 0) continue;
    const lran::config::Value v = lran::schema::entry_signed(e.value, e.len, e.ptype);

    Mirror* slot = nullptr;
    for (size_t k = 0; k < kMirrorRows; ++k) {
      if (mirror_[index][k].set && mirror_[index][k].id == e.param_id) {
        slot = &mirror_[index][k];
        break;
      }
      if (slot == nullptr && !mirror_[index][k].set) slot = &mirror_[index][k];
    }
    if (slot == nullptr) continue;  // more rows than this bridge mirrors
    slot->id    = e.param_id;
    slot->value = v;
    slot->set   = true;
  }
}

bool ConfigStore::mirror_value(lran::NodeId node, uint16_t id, lran::config::Value* out) const {
  const size_t index = mirror_index(node);
  if (index == kNodeCount) return false;
  for (size_t m = 0; m < kMirrorRows; ++m) {
    if (mirror_[index][m].set && mirror_[index][m].id == id) {
      *out = mirror_[index][m].value;
      return true;
    }
  }
  return false;
}

bool ConfigStore::restore(ConfigScope scope, lran::NodeId node, uint16_t id,
                          lran::config::Value v) {
  const ParamDef* d = find_param_by_id(scope, id);
  if (d == nullptr || d->owner == Owner::Node) return false;
  lran::config::Store* store = d->owner == Owner::BridgeGlobal ? bridge_store_ : store_for(node);
  return store != nullptr && store->restore(id, v);
}

void ConfigStore::enable_phy_trial() {
  if (bridge_store_ == nullptr) return;
  bridge_store_->enable_phy_trial();
  // spec 12.4.2 step 2, which binds the bridge for the same reason: a group committed to
  // a store that cannot keep it comes back at boot as the defaults, while the fleet stays
  // where it was sent. Without a usable store the rows go on answering READ_ONLY.
  phy_trial_enabled_ = global_persist_ != nullptr && global_persist_->usable();
}

bool ConfigStore::phy_request(const ConfigSetRequest& req, PhyRequest* out) const {
  *out        = PhyRequest{};
  out->target = phy_group();
  if (!phy_trial_enabled_) return false;
  for (size_t i = 0; i < req.count; ++i) {
    const ParamDef* d = find_param(ConfigScope::Bridge, req.entries[i].name);
    if (d == nullptr || d->access != lran::config::Access::Phy || !req.entries[i].value_readable) {
      continue;
    }
    const size_t k = phy_index_of(d->id);
    if (k == kPhyGroupSize) continue;
    const lran::config::Value asked = req.entries[i].value;
    const lran::config::Value v     = asked < d->min ? d->min : asked > d->max ? d->max : asked;
    out->any          = true;
    out->named[k]     = true;
    out->status[k]    = v == asked ? ResultStatus::Ok : ResultStatus::Clamped;
    out->target.v[k]  = v;
  }
  return out->any;
}

PhyGroup ConfigStore::phy_group() const {
  PhyGroup g;
  for (size_t i = 0; i < kPhyGroupSize; ++i) g.v[i] = global_value(kBridgePhyIds[i]);
  return g;
}

bool ConfigStore::begin_phy_trial(const PhyGroup& g) {
  if (bridge_store_ == nullptr || !phy_trial_enabled_) return false;
  bool ok = true;
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    const ParamDef* d = bridge_phy_row(i);
    bool applied = false, persisted = false;
    const lran::schema::ConfigAckEntry r =
        bridge_store_->apply(entry_for(*d, g.v[i]), &applied, &persisted);
    ok = ok && r.status == lran::ParamStatus::Ok;
  }
  if (!ok) bridge_store_->revert_phy_trial();
  return ok;
}

bool ConfigStore::commit_phy_trial() {
  return bridge_store_ != nullptr && bridge_store_->commit_phy_trial();
}

void ConfigStore::revert_phy_trial() {
  if (bridge_store_ != nullptr) bridge_store_->revert_phy_trial();
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
