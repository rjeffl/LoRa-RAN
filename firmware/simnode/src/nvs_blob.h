// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The board's PHY blob in NVS, through Arduino's Preferences. Task BF-33, slice 3; spec
// 12.4.2 step 2.
//
// ARDUINO-ONLY, AND THAT IS WHY IT IS ITS OWN FILE. phy_trial.h holds the policy and takes
// a BlobStore; this is the one file that knows the bytes live in flash.
//
// THE PHY GROUP AND THE BOOT COUNT ARE ALL A SIMNODE PERSISTS. Everything else on the board
// resets with it, identities included. The PHY group survives because a board that forgot a
// committed group would come back on D1's defaults while the bridge stayed on the new
// settings - the stranded node spec 12.4 exists to prevent. `phy reset` erases it, and
// leaves the boot count alone.

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

// spec 7.2 offset 68 - boot_count "from nonvolatile storage". Counts this boot and returns
// the count, in its own namespace. 0 when NVS refused, which spec 7.2 reads as unavailable,
// so the count wraps from 0xFFFF to 1.
uint16_t nvs_count_boot();

}  // namespace simnode
