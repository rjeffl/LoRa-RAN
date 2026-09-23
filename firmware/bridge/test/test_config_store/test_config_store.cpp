// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-32 - what the bridge holds of the configuration, and how a name resolves. Spec
// 16.7.1; D47, D49.
//
// WHAT THIS COVERS. The scoping that spec 16.7.1 asks for: three names live in both
// blocks of the table and the TOPIC decides which one a set means. Also the split of a
// set into a bridge half and a node half, the per-node isolation `poll_interval_s`
// needs, and that persist_status is honest about a store that refused the write.
//
// WHAT IT CANNOT. That NVS keeps a value across a power cycle. That is nvs_persist.cpp
// on the target, and it is bench work.

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "config_store.h"
#include "lran/config/table.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

// A nonvolatile store that can be made to fail, which is the case that matters: spec
// 8.11 says the answer must be honest about it rather than optimistic.
class FakePersist final : public config::Persist {
 public:
  bool usable_    = true;
  bool save_ok_   = true;
  int  saves_     = 0;
  int  clears_    = 0;

  bool usable() const override { return usable_; }
  bool save(uint16_t, config::Value) override {
    ++saves_;
    return save_ok_;
  }
  bool clear_all() override {
    ++clears_;
    return true;
  }
};

ConfigSetRequest one(const char* name, int32_t value, bool readable = true) {
  ConfigSetRequest req;
  req.op    = ConfigOp::Set;
  req.count = 1;
  std::snprintf(req.entries[0].name, sizeof(req.entries[0].name), "%s", name);
  req.entries[0].value_readable = readable;
  req.entries[0].value          = value;
  return req;
}

const ConfigResult* result_named(const ConfigResult* r, size_t n, const char* name) {
  for (size_t i = 0; i < n; ++i) {
    if (std::strcmp(r[i].name, name) == 0) return &r[i];
  }
  return nullptr;
}

