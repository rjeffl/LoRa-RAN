// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-33, slice 3; see phy_trial.h.

#include "phy_trial.h"

namespace simnode {
namespace {

using lran::config::Access;
using lran::config::ParamDef;
using lran::config::PhyBlob;
using lran::config::Value;

// The node's PHY rows, in table order. table.h's phy_rows_agree() already holds them to
// the bridge's, and the bridge's kNodePhyIds lists the same six ids.
const ParamDef* phy_row(size_t i) {
  size_t n = 0;
  for (const ParamDef& d : lran::config::kNodeCommonParams) {
    if (d.access != Access::Phy) continue;
    if (n++ == i) return &d;
  }
  return nullptr;
}

// spec 7.4 - the wire carries `len` bytes little-endian; a negative value is masked here.
uint32_t raw_bits(Value v, lran::PType t) {
  switch (lran::config::ptype_width(t)) {
    case 1: return static_cast<uint32_t>(v) & 0xFFu;
    case 2: return static_cast<uint32_t>(v) & 0xFFFFu;
    default: return static_cast<uint32_t>(v);
  }
}

constexpr size_t kTrialIndex = 5;  // phy_trial_s, the last of the six

bool expired(uint32_t now_ms, uint32_t since_ms, uint32_t span_ms) {
  return now_ms - since_ms >= span_ms;
}

}  // namespace

// ---------------------------------------------------------------------------

bool PhyPersist::write(const PhyBlob& b) {
  if (!usable()) return false;
  uint8_t      buf[lran::config::kPhyBlobMax];
  const size_t len = lran::config::phy_blob_encode(b, buf, sizeof(buf));
  return len > 0 && bytes_->write(buf, len);
}

bool PhyPersist::read(PhyBlob* out) const {
  if (!usable() || out == nullptr) return false;
  uint8_t      buf[lran::config::kPhyBlobMax];
  const size_t len = bytes_->read(buf, sizeof(buf));
  return len > 0 && lran::config::phy_blob_decode(buf, len, out);
}

bool PhyPersist::save_group(const uint16_t* ids, const Value* values, size_t n) {
  if (ids == nullptr || values == nullptr || n > kPhyGroupSize) return false;
  PhyBlob b;
  b.trial_open = false;
  b.n          = n;
  for (size_t i = 0; i < n; ++i) {
    b.ids[i]    = ids[i];
    b.values[i] = values[i];
  }
  return write(b);
}

bool PhyPersist::mark_trial(bool open) {
  PhyBlob b;
  if (!read(&b)) b = PhyBlob{};  // never committed: the marker alone, and no group
  b.trial_open = open;
  return write(b);
}

// ---------------------------------------------------------------------------

bool PhyGroup::operator==(const PhyGroup& o) const {
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    if (v[i] != o.v[i]) return false;
  }
  return true;
}

bool phy_config_from(const PhyGroup& g, const lran::link::PhyConfig& base,
                     lran::link::PhyConfig* out) {
  if (out == nullptr) return false;
  lran::link::PhyConfig p = base;
  p.freq_hz       = static_cast<uint32_t>(g.v[0]);
  p.sf            = static_cast<uint8_t>(g.v[1]);
  // The table counts whole kHz and PhyConfig tenths, because spec 12.1 allows 62.5.
  p.bw_khz10      = static_cast<uint16_t>(g.v[2] * 10);
  p.cr_denom      = static_cast<uint8_t>(g.v[3]);
  p.conducted_dbm = static_cast<int8_t>(g.v[4]);
  if (!lran::link::within_eirp_ceiling(p)) return false;
  *out = p;
  return true;
}

const char* phy_state_name(PhyState s) {
  switch (s) {
    case PhyState::Idle:    return "idle";
    case PhyState::Pending: return "pending";
    case PhyState::Trial:   return "trial";
  }
  return "?";
}

// ---------------------------------------------------------------------------

PhyTrial::PhyTrial(PhyPersist* persist) : persist_(persist), store_(table_, persist) {
  (void)table_.add_block(lran::config::kNodeCommonParams,
                         lran::config::kNodeCommonParamCount);
  committed_ = group();
}

bool PhyTrial::is_phy(uint16_t id) {
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    if (phy_row(i)->id == id) return true;
  }
  return false;
}

bool PhyTrial::writable() const { return persist_ != nullptr && persist_->usable(); }

RevertCause PhyTrial::begin() {
  if (!writable()) return RevertCause::None;
  store_.enable_phy_trial();

  PhyBlob b;
  if (!persist_->read(&b)) return RevertCause::None;  // never committed: D1's defaults
  // Through Store::restore(), so a value stored before a range changed is clamped on the
  // way back, and never through apply(), which would open a trial at every boot.
  for (size_t i = 0; i < b.n; ++i) {
    if (is_phy(b.ids[i])) (void)store_.restore(b.ids[i], b.values[i]);
  }
  committed_ = group();
  if (!b.trial_open) return RevertCause::None;
  (void)persist_->mark_trial(false);
  ++stats_.reverted;
  return RevertCause::Reboot;
}

