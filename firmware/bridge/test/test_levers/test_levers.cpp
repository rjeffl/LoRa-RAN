// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-23's lever half - what reaches each runtime lever from the configuration store.
// Root rule 8; spec 16.7.1; D47.
//
// THE PROPERTY UNDER TEST is that a consumer runs the store's value and never a constant
// of its own. Two ways that fails quietly: the table's default and a consumer's
// compile-time default drift apart, so a bridge with no override runs a number the table
// does not show HA; or a value lands on the wrong lever, the bridge's or another node's.
//
// WHAT THIS CANNOT COVER. That sched_task and lora_task apply what they take, and that a
// value survives a reboot through NVS. Both need FreeRTOS and a board; the engineering log
// records the bench run. A reboot restores through Store::apply() (nvs_persist.h), which
// is the path these tests drive directly.

#include <unity.h>

#include <cstdio>

#include "command.h"
#include "config_path.h"
#include "config_store.h"
#include "diag_json.h"
#include "error_reply.h"
#include "levers.h"
#include "lran/config.h"
#include "lran/link/media_access.h"
#include "node_availability.h"
#include "registry.h"
#include "scheduler.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

ConfigSetRequest one(const char* name, int32_t value) {
  ConfigSetRequest req;
  req.op    = ConfigOp::Set;
  req.count = 1;
  std::snprintf(req.entries[0].name, sizeof(req.entries[0].name), "%s", name);
  req.entries[0].value_readable = true;
  req.entries[0].value          = value;
  return req;
}

void set(ConfigStore& store, ConfigScope scope, NodeId node, const char* name, int32_t v) {
  ConfigResult     results[8];
  AckPersist       persist = AckPersist::Unknown;
  ConfigSetRequest node_half;
  (void)store.apply(scope, node, one(name, v), results, 8, &persist, &node_half);
}

size_t index_of(NodeId id) {
  for (size_t i = 0; i < kNodeCount; ++i) {
    if (kNodeTable[i].id == id) return i;
  }
  TEST_FAIL_MESSAGE("node not in kNodeTable");
  return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// The store's values reach the right lever.
// ---------------------------------------------------------------------------

// A bridge with no override runs the table's defaults, so each consumer's own constant
// must equal its row's default. When they differ, the bridge runs one number until the
// first lever publish and another after it, and HA's config/state shows only the second.
void test_the_table_defaults_are_the_consumers_defaults() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);
  const Levers v = levers_from(store);

  TEST_ASSERT_EQUAL_UINT16(kDiagPublishIntervalDefaultS, v.diag_interval_s);
  TEST_ASSERT_EQUAL_UINT16(kMissedPollThresholdDefault, v.missed_poll_threshold);
  TEST_ASSERT_EQUAL_UINT32(kPollReplyTimeoutDefaultMs, v.poll_reply_timeout_ms);
  TEST_ASSERT_EQUAL_UINT32(kCommandAckTimeoutDefaultMs, v.command_ack_timeout_ms);
  TEST_ASSERT_EQUAL_UINT8(kCommandRetriesDefault, v.cmd_retries);
  TEST_ASSERT_EQUAL_UINT8(link::MediaAccessConfig{}.cad_retries, v.cad_retries);
  TEST_ASSERT_EQUAL_UINT32(link::MediaAccessConfig{}.backoff_max_ms, v.backoff_max_ms);
  TEST_ASSERT_EQUAL_UINT32(kDefaultFragTimeoutMs, v.frag_reassembly_timeout_ms);
  TEST_ASSERT_EQUAL_UINT32(kErrorMinIntervalDefaultMs, v.error_min_interval_ms);
  TEST_ASSERT_EQUAL_UINT32(kConfigReadbackTimeoutDefaultMs, v.config_readback_timeout_ms);
  TEST_ASSERT_EQUAL_UINT32(kConfigAckTimeoutDefaultMs, v.config_ack_timeout_ms);
  for (size_t i = 0; i < kNodeCount; ++i) {
    TEST_ASSERT_EQUAL_UINT16(kPollIntervalDefaultS, v.poll_interval_s[i]);
  }
  TEST_ASSERT_FALSE(v.simnode_diag_enable);  // spec 16.6 - off unless someone sets it
}

