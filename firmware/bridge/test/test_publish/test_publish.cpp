// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-24 - the publication policy (Impl Plan 6.3, V-B7).
//
// THE PROPERTIES UNDER TEST are V-B7's four and spec 16.6's gate: jitter does not republish,
// a stale block is unavailable and its last value is not republished, a sentinel is null
// and never a number, a synthetic frame stays marked, and a bench node reaches no production
// topic whichever way simnode_diag_enable is set.
//
// WHAT THIS CANNOT COVER. The queue send and SNTP (FreeRTOS and lwIP), and Home Assistant's
// reading of the documents. No GateLink exists to send a real 0x10, so these frames are
// built by the library's own serializer.

#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "lran/schema/gatelink_status_v1.h"
#include "lran/schema/node_health_v1.h"
#include "mqtt_transport.h"
#include "net_policy.h"
#include "publish.h"
#include "registry.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

// 2026-09-23T12:00:00Z
constexpr UtcSeconds kNow = 1790164800;

struct Recorder final : PublishSink {
  struct Item {
    char topic[kMaxTopicLen];
    char payload[kMaxPayloadLen];
  };
  Item   items[16];
  size_t n       = 0;
  bool   refuse  = false;

  bool emit(const char* topic, const char* payload, bool retain) override {
    TEST_ASSERT_TRUE(retain);  // spec 16.2 - every <domain>/state is retained
    if (refuse) return false;
    // The sink app_task uses goes through make_publish(), so the tests do too: a document
    // it would refuse must fail here rather than at the broker.
    PublishMessage m;
    TEST_ASSERT_TRUE_MESSAGE(make_publish(&m, topic, payload, retain, 0), topic);
    TEST_ASSERT_TRUE(n < 16);
    std::snprintf(items[n].topic, sizeof(items[n].topic), "%s", topic);
    std::snprintf(items[n].payload, sizeof(items[n].payload), "%s", payload);
    ++n;
    return true;
  }

  const char* find(const char* topic) const {
    for (size_t i = 0; i < n; ++i) {
      if (std::strcmp(items[i].topic, topic) == 0) return items[i].payload;
    }
    return nullptr;
  }
  void clear() { n = 0; }
};

NodeInfo gatelink() { return NodeInfo{kNodeGateLink, NodeType::GateLink, false}; }
NodeInfo simnode() { return NodeInfo{kNodeSim1, NodeType::Simnode, true}; }

schema::GateLinkStatusV1 healthy() {
  schema::GateLinkStatusV1 s;
  s.gate_state           = static_cast<uint8_t>(GateState::Closed);
  s.last_traversal_age_s = 600;
  s.batt_mv              = 13250;
  s.batt_ma              = -120;
  s.pv_cv                = 1834;
  s.yield_total          = 123456;
  s.mppt_temp_c10        = -5;
  s.bms_soc              = 87;
  s.bms_flags            = 0x01 | 0x02 | 0x04;
  s.pack_mv              = 13240;
  s.cell_count           = 4;
  s.cell_mv[0] = 3310;
  s.cell_mv[1] = 3311;
  s.cell_mv[2] = 3309;
  s.cell_mv[3] = 3310;
  s.bms_capacity_dah = 1000;
  s.bms_rssi_neg     = 71;
  s.bms_age_s        = 30;
  s.uptime_s         = 3600;
  s.boot_count       = 7;
  s.node_flags       = 0x01 | 0x02;
  return s;
}

Header status_hdr(NodeId src, SchemaId schema) {
  Header h;
  h.type   = MsgType::Status;
  h.src    = src;
  h.schema = schema;
  return h;
}

void offer(PublicationPolicy& p, const NodeInfo& info, const schema::GateLinkStatusV1& s,
           uint32_t now_ms, Recorder& r, UtcSeconds utc = kNow,
           SchemaId schema = kSchemaGateLinkStatusV1) {
  uint8_t buf[schema::kGateLinkStatusV1Len];
  size_t  n = 0;
  TEST_ASSERT_EQUAL(Status::Ok, schema::serialize(s, buf, sizeof(buf), &n));
  p.on_status(info, status_hdr(info.id, schema), buf, n, now_ms, utc, r);
}