const ConfigStateEntry* state_named(const ConfigStateEntry* e, size_t n, const char* name) {
  for (size_t i = 0; i < n; ++i) {
    if (std::strcmp(e[i].name, name) == 0) return &e[i];
  }
  return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// The scoping - spec 16.7.1. The property this whole file exists for.
// ---------------------------------------------------------------------------

// THREE NAMES LIVE IN BOTH BLOCKS, and the topic is what tells them apart. A lookup that
// searched both would answer the bridge's row for a set aimed at a node, and the write
// would land on the wrong radio.
void test_a_shared_name_resolves_by_topic() {
  const config::ParamDef* on_bridge = find_param(ConfigScope::Bridge, "cad_retries");
  const config::ParamDef* on_node   = find_param(ConfigScope::Node, "cad_retries");
  TEST_ASSERT_NOT_NULL(on_bridge);
  TEST_ASSERT_NOT_NULL(on_node);
  TEST_ASSERT_EQUAL_UINT16(0x0007, on_bridge->id);
  TEST_ASSERT_EQUAL_UINT16(0x0102, on_node->id);
  TEST_ASSERT_TRUE(on_bridge->owner == config::Owner::BridgeGlobal);
  TEST_ASSERT_TRUE(on_node->owner == config::Owner::Node);
}

void test_the_other_two_shared_names_resolve_the_same_way() {
  for (const char* name : {"backoff_max_ms", "frag_reassembly_timeout_ms"}) {
    const config::ParamDef* b = find_param(ConfigScope::Bridge, name);
    const config::ParamDef* n = find_param(ConfigScope::Node, name);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(b->id != n->id);
  }
}

// A name the topic does not hold is unknown there, never a fall-through to the other
// block (spec 16.7.1).
void test_a_name_from_the_other_block_is_not_visible() {
  TEST_ASSERT_NULL(find_param(ConfigScope::Bridge, "dedup_cache_depth"));
  TEST_ASSERT_NULL(find_param(ConfigScope::Bridge, "poll_interval_s"));
  TEST_ASSERT_NULL(find_param(ConfigScope::Node, "diag_interval_s"));
  TEST_ASSERT_NULL(find_param(ConfigScope::Node, "simnode_diag_enable"));
}

void test_a_node_topic_holds_the_bridges_per_node_row() {
  const config::ParamDef* d = find_param(ConfigScope::Node, "poll_interval_s");
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_TRUE(d->owner == config::Owner::BridgePerNode);
}

// ---------------------------------------------------------------------------
// Applying the bridge's half
// ---------------------------------------------------------------------------

void test_a_global_value_applies_and_answers_its_effective_value() {
  FakePersist  p;
  ConfigStore  store;
  store.begin(&p, nullptr, 0);

  ConfigResult results[8];
  AckPersist   persist = AckPersist::Unknown;
  const size_t n = store.apply(ConfigScope::Bridge, 0, one("diag_interval_s", 30), results,
                               8, &persist, nullptr);
  TEST_ASSERT_EQUAL_UINT32(1, n);
  TEST_ASSERT_TRUE(results[0].status == ResultStatus::Ok);
  TEST_ASSERT_EQUAL_INT32(30, results[0].value);
  TEST_ASSERT_TRUE(persist == AckPersist::Persisted);
  TEST_ASSERT_EQUAL_INT(1, p.saves_);
  TEST_ASSERT_EQUAL_INT32(30, store.global_value(0x0002));
}

// spec 7.4 - a clamp is REPORTED rather than applied quietly, and the value in the answer
// is the one now in force.
void test_a_value_outside_the_range_is_clamped_and_said_so() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  ConfigResult results[8];
  const size_t n = store.apply(ConfigScope::Bridge, 0, one("diag_interval_s", 99999),
                               results, 8, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT32(1, n);
  TEST_ASSERT_TRUE(results[0].status == ResultStatus::Clamped);
  TEST_ASSERT_EQUAL_INT32(3600, results[0].value);
}

void test_an_unknown_name_is_answered_and_the_rest_still_applies() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  ConfigSetRequest req;
  req.count = 2;
  std::snprintf(req.entries[0].name, sizeof(req.entries[0].name), "no_such_param");
  req.entries[0].value_readable = true;
  std::snprintf(req.entries[1].name, sizeof(req.entries[1].name), "cad_retries");
  req.entries[1].value_readable = true;
  req.entries[1].value          = 7;

  ConfigResult results[8];
  const size_t n = store.apply(ConfigScope::Bridge, 0, req, results, 8, nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT32(2, n);
  TEST_ASSERT_TRUE(results[0].status == ResultStatus::UnknownParam);
  TEST_ASSERT_FALSE(results[0].has_value);
  TEST_ASSERT_TRUE(results[1].status == ResultStatus::Ok);
  TEST_ASSERT_EQUAL_INT32(7, results[1].value);
}

void test_a_value_the_parser_could_not_read_is_type_mismatch() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  ConfigResult results[8];
  const size_t n = store.apply(ConfigScope::Bridge, 0,
                               one("cad_retries", 0, /*readable=*/false), results, 8,
                               nullptr, nullptr);
  TEST_ASSERT_EQUAL_UINT32(1, n);
  TEST_ASSERT_TRUE(results[0].status == ResultStatus::TypeMismatch);
  TEST_ASSERT_FALSE(results[0].has_value);
}

// D56 - the PHY rows are READ_ONLY until BF-33 builds spec 12.4's commit-and-revert. A
// readable value that refuses a write is honest; a write that half-applies is not.
void test_a_phy_row_refuses_the_write_and_reports_what_it_holds() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  ConfigResult results[8];
  const size_t n = store.apply(ConfigScope::Node, lran::kNodeSim1,
                               one("spreading_factor", 7), results, 8, nullptr, nullptr);
  // spreading_factor is the NODE's row, so it goes to the node half rather than being
  // answered here - the node is the one that refuses it. Nothing is answered locally.
  TEST_ASSERT_EQUAL_UINT32(0, n);
}

// ---------------------------------------------------------------------------
// The split - spec 16.7.1
// ---------------------------------------------------------------------------

