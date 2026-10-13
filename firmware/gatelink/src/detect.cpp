// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "detect.h"

#include "gate_state.h"

namespace gatelink {

using lran::Direction;
using lran::EventType;

namespace {
constexpr uint8_t kSensors = kInSafety | kInExit;
}  // namespace

lran::schema::GateLinkEventV1 Detector::event(EventType type, const GateContext& g) const {
  lran::schema::GateLinkEventV1 ev;
  ev.event_type  = static_cast<uint8_t>(type);
  ev.hold_source = static_cast<uint8_t>(g.hold_source);
  ev.gate_state  = static_cast<uint8_t>(g.gate_state);
  ev.input_bits  = g.input_bits;
  ev.uptime_s    = g.uptime_s;
  return ev;
}

// The detection's first event, or a held-open alert raised by a later edge of it. Either way
// it takes a new event_id, and the follow-up refines this one.
void Detector::first_event(uint32_t now_ms, const GateContext& g, const DetectParams& p,
                           Direction dir, PushList* out) {
  const bool limited = g.held && p.alert_repeat_s != 0 && alert_ever_ &&
                       now_ms - last_alert_ms_ < p.alert_repeat_s * 1000;
  const bool alert   = g.held && !limited;
  suppressed_        = suppressed_ || limited;
  if (alert) {
    alerted_       = true;
    alert_ever_    = true;
    last_alert_ms_ = now_ms;
  }
  event_type_ = alert ? EventType::VehicleWhileHeldOpen : EventType::VehicleDetected;
  event_id_   = ids_->next();

  Push push;
  push.kind            = Push::Kind::Event;
  push.event           = event(event_type_, g);
  push.event.event_id  = event_id_;
  push.event.direction = static_cast<uint8_t>(dir);
  out->add(push);
}

void Detector::resolve(Direction dir, bool traversal, uint32_t now_ms, const GateContext& g,
                       PushList* out) {
  last_direction_ = dir;
  if (traversal) {
    traversal_seen_ = true;
    traversal_ms_   = now_ms;
  }
  Push follow;
  follow.kind              = Push::Kind::Event;
  follow.event             = event(event_type_, g);
  follow.event.event_flags = 0x01;  // spec 7.3 - a follow-up
  follow.event.event_id    = event_id_;
  follow.event.direction   = static_cast<uint8_t>(dir);
  out->add(follow);

  Push status;
  status.kind   = Push::Kind::Status;
  status.reason = lran::StatusReason::VehicleDetected;
  out->add(status);

  phase_ = quiet_ ? Phase::Idle : Phase::Settling;
}

void Detector::update(uint8_t inputs, uint32_t now_ms, const GateContext& g,
                      const DetectParams& p, PushList* out) {
  const uint8_t bits = inputs & kSensors;
  // A sensor already asserted at boot is not an edge: nothing saw it rise.
  const uint8_t rise = primed_ ? static_cast<uint8_t>(bits & ~prev_) : 0;
  prev_              = bits;
  primed_            = true;
  if (bits != 0) {
    quiet_ = false;
  } else if (!quiet_) {
    quiet_          = true;
    quiet_since_ms_ = now_ms;
  }
  // R-3.2.4b - any rising edge while held alerts, once per detection.
  const bool late_alert = rise != 0 && g.held && !alerted_ && !suppressed_;

  switch (phase_) {
    case Phase::Idle:
      if (rise == 0) break;
      alerted_    = false;
      suppressed_ = false;
      start_ms_   = now_ms;
      first_event(now_ms, g, p, Direction::Undetermined, out);
      if (rise == kSensors) {
        resolve(Direction::Undetermined, true, now_ms, g, out);  // PRD 3.2.2, simultaneous
      } else {
        first_  = rise;
        phase_  = Phase::Armed;
      }
      break;

    case Phase::Armed:
      if ((rise & ~first_) != 0) {
        resolve(first_ == kInExit ? Direction::Exit : Direction::Entry, true, now_ms, g, out);
        break;
      }
      if (late_alert) first_event(now_ms, g, p, Direction::Undetermined, out);
      if (now_ms - start_ms_ >= p.window_ms ||
          (quiet_ && now_ms - quiet_since_ms_ >= p.idle_ms)) {
        resolve(Direction::Undetermined, false, now_ms, g, out);
      }
      break;

    case Phase::Settling:
      // Resolved already, so the alert carries the direction and needs no follow-up.
      if (late_alert) first_event(now_ms, g, p, last_direction_, out);
      if (quiet_) phase_ = Phase::Idle;
      break;
  }
}

uint8_t Detector::detect_flags(uint8_t inputs) const {
  const bool active = phase_ != Phase::Idle;
  return static_cast<uint8_t>(((inputs & kInSafety) != 0 ? 0x01 : 0) |
                              ((inputs & kInExit) != 0 ? 0x02 : 0) |
                              (phase_ == Phase::Armed ? 0x04 : 0) |
                              (active && alerted_ ? 0x08 : 0) |
                              (active && suppressed_ ? 0x10 : 0));
}

uint32_t Detector::last_traversal_age_s(uint32_t now_ms) const {
  return traversal_seen_ ? (now_ms - traversal_ms_) / 1000 : UINT32_MAX;
}

}  // namespace gatelink
