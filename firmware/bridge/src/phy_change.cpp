// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-33, slice 2; spec 12.4.1, 12.4.3; D59.

#include "phy_change.h"

namespace bridge {
namespace {

// Signed, so a margin larger than the window reads as a deadline already passed.
int64_t since(uint32_t now_ms, uint32_t then_ms) {
  return static_cast<int64_t>(static_cast<int32_t>(now_ms - then_ms));
}

// Empties a CONFIG in place. Not `*out = NodeConfigV1{}`: that temporary crashed GCC 13's
// gimplifier on CI's Ubuntu 24.04 runner (internal compiler error in gimple_add_tmp_var,
// 2026-09-24), though clang compiled it. The fields are cleared one by one instead.
void clear_config(lran::schema::NodeConfigV1* out, lran::ConfigOp op) {
  out->op    = op;
  out->count = 0;
  for (size_t i = 0; i < lran::schema::kMaxConfigEntries; ++i) {
    lran::schema::ConfigEntry& e = out->entries[i];
    e.param_id = 0;
    e.ptype    = lran::PType::U8;
    e.len      = 0;
    for (size_t b = 0; b < lran::schema::kMaxParamValueLen; ++b) e.value[b] = 0;
  }
}

}  // namespace

bool PhyGroup::operator==(const PhyGroup& o) const {
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    if (v[i] != o.v[i]) return false;
  }
  return true;
}

const lran::config::ParamDef* bridge_phy_row(size_t i) {
  if (i >= kPhyGroupSize) return nullptr;
  for (size_t k = 0; k < lran::config::kBridgeParamCount; ++k) {
    if (lran::config::kBridgeParams[k].id == kBridgePhyIds[i]) {
      return &lran::config::kBridgeParams[k];
    }
  }
  return nullptr;
}

size_t phy_index_of(uint16_t id) {
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    if (kBridgePhyIds[i] == id || kNodePhyIds[i] == id) return i;
  }
  return kPhyGroupSize;
}

bool phy_config_from(const PhyGroup& g, const lran::link::PhyConfig& base,
                     lran::link::PhyConfig* out) {
  if (out == nullptr) return false;
  lran::link::PhyConfig p = base;
  p.freq_hz       = static_cast<uint32_t>(g.v[kPhyFreq]);
  p.sf            = static_cast<uint8_t>(g.v[kPhySf]);
  // The table counts whole kHz and PhyConfig tenths, because spec 12.1 allows 62.5.
  p.bw_khz10      = static_cast<uint16_t>(g.v[kPhyBw] * 10);
  p.cr_denom      = static_cast<uint8_t>(g.v[kPhyCr]);
  p.conducted_dbm = static_cast<int8_t>(g.v[kPhyTxPower]);
  if (!lran::link::within_eirp_ceiling(p)) return false;
  *out = p;
  return true;
}

void build_phy_set(const PhyGroup& g, lran::schema::NodeConfigV1* out) {
  clear_config(out, lran::ConfigOp::Set);
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    const lran::config::ParamDef* d = bridge_phy_row(i);
    uint32_t                      raw = static_cast<uint32_t>(g.v[i]);
    switch (lran::config::ptype_width(d->type)) {
      case 1: raw &= 0xFFu; break;
      case 2: raw &= 0xFFFFu; break;
      default: break;
    }
    lran::schema::entry_pack(&out->entries[out->count++], kNodePhyIds[i], d->type, raw);
  }
}

void build_phy_get(lran::schema::NodeConfigV1* out) {
  clear_config(out, lran::ConfigOp::Get);
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    out->entries[i].param_id = kNodePhyIds[i];
    out->entries[i].ptype    = bridge_phy_row(i)->type;
  }
  out->count = static_cast<uint8_t>(kPhyGroupSize);
}

// ---------------------------------------------------------------------------

bool PhyChange::blocks_traffic() const {
  switch (phase_) {
    case Phase::Idle:
    case Phase::AbandonDue:
    case Phase::Cooldown:
    case Phase::ReadbackDue:
      return false;
    default:
      return true;
  }
}

uint32_t PhyChange::trial_ms() const { return static_cast<uint32_t>(to_.v[kPhyTrialS]) * 1000u; }

// spec 12.4.1 step 8 - phy_trial_s from the first node's CONFIG_ACK, less one
// config_ack_timeout_ms per node, so step 7 has time for one GET to every node.
bool PhyChange::deadline_passed(uint32_t now_ms) const {
  if (!have_first_ack_) return false;
  const int64_t budget = static_cast<int64_t>(trial_ms()) -
                         static_cast<int64_t>(nfleet_) * static_cast<int64_t>(ack_timeout_ms_);
  return since(now_ms, first_ack_ms_) >= budget;
}

