// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-32, the library half. Every test runs on the host: the store's policy is what a
// firmware gets wrong, and none of it needs a radio or a nonvolatile store.

#include <unity.h>

#include "lran/config/store.h"
#include "lran/config/table.h"

using namespace lran;
using namespace lran::config;

namespace {

// A nonvolatile store that can be made to fail, which is the half of R-5.3d that gets
// skipped: a node with no usable card still applies and still ACKs.
class FakePersist : public Persist {
 public:
  bool usable_    = true;
  bool save_ok_   = true;
  int  saves_     = 0;
  int  clears_    = 0;

  bool usable() const override { return usable_; }
  bool save(uint16_t, Value) override {
    ++saves_;
    return save_ok_;
  }
  bool clear_all() override {
    ++clears_;
    return true;
  }

  bool     group_ok_     = true;
  int      group_saves_  = 0;
  size_t   group_n_      = 0;
  uint16_t group_ids_[8] = {};
  Value    group_vals_[8] = {};
  bool save_group(const uint16_t* ids, const Value* values, size_t n) override {
    ++group_saves_;
    if (!group_ok_) return false;
    group_n_ = n;
    for (size_t i = 0; i < n && i < 8; ++i) {
      group_ids_[i]  = ids[i];
      group_vals_[i] = values[i];
    }
    return true;
  }
  Value group_value(uint16_t id) const {
    for (size_t i = 0; i < group_n_; ++i) {
      if (group_ids_[i] == id) return group_vals_[i];
    }
    return INT32_MIN;
  }
};

Table node_table() {
  Table t;
  t.add_block(kNodeCommonParams, kNodeCommonParamCount);
  return t;
}

schema::ConfigEntry set_entry(uint16_t id, PType t, uint32_t raw) {
  schema::ConfigEntry e;
  schema::entry_pack(&e, id, t, raw);
  return e;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_a_block_is_walked_in_ascending_id_order() {
  Table t = node_table();
  TEST_ASSERT_EQUAL_UINT32(kNodeCommonParamCount, t.size());
  for (size_t i = 1; i < t.size(); ++i) {
    TEST_ASSERT_TRUE(t.at(i - 1)->id < t.at(i)->id);
  }
  TEST_ASSERT_EQUAL_HEX16(0x0100, t.at(0)->id);
  TEST_ASSERT_NOT_NULL(t.find(0x0114));
  TEST_ASSERT_NULL(t.find(0x9999));
}

// Ascending order across blocks is what lets a readback walk produce spec 7.4.1's order
// without sorting, so a block added out of order is refused rather than accepted quietly.
void test_a_block_out_of_order_is_refused() {
  Table t;
  TEST_ASSERT_TRUE(t.add_block(kNodeCommonParams, kNodeCommonParamCount));
  TEST_ASSERT_FALSE(t.add_block(kBridgeParams, kBridgeParamCount));
  TEST_ASSERT_EQUAL_UINT32(kNodeCommonParamCount, t.size());
}

void test_an_unset_parameter_reads_its_compiled_default() {
  Table t = node_table();
  Store s(t, nullptr);
  TEST_ASSERT_EQUAL_INT32(8, s.effective(0x0100));       // dedup_cache_depth
  TEST_ASSERT_EQUAL_INT32(917400000, s.effective(0x0110));  // freq_hz, D1's
  TEST_ASSERT_FALSE(s.is_override(0x0100));
}

void test_a_set_applies_and_the_ack_carries_the_effective_value() {
  Table       t = node_table();
  FakePersist p;
  Store       s(t, &p);

  bool applied = false, persisted = false;
  schema::ConfigAckEntry r =
      s.apply(set_entry(0x0100, PType::U8, 16), &applied, &persisted);

  TEST_ASSERT_EQUAL(ParamStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_UINT32(16, schema::entry_raw(r.value, r.len));
  TEST_ASSERT_TRUE(applied);
  TEST_ASSERT_TRUE(persisted);
  TEST_ASSERT_EQUAL_INT32(16, s.effective(0x0100));
  TEST_ASSERT_TRUE(s.is_override(0x0100));
  TEST_ASSERT_EQUAL_INT32(1, p.saves_);
}

// spec 7.4 - out of range is clamped to the documented range and the clamp is REPORTED,
// not applied quietly. dedup_cache_depth's max is 32.
void test_an_out_of_range_value_is_clamped_and_the_clamp_is_reported() {
  Table t = node_table();
  Store s(t, nullptr);

  schema::ConfigAckEntry r = s.apply(set_entry(0x0100, PType::U8, 200), nullptr, nullptr);
  TEST_ASSERT_EQUAL(ParamStatus::Clamped, r.status);
  TEST_ASSERT_EQUAL_UINT32(32, schema::entry_raw(r.value, r.len));
  TEST_ASSERT_EQUAL_INT32(32, s.effective(0x0100));
}

// spec 7.4 - an unknown key is rejected on its own with a reason, never silently ignored.
// It has no effective value, so the result carries none.
void test_an_unknown_param_is_rejected_alone_and_carries_no_value() {
  Table t = node_table();
  Store s(t, nullptr);

  bool applied = true;
  schema::ConfigAckEntry r = s.apply(set_entry(0x7777, PType::U16, 5), &applied, nullptr);
  TEST_ASSERT_EQUAL(ParamStatus::UnknownParam, r.status);
  TEST_ASSERT_EQUAL_UINT8(0, r.len);
  TEST_ASSERT_FALSE(applied);
}

// D51 - a ptype that differs from the one the node holds costs the entry, and the result
// carries the value the node holds.
void test_a_ptype_mismatch_costs_the_entry_and_reports_the_held_value() {
  Table t = node_table();
  Store s(t, nullptr);

  schema::ConfigAckEntry r = s.apply(set_entry(0x0100, PType::U16, 12), nullptr, nullptr);
  TEST_ASSERT_EQUAL(ParamStatus::TypeMismatch, r.status);
  TEST_ASSERT_EQUAL(PType::U8, r.ptype);
  TEST_ASSERT_EQUAL_UINT32(8, schema::entry_raw(r.value, r.len));  // still the default
  TEST_ASSERT_FALSE(s.is_override(0x0100));
}

// D55 - `len` is a byte count and a multiple of the width, so two units where the
// parameter is a scalar is an array, and an array where a scalar is declared is rejected
// alone.
void test_an_array_where_a_scalar_is_declared_is_rejected_alone() {
  Table t = node_table();
  Store s(t, nullptr);

  schema::ConfigEntry e = set_entry(0x0100, PType::U8, 4);
  e.len                 = 2;  // two u8 units
  schema::ConfigAckEntry r = s.apply(e, nullptr, nullptr);
  TEST_ASSERT_EQUAL(ParamStatus::TypeMismatch, r.status);
  TEST_ASSERT_FALSE(s.is_override(0x0100));
}

// D56 - the PHY rows are declared so HA can read the working point, and answer READ_ONLY
// in a Store whose owner has not enabled spec 12.4's trial.
void test_a_phy_row_answers_read_only_and_still_reports_its_value() {
  Table t = node_table();
  Store s(t, nullptr);

  schema::ConfigAckEntry r =
      s.apply(set_entry(0x0111, PType::U8, 10), nullptr, nullptr);  // spreading_factor
  TEST_ASSERT_EQUAL(ParamStatus::ReadOnly, r.status);
  TEST_ASSERT_EQUAL_UINT32(9, schema::entry_raw(r.value, r.len));  // D1's SF9
  TEST_ASSERT_EQUAL_INT32(9, s.effective(0x0111));
}

// ---------------------------------------------------------------------------
// spec 12.4, D59 - the PHY group's trial copy (BF-33)
// ---------------------------------------------------------------------------

constexpr uint16_t kSf  = 0x0111;
constexpr uint16_t kTxp = 0x0114;

// D59 - the bridge carries the group under the node rows' names, and table.h's
// phy_rows_agree() holds the ranges equal. This is the runtime face of that assert.
void test_the_bridge_holds_the_phy_group_with_the_node_ranges() {
  Table t;
  TEST_ASSERT_TRUE(t.add_block(kBridgeParams, kBridgeParamCount));
  size_t phy = 0;
  for (size_t i = 0; i < t.size(); ++i) {
    if (t.at(i)->access == Access::Phy) {
      TEST_ASSERT_EQUAL(Owner::BridgeGlobal, t.at(i)->owner);
      ++phy;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(kPhyGroupSize, phy);
  TEST_ASSERT_EQUAL_INT32(-4, t.find(0x0014)->max);  // tx_power_dbm, D33's ceiling
}

// spec 12.4.2 step 2 - enabling the trial is not enough without a store to commit into.
void test_a_phy_row_stays_read_only_without_a_usable_store() {
  Table t = node_table();
  Store none(t, nullptr);
  none.enable_phy_trial();
  TEST_ASSERT_EQUAL(ParamStatus::ReadOnly,
                    none.apply(set_entry(kSf, PType::U8, 10), nullptr, nullptr).status);

  FakePersist p;
  p.usable_ = false;
  Store dead(t, &p);
  dead.enable_phy_trial();
  TEST_ASSERT_EQUAL(ParamStatus::ReadOnly,
                    dead.apply(set_entry(kSf, PType::U8, 10), nullptr, nullptr).status);
  TEST_ASSERT_FALSE(dead.phy_trial_pending());
}

// spec 12.4 steps 2 and 3 - the value takes effect and nothing is written: the store
// still holds the last known-good group, which is what a reboot comes back on.
void test_a_phy_set_goes_to_the_trial_copy_and_writes_nothing() {
  Table       t = node_table();
  FakePersist p;
  Store       s(t, &p);
  s.enable_phy_trial();

  bool applied = false, persisted = true;
  schema::ConfigAckEntry r = s.apply(set_entry(kSf, PType::U8, 10), &applied, &persisted);
  TEST_ASSERT_EQUAL(ParamStatus::Ok, r.status);
  TEST_ASSERT_TRUE(applied);
  TEST_ASSERT_FALSE(persisted);
  TEST_ASSERT_TRUE(s.phy_trial_pending());
  TEST_ASSERT_EQUAL_INT32(10, s.effective(kSf));
  TEST_ASSERT_EQUAL_INT32(0, p.saves_);
  TEST_ASSERT_EQUAL_INT32(0, p.group_saves_);
  TEST_ASSERT_EQUAL(PersistStatus::AppliedNotPersisted, s.read_persist_status());
}

// spec 12.4 - TX power is clamped by the table, not by whoever types into HA.
void test_tx_power_above_d33_is_clamped_in_the_trial() {
  Table       t = node_table();
  FakePersist p;
  Store       s(t, &p);
  s.enable_phy_trial();

  schema::ConfigAckEntry r =
      s.apply(set_entry(kTxp, PType::I16, static_cast<uint16_t>(-2)), nullptr, nullptr);
  TEST_ASSERT_EQUAL(ParamStatus::Clamped, r.status);
  TEST_ASSERT_EQUAL_INT32(-4, s.effective(kTxp));
}

// spec 12.4.2 step 6 - a revert restores the committed group and writes nothing.
void test_a_revert_drops_the_trial() {
  Table       t = node_table();
  FakePersist p;
  Store       s(t, &p);
  s.enable_phy_trial();
  (void)s.apply(set_entry(kSf, PType::U8, 10), nullptr, nullptr);

  s.revert_phy_trial();
  TEST_ASSERT_FALSE(s.phy_trial_pending());
  TEST_ASSERT_EQUAL_INT32(9, s.effective(kSf));
  TEST_ASSERT_FALSE(s.is_override(kSf));
  TEST_ASSERT_EQUAL_INT32(0, p.group_saves_);
}

// spec 12.4.2 step 5 - a commit writes the whole group in one call, rows the set did not
// name included, so the store never holds a mixed group.
void test_a_commit_writes_the_whole_group_at_once() {
  Table       t = node_table();
  FakePersist p;
  Store       s(t, &p);
  s.enable_phy_trial();
  (void)s.apply(set_entry(kSf, PType::U8, 10), nullptr, nullptr);

  TEST_ASSERT_TRUE(s.commit_phy_trial());
  TEST_ASSERT_EQUAL_INT32(1, p.group_saves_);
  TEST_ASSERT_EQUAL_UINT32(kPhyGroupSize, p.group_n_);
  TEST_ASSERT_EQUAL_INT32(10, p.group_value(kSf));
  TEST_ASSERT_EQUAL_INT32(917400000, p.group_value(0x0110));
  TEST_ASSERT_EQUAL_INT32(0, p.saves_);
  TEST_ASSERT_FALSE(s.phy_trial_pending());
  TEST_ASSERT_EQUAL_INT32(10, s.effective(kSf));
  TEST_ASSERT_TRUE(s.is_override(kSf));

  // The committed value is now what a later revert returns to.
  (void)s.apply(set_entry(kSf, PType::U8, 11), nullptr, nullptr);
  s.revert_phy_trial();
  TEST_ASSERT_EQUAL_INT32(10, s.effective(kSf));
}

// A commit the store refused leaves the trial in place and nothing committed, so the
// caller's window reverts to settings that are really in the store.
void test_a_failed_commit_keeps_the_trial_and_commits_nothing() {
  Table       t = node_table();
  FakePersist p;
  p.group_ok_ = false;
  Store s(t, &p);
  s.enable_phy_trial();
  (void)s.apply(set_entry(kSf, PType::U8, 10), nullptr, nullptr);

  TEST_ASSERT_FALSE(s.commit_phy_trial());
  TEST_ASSERT_TRUE(s.phy_trial_pending());
  TEST_ASSERT_FALSE(s.is_override(kSf));
  s.revert_phy_trial();
  TEST_ASSERT_EQUAL_INT32(9, s.effective(kSf));
}

// A value put back from the store at boot is committed, so it opens no trial, and it is
// refused where a write would be.
void test_restore_puts_back_a_committed_phy_value_without_a_trial() {
  Table       t = node_table();
  FakePersist p;
  Store       closed(t, &p);
  TEST_ASSERT_FALSE(closed.restore(kSf, 10));
  TEST_ASSERT_EQUAL_INT32(9, closed.effective(kSf));

  Store s(t, &p);
  s.enable_phy_trial();
  TEST_ASSERT_TRUE(s.restore(kSf, 10));
  TEST_ASSERT_TRUE(s.restore(kTxp, 5));  // clamped on the way in
  TEST_ASSERT_FALSE(s.phy_trial_pending());
  TEST_ASSERT_EQUAL_INT32(10, s.effective(kSf));
  TEST_ASSERT_EQUAL_INT32(-4, s.effective(kTxp));
  TEST_ASSERT_EQUAL_INT32(0, p.saves_);
}

// RESTORE_DEFAULTS is a node-level operation, and spec 12.4 lets none move the PHY. The
// committed group survives in RAM and is written back after the store is cleared.
void test_restore_defaults_keeps_the_committed_phy_group() {
  Table       t = node_table();
  FakePersist p;
  Store       s(t, &p);
  s.enable_phy_trial();
  (void)s.apply(set_entry(kSf, PType::U8, 10), nullptr, nullptr);
  TEST_ASSERT_TRUE(s.commit_phy_trial());
  (void)s.apply(set_entry(0x0100, PType::U8, 16), nullptr, nullptr);

  TEST_ASSERT_TRUE(s.restore_defaults());
  TEST_ASSERT_EQUAL_INT32(8, s.effective(0x0100));
  TEST_ASSERT_EQUAL_INT32(10, s.effective(kSf));
  TEST_ASSERT_EQUAL_INT32(1, p.clears_);
  TEST_ASSERT_EQUAL_INT32(2, p.group_saves_);
  TEST_ASSERT_EQUAL_INT32(10, p.group_value(kSf));
}

// spec 7.4, R-5.3d - with no usable store the change is still applied and still ACKed,
// and persist_status says so. HA must never be told a value was saved when it was not.
void test_an_unusable_store_still_applies_and_says_so() {
  Table       t = node_table();
  FakePersist p;
  p.usable_ = false;
  Store s(t, &p);

  bool applied = false, persisted = true;
  schema::ConfigAckEntry r =
      s.apply(set_entry(0x0101, PType::U16, 9000), &applied, &persisted);

  TEST_ASSERT_EQUAL(ParamStatus::Ok, r.status);
  TEST_ASSERT_TRUE(applied);
  TEST_ASSERT_FALSE(persisted);
  TEST_ASSERT_EQUAL_INT32(9000, s.effective(0x0101));
  TEST_ASSERT_EQUAL(PersistStatus::AppliedNotPersisted, s.read_persist_status());
  TEST_ASSERT_EQUAL_INT32(0, p.saves_);
}

// D53 - after a read with no overrides held, persist_status reads PERSISTED.
void test_a_read_with_no_overrides_reports_persisted() {
  Table t = node_table();
  Store s(t, nullptr);
  TEST_ASSERT_EQUAL(PersistStatus::Persisted, s.read_persist_status());
}

// D52 - RESTORE_DEFAULTS clears every override.
void test_restore_defaults_clears_every_override() {
  Table       t = node_table();
  FakePersist p;
  Store       s(t, &p);

  s.apply(set_entry(0x0100, PType::U8, 20), nullptr, nullptr);
  TEST_ASSERT_TRUE(s.is_override(0x0100));

  TEST_ASSERT_TRUE(s.restore_defaults());
  TEST_ASSERT_FALSE(s.is_override(0x0100));
  TEST_ASSERT_EQUAL_INT32(8, s.effective(0x0100));
  TEST_ASSERT_EQUAL_INT32(1, p.clears_);
  TEST_ASSERT_EQUAL(PersistStatus::Persisted, s.read_persist_status());
}

// spec 7.4.1 - a node whose answer fits one frame leaves MORE_FOLLOWS clear. Every vector
// committed before D57 is this case.
void test_a_readback_that_fits_one_frame_is_not_marked() {
  Table          t = node_table();
  Store          s(t, nullptr);
  ReadbackCursor c;
  schema::NodeConfigAckV1 msg;

  TEST_ASSERT_TRUE(s.next_readback_message(&c, ConfigOp::GetAll, &msg));
  TEST_ASSERT_FALSE(msg.more_follows);
  TEST_ASSERT_EQUAL_UINT8(kNodeCommonParamCount, msg.count);
  TEST_ASSERT_EQUAL(ConfigOp::GetAll, msg.op);
  TEST_ASSERT_FALSE(s.next_readback_message(&c, ConfigOp::GetAll, &msg));
}

// spec 7.4.1, D57 - the answer a table too large for one frame produces: every message
// but the last marked, ascending param_id across the whole answer, every row once.
void test_a_readback_larger_than_one_frame_splits_and_marks_every_message_but_the_last() {
  // 40 u32 rows: a result costs 9 bytes, so 21 fit a message and the answer needs two.
  static ParamDef rows[40];
  for (size_t i = 0; i < 40; ++i) {
    rows[i] = ParamDef{static_cast<uint16_t>(0x1000 + i),
                       "synthetic",
                       Owner::Node,
                       Access::ReadWrite,
                       PType::U32,
                       0,
                       1000000,
                       static_cast<Value>(100 + i),
                       nullptr,
                       "test row"};
  }
  Table t;
  TEST_ASSERT_TRUE(t.add_block(rows, 40));
  Store s(t, nullptr);

  ReadbackCursor          c;
  schema::NodeConfigAckV1 msg;
  uint16_t                seen[40] = {};
  size_t                  nseen    = 0;
  int                     messages = 0;
  bool                    marks[4] = {false, false, false, false};

  while (s.next_readback_message(&c, ConfigOp::GetAll, &msg)) {
    marks[messages] = msg.more_follows;
    ++messages;
    uint8_t buf[kMaxSchemaPayload] = {};
    size_t  n                      = 0;
    // Every message must be a legal frame payload, which is the whole point of the split.
    TEST_ASSERT_EQUAL(Status::Ok, schema::serialize(msg, buf, sizeof(buf), &n));
    TEST_ASSERT_TRUE(n <= kMaxSchemaPayload);
    for (uint8_t i = 0; i < msg.count; ++i) seen[nseen++] = msg.entries[i].param_id;
  }

  TEST_ASSERT_EQUAL_INT(2, messages);
  TEST_ASSERT_TRUE(marks[0]);
  TEST_ASSERT_FALSE(marks[1]);
  TEST_ASSERT_EQUAL_UINT32(40, nseen);
  for (size_t i = 0; i < nseen; ++i) {
    TEST_ASSERT_EQUAL_HEX16(0x1000 + i, seen[i]);
  }
}

// An override set before the readback is what the readback reports: the answer carries
// effective values, not defaults.
void test_a_readback_carries_effective_values() {
  Table t = node_table();
  Store s(t, nullptr);
  s.apply(set_entry(0x0102, PType::U8, 2), nullptr, nullptr);  // cad_retries

  ReadbackCursor          c;
  schema::NodeConfigAckV1 msg;
  TEST_ASSERT_TRUE(s.next_readback_message(&c, ConfigOp::GetAll, &msg));

  bool found = false;
  for (uint8_t i = 0; i < msg.count; ++i) {
    if (msg.entries[i].param_id == 0x0102) {
      TEST_ASSERT_EQUAL_UINT32(2, schema::entry_raw(msg.entries[i].value,
                                                    msg.entries[i].len));
      found = true;
    }
  }
  TEST_ASSERT_TRUE(found);
}

// tx_power_dbm is the one signed row, and D33's ceiling is its maximum. A readback that
// lost the sign would report a transmitter 4 dB the wrong side of a legal limit.
void test_the_signed_phy_row_survives_a_readback() {
  Table t = node_table();
  Store s(t, nullptr);
  ReadbackCursor          c;
  schema::NodeConfigAckV1 msg;
  TEST_ASSERT_TRUE(s.next_readback_message(&c, ConfigOp::GetAll, &msg));

  for (uint8_t i = 0; i < msg.count; ++i) {
    if (msg.entries[i].param_id == 0x0114) {
      TEST_ASSERT_EQUAL_INT32(-4, schema::entry_signed(msg.entries[i].value,
                                                       msg.entries[i].len, PType::I16));
      return;
    }
  }
  TEST_FAIL_MESSAGE("tx_power_dbm was not in the readback");
}

// spec 8.12, 12.4, D64 - bandwidth takes 125, 250 or 500. 300 is inside the range and is
// no SX1262 bandwidth, so it answers INVALID_VALUE, applies nothing, and carries the held
// value. Above the range still clamps, to 500, which is on the list.
void test_a_bandwidth_off_the_list_is_invalid_and_applies_nothing() {
  const uint16_t kBw = 0x0112;
  Table          t   = node_table();
  FakePersist    p;
  Store          s(t, &p);
  s.enable_phy_trial();

  bool applied = true;
  schema::ConfigAckEntry r = s.apply(set_entry(kBw, PType::U16, 300), &applied, nullptr);
  TEST_ASSERT_EQUAL(ParamStatus::InvalidValue, r.status);
  TEST_ASSERT_EQUAL_UINT32(125, schema::entry_raw(r.value, r.len));
  TEST_ASSERT_FALSE(r.is_override);
  TEST_ASSERT_FALSE(applied);
  TEST_ASSERT_FALSE(s.phy_trial_pending());

  r = s.apply(set_entry(kBw, PType::U16, 250), &applied, nullptr);
  TEST_ASSERT_EQUAL(ParamStatus::Ok, r.status);
  TEST_ASSERT_TRUE(applied);
  s.revert_phy_trial();

  r = s.apply(set_entry(kBw, PType::U16, 600), &applied, nullptr);
  TEST_ASSERT_EQUAL(ParamStatus::Clamped, r.status);
  TEST_ASSERT_EQUAL_UINT32(500, schema::entry_raw(r.value, r.len));
}

// A value stored before D64 comes back checked, as apply() would check it.
void test_restore_refuses_a_bandwidth_off_the_list() {
  Table       t = node_table();
  FakePersist p;
  Store       s(t, &p);
  s.enable_phy_trial();
  TEST_ASSERT_FALSE(s.restore(0x0112, 300));
  TEST_ASSERT_EQUAL_INT32(125, s.effective(0x0112));
  TEST_ASSERT_TRUE(s.restore(0x0112, 250));
}

// spec 7.4, D68 - OVERRIDE marks a value the node holds as an override, even one equal to
// its default, and a readback carries it per result.
void test_override_is_marked_on_the_set_and_on_the_readback() {
  Table       t = node_table();
  FakePersist p;
  Store       s(t, &p);

  schema::ConfigAckEntry r = s.apply(set_entry(0x0100, PType::U8, 8), nullptr, nullptr);
  TEST_ASSERT_EQUAL(ParamStatus::Ok, r.status);
  TEST_ASSERT_TRUE(r.is_override);  // 8 is dedup_cache_depth's default

  ReadbackCursor          c;
  schema::NodeConfigAckV1 ack;
  TEST_ASSERT_TRUE(s.next_readback_message(&c, ConfigOp::GetAll, &ack));
  TEST_ASSERT_EQUAL_HEX16(0x0100, ack.entries[0].param_id);
  TEST_ASSERT_TRUE(ack.entries[0].is_override);
  TEST_ASSERT_EQUAL_HEX16(0x0101, ack.entries[1].param_id);
  TEST_ASSERT_FALSE(ack.entries[1].is_override);

  // A rejected entry reports the marking of the value it carries.
  r = s.apply(set_entry(0x0100, PType::U16, 8), nullptr, nullptr);
  TEST_ASSERT_EQUAL(ParamStatus::TypeMismatch, r.status);
  TEST_ASSERT_TRUE(r.is_override);
}

// A PHY trial value is what the radio runs and what the result carries, so the bit marks
// it, though is_override() does not count it until the commit.
void test_a_trial_value_is_marked_override() {
  Table       t = node_table();
  FakePersist p;
  Store       s(t, &p);
  s.enable_phy_trial();
  schema::ConfigAckEntry r = s.apply(set_entry(0x0111, PType::U8, 10), nullptr, nullptr);
  TEST_ASSERT_TRUE(r.is_override);
  TEST_ASSERT_TRUE(s.marked_override(0x0111));
  TEST_ASSERT_FALSE(s.is_override(0x0111));
  s.revert_phy_trial();
  TEST_ASSERT_FALSE(s.marked_override(0x0111));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_block_is_walked_in_ascending_id_order);
  RUN_TEST(test_a_block_out_of_order_is_refused);
  RUN_TEST(test_an_unset_parameter_reads_its_compiled_default);
  RUN_TEST(test_a_set_applies_and_the_ack_carries_the_effective_value);
  RUN_TEST(test_an_out_of_range_value_is_clamped_and_the_clamp_is_reported);
  RUN_TEST(test_an_unknown_param_is_rejected_alone_and_carries_no_value);
  RUN_TEST(test_a_ptype_mismatch_costs_the_entry_and_reports_the_held_value);
  RUN_TEST(test_an_array_where_a_scalar_is_declared_is_rejected_alone);
  RUN_TEST(test_a_phy_row_answers_read_only_and_still_reports_its_value);
  RUN_TEST(test_the_bridge_holds_the_phy_group_with_the_node_ranges);
  RUN_TEST(test_a_phy_row_stays_read_only_without_a_usable_store);
  RUN_TEST(test_a_phy_set_goes_to_the_trial_copy_and_writes_nothing);
  RUN_TEST(test_tx_power_above_d33_is_clamped_in_the_trial);
  RUN_TEST(test_a_revert_drops_the_trial);
  RUN_TEST(test_a_commit_writes_the_whole_group_at_once);
  RUN_TEST(test_a_failed_commit_keeps_the_trial_and_commits_nothing);
  RUN_TEST(test_restore_puts_back_a_committed_phy_value_without_a_trial);
  RUN_TEST(test_restore_defaults_keeps_the_committed_phy_group);
  RUN_TEST(test_an_unusable_store_still_applies_and_says_so);
  RUN_TEST(test_a_read_with_no_overrides_reports_persisted);
  RUN_TEST(test_restore_defaults_clears_every_override);
  RUN_TEST(test_a_readback_that_fits_one_frame_is_not_marked);
  RUN_TEST(test_a_readback_larger_than_one_frame_splits_and_marks_every_message_but_the_last);
  RUN_TEST(test_a_readback_carries_effective_values);
  RUN_TEST(test_the_signed_phy_row_survives_a_readback);
  RUN_TEST(test_a_bandwidth_off_the_list_is_invalid_and_applies_nothing);
  RUN_TEST(test_restore_refuses_a_bandwidth_off_the_list);
  RUN_TEST(test_override_is_marked_on_the_set_and_on_the_readback);
  RUN_TEST(test_a_trial_value_is_marked_override);
  return UNITY_END();
}
