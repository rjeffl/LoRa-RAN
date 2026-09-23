// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-32 - the `config/*` documents and their topics. Spec 16.7.
//
// WHAT THIS COVERS. Spec 16.7.2's payload, read strictly; the two kinds of refusal it
// separates; 16.7.3's combination rule for a set with two halves; and that the largest
// document either table can produce still fits kMaxPayloadLen.
//
// WHAT IT CANNOT. That Home Assistant's `command_template` produces what the parser
// accepts, or that a `number` entity reads `config/state` back. Those need a broker and
// an HA instance (B6), and the shapes here are copied from spec 16.7.2's examples.

#include <unity.h>

#include <cstdio>
#include <cstring>

#include "config_json.h"
#include "lran/config/store.h"
#include "lran/config/table.h"
#include "mqtt_transport.h"
#include "net_policy.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

bool parse(const char* payload, ConfigSetRequest* out, const char** error) {
  return parse_config_set(payload, std::strlen(payload), out, error);
}

}  // namespace

// ---------------------------------------------------------------------------
// The topics - spec 16.7, 16.1's grammar.
// ---------------------------------------------------------------------------

void test_the_bridge_is_a_legitimate_config_target() {
  // It is not one for `cmd`. A parser that returned an address here would answer 0x00,
  // which is the bridge addressing itself.
  ConfigTopic t;
  TEST_ASSERT_TRUE(parse_config_topic("lran/bridge/config/set", &t));
  TEST_ASSERT_TRUE(t.is_bridge);
}

void test_a_node_config_topic_yields_its_address() {
  ConfigTopic t;
  TEST_ASSERT_TRUE(parse_config_topic("lran/gatelink/config/set", &t));
  TEST_ASSERT_FALSE(t.is_bridge);
  TEST_ASSERT_EQUAL_UINT8(0x01, t.node_id);

  TEST_ASSERT_TRUE(parse_config_topic("lran/simnode1/config/set", &t));
  TEST_ASSERT_EQUAL_UINT8(0xF1, t.node_id);
}

void test_anything_but_that_exact_shape_is_refused() {
  ConfigTopic t;
  TEST_ASSERT_FALSE(parse_config_topic("lran/gatelink/config/state", &t));
  TEST_ASSERT_FALSE(parse_config_topic("lran/gatelink/config/set/extra", &t));
  TEST_ASSERT_FALSE(parse_config_topic("lran/nosuchnode/config/set", &t));
  TEST_ASSERT_FALSE(parse_config_topic("lran/gatelink/cmd/set", &t));
  TEST_ASSERT_FALSE(parse_config_topic("other/gatelink/config/set", &t));
}

void test_the_published_config_topics() {
  char topic[64];
  TEST_ASSERT_TRUE(topic_config("gatelink", "ack", topic, sizeof(topic)) > 0);
  TEST_ASSERT_EQUAL_STRING("lran/gatelink/config/ack", topic);
  TEST_ASSERT_TRUE(topic_config("bridge", "state", topic, sizeof(topic)) > 0);
  TEST_ASSERT_EQUAL_STRING("lran/bridge/config/state", topic);
}

void test_a_topic_that_would_be_truncated_is_refused() {
  char small[10];
  TEST_ASSERT_EQUAL_UINT32(0, topic_config("gatelink", "ack", small, sizeof(small)));
  TEST_ASSERT_EQUAL_STRING("", small);
}

// ---------------------------------------------------------------------------
// config/set - spec 16.7.2
// ---------------------------------------------------------------------------

void test_a_set_carries_its_names_and_values() {
  ConfigSetRequest req;
  const char*      error = nullptr;
  TEST_ASSERT_TRUE(parse("{\"set\": {\"poll_interval_s\": 120, \"dedup_cache_depth\": 8}}",
                         &req, &error));
  TEST_ASSERT_NULL(error);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConfigOp::Set), static_cast<uint8_t>(req.op));
  TEST_ASSERT_EQUAL_UINT8(2, req.count);
  TEST_ASSERT_EQUAL_STRING("poll_interval_s", req.entries[0].name);
  TEST_ASSERT_TRUE(req.entries[0].value_readable);
  TEST_ASSERT_EQUAL_INT32(120, req.entries[0].value);
  TEST_ASSERT_EQUAL_STRING("dedup_cache_depth", req.entries[1].name);
  TEST_ASSERT_EQUAL_INT32(8, req.entries[1].value);
}

