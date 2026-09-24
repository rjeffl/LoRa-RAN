// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// MqttTransport over PubSubClient. Task BF-12; D5, Impl Plan 4.3.

#include "mqtt_pubsub.h"

#include "net_policy.h"

namespace bridge {
namespace {

// The one instance, for the callback trampoline. See mqtt_pubsub.h on why a file
// static rather than a context pointer: PubSubClient's callback signature has no
// room for one.
PubSubTransport* g_instance = nullptr;

}  // namespace

// Static. PubSubClient hands back the topic as a NUL-terminated C string and the
// payload as a length-counted buffer that is NOT terminated - make_inbound() is
// written for exactly that pair.
void PubSubTransport::dispatch(char* topic, uint8_t* payload, unsigned int len) {
  PubSubTransport* self = g_instance;
  if (self == nullptr) return;
  if (self->inbound_ == nullptr) {
    ++self->inbound_refused_;
    return;
  }
  InboundMessage msg;
  if (!make_inbound(&msg, topic, payload, len)) {
    ++self->inbound_refused_;  // refused, never truncated
    return;
  }
  self->inbound_->on_message(msg);
}

void PubSubTransport::set_inbound(MqttInbound* sink) { inbound_ = sink; }

bool PubSubTransport::begin(const MqttConfig& cfg) {
  cfg_ = cfg;
  if (cfg_.host == nullptr || cfg_.client_id == nullptr) {
    return false;
  }
  client_.setServer(cfg_.host, cfg_.port);

  // MQTT_MAX_PACKET_SIZE is 1024 from the build flags (D5, and the trap with its own
  // line in this node's CLAUDE.md). setBufferSize asks the library to honour it at
  // runtime too; a false return means the allocation failed and discovery configs
  // would vanish with no error, which is worth refusing the whole begin() for.
  if (!client_.setBufferSize(MQTT_MAX_PACKET_SIZE)) {
    return false;
  }

  // Keepalive of 30 s against PubSubClient's default 15. The bridge publishes on
  // node cadence - a poll per node every 1-5 minutes - so it is often silent for
  // longer than the default window, and a keepalive shorter than the traffic pattern
  // produces reconnects that look like network faults.
  client_.setKeepAlive(30);

  // Socket timeout bounds how long a publish or a connect can occupy mqtt_task.
  // This task may block; that is what the queue in front of it is for. The one that
  // may not is lora_task, which never touches this object.
  client_.setSocketTimeout(5);

  // The trampoline, installed last so a begin() that failed above leaves no callback
  // pointing at a half-configured transport. A SECOND INSTANCE IS REFUSED rather than
  // allowed to steal the first's callbacks - a bridge with two transports is a defect,
  // and one that silently delivered every command to the wrong one would be a hard
  // afternoon.
  if (g_instance != nullptr && g_instance != this) {
    return false;
  }
  g_instance = this;
  client_.setCallback(&PubSubTransport::dispatch);
  return true;
}

bool PubSubTransport::connect_once() {
  if (client_.connected()) {
    return true;
  }
  ++attempts_;

  // LWT, set at connect time because that is when the broker records it. Without it
  // "the bridge is gone" and "the bridge has nothing to say" look identical to Home
  // Assistant, and every node entity keeps its last value indefinitely (spec 16.5).
  const bool ok =
      cfg_.will_topic != nullptr
          ? client_.connect(cfg_.client_id, cfg_.user, cfg_.password, cfg_.will_topic,
                            0 /* will qos */, cfg_.will_retain, cfg_.will_payload)
          : client_.connect(cfg_.client_id, cfg_.user, cfg_.password);
  if (!ok) {
    ++failures_;
  }
  return ok;
}

bool PubSubTransport::connected() { return client_.connected(); }

void PubSubTransport::loop() { client_.loop(); }

bool PubSubTransport::publish(const PublishMessage& msg) {
  // Checked again here, not only in make_publish. This is the last point before the
  // wire, and spec 16.3's rule is one where being caught twice costs nothing and
  // being missed once sends a 2 AM SMS about last Tuesday.
  if (!retain_is_permitted(msg.topic, msg.retain)) {
    return false;
  }
  // msg.qos IS NOT HONOURED. PubSubClient 2.8 publishes at QoS 0 only, so an event leaves
  // at QoS 0 although spec 16.3 requires QoS 1 and make_publish() has checked the request.
  // Over TCP to a LAN broker, QoS 0 loses a message only when the connection drops during
  // the publish. D5's designated fallback, espMqttClient, publishes at QoS 1; the move is
  // recorded under the bridge handoff's Open (BF-25).
  return client_.publish(msg.topic, reinterpret_cast<const uint8_t*>(msg.payload),
                         msg.payload_len, msg.retain);
}

bool PubSubTransport::subscribe(const char* topic, uint8_t qos) {
  return client_.subscribe(topic, qos);
}

}  // namespace bridge
