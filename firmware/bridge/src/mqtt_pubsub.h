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

  uint32_t connect_attempts() const override { return attempts_; }
  uint32_t connect_failures() const override { return failures_; }

 private:
  WiFiClient   net_;
  PubSubClient client_{net_};
  MqttConfig   cfg_{};
  uint32_t     attempts_ = 0;
  uint32_t     failures_ = 0;
};

}  // namespace bridge