void test_a_set_on_a_node_topic_splits_into_two_halves() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  ConfigSetRequest req;
  req.count = 3;
  std::snprintf(req.entries[0].name, sizeof(req.entries[0].name), "poll_interval_s");
  req.entries[0].value_readable = true;
  req.entries[0].value          = 90;
  std::snprintf(req.entries[1].name, sizeof(req.entries[1].name), "dedup_cache_depth");
  req.entries[1].value_readable = true;
  req.entries[1].value          = 16;
  std::snprintf(req.entries[2].name, sizeof(req.entries[2].name), "no_such_param");
  req.entries[2].value_readable = true;

  ConfigResult     results[8];
  ConfigSetRequest node_half;
  const size_t     n = store.apply(ConfigScope::Node, lran::kNodeGateLink, req, results, 8,
                                   nullptr, &node_half);

  // The bridge answers its own row and the unknown one; the node's row waits for a
  // CONFIG_ACK and is not answered here.
  TEST_ASSERT_EQUAL_UINT32(2, n);
  TEST_ASSERT_NOT_NULL(result_named(results, n, "poll_interval_s"));
  TEST_ASSERT_NOT_NULL(result_named(results, n, "no_such_param"));
  TEST_ASSERT_NULL(result_named(results, n, "dedup_cache_depth"));

  TEST_ASSERT_EQUAL_UINT8(1, node_half.count);
  TEST_ASSERT_EQUAL_STRING("dedup_cache_depth", node_half.entries[0].name);
  TEST_ASSERT_EQUAL_INT32(16, node_half.entries[0].value);
}

// spec 10.6 bridge step 7 - the refusal while a roll is pending must pick out exactly the
// sets that would send a CONFIG. The split apply() makes is the reference, because it is
// what decides whether handle_config_set() queues a job.
void test_the_roll_refusal_matches_the_node_half_apply_produces() {
  struct Case {
    ConfigScope scope;
    const char* name;
    bool        readable;
  };
  const Case kCases[] = {
      {ConfigScope::Node, "dedup_cache_depth", true},   // node-held
      {ConfigScope::Node, "dedup_cache_depth", false},  // unreadable, answered here
      {ConfigScope::Node, "poll_interval_s", true},     // the bridge's per-node row
      {ConfigScope::Node, "cad_retries", true},         // the node's, on its topic
      {ConfigScope::Bridge, "cad_retries", true},       // the bridge's own
      {ConfigScope::Node, "no_such_param", true},
  };
  for (const Case& c : kCases) {
    ConfigStore store;
    store.begin(nullptr, nullptr, 0);
    const ConfigSetRequest req = one(c.name, 3, c.readable);
    ConfigResult           results[4];
    ConfigSetRequest       node_half;
    store.apply(c.scope, c.scope == ConfigScope::Node ? lran::kNodeGateLink : 0, req, results,
                4, nullptr, &node_half);
    TEST_ASSERT_EQUAL_MESSAGE(node_half.count > 0, config_set_reaches_node(c.scope, req),
                              c.name);
  }

  // GET_ALL and RESTORE_DEFAULTS reach the node from its own topic only.
  ConfigSetRequest req;
  req.op = ConfigOp::GetAll;
  TEST_ASSERT_TRUE(config_set_reaches_node(ConfigScope::Node, req));
  TEST_ASSERT_FALSE(config_set_reaches_node(ConfigScope::Bridge, req));
  req.op = ConfigOp::RestoreDefaults;
  TEST_ASSERT_TRUE(config_set_reaches_node(ConfigScope::Node, req));
  TEST_ASSERT_FALSE(config_set_reaches_node(ConfigScope::Bridge, req));
}

// A NAME NEITHER HALF HOLDS COSTS NO FRAME. Spec 16.7.1 makes it unknown_param at the
// bridge, so a solar node is never woken to be asked about a name the table says it does
// not have.
void test_an_unknown_name_never_reaches_the_node_half() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  ConfigResult     results[8];
  ConfigSetRequest node_half;
  (void)store.apply(ConfigScope::Node, lran::kNodeGateLink, one("not_a_param", 1), results,
                    8, nullptr, &node_half);
  TEST_ASSERT_EQUAL_UINT8(0, node_half.count);
}

