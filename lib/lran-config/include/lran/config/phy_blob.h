// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The PHY group's nonvolatile record: spec 12.4, D59. Task BF-33.
//
// ONE BLOB, SO ONE WRITE. Six keys would be six commits, and a reboot between two of them
// brings the radio back on a group nobody chose (store.h's save_group()). The same blob
// carries the trial marker for the same reason: the write that commits a new group also
// clears the marker, so a boot never sees one without the other.
//
// Shared by every firmware that persists the group - the bridge's NVS and the simnode's -
// so the layout is written once. The library still names no storage technology; a
// Persist implementation stores these bytes wherever it stores things.
//
// Serialized field by field, little-endian (root rule 1), though it never crosses the
// air: the layout then outlives a compiler change, and a firmware update reads the blob
// the previous image wrote.
//
//   u8 version (1) | u8 flags (bit 0: a trial was open) | u8 n | n x (u16 id, i32 value)

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config/table.h"

namespace lran {
namespace config {

inline constexpr uint8_t kPhyBlobVersion = 1;
inline constexpr size_t  kPhyBlobMax     = 3 + kPhyGroupSize * 6;

struct PhyBlob {
  bool     trial_open = false;
  size_t   n          = 0;
  uint16_t ids[kPhyGroupSize]    = {};
  Value    values[kPhyGroupSize] = {};
};

// The bytes written, or 0 when `cap` is too small or the blob holds more than a group.
size_t phy_blob_encode(const PhyBlob& b, uint8_t* out, size_t cap);

// False for a blob of another version or a malformed one, which the caller treats as no
// stored group: the radio then boots on the table's defaults, which is D1's envelope.
bool phy_blob_decode(const uint8_t* in, size_t len, PhyBlob* out);

}  // namespace config
}  // namespace lran
