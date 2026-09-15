// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-20 - the availability watchdog (PRD R-3.4a-d; spec 16.5, 16.6; V-B3).
//
// THE PROPERTY UNDER TEST is spec 16.5's rule and nothing looser: `offline` after
// missed_poll_threshold consecutive unanswered polls, `online` on any valid frame. The
// registry is real, so missed_polls and frames_heard move the way sched_task and app_task
// move them.
//
// WHAT THIS CANNOT COVER. The queue send and the broker (task_runtime.cpp needs FreeRTOS),
// and V-B3 itself, which stops a simnode identity on the bench.

#include <unity.h>

#include <cstring>

#include "lran/lran.h"
#include "net_policy.h"
#include "node_availability.h"
#include "refimpl_mac.h"
#include "registry.h"
#include "test_key.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

// Registry rows, by kNodeTable order.
constexpr size_t kGate = 0;
constexpr size_t kWell = 1;
constexpr size_t kSim0 = 2;

struct Rig {
  Registry             reg;
  AvailabilityWatchdog wd;

  Rig() {
    refimpl::RefKdf kdf;
    reg.load(lran_test::kTestMasterKey, &kdf);
  }
  const NodeInfo& info(size_t i) const { return reg.info_at(i); }
  void frame(size_t i, uint32_t now = 1000) {
    Header h;
    h.type   = MsgType::Status;
    h.src    = kNodeTable[i].id;
    h.dst    = kNodeBridge;
    h.ctx_id = 7;
    reg.observe(h, -80, 5, now);
  }
  void miss(size_t i, int n = 1) {
    for (int k = 0; k < n; ++k) reg.note_poll_missed(kNodeTable[i].id);
  }
  AvailabilityChange tick(size_t i) {
    return wd.evaluate(i, info(i), *reg.state(kNodeTable[i].id));
  }
};

void expect_state(const Rig& r, size_t i, Availability a) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(a), static_cast<int>(r.wd.state(i)));
}

}  // namespace

// Neither answered nor missed enough: Unknown, and nothing to publish.
void test_a_node_not_yet_judged_is_unknown_and_not_published() {
  Rig r;
  r.miss(kGate, 2);
  TEST_ASSERT_FALSE(r.tick(kGate).changed);
  expect_state(r, kGate, Availability::Unknown);
  TEST_ASSERT_FALSE(r.wd.pending(kGate));
  TEST_ASSERT_NULL(availability_payload(Availability::Unknown));
}

void test_any_valid_frame_marks_a_node_online() {
  Rig r;
  r.frame(kGate);
  const AvailabilityChange c = r.tick(kGate);
  TEST_ASSERT_TRUE(c.changed);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Availability::Unknown), static_cast<int>(c.from));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Availability::Online), static_cast<int>(c.to));
  TEST_ASSERT_TRUE(r.wd.pending(kGate));
  TEST_ASSERT_EQUAL_STRING("online", availability_payload(r.wd.state(kGate)));
}

// R-3.4b - the threshold, exactly: two misses leave the node online, the third does not.
void test_the_third_consecutive_miss_marks_a_node_offline() {
  Rig r;
  r.frame(kGate);
  r.tick(kGate);
  r.wd.clear_pending(kGate);

  r.miss(kGate, 2);
  TEST_ASSERT_FALSE(r.tick(kGate).changed);
  TEST_ASSERT_FALSE(r.wd.pending(kGate));

  r.miss(kGate);
  TEST_ASSERT_TRUE(r.tick(kGate).changed);
  expect_state(r, kGate, Availability::Offline);
  TEST_ASSERT_TRUE(r.wd.pending(kGate));
  TEST_ASSERT_EQUAL_STRING("offline", availability_payload(r.wd.state(kGate)));
}

// Consecutive: a frame between misses starts the count again (registry, Impl Plan 6.1).
void test_a_frame_between_misses_restarts_the_count() {
  Rig r;
  r.frame(kGate);
  r.tick(kGate);
  r.miss(kGate, 2);
  r.tick(kGate);
  r.frame(kGate, 2000);
  r.tick(kGate);
  r.miss(kGate, 2);
  r.tick(kGate);
  expect_state(r, kGate, Availability::Online);
}

// A node that has never answered since boot is offline once it misses the threshold.
void test_a_node_that_never_answers_goes_offline() {
  Rig r;
  r.miss(kWell, 3);
  TEST_ASSERT_TRUE(r.tick(kWell).changed);
  expect_state(r, kWell, Availability::Offline);
}

// V-B3's second half - offline, then online on the next valid frame, with no poll needed.
void test_an_offline_node_comes_back_on_its_next_frame() {
  Rig r;
  r.miss(kGate, 3);
  r.tick(kGate);
  r.wd.clear_pending(kGate);

  r.miss(kGate, 5);  // still silent: no second publication
  TEST_ASSERT_FALSE(r.tick(kGate).changed);
  TEST_ASSERT_FALSE(r.wd.pending(kGate));

  r.frame(kGate, 5000);
  TEST_ASSERT_TRUE(r.tick(kGate).changed);
  expect_state(r, kGate, Availability::Online);
}

// A frame and a later miss inside one tick. missed_polls is 1, not 0, and the frame still
// counts: this is why the watchdog reads frames_heard.
void test_a_frame_followed_by_a_miss_within_one_tick_still_counts() {
  Rig r;
  r.frame(kGate);  // heard before, so the watchdog has a frame count to compare against
  r.tick(kGate);
  r.miss(kGate, 3);
  r.tick(kGate);
  expect_state(r, kGate, Availability::Offline);
  r.frame(kGate, 5000);
  r.miss(kGate);
  r.tick(kGate);
  expect_state(r, kGate, Availability::Online);
}

