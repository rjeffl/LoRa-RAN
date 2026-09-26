// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The bridge's half of a fleet PHY change: spec 12.4.1 steps 3 to 8, and the PHY group's
// NVS record. Task BF-33, slice 2; D59.
//
// ARDUINO-FREE AND IT DOES NO I/O, like config_path.h. next() says what to transmit, when
// to retune and when the change has ended; sched_task builds the frames, asks lora_task
// for the retune, and reports back.
//
// THE BRIDGE RETUNES LAST, AND NO NODE COMMITS UNTIL THE BRIDGE HAS HEARD EVERY NODE. That
// order is what keeps the fleet together (spec 12.4.1). This machine therefore sends no
// authenticated frame between the retune and its own commit. The rest of sched_task must
// not send one either, and blocks_traffic() is what tells it so: a COMMAND or a stray
// CONFIG in that interval confirms whichever node receives it, and a far node that is
// never heard would then leave a near one committed on settings the bridge abandons.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config/phy_blob.h"
#include "lran/config/table.h"
#include "lran/link/radio_config.h"
#include "lran/schema/node_config_v1.h"
#include "lran/types.h"

namespace bridge {

using lran::config::kPhyGroupSize;

// The six rows in one fixed order: frequency, SF, BW, CR, TX power, phy_trial_s. The
// bridge's rows and a node's differ only in id (table.h's phy_rows_agree()), so one index
// names the same parameter on both. test_phy_change checks both lists against the table.
inline constexpr uint16_t kBridgePhyIds[kPhyGroupSize] = {0x0010, 0x0011, 0x0012,
                                                          0x0013, 0x0014, 0x0015};
inline constexpr uint16_t kNodePhyIds[kPhyGroupSize]   = {0x0110, 0x0111, 0x0112,
                                                          0x0113, 0x0114, 0x0115};

enum PhyIndex : size_t {
  kPhyFreq = 0,
  kPhySf,
  kPhyBw,
  kPhyCr,
  kPhyTxPower,
  kPhyTrialS,
};

struct PhyGroup {
  lran::config::Value v[kPhyGroupSize] = {};
  bool operator==(const PhyGroup& o) const;
  bool operator!=(const PhyGroup& o) const { return !(*this == o); }
};

// The bridge's row for an index, and the index of a bridge or node PHY id (kPhyGroupSize
// when it is neither).
const lran::config::ParamDef* bridge_phy_row(size_t i);
size_t                        phy_index_of(uint16_t id);

// The radio settings a group describes. `base` supplies what the group does not carry:
// the sync word, the preamble and the antenna gain, which spec 12.4 keeps fixed. False,
// with `out` untouched, when the result would break D33's EIRP ceiling. The table's range
// already stops tx_power_dbm at that ceiling, so false here means a table and an antenna
// that disagree, and the radio keeps the settings it has.
bool phy_config_from(const PhyGroup& g, const lran::link::PhyConfig& base,
                     lran::link::PhyConfig* out);

// spec 12.4.1 step 3 - one CONFIG SET carrying the whole group, under the node's ids.
void build_phy_set(const PhyGroup& g, lran::schema::NodeConfigV1* out);

// spec 12.4.1 step 7 - one CONFIG GET naming the group. Each entry's len is 0 (spec 8.10).
void build_phy_get(lran::schema::NodeConfigV1* out);

// The PHY group's NVS record is lran-config's phy_blob.h, which the simnode shares. The
// bridge's NvsPersist writes it under one key.
using lran::config::kPhyBlobMax;
using lran::config::kPhyBlobVersion;
using lran::config::phy_blob_decode;
using lran::config::phy_blob_encode;
using lran::config::PhyBlob;

// ---------------------------------------------------------------------------
// The machine.
// ---------------------------------------------------------------------------

enum class PhyAction : uint8_t {
  None,
  SendSet,        // CONFIG SET of `group` to `dst` on the current settings, then on_sent()
  Retune,         // ask lora_task for `group`, then on_retuned() once it has applied it
  SendPoll,       // POLL to `dst` on the new settings, then on_sent()
  Commit,         // step 7 - persist `group` as last known-good and answer config/ack
  SendGet,        // CONFIG GET of the group to `dst`, then on_sent()
  NodeConfirmed,  // `dst` answered the GET; `results` republish its config/state
  Abandon,        // the change ended without a commit; see `reason` and `retuned`
  Readback,       // spec 12.4.1 step 4 - the deferred readback of `dst` is now due
};

// spec 16.7.5's `reason`, less `restart`, which only a boot can report.
enum class PhyReason : uint8_t {
  None,
  NotAccepted,   // step 4 - a node refused, clamped or did not answer
  NotHeard,      // step 8 - a node was not heard on the new settings in time
  // step 7 - the bridge could not write its own last known-good. A change that did not
  // commit is reported as an event (spec 16.7.5), and a failed write did not (D63).
  CommitFailed,
};

struct PhyStep {
  PhyAction    action = PhyAction::None;
  lran::NodeId dst    = 0;
  PhyGroup     group{};

  // Abandon
  PhyReason    reason  = PhyReason::None;
  lran::NodeId culprit = 0;      // the first node that caused it
  bool         retuned = false;  // the bridge had moved, so it retunes back to `group`

