// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-19 - the diagnostic documents (spec 14.1, 16.1, 16.2).
//
// THE PROPERTY UNDER TEST is that every spec 14.1 counter reaches the broker under its
// normative name, with rx_dropped as spec 14.1 sums it, and that nothing is ever published
// truncated or with a sentinel dressed as a number.
//
// WHAT THIS CANNOT COVER. lora_task's snapshot and the queue send (FreeRTOS), and a counter
// moved by a frame on air; BF-8's fault catalogue drives those on the bench (V-B B3 row).

#include <unity.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "diag_json.h"
#include "lran/counters.h"
#include "mqtt_transport.h"
#include "net_policy.h"
#include "registry.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

char g_buf[kMaxPayloadLen];

// The value of "key": in `json`, or -1 when the key is absent. -2 for null.
long long value_of(const char* json, const char* key) {
  char needle[64];
  std::snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char* p = std::strstr(json, needle);
  if (p == nullptr) return -1;
  p += std::strlen(needle);
  if (std::strncmp(p, "null", 4) == 0) return -2;
  return std::strtoll(p, nullptr, 10);
}

size_t count_of(const char* json, const char* key) {
  char needle[64];
  std::snprintf(needle, sizeof(needle), "\"%s\":", key);
  size_t n = 0;
  for (const char* p = std::strstr(json, needle); p != nullptr; p = std::strstr(p + 1, needle)) ++n;
  return n;
}

Counters every_counter_distinct() {
  Counters c;
  uint32_t v = 1;
  for (const CounterField& f : kCounterRegistry) c.*(f.field) = v++;
  c.rx_frames    = 1000;
  c.tx_frames    = 2000;
  c.cad_backoffs = 3000;
  return c;
}

}  // namespace

// spec 14.1 - every registry name, once, carrying its own counter.
void test_every_spec_14_1_counter_is_published_under_its_name() {
  const Counters c = every_counter_distinct();
  TEST_ASSERT_GREATER_THAN(0, diag_rx_json(c, g_buf, sizeof(g_buf)));
  for (const CounterField& f : kCounterRegistry) {
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, count_of(g_buf, f.name), f.name);
    TEST_ASSERT_EQUAL_INT64_MESSAGE(c.*(f.field), value_of(g_buf, f.name), f.name);
  }
  TEST_ASSERT_EQUAL_INT64(1000, value_of(g_buf, "rx_frames"));
  // BF-15a - rx_unknown_src is a registry row now, checked by the loop above, and the
  // separate bridge-local key is gone.
  TEST_ASSERT_EQUAL_INT64(-1, value_of(g_buf, "unregistered_src"));
  TEST_ASSERT_EQUAL_INT64(-1, value_of(g_buf, "tx_frames"));  // the radio document's
}

// The registry's order, so the document reads like spec 14.1's table.
void test_counters_appear_in_registry_order() {
  diag_rx_json(every_counter_distinct(), g_buf, sizeof(g_buf));
  const char* prev = g_buf;
  for (const CounterField& f : kCounterRegistry) {
    const char* p = std::strstr(g_buf, f.name);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE_MESSAGE(p >= prev, f.name);
    prev = p;
  }
}

// spec 14.1 - rx_dropped is the codec's sum, which leaves out the three marked no.
void test_rx_dropped_is_the_spec_14_1_sum() {
  const Counters c = every_counter_distinct();
  diag_rx_json(c, g_buf, sizeof(g_buf));
  TEST_ASSERT_EQUAL_INT64(c.total_dropped(), value_of(g_buf, "rx_dropped"));
  uint32_t all = 0;
  for (const CounterField& f : kCounterRegistry) all += c.*(f.field);
  TEST_ASSERT_TRUE(c.total_dropped() < all);
}

