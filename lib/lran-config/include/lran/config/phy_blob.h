// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The PHY group's nonvolatile record: spec 12.4, D59. Task BF-33; version 2 for the owed
// config/ack, 2026-09-26.
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
//   version 2: u8 version | u8 flags | u16 ack_rows | u8 n | n x (u16 id, i32 value)
//   version 1: u8 version | u8 flags | u8 n | n x (u16 id, i32 value)
//
// flags bit 0: a trial was open. Bit 1: the commit's answer is owed. A reset between the
// commit and the answer reaching the broker lost the answer on the bench (spec 16.7.5);
// the bit lands in the commit's own write, so a boot finds it whenever it finds the
// group. Version 1 is still read, as a blob with nothing owed, because a bridge flashed
// over a committed group must boot on it.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config/table.h"

namespace lran {
namespace config {

inline constexpr uint8_t kPhyBlobVersion = 2;
inline constexpr size_t  kPhyBlobMax     = 5 + kPhyGroupSize * 6;

struct PhyBlob {
  bool     trial_open = false;
  bool     ack_owed   = false;
  // Two bits per group row, in the owner's own encoding. The bridge's is config_path.h's
  // phy_ack_rows(); a node writes zero.
  uint16_t ack_rows   = 0;
  size_t   n          = 0;
  uint16_t ids[kPhyGroupSize]    = {};
  Value    values[kPhyGroupSize] = {};
};

// The bytes written, always version 2, or 0 when `cap` is too small or the blob holds
// more than a group.
size_t phy_blob_encode(const PhyBlob& b, uint8_t* out, size_t cap);

// Reads version 1 or 2. False for a blob of an unknown version or a malformed one, which
// the caller treats as no stored group: the radio then boots on the table's defaults,
// which is D1's envelope.
bool phy_blob_decode(const uint8_t* in, size_t len, PhyBlob* out);

}  // namespace config
}  // namespace lran