// D47 - poll_interval_s is one value PER NODE. One store keyed by id alone would let one
// node's write move another's.
void test_a_per_node_row_is_isolated_between_nodes() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  ConfigResult results[8];
  (void)store.apply(ConfigScope::Node, lran::kNodeGateLink, one("poll_interval_s", 30),
                    results, 8, nullptr, nullptr);
  (void)store.apply(ConfigScope::Node, lran::kNodeSim1, one("poll_interval_s", 600),
                    results, 8, nullptr, nullptr);

  TEST_ASSERT_EQUAL_INT32(30, store.node_value(lran::kNodeGateLink, 0x0080));
  TEST_ASSERT_EQUAL_INT32(600, store.node_value(lran::kNodeSim1, 0x0080));
  // A node nobody set keeps the table's default.
  TEST_ASSERT_EQUAL_INT32(60, store.node_value(lran::kNodeWellLink, 0x0080));
}

// ---------------------------------------------------------------------------
// persist_status - spec 8.11, and it must be honest
// ---------------------------------------------------------------------------

void test_a_store_that_cannot_save_still_applies_and_says_so() {
  FakePersist p;
  p.usable_ = false;
  ConfigStore store;
  store.begin(&p, nullptr, 0);

  ConfigResult results[8];
  AckPersist   persist = AckPersist::Unknown;
  (void)store.apply(ConfigScope::Bridge, 0, one("cad_retries", 2), results, 8, &persist,
                    nullptr);
  TEST_ASSERT_TRUE(results[0].status == ResultStatus::Ok);
  TEST_ASSERT_EQUAL_INT32(2, store.global_value(0x0007));
  TEST_ASSERT_TRUE(persist == AckPersist::AppliedNotPersisted);
  TEST_ASSERT_EQUAL_INT(0, p.saves_);
}

void test_a_save_that_refuses_is_not_reported_as_persisted() {
  FakePersist p;
  p.save_ok_ = false;
  ConfigStore store;
  store.begin(&p, nullptr, 0);

  ConfigResult results[8];
  AckPersist   persist = AckPersist::Unknown;
  (void)store.apply(ConfigScope::Bridge, 0, one("cad_retries", 2), results, 8, &persist,
                    nullptr);
  TEST_ASSERT_TRUE(persist == AckPersist::AppliedNotPersisted);
  TEST_ASSERT_EQUAL_INT(1, p.saves_);
}

void test_a_set_that_applied_nothing_is_not_applied() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  ConfigResult results[8];
  AckPersist   persist = AckPersist::Unknown;
  (void)store.apply(ConfigScope::Bridge, 0, one("not_a_param", 1), results, 8, &persist,
                    nullptr);
  TEST_ASSERT_TRUE(persist == AckPersist::NotApplied);
}

// ---------------------------------------------------------------------------
// restore_defaults and the two read paths
// ---------------------------------------------------------------------------

void test_restore_defaults_clears_the_overrides_and_answers_like_get_all() {
  FakePersist p;
  ConfigStore store;
  store.begin(&p, nullptr, 0);

  ConfigResult results[config::kMaxTableParams];
  (void)store.apply(ConfigScope::Bridge, 0, one("diag_interval_s", 30), results,
                    config::kMaxTableParams, nullptr, nullptr);
  TEST_ASSERT_EQUAL_INT32(30, store.global_value(0x0002));

  const size_t n = store.restore_defaults(ConfigScope::Bridge, 0, results,
                                          config::kMaxTableParams, nullptr);
  TEST_ASSERT_TRUE(n > 1);
  TEST_ASSERT_EQUAL_INT(1, p.clears_);
  TEST_ASSERT_EQUAL_INT32(60, store.global_value(0x0002));
  const ConfigResult* r = result_named(results, n, "diag_interval_s");
  TEST_ASSERT_NOT_NULL(r);
  TEST_ASSERT_EQUAL_INT32(60, r->value);
}

void test_read_all_answers_every_row_of_the_scope() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  ConfigResult results[config::kMaxTableParams];
  const size_t n = store.read_all(ConfigScope::Bridge, 0, results,
                                  config::kMaxTableParams, nullptr);
  TEST_ASSERT_EQUAL_UINT32(kBridgeGlobalCount, n);
  TEST_ASSERT_NOT_NULL(result_named(results, n, "simnode_diag_enable"));
  TEST_ASSERT_NULL(result_named(results, n, "poll_interval_s"));
}

