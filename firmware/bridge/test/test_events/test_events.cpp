// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-25 - event republication (Impl Plan 6.3.2, PRD R-5.2e, V-B8).
//
// THE PROPERTIES UNDER TEST. An event is published with retain clear at QoS 1, to a topic
// named for its type. A retransmission is withheld, and a follow-up is not. Nothing
// republishes an event: not a broker connect, not a later STATUS. A bench node's event
// reaches no topic.
//
// WHAT THIS CANNOT COVER. V-B8 as written is an HA restart and a discovery refresh with an
// event in history, and no node on the bench sends an EVENT: a simnode is a bench node, and
// spec 16.6 keeps its events off every topic. These frames are built by the library's own
// serializer. The transport publishes at QoS 0 whatever is asked (mqtt_pubsub.cpp).

#include <unity.h>

#include <cstdio>
#include <cstring>

#include "lran/schema/gatelink_event_v1.h"
#include "lran/schema/gatelink_status_v1.h"
#include "mqtt_transport.h"
#include "net_policy.h"
#include "publish.h"
#include "registry.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

constexpr CtxId kCtx = 0x1234ABCD;

struct Recorder final : PublishSink {
  struct Item {
    char    topic[kMaxTopicLen];
    char    payload[kMaxPayloadLen];
    bool    retain;
    uint8_t qos;
  };
  Item   items[24];
  size_t n      = 0;
  bool   refuse = false;

  bool emit(const char* topic, const char* payload, bool retain, uint8_t qos) override {
    if (refuse) return false;
    // Through make_publish(), as app_task's sink goes, so an event it would refuse fails
    // here rather than at the broker.
    PublishMessage m;
    TEST_ASSERT_TRUE_MESSAGE(make_publish(&m, topic, payload, retain, qos), topic);
    TEST_ASSERT_TRUE(n < 24);
    std::snprintf(items[n].topic, sizeof(items[n].topic), "%s", topic);
    std::snprintf(items[n].payload, sizeof(items[n].payload), "%s", payload);
    items[n].retain = retain;
    items[n].qos    = qos;
    ++n;
    return true;
  }
};

NodeInfo gatelink() { return NodeInfo{kNodeGateLink, NodeType::GateLink, false}; }
NodeInfo simnode() { return NodeInfo{kNodeSim1, NodeType::Simnode, true}; }

schema::GateLinkEventV1 vehicle(uint32_t id) {
  schema::GateLinkEventV1 e;
  e.event_type  = static_cast<uint8_t>(EventType::VehicleWhileHeldOpen);
  e.hold_source = static_cast<uint8_t>(HoldSource::Lran);
  e.direction   = static_cast<uint8_t>(Direction::Undetermined);
  e.gate_state  = static_cast<uint8_t>(GateState::OpenHeld);
  e.input_bits  = 0x05;
  e.detail      = 300;
  e.event_id    = id;
  e.uptime_s    = 86400;
  return e;
}

void send(PublicationPolicy& p, const NodeInfo& info, const schema::GateLinkEventV1& e,
          Recorder& r, CtxId ctx = kCtx, SchemaId schema = kSchemaGateLinkEventV1,
          bool synthetic = false) {
  uint8_t buf[schema::kGateLinkEventV1Len];
  size_t  n = 0;
  TEST_ASSERT_EQUAL(Status::Ok, schema::serialize(e, buf, sizeof(buf), &n));
  Header h;
  h.type   = MsgType::Event;
  h.src    = info.id;
  h.ctx_id = ctx;
  h.schema = schema;
  p.on_event(info, h, buf, n, synthetic, r);
}

// The text after "key": up to the next comma or brace.
void expect(const char* json, const char* key, const char* want) {
  char needle[48];
  std::snprintf(needle, sizeof(needle), "\"%s\":", key);
  const char* at = std::strstr(json, needle);
  TEST_ASSERT_NOT_NULL_MESSAGE(at, key);
  at += std::strlen(needle);
  char   got[64];
  size_t i = 0;
  while (at[i] != '\0' && at[i] != ',' && at[i] != '}' && i + 1 < sizeof(got)) {
    got[i] = at[i];
    ++i;
  }
  got[i] = '\0';
  TEST_ASSERT_EQUAL_STRING_MESSAGE(want, got, key);
}

}  // namespace

// ---------------------------------------------------------------------------

