// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-32; D49.

#include "nvs_persist.h"

#include <cstdio>
#include <cstring>

namespace bridge {
namespace {

// NVS caps a key and a namespace at 15 characters. Both helpers below refuse rather than
// truncate, because a truncated namespace is a real and wrong one - two nodes would share
// a store and each would overwrite the other's values.
inline constexpr size_t kNvsNameMax = 16;  // 15 plus the terminator

}  // namespace

size_t nvs_key_for(uint16_t param_id, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  const int n = std::snprintf(out, cap, "p%04x", static_cast<unsigned>(param_id));
  if (n < 0 || static_cast<size_t>(n) >= cap || static_cast<size_t>(n) >= kNvsNameMax) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t nvs_namespace_for(bool global, lran::NodeId node, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  const int n = global ? std::snprintf(out, cap, "cfg")
                       : std::snprintf(out, cap, "cfg_%02x", static_cast<unsigned>(node));
  if (n < 0 || static_cast<size_t>(n) >= cap || static_cast<size_t>(n) >= kNvsNameMax) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

bool NvsPersist::begin(bool global, lran::NodeId node) {
  char ns[kNvsNameMax];
  if (nvs_namespace_for(global, node, ns, sizeof(ns)) == 0) {
    open_ = false;
    return false;
  }
  open_ = prefs_.begin(ns, /*readOnly=*/false);
  return open_;
}

bool NvsPersist::save(uint16_t id, lran::config::Value v) {
  if (!open_) return false;
  char key[kNvsNameMax];
  if (nvs_key_for(id, key, sizeof(key)) == 0) return false;
  // putInt returns the bytes written. NVS itself skips a write whose value is unchanged,
  // so replaying the whole set at boot does not cost a flash erase per parameter.
  return prefs_.putInt(key, static_cast<int32_t>(v)) == sizeof(int32_t);
}

bool NvsPersist::clear_all() {
  if (!open_) return false;
  return prefs_.clear();
}

bool NvsPersist::load(uint16_t id, lran::config::Value* out) const {
  if (!open_ || out == nullptr) return false;
  char key[kNvsNameMax];
  if (nvs_key_for(id, key, sizeof(key)) == 0) return false;
  if (!prefs_.isKey(key)) return false;
  *out = prefs_.getInt(key, 0);
  return true;
}

size_t nvs_restore(ConfigStore& store, ConfigScope scope, lran::NodeId node,
                   const NvsPersist& persist) {
  if (!persist.usable()) return 0;

  const lran::config::ParamDef* rows[lran::config::kMaxTableParams];
  const size_t total = scope_rows(scope, rows, lran::config::kMaxTableParams);

  size_t restored = 0;
  for (size_t i = 0; i < total; ++i) {
    const lran::config::ParamDef& d = *rows[i];
    // A node's own rows are not the bridge's to hold. What the node has stored is the
    // node's business, and the bridge learns it from a readback.
    if (d.owner == lran::config::Owner::Node) continue;

    lran::config::Value v = 0;
    if (!persist.load(d.id, &v)) continue;

    // One parameter per call. A ConfigSetRequest holds kMaxConfigSetEntries and a scope
    // may have more rows than that, and batching them would buy nothing at boot.
    ConfigSetRequest req;
    req.op    = lran::ConfigOp::Set;
    req.count = 1;
    std::snprintf(req.entries[0].name, sizeof(req.entries[0].name), "%s", d.name);
    req.entries[0].value_readable = true;
    req.entries[0].value          = v;

    ConfigResult result;
    if (store.apply(scope, node, req, &result, 1, nullptr, nullptr) == 1 &&
        (result.status == ResultStatus::Ok || result.status == ResultStatus::Clamped)) {
      ++restored;
    }
  }
  return restored;
}

}  // namespace bridge
