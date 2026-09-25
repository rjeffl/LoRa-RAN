// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// PublishMessage construction, and inbound reassembly. Tasks BF-12 and BF-37.
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
  // spec 16.3 - and at QoS 1. Refused rather than raised, for the reason the retain rule
  // is: a caller asking for QoS 0 on an event believes something untrue about it.
  if (is_event_topic(topic) && qos != 1) {
    return false;
  }
  // spec 16.6 axis 1 - a bench node never reaches a production domain or an event.
  if (bench_topic_forbidden(topic)) {
    return false;
  }

  std::memcpy(out->topic, topic, tlen + 1);
  std::memcpy(out->payload, payload, plen + 1);
  out->payload_len = plen;
  out->retain      = retain;
  out->qos         = qos;
  return true;
}

// BF-18. A payload from the wire is NOT NUL-terminated and its length is the only
// thing that bounds it, which is why this takes a length rather than a C string:
// the one place a stray strlen would read past a broker-supplied buffer.
bool make_inbound(InboundMessage* out, const char* topic, const uint8_t* payload,
                  size_t payload_len) {
  if (out == nullptr || topic == nullptr) {
    return false;
  }
  if (payload == nullptr && payload_len != 0) {
    return false;
  }

  const size_t tlen = std::strlen(topic);
  if (tlen == 0 || tlen >= kMaxTopicLen || payload_len >= kMaxInboundPayloadLen) {
    return false;  // refused, never truncated
  }

  std::memcpy(out->topic, topic, tlen + 1);
  if (payload_len != 0) {
    std::memcpy(out->payload, payload, payload_len);
  }
  out->payload[payload_len] = '\0';  // the parsers take a length, but a log does not
  out->payload_len          = payload_len;
  return true;
}

bool InboundAssembler::add(const char* topic, const uint8_t* piece, size_t len,
                           size_t index, size_t total, InboundMessage* out) {
  if (index == 0) {
    // A publication left part-assembled is one the connection dropped mid-delivery.
    if (active_) ++refused_;
    active_   = false;
    skipping_ = false;
    // make_inbound() with no payload checks the topic, and the total is checked here,
    // before any piece is copied: refused whole, never truncated.
    if (total >= kMaxInboundPayloadLen || !make_inbound(&msg_, topic, nullptr, 0)) {
      ++refused_;
      skipping_ = true;
      return false;
    }
    expected_ = total;
    active_   = true;
  } else if (skipping_) {
    return false;
  } else if (!active_ || index != msg_.payload_len) {
    // A piece that does not continue the one in progress. Nothing assembled from it
    // would be the publication the broker sent.
    if (active_) ++refused_;
    active_   = false;
    skipping_ = true;
    return false;
  }

  if (msg_.payload_len + len > expected_ || (len != 0 && piece == nullptr)) {
    ++refused_;
    active_   = false;
    skipping_ = true;
    return false;
  }
  if (len != 0) std::memcpy(msg_.payload + msg_.payload_len, piece, len);
  msg_.payload_len += len;
  if (msg_.payload_len != expected_) return false;

  msg_.payload[msg_.payload_len] = '\0';
  active_ = false;
  if (out != nullptr) *out = msg_;
  return true;
}

}  // namespace bridge