void test_the_two_ops() {
  ConfigSetRequest req;
  TEST_ASSERT_TRUE(parse("{\"op\": \"restore_defaults\"}", &req, nullptr));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConfigOp::RestoreDefaults),
                          static_cast<uint8_t>(req.op));
  TEST_ASSERT_EQUAL_UINT8(0, req.count);

  TEST_ASSERT_TRUE(parse("{\"op\": \"get_all\"}", &req, nullptr));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConfigOp::GetAll), static_cast<uint8_t>(req.op));
}

void test_a_bool_arrives_as_one_and_zero() {
  ConfigSetRequest req;
  TEST_ASSERT_TRUE(parse("{\"set\":{\"simnode_diag_enable\":true}}", &req, nullptr));
  TEST_ASSERT_EQUAL_INT32(1, req.entries[0].value);
  TEST_ASSERT_TRUE(parse("{\"set\":{\"simnode_diag_enable\":false}}", &req, nullptr));
  TEST_ASSERT_EQUAL_INT32(0, req.entries[0].value);
}

void test_a_negative_value_survives() {
  // tx_power_dbm is i16 and its whole range is negative.
  ConfigSetRequest req;
  TEST_ASSERT_TRUE(parse("{\"set\":{\"tx_power_dbm\":-4}}", &req, nullptr));
  TEST_ASSERT_TRUE(req.entries[0].value_readable);
  TEST_ASSERT_EQUAL_INT32(-4, req.entries[0].value);
}

// THE DISTINCTION SPEC 16.7.2 DRAWS, and the reason the parser keeps a bad entry rather
// than dropping it: one mistyped value must not discard the rest of the set.
void test_an_unreadable_value_is_kept_and_the_rest_of_the_set_survives() {
  ConfigSetRequest req;
  const char*      error = nullptr;
  TEST_ASSERT_TRUE(parse("{\"set\":{\"poll_interval_s\":\"fast\",\"cad_retries\":5}}",
                         &req, &error));
  TEST_ASSERT_NULL(error);
  TEST_ASSERT_EQUAL_UINT8(2, req.count);
  TEST_ASSERT_EQUAL_STRING("poll_interval_s", req.entries[0].name);
  TEST_ASSERT_FALSE(req.entries[0].value_readable);
  TEST_ASSERT_EQUAL_STRING("cad_retries", req.entries[1].name);
  TEST_ASSERT_TRUE(req.entries[1].value_readable);
  TEST_ASSERT_EQUAL_INT32(5, req.entries[1].value);
}

void test_a_float_is_not_an_integer() {
  // 1.5 for a u8 must not apply as 1 and report ok.
  ConfigSetRequest req;
  TEST_ASSERT_TRUE(parse("{\"set\":{\"cad_retries\":1.5}}", &req, nullptr));
  TEST_ASSERT_EQUAL_UINT8(1, req.count);
  TEST_ASSERT_FALSE(req.entries[0].value_readable);
}

void test_a_value_past_int32_is_unreadable_rather_than_wrapped() {
  ConfigSetRequest req;
  TEST_ASSERT_TRUE(parse("{\"set\":{\"freq_hz\":99999999999}}", &req, nullptr));
  TEST_ASSERT_FALSE(req.entries[0].value_readable);
}

void test_an_object_or_array_value_is_skipped_whole() {
  ConfigSetRequest req;
  TEST_ASSERT_TRUE(parse("{\"set\":{\"a\":{\"b\":1},\"c\":[1,2],\"cad_retries\":3}}",
                         &req, nullptr));
  TEST_ASSERT_EQUAL_UINT8(3, req.count);
  TEST_ASSERT_FALSE(req.entries[0].value_readable);
  TEST_ASSERT_FALSE(req.entries[1].value_readable);
  TEST_ASSERT_TRUE(req.entries[2].value_readable);
  TEST_ASSERT_EQUAL_INT32(3, req.entries[2].value);
}

// ---------------------------------------------------------------------------
// The whole-payload refusals - spec 16.7.2's last paragraph.
// ---------------------------------------------------------------------------