// The text after "key": up to the next comma or brace.
const char* raw_of(const char* json, const char* key, char* out, size_t cap) {
  char needle[48];
  std::snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char* p = json == nullptr ? nullptr : std::strstr(json, needle);
  if (p == nullptr) {
    std::snprintf(out, cap, "<absent>");
    return out;
  }
  p += std::strlen(needle);
  size_t i = 0;
  while (p[i] != '\0' && p[i] != ',' && p[i] != '}' && i + 1 < cap) {
    out[i] = p[i];
    ++i;
  }
  out[i] = '\0';
  return out;
}

void expect(const char* json, const char* key, const char* want) {
  char got[64];
  TEST_ASSERT_EQUAL_STRING_MESSAGE(want, raw_of(json, key, got, sizeof(got)), key);
}

}  // namespace

// ---------------------------------------------------------------------------

void test_first_frame_publishes_five_documents() {
  PublicationPolicy p;
  Recorder          r;
  offer(p, gatelink(), healthy(), 1000, r);
  TEST_ASSERT_EQUAL(5, r.n);
  TEST_ASSERT_NOT_NULL(r.find("lran/gatelink/gate/state"));
  TEST_ASSERT_NOT_NULL(r.find("lran/gatelink/detect/state"));
  TEST_ASSERT_NOT_NULL(r.find("lran/gatelink/solar/state"));
  TEST_ASSERT_NOT_NULL(r.find("lran/gatelink/battery/state"));
  TEST_ASSERT_NOT_NULL(r.find("lran/gatelink/node/state"));
  TEST_ASSERT_EQUAL(5, p.stats().documents);
}

void test_units_are_converted_exactly() {
  PublicationPolicy p;
  Recorder          r;
  offer(p, gatelink(), healthy(), 1000, r);
  const char* solar = r.find("lran/gatelink/solar/state");
  expect(solar, "pv_mv", "18340");
  expect(solar, "yield_total_kwh", "1234.56");
  expect(solar, "temp_c", "-0.5");
  expect(solar, "batt_ma", "-120");
  const char* bat = r.find("lran/gatelink/battery/state");
  expect(bat, "capacity_ah", "100.0");
  expect(bat, "ble_rssi_dbm", "-71");
  expect(r.find("lran/gatelink/gate/state"), "state", "\"closed\"");
}

// R-5.2a - cell jitter inside the deadband leaves every document identical, so nothing is
// republished until the heartbeat.
void test_cell_jitter_is_not_republished() {
  PublicationPolicy p;
  Recorder          r;
  auto              s = healthy();
  s.uptime_s = 0;  // hold the node document still too; it has its own jitter
  offer(p, gatelink(), s, 1000, r);
  r.clear();
  s.cell_mv[0] += 2;
  s.cell_mv[2] -= 1;
  s.last_traversal_age_s += 60;  // the same traversal, a minute older
  offer(p, gatelink(), s, 61000, r, kNow + 60);
  TEST_ASSERT_EQUAL(0, r.n);
  TEST_ASSERT_EQUAL(5, p.stats().unchanged);
}

// The deadband is measured from the PUBLISHED value, so a slow drift still crosses it.
void test_cell_drift_crosses_the_deadband() {
  PublicationPolicy p;
  Recorder          r;
  auto              s = healthy();
  offer(p, gatelink(), s, 1000, r);
  for (int i = 0; i < 4; ++i) {
    r.clear();
    s.cell_mv[0] += 1;
    offer(p, gatelink(), s, 2000 + i, r);
  }
  TEST_ASSERT_NULL(r.find("lran/gatelink/battery/state"));  // 3314: 4 mV, still inside
  r.clear();
  s.cell_mv[0] += 1;
  offer(p, gatelink(), s, 3000, r);
  expect(r.find("lran/gatelink/battery/state"), "cell1_mv", "3315");
}

void test_deadband_zero_publishes_every_change() {
  PublicationPolicy p;
  p.set_levers(PublishLevers{900, 600, 0});
  Recorder r;
  auto     s = healthy();
  offer(p, gatelink(), s, 1000, r);
  r.clear();
  s.cell_mv[3] += 1;
  offer(p, gatelink(), s, 2000, r);
  expect(r.find("lran/gatelink/battery/state"), "cell4_mv", "3311");
}

void test_heartbeat_republishes_unchanged_state() {
  PublicationPolicy p;
  p.set_levers(PublishLevers{60, 600, 5});
  Recorder r;
  auto     s = healthy();
  offer(p, gatelink(), s, 1000, r);
  r.clear();
  offer(p, gatelink(), s, 60999, r);
  TEST_ASSERT_NULL(r.find("lran/gatelink/gate/state"));
  offer(p, gatelink(), s, 61000, r);
  TEST_ASSERT_NOT_NULL(r.find("lran/gatelink/gate/state"));
  TEST_ASSERT_TRUE(p.stats().heartbeats >= 1);
}

