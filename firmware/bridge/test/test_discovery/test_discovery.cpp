// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-23 - the discovery configs, and the three requirements they carry.
//
// WHAT THIS CANNOT COVER. That Home Assistant accepts these documents, that the entity
// appears, or that its value template resolves against a real payload. Only HA shows
// that, and the dev VM is where it is shown (Impl Plan 11.3). What is under test is what
// can be got wrong at a desk and is silent at the broker: a unique_id that collides, an
// availability topic pointing at the bridge instead of the node, a bench node reaching
// the registry, a button for a command its node type does not implement, and a document
// that did not fit being published truncated rather than dropped.

#include <unity.h>

#include <cstdio>
#include <cstring>

#include "command.h"
#include "discovery.h"
#include "mqtt_transport.h"
#include "net_policy.h"
#include "registry.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

char g_topic[128];
char g_buf[kMaxDiscoveryPayload];

// The provisioned set, as registry.cpp derives it from kNodeTable.
struct Fleet {
  NodeInfo nodes[kNodeCount];
  Fleet() {
    for (size_t i = 0; i < kNodeCount; ++i) {
      nodes[i].id       = kNodeTable[i].id;
      nodes[i].type     = kNodeTable[i].type;
      nodes[i].is_bench = is_bench_node(kNodeTable[i].id);
    }
  }
};

// Every item the cursor yields, collected so a test can ask questions of the whole set.
struct Walk {
  static constexpr size_t kMax = 128;
  DiscoveryItem items[kMax];
  char          topics[kMax][128];
  char          configs[kMax][kMaxDiscoveryPayload];
  size_t        n = 0;

  void run(const Fleet& f, bool bench_enabled) {
    DiscoveryCursor cur;
    DiscoveryItem   item;
    n = 0;
    while (n < kMax && discovery_next(&cur, f.nodes, kNodeCount, bench_enabled, &item)) {
      items[n] = item;
      TEST_ASSERT_GREATER_THAN(0, discovery_topic(item, topics[n], sizeof(topics[n])));
      TEST_ASSERT_GREATER_THAN(0,
                               discovery_config_json(item, configs[n], sizeof(configs[n])));
      ++n;
    }
  }
};

// The value of a string key, or nullptr. Small and literal rather than a parser: these
// documents are built by one writer whose output shape is known.
const char* value_of(const char* json, const char* key, char* out, size_t cap) {
  char pattern[64];
  std::snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  const char* p = std::strstr(json, pattern);
  if (p == nullptr) return nullptr;
  p += std::strlen(pattern);
  const char* end = std::strchr(p, '"');
  if (end == nullptr || static_cast<size_t>(end - p) >= cap) return nullptr;
  std::memcpy(out, p, static_cast<size_t>(end - p));
  out[end - p] = '\0';
  return out;
}

