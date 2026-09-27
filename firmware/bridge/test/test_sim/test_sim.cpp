// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-27 - the bridge-side GateLink simulator (Impl Plan 6.6.3, PRD R-5.4c, R-5.2d).
//
// THE PROPERTIES UNDER TEST. Every frame is marked synthetic and goes to the policy as the
// dummy's does. STATUS keeps its period and never bursts to catch up. No event is sent
// unless `gate` asks, and a cycle is five edges in order. The model moves: the sun sets,
// yield rolls at midnight, the pack stays inside its capacity. A `start` with any bad
// setting applies none of them.
//
// WHAT THIS CANNOT COVER. The refusal of a node heard this boot, and the stop that follows
// it, are task_runtime.cpp's; and whether HA's graphs move is the bench's (Impl Plan 6.6.3).

#include <unity.h>

#include <cstdio>
#include <cstring>

#include "gatelink_sim.h"
#include "lran/schema/gatelink_event_v1.h"
#include "mqtt_transport.h"
#include "net_policy.h"
#include "publish.h"
#include "registry.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

constexpr CtxId    kCtx = 0x51515151;
constexpr uint32_t kT0  = 5000;

struct Counter final : PublishSink {
  size_t n         = 0;
  size_t synthetic = 0;
  bool emit(const char*, const char* payload, bool, uint8_t) override {
    ++n;
    if (std::strstr(payload, "\"synthetic\":true") != nullptr) ++synthetic;
    return true;
  }
};

SimOutcome run(GateLinkSim& s, const char* line, uint32_t now_ms = kT0) {
  char reply[256];
  return s.handle(line, kCtx, now_ms, reply, sizeof(reply));
}

// Polls every 50 ms, as loop() does, from `from` up to and including `to`. Counts what came.
struct Tally {
  size_t   status = 0;
  size_t   events = 0;
  uint8_t  types[16];
  uint8_t  states[16];
  uint32_t ids[16];
};

