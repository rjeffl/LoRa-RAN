// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// D83; see alarm_watch.h.

#include "alarm_watch.h"

#include "gate_state.h"
#include "lran/types.h"

namespace gatelink {
namespace {

struct Alarm {
  uint8_t            bit;
  lran::StatusReason reason;
  lran::EventType    event;
};

// FIRE first: when both rise in one pass, the highest-priority alert queues first (spec 8.9).
constexpr Alarm kAlarms[] = {
    {kInFire, lran::StatusReason::Fire, lran::EventType::FireAsserted},
    {kInAlarm, lran::StatusReason::HardShutdown, lran::EventType::HardShutdown},
};

}  // namespace

bool AlarmWatch::update(uint8_t inputs, const GateContext& g, EventIds* ids, PushList* out) {
  const uint8_t now = static_cast<uint8_t>(inputs & (kInFire | kInAlarm));
  if (!seen_) {
    seen_ = true;
    last_ = now;
    return false;
  }
  const uint8_t changed = static_cast<uint8_t>(now ^ last_);
  last_ = now;
  if (changed == 0) return false;

  for (const Alarm& a : kAlarms) {
    if ((changed & a.bit) == 0) continue;
    Push st;
    st.reason = a.reason;
    out->add(st);
    if ((now & a.bit) != 0) {
      Push ev;
      ev.kind              = Push::Kind::Event;
      ev.event.event_type  = static_cast<uint8_t>(a.event);
      ev.event.hold_source = static_cast<uint8_t>(g.hold_source);
      ev.event.gate_state  = static_cast<uint8_t>(g.gate_state);
      ev.event.input_bits  = g.input_bits;
      ev.event.uptime_s    = g.uptime_s;
      ev.event.event_id    = ids->next();
      out->add(ev);
    }
  }
  return true;
}

}  // namespace gatelink