bool has_key(const char* json, const char* key) {
  char pattern[64];
  std::snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  return std::strstr(json, pattern) != nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
// R-3.3c - a unique_id is prefixed per node, stable, and does not collide.
// ---------------------------------------------------------------------------

void test_every_unique_id_across_the_whole_fleet_is_distinct() {
  // The requirement's actual claim, tested as stated rather than per node. A suffix
  // reused between the bridge and a node would collide only when both are published,
  // which is every boot.
  Fleet f;
  Walk  w;
  w.run(f, false);
  TEST_ASSERT_GREATER_THAN(0, w.n);
  char a[80], b[80];
  for (size_t i = 0; i < w.n; ++i) {
    TEST_ASSERT_NOT_NULL(value_of(w.configs[i], "uniq_id", a, sizeof(a)));
    for (size_t k = i + 1; k < w.n; ++k) {
      TEST_ASSERT_NOT_NULL(value_of(w.configs[k], "uniq_id", b, sizeof(b)));
      TEST_ASSERT_FALSE_MESSAGE(std::strcmp(a, b) == 0, a);
    }
  }
}

void test_a_unique_id_carries_its_node_prefix() {
  Fleet f;
  Walk  w;
  w.run(f, false);
  char id[80];
  bool seen = false;
  for (size_t i = 0; i < w.n; ++i) {
    if (w.items[i].node_id != kNodeGateLink) continue;
    TEST_ASSERT_NOT_NULL(value_of(w.configs[i], "uniq_id", id, sizeof(id)));
    TEST_ASSERT_EQUAL_STRING_LEN("lran_gatelink_", id, 14);
    seen = true;
  }
  TEST_ASSERT_TRUE(seen);
}

void test_the_discovery_topic_matches_the_unique_id() {
  // HA keys the entity on unique_id and the config's location on the topic. Letting the
  // two drift produces a config that publishes to one place and registers another.
  Fleet f;
  Walk  w;
  w.run(f, false);
  char id[80];
  for (size_t i = 0; i < w.n; ++i) {
    TEST_ASSERT_NOT_NULL(value_of(w.configs[i], "uniq_id", id, sizeof(id)));
    TEST_ASSERT_NOT_NULL(std::strstr(w.topics[i], id));
    TEST_ASSERT_EQUAL_STRING_LEN("homeassistant/", w.topics[i], 14);
    const size_t len = std::strlen(w.topics[i]);
    TEST_ASSERT_EQUAL_STRING("/config", w.topics[i] + len - 7);
  }
}

// ---------------------------------------------------------------------------
// R-3.3d - a node entity's availability is that node's, never the bridge's LWT.
// ---------------------------------------------------------------------------

void test_a_node_entity_points_at_its_own_availability_topic() {
  // THE REQUIREMENT'S WHOLE POINT: a node that has gone offline must read unavailable
  // while the bridge is still connected and publishing. Pointing at the bridge's LWT
  // would leave a dead gate's entities showing their last value indefinitely.
  Fleet f;
  Walk  w;
  w.run(f, false);
  char base[80], avty[80];
  for (size_t i = 0; i < w.n; ++i) {
    TEST_ASSERT_NOT_NULL(value_of(w.configs[i], "~", base, sizeof(base)));
    TEST_ASSERT_NOT_NULL(value_of(w.configs[i], "avty_t", avty, sizeof(avty)));
    // Relative to the device's own base topic, so it resolves to that node's.
    TEST_ASSERT_EQUAL_STRING("~/availability", avty);
    if (w.items[i].node_id == kNodeGateLink) {
      TEST_ASSERT_EQUAL_STRING("lran/gatelink", base);
    }
  }
}

void test_the_availability_payloads_are_the_tokens_the_bridge_publishes() {
  // These two strings are spec 16.5's, and node_availability.cpp publishes them. An
  // entity that expects different words stays unavailable forever with nothing logged.
  Fleet f;
  Walk  w;
  w.run(f, false);
  char on[32], off[32];
  TEST_ASSERT_NOT_NULL(value_of(w.configs[0], "pl_avail", on, sizeof(on)));
  TEST_ASSERT_NOT_NULL(value_of(w.configs[0], "pl_not_avail", off, sizeof(off)));
  TEST_ASSERT_EQUAL_STRING(kPayloadOnline, on);
  TEST_ASSERT_EQUAL_STRING(kPayloadOffline, off);
}

// ---------------------------------------------------------------------------
// Spec 16.6 - a bench node is gated at publication, and discovery is publication.
// ---------------------------------------------------------------------------

void test_a_bench_node_produces_no_discovery_until_the_toggle_exists() {
  // HA's registry remembers a unique_id forever and a retained config survives a
  // reflash, so four simnode devices whose entities can never update is a cost paid
  // once and kept. BF-26 owns the toggle.
  Fleet f;
  Walk  w;
  w.run(f, false);
  for (size_t i = 0; i < w.n; ++i) {
    TEST_ASSERT_FALSE(is_bench_node(w.items[i].node_id));
  }
}

void test_the_toggle_admits_them_without_any_other_change() {
  Fleet f;
  Walk  off, on;
  off.run(f, false);
  on.run(f, true);
  TEST_ASSERT_GREATER_THAN(off.n, on.n);
  bool bench = false;
  for (size_t i = 0; i < on.n; ++i) {
    if (is_bench_node(on.items[i].node_id)) bench = true;
  }
  TEST_ASSERT_TRUE(bench);
}

// ---------------------------------------------------------------------------
// BG-2 - the capability filter decides the buttons, not a table per node type.
// ---------------------------------------------------------------------------

void test_a_button_exists_only_where_the_command_path_would_send_it() {
  Fleet f;
  Walk  w;
  w.run(f, true);
  for (size_t i = 0; i < w.n; ++i) {
    if (w.items[i].desc->command_suffix == nullptr) continue;
    NodeType type = NodeType::GateLink;
    for (size_t k = 0; k < kNodeCount; ++k) {
      if (f.nodes[k].id == w.items[i].node_id) type = f.nodes[k].type;
    }
    TEST_ASSERT_TRUE(command_allowed(type, w.items[i].desc->cmd));
  }
}

void test_welllink_gets_no_gate_buttons() {
  // The check that BG-2 is real: WellLink has no relays, command_allowed() says so, and
  // discovery never learns what a gate is.
  Fleet f;
  Walk  w;
  w.run(f, false);
  for (size_t i = 0; i < w.n; ++i) {
    if (w.items[i].node_id != kNodeWellLink) continue;
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(Cmd::Open), w.items[i].desc->cmd);
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(Cmd::HoldOpen), w.items[i].desc->cmd);
  }
}