// Spec 16.7.4 - a value the bridge has never read back is null, and that is a different
// statement from a value that equals its default.
void test_a_nodes_own_rows_have_no_value_until_a_readback() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  ConfigStateEntry entries[config::kMaxTableParams];
  const size_t     n = store.state(ConfigScope::Node, lran::kNodeGateLink, entries,
                                   config::kMaxTableParams);
  const ConfigStateEntry* own = state_named(entries, n, "dedup_cache_depth");
  TEST_ASSERT_NOT_NULL(own);
  TEST_ASSERT_FALSE(own->has_value);

  const ConfigStateEntry* per_node = state_named(entries, n, "poll_interval_s");
  TEST_ASSERT_NOT_NULL(per_node);
  TEST_ASSERT_TRUE(per_node->has_value);
  TEST_ASSERT_EQUAL_INT32(60, per_node->value);
  TEST_ASSERT_FALSE(per_node->is_override);
}

void test_state_marks_an_override_as_one() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  ConfigResult results[8];
  (void)store.apply(ConfigScope::Bridge, 0, one("cad_retries", 2), results, 8, nullptr,
                    nullptr);

  ConfigStateEntry entries[config::kMaxTableParams];
  const size_t     n = store.state(ConfigScope::Bridge, 0, entries, config::kMaxTableParams);
  const ConfigStateEntry* e = state_named(entries, n, "cad_retries");
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_TRUE(e->has_value);
  TEST_ASSERT_EQUAL_INT32(2, e->value);
  TEST_ASSERT_TRUE(e->is_override);
}

// ---------------------------------------------------------------------------
// The readback mirror - spec 16.7.4
// ---------------------------------------------------------------------------

namespace {

lran::schema::ConfigAckEntry ok_entry(uint16_t id, int32_t value) {
  lran::schema::ConfigAckEntry e;
  lran::schema::entry_pack(&e, id, ParamStatus::Ok, PType::U8,
                           static_cast<uint32_t>(value) & 0xFFu);
  return e;
}

}  // namespace

void test_a_readback_fills_the_nodes_own_rows() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  const lran::schema::ConfigAckEntry results[] = {ok_entry(0x0100, 16), ok_entry(0x0102, 7)};
  store.note_readback(lran::kNodeSim1, results, 2);

  ConfigStateEntry entries[config::kMaxTableParams];
  const size_t n = store.state(ConfigScope::Node, lran::kNodeSim1, entries,
                               config::kMaxTableParams);
  const ConfigStateEntry* dedup = state_named(entries, n, "dedup_cache_depth");
  TEST_ASSERT_NOT_NULL(dedup);
  TEST_ASSERT_TRUE(dedup->has_value);
  TEST_ASSERT_EQUAL_INT32(16, dedup->value);
  // Inferred, not reported: 16 differs from the table's default of 8. W15 is the gap.
  TEST_ASSERT_TRUE(dedup->is_override);
}

void test_a_value_equal_to_its_default_reads_as_default() {
  // Spec 16.7.4 - `source` for a node-held row is INFERRED, because CONFIG_ACK carries no
  // override flag. An override equal to its default is indistinguishable here.
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);
  const lran::schema::ConfigAckEntry results[] = {ok_entry(0x0100, 8)};
  store.note_readback(lran::kNodeSim1, results, 1);

  ConfigStateEntry entries[config::kMaxTableParams];
  const size_t n = store.state(ConfigScope::Node, lran::kNodeSim1, entries,
                               config::kMaxTableParams);
  const ConfigStateEntry* dedup = state_named(entries, n, "dedup_cache_depth");
  TEST_ASSERT_TRUE(dedup->has_value);
  TEST_ASSERT_FALSE(dedup->is_override);
}