void test_a_payload_that_is_not_an_object_is_refused_whole() {
  ConfigSetRequest req;
  const char*      error = nullptr;
  TEST_ASSERT_FALSE(parse("120", &req, &error));
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_FALSE(parse("[1,2]", &req, &error));
  TEST_ASSERT_FALSE(parse("", &req, &error));
  TEST_ASSERT_FALSE(parse("{", &req, &error));
}

void test_neither_key_and_both_keys_are_both_refused() {
  ConfigSetRequest req;
  const char*      error = nullptr;
  TEST_ASSERT_FALSE(parse("{}", &req, &error));
  TEST_ASSERT_NOT_NULL(std::strstr(error, "neither"));

  TEST_ASSERT_FALSE(parse("{\"set\":{\"cad_retries\":1},\"op\":\"get_all\"}", &req, &error));
  TEST_ASSERT_NOT_NULL(std::strstr(error, "both"));
}

void test_an_undefined_key_is_refused_rather_than_ignored() {
  // A payload carrying `sett` applies nothing and must say so, not succeed silently.
  ConfigSetRequest req;
  const char*      error = nullptr;
  TEST_ASSERT_FALSE(parse("{\"sett\":{\"cad_retries\":1}}", &req, &error));
  TEST_ASSERT_NOT_NULL(error);
}

void test_an_unknown_op_is_refused() {
  ConfigSetRequest req;
  const char*      error = nullptr;
  TEST_ASSERT_FALSE(parse("{\"op\":\"reboot\"}", &req, &error));
  TEST_ASSERT_NOT_NULL(std::strstr(error, "`op`"));
}

void test_an_empty_set_is_refused() {
  ConfigSetRequest req;
  const char*      error = nullptr;
  TEST_ASSERT_FALSE(parse("{\"set\":{}}", &req, &error));
  TEST_ASSERT_NOT_NULL(error);
}

void test_more_entries_than_the_bridge_accepts_is_refused_whole() {
  char payload[1024];
  int  n = std::snprintf(payload, sizeof(payload), "{\"set\":{");
  for (size_t i = 0; i <= kMaxConfigSetEntries; ++i) {
    n += std::snprintf(payload + n, sizeof(payload) - n, "%s\"p%u\":1",
                       i == 0 ? "" : ",", static_cast<unsigned>(i));
  }
  std::snprintf(payload + n, sizeof(payload) - n, "}}");

  ConfigSetRequest req;
  const char*      error = nullptr;
  TEST_ASSERT_FALSE(parse(payload, &req, &error));
  TEST_ASSERT_NOT_NULL(error);
}

void test_trailing_content_after_the_object_is_refused() {
  ConfigSetRequest req;
  TEST_ASSERT_FALSE(parse("{\"op\":\"get_all\"} junk", &req, nullptr));
}

void test_a_duplicate_key_is_refused() {
  ConfigSetRequest req;
  const char*      error = nullptr;
  TEST_ASSERT_FALSE(parse("{\"op\":\"get_all\",\"op\":\"get_all\"}", &req, &error));
  TEST_ASSERT_NOT_NULL(std::strstr(error, "twice"));
}

// ---------------------------------------------------------------------------
// config/ack - spec 16.7.3
// ---------------------------------------------------------------------------

void test_the_ack_carries_op_persist_and_one_result_per_name() {
  ConfigResult results[2] = {};
  std::snprintf(results[0].name, sizeof(results[0].name), "poll_interval_s");
  results[0].status    = ResultStatus::Ok;
  results[0].has_value = true;
  results[0].value     = 120;
  std::snprintf(results[1].name, sizeof(results[1].name), "dedup_cache_depth");
  results[1].status    = ResultStatus::Clamped;
  results[1].has_value = true;
  results[1].value     = 32;

  char doc[kMaxPayloadLen];
  TEST_ASSERT_TRUE(build_config_ack(ConfigOp::Set, AckPersist::AppliedNotPersisted,
                                    results, 2, nullptr, doc, sizeof(doc)) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"op\":\"set\",\"persist\":\"applied_not_persisted\","
      "\"results\":{\"poll_interval_s\":{\"status\":\"ok\",\"value\":120},"
      "\"dedup_cache_depth\":{\"status\":\"clamped\",\"value\":32}}}",
      doc);
}