void test_reboot_has_no_button_anywhere() {
  // Spec 8.1 guards REBOOT with 0xA5 in `arg` so it cannot be issued by accident, and a
  // dashboard button is that accident. It stays reachable from the topic.
  Fleet f;
  Walk  w;
  w.run(f, true);
  for (size_t i = 0; i < w.n; ++i) {
    TEST_ASSERT_NOT_EQUAL(static_cast<uint8_t>(Cmd::Reboot), w.items[i].desc->cmd);
  }
}

void test_a_button_publishes_to_a_topic_the_bridge_parses() {
  // The failure this prevents is silent at both ends: HA publishes happily and the
  // bridge's subscription never matches. Built here and fed to the real parser.
  Fleet f;
  Walk  w;
  w.run(f, false);
  char base[80], cmd[80];
  size_t buttons = 0;
  for (size_t i = 0; i < w.n; ++i) {
    if (w.items[i].desc->command_suffix == nullptr) continue;
    ++buttons;
    TEST_ASSERT_NOT_NULL(value_of(w.configs[i], "~", base, sizeof(base)));
    TEST_ASSERT_NOT_NULL(value_of(w.configs[i], "cmd_t", cmd, sizeof(cmd)));
    char full[160];
    std::snprintf(full, sizeof(full), "%s%s", base, cmd + 1);  // `~` expanded
    CmdTopic parsed;
    TEST_ASSERT_TRUE_MESSAGE(parse_cmd_topic(full, &parsed), full);
    TEST_ASSERT_EQUAL_HEX8(w.items[i].node_id, parsed.node_id);
    TEST_ASSERT_EQUAL_HEX8(w.items[i].desc->cmd, parsed.cmd);
  }
  TEST_ASSERT_GREATER_THAN(0, buttons);
}

void test_a_button_press_payload_is_one_the_bridge_accepts() {
  Fleet f;
  Walk  w;
  w.run(f, false);
  char press[32];
  for (size_t i = 0; i < w.n; ++i) {
    if (w.items[i].desc->command_suffix == nullptr) continue;
    TEST_ASSERT_NOT_NULL(value_of(w.configs[i], "payload_press", press, sizeof(press)));
    uint8_t  arg  = 0xFF;
    uint16_t arg2 = 0xFFFF;
    TEST_ASSERT_TRUE(parse_cmd_payload(press, std::strlen(press), &arg, &arg2));
    TEST_ASSERT_EQUAL_UINT8(0, arg);
    TEST_ASSERT_EQUAL_UINT16(0, arg2);
  }
}

