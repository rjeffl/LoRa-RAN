// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// MqttTransport over PubSubClient. Task BF-12; D5.

#pragma once

#include <PubSubClient.h>
#include <WiFi.h>

#include "mqtt_transport.h"

namespace bridge {

// The first implementation of the seam, and the one D5 chose. espMqttClient is the
// designated fallback; nothing outside this file should need to know which is in use.
class PubSubTransport final : public MqttTransport {
 public:
  bool begin(const MqttConfig& cfg) override;
  bool connect_once() override;
  bool connected() override;
  void loop() override;
  bool publish(const PublishMessage& msg) override;
  bool subscribe(const char* topic, uint8_t qos) override;
  void set_inbound(MqttInbound* sink) override;

  uint32_t connect_attempts() const override { return attempts_; }
  uint32_t connect_failures() const override { return failures_; }

  // Inbound publications the transport could not hand on: too long for an
  // InboundMessage, or arriving with no sink attached. Counted here because this is
  // the last point at which they exist - root rule 4's intent, applied to a
  // publication rather than a frame.
  uint32_t inbound_refused() const { return inbound_refused_; }

 private:
  // PubSubClient's callback is `void(char*, uint8_t*, unsigned)` with no user
  // context, so the trampoline needs a file-scope way back to the instance. There is
  // exactly one PubSubTransport in the firmware (task_runtime.cpp's g_mqtt) and it is
  // static, which is what makes this safe rather than merely convenient. A second
  // instance would silently steal the first's callbacks, so begin() refuses to
  // install one if another instance already has.
  static void dispatch(char* topic, uint8_t* payload, unsigned int len);

  WiFiClient   net_;
  PubSubClient client_{net_};
  MqttConfig   cfg_{};
  uint32_t     attempts_        = 0;
  uint32_t     failures_        = 0;
  uint32_t     inbound_refused_ = 0;
  MqttInbound* inbound_         = nullptr;
};

}  // namespace bridge