void test_an_unknown_outcome_carries_a_null_value() {
  // Spec 16.7.3 - a CONFIG that drew no CONFIG_ACK is neither success nor failure, so
  // the value is not known and must not be reported as the requested one.
  ConfigResult r = {};
  std::snprintf(r.name, sizeof(r.name), "dedup_cache_depth");
  r.status    = ResultStatus::Unknown;
  r.has_value = false;

  char doc[kMaxPayloadLen];
  TEST_ASSERT_TRUE(build_config_ack(ConfigOp::Set, AckPersist::Unknown, &r, 1, nullptr,
                                    doc, sizeof(doc)) > 0);
  TEST_ASSERT_NOT_NULL(std::strstr(doc, "\"status\":\"unknown\",\"value\":null"));
  TEST_ASSERT_NOT_NULL(std::strstr(doc, "\"persist\":\"unknown\""));
}

void test_a_rejected_payload_carries_an_error_and_no_results() {
  // An empty `results` object would read as "every parameter was considered and none
  // had an outcome", which is a different claim from "nothing was read at all".
  char doc[kMaxPayloadLen];
  TEST_ASSERT_TRUE(build_config_ack(ConfigOp::Set, AckPersist::NotApplied, nullptr, 0,
                                    "payload is not a JSON object", doc, sizeof(doc)) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"op\":\"set\",\"persist\":\"not_applied\","
      "\"error\":\"payload is not a JSON object\"}",
      doc);
  TEST_ASSERT_NULL(std::strstr(doc, "results"));
}

void test_a_document_that_does_not_fit_reports_zero() {
  // json_writer refuses rather than truncates, and the caller drops the publication.
  ConfigResult r = {};
  std::snprintf(r.name, sizeof(r.name), "poll_interval_s");
  r.has_value = true;
  r.value     = 120;
  char small[20];
  TEST_ASSERT_EQUAL_UINT32(0, build_config_ack(ConfigOp::Set, AckPersist::Persisted, &r, 1,
                                               nullptr, small, sizeof(small)));
}

// ---------------------------------------------------------------------------
// Spec 16.7.3's combination rule
// ---------------------------------------------------------------------------

void test_unknown_wins_over_everything() {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckPersist::Unknown),
                          static_cast<uint8_t>(combine_persist(AckPersist::Persisted,
                                                               AckPersist::Unknown)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckPersist::Unknown),
                          static_cast<uint8_t>(combine_persist(AckPersist::Unknown,
                                                               AckPersist::NotApplied)));
}

void test_otherwise_the_less_persisted_half_wins() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(AckPersist::NotApplied),
      static_cast<uint8_t>(combine_persist(AckPersist::Persisted, AckPersist::NotApplied)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckPersist::AppliedNotPersisted),
                          static_cast<uint8_t>(combine_persist(
                              AckPersist::Persisted, AckPersist::AppliedNotPersisted)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(AckPersist::Persisted),
      static_cast<uint8_t>(combine_persist(AckPersist::Persisted, AckPersist::Persisted)));
}

// ---------------------------------------------------------------------------
// config/state - spec 16.7.4
// ---------------------------------------------------------------------------

void test_the_state_names_a_value_and_where_it_came_from() {
  ConfigStateEntry e[2] = {};
  std::snprintf(e[0].name, sizeof(e[0].name), "poll_interval_s");
  e[0].has_value   = true;
  e[0].value       = 120;
  e[0].is_override = true;
  std::snprintf(e[1].name, sizeof(e[1].name), "cad_retries");
  e[1].has_value = true;
  e[1].value     = 5;

  char doc[kMaxPayloadLen];
  TEST_ASSERT_TRUE(build_config_state(e, 2, doc, sizeof(doc)) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "{\"poll_interval_s\":{\"value\":120,\"source\":\"override\"},"
      "\"cad_retries\":{\"value\":5,\"source\":\"default\"}}",
      doc);
}

void test_a_value_never_read_back_is_null() {
  // Spec 16.7.4 - a different statement from a value that equals its default.
  ConfigStateEntry e = {};
  std::snprintf(e.name, sizeof(e.name), "dedup_cache_depth");
  e.has_value = false;

  char doc[kMaxPayloadLen];
  TEST_ASSERT_TRUE(build_config_state(&e, 1, doc, sizeof(doc)) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"dedup_cache_depth\":{\"value\":null,\"source\":\"default\"}}",
                           doc);
}