// Spec 16.3 - retain clear, QoS 1, carrying the event_id, on a topic named for spec 8.9's
// type.
void test_event_publishes_once_not_retained_at_qos_1() {
  PublicationPolicy p;
  Recorder          r;
  send(p, gatelink(), vehicle(7), r);
  TEST_ASSERT_EQUAL(1, r.n);
  TEST_ASSERT_EQUAL_STRING("lran/gatelink/event/vehicle_while_held_open", r.items[0].topic);
  TEST_ASSERT_FALSE(r.items[0].retain);
  TEST_ASSERT_EQUAL_UINT8(1, r.items[0].qos);
  const char* doc = r.items[0].payload;
  expect(doc, "event_id", "7");
  expect(doc, "ctx_id", "305441741");
  expect(doc, "event_type", "\"vehicle_while_held_open\"");
  expect(doc, "event_code", "1");
  expect(doc, "follow_up", "false");
  expect(doc, "hold_source", "\"lran\"");
  expect(doc, "direction", "\"undetermined\"");
  expect(doc, "gate_state", "\"open_held\"");
  expect(doc, "input_bits", "5");
  expect(doc, "detail", "300");
  expect(doc, "uptime_s", "86400");
  expect(doc, "synthetic", "false");
  TEST_ASSERT_EQUAL(1, p.stats().event_frames);
  TEST_ASSERT_EQUAL(1, p.stats().events);
}

// R-5.2d, BF-27. An EVENT has no status_reason, so the mark comes from the caller: the dummy
// publish says so, and the payload carries it. Deduplication is unchanged by it.
void test_synthetic_event_is_marked() {
  PublicationPolicy p;
  Recorder          r;
  send(p, gatelink(), vehicle(7), r, kCtx, kSchemaGateLinkEventV1, /*synthetic=*/true);
  send(p, gatelink(), vehicle(7), r, kCtx, kSchemaGateLinkEventV1, /*synthetic=*/true);
  TEST_ASSERT_EQUAL(1, r.n);
  expect(r.items[0].payload, "synthetic", "true");
  TEST_ASSERT_FALSE(r.items[0].retain);
  TEST_ASSERT_EQUAL(1, p.stats().event_repeats);
}

// Spec 8.9 - FIRE is the one signal allowed to wake someone, so it has a topic of its own.
void test_fire_has_its_own_topic() {
  PublicationPolicy       p;
  Recorder                r;
  schema::GateLinkEventV1 e = vehicle(1);
  e.event_type = static_cast<uint8_t>(EventType::FireAsserted);
  send(p, gatelink(), e, r);
  TEST_ASSERT_EQUAL(1, r.n);
  TEST_ASSERT_EQUAL_STRING("lran/gatelink/event/fire_asserted", r.items[0].topic);
}

// Spec 7.3 - an EVENT has no ACK, so the node may send it again after a CAD backoff.
void test_retransmission_is_withheld() {
  PublicationPolicy p;
  Recorder          r;
  send(p, gatelink(), vehicle(7), r);
  send(p, gatelink(), vehicle(7), r);
  send(p, gatelink(), vehicle(7), r);
  TEST_ASSERT_EQUAL(1, r.n);
  TEST_ASSERT_EQUAL(3, p.stats().event_frames);
  TEST_ASSERT_EQUAL(2, p.stats().event_repeats);
}

// Spec 7.3 - a follow-up carries its first edge's event_id with event_flags bit 0 set. It
// is the only way the classified direction reaches HA, so the key includes the bit.
void test_follow_up_is_published_and_its_repeat_is_not() {
  PublicationPolicy p;
  Recorder          r;
  send(p, gatelink(), vehicle(7), r);
  schema::GateLinkEventV1 f = vehicle(7);
  f.event_flags = schema::kEventFlagFollowUp;
  f.direction   = static_cast<uint8_t>(Direction::Entry);
  send(p, gatelink(), f, r);
  send(p, gatelink(), f, r);
  TEST_ASSERT_EQUAL(2, r.n);
  expect(r.items[1].payload, "follow_up", "true");
  expect(r.items[1].payload, "direction", "\"entry\"");
  expect(r.items[1].payload, "event_id", "7");
  TEST_ASSERT_EQUAL(1, p.stats().event_repeats);
}

// Root rule 5 - event_flags bits 7:1 are reserved and ignored, so they do not make a
// retransmission look like a new event.
void test_reserved_flag_bits_are_ignored() {
  PublicationPolicy p;
  Recorder          r;
  send(p, gatelink(), vehicle(7), r);
  schema::GateLinkEventV1 e = vehicle(7);
  e.event_flags = 0xFE;
  send(p, gatelink(), e, r);
  TEST_ASSERT_EQUAL(1, r.n);
  TEST_ASSERT_EQUAL(1, p.stats().event_repeats);
}

