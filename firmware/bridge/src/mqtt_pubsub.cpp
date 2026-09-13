// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// MqttTransport over PubSubClient. Task BF-12; D5, Impl Plan 4.3.

#include "mqtt_pubsub.h"

#include "net_policy.h"

namespace bridge {

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
  return client_.publish(msg.topic, reinterpret_cast<const uint8_t*>(msg.payload),
                         msg.payload_len, msg.retain);
}

bool PubSubTransport::subscribe(const char* topic, uint8_t qos) {
  return client_.subscribe(topic, qos);
}

}  // namespace bridge
