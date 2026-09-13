// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-12 - reconnect timing, topic grammar, and the two rules a publication must
// pass before it reaches the wire.
//
// WHAT THIS COVERS. The policy half of R-3.2b and spec 16.3: the backoff sequence,
// the exact topic strings, and the refusal of anything that would violate either the
// payload cap or the never-retain-an-event rule.
//
// WHAT IT CANNOT. That WiFi.begin() was actually called, that PubSubClient set the
// LWT, that a reconnect happened on a real AP. Those need the target and B2 bench
// time; the bench does not need to be spent on arithmetic.

#include <unity.h>

#include <cstring>

#include "mqtt_transport.h"
#include "net_policy.h"

using namespace bridge;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Reconnect backoff
// ---------------------------------------------------------------------------

// The first connect waits for nothing. A bridge that sleeps before its first
// association turns every power cut into a delay nobody asked for (R-4.2b).
void test_first_connect_does_not_wait() {
  TEST_ASSERT_EQUAL_UINT32(0, reconnect_delay_ms(0));
}

// 1, 2, 4, 8, 16, then the cap. Deterministic because there is one bridge and a
// recognizable sequence in a log is worth more here than lockstep avoidance.
void test_backoff_doubles_then_caps() {
  TEST_ASSERT_EQUAL_UINT32(1000, reconnect_delay_ms(1));
  TEST_ASSERT_EQUAL_UINT32(2000, reconnect_delay_ms(2));
  TEST_ASSERT_EQUAL_UINT32(4000, reconnect_delay_ms(3));
  TEST_ASSERT_EQUAL_UINT32(8000, reconnect_delay_ms(4));
  TEST_ASSERT_EQUAL_UINT32(16000, reconnect_delay_ms(5));
  TEST_ASSERT_EQUAL_UINT32(kReconnectMaxDelayMs, reconnect_delay_ms(6));
}

// An outage lasting days must not push the delay past the cap or wrap it. A wrapped
// delay is a bridge that retries every few milliseconds for the rest of the outage,
// or one that waits weeks - both from the same defect.
void test_backoff_never_exceeds_the_cap() {
  for (uint32_t attempt = 6; attempt < 500; ++attempt) {
    TEST_ASSERT_EQUAL_UINT32(kReconnectMaxDelayMs, reconnect_delay_ms(attempt));
  }
  TEST_ASSERT_EQUAL_UINT32(kReconnectMaxDelayMs, reconnect_delay_ms(0xFFFFFFFFu));
}

// The cap is short enough that a recovery is noticed within one poll interval at the
// fleet's own cadence (1-5 minutes).
void test_cap_is_shorter_than_the_fleet_poll_interval() {
  TEST_ASSERT_TRUE(kReconnectMaxDelayMs < 60000);
}

// ---------------------------------------------------------------------------
// Topic grammar - spec 16.1. Exact tokens; this is the interface HA sees.
// ---------------------------------------------------------------------------

