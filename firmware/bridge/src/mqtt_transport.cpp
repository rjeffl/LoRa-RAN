// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// PublishMessage construction. Task BF-12.
//
// Arduino-free on purpose: this is where a publication is accepted or refused, and
// both the size rule and spec 16.3's retain rule are testable at a desk.

#include "mqtt_transport.h"

#include <cstring>

#include "net_policy.h"

namespace bridge {

bool make_publish(PublishMessage* out, const char* topic, const char* payload,
                  bool retain, uint8_t qos) {
  if (out == nullptr || topic == nullptr || payload == nullptr) {
    return false;
  }

  const size_t tlen = std::strlen(topic);
  const size_t plen = std::strlen(payload);
  if (tlen == 0 || tlen >= kMaxTopicLen || plen >= kMaxPayloadLen) {
    return false;  // refused, never truncated
  }

  // spec 16.3 - the hard rule, enforced before the message exists rather than
  // checked at the broker's expense.
  if (!retain_is_permitted(topic, retain)) {
    return false;
  }

  std::memcpy(out->topic, topic, tlen + 1);
  std::memcpy(out->payload, payload, plen + 1);
  out->payload_len = plen;
  out->retain      = retain;
  out->qos         = qos;
  return true;
}

}  // namespace bridge
