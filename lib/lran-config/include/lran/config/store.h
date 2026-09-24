// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The three layers of spec 7.4 and GateLink PRD R-5.3c, with the bottom one injected:
// compiled defaults from the table, overrides held here in RAM, and a nonvolatile store
// the library never names. The bridge's is NVS and GateLink's is microSD (D49); this
// library sees a Persist* that may be null.
//
// WHAT THIS FILE OWES THE SPECIFICATION. Three properties of 7.4 are load-bearing and are
// implemented here rather than by each firmware: an unknown key is rejected on its own
// with a reason and the rest of the set still applies, the ACK carries the EFFECTIVE value
// rather than the requested one, and persist_status is honest about a store that did not
// take the write.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config/table.h"
#include "lran/schema/node_config_v1.h"
#include "lran/types.h"

namespace lran {
namespace config {

// Injected nonvolatile storage. A node with none still applies and still ACKs, with
// APPLIED_NOT_PERSISTED (spec 8.11) - a missing card must never make a node
// unconfigurable, and HA must never be told a value was saved when it was not.
class Persist {
 public:
  virtual ~Persist()                           = default;
  virtual bool usable() const                  = 0;
  virtual bool save(uint16_t id, Value v)      = 0;
  virtual bool clear_all()                     = 0;

  // spec 12.4 - the PHY group written as one. A reboot between two save() calls would
  // come back on a mixed group, a new frequency with the old SF, which nobody chose and
  // no other radio in the fleet runs. An implementation writes all n or none and returns
  // false in the second case. There is deliberately no default built from save(), because
  // one would compile and be wrong.
  virtual bool save_group(const uint16_t* ids, const Value* values, size_t n) = 0;
};

// Up to three blocks: node-common, this node's own, and the bridge's per-node rows where
// a bridge holds them. Fixed, because root rule 3 forbids allocating here.
inline constexpr size_t kMaxBlocks = 3;

// A table assembled from blocks, in ascending id order. The order is not cosmetic: spec
// 7.4.1 asks a GET_ALL walk to produce ascending param_id across the whole answer, and
// walking the blocks in order is how this produces it without sorting.
class Table {
 public:
  bool add_block(const ParamDef* rows, size_t n);
  const ParamDef* find(uint16_t id) const;
  const ParamDef* at(size_t i) const;
  size_t size() const { return total_; }

 private:
  const ParamDef* blocks_[kMaxBlocks] = {nullptr, nullptr, nullptr};
  size_t counts_[kMaxBlocks]          = {0, 0, 0};
  size_t nblocks_                     = 0;
  size_t total_                       = 0;
};

// Where a readback has reached. Held by the caller, so the store stages nothing.
struct ReadbackCursor {
  size_t  next     = 0;  // index into the table
  uint8_t messages = 0;  // spec 7.4.1 bounds an answer at kMaxConfigAckMessages
};

class Store {
 public:
  Store(const Table& table, Persist* persist) : table_(table), persist_(persist) {}

  Value effective(uint16_t id) const;
  bool  is_override(uint16_t id) const;

  // spec 7.4 - one entry in, one result out. The result carries the effective value in
  // every case but an unknown key, which has none.
  schema::ConfigAckEntry apply(const schema::ConfigEntry& in, bool* applied,
                               bool* persisted);

  // Puts back a value the nonvolatile store held, at boot. Clamped and refused as apply()
  // would be, so a value stored before a range changed cannot come back unchecked, but it
  // never writes the store and never opens a PHY trial: a PHY value in the store is a
  // committed one (spec 12.4 step 2). False when the row is unknown or refuses writes.
  bool restore(uint16_t id, Value v);

  // D52 - RESTORE_DEFAULTS clears every override and is answered as GET_ALL is. It leaves
  // the PHY group and any trial alone and writes the committed group back after clearing
  // the store: a PHY row set to its default would take this node off the fleet's settings,
  // and spec 12.4 lets no node-level operation do that.
  bool restore_defaults();

  // ---- spec 12.4, the PHY group's trial copy (D59) ----
  //
  // A Store answers every Access::Phy row READ_ONLY until its owner calls this, because
  // the table cannot know whether the firmware around it has built the retune, the window
  // and the revert. It stays READ_ONLY whatever this says when the Persist is null or
  // unusable (spec 12.4.2 step 2): a committed PHY value that a reboot forgets strands the
  // node on its compiled defaults.
  void enable_phy_trial() { phy_trial_enabled_ = true; }

  // True while PHY values sit in the trial copy. apply() puts them there and nowhere else:
  // effective() reports them, so the caller retunes from effective() once its CONFIG_ACK
  // has gone out on the old settings (spec 12.4 step 3), and the store is not written.
  bool phy_trial_pending() const { return ntrial_ > 0; }

  // spec 12.4.2 step 5 - confirmation. Writes the whole group through save_group(), then
  // makes the trial values the committed ones. False, with the trial left in place and the
  // store as it was, when the write failed; the caller's window then reverts.
  bool commit_phy_trial();

  // spec 12.4.2 step 6 - the window expired. Drops the trial copy, so effective() is the
  // committed group again and the caller retunes from it. Nothing is written.
  void revert_phy_trial() { ntrial_ = 0; }

  // spec 8.11, D53 - after a read this reports whether the current overrides are
  // persisted, and reads PERSISTED when there are none.
  PersistStatus read_persist_status() const;

  // spec 7.4.1, D57 - fill one message of a readback and advance the cursor. Returns
  // false when the answer is complete, so a caller sends while it returns true. The
  // message it fills carries more_follows when another one follows it.
  bool next_readback_message(ReadbackCursor* cursor, ConfigOp op,
                             schema::NodeConfigAckV1* out) const;

 private:
  struct Override {
    uint16_t id    = 0;
    Value    value = 0;
    bool     set   = false;
  };

  const ParamDef* find(uint16_t id) const { return table_.find(id); }
  Override*       slot(uint16_t id);
  const Override* slot(uint16_t id) const;

  bool      phy_writable() const;
  Override* set_override(uint16_t id, Value v);
  const Override* trial_slot(uint16_t id) const;

  const Table& table_;
  Persist*     persist_ = nullptr;
  Override     overrides_[kMaxTableParams] = {};
  size_t       noverrides_                 = 0;
  bool         phy_trial_enabled_          = false;
  Override     trial_[kPhyGroupSize]       = {};
  size_t       ntrial_                     = 0;
};

// The bytes one result costs on the wire, spec 7.4: param_id, status, ptype, len, value.
constexpr size_t result_bytes(PType t) {
  return schema::kConfigAckEntryHdrLen + ptype_width(t);
}

// spec 7.4.1 - a node must be able to answer its whole table inside the message bound.
// The two constants are set in different files for different reasons, and this is where
// they have to agree: raise kMaxTableParams past this and a node acquires rows it can
// never read back, which no test would catch because the rows above the line simply never
// appear in an answer.
static_assert(kMaxTableParams <=
                  schema::kMaxConfigAckMessages * schema::kMaxConfigAckEntries,
              "spec 7.4.1 - a table larger than the message bound cannot be read back");

}  // namespace config
}  // namespace lran