bool PhyChange::start(const PhyGroup& from, const PhyGroup& to, const lran::NodeId* fleet,
                      size_t n, uint32_t now_ms) {
  if (phase_ != Phase::Idle || fleet == nullptr || n == 0 || n > kMaxPhyFleet || from == to) {
    return false;
  }
  // Every field back to its initial value but the two that outlive one change.
  const uint32_t       timeout = ack_timeout_ms_;
  const PhyChangeStats stats   = stats_;
  *this           = PhyChange{};
  ack_timeout_ms_ = timeout;
  stats_          = stats;
  from_   = from;
  to_     = to;
  nfleet_ = n;
  for (size_t i = 0; i < n; ++i) fleet_[i] = fleet[i];
  sent_ms_ = now_ms;
  phase_   = Phase::SetDue;
  ++stats_.started;
  return true;
}

void PhyChange::abandon(PhyReason reason, lran::NodeId culprit) {
  reason_  = reason;
  culprit_ = culprit;
  if (reason == PhyReason::NotAccepted) ++stats_.not_accepted;
  if (reason == PhyReason::NotHeard) ++stats_.not_heard;
  phase_ = Phase::AbandonDue;
}

void PhyChange::next_get(uint32_t now_ms) {
  (void)now_ms;
  ++index_;
  get_attempts_ = 0;
  phase_        = index_ < nfleet_ ? Phase::GetDue : Phase::Idle;
}

PhyStep PhyChange::next(uint32_t now_ms) {
  PhyStep step;
  switch (phase_) {
    case Phase::Idle:
      return step;

    case Phase::SetDue:
      if (deadline_passed(now_ms)) {
        abandon(PhyReason::NotAccepted, fleet_[index_]);
        return next(now_ms);
      }
      step.action = PhyAction::SendSet;
      step.dst    = fleet_[index_];
      step.group  = to_;
      return step;

    case Phase::AwaitingSetAck:
      // spec 7.4's `unknown`. The node may have retuned and lost only its answer, so its
      // window counts from the send, and its readback waits for that window to close.
      if (since(now_ms, sent_ms_) >= ack_timeout_ms_) {
        readback_owed_ = true;
        last_open_ms_  = sent_ms_;
        abandon(PhyReason::NotAccepted, fleet_[index_]);
        return next(now_ms);
      }
      if (deadline_passed(now_ms)) {
        last_open_ms_ = sent_ms_;
        abandon(PhyReason::NotAccepted, fleet_[index_]);
        return next(now_ms);
      }
      return step;

    case Phase::RetuneDue:
      // Counted as moved from here on. lora_task applies the request on its own pass, so
      // an abandon that follows must retune back whether or not on_retuned() came first.
      retuned_    = true;
      phase_      = Phase::AwaitingRetune;
      step.action = PhyAction::Retune;
      step.group  = to_;
      return step;

    case Phase::AwaitingRetune:
    case Phase::Hearing: {
      size_t unheard = nfleet_;
      for (size_t i = 0; i < nfleet_; ++i) {
        if (!heard_[i]) {
          unheard = i;
          break;
        }
      }
      if (phase_ == Phase::Hearing && unheard == nfleet_) {
        phase_ = Phase::CommitDue;
        return next(now_ms);
      }
      if (deadline_passed(now_ms)) {
        abandon(PhyReason::NotHeard, fleet_[unheard < nfleet_ ? unheard : 0]);
        return next(now_ms);
      }
      if (phase_ == Phase::AwaitingRetune) return step;
      // spec 12.4.1 step 6 - a POLL to each node not yet heard, again after each ACK
      // timeout. POLL is unauthenticated (spec 9.2), so no node counts it as confirmation.
      for (size_t i = 0; i < nfleet_; ++i) {
        if (heard_[i]) continue;
        if (polled_[i] && since(now_ms, polled_ms_[i]) < ack_timeout_ms_) continue;
        index_      = i;
        step.action = PhyAction::SendPoll;
        step.dst    = fleet_[i];
        return step;
      }
      return step;
    }

    case Phase::CommitDue:
      ++stats_.committed;
      index_        = 0;
      get_attempts_ = 0;
      phase_        = Phase::GetDue;
      step.action   = PhyAction::Commit;
      step.group    = to_;
      return step;

    case Phase::GetDue:
      step.action = PhyAction::SendGet;
      step.dst    = fleet_[index_];
      return step;

    case Phase::AwaitingGet:
      if (since(now_ms, sent_ms_) < ack_timeout_ms_) return step;
      // A GET applies nothing, so it may go again (step 7) - until this node's own
      // window has closed, after which an answer could only report the old settings.
      if (since(now_ms, ack_ms_[index_]) >= trial_ms()) {
        ++stats_.confirm_missed;
        next_get(now_ms);
        return next(now_ms);
      }
      ++stats_.get_resent;
      phase_ = Phase::GetDue;
      return next(now_ms);

    case Phase::Confirmed:
      step.action       = PhyAction::NodeConfirmed;
      step.dst          = fleet_[index_];
      step.results      = confirm_;
      step.result_count = confirm_count_;
      next_get(now_ms);
      return step;

    case Phase::AbandonDue:
      step.action  = PhyAction::Abandon;
      step.group   = from_;
      step.reason  = reason_;
      step.culprit = culprit_;
      step.retuned = retuned_;
      phase_       = Phase::Cooldown;
      return step;

    case Phase::Cooldown:
      // Until the last window that may have opened has closed. A second change started
      // sooner would reach nodes still counting down the first one's window.
      if (since(now_ms, last_open_ms_) < trial_ms()) return step;
      phase_ = readback_owed_ ? Phase::ReadbackDue : Phase::Idle;
      return next(now_ms);

    case Phase::ReadbackDue:
      phase_      = Phase::Idle;
      step.action = PhyAction::Readback;
      step.dst    = culprit_;
      return step;
  }
  return step;
}

