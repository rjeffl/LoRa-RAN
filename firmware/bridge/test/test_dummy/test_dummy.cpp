// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-27 - the dummy publish (Impl Plan 6.6.2, PRD R-5.2d).
//
// THE PROPERTIES UNDER TEST. A console line becomes a frame the real policy publishes, and
// every document and event from it says `synthetic: true`. Nothing on the console can clear
// the mark. A bench node is refused. A sentinel set from the console is published as null,
// and a follow-up reuses its event's event_id (spec 7.3).
//
// WHAT THIS CANNOT COVER. The refusal of a node heard this boot is task_runtime.cpp's, which
// asks the registry; and whether Home Assistant shows what the policy published is the
// bench run's (Impl Plan 6.6.2).

#include <unity.h>

#include <cstdio>
#include <cstring>

#include "dummy.h"
#include "mqtt_transport.h"
#include "publish.h"
#include "registry.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

constexpr CtxId kCtx = 0x0D0D0D0D;

struct Recorder final : PublishSink {
  struct Item {
    char    topic[kMaxTopicLen];
    char    payload[kMaxPayloadLen];
    bool    retain;
  };
  Item   items[16];
  size_t n = 0;

  bool emit(const char* topic, const char* payload, bool retain, uint8_t qos) override {
    PublishMessage m;
    TEST_ASSERT_TRUE_MESSAGE(make_publish(&m, topic, payload, retain, qos), topic);
    TEST_ASSERT_TRUE(n < 16);
    std::snprintf(items[n].topic, sizeof(items[n].topic), "%s", topic);
    std::snprintf(items[n].payload, sizeof(items[n].payload), "%s", payload);
    items[n].retain = retain;
    ++n;
    return true;
  }

  const char* payload_of(const char* topic) const {
    for (size_t i = 0; i < n; ++i) {
      if (std::strcmp(items[i].topic, topic) == 0) return items[i].payload;
    }
    return nullptr;
  }
};

// As app_task hands a dummy message over: the event marked by the caller, the STATUS by its
// own status_reason.
void deliver(PublicationPolicy& p, const RxMessage& m, Recorder& r) {
  const NodeInfo info{m.hdr.src, NodeType::GateLink, is_bench_node(m.hdr.src)};
  if (m.hdr.type == MsgType::Event) {
    p.on_event(info, m.hdr, m.payload, m.payload_len, /*synthetic=*/true, r);
  } else {
    p.on_status(info, m.hdr, m.payload, m.payload_len, m.rx_millis, 0, r);
  }
}

DummyOutcome run(DummyPublisher& d, const char* line, RxMessage* m, char* reply = nullptr,
                 uint32_t now_ms = 1000) {
  char buf[1024];
  return d.handle(line, kCtx, now_ms, m, reply != nullptr ? reply : buf, sizeof(buf));
}

bool has(const char* json, const char* fragment) {
  return json != nullptr && std::strstr(json, fragment) != nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------

void test_not_a_dummy_line_is_left_alone() {
  DummyPublisher d;
  RxMessage      m;
  TEST_ASSERT_EQUAL(DummyOutcome::NotMine, run(d, "ping 3", &m));
  TEST_ASSERT_EQUAL(DummyOutcome::NotMine, run(d, "", &m));
}

// R-5.2d - all five documents from a dummy STATUS carry the mark, on the named node's topics.
void test_status_publishes_every_document_marked() {
  DummyPublisher    d;
  PublicationPolicy p;
  Recorder          r;
  RxMessage         m;
  TEST_ASSERT_EQUAL(DummyOutcome::Inject, run(d, "dummy status gatelink", &m));
  TEST_ASSERT_TRUE(m.dummy);
  TEST_ASSERT_EQUAL_HEX8(kNodeGateLink, m.hdr.src);
  TEST_ASSERT_EQUAL_UINT32(kCtx, m.hdr.ctx_id);
  deliver(p, m, r);
  TEST_ASSERT_EQUAL(5, r.n);
  for (size_t i = 0; i < r.n; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(has(r.items[i].payload, "\"synthetic\":true"), r.items[i].topic);
    TEST_ASSERT_TRUE(r.items[i].retain);
  }
  TEST_ASSERT_NOT_NULL(r.payload_of("lran/gatelink/gate/state"));
  TEST_ASSERT_NOT_NULL(r.payload_of("lran/gatelink/battery/state"));
}

// Nothing on the console reaches status_reason, so nothing can clear the mark.
void test_status_reason_cannot_be_set() {
  DummyPublisher d;
  RxMessage      m;
  TEST_ASSERT_EQUAL(DummyOutcome::Refused, run(d, "dummy set status_reason=0", &m));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::DebugSynthetic),
                          d.status_template().status_reason);
}