// R-5.2b - a stale VE.Direct link: `available` false, and no MPPT reading survives, so the
// last good value cannot be shown as current.
void test_stale_mppt_is_unavailable_with_no_reading() {
  PublicationPolicy p;
  Recorder          r;
  auto              s = healthy();
  s.mppt_flags      = 0x02;
  offer(p, gatelink(), s, 1000, r);
  const char* solar = r.find("lran/gatelink/solar/state");
  expect(solar, "available", "false");
  expect(solar, "batt_mv", "null");
  expect(solar, "pv_w", "null");
  expect(solar, "yield_total_kwh", "null");
  TEST_ASSERT_NULL(std::strstr(solar, "13250"));
}

void test_bms_age_above_threshold_is_unavailable() {
  PublicationPolicy p;
  p.set_levers(PublishLevers{900, 120, 5});
  Recorder r;
  auto     s = healthy();
  s.bms_age_s = 120;
  offer(p, gatelink(), s, 1000, r);
  expect(r.find("lran/gatelink/battery/state"), "available", "true");
  r.clear();
  s.bms_age_s = 121;
  offer(p, gatelink(), s, 2000, r);
  const char* bat = r.find("lran/gatelink/battery/state");
  expect(bat, "available", "false");
  expect(bat, "soc", "null");
  expect(bat, "cell1_mv", "null");
  expect(bat, "age_s", "121");  // the reason stays readable
}

void test_bms_never_read_is_unavailable() {
  PublicationPolicy p;
  Recorder          r;
  auto              s = healthy();
  s.bms_flags       = 0;  // bit 0 clear - no successful read (spec 7.2.7)
  offer(p, gatelink(), s, 1000, r);
  expect(r.find("lran/gatelink/battery/state"), "available", "false");
  r.clear();
  s.bms_flags = 0x01;
  s.bms_age_s = kU16NotAvailable;
  offer(p, gatelink(), s, 2000, r);
  expect(r.find("lran/gatelink/battery/state"), "available", "false");
  expect(r.find("lran/gatelink/battery/state"), "age_s", "null");
}

// R-5.2c - every sentinel in schema 0x10 reaches the document as null.
void test_sentinels_are_null() {
  PublicationPolicy p;
  Recorder          r;
  auto              s = healthy();
  s.load_ma              = kI16NotAvailable;
  s.mppt_temp_c10        = kI16NotAvailable;
  s.bms_soc              = kSocNotAvailable;
  s.bms_rssi_neg         = 0;
  s.enclosure_temp_c10   = kI16NotAvailable;
  s.boot_count           = 0;
  s.last_traversal_age_s = kU32NotAvailable;
  s.cell_count           = 3;
  s.gate_state           = 0x07;  // not in spec 8.3's table
  offer(p, gatelink(), s, 1000, r);
  expect(r.find("lran/gatelink/solar/state"), "load_ma", "null");
  expect(r.find("lran/gatelink/solar/state"), "temp_c", "null");
  expect(r.find("lran/gatelink/battery/state"), "soc", "null");
  expect(r.find("lran/gatelink/battery/state"), "ble_rssi_dbm", "null");
  expect(r.find("lran/gatelink/battery/state"), "cell4_mv", "null");
  expect(r.find("lran/gatelink/battery/state"), "cell4_temp_c", "null");
  expect(r.find("lran/gatelink/node/state"), "enclosure_temp_c", "null");
  expect(r.find("lran/gatelink/node/state"), "boot_count", "null");
  expect(r.find("lran/gatelink/detect/state"), "last_traversal", "null");
  expect(r.find("lran/gatelink/gate/state"), "state", "null");
  for (size_t i = 0; i < r.n; ++i) {
    TEST_ASSERT_NULL_MESSAGE(std::strstr(r.items[i].payload, "-32768"), r.items[i].topic);
    TEST_ASSERT_NULL_MESSAGE(std::strstr(r.items[i].payload, "65535"), r.items[i].topic);
    TEST_ASSERT_NULL_MESSAGE(std::strstr(r.items[i].payload, "4294967295"), r.items[i].topic);
  }
}

