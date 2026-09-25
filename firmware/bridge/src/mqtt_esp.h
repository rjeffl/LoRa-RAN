// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// MqttTransport over espMqttClient. Task BF-37; D5, Impl Plan 6.3.2.

#pragma once

#include <espMqttClient.h>

#include "mqtt_transport.h"

namespace bridge {

// D5's designated fallback, taken because PubSubClient 2.8 publishes at QoS 0 only and
// spec 16.3 requires QoS 1 for events. Nothing outside this file names the library.
//
// NO INTERNAL TASK. espMqttClient can run its own FreeRTOS task, and does by default on
// the ESP32. Here mqtt_task calls loop(), so the library runs where PubSubClient ran,
// at the priority and core Impl Plan 5.2.1 gives mqtt_task, and the inbound sink still
// runs on mqtt_task as mqtt_transport.h promises.
//
// ITS MEMORY IS A STATIC POOL (root rule 3). Built with EMC_USE_MEMPOOL, the library
// takes each outgoing packet from a fixed buffer instead of the heap; platformio.ini sizes
// it. A full pool refuses the publish, and mqtt_task counts that as it counted a refused
// PubSubClient publish. The library's mutex is created once, at static construction.
class EspMqttTransport final : public MqttTransport {
 public:
  EspMqttTransport();

  bool   begin(const MqttConfig& cfg) override;
  bool   connect_once() override;
  bool   connected() override;
  void   loop() override;
  bool   publish(const PublishMessage& msg) override;
  bool   subscribe(const char* topic, uint8_t qos) override;
  void   set_inbound(MqttInbound* sink) override;
  size_t pending() override;

  uint32_t connect_attempts() const override { return attempts_; }
  uint32_t connect_failures() const override { return failures_; }

  // Inbound publications the transport could not hand on: too long for an
  // InboundMessage, cut off by a dropped connection, or arriving with no sink attached.
  // Counted here because this is the last point at which they exist - root rule 4's
  // intent, applied to a publication rather than a frame.
  uint32_t inbound_refused() const { return assembler_.refused() + no_sink_; }

 private:
  void on_piece(const char* topic, const uint8_t* payload, size_t len, size_t index,
                size_t total, bool retained);

  espMqttClient    client_;
  MqttConfig       cfg_{};
  InboundAssembler assembler_;
  InboundMessage   inbound_msg_{};  // static with the transport: ~600 bytes, not stack
  MqttInbound*     inbound_  = nullptr;
  uint32_t         attempts_ = 0;
  uint32_t         failures_ = 0;
  uint32_t         no_sink_  = 0;
};

}  // namespace bridge