PhyGroup PhyTrial::group() const {
  PhyGroup g;
  for (size_t i = 0; i < kPhyGroupSize; ++i) g.v[i] = store_.effective(phy_row(i)->id);
  return g;
}

lran::schema::ConfigAckEntry PhyTrial::get(uint16_t id) const {
  lran::schema::ConfigAckEntry out;
  const ParamDef*              d = table_.find(id);
  if (d == nullptr) {
    out.param_id = id;
    out.status   = lran::ParamStatus::UnknownParam;
    out.len      = 0;
    return out;
  }
  lran::schema::entry_pack(&out, id, lran::ParamStatus::Ok, d->type,
                           raw_bits(store_.effective(id), d->type));
  out.is_override = store_.marked_override(id);  // spec 7.4, D68
  return out;
}

lran::schema::ConfigAckEntry PhyTrial::set(const lran::schema::ConfigEntry& in,
                                           bool* applied) {
  if (applied != nullptr) *applied = false;
  if (state_ == PhyState::Trial) {
    ++stats_.refused_in_trial;
    const ParamDef*              d = table_.find(in.param_id);
    lran::schema::ConfigAckEntry out;
    lran::schema::entry_pack(&out, in.param_id, lran::ParamStatus::ReadOnly, d->type,
                             raw_bits(store_.effective(in.param_id), d->type));
    out.is_override = store_.marked_override(in.param_id);
    return out;
  }
  bool persisted = false;
  return store_.apply(in, applied, &persisted);
}

void PhyTrial::on_set(size_t slot, bool accepted, uint8_t members, uint32_t now_ms) {
  if (state_ == PhyState::Trial || !accepted || slot >= 8) return;
  const PhyGroup g = group();
  // A different group from a later identity starts the count again: the board retunes
  // only onto a group every member accepted.
  if (state_ == PhyState::Idle || g != pending_) {
    pending_          = g;
    accepted_         = 0;
    pending_since_ms_ = now_ms;
    state_            = PhyState::Pending;
  }
  accepted_ = static_cast<uint8_t>(accepted_ | (1u << slot));
  if (members != 0 && (accepted_ & members) == members) {
    retune_due_      = true;
    retune_to_trial_ = true;
  }
}

uint32_t PhyTrial::trial_ms() const {
  return static_cast<uint32_t>(store_.effective(phy_row(kTrialIndex)->id)) * 1000u;
}

void PhyTrial::on_retuned(uint32_t now_ms) {
  retune_due_ = false;
  if (!retune_to_trial_) return;
  retune_to_trial_ = false;
  // spec 12.4.2 step 3 - the window opens when the node retunes, using the phy_trial_s the
  // CONFIG carried, which is the trial copy's.
  state_           = PhyState::Trial;
  window_start_ms_ = now_ms;
  window_ms_       = trial_ms();
  ++stats_.trials;
  // spec 12.4.2 step 7. A marker that did not land costs only the Reboot report, because
  // the store still holds the old group and a reboot comes back on it either way.
  (void)persist_->mark_trial(true);
}

void PhyTrial::on_authenticated() {
  if (state_ != PhyState::Trial) return;
  // spec 12.4.2 step 5. save_group() writes the new group and clears the marker at once.
  if (!store_.commit_phy_trial()) {
    ++stats_.commit_failed;  // the trial stays, and the window reverts it
    return;
  }
  committed_ = group();
  state_     = PhyState::Idle;
  accepted_  = 0;
  ++stats_.committed;
}

RevertCause PhyTrial::tick(uint32_t now_ms) {
  if (state_ == PhyState::Pending && !retune_due_ &&
      expired(now_ms, pending_since_ms_, trial_ms())) {
    // The bridge abandoned the change before every member accepted. The radio never
    // moved, so there is nothing to revert on air and nothing to report.
    store_.revert_phy_trial();
    state_    = PhyState::Idle;
    accepted_ = 0;
    ++stats_.abandoned;
    return RevertCause::None;
  }
  if (state_ != PhyState::Trial || !expired(now_ms, window_start_ms_, window_ms_)) {
    return RevertCause::None;
  }
  // spec 12.4.2 step 6. The store never held the trial group, so dropping the copy is the
  // whole revert; the radio retunes from group(), which is the committed group again.
  store_.revert_phy_trial();
  (void)persist_->mark_trial(false);
  state_           = PhyState::Idle;
  accepted_        = 0;
  retune_due_      = true;
  retune_to_trial_ = false;
  ++stats_.reverted;
  return RevertCause::Window;
}

bool PhyTrial::reset_to_defaults() {
  if (!writable()) return false;
  store_.revert_phy_trial();
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    (void)store_.restore(phy_row(i)->id, phy_row(i)->def);
  }
  committed_       = group();
  state_           = PhyState::Idle;
  accepted_        = 0;
  retune_due_      = true;
  retune_to_trial_ = false;
  return persist_->erase();
}

uint32_t PhyTrial::window_left_ms(uint32_t now_ms) const {
  if (state_ != PhyState::Trial) return 0;
  const uint32_t used = now_ms - window_start_ms_;
  return used >= window_ms_ ? 0 : window_ms_ - used;
}

}  // namespace simnode
