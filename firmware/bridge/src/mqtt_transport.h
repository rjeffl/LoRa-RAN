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
// SIZES. 1536 bytes of payload against MQTT_MAX_PACKET_SIZE of 2048. The largest
// thing this firmware builds is a discovery config, and discovery does NOT pass
// through this queue - it is generated inside mqtt_task (Impl Plan 5.2) and published
// from there, so the queue is sized for state and diagnostics rather than for the one
// payload that dwarfs them. 32 slots x ~1640 bytes is ~52 KB of static RAM, which is
// the cost of never blocking a producer.
//
// IT HAS MOVED THREE TIMES AND EACH MOVE HAD A MEASUREMENT BEHIND IT. 512 to 768 at BF-19,
// when lran/bridge/diag/state gained all 21 spec 14.1 counters by name - 681 bytes with
// every one at UINT32_MAX. 768 to 1024 at BF-32, when two documents arrived close to
// the old line at once: the radio document gained a sixth queue's pair of counters, and
// spec 16.7's config/ack for a whole table left about twenty bytes spare on the bridge's
// own block. Twenty bytes is not headroom - GateLink's counted 25 parameters (W10) would
// have crossed it, and the failure is a DROPPED publication, so the entity keeps a stale
// value and nothing says why. 1024 to 1536 at BF-33, when D59 gave the bridge the six
// PHY rows: test_config failed on the bridge's get_all answer, which an offline count put
// at about 1.2 KB for 21 rows, and config/state close behind it.
//
// test_diag and test_config are the checks. Each builds the worst document its table can
// produce and fails here rather than at the broker.
//
// A payload that does not fit is REFUSED AND COUNTED, never truncated. Truncated JSON
// is worse than absent: Home Assistant logs a parse error against a topic that looks
// alive, and the entity keeps its last good value while the real one drifts away.
// ---------------------------------------------------------------------------

inline constexpr size_t kMaxTopicLen   = 96;
inline constexpr size_t kMaxPayloadLen = 1536;

struct PublishMessage {
  char   topic[kMaxTopicLen]     = {0};
  char   payload[kMaxPayloadLen] = {0};
  size_t payload_len             = 0;
  bool   retain                  = false;
  uint8_t qos                    = 0;  // spec 16.3 requires QoS 1 for events
};

// Fills `out` from C strings, refusing rather than truncating. False also when the
// retain flag or the QoS would violate spec 16.3 (`lran/<node>/event/` is never retained,
// and is QoS 1) - see net_policy.h for why that check lives on the path rather than in a
// comment.
bool make_publish(PublishMessage* out, const char* topic, const char* payload,
                  bool retain, uint8_t qos);

// ---------------------------------------------------------------------------
// The inbound direction - BF-18 and BF-32. `lran/<node>/cmd/<action>/set` and
// `lran/<node>/config/set` are subscribed today (spec 16.2, 16.7.2); B5 adds the HEX
// request.
//
// STILL SMALL, AND NO LONGER TINY. It was 64 bytes while a command payload was the only
// thing that arrived - `PRESS`, `ON`, a small decimal (net_policy.h) - and the asymmetry
// against the outbound cap was the point: nothing the bridge ACTS on should arrive in a
// large buffer. Spec 16.7.2's `config/set` is JSON and breaks that bargain, so the cap is
// now what the largest ACCEPTABLE one needs: kMaxConfigSetEntries names at the longest
// length config_json.h will read, each with a value, is about 450 bytes.
//
// IT IS NOT THE OUTBOUND CAP, and that is deliberate. A payload arriving larger than this
// is refused whole rather than parsed, so the bound still limits what the bridge can be
// asked to act on - it has moved, not gone.
//
// REFUSED, NOT TRUNCATED, like every other size limit here. A truncated topic
// addresses something real and wrong, and a truncated payload is a different command.
// ---------------------------------------------------------------------------

inline constexpr size_t kMaxInboundPayloadLen = 512;

struct InboundMessage {
  char   topic[kMaxTopicLen]            = {0};
  char   payload[kMaxInboundPayloadLen] = {0};
  size_t payload_len                    = 0;
};

// Fills `out` from what the transport received, refusing rather than truncating.
// `payload` need not be NUL-terminated; `out->payload` always is.
bool make_inbound(InboundMessage* out, const char* topic, const uint8_t* payload,
                  size_t payload_len);

// Where a received publication goes. The transport calls this from its own loop(),
// which runs on mqtt_task - so an implementation must not block, and must not do
// anything a queue send cannot do.
class MqttInbound {
 public:
  virtual ~MqttInbound()                             = default;
  virtual void on_message(const InboundMessage& msg) = 0;
};

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

  // Where loop() delivers what arrives. Null detaches. Set it BEFORE the first
  // connect_once(): a subscription made before a sink exists delivers to nothing,
  // and the broker will not send a retained command again to make up for it.
  //
  // THIS IS THE SEAM'S ONE CONCESSION TO PubSubClient. The header above says a
  // method that exists because PubSubClient needs it called means the seam has
  // leaked - this one exists because PubSubClient's callback is a bare function
  // pointer with no user context, so the implementation needs somewhere to keep the
  // sink. espMqttClient takes a std::function and would not need it. Kept because
  // the alternative is every caller knowing which library is underneath, which is
  // the thing the seam is for.
  virtual void set_inbound(MqttInbound* sink) = 0;

  // Connection attempts and failures, for the diagnostic topics. Not the publish
  // counts: those belong to the queue accounting, which is where a drop is visible.
  virtual uint32_t connect_attempts() const = 0;
  virtual uint32_t connect_failures() const = 0;
};

}  // namespace bridge