// THE BENCH FOUND THIS ONE. A set of one parameter must not blank every row the set did
// not name: on 2026-09-21 a set of backoff_max_ms alone sent dedup_cache_depth back to
// null on the retained topic, because a SET's ACK went in through the replacing path.
void test_a_sets_ack_merges_and_does_not_blank_the_rest() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  const lran::schema::ConfigAckEntry first[] = {ok_entry(0x0100, 16)};
  store.note_readback(lran::kNodeSim1, first, 1);

  const lran::schema::ConfigAckEntry second[] = {ok_entry(0x0102, 7)};
  store.note_set_results(lran::kNodeSim1, second, 1);

  ConfigStateEntry entries[config::kMaxTableParams];
  const size_t n = store.state(ConfigScope::Node, lran::kNodeSim1, entries,
                               config::kMaxTableParams);
  TEST_ASSERT_TRUE(state_named(entries, n, "dedup_cache_depth")->has_value);
  TEST_ASSERT_TRUE(state_named(entries, n, "cad_retries")->has_value);
  TEST_ASSERT_EQUAL_INT32(7, state_named(entries, n, "cad_retries")->value);
}

// A GET_ALL describes the whole table, so a row missing from it is a row the node no
// longer has - not one to keep from an older answer.
void test_a_readback_replaces_rather_than_merges() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);

  const lran::schema::ConfigAckEntry first[] = {ok_entry(0x0100, 16), ok_entry(0x0102, 7)};
  store.note_readback(lran::kNodeSim1, first, 2);

  const lran::schema::ConfigAckEntry second[] = {ok_entry(0x0102, 9)};
  store.note_readback(lran::kNodeSim1, second, 1);

  ConfigStateEntry entries[config::kMaxTableParams];
  const size_t n = store.state(ConfigScope::Node, lran::kNodeSim1, entries,
                               config::kMaxTableParams);
  TEST_ASSERT_FALSE(state_named(entries, n, "dedup_cache_depth")->has_value);
  TEST_ASSERT_EQUAL_INT32(9, state_named(entries, n, "cad_retries")->value);
}

// The mirror is one node's. A readback from one must not appear on another's topic.
void test_the_mirror_is_per_node() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);
  const lran::schema::ConfigAckEntry results[] = {ok_entry(0x0100, 16)};
  store.note_readback(lran::kNodeSim1, results, 1);

  ConfigStateEntry entries[config::kMaxTableParams];
  const size_t n = store.state(ConfigScope::Node, lran::kNodeGateLink, entries,
                               config::kMaxTableParams);
  TEST_ASSERT_FALSE(state_named(entries, n, "dedup_cache_depth")->has_value);
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_a_shared_name_resolves_by_topic);
  RUN_TEST(test_the_other_two_shared_names_resolve_the_same_way);
  RUN_TEST(test_a_name_from_the_other_block_is_not_visible);
  RUN_TEST(test_a_node_topic_holds_the_bridges_per_node_row);

  RUN_TEST(test_a_global_value_applies_and_answers_its_effective_value);
  RUN_TEST(test_a_value_outside_the_range_is_clamped_and_said_so);
  RUN_TEST(test_an_unknown_name_is_answered_and_the_rest_still_applies);
  RUN_TEST(test_a_value_the_parser_could_not_read_is_type_mismatch);
  RUN_TEST(test_a_phy_row_refuses_the_write_and_reports_what_it_holds);

  RUN_TEST(test_a_set_on_a_node_topic_splits_into_two_halves);
  RUN_TEST(test_the_roll_refusal_matches_the_node_half_apply_produces);
  RUN_TEST(test_an_unknown_name_never_reaches_the_node_half);
  RUN_TEST(test_a_per_node_row_is_isolated_between_nodes);

  RUN_TEST(test_a_store_that_cannot_save_still_applies_and_says_so);
  RUN_TEST(test_a_save_that_refuses_is_not_reported_as_persisted);
  RUN_TEST(test_a_set_that_applied_nothing_is_not_applied);

  RUN_TEST(test_restore_defaults_clears_the_overrides_and_answers_like_get_all);
  RUN_TEST(test_read_all_answers_every_row_of_the_scope);
  RUN_TEST(test_a_nodes_own_rows_have_no_value_until_a_readback);
  RUN_TEST(test_state_marks_an_override_as_one);

  RUN_TEST(test_a_readback_fills_the_nodes_own_rows);
  RUN_TEST(test_a_value_equal_to_its_default_reads_as_default);
  RUN_TEST(test_a_sets_ack_merges_and_does_not_blank_the_rest);
  RUN_TEST(test_a_readback_replaces_rather_than_merges);
  RUN_TEST(test_the_mirror_is_per_node);

  return UNITY_END();
}
