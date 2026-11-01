// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// D86; see mppt_error_watch.h.

#include "mppt_error_watch.h"

#include "lran/types.h"

namespace gatelink {

bool MpptErrorWatch::update(const MpptSnapshot& m, const GateContext& g, EventIds* ids,
                            PushList* out) {
  if ((m.mppt_flags & kMpptFlagStale) != 0 || m.mppt_err == kCodeNotAvailable) return false;
  if (m.mppt_err == last_) return false;
  last_ = m.mppt_err;

  Push st;
  st.reason = lran::StatusReason::MpptError;
  out->add(st);

  Push ev;
  ev.kind              = Push::Kind::Event;
  ev.event.event_type  = static_cast<uint8_t>(lran::EventType::MpptError);
  ev.event.hold_source = static_cast<uint8_t>(g.hold_source);
  ev.event.gate_state  = static_cast<uint8_t>(g.gate_state);
  ev.event.input_bits  = g.input_bits;
  ev.event.uptime_s    = g.uptime_s;
  ev.event.detail      = m.mppt_err;  // spec 8.9: the low byte, and the high byte 0
  ev.event.event_id    = ids->next();
  out->add(ev);
  return true;
}

}  // namespace gatelink