// event_id restarts with each boot (spec 7.3), and a new boot is a new ctx_id (spec 10.1).
void test_same_event_id_under_a_new_ctx_id_is_published() {
  PublicationPolicy p;
  Recorder          r;
  send(p, gatelink(), vehicle(1), r, kCtx);
  send(p, gatelink(), vehicle(1), r, kCtx + 1);
  TEST_ASSERT_EQUAL(2, r.n);
  expect(r.items[1].payload, "ctx_id", "305441742");
}

// A refused event is not remembered, so the node's retransmission of it gets through.
void test_refused_event_is_not_remembered() {
  PublicationPolicy p;
  Recorder          r;
  r.refuse = true;
  send(p, gatelink(), vehicle(7), r);
  TEST_ASSERT_EQUAL(1, p.stats().queue_refused);
  r.refuse = false;
  send(p, gatelink(), vehicle(7), r);
  TEST_ASSERT_EQUAL(1, r.n);
  TEST_ASSERT_EQUAL(0, p.stats().event_repeats);
}

// V-B8's property at the bridge. A broker connect makes the documents due again and
// leaves the events alone: it publishes nothing by itself, and a retransmission that
// arrives after it is still a repeat. A STATUS publishes no event topic either.
void test_nothing_republishes_an_event() {
  PublicationPolicy p;
  Recorder          r;
  send(p, gatelink(), vehicle(7), r);
  p.forget_published();
  send(p, gatelink(), vehicle(7), r);
  TEST_ASSERT_EQUAL(1, r.n);

  schema::GateLinkStatusV1 s;
  uint8_t                  buf[schema::kGateLinkStatusV1Len];
  size_t                   n = 0;
  TEST_ASSERT_EQUAL(Status::Ok, schema::serialize(s, buf, sizeof(buf), &n));
  Header h;
  h.type   = MsgType::Status;
  h.src    = kNodeGateLink;
  h.ctx_id = kCtx;
  h.schema = kSchemaGateLinkStatusV1;
  p.on_status(gatelink(), h, buf, n, 1000, 0, r);
  TEST_ASSERT_EQUAL(6, r.n);  // the event, then five documents
  for (size_t i = 1; i < r.n; ++i) {
    TEST_ASSERT_FALSE_MESSAGE(is_event_topic(r.items[i].topic), r.items[i].topic);
  }
}

// The memory holds kEventMemory events a node. Inside it a repeat is withheld; the
// seventeenth event evicts the first, whose repeat then publishes again. A
// retransmission follows by one CAD backoff, so the limit is not reached in service.
void test_memory_is_a_ring_of_sixteen() {
  PublicationPolicy p;
  Recorder          r;
  for (uint32_t id = 1; id <= 16; ++id) send(p, gatelink(), vehicle(id), r);
  send(p, gatelink(), vehicle(1), r);
  TEST_ASSERT_EQUAL(16, r.n);
  send(p, gatelink(), vehicle(17), r);
  send(p, gatelink(), vehicle(1), r);
  TEST_ASSERT_EQUAL(18, r.n);
}

// One node's events do not suppress another's with the same key.
void test_memory_is_per_node() {
  PublicationPolicy p;
  Recorder          r;
  const NodeInfo    well{kNodeWellLink, NodeType::WellLink, false};
  send(p, gatelink(), vehicle(7), r);
  send(p, well, vehicle(7), r);
  TEST_ASSERT_EQUAL(2, r.n);
  TEST_ASSERT_EQUAL_STRING("lran/welllink/event/vehicle_while_held_open", r.items[1].topic);
}

// Spec 16.6 axis 1 - decoded and counted, never published, and the transport refuses the
// topic as a second check.
void test_bench_event_publishes_nothing() {
  PublicationPolicy p;
  Recorder          r;
  send(p, simnode(), vehicle(7), r);
  TEST_ASSERT_EQUAL(0, r.n);
  TEST_ASSERT_EQUAL(1, p.stats().bench_withheld);
  PublishMessage m;
  TEST_ASSERT_FALSE(make_publish(&m, "lran/simnode1/event/vehicle_while_held_open", "{}",
                                 false, 1));
}