void test_each_global_override_reaches_its_lever() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);
  set(store, ConfigScope::Bridge, 0, "diag_interval_s", 15);
  set(store, ConfigScope::Bridge, 0, "missed_poll_threshold", 7);
  set(store, ConfigScope::Bridge, 0, "poll_reply_timeout_ms", 12000);
  set(store, ConfigScope::Bridge, 0, "command_ack_timeout_ms", 4000);
  set(store, ConfigScope::Bridge, 0, "cmd_retries", 1);
  set(store, ConfigScope::Bridge, 0, "cad_retries", 2);
  set(store, ConfigScope::Bridge, 0, "backoff_max_ms", 900);
  set(store, ConfigScope::Bridge, 0, "frag_reassembly_timeout_ms", 7000);
  set(store, ConfigScope::Bridge, 0, "error_min_interval_ms", 2500);
  set(store, ConfigScope::Bridge, 0, "config_readback_timeout_ms", 20000);
  set(store, ConfigScope::Bridge, 0, "config_ack_timeout_ms", 12000);
  set(store, ConfigScope::Bridge, 0, "simnode_diag_enable", 1);

  const Levers v = levers_from(store);
  TEST_ASSERT_EQUAL_UINT16(15, v.diag_interval_s);
  TEST_ASSERT_EQUAL_UINT16(7, v.missed_poll_threshold);
  TEST_ASSERT_EQUAL_UINT32(12000, v.poll_reply_timeout_ms);
  TEST_ASSERT_EQUAL_UINT32(4000, v.command_ack_timeout_ms);
  TEST_ASSERT_EQUAL_UINT8(1, v.cmd_retries);
  TEST_ASSERT_EQUAL_UINT8(2, v.cad_retries);
  TEST_ASSERT_EQUAL_UINT32(900, v.backoff_max_ms);
  TEST_ASSERT_EQUAL_UINT32(7000, v.frag_reassembly_timeout_ms);
  TEST_ASSERT_EQUAL_UINT32(2500, v.error_min_interval_ms);
  TEST_ASSERT_EQUAL_UINT32(20000, v.config_readback_timeout_ms);
  TEST_ASSERT_EQUAL_UINT32(12000, v.config_ack_timeout_ms);
  TEST_ASSERT_TRUE(v.simnode_diag_enable);
}

// A value outside its row's range is clamped by the store, and the lever runs the clamped
// value - the one config/ack reported - not the one HA asked for.
void test_a_clamped_value_is_what_the_lever_runs() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);
  set(store, ConfigScope::Bridge, 0, "diag_interval_s", 1);  // min 10
  TEST_ASSERT_EQUAL_UINT16(10, levers_from(store).diag_interval_s);
}

// config_store.h's hazard, from the lever's side. `cad_retries` on a node's topic is that
// node's row, sent to its radio as CONFIG. The bridge's own radio must not move.
void test_a_node_scoped_radio_row_leaves_the_bridges_lever_alone() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);
  set(store, ConfigScope::Node, kNodeGateLink, "cad_retries", 1);
  set(store, ConfigScope::Node, kNodeGateLink, "backoff_max_ms", 400);
  const Levers v = levers_from(store);
  TEST_ASSERT_EQUAL_UINT8(link::MediaAccessConfig{}.cad_retries, v.cad_retries);
  TEST_ASSERT_EQUAL_UINT32(link::MediaAccessConfig{}.backoff_max_ms, v.backoff_max_ms);
}

// D47 - one poll interval per node, at that node's index and no other.
void test_a_poll_interval_lands_on_its_own_node_only() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);
  set(store, ConfigScope::Node, kNodeSim1, "poll_interval_s", 20);
  const Levers v = levers_from(store);
  for (size_t i = 0; i < kNodeCount; ++i) {
    const uint16_t want = i == index_of(kNodeSim1) ? 20 : kPollIntervalDefaultS;
    TEST_ASSERT_EQUAL_UINT16(want, v.poll_interval_s[i]);
  }
}