// ---------------------------------------------------------------------------
// The documents themselves.
// ---------------------------------------------------------------------------

void test_a_value_template_reads_the_key_and_nothing_more() {
  // A default or a filter would turn an absent reading into a number, which spec
  // 16.2.1's null exists to prevent.
  Fleet f;
  Walk  w;
  w.run(f, false);
  char tmpl[80];
  bool seen = false;
  for (size_t i = 0; i < w.n; ++i) {
    if (w.items[i].node_id != kNodeGateLink) continue;
    if (std::strcmp(w.items[i].desc->object_id, "rssi") != 0) continue;
    TEST_ASSERT_NOT_NULL(value_of(w.configs[i], "val_tpl", tmpl, sizeof(tmpl)));
    TEST_ASSERT_EQUAL_STRING("{{ value_json.rssi_dbm }}", tmpl);
    seen = true;
  }
  TEST_ASSERT_TRUE(seen);
}

void test_an_unset_field_is_omitted_rather_than_written_null() {
  // HA reads an absent key as "use the default" and an explicit null as a value. A
  // button has no unit, no device class and no state class.
  Fleet f;
  Walk  w;
  w.run(f, false);
  for (size_t i = 0; i < w.n; ++i) {
    if (w.items[i].desc->command_suffix == nullptr) continue;
    TEST_ASSERT_FALSE(has_key(w.configs[i], "unit_of_meas"));
    TEST_ASSERT_FALSE(has_key(w.configs[i], "stat_cla"));
    TEST_ASSERT_FALSE(has_key(w.configs[i], "stat_t"));
    return;
  }
  TEST_FAIL_MESSAGE("no button in the set");
}

void test_every_node_sits_under_the_bridge_in_the_device_tree() {
  Fleet f;
  Walk  w;
  w.run(f, false);
  for (size_t i = 0; i < w.n; ++i) {
    const bool is_bridge = w.items[i].node_id == kNodeBridge;
    TEST_ASSERT_EQUAL(!is_bridge, has_key(w.configs[i], "via_device"));
  }
}

void test_the_bridge_device_keeps_the_name_home_assistant_already_knows() {
  // D17's amendment retired `LoRaBridge` in the documents and kept "LoRa Bridge" as the
  // HA device name on purpose. Renaming a device HA has registered is not free.
  TEST_ASSERT_EQUAL_STRING("LoRa Bridge", device_name(kNodeBridge));
}

void test_every_config_fits_a_publish_message() {
  // kMaxPayloadLen is 768 and growing it costs RAM in every publish queue slot. A config
  // that outgrew it would be refused at the transport, so this is the earlier warning.
  Fleet f;
  Walk  w;
  w.run(f, true);
  for (size_t i = 0; i < w.n; ++i) {
    TEST_ASSERT_LESS_THAN(kMaxDiscoveryPayload, std::strlen(w.configs[i]));
  }
}

void test_a_topic_fits_the_transport_s_own_limit() {
  Fleet f;
  Walk  w;
  w.run(f, true);
  for (size_t i = 0; i < w.n; ++i) {
    TEST_ASSERT_LESS_THAN(kMaxTopicLen, std::strlen(w.topics[i]));
  }
}

void test_a_document_that_does_not_fit_is_refused_whole() {
  // Truncated JSON is worse than absent: HA logs a parse error against a topic that
  // looks alive while the entity keeps a stale value.
  Fleet          f;
  DiscoveryCursor cur;
  DiscoveryItem  item;
  TEST_ASSERT_TRUE(discovery_next(&cur, f.nodes, kNodeCount, false, &item));
  char small[64];
  std::memset(small, 'x', sizeof(small));
  TEST_ASSERT_EQUAL_UINT(0, discovery_config_json(item, small, sizeof(small)));
  TEST_ASSERT_EQUAL_STRING("", small);
  TEST_ASSERT_EQUAL_UINT(0, discovery_topic(item, g_topic, 4));
  TEST_ASSERT_EQUAL_STRING("", g_topic);
}

