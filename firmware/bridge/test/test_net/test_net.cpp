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

#include <cstdio>
#include <cstring>

#include "lran/types.h"
#include "mqtt_transport.h"
#include "net_policy.h"

using namespace bridge;
using namespace lran;

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

// Spec 16.3's other half. An event asked for at QoS 0 is refused on the same ground as a
// retained one; a state topic takes either.
void test_an_event_at_qos_0_is_refused() {
  PublishMessage msg;
  TEST_ASSERT_FALSE(make_publish(&msg, "lran/gatelink/event/fire_asserted",
                                 "{\"event_id\":7}", false, 0));
  TEST_ASSERT_TRUE(make_publish(&msg, "lran/gatelink/gate/state", "{}", true, 1));
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

// ---------------------------------------------------------------------------
// BF-18 - the inbound command topic (spec 16.2).
// ---------------------------------------------------------------------------

namespace {

CmdTopic parsed_or_fail(const char* topic) {
  CmdTopic t;
  TEST_ASSERT_TRUE_MESSAGE(parse_cmd_topic(topic, &t), topic);
  return t;
}

}  // namespace

// The filter is what the bridge subscribes to, and every topic below has to match it
// at the broker before it ever reaches parse_cmd_topic().
void test_the_command_filter_is_the_grammar_with_wildcards() {
  TEST_ASSERT_EQUAL_STRING("lran/+/cmd/+/set", kTopicCmdFilter);
}

void test_a_command_topic_parses_to_an_address_and_a_spec_8_1_cmd() {
  const CmdTopic gate = parsed_or_fail("lran/gatelink/cmd/open/set");
  TEST_ASSERT_EQUAL_HEX8(0x01, gate.node_id);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(Cmd::Open), gate.cmd);

  const CmdTopic bench = parsed_or_fail("lran/simnode2/cmd/request_status/set");
  TEST_ASSERT_EQUAL_HEX8(0xF2, bench.node_id);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(Cmd::RequestStatus), bench.cmd);

  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(Cmd::HoldOpen),
                         parsed_or_fail("lran/gatelink/cmd/hold_open/set").cmd);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(Cmd::Reboot),
                         parsed_or_fail("lran/welllink/cmd/reboot/set").cmd);
}

// The node token is the one node_topic_name() publishes. Two functions that must
// agree, asserted to agree rather than trusted to.
void test_the_parsed_node_token_is_the_published_one() {
  const uint8_t kIds[] = {0x01, 0x02, 0xF0, 0xF1, 0xF2, 0xF3};
  for (uint8_t id : kIds) {
    char name[32];
    TEST_ASSERT_TRUE(node_topic_name(id, name, sizeof(name)) > 0);
    char topic[96];
    std::snprintf(topic, sizeof(topic), "lran/%s/cmd/nop/set", name);
    TEST_ASSERT_EQUAL_HEX8(id, parsed_or_fail(topic).node_id);
  }
}

// Every one of these is a subscription the bridge should not have received, and a
// gate command is not acted on because a broker was well behaved.
void test_a_topic_outside_the_grammar_is_refused() {
  CmdTopic t;
  TEST_ASSERT_FALSE(parse_cmd_topic(nullptr, &t));
  TEST_ASSERT_FALSE(parse_cmd_topic("lran/gatelink/cmd/open/set", nullptr));
  TEST_ASSERT_FALSE(parse_cmd_topic("lran/gatelink/cmd/open", &t));        // no leaf
  TEST_ASSERT_FALSE(parse_cmd_topic("lran/gatelink/cmd/open/state", &t));  // wrong leaf
  TEST_ASSERT_FALSE(parse_cmd_topic("lran/gatelink/diag/open/set", &t));   // wrong domain
  TEST_ASSERT_FALSE(parse_cmd_topic("nral/gatelink/cmd/open/set", &t));    // wrong root
  TEST_ASSERT_FALSE(parse_cmd_topic("lran/gatelink/cmd/open/set/x", &t));  // a sixth
  TEST_ASSERT_FALSE(parse_cmd_topic("lran/bridge/cmd/open/set", &t));      // not a node
  TEST_ASSERT_FALSE(parse_cmd_topic("lran/simnode9/cmd/open/set", &t));    // no such node
  TEST_ASSERT_FALSE(parse_cmd_topic("lran/gatelink/cmd/OPEN/set", &t));    // case matters
  TEST_ASSERT_FALSE(parse_cmd_topic("lran/gatelink/cmd/opening/set", &t)); // not a prefix
  TEST_ASSERT_FALSE(parse_cmd_topic("lran/gatelink/cmd//set", &t));        // empty action
}

void test_a_command_payload_carries_arg_and_arg2() {
  uint8_t  arg  = 9;
  uint16_t arg2 = 9;

  TEST_ASSERT_TRUE(parse_cmd_payload("", 0, &arg, &arg2));  // a button
  TEST_ASSERT_EQUAL_UINT8(0, arg);
  TEST_ASSERT_EQUAL_UINT16(0, arg2);

  TEST_ASSERT_TRUE(parse_cmd_payload("PRESS", 5, &arg, &arg2));
  TEST_ASSERT_EQUAL_UINT8(0, arg);

  TEST_ASSERT_TRUE(parse_cmd_payload("ON", 2, &arg, &arg2));
  TEST_ASSERT_EQUAL_UINT8(1, arg);
  TEST_ASSERT_TRUE(parse_cmd_payload("OFF", 3, &arg, &arg2));
  TEST_ASSERT_EQUAL_UINT8(0, arg);

  // spec 8.1 - REBOOT's confirmation guard is 0xA5, which arrives as decimal 165.
  TEST_ASSERT_TRUE(parse_cmd_payload("165", 3, &arg, &arg2));
  TEST_ASSERT_EQUAL_HEX8(0xA5, arg);

  // SET_DEBUG_MODE's bitmask lives in arg2.
  TEST_ASSERT_TRUE(parse_cmd_payload("0,4096", 6, &arg, &arg2));
  TEST_ASSERT_EQUAL_UINT8(0, arg);
  TEST_ASSERT_EQUAL_UINT16(4096, arg2);
}

