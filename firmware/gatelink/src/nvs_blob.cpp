// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink's committed PHY group in NVS; see nvs_blob.h.

#include "nvs_blob.h"

namespace gatelink {
namespace {

// NVS caps a namespace and a key at 15 characters. A name of its own, so a StamPLC
// reflashed from another role never reads that role's group.
constexpr const char* kNamespace = "gatelink_phy";
constexpr const char* kKey       = "phy";

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

}  // namespace gatelink