// R-5.2d - spec 8.7's DEBUG_SYNTHETIC marks every document from that frame.
void test_synthetic_marks_every_document() {
  PublicationPolicy p;
  Recorder          r;
  auto              s = healthy();
  s.status_reason   = static_cast<uint8_t>(StatusReason::DebugSynthetic);
  offer(p, gatelink(), s, 1000, r);
  TEST_ASSERT_EQUAL(5, r.n);
  for (size_t i = 0; i < r.n; ++i) expect(r.items[i].payload, "synthetic", "true");
  r.clear();
  s.status_reason = 0;
  offer(p, gatelink(), s, 2000, r);
  TEST_ASSERT_EQUAL(5, r.n);  // clearing the mark is a change in every document
  for (size_t i = 0; i < r.n; ++i) expect(r.items[i].payload, "synthetic", "false");
}

// spec 7.2.9 - the age becomes an absolute time, steady across polls.
void test_traversal_is_an_absolute_time() {
  PublicationPolicy p;
  Recorder          r;
  auto              s = healthy();
  offer(p, gatelink(), s, 1000, r);
  expect(r.find("lran/gatelink/detect/state"), "last_traversal", "\"2026-09-23T11:50:00Z\"");
  r.clear();
  s.last_traversal_age_s = 661;  // 60 s later by the bridge's clock, a second of jitter
  offer(p, gatelink(), s, 61000, r, kNow + 60);
  TEST_ASSERT_NULL(r.find("lran/gatelink/detect/state"));
  r.clear();
  s.last_traversal_age_s = 5;  // a new vehicle
  offer(p, gatelink(), s, 121000, r, kNow + 120);
  expect(r.find("lran/gatelink/detect/state"), "last_traversal", "\"2026-09-23T12:01:55Z\"");
}

void test_traversal_is_null_without_a_clock() {
  PublicationPolicy p;
  Recorder          r;
  offer(p, gatelink(), healthy(), 1000, r, 0);
  expect(r.find("lran/gatelink/detect/state"), "last_traversal", "null");
}

// Spec 16.6 - a bench node reaches NO topic from here, with the flag either way (the flag
// is not an input to this path at all, which is the point), and 0xFE is bench-only.
void test_bench_node_publishes_nothing() {
  PublicationPolicy p;
  Recorder          r;
  offer(p, simnode(), healthy(), 1000, r, kNow, kSchemaSimnodeStatusV1);
  offer(p, simnode(), healthy(), 2000, r, kNow, kSchemaGateLinkStatusV1);
  schema::NodeHealthV1 h;
  uint8_t              buf[schema::kNodeHealthV1Len];
  size_t               n = 0;
  TEST_ASSERT_EQUAL(Status::Ok, schema::serialize(h, buf, sizeof(buf), &n));
  p.on_status(simnode(), status_hdr(kNodeSim1, kSchemaNodeHealthV1), buf, n, 3000, kNow, r);
  TEST_ASSERT_EQUAL(0, r.n);
  TEST_ASSERT_EQUAL(3, p.stats().bench_withheld);
}

// The second check, on the path every publication takes.
void test_transport_refuses_bench_production_topics() {
  PublishMessage m;
  for (const char* t : {"lran/simnode0/gate/state", "lran/simnode1/detect/state",
                        "lran/simnode2/battery/state", "lran/simnode3/solar/state",
                        "lran/simnode0/event/held_open"}) {
    TEST_ASSERT_FALSE_MESSAGE(make_publish(&m, t, "{}", false, 1), t);
    TEST_ASSERT_TRUE_MESSAGE(bench_topic_forbidden(t), t);
  }
  for (const char* t : {"lran/simnode0/diag/state", "lran/simnode0/availability",
                        "lran/simnode0/config/state", "lran/gatelink/gate/state"}) {
    TEST_ASSERT_TRUE_MESSAGE(make_publish(&m, t, "{}", true, 0), t);
  }
}

void test_health_goes_to_its_own_document() {
  PublicationPolicy p;
  Recorder          r;
  schema::NodeHealthV1 h;
  h.uptime_s      = 42;
  h.boot_count    = 3;
  h.last_snr_db10 = -75;
  uint8_t buf[schema::kNodeHealthV1Len];
  size_t  n = 0;
  TEST_ASSERT_EQUAL(Status::Ok, schema::serialize(h, buf, sizeof(buf), &n));
  const NodeInfo well{kNodeWellLink, NodeType::WellLink, false};
  p.on_status(well, status_hdr(kNodeWellLink, kSchemaNodeHealthV1), buf, n, 1000, kNow, r);
  TEST_ASSERT_EQUAL(1, r.n);
  const char* doc = r.find("lran/welllink/node/health/state");
  expect(doc, "last_snr_db", "-7.5");
  expect(doc, "last_rssi_dbm", "null");
}