// Root rule 6 - `na` is the sentinel, and the policy publishes it as null, not a number.
void test_sentinel_publishes_null() {
  DummyPublisher    d;
  PublicationPolicy p;
  Recorder          r;
  RxMessage         m;
  TEST_ASSERT_EQUAL(DummyOutcome::Reply, run(d, "dummy set enclosure_temp_c10=na", &m));
  TEST_ASSERT_EQUAL_INT16(kI16NotAvailable, d.status_template().enclosure_temp_c10);
  TEST_ASSERT_EQUAL(DummyOutcome::Inject, run(d, "dummy status gatelink", &m));
  deliver(p, m, r);
  TEST_ASSERT_TRUE(has(r.payload_of("lran/gatelink/node/state"), "\"enclosure_temp_c\":null"));
}

// A set is all or nothing, so a typo in one pair does not leave half a change behind.
void test_set_is_all_or_nothing() {
  DummyPublisher d;
  RxMessage      m;
  const uint16_t before = d.status_template().batt_mv;
  TEST_ASSERT_EQUAL(DummyOutcome::Refused, run(d, "dummy set batt_mv=12000 no_such=1", &m));
  TEST_ASSERT_EQUAL_UINT16(before, d.status_template().batt_mv);
  TEST_ASSERT_EQUAL(DummyOutcome::Refused, run(d, "dummy set batt_mv=70000", &m));
  TEST_ASSERT_EQUAL(DummyOutcome::Refused, run(d, "dummy set batt_mv=12x", &m));
  TEST_ASSERT_EQUAL(DummyOutcome::Refused, run(d, "dummy set pv_w=na", &m));  // no sentinel
  TEST_ASSERT_EQUAL(DummyOutcome::Reply, run(d, "dummy set batt_mv=12000 cell2_mv=3290", &m));
  TEST_ASSERT_EQUAL_UINT16(12000, d.status_template().batt_mv);
  TEST_ASSERT_EQUAL_UINT16(3290, d.status_template().cell_mv[1]);
}

// Spec 16.6 publishes neither a bench node's STATUS nor its EVENT, so the console says so
// rather than queueing a frame the policy would withhold.
void test_bench_node_is_refused() {
  DummyPublisher d;
  RxMessage      m;
  TEST_ASSERT_EQUAL(DummyOutcome::Refused, run(d, "dummy status simnode1", &m));
  TEST_ASSERT_EQUAL(DummyOutcome::Refused, run(d, "dummy event simnode1 boot", &m));
  TEST_ASSERT_EQUAL(DummyOutcome::Refused, run(d, "dummy status nosuchnode", &m));
}

// Spec 16.3 and R-5.2d - an event publishes once, unretained, marked. A follow-up reuses its
// event's event_id (spec 7.3) and is published too.
void test_event_and_follow_up() {
  DummyPublisher    d;
  PublicationPolicy p;
  Recorder          r;
  RxMessage         m;
  TEST_ASSERT_EQUAL(DummyOutcome::Refused, run(d, "dummy event gatelink vehicle_detected follow", &m));
  TEST_ASSERT_EQUAL(DummyOutcome::Inject, run(d, "dummy event gatelink vehicle_detected", &m));
  deliver(p, m, r);
  TEST_ASSERT_EQUAL(DummyOutcome::Inject,
                    run(d, "dummy event gatelink vehicle_detected follow", &m));
  deliver(p, m, r);
  TEST_ASSERT_EQUAL(2, r.n);
  for (size_t i = 0; i < r.n; ++i) {
    TEST_ASSERT_EQUAL_STRING("lran/gatelink/event/vehicle_detected", r.items[i].topic);
    TEST_ASSERT_FALSE(r.items[i].retain);
    TEST_ASSERT_TRUE(has(r.items[i].payload, "\"synthetic\":true"));
    TEST_ASSERT_TRUE(has(r.items[i].payload, "\"event_id\":1,"));
  }
  TEST_ASSERT_TRUE(has(r.items[0].payload, "\"follow_up\":false"));
  TEST_ASSERT_TRUE(has(r.items[1].payload, "\"follow_up\":true"));

  // The next event takes the next id; a number names a type as well as its name does.
  TEST_ASSERT_EQUAL(DummyOutcome::Inject, run(d, "dummy event gatelink 2", &m));
  deliver(p, m, r);
  TEST_ASSERT_EQUAL(3, r.n);
  TEST_ASSERT_EQUAL_STRING("lran/gatelink/event/fire_asserted", r.items[2].topic);
  TEST_ASSERT_TRUE(has(r.items[2].payload, "\"event_id\":2,"));
  TEST_ASSERT_EQUAL(DummyOutcome::Refused, run(d, "dummy event gatelink no_such_event", &m));
}