void poll_span(GateLinkSim& s, uint32_t from, uint32_t to, Tally* t) {
  RxMessage m;
  for (uint32_t now = from; now <= to; now += 50) {
    while (s.poll(now, &m)) {
      TEST_ASSERT_TRUE(m.dummy);
      if (m.hdr.type == MsgType::Status) {
        ++t->status;
        continue;
      }
      schema::GateLinkEventV1 e;
      TEST_ASSERT_EQUAL(Status::Ok, schema::deserialize(m.payload, m.payload_len, &e));
      if (t->events < 16) {
        t->types[t->events]  = e.event_type;
        t->states[t->events] = e.gate_state;
        t->ids[t->events]    = e.event_id;
      }
      ++t->events;
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------

void test_not_a_sim_line_is_left_alone() {
  GateLinkSim s;
  TEST_ASSERT_EQUAL(SimOutcome::NotMine, run(s, "dummy show"));
  TEST_ASSERT_EQUAL(SimOutcome::NotMine, run(s, ""));
  TEST_ASSERT_FALSE(s.running());
}

void test_start_refuses_a_bench_node_and_an_unknown_one() {
  GateLinkSim s;
  TEST_ASSERT_EQUAL(SimOutcome::Refused, run(s, "sim start nosuchnode"));
  TEST_ASSERT_EQUAL(SimOutcome::Refused, run(s, "sim start"));
  char name[16];
  TEST_ASSERT_TRUE(node_topic_name(kNodeSim0, name, sizeof(name)) != 0);
  char line[48];
  std::snprintf(line, sizeof(line), "sim start %s", name);
  TEST_ASSERT_EQUAL(SimOutcome::Refused, run(s, line));
  TEST_ASSERT_FALSE(s.running());
}

// A bad setting anywhere refuses the whole line, and the simulator stays stopped.
void test_start_with_a_bad_setting_applies_nothing() {
  GateLinkSim s;
  TEST_ASSERT_EQUAL(SimOutcome::Refused, run(s, "sim start gatelink period=10 gate=30"));
  TEST_ASSERT_EQUAL(SimOutcome::Refused, run(s, "sim start gatelink period=0"));
  TEST_ASSERT_EQUAL(SimOutcome::Refused, run(s, "sim start gatelink day=59"));
  TEST_ASSERT_EQUAL(SimOutcome::Refused, run(s, "sim start gatelink colour=blue"));
  TEST_ASSERT_FALSE(s.running());
}

// R-5.2d - the first STATUS at once, on the named node, marked, under the start's ctx_id.
void test_first_status_is_immediate_and_marked() {
  GateLinkSim s;
  TEST_ASSERT_EQUAL(SimOutcome::Reply, run(s, "sim start gatelink"));
  RxMessage m;
  TEST_ASSERT_TRUE(s.poll(kT0, &m));
  TEST_ASSERT_TRUE(m.dummy);
  TEST_ASSERT_EQUAL(MsgType::Status, m.hdr.type);
  TEST_ASSERT_EQUAL_HEX8(kNodeGateLink, m.hdr.src);
  TEST_ASSERT_EQUAL_UINT32(kCtx, m.hdr.ctx_id);
  schema::GateLinkStatusV1 st;
  TEST_ASSERT_EQUAL(Status::Ok, schema::deserialize(m.payload, m.payload_len, &st));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::DebugSynthetic),
                          st.status_reason);

  // Through the real policy, as app_task hands it over: every document says so.
  PublicationPolicy p;
  Counter           c;
  const NodeInfo    info{m.hdr.src, NodeType::GateLink, false};
  p.on_status(info, m.hdr, m.payload, m.payload_len, m.rx_millis, 0, c);
  TEST_ASSERT_EQUAL(5, c.n);
  TEST_ASSERT_EQUAL(5, c.synthetic);
}

void test_status_keeps_its_period_and_never_bursts() {
  GateLinkSim s;
  TEST_ASSERT_EQUAL(SimOutcome::Reply, run(s, "sim start gatelink period=10"));
  Tally t;
  poll_span(s, kT0, kT0 + 9950, &t);
  TEST_ASSERT_EQUAL(1, t.status);
  poll_span(s, kT0 + 10000, kT0 + 10000, &t);
  TEST_ASSERT_EQUAL(2, t.status);

  // A loop() stalled for a minute sends one STATUS, not six, and resumes the cadence.
  RxMessage m;
  TEST_ASSERT_TRUE(s.poll(kT0 + 70000, &m));
  TEST_ASSERT_FALSE(s.poll(kT0 + 70050, &m));
  TEST_ASSERT_FALSE(s.poll(kT0 + 79950, &m));
  TEST_ASSERT_TRUE(s.poll(kT0 + 80000, &m));
}

// Events drive email and SMS, so an unattended source sends none unless asked.
void test_no_event_without_gate() {
  GateLinkSim s;
  TEST_ASSERT_EQUAL(SimOutcome::Reply, run(s, "sim start gatelink period=60"));
  Tally t;
  poll_span(s, kT0, kT0 + 600000, &t);
  TEST_ASSERT_EQUAL(0, t.events);
  TEST_ASSERT_TRUE(t.status >= 10);
}

// One cycle: VEHICLE_DETECTED, then the gate MOVING, OPEN_COUNTDOWN, MOVING, CLOSED, with
// event_ids 1..5 under one ctx_id (spec 7.3), and the traversal clock restarted.
void test_gate_cycle_is_five_edges_in_order() {
  GateLinkSim s;
  TEST_ASSERT_EQUAL(SimOutcome::Reply, run(s, "sim start gatelink period=3600 gate=90"));
  TEST_ASSERT_EQUAL_UINT32(kU32NotAvailable, s.status().last_traversal_age_s);
  Tally t;
  poll_span(s, kT0, kT0 + 89950, &t);
  TEST_ASSERT_EQUAL(0, t.events);
  poll_span(s, kT0 + 90000, kT0 + 150000, &t);
  TEST_ASSERT_EQUAL(5, t.events);
  const uint8_t types[]  = {0x04, 0x05, 0x05, 0x05, 0x05};
  const uint8_t states[] = {0x01, 0x02, 0x03, 0x02, 0x01};
  for (size_t i = 0; i < 5; ++i) {
    TEST_ASSERT_EQUAL_HEX8(types[i], t.types[i]);
    TEST_ASSERT_EQUAL_HEX8(states[i], t.states[i]);
    TEST_ASSERT_EQUAL_UINT32(i + 1, t.ids[i]);
  }
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GateState::Closed), s.status().gate_state);
  TEST_ASSERT_TRUE(s.status().last_traversal_age_s <= 60);
  // The next cycle starts one interval after the last.
  poll_span(s, kT0 + 150050, kT0 + 179950, &t);
  TEST_ASSERT_EQUAL(5, t.events);
  poll_span(s, kT0 + 180000, kT0 + 180000, &t);
  TEST_ASSERT_EQUAL(7, t.events);
}