  // NodeConfirmed
  const lran::schema::ConfigAckEntry* results      = nullptr;
  size_t                              result_count = 0;
};

struct PhyChangeStats {
  uint32_t started       = 0;
  uint32_t committed     = 0;
  uint32_t not_accepted  = 0;
  uint32_t not_heard     = 0;
  uint32_t get_resent    = 0;
  // W17 (spec 12.4.4) - a node whose GET was never answered before its window closed.
  // The change committed; this node has probably reverted, and nothing here repairs it.
  uint32_t confirm_missed = 0;
};

inline constexpr size_t kMaxPhyFleet = 8;

class PhyChange {
 public:
  void set_ack_timeout_ms(uint32_t ms) { ack_timeout_ms_ = ms; }

  // True from start() until the machine is idle again, including the cooldown after an
  // abandon, while nodes that retuned may still be in their window. spec 12.4.1 step 2's
  // `phy_change_in_progress` refuses a second change for all of it.
  bool busy() const { return phase_ != Phase::Idle; }

  // True from the first CONFIG to the last confirming GET. No other authenticated frame
  // may go to a node in this interval; see the comment at the top of this file.
  bool blocks_traffic() const;

  // Starts a change from `from` to `to` across `fleet`, in that order (step 3). False
  // when one is in progress, the fleet is empty or larger than kMaxPhyFleet, or the two
  // groups are equal.
  bool start(const PhyGroup& from, const PhyGroup& to, const lran::NodeId* fleet, size_t n,
             uint32_t now_ms);

  // Call until it returns None.
  PhyStep next(uint32_t now_ms);

  // The frame from the last SendSet, SendPoll or SendGet is queued, under `seq` for a
  // CONFIG. A frame the TX queue refused is not reported, and the next tick asks again.
  void on_sent(lran::Seq seq, uint32_t now_ms);

  // That frame left lora_task at `aired_ms`. Its ACK timeout, or a step-6 POLL's, counts
  // from then, and so does the window a node that lost only its answer may have opened.
  void on_aired(uint32_t aired_ms);

  // The Commit step's write failed. No GET has gone, so no node has committed: the bridge
  // reverts, and every node reverts on silence.
  void commit_failed();

  // lora_task has applied the settings from the last Retune.
  void on_retuned(uint32_t now_ms);

  // Any frame from `src` passed the receive ladder. `rx_ms` is when lora_task heard it,
  // so a frame heard on the old settings and dequeued after the retune does not count.
  void on_heard(lran::NodeId src, uint32_t rx_ms);

  // A CONFIG_ACK passed the receive ladder. True when this machine claimed it, so the
  // caller does not also hand it to ConfigPath.
  bool on_config_ack(lran::NodeId src, const lran::schema::NodeConfigAckV1& ack,
                     lran::Seq ack_seq, uint32_t now_ms);

  const PhyChangeStats& stats() const { return stats_; }

 private:
  enum class Phase : uint8_t {
    Idle,
    SetDue,         // step 3, fleet_[index_]
    AwaitingSetAck,
    RetuneDue,      // step 5
    AwaitingRetune,
    Hearing,        // step 6
    CommitDue,      // step 7, first half
    GetDue,         // step 7, fleet_[index_]
    AwaitingGet,
    Confirmed,      // fleet_[index_] answered; NodeConfirmed is owed
    AbandonDue,
    Cooldown,       // after an abandon, until every node's window has closed
    ReadbackDue,
  };

  bool     deadline_passed(uint32_t now_ms) const;
  uint32_t trial_ms() const;
  void     abandon(PhyReason reason, lran::NodeId culprit);
  void     next_get(uint32_t now_ms);

  Phase        phase_ = Phase::Idle;
  PhyGroup     from_{};
  PhyGroup     to_{};
  lran::NodeId fleet_[kMaxPhyFleet] = {};
  size_t       nfleet_              = 0;
  size_t       index_               = 0;

  lran::Seq seq_           = 0;
  lran::Seq first_get_seq_ = 0;
  uint8_t   get_attempts_  = 0;
  uint32_t  sent_ms_       = 0;

  bool     have_first_ack_ = false;
  uint32_t first_ack_ms_   = 0;
  uint32_t ack_ms_[kMaxPhyFleet] = {};  // when each node answered step 4: its window opened
  uint32_t last_open_ms_   = 0;  // the latest time a node's window may have opened
  bool     retuned_        = false;
  uint32_t retuned_ms_     = 0;
  bool     heard_[kMaxPhyFleet]     = {};
  uint32_t polled_ms_[kMaxPhyFleet] = {};
  bool     polled_[kMaxPhyFleet]    = {};

  PhyReason    reason_  = PhyReason::None;
  lran::NodeId culprit_ = 0;
  // spec 12.4.1 step 4 - a node whose CONFIG_ACK never arrived is read back once every
  // window has closed, because a node that did retune cannot hear the readback sooner.
  bool         readback_owed_ = false;

  lran::schema::ConfigAckEntry confirm_[lran::schema::kMaxConfigAckEntries] = {};
  size_t                       confirm_count_                               = 0;

  uint32_t       ack_timeout_ms_ = 8000;
  PhyChangeStats stats_;
};

}  // namespace bridge
