// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The MqttTransport seam, and the publish queue's message type. Task BF-12;
// Impl Plan 4.3, D5.
//
// WHY AN INTERFACE AT ALL. D5 chose PubSubClient first and named espMqttClient the
// designated fallback. The seam is what makes that a swap rather than a rewrite, and
// the reasons to take it are already written down: PubSubClient is synchronous and
// unmaintained-adjacent, and large payloads are exactly where it is weakest.
//
// Keep the interface free of PubSubClient's shape. If a method here exists because
// PubSubClient needs it called, the seam has already leaked.

#pragma once

#include <cstddef>
#include <cstdint>

namespace bridge {

// ---------------------------------------------------------------------------
// A queued publication. app_task fills one, mqtt_task sends it.
//
// BY VALUE, like RxMessage and for the same reason: the producer's buffer is gone by
// the time mqtt_task runs, and a queue is where a pointer becomes a dangling one.
//
// SIZES. 768 bytes of payload against MQTT_MAX_PACKET_SIZE of 1024. The largest
// thing this firmware builds is a discovery config, and discovery does NOT pass
// through this queue - it is generated inside mqtt_task (Impl Plan 5.2) and published
// from there, so the queue is sized for state and diagnostics rather than for the one
// payload that dwarfs them. 32 slots x ~872 bytes is ~28 KB of static RAM, which is
// the cost of never blocking a producer.
//
// 768, not 512, since BF-19: lran/bridge/diag/state carries all 21 spec 14.1 counters
// by name, 681 bytes when every one reads UINT32_MAX. test_diag asserts it fits.
//
// A payload that does not fit is REFUSED AND COUNTED, never truncated. Truncated JSON
// is worse than absent: Home Assistant logs a parse error against a topic that looks
// alive, and the entity keeps its last good value while the real one drifts away.
// ---------------------------------------------------------------------------

inline constexpr size_t kMaxTopicLen   = 96;
inline constexpr size_t kMaxPayloadLen = 768;

struct PublishMessage {
  char   topic[kMaxTopicLen]     = {0};
  char   payload[kMaxPayloadLen] = {0};
  size_t payload_len             = 0;
  bool   retain                  = false;
  uint8_t qos                    = 0;  // spec 16.3 requires QoS 1 for events
};

// Fills `out` from C strings, refusing rather than truncating. False also when the
// retain flag would violate spec 16.3 (`lran/<node>/event/` is never retained) -
// see net_policy.h for why that check lives on the path rather than in a comment.
bool make_publish(PublishMessage* out, const char* topic, const char* payload,
                  bool retain, uint8_t qos);

// ---------------------------------------------------------------------------
// The transport.
// ---------------------------------------------------------------------------

struct MqttConfig {
  const char* host         = nullptr;
  uint16_t    port         = 1883;
  const char* user         = nullptr;
  const char* password     = nullptr;
  const char* client_id    = nullptr;

  // LWT. The broker publishes this if the bridge disappears without saying goodbye,
  // which is what makes "the bridge is gone" distinguishable from "the bridge has
  // nothing to say" (spec 16.5).
  const char* will_topic   = nullptr;
  const char* will_payload = nullptr;
  bool        will_retain  = true;
};

class MqttTransport {
 public:
  virtual ~MqttTransport() = default;

  virtual bool begin(const MqttConfig& cfg) = 0;

  // One non-blocking attempt. Returns true if connected when it returns. The CALLER
  // owns the retry cadence (net_policy.h) - a transport that sleeps inside its own
  // reconnect is a transport that cannot be paced from outside.
  virtual bool connect_once() = 0;

  virtual bool connected() = 0;

  // Service the client: keepalives, and inbound dispatch. Called from mqtt_task's
  // tick, never from lora_task.
  virtual void loop() = 0;

  virtual bool publish(const PublishMessage& msg) = 0;
  virtual bool subscribe(const char* topic, uint8_t qos) = 0;

  // Connection attempts and failures, for the diagnostic topics. Not the publish
  // counts: those belong to the queue accounting, which is where a drop is visible.
  virtual uint32_t connect_attempts() const = 0;
  virtual uint32_t connect_failures() const = 0;
};

}  // namespace bridge