void test_an_invalid_item_produces_nothing() {
  DiscoveryItem item;  // valid = false
  TEST_ASSERT_EQUAL_UINT(0, discovery_config_json(item, g_buf, sizeof(g_buf)));
  TEST_ASSERT_EQUAL_UINT(0, discovery_topic(item, g_topic, sizeof(g_topic)));
}

// ---------------------------------------------------------------------------
// The cursor.
// ---------------------------------------------------------------------------

void test_the_walk_ends_and_stays_ended() {
  // R-3.3b republishes the whole set on every reconnect, so the cursor is restarted
  // often and a cursor that never ends is a publish queue that never drains.
  Fleet f;
  DiscoveryCursor cur;
  DiscoveryItem   item;
  size_t          n = 0;
  while (discovery_next(&cur, f.nodes, kNodeCount, true, &item) && n < 512) ++n;
  TEST_ASSERT_GREATER_THAN(0, n);
  TEST_ASSERT_LESS_THAN(512, n);
  TEST_ASSERT_FALSE(discovery_next(&cur, f.nodes, kNodeCount, true, &item));
  TEST_ASSERT_FALSE(discovery_next(&cur, f.nodes, kNodeCount, true, &item));
}

void test_the_same_walk_twice_gives_the_same_set() {
  // The reconnect path publishes the identical set, so a retained config is overwritten
  // by its equal rather than by a variant.
  Fleet f;
  Walk  a, b;
  a.run(f, false);
  b.run(f, false);
  TEST_ASSERT_EQUAL_UINT(a.n, b.n);
  for (size_t i = 0; i < a.n; ++i) {
    TEST_ASSERT_EQUAL_STRING(a.topics[i], b.topics[i]);
    TEST_ASSERT_EQUAL_STRING(a.configs[i], b.configs[i]);
  }
}

void test_the_bridge_s_own_entities_are_in_the_set() {
  Fleet f;
  Walk  w;
  w.run(f, false);
  size_t bridge_entities = 0;
  for (size_t i = 0; i < w.n; ++i) {
    if (w.items[i].node_id == kNodeBridge) ++bridge_entities;
  }
  TEST_ASSERT_GREATER_THAN(0, bridge_entities);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_unique_id_across_the_whole_fleet_is_distinct);
  RUN_TEST(test_a_unique_id_carries_its_node_prefix);
  RUN_TEST(test_the_discovery_topic_matches_the_unique_id);
  RUN_TEST(test_a_node_entity_points_at_its_own_availability_topic);
  RUN_TEST(test_the_availability_payloads_are_the_tokens_the_bridge_publishes);
  RUN_TEST(test_a_bench_node_produces_no_discovery_until_the_toggle_exists);
  RUN_TEST(test_the_toggle_admits_them_without_any_other_change);
  RUN_TEST(test_a_button_exists_only_where_the_command_path_would_send_it);
  RUN_TEST(test_welllink_gets_no_gate_buttons);
  RUN_TEST(test_reboot_has_no_button_anywhere);
  RUN_TEST(test_a_button_publishes_to_a_topic_the_bridge_parses);
  RUN_TEST(test_a_button_press_payload_is_one_the_bridge_accepts);
  RUN_TEST(test_a_value_template_reads_the_key_and_nothing_more);
  RUN_TEST(test_an_unset_field_is_omitted_rather_than_written_null);
  RUN_TEST(test_every_node_sits_under_the_bridge_in_the_device_tree);
  RUN_TEST(test_the_bridge_device_keeps_the_name_home_assistant_already_knows);
  RUN_TEST(test_every_config_fits_a_publish_message);
  RUN_TEST(test_a_topic_fits_the_transport_s_own_limit);
  RUN_TEST(test_a_document_that_does_not_fit_is_refused_whole);
  RUN_TEST(test_an_invalid_item_produces_nothing);
  RUN_TEST(test_the_walk_ends_and_stays_ended);
  RUN_TEST(test_the_same_walk_twice_gives_the_same_set);
  RUN_TEST(test_the_bridge_s_own_entities_are_in_the_set);
  return UNITY_END();
}
