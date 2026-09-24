// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The simnode's half of a fleet PHY change: spec 12.4.2, D59. Task BF-33, slice 3.
//
// ARDUINO-FREE AND IT DOES NO I/O. It holds the board's PHY group in lran-config's Store,
// says when the radio should retune, and runs the trial window. main.cpp moves a retune to
// radio.cpp and reports back; nvs_blob.cpp is the only part that touches flash.
//
// ONE GROUP PER BOARD, NOT PER IDENTITY. Up to four identities share one SX1262, and one
// SX1262 listens on one configuration. The bridge sends each identity its own SET in turn,
// on the old settings (spec 12.4.1 step 3), so a board that retuned after its first
// identity's ACK would never hear the SETs for the others. THE BOARD RETUNES ONCE EVERY
// MEMBER HAS ACCEPTED THE SAME GROUP - every enabled identity whose role answers CONFIG
// (decided with the operator 2026-09-24). An enabled identity the bridge does not watch
// therefore holds the board on the old settings, and the change ends `not_heard`: disable
// it for a PHY change. Any authenticated frame to any identity confirms the board.
//
// That is where a simnode stops looking like separate nodes (simnode CLAUDE.md,
// Multi-identity). It is a property of one radio, not a bridge defect.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config/phy_blob.h"
#include "lran/config/store.h"
#include "lran/config/table.h"
#include "lran/link/radio_config.h"
#include "lran/schema/node_config_v1.h"
#include "lran/types.h"

namespace simnode {

using lran::config::kPhyGroupSize;

// The board's nonvolatile bytes. nvs_blob.cpp on the board; a RAM fake in the tests.
class BlobStore {
 public:
  virtual ~BlobStore()                                    = default;
  virtual bool   usable() const                           = 0;
  // Bytes read, or 0 when nothing is stored or it does not fit `cap`.
  virtual size_t read(uint8_t* out, size_t cap) const     = 0;
  virtual bool   write(const uint8_t* bytes, size_t len)  = 0;
  virtual bool   erase()                                  = 0;
};

// lran-config's Persist over one PhyBlob. The simnode persists the PHY group and nothing
// else, so save() refuses: a non-PHY override keeps its APPLIED_NOT_PERSISTED answer.
class PhyPersist final : public lran::config::Persist {
 public:
  explicit PhyPersist(BlobStore* bytes) : bytes_(bytes) {}

  bool usable() const override { return bytes_ != nullptr && bytes_->usable(); }
  bool save(uint16_t, lran::config::Value) override { return false; }
  bool clear_all() override { return false; }
  // Writing the group also clears the trial marker, in the same write (phy_blob.h).
  bool save_group(const uint16_t* ids, const lran::config::Value* values, size_t n) override;

  // spec 12.4.2 step 7 - set when the board retunes, cleared by the commit or the revert.
  // Rewrites the group the blob already holds, so the marker never lands without it.
  bool mark_trial(bool open);

  bool read(lran::config::PhyBlob* out) const;
  bool erase() { return usable() && bytes_->erase(); }

 private:
  bool write(const lran::config::PhyBlob& b);