void PhyChange::on_sent(lran::Seq seq, uint32_t now_ms) {
  switch (phase_) {
    case Phase::SetDue:
      seq_     = seq;
      sent_ms_ = now_ms;
      phase_   = Phase::AwaitingSetAck;
      return;
    case Phase::GetDue:
      if (get_attempts_ == 0) first_get_seq_ = seq;
      ++get_attempts_;
      seq_     = seq;
      sent_ms_ = now_ms;
      phase_   = Phase::AwaitingGet;
      return;
    case Phase::Hearing:
      polled_[index_]    = true;
      polled_ms_[index_] = now_ms;
      return;
    default:
      return;
  }
}

void PhyChange::commit_failed() {
  if (phase_ != Phase::GetDue || index_ != 0 || get_attempts_ != 0) return;
  --stats_.committed;
  abandon(PhyReason::StoreFailed, 0);
}

void PhyChange::on_retuned(uint32_t now_ms) {
  if (phase_ != Phase::AwaitingRetune) return;
  retuned_ms_ = now_ms;
  phase_      = Phase::Hearing;
}

void PhyChange::on_heard(lran::NodeId src, uint32_t rx_ms) {
  if (phase_ != Phase::Hearing || since(rx_ms, retuned_ms_) < 0) return;
  for (size_t i = 0; i < nfleet_; ++i) {
    if (fleet_[i] == src) heard_[i] = true;
  }
}

bool PhyChange::on_config_ack(lran::NodeId src, const lran::schema::NodeConfigAckV1& ack,
                              lran::Seq ack_seq, uint32_t now_ms) {
  if (phase_ == Phase::AwaitingSetAck) {
    if (src != fleet_[index_] || ack_seq != seq_) return false;
    // spec 12.4.1 step 4 - every PHY entry present, Ok, and carrying exactly the value
    // sent. APPLIED_NOT_PERSISTED is the truth about a trial (D60), so persist_status is
    // not read.
    bool seen[kPhyGroupSize] = {};
    bool accepted            = true;
    for (size_t i = 0; i < ack.count; ++i) {
      const lran::schema::ConfigAckEntry& e = ack.entries[i];
      const size_t k = phy_index_of(e.param_id);
      if (k == kPhyGroupSize) continue;
      seen[k] = true;
      if (e.status != lran::ParamStatus::Ok || e.len == 0 ||
          lran::schema::entry_signed(e.value, e.len, e.ptype) != to_.v[k]) {
        accepted = false;
      }
    }
    for (size_t k = 0; k < kPhyGroupSize; ++k) accepted = accepted && seen[k];

    // A node that clamped one entry may still have applied the rest, so its window counts
    // as open either way.
    ack_ms_[index_] = now_ms;
    last_open_ms_   = now_ms;
    if (!accepted) {
      abandon(PhyReason::NotAccepted, src);
      return true;
    }
    if (!have_first_ack_) {
      have_first_ack_ = true;
      first_ack_ms_   = now_ms;
    }
    ++index_;
    phase_ = index_ < nfleet_ ? Phase::SetDue : Phase::RetuneDue;
    return true;
  }

  if (phase_ == Phase::AwaitingGet || phase_ == Phase::GetDue) {
    if (src != fleet_[index_] || get_attempts_ == 0) return false;
    // An answer to any GET sent to this node, not only the last: a slow answer to the
    // first arrives after the second has gone, and it confirms the node just as well.
    const lran::Seq span = static_cast<lran::Seq>(seq_ - first_get_seq_);
    if (static_cast<lran::Seq>(seq_ - ack_seq) > span) return false;
    confirm_count_ = 0;
    for (size_t i = 0; i < ack.count && confirm_count_ < lran::schema::kMaxConfigAckEntries;
         ++i) {
      confirm_[confirm_count_++] = ack.entries[i];
    }
    phase_ = Phase::Confirmed;
    return true;
  }

  return false;
}

}  // namespace bridge