// A simulated day in a minute: dark at night, charging at noon, yield rolled at midnight,
// and the pack never outside its capacity.
void test_the_model_moves_through_a_day() {
  GateLinkSim s;
  TEST_ASSERT_EQUAL(SimOutcome::Reply, run(s, "sim start gatelink period=3600 day=60"));
  RxMessage m;
  bool      saw_night = false, saw_noon = false, rolled = false;
  uint32_t  last_tod  = s.time_of_day_s();
  for (uint32_t now = kT0; now <= kT0 + 120000; now += 50) {
    (void)s.poll(now, &m);
    const auto&    st  = s.status();
    const uint32_t tod = s.time_of_day_s();
    TEST_ASSERT_TRUE(st.bms_soc <= 100);
    if (tod < 4 * 3600) {
      saw_night = true;
      TEST_ASSERT_EQUAL_UINT16(0, st.pv_w);
      TEST_ASSERT_EQUAL_UINT8(0, st.charge_state);
      TEST_ASSERT_TRUE(st.batt_ma < 0);
    }
    if (tod > 11 * 3600 && tod < 13 * 3600) {
      saw_noon = true;
      TEST_ASSERT_TRUE(st.pv_w > 50);
      TEST_ASSERT_TRUE(st.charge_state >= 3);
    }
    if (tod < last_tod) {
      rolled = true;
      TEST_ASSERT_TRUE(st.yield_yest > 0);
      TEST_ASSERT_TRUE(st.yield_today <= 1);
    }
    last_tod = tod;
  }
  TEST_ASSERT_TRUE(saw_night);
  TEST_ASSERT_TRUE(saw_noon);
  TEST_ASSERT_TRUE(rolled);
  // The node's own clock ran in real seconds: two minutes, not two days.
  TEST_ASSERT_UINT32_WITHIN(1, 120, s.status().uptime_s);
}

void test_stop_stops() {
  GateLinkSim s;
  TEST_ASSERT_EQUAL(SimOutcome::Reply, run(s, "sim start gatelink"));
  TEST_ASSERT_EQUAL(SimOutcome::Reply, run(s, "sim stop"));
  RxMessage m;
  TEST_ASSERT_FALSE(s.poll(kT0 + 100000, &m));
  TEST_ASSERT_EQUAL(SimOutcome::Reply, run(s, "sim show"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_not_a_sim_line_is_left_alone);
  RUN_TEST(test_start_refuses_a_bench_node_and_an_unknown_one);
  RUN_TEST(test_start_with_a_bad_setting_applies_nothing);
  RUN_TEST(test_first_status_is_immediate_and_marked);
  RUN_TEST(test_status_keeps_its_period_and_never_bursts);
  RUN_TEST(test_no_event_without_gate);
  RUN_TEST(test_gate_cycle_is_five_edges_in_order);
  RUN_TEST(test_the_model_moves_through_a_day);
  RUN_TEST(test_stop_stops);
  return UNITY_END();
}