// The reason kMaxPayloadLen is 768. Every counter at UINT32_MAX still fits the queue.
void test_the_worst_case_documents_fit_a_queued_publication() {
  Counters c;
  for (const CounterField& f : kCounterRegistry) c.*(f.field) = UINT32_MAX;
  c.rx_frames = UINT32_MAX;
  const size_t rx = diag_rx_json(c, g_buf, sizeof(g_buf));
  TEST_ASSERT_GREATER_THAN(512, rx);  // would not have fitted before BF-19
  TEST_ASSERT_LESS_THAN(kMaxPayloadLen, rx);
  PublishMessage msg;
  TEST_ASSERT_TRUE(make_publish(&msg, "lran/bridge/diag/state", g_buf, true, 0));

  RadioDiag r;
  r.stats.begin_failures = r.stats.rx_driver_errors = r.stats.tx_forced = UINT32_MAX;
  r.stats.tx_errors = r.stats.tx_timeouts = r.stats.tx_dropped_no_radio = UINT32_MAX;
  r.stats.cad_errors = r.stats.cad_deferred = UINT32_MAX;
  r.stats.last_begin_status = INT16_MIN;
  r.tx_frames = r.cad_backoffs = UINT32_MAX;
  for (QueueStat& q : r.queues) q.dropped = q.high_water = UINT32_MAX;
  const size_t radio = diag_radio_json(r, g_buf, sizeof(g_buf));
  TEST_ASSERT_GREATER_THAN(0, radio);
  TEST_ASSERT_LESS_THAN(kMaxPayloadLen, radio);

  NodeState s;
  s.heard = true;
  s.rssi_dbm = -32767;
  s.snr_db = -127;
  s.missed_polls = UINT16_MAX;
  s.proto_ver = 254;
  TEST_ASSERT_GREATER_THAN(0, diag_node_json(s, UINT32_MAX, g_buf, sizeof(g_buf)));
}

// Refused, never truncated.
void test_a_document_that_does_not_fit_is_refused_whole() {
  char small[64];
  std::memset(small, 'x', sizeof(small));
  TEST_ASSERT_EQUAL_UINT(0, diag_rx_json(Counters{}, small, sizeof(small)));
  TEST_ASSERT_EQUAL_STRING("", small);
  TEST_ASSERT_EQUAL_UINT(0, diag_rx_json(Counters{}, nullptr, 100));
  TEST_ASSERT_EQUAL_UINT(0, diag_rx_json(Counters{}, small, 0));
}

void test_the_radio_document_carries_the_driver_and_queue_numbers() {
  RadioDiag r;
  r.tx_frames                 = 11;
  r.cad_backoffs              = 12;
  r.stats.last_begin_status   = -2;
  r.stats.tx_forced           = 3;
  r.queues[static_cast<size_t>(QueueId::Publish)].dropped    = 4;
  r.queues[static_cast<size_t>(QueueId::Rx)].high_water      = 5;
  TEST_ASSERT_GREATER_THAN(0, diag_radio_json(r, g_buf, sizeof(g_buf)));
  TEST_ASSERT_EQUAL_INT64(11, value_of(g_buf, "tx_frames"));
  TEST_ASSERT_EQUAL_INT64(12, value_of(g_buf, "cad_backoffs"));
  TEST_ASSERT_EQUAL_INT64(-2, value_of(g_buf, "last_begin_status"));
  TEST_ASSERT_EQUAL_INT64(3, value_of(g_buf, "tx_forced"));
  TEST_ASSERT_EQUAL_INT64(4, value_of(g_buf, "q_publish_dropped"));
  TEST_ASSERT_EQUAL_INT64(5, value_of(g_buf, "q_rx_high_water"));
  TEST_ASSERT_EQUAL_INT64(0, value_of(g_buf, "q_log_dropped"));
}

// Root rule 6 - a node never heard is all null, not a row of zeros.
void test_a_node_not_yet_heard_publishes_null_not_zero() {
  NodeState s;
  TEST_ASSERT_GREATER_THAN(0, diag_node_json(s, 5000, g_buf, sizeof(g_buf)));
  TEST_ASSERT_EQUAL_INT64(-2, value_of(g_buf, "rssi_dbm"));
  TEST_ASSERT_EQUAL_INT64(-2, value_of(g_buf, "snr_db"));
  TEST_ASSERT_EQUAL_INT64(-2, value_of(g_buf, "last_seen_s"));
  TEST_ASSERT_EQUAL_INT64(-2, value_of(g_buf, "proto_ver"));
  TEST_ASSERT_EQUAL_INT64(0, value_of(g_buf, "missed_polls"));
}

