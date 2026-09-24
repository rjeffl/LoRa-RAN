// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The board's PHY blob in NVS, through Arduino's Preferences. Task BF-33, slice 3; spec
// 12.4.2 step 2.
//
// ARDUINO-ONLY, AND THAT IS WHY IT IS ITS OWN FILE. phy_trial.h holds the policy and takes
// a BlobStore; this is the one file that knows the bytes live in flash.
//
// THE FIRST THING A SIMNODE PERSISTS. Everything else on the board still resets with it,
// identities included. Only the PHY group survives, because a board that forgot a
// committed group would come back on D1's defaults while the bridge stayed on the new
// settings - the stranded node spec 12.4 exists to prevent. `phy reset` erases it.

#pragma once

#include <Preferences.h>

#include "phy_trial.h"

namespace simnode {

class NvsBlob final : public BlobStore {
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

}  // namespace simnode