// A repeated STATUS goes through the policy's deadband like a node's does: unchanged is
// withheld, which is the rule the console exists to demonstrate.
void test_unchanged_status_is_withheld() {
  DummyPublisher    d;
  PublicationPolicy p;
  Recorder          r;
  RxMessage         m;
  run(d, "dummy status gatelink", &m);
  deliver(p, m, r);
  run(d, "dummy status gatelink", &m);
  deliver(p, m, r);
  TEST_ASSERT_EQUAL(5, r.n);
  TEST_ASSERT_EQUAL(5, p.stats().unchanged);
}

// A node's clock moves between frames, so the dummy's does: uptime and the traversal age
// advance together, and the traversal's absolute time stays put (spec 7.2.9). Found on the
// bench, 2026-09-23: held still, the detect document republished on every frame.
void test_clock_fields_advance() {
  DummyPublisher    d;
  PublicationPolicy p;
  Recorder          r;
  RxMessage         m;
  char              reply[1024];
  const uint32_t    up  = d.status_template().uptime_s;
  const uint32_t    age = d.status_template().last_traversal_age_s;
  run(d, "dummy status gatelink", &m, reply, 1000);
  p.on_status(NodeInfo{kNodeGateLink, NodeType::GateLink, false}, m.hdr, m.payload,
              m.payload_len, m.rx_millis, 1790000000, r);
  run(d, "dummy status gatelink", &m, reply, 7500);  // 6.5 s on
  TEST_ASSERT_EQUAL_UINT32(up + 6, d.status_template().uptime_s);
  TEST_ASSERT_EQUAL_UINT32(age + 6, d.status_template().last_traversal_age_s);
  const size_t before = r.n;
  p.on_status(NodeInfo{kNodeGateLink, NodeType::GateLink, false}, m.hdr, m.payload,
              m.payload_len, m.rx_millis, 1790000006, r);
  for (size_t i = before; i < r.n; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(std::strcmp(r.items[i].topic, "lran/gatelink/detect/state") != 0,
                             "the traversal moved");
  }
  run(d, "dummy status gatelink", &m, reply, 8000);  // the carried half second counts
  TEST_ASSERT_EQUAL_UINT32(up + 7, d.status_template().uptime_s);

  // A sentinel age stays a sentinel.
  run(d, "dummy set last_traversal_age_s=na", &m, reply, 8000);
  run(d, "dummy status gatelink", &m, reply, 20000);
  TEST_ASSERT_EQUAL_UINT32(kU32NotAvailable, d.status_template().last_traversal_age_s);
}

void test_show_and_help_reply() {
  DummyPublisher d;
  RxMessage      m;
  char           reply[1024];
  TEST_ASSERT_EQUAL(DummyOutcome::Reply, run(d, "dummy show", &m, reply));
  TEST_ASSERT_TRUE(has(reply, "batt_mv=13250"));
  TEST_ASSERT_TRUE(has(reply, "cell4_temp_c=22"));
  TEST_ASSERT_EQUAL(DummyOutcome::Reply, run(d, "dummy", &m, reply));
  TEST_ASSERT_TRUE(has(reply, "status <node>"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_not_a_dummy_line_is_left_alone);
  RUN_TEST(test_status_publishes_every_document_marked);
  RUN_TEST(test_status_reason_cannot_be_set);
  RUN_TEST(test_sentinel_publishes_null);
  RUN_TEST(test_set_is_all_or_nothing);
  RUN_TEST(test_bench_node_is_refused);
  RUN_TEST(test_event_and_follow_up);
  RUN_TEST(test_unchanged_status_is_withheld);
  RUN_TEST(test_clock_fields_advance);
  RUN_TEST(test_show_and_help_reply);
  return UNITY_END();
}