void test_a_heard_node_publishes_its_link() {
  NodeState s;
  s.heard        = true;
  s.last_seen_ms = 1000;
  s.rssi_dbm     = -97;
  s.snr_db       = -3;
  s.proto_ver    = 2;
  s.missed_polls = 1;
  diag_node_json(s, 43999, g_buf, sizeof(g_buf));
  TEST_ASSERT_EQUAL_INT64(-97, value_of(g_buf, "rssi_dbm"));
  TEST_ASSERT_EQUAL_INT64(-3, value_of(g_buf, "snr_db"));
  TEST_ASSERT_EQUAL_INT64(42, value_of(g_buf, "last_seen_s"));
  TEST_ASSERT_EQUAL_INT64(2, value_of(g_buf, "proto_ver"));
  TEST_ASSERT_EQUAL_INT64(1, value_of(g_buf, "missed_polls"));
}

// millis() wraps at ~49.7 days; the age must not jump to 49 days when it does.
void test_last_seen_survives_the_millis_wrap() {
  NodeState s;
  s.heard        = true;
  s.last_seen_ms = UINT32_MAX - 1999;
  diag_node_json(s, 3000, g_buf, sizeof(g_buf));
  TEST_ASSERT_EQUAL_INT64(5, value_of(g_buf, "last_seen_s"));
}

void test_diag_topics_follow_spec_16_1() {
  char t[kMaxTopicLen];
  TEST_ASSERT_GREATER_THAN(0, topic_diag("bridge", nullptr, t, sizeof(t)));
  TEST_ASSERT_EQUAL_STRING("lran/bridge/diag/state", t);
  topic_diag("bridge", "radio", t, sizeof(t));
  TEST_ASSERT_EQUAL_STRING("lran/bridge/diag/radio/state", t);
  topic_diag("gatelink", nullptr, t, sizeof(t));
  TEST_ASSERT_EQUAL_STRING("lran/gatelink/diag/state", t);

  TEST_ASSERT_EQUAL_UINT(0, topic_diag("", nullptr, t, sizeof(t)));
  TEST_ASSERT_EQUAL_UINT(0, topic_diag("bridge", "", t, sizeof(t)));
  TEST_ASSERT_EQUAL_UINT(0, topic_diag("bridge", nullptr, t, 10));
  TEST_ASSERT_EQUAL_STRING("", t);
}

// BF-18. The keys are what Home Assistant charts, so they are asserted rather than
// trusted, like every other counter name this file guards.
void test_the_command_diagnostics_carry_every_stat() {
  CommandStats s;
  s.submitted = 1; s.refused_busy = 2; s.sent = 3; s.retries = 4; s.acked = 5;
  s.no_ack = 6; s.resyncs = 7; s.resync_failed = 8; s.ack_ignored = 9;

  char out[kMaxPayloadLen];
  TEST_ASSERT_TRUE(diag_command_json(s, out, sizeof(out)) > 0);
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd_submitted\":1"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd_refused_busy\":2"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd_sent\":3"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd_retries\":4"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd_acked\":5"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd_no_ack\":6"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd_resyncs\":7"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd_resync_failed\":8"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"cmd_ack_ignored\":9"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_command_diagnostics_carry_every_stat);
  RUN_TEST(test_every_spec_14_1_counter_is_published_under_its_name);
  RUN_TEST(test_counters_appear_in_registry_order);
  RUN_TEST(test_rx_dropped_is_the_spec_14_1_sum);
  RUN_TEST(test_the_worst_case_documents_fit_a_queued_publication);
  RUN_TEST(test_a_document_that_does_not_fit_is_refused_whole);
  RUN_TEST(test_the_radio_document_carries_the_driver_and_queue_numbers);
  RUN_TEST(test_a_node_not_yet_heard_publishes_null_not_zero);
  RUN_TEST(test_a_heard_node_publishes_its_link);
  RUN_TEST(test_last_seen_survives_the_millis_wrap);
  RUN_TEST(test_diag_topics_follow_spec_16_1);
  return UNITY_END();
}
