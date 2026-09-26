// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The bridge's nonvolatile configuration store - NVS, through Arduino's Preferences.
// Task BF-32; D49; spec 8.11.
//
// ARDUINO-ONLY, AND THAT IS WHY IT IS ITS OWN FILE. `/lib/lran-config/` takes a Persist*
// and never names a storage technology, so the bridge's NVS and GateLink's microSD (D49)
// are each one small file rather than a branch inside the library.
//
// A STORE THAT DOES NOT WORK MUST NOT MAKE THE BRIDGE UNCONFIGURABLE. Every method here
// reports failure rather than throwing or retrying, and Store answers
// APPLIED_NOT_PERSISTED on a false from save(). HA is then told the truth: the value is
// in force and will not survive a reboot.
//
// ONE NAMESPACE PER SCOPE. The global rows are one, and each node's per-node rows are
// another, so a node added later cannot collide with a value already stored. NVS
// namespaces and keys are both capped at 15 characters, which is what bounds the names
// below.

#pragma once

#include <Preferences.h>

#include <cstddef>
#include <cstdint>

#include "config_store.h"
#include "lran/config/store.h"
#include "lran/types.h"
#include "phy_change.h"

namespace bridge {

// `p` plus the param id in hex - `p0002`. Five characters, well inside NVS's 15, and it
// carries no name: a parameter RENAMED in the table keeps its stored value, because the
// id is what identifies a row on the wire (spec 7.4) and a name is what identifies it to
// Home Assistant.
size_t nvs_key_for(uint16_t param_id, char* out, size_t cap);

// `cfg` for the bridge's global rows, `cfg_<id>` for one node's per-node rows.
size_t nvs_namespace_for(bool global, lran::NodeId node, char* out, size_t cap);

class NvsPersist final : public lran::config::Persist {
 public:
  // Opens the namespace. False when NVS refused it, which leaves this store unusable and
  // every write APPLIED_NOT_PERSISTED - deliberately not a boot failure.
  bool begin(bool global, lran::NodeId node);

  bool usable() const override { return open_; }
  bool save(uint16_t id, lran::config::Value v) override;
  // Removes this scope's table keys one at a time, never the namespace. The blob and the
  // bench record below are not table keys, so a RESTORE_DEFAULTS leaves an open trial's
  // marker in place and the committed group never leaves flash (store.h).
  bool clear_all() override;
  // spec 12.4 - the PHY group as one blob under one key, so the group lands in one NVS
  // commit (phy_change.h has the layout). Writing it also clears the trial marker below,
  // and records the answer stage_owed_ack() staged, in the same commit.
  bool save_group(const uint16_t* ids, const lran::config::Value* values, size_t n) override;

  // spec 16.7.5 - the commit's config/ack, owed until mqtt_task has sent it. Staged before
  // the commit so it lands in the commit's own write: a reset 150 ms after a commit lost
  // the answer on 2026-09-24. `rows` is config_path.h's phy_ack_rows(). The next
  // save_group() consumes the stage, whether or not its write succeeds.
  void stage_owed_ack(uint16_t rows);
  bool owed_ack(uint16_t* rows) const;
  bool clear_owed_ack();

  // spec 16.6 - the bench nodes whose availability topic this bridge left `online`,
  // retained, one bit per address from 0xF0 (node_availability.h's bench_bit()). A flag
  // set `applied_not_persisted` comes back clear after a reboot, and this is how the boot
  // knows which `online` to withdraw. Not a table key, so clear_all() keeps it.
  uint16_t bench_online() const;
  bool     save_bench_online(uint16_t mask);

  // spec 12.4.1 - the bridge restarted during a trial if this reads true at boot. Set at
  // step 5 and cleared by the commit or the revert, each by rewriting the blob with the
  // group it already holds, so the marker never lands without the group beside it.
  bool mark_trial(bool open);  // keeps an owed ack beside it
  bool trial_marked() const;

  // What this namespace holds for `id`. False when nothing is stored, which is the
  // ordinary case for a parameter never set.
  bool load(uint16_t id, lran::config::Value* out) const;

 private:
  bool read_blob(PhyBlob* out) const;
  bool write_blob(const PhyBlob& b);

  mutable Preferences prefs_;
  bool                open_       = false;
  bool                global_     = false;
  bool                ack_staged_ = false;
  uint16_t            ack_rows_   = 0;
};

// Replays every stored value of a scope back through the store at boot.
//
// THROUGH Store::restore() RATHER THAN INTO THE OVERRIDES DIRECTLY, so a value stored
// before a range changed is clamped on the way back in and a row that has since become
// READ_ONLY is refused. A store written straight into memory would reinstate a value the
// current firmware would refuse to accept from Home Assistant, which is the one way a
// bridge ends up running a configuration nobody could have set. Not through apply(),
// which a PHY value would turn into a trial at every boot (BF-33).
//
// Returns how many values it put back.
size_t nvs_restore(ConfigStore& store, ConfigScope scope, lran::NodeId node,
                   const NvsPersist& persist);

}  // namespace bridge