// Refused, not defaulted to 0. `arg` carries REBOOT's guard, so a payload that
// silently became 0 would turn something unreadable into a different command.
void test_an_unreadable_command_payload_is_refused() {
  uint8_t  arg  = 0;
  uint16_t arg2 = 0;
  TEST_ASSERT_FALSE(parse_cmd_payload("on", 2, &arg, &arg2));      // case matters
  TEST_ASSERT_FALSE(parse_cmd_payload("1.5", 3, &arg, &arg2));
  TEST_ASSERT_FALSE(parse_cmd_payload("-1", 2, &arg, &arg2));
  TEST_ASSERT_FALSE(parse_cmd_payload("256", 3, &arg, &arg2));     // above uint8
  TEST_ASSERT_FALSE(parse_cmd_payload("1,65536", 7, &arg, &arg2)); // above uint16
  TEST_ASSERT_FALSE(parse_cmd_payload("1,", 2, &arg, &arg2));      // empty second field
  TEST_ASSERT_FALSE(parse_cmd_payload(",1", 2, &arg, &arg2));      // empty first field
  TEST_ASSERT_FALSE(parse_cmd_payload("1,2,3", 5, &arg, &arg2));
  TEST_ASSERT_FALSE(parse_cmd_payload("open", 4, &arg, &arg2));
  TEST_ASSERT_FALSE(parse_cmd_payload("1", 1, nullptr, &arg2));
}

// A wire payload is not NUL-terminated and its length is all that bounds it.
void test_an_inbound_message_is_copied_by_length_and_terminated() {
  const uint8_t  wire[] = {'O', 'N', 0xFF, 0xFF};  // trailing bytes are not ours
  InboundMessage msg;
  TEST_ASSERT_TRUE(make_inbound(&msg, "lran/gatelink/cmd/open/set", wire, 2));
  TEST_ASSERT_EQUAL_STRING("lran/gatelink/cmd/open/set", msg.topic);
  TEST_ASSERT_EQUAL_size_t(2, msg.payload_len);
  TEST_ASSERT_EQUAL_STRING("ON", msg.payload);

  // An empty payload is a button press, not an error.
  TEST_ASSERT_TRUE(make_inbound(&msg, "lran/gatelink/cmd/open/set", nullptr, 0));
  TEST_ASSERT_EQUAL_size_t(0, msg.payload_len);
  TEST_ASSERT_EQUAL_STRING("", msg.payload);
}

// Refused, never truncated: a truncated topic addresses something real and wrong, and
// a truncated payload is a different command.
void test_an_oversized_inbound_message_is_refused() {
  InboundMessage msg;
  uint8_t        big[kMaxInboundPayloadLen + 1] = {0};
  TEST_ASSERT_FALSE(make_inbound(&msg, "lran/gatelink/cmd/open/set", big, sizeof(big)));
  TEST_ASSERT_FALSE(
      make_inbound(&msg, "lran/gatelink/cmd/open/set", big, kMaxInboundPayloadLen));

  char long_topic[kMaxTopicLen + 8];
  std::memset(long_topic, 'a', sizeof(long_topic) - 1);
  long_topic[sizeof(long_topic) - 1] = '\0';
  TEST_ASSERT_FALSE(make_inbound(&msg, long_topic, big, 1));

  TEST_ASSERT_FALSE(make_inbound(nullptr, "lran/x/cmd/open/set", big, 1));
  TEST_ASSERT_FALSE(make_inbound(&msg, nullptr, big, 1));
  TEST_ASSERT_FALSE(make_inbound(&msg, "", big, 1));
  TEST_ASSERT_FALSE(make_inbound(&msg, "lran/x/cmd/open/set", nullptr, 1));
}

// The asymmetry is deliberate: nothing the bridge ACTS on arrives in a large buffer.
void test_the_inbound_buffer_is_much_smaller_than_the_outbound_one() {
  TEST_ASSERT_TRUE(kMaxInboundPayloadLen < kMaxPayloadLen);
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
  RUN_TEST(test_an_event_at_qos_0_is_refused);
  RUN_TEST(test_an_oversized_payload_is_refused);
  RUN_TEST(test_an_oversized_topic_is_refused);
  RUN_TEST(test_null_arguments_are_refused);
  RUN_TEST(test_availability_payloads_are_the_expected_tokens);
  RUN_TEST(test_the_command_filter_is_the_grammar_with_wildcards);
  RUN_TEST(test_a_command_topic_parses_to_an_address_and_a_spec_8_1_cmd);
  RUN_TEST(test_the_parsed_node_token_is_the_published_one);
  RUN_TEST(test_a_topic_outside_the_grammar_is_refused);
  RUN_TEST(test_a_command_payload_carries_arg_and_arg2);
  RUN_TEST(test_an_unreadable_command_payload_is_refused);
  RUN_TEST(test_an_inbound_message_is_copied_by_length_and_terminated);
  RUN_TEST(test_an_oversized_inbound_message_is_refused);
  RUN_TEST(test_the_inbound_buffer_is_much_smaller_than_the_outbound_one);
  return UNITY_END();
}
