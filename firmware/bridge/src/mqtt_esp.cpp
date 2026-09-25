// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// MqttTransport over espMqttClient. Task BF-37; D5, Impl Plan 6.3.2.

#include "mqtt_esp.h"

#include <Arduino.h>

#include "net_policy.h"

namespace bridge {
namespace {

// How long connect_once() waits for the TCP connect and the broker's CONNACK together.
// PubSubClient's socket timeout was 5 s and bounded the same wait; mqtt_task may block
// this long, and lora_task never waits on it.
constexpr uint32_t kConnectTimeoutMs = 5000;

// loop() passes allowed for a dropped connection to finish closing. espMqttClient takes
// two, one to stop the socket and one to see it stopped; the rest is margin.
constexpr int kTeardownPasses = 8;

}  // namespace

EspMqttTransport::EspMqttTransport() : client_(espMqttClientTypes::UseInternalTask::NO) {}

void EspMqttTransport::set_inbound(MqttInbound* sink) { inbound_ = sink; }

bool EspMqttTransport::begin(const MqttConfig& cfg) {
  cfg_ = cfg;
  if (cfg_.host == nullptr || cfg_.client_id == nullptr) {
    return false;
  }
  client_.setServer(cfg_.host, cfg_.port);
  client_.setClientId(cfg_.client_id);
  if (cfg_.user != nullptr) {
    client_.setCredentials(cfg_.user, cfg_.password);
  }

  // Keepalive of 30 s, as BF-12 chose for PubSubClient. The bridge publishes on node
  // cadence - a poll per node every 1-5 minutes - so it is often silent for longer than
  // a 15 s window, and a keepalive shorter than the traffic produces reconnects that
  // look like network faults.
  client_.setKeepAlive(30);

  // A clean session, as PubSubClient's was. The broker keeps nothing for the bridge
  // across a reconnect; the library keeps its own unacknowledged QoS 1 publications and
  // sends them again after the CONNACK, which is what spec 16.3's QoS 1 is for here.
  client_.setCleanSession(true);

  // LWT, recorded by the broker at connect time. Without it "the bridge is gone" and
  // "the bridge has nothing to say" look identical to Home Assistant, and every node
  // entity keeps its last value indefinitely (spec 16.5). QoS 0, as before BF-37.
  if (cfg_.will_topic != nullptr && cfg_.will_payload != nullptr) {
    client_.setWill(cfg_.will_topic, 0, cfg_.will_retain, cfg_.will_payload);
  }

  // A lambda capturing one pointer fits std::function's inline storage, so installing
  // it allocates nothing.
  client_.onMessage([this](const espMqttClientTypes::MessageProperties&, const char* topic,
                           const uint8_t* payload, size_t len, size_t index, size_t total) {
    on_piece(topic, payload, len, index, total);
  });
  return true;
}

// Called from inside loop(), on mqtt_task. The library releases its mutex around this
// callback, so the sink may publish.
void EspMqttTransport::on_piece(const char* topic, const uint8_t* payload, size_t len,
                                size_t index, size_t total) {
  if (!assembler_.add(topic, payload, len, index, total, &inbound_msg_)) {
    return;
  }
  if (inbound_ == nullptr) {
    ++no_sink_;
    return;
  }
  inbound_->on_message(inbound_msg_);
}

bool EspMqttTransport::connect_once() {
  if (client_.connected()) {
    return true;
  }
  ++attempts_;

  // A connection that dropped is closed by loop(), and mqtt_task stops calling loop()
  // once connected() is false. Finish closing it here, or connect() refuses to start.
  for (int i = 0; i < kTeardownPasses && !client_.disconnected(); ++i) {
    client_.loop();
  }
  if (!client_.connected() && !client_.disconnected()) {
    ++failures_;
    return false;
  }

  // espMqttClient connects inside loop(): the TCP connect in the first pass, then the
  // CONNACK some passes later. The seam promises one attempt that reports the outcome,
  // so the wait happens here.
  if (!client_.connect()) {
    ++failures_;
    return false;
  }
  const uint32_t start = millis();
  while (!client_.connected() && !client_.disconnected()) {
    if (millis() - start >= kConnectTimeoutMs) {
      client_.disconnect(/*force=*/true);
      for (int i = 0; i < kTeardownPasses && !client_.disconnected(); ++i) {
        client_.loop();
      }
      break;
    }
    client_.loop();
    delay(10);
  }
  if (!client_.connected()) {
    ++failures_;
    return false;
  }
  return true;
}

bool EspMqttTransport::connected() { return client_.connected(); }

void EspMqttTransport::loop() { client_.loop(); }

size_t EspMqttTransport::pending() { return client_.queueSize(); }

bool EspMqttTransport::publish(const PublishMessage& msg) {
  // Checked again here, not only in make_publish. This is the last point before the
  // wire, and spec 16.3's rule is one where being caught twice costs nothing and
  // being missed once sends a 2 AM SMS about last Tuesday.
  if (!retain_is_permitted(msg.topic, msg.retain)) {
    return false;
  }
  // msg.qos is honoured since BF-37. A QoS 1 publication stays in the library's pool
  // until the broker acknowledges it, and goes again after a reconnect if it was not.
  // Zero is the library's refusal: not connected, or the pool is full.
  return client_.publish(msg.topic, msg.qos, msg.retain,
                         reinterpret_cast<const uint8_t*>(msg.payload),
                         msg.payload_len) != 0;
}

bool EspMqttTransport::subscribe(const char* topic, uint8_t qos) {
  return client_.subscribe(topic, qos) != 0;
}

}  // namespace bridge
