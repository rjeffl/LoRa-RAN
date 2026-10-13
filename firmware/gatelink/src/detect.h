// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL3 - vehicle detection, direction classification and the held-open alert (PRD 3.2.2-
// 3.2.4; spec 7.2.5, 7.2.9, 7.3, 8.6).
//
// ARDUINO-FREE, time passed in. app_task is the only caller.
//
// ONE DETECTION, ONE event_id. The first rising edge on SAFETY or EXIT sends an event at once,
// with direction UNDETERMINED: VEHICLE_WHILE_HELD_OPEN if the gate is held, VEHICLE_DETECTED
// otherwise (R-3.2.3c, R-3.2.4c). When the sequence resolves, a follow-up with the same
// event_id carries the direction (spec 7.3), and a STATUS carries last_direction.
//
// THE STATE MACHINE (R-3.2.3d):
//   Idle      a rising edge starts a detection. Both sensors rising in one debounced read
//             resolve it at once as UNDETERMINED.
//   Armed     the other sensor's rising edge resolves it: EXIT then SAFETY is EXIT, SAFETY
//             then EXIT is ENTRY. detect_sequence_window_ms from the first edge, or
//             detect_sequence_idle_ms with both sensors clear, resolves it as UNDETERMINED:
//             the partial traversals.
//   Settling  resolved, waiting for both sensors to clear, so the tail of one vehicle does
//             not start the next detection.
//
// A TRAVERSAL is a detection that saw both sensors (spec 7.2.9). A partial one is not, and
// leaves last_traversal_age_s alone.
//
// held_open_alert_repeat_s IS A RATE LIMIT (operator, 2026-10-13). At 0 every detection while
// held alerts. Above 0, a detection within that many seconds of the last alert sends
// VEHICLE_DETECTED instead, and sets detect_flags bit 4: a filter for the double alert from a
// vehicle towing a trailer.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "lran/schema/gatelink_event_v1.h"
#include "lran/types.h"

namespace gatelink {

// spec 7.3 - event_id is monotonic per boot. lora_task's BOOT and alarm events and app_task's
// detections draw from one counter, so no two events share an id.
class EventIds {
 public:
  uint32_t next() { return last_.fetch_add(1, std::memory_order_relaxed) + 1; }

 private:
  std::atomic<uint32_t> last_{0};
};

struct DetectParams {
  uint32_t window_ms      = 60000;  // lran-config 0x1020, detect_sequence_window_ms
  uint32_t idle_ms        = 10000;  // lran-config 0x1021, detect_sequence_idle_ms
  uint32_t alert_repeat_s = 0;      // lran-config 0x1022, held_open_alert_repeat_s
};

// The gate as the tracker reports it, which the detector copies into each event.
struct GateContext {
  bool             held        = false;
  lran::HoldSource hold_source = lran::HoldSource::None;
  lran::GateState  gate_state  = lran::GateState::Unknown;
  uint8_t          input_bits  = 0;
  uint32_t         uptime_s    = 0;
};

// What one update() asks lora_task to send.
struct Push {
  enum class Kind : uint8_t { Status, Event };
  Kind                          kind   = Kind::Status;
  lran::StatusReason            reason = lran::StatusReason::PollResponse;
  lran::schema::GateLinkEventV1 event;
};

class PushList {
 public:
  static constexpr size_t kCap = 4;
  bool add(const Push& p) {
    if (n_ == kCap) return false;
    items_[n_++] = p;
    return true;
  }
  size_t      size() const { return n_; }
  const Push& operator[](size_t i) const { return items_[i]; }

 private:
  Push   items_[kCap];
  size_t n_ = 0;
};

class Detector {
 public:
  explicit Detector(EventIds* ids) : ids_(ids) {}

  // One debounced read. Appends what is to be sent.
  void update(uint8_t inputs, uint32_t now_ms, const GateContext& g, const DetectParams& p,
              PushList* out);

  lran::Direction last_direction() const { return last_direction_; }
  // spec 7.2.5
  uint8_t detect_flags(uint8_t inputs) const;
  // spec 7.2.9 - UINT32_MAX until the first traversal. Not persisted yet: node_flags says so.
  uint32_t last_traversal_age_s(uint32_t now_ms) const;

 private:
  enum class Phase : uint8_t { Idle, Armed, Settling };

  void first_event(uint32_t now_ms, const GateContext& g, const DetectParams& p,
                   lran::Direction dir, PushList* out);
  void resolve(lran::Direction dir, bool traversal, uint32_t now_ms, const GateContext& g,
               PushList* out);
  lran::schema::GateLinkEventV1 event(lran::EventType type, const GateContext& g) const;

  EventIds* ids_;
  bool      primed_ = false;
  uint8_t   prev_   = 0;  // SAFETY and EXIT, last read
  Phase     phase_  = Phase::Idle;
  uint8_t   first_  = 0;  // the sensor that started an Armed detection
  uint32_t  start_ms_ = 0;
  bool      quiet_    = true;
  uint32_t  quiet_since_ms_ = 0;

  uint32_t        event_id_   = 0;  // the detection's event, which the follow-up refines
  lran::EventType event_type_ = lran::EventType::VehicleDetected;
  bool            alerted_    = false;  // this detection raised VEHICLE_WHILE_HELD_OPEN
  bool            suppressed_ = false;  // this detection's alert fell inside the repeat limit
  bool            alert_ever_ = false;
  uint32_t        last_alert_ms_ = 0;

  lran::Direction last_direction_ = lran::Direction::None;
  bool            traversal_seen_ = false;
  uint32_t        traversal_ms_   = 0;
};

}  // namespace gatelink