// ---------------------------------------------------------------------------
// THE PREMISE THIS WHOLE PATH RESTS ON, and the check that would falsify it.
//
// Every document above is published on one MQTT topic and capped at kMaxPayloadLen.
// A get_all answer carries one result per row of a whole table, so the cap is not
// obviously satisfied - it is satisfied because spec 16.7.1 splits the rows across two
// topics by owner. If a block grows past the cap, the publication is DROPPED and the
// entity keeps a stale value, which is exactly the silent failure this file exists to
// prevent. These two tests build the largest real document and say how much room is
// left, so the failure arrives here rather than at the broker.
// ---------------------------------------------------------------------------

namespace {

// A TABLE-DRIVEN ANSWER, which is what `get_all` and `restore_defaults` produce: one
// result per row of the topic's block, every status `ok`, because the rows come from the
// table rather than from a payload and a row cannot be unknown to its own table. The
// value is the widest any row can carry, freq_hz's nine digits with room for a sign.
//
// A SET-DRIVEN ANSWER IS THE OTHER SHAPE AND HAS ITS OWN TEST. Its statuses can be the
// long ones, and it is bounded by kMaxConfigSetEntries rather than by a table.
size_t ack_document_for(const config::ParamDef* rows, size_t n, char* doc, size_t cap) {
  ConfigResult results[config::kMaxTableParams] = {};
  for (size_t i = 0; i < n; ++i) {
    std::snprintf(results[i].name, sizeof(results[i].name), "%s", rows[i].name);
    results[i].status    = ResultStatus::Ok;
    results[i].has_value = true;
    results[i].value     = -917400000;
  }
  return build_config_ack(ConfigOp::GetAll, AckPersist::AppliedNotPersisted, results, n,
                          nullptr, doc, cap);
}

size_t state_document_for(const config::ParamDef* rows, size_t n, char* doc, size_t cap) {
  ConfigStateEntry entries[config::kMaxTableParams] = {};
  for (size_t i = 0; i < n; ++i) {
    std::snprintf(entries[i].name, sizeof(entries[i].name), "%s", rows[i].name);
    entries[i].has_value   = true;
    entries[i].value       = -917400000;
    entries[i].is_override = true;  // "override" is longer than "default"
  }
  return build_config_state(entries, n, doc, cap);
}

}  // namespace

namespace {

// The rows one topic carries, spec 16.7.1. `lran/bridge/config/*` carries the global
// rows ALONE - the per-node ones live on each node's topic - and getting that split
// wrong here would test a document the bridge never publishes.
size_t rows_for_owner(config::Owner owner, config::ParamDef* out) {
  size_t n = 0;
  for (size_t i = 0; i < config::kBridgeParamCount; ++i) {
    if (config::kBridgeParams[i].owner == owner) out[n++] = config::kBridgeParams[i];
  }
  return n;
}

}  // namespace

void test_the_bridges_own_block_fits_one_publication() {
  static config::ParamDef rows[config::kMaxTableParams];
  const size_t            n = rows_for_owner(config::Owner::BridgeGlobal, rows);
  TEST_ASSERT_TRUE(n > 0);

  static char  doc[kMaxPayloadLen];
  const size_t ack = ack_document_for(rows, n, doc, sizeof(doc));
  TEST_ASSERT_TRUE_MESSAGE(ack > 0, "the bridge's get_all answer no longer fits kMaxPayloadLen");
  const size_t state = state_document_for(rows, n, doc, sizeof(doc));
  TEST_ASSERT_TRUE_MESSAGE(state > 0, "lran/bridge/config/state no longer fits kMaxPayloadLen");
}