  BlobStore* bytes_ = nullptr;
};

// The six rows in the table's order: frequency, SF, BW, CR, TX power, phy_trial_s.
struct PhyGroup {
  lran::config::Value v[kPhyGroupSize] = {};
  bool operator==(const PhyGroup& o) const;
  bool operator!=(const PhyGroup& o) const { return !(*this == o); }
};

// The radio settings a group describes; `base` supplies the sync word, the preamble and
// the antenna gain, which spec 12.4 keeps fixed. False, with `out` untouched, when the
// result would break D33's EIRP ceiling. The bridge's phy_change.cpp has the same ten
// lines: a table in whole kHz, a PhyConfig in tenths.
bool phy_config_from(const PhyGroup& g, const lran::link::PhyConfig& base,
                     lran::link::PhyConfig* out);

enum class PhyState : uint8_t {
  Idle,     // the committed group is on the air
  Pending,  // some members accepted a group; the board has not retuned
  Trial,    // retuned, waiting for an authenticated frame
};
const char* phy_state_name(PhyState s);

// spec 12.4.2 step 8's `detail`.
enum class RevertCause : uint16_t { None = 0x0000, Window = 0x0001, Reboot = 0x0002 };

struct PhyTrialStats {
  uint32_t trials         = 0;  // retunes onto a trial group
  uint32_t committed      = 0;
  uint32_t reverted       = 0;  // window expiries
  uint32_t abandoned      = 0;  // a Pending group that never gathered every member
  uint32_t commit_failed  = 0;  // save_group() refused; the window then reverts
  uint32_t refused_in_trial = 0;  // PHY SET entries answered READ_ONLY during a trial
};

class PhyTrial {
 public:
  // nullptr is a board with no store: every PHY row answers READ_ONLY (spec 12.4.2 step 2).
  explicit PhyTrial(PhyPersist* persist);

  PhyTrial(const PhyTrial&)            = delete;
  PhyTrial& operator=(const PhyTrial&) = delete;

  // Boot. Puts back the committed group, and reports Reboot when the blob says a trial was
  // open, clearing the marker (spec 12.4.2 step 7). The radio starts on group().
  RevertCause begin();

  static bool is_phy(uint16_t id);

  // spec 7.4 - one entry of a GET, carrying the effective value.
  lran::schema::ConfigAckEntry get(uint16_t id) const;

  // spec 12.4.2 steps 1 and 2 - one PHY entry of a SET, through lran-config's Store. During
  // a trial a PHY entry answers READ_ONLY: the bridge admits no CONFIG in that interval
  // (phy_change.h's blocks_traffic()), and a second group on top of an unconfirmed one
  // would leave nothing coherent to revert to.
  lran::schema::ConfigAckEntry set(const lran::schema::ConfigEntry& in, bool* applied);

  // After a SET from identity slot `slot` that named any PHY row. `accepted` is true when
  // every PHY entry answered OK; `members` has a bit for every slot that must accept before
  // the board retunes.
  void on_set(size_t slot, bool accepted, uint8_t members, uint32_t now_ms);

  // A retune is owed: to the trial group once every member accepted, or back to the
  // committed one after a revert. main.cpp hands group() to the radio, which applies it
  // once its outbox is empty - the CONFIG_ACK goes out on the old settings (spec 12.4
  // step 3) - and then calls on_retuned().
  bool     retune_due() const { return retune_due_; }
  PhyGroup group() const;
  void     on_retuned(uint32_t now_ms);

  // spec 12.4.2 step 5 - an authenticated frame passed stage 11 on some identity.
  void on_authenticated();

  // Window and Pending expiry. Returns Window on the tick that reverted a trial.
  RevertCause tick(uint32_t now_ms);

  // `phy reset` - the committed group back to the table's defaults and the blob erased,
  // so a bench board left on a stranded group recovers without a reflash. Any trial is
  // dropped. Owes a retune.
  bool reset_to_defaults();

  bool                         writable() const;
  PhyState                     state() const { return state_; }
  uint8_t                      accepted_mask() const { return accepted_; }
  uint32_t                     window_left_ms(uint32_t now_ms) const;
  lran::PersistStatus          read_persist_status() const { return store_.read_persist_status(); }
  const PhyTrialStats&         stats() const { return stats_; }
  PhyGroup                     committed() const { return committed_; }

 private:
  uint32_t trial_ms() const;

  lran::config::Table table_;
  PhyPersist*         persist_;
  lran::config::Store store_;

  PhyState state_       = PhyState::Idle;
  bool     retune_due_  = false;
  bool     retune_to_trial_ = false;
  uint8_t  accepted_    = 0;
  PhyGroup committed_{};
  PhyGroup pending_{};
  uint32_t pending_since_ms_ = 0;
  uint32_t window_start_ms_  = 0;
  uint32_t window_ms_        = 0;
  PhyTrialStats stats_;
};

}  // namespace simnode
