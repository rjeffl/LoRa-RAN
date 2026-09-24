// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-33, slice 3; see nvs_blob.h.

#include "nvs_blob.h"

namespace simnode {
namespace {

// NVS caps a namespace and a key at 15 characters. The bridge's namespaces are `cfg` and
// `cfg_<id>`; a simnode board never shares flash with a bridge, but a distinct name keeps a
// board reflashed from one role to the other from reading the other's group.
inline constexpr const char* kNamespace = "simnode_phy";
inline constexpr const char* kKey       = "phy";

}  // namespace

bool NvsBlob::begin() {
  open_ = prefs_.begin(kNamespace, /*readOnly=*/false);
  return open_;
}

size_t NvsBlob::read(uint8_t* out, size_t cap) const {
  if (!open_ || out == nullptr || !prefs_.isKey(kKey)) return 0;
  const size_t len = prefs_.getBytesLength(kKey);
  if (len == 0 || len > cap) return 0;
  return prefs_.getBytes(kKey, out, len) == len ? len : 0;
}

bool NvsBlob::write(const uint8_t* bytes, size_t len) {
  if (!open_ || bytes == nullptr || len == 0) return false;
  return prefs_.putBytes(kKey, bytes, len) == len;
}

bool NvsBlob::erase() {
  if (!open_) return false;
  return !prefs_.isKey(kKey) || prefs_.remove(kKey);
}

}  // namespace simnode
