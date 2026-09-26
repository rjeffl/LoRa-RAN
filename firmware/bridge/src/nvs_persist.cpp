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

// The PHY group's key. Not of the form `p%04x`, so no row id can collide with it.
inline constexpr const char* kPhyBlobKey     = "phy";
inline constexpr const char* kBenchOnlineKey = "bench_on";

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
  global_ = global;
  open_   = prefs_.begin(ns, /*readOnly=*/false);
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
  const lran::config::ParamDef* rows[lran::config::kMaxTableParams];
  const size_t total = scope_rows(global_ ? ConfigScope::Bridge : ConfigScope::Node, rows,
                                  lran::config::kMaxTableParams);
  bool ok = true;
  for (size_t i = 0; i < total; ++i) {
    if (phy_index_of(rows[i]->id) != kPhyGroupSize) continue;  // in the blob
    char key[kNvsNameMax];
    if (nvs_key_for(rows[i]->id, key, sizeof(key)) == 0) continue;
    if (prefs_.isKey(key) && !prefs_.remove(key)) ok = false;
  }
  return ok;
}

bool NvsPersist::read_blob(PhyBlob* out) const {
  if (!open_ || !prefs_.isKey(kPhyBlobKey)) return false;
  uint8_t      buf[kPhyBlobMax];
  const size_t len = prefs_.getBytesLength(kPhyBlobKey);
  if (len > sizeof(buf) || prefs_.getBytes(kPhyBlobKey, buf, len) != len) return false;
  return phy_blob_decode(buf, len, out);
}

bool NvsPersist::write_blob(const PhyBlob& b) {
  if (!open_) return false;
  uint8_t      buf[kPhyBlobMax];
  const size_t len = phy_blob_encode(b, buf, sizeof(buf));
  return len > 0 && prefs_.putBytes(kPhyBlobKey, buf, len) == len;
}

bool NvsPersist::save_group(const uint16_t* ids, const lran::config::Value* values, size_t n) {
  if (ids == nullptr || values == nullptr || n > kPhyGroupSize) return false;
  PhyBlob b;
  b.trial_open = false;
  b.ack_owed   = ack_staged_;
  b.ack_rows   = ack_staged_ ? ack_rows_ : 0;
  ack_staged_  = false;
  b.n          = n;
  for (size_t i = 0; i < n; ++i) {
    b.ids[i]    = ids[i];
    b.values[i] = values[i];
  }
  return write_blob(b);
}

void NvsPersist::stage_owed_ack(uint16_t rows) {
  ack_staged_ = true;
  ack_rows_   = rows;
}

bool NvsPersist::owed_ack(uint16_t* rows) const {
  PhyBlob b;
  if (!read_blob(&b) || !b.ack_owed) return false;
  if (rows != nullptr) *rows = b.ack_rows;
  return true;
}

bool NvsPersist::clear_owed_ack() {
  PhyBlob b;
  if (!read_blob(&b)) return false;
  if (!b.ack_owed) return true;
  b.ack_owed = false;
  b.ack_rows = 0;
  return write_blob(b);
}

uint16_t NvsPersist::bench_online() const {
  if (!open_ || !prefs_.isKey(kBenchOnlineKey)) return 0;
  return prefs_.getUShort(kBenchOnlineKey, 0);
}

bool NvsPersist::save_bench_online(uint16_t mask) {
  if (!open_) return false;
  return prefs_.putUShort(kBenchOnlineKey, mask) == sizeof(uint16_t);
}

bool NvsPersist::mark_trial(bool open) {
  PhyBlob b;
  if (!read_blob(&b)) b = PhyBlob{};  // never committed: the marker alone, and no group
  b.trial_open = open;
  return write_blob(b);
}

bool NvsPersist::trial_marked() const {
  PhyBlob b;
  return read_blob(&b) && b.trial_open;
}

bool NvsPersist::load(uint16_t id, lran::config::Value* out) const {
  if (!open_ || out == nullptr) return false;
  // A PHY row lives in the blob, never under its own key.
  if (phy_index_of(id) != kPhyGroupSize) {
    PhyBlob b;
    if (!read_blob(&b)) return false;
    for (size_t i = 0; i < b.n; ++i) {
      if (b.ids[i] == id) {
        *out = b.values[i];
        return true;
      }
    }
    return false;
  }
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

    if (store.restore(scope, node, d.id, v)) ++restored;
  }
  return restored;
}

}  // namespace bridge