// Misses that reach the threshold after a frame are the newer fact.
void test_the_threshold_reached_after_a_frame_wins() {
  Rig r;
  r.frame(kGate);
  r.miss(kGate, 3);
  r.tick(kGate);
  expect_state(r, kGate, Availability::Offline);
}

// Root rule 8 - runtime-settable. 0 is held to 1.
void test_the_threshold_is_settable_and_never_zero() {
  Rig r;
  TEST_ASSERT_EQUAL_UINT16(3, r.wd.threshold());
  r.wd.set_threshold(5);
  r.frame(kGate);
  r.miss(kGate, 4);
  r.tick(kGate);
  expect_state(r, kGate, Availability::Online);
  r.miss(kGate);
  r.tick(kGate);
  expect_state(r, kGate, Availability::Offline);

  r.wd.set_threshold(0);
  TEST_ASSERT_EQUAL_UINT16(1, r.wd.threshold());
  Rig q;
  q.wd.set_threshold(0);
  q.tick(kWell);
  expect_state(q, kWell, Availability::Unknown);  // no poll missed, so still not judged
}

// Mirrors the scheduler: production nodes are watched from boot, a bench node once heard.
void test_a_bench_node_is_watched_only_once_heard() {
  Rig r;
  for (size_t i = 0; i < kNodeCount; ++i) r.tick(i);
  TEST_ASSERT_TRUE(r.wd.watched(kGate));
  TEST_ASSERT_TRUE(r.wd.watched(kWell));
  TEST_ASSERT_FALSE(r.wd.watched(kSim0));
  TEST_ASSERT_EQUAL_UINT8(2, r.wd.watched_count());
  TEST_ASSERT_EQUAL_UINT8(0, r.wd.online_count());

  r.frame(kSim0);
  r.frame(kGate);
  for (size_t i = 0; i < kNodeCount; ++i) r.tick(i);
  TEST_ASSERT_TRUE(r.wd.watched(kSim0));
  TEST_ASSERT_EQUAL_UINT8(3, r.wd.watched_count());
  TEST_ASSERT_EQUAL_UINT8(2, r.wd.online_count());
}

// spec 16.5 - after a broker connect every judged node is published again; Unknown is not.
void test_a_broker_connect_republishes_every_known_node() {
  Rig r;
  r.frame(kGate);
  r.tick(kGate);
  r.tick(kWell);
  r.wd.clear_pending(kGate);

  r.wd.mark_known_pending();
  TEST_ASSERT_TRUE(r.wd.pending(kGate));
  TEST_ASSERT_FALSE(r.wd.pending(kWell));
}

// spec 16.6 - a bench node's availability is gated on simnode_diag_enable; production is not.
void test_bench_availability_is_published_only_with_simnode_diag_enable() {
  Rig r;
  TEST_ASSERT_TRUE(bench_publication_allowed(r.info(kGate), false));
  TEST_ASSERT_FALSE(bench_publication_allowed(r.info(kSim0), false));
  TEST_ASSERT_TRUE(bench_publication_allowed(r.info(kSim0), true));
}

// spec 16.1's tokens, and no topic for an address it does not name.
void test_node_topic_names_are_the_spec_tokens() {
  char name[16];
  TEST_ASSERT_EQUAL_UINT(8, node_topic_name(kNodeGateLink, name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("gatelink", name);
  node_topic_name(kNodeWellLink, name, sizeof(name));
  TEST_ASSERT_EQUAL_STRING("welllink", name);
  node_topic_name(kNodeSim0, name, sizeof(name));
  TEST_ASSERT_EQUAL_STRING("simnode0", name);
  node_topic_name(kNodeSim3, name, sizeof(name));
  TEST_ASSERT_EQUAL_STRING("simnode3", name);

  TEST_ASSERT_EQUAL_UINT(0, node_topic_name(0xF4, name, sizeof(name)));
  TEST_ASSERT_EQUAL_STRING("", name);
  TEST_ASSERT_EQUAL_UINT(0, node_topic_name(kNodeBridge, name, sizeof(name)));
  TEST_ASSERT_EQUAL_UINT(0, node_topic_name(kNodeGateLink, name, 8));  // no room for NUL

  char topic[40];
  node_topic_name(kNodeSim1, name, sizeof(name));
  topic_availability(name, topic, sizeof(topic));
  TEST_ASSERT_EQUAL_STRING("lran/simnode1/availability", topic);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_node_not_yet_judged_is_unknown_and_not_published);
  RUN_TEST(test_any_valid_frame_marks_a_node_online);
  RUN_TEST(test_the_third_consecutive_miss_marks_a_node_offline);
  RUN_TEST(test_a_frame_between_misses_restarts_the_count);
  RUN_TEST(test_a_node_that_never_answers_goes_offline);
  RUN_TEST(test_an_offline_node_comes_back_on_its_next_frame);
  RUN_TEST(test_a_frame_followed_by_a_miss_within_one_tick_still_counts);
  RUN_TEST(test_the_threshold_reached_after_a_frame_wins);
  RUN_TEST(test_the_threshold_is_settable_and_never_zero);
  RUN_TEST(test_a_bench_node_is_watched_only_once_heard);
  RUN_TEST(test_a_broker_connect_republishes_every_known_node);
  RUN_TEST(test_bench_availability_is_published_only_with_simnode_diag_enable);
  RUN_TEST(test_node_topic_names_are_the_spec_tokens);
  return UNITY_END();
}