// spec 16.7.5 - a node's PHY_REVERTED publishes on its own topic, named as 8.9 names it,
// rather than falling through to `event/unknown`.
void test_phy_reverted_has_its_own_topic() {
  PublicationPolicy       p;
  Recorder                r;
  schema::GateLinkEventV1 e = vehicle(3);
  e.event_type = static_cast<uint8_t>(lran::EventType::PhyReverted);
  e.detail     = 0x0001;
  send(p, gatelink(), e, r);
  TEST_ASSERT_EQUAL(1, r.n);
  TEST_ASSERT_EQUAL_STRING("lran/gatelink/event/phy_reverted", r.items[0].topic);
}

// A type spec 8.9 does not list is published, not dropped: a newer node's event may be an
// alert. Its name is null and its raw value is kept, as BF-24 treats an unknown enum.
void test_unknown_type_goes_to_event_unknown() {
  PublicationPolicy       p;
  Recorder                r;
  schema::GateLinkEventV1 e = vehicle(3);
  e.event_type  = 0x42;
  e.hold_source = 0x09;
  e.direction   = 0x09;
  e.gate_state  = 0x09;
  send(p, gatelink(), e, r);
  TEST_ASSERT_EQUAL(1, r.n);
  TEST_ASSERT_EQUAL_STRING("lran/gatelink/event/unknown", r.items[0].topic);
  expect(r.items[0].payload, "event_type", "null");
  expect(r.items[0].payload, "event_code", "66");
  expect(r.items[0].payload, "hold_source", "null");
  expect(r.items[0].payload, "direction", "null");
  expect(r.items[0].payload, "gate_state", "null");
}

void test_wrong_schema_or_length_is_undecodable() {
  PublicationPolicy p;
  Recorder          r;
  send(p, gatelink(), vehicle(1), r, kCtx, kSchemaGateLinkStatusV1);
  uint8_t buf[schema::kGateLinkEventV1Len] = {0};
  Header  h;
  h.type   = MsgType::Event;
  h.src    = kNodeGateLink;
  h.schema = kSchemaGateLinkEventV1;
  p.on_event(gatelink(), h, buf, sizeof(buf) - 1, false, r);
  TEST_ASSERT_EQUAL(0, r.n);
  TEST_ASSERT_EQUAL(2, p.stats().undecodable);
}

// Each entry point ignores the other's type and does not count it.
void test_status_is_not_an_event() {
  PublicationPolicy p;
  Recorder          r;
  uint8_t           buf[schema::kGateLinkEventV1Len] = {0};
  Header            h;
  h.type   = MsgType::Status;
  h.src    = kNodeGateLink;
  h.schema = kSchemaGateLinkEventV1;
  p.on_event(gatelink(), h, buf, sizeof(buf), false, r);
  TEST_ASSERT_EQUAL(0, r.n);
  TEST_ASSERT_EQUAL(0, p.stats().event_frames);
}

void test_stats_carry_the_event_counts() {
  PublishStats s;
  s.event_frames  = 3;
  s.events        = 2;
  s.event_repeats = 1;
  char   out[512];
  size_t n = publish_stats_json(s, out, sizeof(out));
  TEST_ASSERT_TRUE(n > 0);
  expect(out, "event_frames", "3");
  expect(out, "events", "2");
  expect(out, "event_repeats", "1");
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_event_publishes_once_not_retained_at_qos_1);
  RUN_TEST(test_fire_has_its_own_topic);
  RUN_TEST(test_retransmission_is_withheld);
  RUN_TEST(test_follow_up_is_published_and_its_repeat_is_not);
  RUN_TEST(test_reserved_flag_bits_are_ignored);
  RUN_TEST(test_same_event_id_under_a_new_ctx_id_is_published);
  RUN_TEST(test_refused_event_is_not_remembered);
  RUN_TEST(test_nothing_republishes_an_event);
  RUN_TEST(test_memory_is_a_ring_of_sixteen);
  RUN_TEST(test_memory_is_per_node);
  RUN_TEST(test_bench_event_publishes_nothing);
  RUN_TEST(test_unknown_type_goes_to_event_unknown);
  RUN_TEST(test_phy_reverted_has_its_own_topic);
  RUN_TEST(test_wrong_schema_or_length_is_undecodable);
  RUN_TEST(test_status_is_not_an_event);
  RUN_TEST(test_stats_carry_the_event_counts);
  RUN_TEST(test_synthetic_event_is_marked);
  return UNITY_END();
}
