// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink's committed PHY group in NVS, through Arduino's Preferences. Spec 12.4 step 2,
// 12.4.2 step 2; Impl Plan 6.4.
//
// NVS, NOT THE microSD, THOUGH EVERY OTHER OVERRIDE IS ON THE CARD (operator, 2026-10-13).
// A card that fails after a committed change would boot the node on D1's defaults while the
// bridge stayed on the new settings: the stranded node spec 12.4 exists to prevent, and at
// the gate it costs a walk. Internal flash does not leave with a card. config.json never
// holds a PHY row.
//
// ARDUINO-ONLY, AND THAT IS WHY IT IS ITS OWN FILE. lran-node's phy_trial.h holds the
// policy and takes a BlobStore; this is the one file that knows the bytes live in flash.
// The simnode's nvs_blob.cpp is the same thirty lines under another namespace.

#pragma once

#include <Preferences.h>

#include "lran/node/phy_trial.h"

namespace gatelink {

class NvsBlob final : public lran::node::BlobStore {
 public:
  // Opens the namespace. False when NVS refused it, which leaves the store unusable and
  // every PHY row READ_ONLY - deliberately not a boot failure.
  bool begin();

  bool   usable() const override { return open_; }
  size_t read(uint8_t* out, size_t cap) const override;
  bool   write(const uint8_t* bytes, size_t len) override;
  bool   erase() override;

 private:
  mutable Preferences prefs_;
  bool                open_ = false;
};

}  // namespace gatelink
