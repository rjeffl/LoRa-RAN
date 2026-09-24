// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "lran/config/store.h"

namespace lran {
namespace config {
namespace {

// spec 7.4 - the wire carries `len` bytes little-endian. The codec's entry_pack takes the
// raw bits, so a negative value is converted here rather than there.
uint32_t raw_bits(Value v, PType t) {
  switch (ptype_width(t)) {
    case 1: return static_cast<uint32_t>(v) & 0xFFu;
    case 2: return static_cast<uint32_t>(v) & 0xFFFFu;
    default: return static_cast<uint32_t>(v);
  }
}

Value clamp(Value v, const ParamDef& d) {
  if (v < d.min) return d.min;
  if (v > d.max) return d.max;
  return v;
}

}  // namespace

bool Table::add_block(const ParamDef* rows, size_t n) {
  if (rows == nullptr || n == 0) return false;
  if (nblocks_ >= kMaxBlocks) return false;
  if (total_ + n > kMaxTableParams) return false;
  // Ascending across blocks as well as inside one, so a readback walk needs no sort
  // (spec 7.4.1). A block added out of order is a build-time mistake, and returning
  // false here is how it surfaces instead of producing a shuffled answer.
  if (nblocks_ > 0) {
    const ParamDef* prev = blocks_[nblocks_ - 1];
    if (!(prev[counts_[nblocks_ - 1] - 1].id < rows[0].id)) return false;
  }
  blocks_[nblocks_] = rows;
  counts_[nblocks_] = n;
  ++nblocks_;
  total_ += n;
  return true;
}

const ParamDef* Table::at(size_t i) const {
  for (size_t b = 0; b < nblocks_; ++b) {
    if (i < counts_[b]) return &blocks_[b][i];
    i -= counts_[b];
  }
  return nullptr;
}

const ParamDef* Table::find(uint16_t id) const {
  for (size_t b = 0; b < nblocks_; ++b) {
    for (size_t i = 0; i < counts_[b]; ++i) {
      if (blocks_[b][i].id == id) return &blocks_[b][i];
    }
  }
  return nullptr;
}

Store::Override* Store::slot(uint16_t id) {
  for (size_t i = 0; i < noverrides_; ++i) {
    if (overrides_[i].id == id) return &overrides_[i];
  }
  return nullptr;
}

const Store::Override* Store::slot(uint16_t id) const {
  for (size_t i = 0; i < noverrides_; ++i) {
    if (overrides_[i].id == id) return &overrides_[i];
  }
  return nullptr;
}

const Store::Override* Store::trial_slot(uint16_t id) const {
  for (size_t i = 0; i < ntrial_; ++i) {
    if (trial_[i].id == id) return &trial_[i];
  }
  return nullptr;
}

bool Store::phy_writable() const {
  return phy_trial_enabled_ && persist_ != nullptr && persist_->usable();
}

Store::Override* Store::set_override(uint16_t id, Value v) {
  Override* o = slot(id);
  if (o == nullptr) {
    // Unreachable while the table itself is capped at kMaxTableParams, and a silent wrong
    // answer if it ever stops being true, so the caller reports it.
    if (noverrides_ >= kMaxTableParams) return nullptr;
    o     = &overrides_[noverrides_++];
    o->id = id;
  }
  o->value = v;
  o->set   = true;
  return o;
}

Value Store::effective(uint16_t id) const {
  const ParamDef* d = find(id);
  if (d == nullptr) return 0;
  // spec 12.4 - during a trial the radio runs the trial values, so they are what a
  // readback reports and what the caller retunes from.
  const Override* t = trial_slot(id);
  if (t != nullptr) return t->value;
  const Override* o = slot(id);
  return (o != nullptr && o->set) ? o->value : d->def;
}

bool Store::is_override(uint16_t id) const {
  const Override* o = slot(id);
  return o != nullptr && o->set;
}

schema::ConfigAckEntry Store::apply(const schema::ConfigEntry& in, bool* applied,
                                    bool* persisted) {
  if (applied != nullptr) *applied = false;
  if (persisted != nullptr) *persisted = true;

  schema::ConfigAckEntry out;
  const ParamDef*        d = find(in.param_id);

  // spec 7.4 - an unknown key is rejected on its own with a reason, and the rest of the
  // set still applies. It has no effective value, so the result carries none.
  if (d == nullptr) {
    out.param_id = in.param_id;
    out.status   = ParamStatus::UnknownParam;
    out.ptype    = in.ptype;
    out.len      = 0;
    return out;
  }

  // D51 - a ptype that differs from the one the node holds, or a `len` that is not one
  // unit of it, costs the entry and not the frame. D55 makes `len` a byte count that is a
  // multiple of the width, so anything but exactly one unit is an array where this
  // parameter is a scalar. The result carries the value the node holds.
  const size_t width = ptype_width(d->type);
  if (in.ptype != d->type || in.len != width) {
    schema::entry_pack(&out, in.param_id, ParamStatus::TypeMismatch, d->type,
                       raw_bits(effective(in.param_id), d->type));
    return out;
  }

  // D56 - a row the firmware publishes but cannot yet apply answers READ_ONLY, carrying
  // the value it does hold. A readable value that refuses a write is honest; a write that
  // half-applies is not. A PHY row is one of these until the trial is enabled and the
  // store can hold a committed group (spec 12.4.2 step 2).
  if (d->access == Access::ReadOnly || (d->access == Access::Phy && !phy_writable())) {
    schema::entry_pack(&out, in.param_id, ParamStatus::ReadOnly, d->type,
                       raw_bits(effective(in.param_id), d->type));
    return out;
  }

  const Value requested = schema::entry_signed(in.value, in.len, in.ptype);
  const Value eff       = clamp(requested, *d);

  const ParamStatus status = eff == requested ? ParamStatus::Ok : ParamStatus::Clamped;

  // spec 12.4 - a PHY value goes to the trial copy and nowhere else. The committed group
  // stays in the store as it was, which is step 2's "old settings persisted first" at no
  // cost: nothing in the store moves until commit_phy_trial(). Rows the set does not name
  // keep their committed values (spec 12.4.2 step 1), because effective() falls back to
  // them. `persisted` is false because this value is not.
  if (d->access == Access::Phy) {
    Override* t = nullptr;
    for (size_t i = 0; i < ntrial_; ++i) {
      if (trial_[i].id == in.param_id) t = &trial_[i];
    }
    if (t == nullptr) {
      t     = &trial_[ntrial_++];  // bounded: kPhyGroupSize Phy rows, table.h asserts it
      t->id = in.param_id;
    }
    t->value = eff;
    t->set   = true;
    if (applied != nullptr) *applied = true;
    if (persisted != nullptr) *persisted = false;
    schema::entry_pack(&out, in.param_id, status, d->type, raw_bits(eff, d->type));
    return out;
  }

  if (set_override(in.param_id, eff) == nullptr) {
    schema::entry_pack(&out, in.param_id, ParamStatus::TypeMismatch, d->type,
                       raw_bits(effective(in.param_id), d->type));
    return out;
  }

  if (applied != nullptr) *applied = true;
  const bool saved = (persist_ != nullptr) && persist_->usable() &&
                     persist_->save(in.param_id, eff);
  if (persisted != nullptr) *persisted = saved;

  // spec 7.4 - the ACK carries the effective value, and a clamp is reported rather than
  // applied quietly.
  schema::entry_pack(&out, in.param_id, status, d->type, raw_bits(eff, d->type));
  return out;
}

bool Store::restore(uint16_t id, Value v) {
  const ParamDef* d = find(id);
  if (d == nullptr || d->access == Access::ReadOnly) return false;
  if (d->access == Access::Phy && !phy_writable()) return false;
  return set_override(id, clamp(v, *d)) != nullptr;
}

bool Store::commit_phy_trial() {
  if (ntrial_ == 0) return true;
  if (!phy_writable()) return false;

  // The whole group, not only the rows the trial named: the store then holds one
  // consistent group whichever rows earlier commits happened to touch.
  uint16_t ids[kPhyGroupSize]    = {};
  Value    values[kPhyGroupSize] = {};
  size_t   n                     = 0;
  for (size_t i = 0; i < table_.size() && n < kPhyGroupSize; ++i) {
    const ParamDef* d = table_.at(i);
    if (d == nullptr || d->access != Access::Phy) continue;
    ids[n]    = d->id;
    values[n] = effective(d->id);
    ++n;
  }
  if (!persist_->save_group(ids, values, n)) return false;

  for (size_t i = 0; i < n; ++i) (void)set_override(ids[i], values[i]);
  ntrial_ = 0;
  return true;
}

bool Store::restore_defaults() {
  // The committed PHY group survives, in RAM and in the store (see store.h).
  Override kept[kPhyGroupSize] = {};
  size_t   nkept               = 0;
  for (size_t i = 0; i < noverrides_; ++i) {
    const ParamDef* d = find(overrides_[i].id);
    if (d != nullptr && d->access == Access::Phy && nkept < kPhyGroupSize) {
      kept[nkept++] = overrides_[i];
    }
  }

  noverrides_ = 0;
  for (size_t i = 0; i < kMaxTableParams; ++i) overrides_[i] = Override{};
  for (size_t i = 0; i < nkept; ++i) (void)set_override(kept[i].id, kept[i].value);

  if (persist_ == nullptr || !persist_->usable()) return false;
  if (!persist_->clear_all()) return false;
  if (nkept == 0) return true;
  uint16_t ids[kPhyGroupSize]    = {};
  Value    values[kPhyGroupSize] = {};
  for (size_t i = 0; i < nkept; ++i) {
    ids[i]    = kept[i].id;
    values[i] = kept[i].value;
  }
  return persist_->save_group(ids, values, nkept);
}

PersistStatus Store::read_persist_status() const {
  // D53 - after a read, this reports whether the node's current overrides are persisted,
  // and reads PERSISTED when there are none.
  // A PHY trial is applied and deliberately not persisted until it is confirmed.
  if (ntrial_ > 0) return PersistStatus::AppliedNotPersisted;
  if (noverrides_ == 0) return PersistStatus::Persisted;
  if (persist_ == nullptr || !persist_->usable()) {
    return PersistStatus::AppliedNotPersisted;
  }
  return PersistStatus::Persisted;
}

bool Store::next_readback_message(ReadbackCursor* cursor, ConfigOp op,
                                  schema::NodeConfigAckV1* out) const {
  if (cursor == nullptr || out == nullptr) return false;
  if (cursor->next >= table_.size()) return false;
  if (cursor->messages >= schema::kMaxConfigAckMessages) return false;

  *out                = schema::NodeConfigAckV1{};
  out->op             = op;
  out->persist_status = read_persist_status();

  size_t used = schema::kConfigAckHdrLen;
  while (cursor->next < table_.size()) {
    const ParamDef* d = table_.at(cursor->next);
    if (d == nullptr) break;
    const size_t cost = result_bytes(d->type);
    if (used + cost > kMaxSchemaPayload) break;
    if (out->count >= schema::kMaxConfigAckEntries) break;
    schema::entry_pack(&out->entries[out->count], d->id, ParamStatus::Ok, d->type,
                       raw_bits(effective(d->id), d->type));
    ++out->count;
    used += cost;
    ++cursor->next;
  }
  ++cursor->messages;

  // spec 7.4.1 - mark every message but the last. The bound is the node's own promise:
  // an answer it cannot finish inside kMaxConfigAckMessages ends unmarked rather than
  // marked and abandoned, because a bridge holding a marked last message waits out
  // config_readback_timeout_ms for a message that is never coming.
  const bool more = cursor->next < table_.size() &&
                    cursor->messages < schema::kMaxConfigAckMessages;
  out->more_follows = more;
  return out->count > 0;
}

}  // namespace config
}  // namespace lran
