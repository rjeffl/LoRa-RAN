// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-33, slice 2 - the bridge's fleet PHY change. Spec 12.4.1, 12.4.3, 16.7.5; D59.
//
// WHAT THIS COVERS. The order spec 12.4.1 makes load-bearing: every node accepts before
// the bridge retunes, every node is heard before anything commits, and a change that
// fails anywhere ends with the bridge back on its old settings. Also the step 8 deadline,
// the GET that may be sent again, and the NVS blob's layout.
//
// WHAT IT CANNOT. That lora_task retunes the radio, and that a node reverts on silence.
// Both are bench work, B4b's row in Impl Plan 8.

#include <unity.h>

#include "lran/config/table.h"
#include "phy_change.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

constexpr NodeId kA = 0xF1;
constexpr NodeId kB = 0xF2;
constexpr uint32_t kAckMs = 8000;

PhyGroup defaults() {
  PhyGroup g;
  for (size_t i = 0; i < kPhyGroupSize; ++i) g.v[i] = bridge_phy_row(i)->def;
  return g;
}

PhyGroup moved() {
  PhyGroup g    = defaults();
  g.v[kPhySf]   = 10;
  return g;
}

// A node's answer to step 3: every entry of `g` under the node's ids, Ok unless `bad`
// names an index to refuse.
schema::NodeConfigAckV1 set_ack(const PhyGroup& g, size_t bad = kPhyGroupSize) {
  schema::NodeConfigAckV1 a;
  a.op             = ConfigOp::Set;
  a.persist_status = PersistStatus::AppliedNotPersisted;  // D60 - a trial reads this
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    const config::ParamDef* d = bridge_phy_row(i);
    schema::entry_pack(&a.entries[a.count++], kNodePhyIds[i],
                       i == bad ? ParamStatus::ReadOnly : ParamStatus::Ok, d->type,
                       static_cast<uint32_t>(g.v[i]) & (config::ptype_width(d->type) == 1
                                                            ? 0xFFu
                                                            : config::ptype_width(d->type) == 2
                                                                  ? 0xFFFFu
                                                                  : 0xFFFFFFFFu));
  }
  return a;
}

struct Rig {
  PhyChange m;
  NodeId    fleet[2] = {kA, kB};
  size_t    n        = 2;
  Seq       seq      = 100;

  explicit Rig(size_t count = 2) : n(count) {
    m.set_ack_timeout_ms(kAckMs);
    TEST_ASSERT_TRUE(m.start(defaults(), moved(), fleet, n, 0));
  }

  // Runs next() and, for a send, reports it sent. Returns the step.
  PhyStep step(uint32_t now) {
    PhyStep s = m.next(now);
    if (s.action == PhyAction::SendSet || s.action == PhyAction::SendGet ||
        s.action == PhyAction::SendPoll) {
      m.on_sent(++seq, now);
    }
    return s;
  }

  // Steps 3 and 4 for every node, answered at `now`.
  void fan_out(uint32_t now) {
    for (size_t i = 0; i < n; ++i) {
      TEST_ASSERT_TRUE(step(now).action == PhyAction::SendSet);
      TEST_ASSERT_TRUE(m.on_config_ack(fleet[i], set_ack(moved()), seq, now));
    }
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// The group and its conversions
// ---------------------------------------------------------------------------

void test_the_id_lists_name_the_tables_rows() {
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    const config::ParamDef* b = bridge_phy_row(i);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(b->access == config::Access::Phy);
    bool found = false;
    for (size_t k = 0; k < config::kNodeCommonParamCount; ++k) {
      const config::ParamDef& n = config::kNodeCommonParams[k];
      if (n.id == kNodePhyIds[i]) {
        found = true;
        TEST_ASSERT_EQUAL_STRING(b->name, n.name);
      }
    }
    TEST_ASSERT_TRUE(found);
  }
}

// The defaults describe the radio the bridge boots on today, kPhy.
void test_the_default_group_is_d1s_envelope() {
  link::PhyConfig p{};
  TEST_ASSERT_TRUE(phy_config_from(defaults(), link::kPhy, &p));
  TEST_ASSERT_EQUAL_UINT32(link::kPhy.freq_hz, p.freq_hz);
  TEST_ASSERT_EQUAL_UINT16(link::kPhy.bw_khz10, p.bw_khz10);
  TEST_ASSERT_EQUAL_UINT8(link::kPhy.sf, p.sf);
  TEST_ASSERT_EQUAL_UINT8(link::kPhy.cr_denom, p.cr_denom);
  TEST_ASSERT_EQUAL_INT8(link::kPhy.conducted_dbm, p.conducted_dbm);
}

// D33 - a power the ceiling forbids is refused, and the radio keeps what it has.
void test_a_group_above_the_eirp_ceiling_is_refused() {
  PhyGroup g              = defaults();
  g.v[kPhyTxPower]        = 0;
  link::PhyConfig p       = link::kPhy;
  p.freq_hz               = 1;
  TEST_ASSERT_FALSE(phy_config_from(g, link::kPhy, &p));
  TEST_ASSERT_EQUAL_UINT32(1, p.freq_hz);
}

void test_the_set_carries_the_whole_group_under_the_nodes_ids() {
  schema::NodeConfigV1 c;
  build_phy_set(moved(), &c);
  TEST_ASSERT_TRUE(c.op == ConfigOp::Set);
  TEST_ASSERT_EQUAL_UINT8(kPhyGroupSize, c.count);
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    TEST_ASSERT_EQUAL_UINT16(kNodePhyIds[i], c.entries[i].param_id);
  }
  TEST_ASSERT_EQUAL_INT32(10, schema::entry_signed(c.entries[kPhySf].value,
                                                   c.entries[kPhySf].len,
                                                   c.entries[kPhySf].ptype));
  // tx_power_dbm is signed, and -4 must survive the trip.
  TEST_ASSERT_EQUAL_INT32(-4, schema::entry_signed(c.entries[kPhyTxPower].value,
                                                   c.entries[kPhyTxPower].len,
                                                   c.entries[kPhyTxPower].ptype));