// A refused document is not recorded as published, so the next frame retries it.
void test_refused_document_is_retried() {
  PublicationPolicy p;
  Recorder          r;
  r.refuse = true;
  offer(p, gatelink(), healthy(), 1000, r);
  TEST_ASSERT_EQUAL(5, p.stats().queue_refused);
  r.refuse = false;
  offer(p, gatelink(), healthy(), 2000, r);
  TEST_ASSERT_EQUAL(5, r.n);
}

void test_reconnect_republishes_everything() {
  PublicationPolicy p;
  Recorder          r;
  offer(p, gatelink(), healthy(), 1000, r);
  r.clear();
  p.forget_published();
  offer(p, gatelink(), healthy(), 2000, r);
  TEST_ASSERT_EQUAL(5, r.n);
}

void test_other_types_are_ignored() {
  PublicationPolicy p;
  Recorder          r;
  Header            h = status_hdr(kNodeGateLink, kSchemaGateLinkEventV1);
  h.type              = MsgType::Event;
  uint8_t buf[16]     = {0};
  p.on_status(gatelink(), h, buf, sizeof(buf), 1000, kNow, r);
  TEST_ASSERT_EQUAL(0, r.n);
  TEST_ASSERT_EQUAL(0, p.stats().status_frames);
}

// The largest document every key of can produce still fits a PublishMessage.
void test_worst_documents_fit() {
  PublicationPolicy p;
  Recorder          r;
  schema::GateLinkStatusV1 s = healthy();
  s.batt_ma = INT16_MIN + 1;
  s.pack_ma = INT16_MIN + 1;
  s.node_ma = INT16_MIN + 1;
  s.yield_total = UINT32_MAX;
  s.uptime_s    = UINT32_MAX;
  s.gate_state  = static_cast<uint8_t>(GateState::OpenCountdown);
  s.movement_cause = static_cast<uint8_t>(MovementCause::ExternalMomentary);
  offer(p, gatelink(), s, 1000, r);
  TEST_ASSERT_EQUAL(5, r.n);
  for (size_t i = 0; i < r.n; ++i) {
    TEST_ASSERT_TRUE(std::strlen(r.items[i].payload) < kMaxPayloadLen / 2);
  }
}

void test_format_utc() {
  char b[24];
  TEST_ASSERT_TRUE(format_utc(0, b, sizeof(b)) > 0);
  TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00Z", b);
  TEST_ASSERT_TRUE(format_utc(951782400, b, sizeof(b)) > 0);  // a leap day
  TEST_ASSERT_EQUAL_STRING("2000-02-29T00:00:00Z", b);
  TEST_ASSERT_TRUE(format_utc(kNow + 3599, b, sizeof(b)) > 0);
  TEST_ASSERT_EQUAL_STRING("2026-09-23T12:59:59Z", b);
  TEST_ASSERT_EQUAL(0, format_utc(0, b, 10));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_frame_publishes_five_documents);
  RUN_TEST(test_units_are_converted_exactly);
  RUN_TEST(test_cell_jitter_is_not_republished);
  RUN_TEST(test_cell_drift_crosses_the_deadband);
  RUN_TEST(test_deadband_zero_publishes_every_change);
  RUN_TEST(test_heartbeat_republishes_unchanged_state);
  RUN_TEST(test_stale_mppt_is_unavailable_with_no_reading);
  RUN_TEST(test_bms_age_above_threshold_is_unavailable);
  RUN_TEST(test_bms_never_read_is_unavailable);
  RUN_TEST(test_sentinels_are_null);
  RUN_TEST(test_synthetic_marks_every_document);
  RUN_TEST(test_traversal_is_an_absolute_time);
  RUN_TEST(test_traversal_is_null_without_a_clock);
  RUN_TEST(test_bench_node_publishes_nothing);
  RUN_TEST(test_transport_refuses_bench_production_topics);
  RUN_TEST(test_health_goes_to_its_own_document);
  RUN_TEST(test_refused_document_is_retried);
  RUN_TEST(test_reconnect_republishes_everything);
  RUN_TEST(test_other_types_are_ignored);
  RUN_TEST(test_worst_documents_fit);
  RUN_TEST(test_format_utc);
  return UNITY_END();
}