// The worst answer of all is not a table's - it is a `set` naming parameters the table
// does not hold, because each one costs the longest status name and its own length.
// kMaxConfigSetEntries is set from this bound; this is where the two meet.
void test_the_largest_refusable_set_still_fits_its_answer() {
  ConfigResult results[kMaxConfigSetEntries] = {};
  for (size_t i = 0; i < kMaxConfigSetEntries; ++i) {
    // One character short of the longest name this parser will accept.
    std::memset(results[i].name, 'x', kMaxParamNameLen - 1);
    results[i].name[0]                   = static_cast<char>('a' + i);  // distinct keys
    results[i].name[kMaxParamNameLen - 1] = '\0';
    results[i].status                     = ResultStatus::UnknownParam;
    results[i].has_value                  = false;
  }
  static char  doc[kMaxPayloadLen];
  const size_t n = build_config_ack(ConfigOp::Set, AckPersist::NotApplied, results,
                                    kMaxConfigSetEntries, nullptr, doc, sizeof(doc));
  TEST_ASSERT_TRUE_MESSAGE(n > 0, "kMaxConfigSetEntries is too large for one config/ack");
}

void test_a_nodes_block_fits_one_publication() {
  // A node's topic carries node-common AND the bridge's per-node rows for it (16.7.1),
  // so the worst case is both together rather than either alone.
  static config::ParamDef rows[config::kMaxTableParams];
  size_t                  n = 0;
  for (size_t i = 0; i < config::kNodeCommonParamCount; ++i) {
    rows[n++] = config::kNodeCommonParams[i];
  }
  for (size_t i = 0; i < config::kBridgeParamCount; ++i) {
    if (config::kBridgeParams[i].owner == config::Owner::BridgePerNode) {
      rows[n++] = config::kBridgeParams[i];
    }
  }

  static char  buf[kMaxPayloadLen];
  const size_t ack = ack_document_for(rows, n, buf, sizeof(buf));
  TEST_ASSERT_TRUE_MESSAGE(ack > 0, "a node's get_all answer no longer fits kMaxPayloadLen");
  const size_t state = state_document_for(rows, n, buf, sizeof(buf));
  TEST_ASSERT_TRUE_MESSAGE(state > 0, "a node's config/state no longer fits kMaxPayloadLen");
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_the_bridge_is_a_legitimate_config_target);
  RUN_TEST(test_a_node_config_topic_yields_its_address);
  RUN_TEST(test_anything_but_that_exact_shape_is_refused);
  RUN_TEST(test_the_published_config_topics);
  RUN_TEST(test_a_topic_that_would_be_truncated_is_refused);

  RUN_TEST(test_a_set_carries_its_names_and_values);
  RUN_TEST(test_the_two_ops);
  RUN_TEST(test_a_bool_arrives_as_one_and_zero);
  RUN_TEST(test_a_negative_value_survives);
  RUN_TEST(test_an_unreadable_value_is_kept_and_the_rest_of_the_set_survives);
  RUN_TEST(test_a_float_is_not_an_integer);
  RUN_TEST(test_a_value_past_int32_is_unreadable_rather_than_wrapped);
  RUN_TEST(test_an_object_or_array_value_is_skipped_whole);

  RUN_TEST(test_a_payload_that_is_not_an_object_is_refused_whole);
  RUN_TEST(test_neither_key_and_both_keys_are_both_refused);
  RUN_TEST(test_an_undefined_key_is_refused_rather_than_ignored);
  RUN_TEST(test_an_unknown_op_is_refused);
  RUN_TEST(test_an_empty_set_is_refused);
  RUN_TEST(test_more_entries_than_the_bridge_accepts_is_refused_whole);
  RUN_TEST(test_trailing_content_after_the_object_is_refused);
  RUN_TEST(test_a_duplicate_key_is_refused);

  RUN_TEST(test_the_ack_carries_op_persist_and_one_result_per_name);
  RUN_TEST(test_an_unknown_outcome_carries_a_null_value);
  RUN_TEST(test_a_rejected_payload_carries_an_error_and_no_results);
  RUN_TEST(test_a_document_that_does_not_fit_reports_zero);

  RUN_TEST(test_unknown_wins_over_everything);
  RUN_TEST(test_otherwise_the_less_persisted_half_wins);

  RUN_TEST(test_the_state_names_a_value_and_where_it_came_from);
  RUN_TEST(test_a_value_never_read_back_is_null);

  RUN_TEST(test_the_bridges_own_block_fits_one_publication);
  RUN_TEST(test_the_largest_refusable_set_still_fits_its_answer);
  RUN_TEST(test_a_nodes_block_fits_one_publication);

  return UNITY_END();
}