  build_phy_get(&c);
  TEST_ASSERT_TRUE(c.op == ConfigOp::Get);
  TEST_ASSERT_EQUAL_UINT8(kPhyGroupSize, c.count);
  TEST_ASSERT_EQUAL_UINT8(0, c.entries[0].len);  // spec 8.10
}

void test_the_blob_round_trips_and_refuses_another_version() {
  PhyBlob b;
  b.trial_open = true;
  b.n          = 2;
  b.ids[0]     = 0x0010;
  b.values[0]  = 917400000;
  b.ids[1]     = 0x0014;
  b.values[1]  = -4;
  uint8_t buf[kPhyBlobMax];
  const size_t n = phy_blob_encode(b, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT32(15, n);
  TEST_ASSERT_EQUAL_HEX8(0x10, buf[3]);  // little-endian, field by field (root rule 1)

  PhyBlob out;
  TEST_ASSERT_TRUE(phy_blob_decode(buf, n, &out));
  TEST_ASSERT_TRUE(out.trial_open);
  TEST_ASSERT_EQUAL_INT32(917400000, out.values[0]);
  TEST_ASSERT_EQUAL_INT32(-4, out.values[1]);

  buf[0] = 2;
  TEST_ASSERT_FALSE(phy_blob_decode(buf, n, &out));
  buf[0] = kPhyBlobVersion;
  TEST_ASSERT_FALSE(phy_blob_decode(buf, n - 1, &out));
}

// ---------------------------------------------------------------------------
// The order - spec 12.4.1
// ---------------------------------------------------------------------------

void test_an_equal_group_starts_nothing() {
  PhyChange m;
  NodeId    fleet[1] = {kA};
  TEST_ASSERT_FALSE(m.start(defaults(), defaults(), fleet, 1, 0));
  TEST_ASSERT_FALSE(m.busy());
}

void test_a_change_that_works_commits_once_every_node_is_heard() {
  Rig r;
  r.fan_out(1000);

  // Step 5 - only after the last node has accepted.
  PhyStep s = r.step(1000);
  TEST_ASSERT_TRUE(s.action == PhyAction::Retune);
  TEST_ASSERT_EQUAL_INT32(10, s.group.v[kPhySf]);
  TEST_ASSERT_TRUE(r.m.blocks_traffic());
  r.m.on_retuned(1100);

  // Step 6 - a frame heard before the retune does not count.
  r.m.on_heard(kA, 1050);
  s = r.step(1200);
  TEST_ASSERT_TRUE(s.action == PhyAction::SendPoll);
  TEST_ASSERT_EQUAL_HEX8(kA, s.dst);
  s = r.step(1200);
  TEST_ASSERT_TRUE(s.action == PhyAction::SendPoll);
  TEST_ASSERT_EQUAL_HEX8(kB, s.dst);
  TEST_ASSERT_TRUE(r.step(1300).action == PhyAction::None);

  r.m.on_heard(kA, 1500);
  TEST_ASSERT_TRUE(r.step(1600).action == PhyAction::None);  // kB not heard yet
  r.m.on_heard(kB, 1700);

  // Step 7 - commit, then one GET per node in the order of step 3.
  s = r.step(1800);
  TEST_ASSERT_TRUE(s.action == PhyAction::Commit);
  TEST_ASSERT_EQUAL_INT32(10, s.group.v[kPhySf]);
  s = r.step(1800);
  TEST_ASSERT_TRUE(s.action == PhyAction::SendGet);
  TEST_ASSERT_EQUAL_HEX8(kA, s.dst);
  TEST_ASSERT_TRUE(r.m.on_config_ack(kA, set_ack(moved()), r.seq, 1900));
  s = r.step(1900);
  TEST_ASSERT_TRUE(s.action == PhyAction::NodeConfirmed);
  TEST_ASSERT_EQUAL_UINT32(kPhyGroupSize, s.result_count);
  TEST_ASSERT_EQUAL_HEX8(kB, r.step(1900).dst);
  TEST_ASSERT_TRUE(r.m.on_config_ack(kB, set_ack(moved()), r.seq, 2000));
  TEST_ASSERT_TRUE(r.step(2000).action == PhyAction::NodeConfirmed);

  TEST_ASSERT_FALSE(r.m.busy());
  TEST_ASSERT_EQUAL_UINT32(1, r.m.stats().committed);
}

// Step 4 - a refusal abandons the change before the bridge moves, and the node after it
// is never sent anything.
void test_a_refusal_abandons_before_the_bridge_moves() {
  Rig r;
  TEST_ASSERT_TRUE(r.step(0).action == PhyAction::SendSet);
  TEST_ASSERT_TRUE(r.m.on_config_ack(kA, set_ack(moved(), kPhySf), r.seq, 500));

  const PhyStep s = r.step(500);
  TEST_ASSERT_TRUE(s.action == PhyAction::Abandon);
  TEST_ASSERT_TRUE(s.reason == PhyReason::NotAccepted);
  TEST_ASSERT_EQUAL_HEX8(kA, s.culprit);
  TEST_ASSERT_FALSE(s.retuned);
  TEST_ASSERT_EQUAL_INT32(9, s.group.v[kPhySf]);  // the settings it returns to

  // Busy until kA's window has closed, but no longer blocking other traffic.
  TEST_ASSERT_TRUE(r.m.busy());
  TEST_ASSERT_FALSE(r.m.blocks_traffic());
  TEST_ASSERT_TRUE(r.step(500 + 119999).action == PhyAction::None);
  TEST_ASSERT_TRUE(r.step(500 + 120000).action == PhyAction::None);
  TEST_ASSERT_FALSE(r.m.busy());
}

// A value the node changed is a clamp, and step 4 treats it as a refusal.
void test_a_clamped_value_abandons() {
  Rig r;
  (void)r.step(0);
  PhyGroup other = moved();
  other.v[kPhySf] = 11;
  TEST_ASSERT_TRUE(r.m.on_config_ack(kA, set_ack(other), r.seq, 500));
  TEST_ASSERT_TRUE(r.step(500).reason == PhyReason::NotAccepted);
}

// Step 4's `unknown` - no answer. The readback waits for the node's window (spec 12.4.1
// step 4), because a node that did retune cannot hear it sooner.
void test_a_missing_ack_abandons_and_defers_the_readback() {
  Rig r;
  (void)r.step(0);
  TEST_ASSERT_TRUE(r.step(kAckMs - 1).action == PhyAction::None);
  const PhyStep s = r.step(kAckMs);
  TEST_ASSERT_TRUE(s.action == PhyAction::Abandon);
  TEST_ASSERT_EQUAL_HEX8(kA, s.culprit);

  TEST_ASSERT_TRUE(r.step(119999).action == PhyAction::None);
  const PhyStep rb = r.step(120000);
  TEST_ASSERT_TRUE(rb.action == PhyAction::Readback);
  TEST_ASSERT_EQUAL_HEX8(kA, rb.dst);
  TEST_ASSERT_FALSE(r.m.busy());
}

// An ACK under another seq, or from another node, is not this change's.
void test_a_stray_ack_is_not_claimed() {
  Rig r;
  (void)r.step(0);
  TEST_ASSERT_FALSE(r.m.on_config_ack(kB, set_ack(moved()), r.seq, 10));
  TEST_ASSERT_FALSE(r.m.on_config_ack(kA, set_ack(moved()), r.seq + 1, 10));
}

// Step 8 - a node not heard by the deadline reverts the bridge. With two nodes and the
// defaults the deadline is 120 s - 2 x 8 s after the first node's answer.
void test_a_node_not_heard_by_the_deadline_reverts_the_bridge() {
  Rig r;
  r.fan_out(1000);
  (void)r.step(1000);
  r.m.on_retuned(1000);
  r.m.on_heard(kA, 2000);

  const uint32_t deadline = 1000 + 120000 - 2 * kAckMs;
  PhyStep s;
  for (uint32_t t = 2000; t < deadline; t += 1000) {
    s = r.step(t);
    TEST_ASSERT_TRUE(s.action == PhyAction::None || s.action == PhyAction::SendPoll);
    if (s.action == PhyAction::SendPoll) TEST_ASSERT_EQUAL_HEX8(kB, s.dst);
  }
  s = r.step(deadline);
  TEST_ASSERT_TRUE(s.action == PhyAction::Abandon);
  TEST_ASSERT_TRUE(s.reason == PhyReason::NotHeard);
  TEST_ASSERT_EQUAL_HEX8(kB, s.culprit);
  TEST_ASSERT_TRUE(s.retuned);
  TEST_ASSERT_EQUAL_INT32(9, s.group.v[kPhySf]);
  TEST_ASSERT_EQUAL_UINT32(0, r.m.stats().committed);
}

// Step 7 - a GET may go again, and a late answer to the first one still confirms.
void test_an_unanswered_get_is_sent_again_and_a_late_answer_counts() {
  Rig r(1);
  r.fan_out(0);
  (void)r.step(0);
  r.m.on_retuned(0);
  r.m.on_heard(kA, 10);
  TEST_ASSERT_TRUE(r.step(20).action == PhyAction::Commit);
  TEST_ASSERT_TRUE(r.step(20).action == PhyAction::SendGet);
  const Seq first = r.seq;
  TEST_ASSERT_TRUE(r.step(20 + kAckMs).action == PhyAction::SendGet);
  TEST_ASSERT_EQUAL_UINT32(1, r.m.stats().get_resent);
  TEST_ASSERT_TRUE(r.m.on_config_ack(kA, set_ack(moved()), first, 20 + kAckMs + 5));
  TEST_ASSERT_TRUE(r.step(20 + kAckMs + 5).action == PhyAction::NodeConfirmed);
  TEST_ASSERT_FALSE(r.m.busy());
}

// W17 (spec 12.4.4) - a node that never answers a GET is given up on once its window has
// closed, and counted. The change stays committed.
void test_a_node_that_never_confirms_is_counted_as_w17() {
  Rig r(1);
  r.fan_out(0);
  (void)r.step(0);
  r.m.on_retuned(0);
  r.m.on_heard(kA, 10);
  (void)r.step(20);  // Commit
  for (uint32_t t = 20; t < 120000; t += kAckMs) (void)r.step(t);
  (void)r.step(120000 + kAckMs);
  TEST_ASSERT_FALSE(r.m.busy());
  TEST_ASSERT_EQUAL_UINT32(1, r.m.stats().confirm_missed);
  TEST_ASSERT_EQUAL_UINT32(1, r.m.stats().committed);
}

// Step 7's write failed: no GET goes, and the bridge reverts. No phy_reverted reason fits,
// so the abandon carries StoreFailed and the caller publishes no event.
void test_a_failed_commit_reverts_before_any_get() {
  Rig r(1);
  r.fan_out(0);
  (void)r.step(0);
  r.m.on_retuned(0);
  r.m.on_heard(kA, 10);
  TEST_ASSERT_TRUE(r.step(20).action == PhyAction::Commit);
  r.m.commit_failed();
  const PhyStep s = r.step(20);
  TEST_ASSERT_TRUE(s.action == PhyAction::Abandon);
  TEST_ASSERT_TRUE(s.reason == PhyReason::StoreFailed);
  TEST_ASSERT_TRUE(s.retuned);
  TEST_ASSERT_EQUAL_UINT32(0, r.m.stats().committed);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_id_lists_name_the_tables_rows);
  RUN_TEST(test_the_default_group_is_d1s_envelope);
  RUN_TEST(test_a_group_above_the_eirp_ceiling_is_refused);
  RUN_TEST(test_the_set_carries_the_whole_group_under_the_nodes_ids);
  RUN_TEST(test_the_blob_round_trips_and_refuses_another_version);
  RUN_TEST(test_an_equal_group_starts_nothing);
  RUN_TEST(test_a_change_that_works_commits_once_every_node_is_heard);
  RUN_TEST(test_a_refusal_abandons_before_the_bridge_moves);
  RUN_TEST(test_a_clamped_value_abandons);
  RUN_TEST(test_a_missing_ack_abandons_and_defers_the_readback);
  RUN_TEST(test_a_stray_ack_is_not_claimed);
  RUN_TEST(test_a_node_not_heard_by_the_deadline_reverts_the_bridge);
  RUN_TEST(test_an_unanswered_get_is_sent_again_and_a_late_answer_counts);
  RUN_TEST(test_a_node_that_never_confirms_is_counted_as_w17);
  RUN_TEST(test_a_failed_commit_reverts_before_any_get);
  return UNITY_END();
}