// D52 - RESTORE_DEFAULTS puts every lever in the scope back on its default.
void test_restore_defaults_returns_the_levers_to_their_defaults() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);
  set(store, ConfigScope::Bridge, 0, "poll_reply_timeout_ms", 20000);
  ConfigResult results[32];
  AckPersist   persist = AckPersist::Unknown;
  (void)store.restore_defaults(ConfigScope::Bridge, 0, results, 32, &persist);
  TEST_ASSERT_EQUAL_UINT32(kPollReplyTimeoutDefaultMs, levers_from(store).poll_reply_timeout_ms);
}

// ---------------------------------------------------------------------------
// The board between tasks.
// ---------------------------------------------------------------------------

// Nothing published means nothing to apply: a task that starts before config_begin()
// keeps its compile-time defaults rather than applying zeros.
void test_an_unpublished_board_offers_nothing() {
  LeverBoard board;
  uint32_t   seen = 0;
  Levers     out;
  TEST_ASSERT_FALSE(board.take_if_changed(&seen, &out));
}

// Each reader takes each publish once, independently of the others - sched_task and
// lora_task hold their own `seen`.
void test_each_reader_takes_each_publish_once() {
  ConfigStore store;
  store.begin(nullptr, nullptr, 0);
  LeverBoard board;
  board.publish(levers_from(store));

  uint32_t sched_seen = 0;
  uint32_t lora_seen  = 0;
  Levers   out;
  TEST_ASSERT_TRUE(board.take_if_changed(&sched_seen, &out));
  TEST_ASSERT_EQUAL_UINT32(kPollReplyTimeoutDefaultMs, out.poll_reply_timeout_ms);
  TEST_ASSERT_FALSE(board.take_if_changed(&sched_seen, &out));
  TEST_ASSERT_TRUE(board.take_if_changed(&lora_seen, &out));
  TEST_ASSERT_FALSE(board.take_if_changed(&lora_seen, &out));

  set(store, ConfigScope::Bridge, 0, "backoff_max_ms", 800);
  set(store, ConfigScope::Node, kNodeGateLink, "poll_interval_s", 300);
  set(store, ConfigScope::Bridge, 0, "simnode_diag_enable", 1);
  board.publish(levers_from(store));
  TEST_ASSERT_TRUE(board.take_if_changed(&lora_seen, &out));
  TEST_ASSERT_EQUAL_UINT32(800, out.backoff_max_ms);
  TEST_ASSERT_TRUE(out.simnode_diag_enable);
  TEST_ASSERT_EQUAL_UINT16(300, out.poll_interval_s[index_of(kNodeGateLink)]);
  TEST_ASSERT_TRUE(board.take_if_changed(&sched_seen, &out));
}

// The generation stays even between publishes. An odd one is the mark of a publish
// under way, and take_if_changed() refuses to copy while it shows.
void test_the_generation_is_even_between_publishes() {
  LeverBoard board;
  Levers     v;
  TEST_ASSERT_EQUAL_UINT32(0, board.generation());
  board.publish(v);
  TEST_ASSERT_EQUAL_UINT32(2, board.generation());
  board.publish(v);
  TEST_ASSERT_EQUAL_UINT32(4, board.generation());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_table_defaults_are_the_consumers_defaults);
  RUN_TEST(test_each_global_override_reaches_its_lever);
  RUN_TEST(test_a_clamped_value_is_what_the_lever_runs);
  RUN_TEST(test_a_node_scoped_radio_row_leaves_the_bridges_lever_alone);
  RUN_TEST(test_a_poll_interval_lands_on_its_own_node_only);
  RUN_TEST(test_restore_defaults_returns_the_levers_to_their_defaults);
  RUN_TEST(test_an_unpublished_board_offers_nothing);
  RUN_TEST(test_each_reader_takes_each_publish_once);
  RUN_TEST(test_the_generation_is_even_between_publishes);
  return UNITY_END();
}