void test_availability_topic_is_exactly_the_grammar() {
  char buf[kMaxTopicLen];
  TEST_ASSERT_EQUAL_size_t(std::strlen("lran/bridge/availability"),
                           topic_availability("bridge", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("lran/bridge/availability", buf);

  TEST_ASSERT_TRUE(topic_availability("gatelink", buf, sizeof(buf)) > 0);
  TEST_ASSERT_EQUAL_STRING("lran/gatelink/availability", buf);
}

void test_bridge_version_topic() {
  char buf[kMaxTopicLen];
  TEST_ASSERT_TRUE(topic_bridge_version(buf, sizeof(buf)) > 0);
  TEST_ASSERT_EQUAL_STRING("lran/bridge/version", buf);
}

// A truncated topic is worse than no topic: it publishes somewhere real and wrong.
// So a buffer that cannot hold the whole thing yields an empty string and a zero.
void test_a_topic_that_does_not_fit_is_refused_not_truncated() {
  char small[8];
  TEST_ASSERT_EQUAL_size_t(0, topic_availability("gatelink", small, sizeof(small)));
  TEST_ASSERT_EQUAL_STRING("", small);
}

// ---------------------------------------------------------------------------
// spec 16.3 - event topics are never retained. A hard rule, on the path.
// ---------------------------------------------------------------------------

void test_event_topics_are_recognized_by_segment() {
  TEST_ASSERT_TRUE(is_event_topic("lran/gatelink/event/held_open"));
  TEST_ASSERT_TRUE(is_event_topic("lran/simnode0/event/fire"));

  // Not events: a different domain, and a node whose NAME contains the word. The
  // rule matches the segment rather than the substring, because a rule that cannot
  // tell these apart gets switched off by whoever it first annoys.
  TEST_ASSERT_FALSE(is_event_topic("lran/gatelink/gate/state"));
  TEST_ASSERT_FALSE(is_event_topic("lran/eventful/gate/state"));
  TEST_ASSERT_FALSE(is_event_topic("homeassistant/sensor/lran_gatelink/config"));
  TEST_ASSERT_FALSE(is_event_topic(nullptr));
}

void test_retain_is_refused_on_event_topics_only() {
  TEST_ASSERT_FALSE(retain_is_permitted("lran/gatelink/event/held_open", true));
  TEST_ASSERT_TRUE(retain_is_permitted("lran/gatelink/event/held_open", false));
  TEST_ASSERT_TRUE(retain_is_permitted("lran/gatelink/gate/state", true));
}

// ---------------------------------------------------------------------------
// make_publish - the gate every queued publication passes through
// ---------------------------------------------------------------------------

void test_a_normal_retained_state_message_is_accepted() {
  PublishMessage msg;
  TEST_ASSERT_TRUE(make_publish(&msg, "lran/gatelink/gate/state", "{\"state\":\"open\"}",
                                true, 0));
  TEST_ASSERT_EQUAL_STRING("lran/gatelink/gate/state", msg.topic);
  TEST_ASSERT_EQUAL_STRING("{\"state\":\"open\"}", msg.payload);
  TEST_ASSERT_EQUAL_size_t(std::strlen("{\"state\":\"open\"}"), msg.payload_len);
  TEST_ASSERT_TRUE(msg.retain);
}

// The same message on an event topic, retained, is refused outright rather than
// having its flag quietly cleared: a caller that set it believes something untrue,
// and clearing it silently leaves that belief in place (spec 16.3).
void test_a_retained_event_is_refused_rather_than_corrected() {
  PublishMessage msg;
  TEST_ASSERT_FALSE(make_publish(&msg, "lran/gatelink/event/held_open",
                                 "{\"event_id\":7}", true, 1));
  TEST_ASSERT_TRUE(make_publish(&msg, "lran/gatelink/event/held_open",
                                "{\"event_id\":7}", false, 1));
  TEST_ASSERT_EQUAL_UINT8(1, msg.qos);  // spec 16.3 requires QoS 1 for events
}

// Refused, never truncated. Truncated JSON is worse than absent: HA logs a parse
// error against a topic that looks alive and the entity keeps a stale value.
void test_an_oversized_payload_is_refused() {
  char big[kMaxPayloadLen + 8];
  std::memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';

  PublishMessage msg;
  TEST_ASSERT_FALSE(make_publish(&msg, "lran/bridge/diag/state", big, true, 0));
  TEST_ASSERT_EQUAL_size_t(0, msg.payload_len);
}

void test_an_oversized_topic_is_refused() {
  char big[kMaxTopicLen + 8];
  std::memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = '\0';

  PublishMessage msg;
  TEST_ASSERT_FALSE(make_publish(&msg, big, "{}", false, 0));
}

void test_null_arguments_are_refused() {
  PublishMessage msg;
  TEST_ASSERT_FALSE(make_publish(nullptr, "lran/bridge/diag/state", "{}", false, 0));
  TEST_ASSERT_FALSE(make_publish(&msg, nullptr, "{}", false, 0));
  TEST_ASSERT_FALSE(make_publish(&msg, "lran/bridge/diag/state", nullptr, false, 0));
}

// The availability payloads are the exact strings Home Assistant's discovery configs
// will name in payload_available / payload_not_available (spec 16.5).
void test_availability_payloads_are_the_expected_tokens() {
  TEST_ASSERT_EQUAL_STRING("online", kPayloadOnline);
  TEST_ASSERT_EQUAL_STRING("offline", kPayloadOffline);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_connect_does_not_wait);
  RUN_TEST(test_backoff_doubles_then_caps);
  RUN_TEST(test_backoff_never_exceeds_the_cap);
  RUN_TEST(test_cap_is_shorter_than_the_fleet_poll_interval);
  RUN_TEST(test_availability_topic_is_exactly_the_grammar);
  RUN_TEST(test_bridge_version_topic);
  RUN_TEST(test_a_topic_that_does_not_fit_is_refused_not_truncated);
  RUN_TEST(test_event_topics_are_recognized_by_segment);
  RUN_TEST(test_retain_is_refused_on_event_topics_only);
  RUN_TEST(test_a_normal_retained_state_message_is_accepted);
  RUN_TEST(test_a_retained_event_is_refused_rather_than_corrected);
  RUN_TEST(test_an_oversized_payload_is_refused);
  RUN_TEST(test_an_oversized_topic_is_refused);
  RUN_TEST(test_null_arguments_are_refused);
  RUN_TEST(test_availability_payloads_are_the_expected_tokens);
  return UNITY_END();
}
