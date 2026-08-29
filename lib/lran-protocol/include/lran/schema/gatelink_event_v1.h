// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Schema 0x11 - GateLink event v1, 16 bytes. Spec 7.3.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/types.h"

namespace lran {
namespace schema {

inline constexpr size_t kGateLinkEventV1Len = 16;

// spec 7.3 - bit 0 of event_flags marks a follow-up refining an earlier event. When
// a detection fires before its direction is classified, the node emits the event
// immediately with direction = UNDETERMINED and then a second event with this bit
// set and the SAME event_id once classification completes. That preserves "alert on
// the first edge, no dead window" while still delivering direction.
inline constexpr uint8_t kEventFlagFollowUp = 0x01;

struct GateLinkEventV1 {
  uint8_t  event_type  = 0;  // 0   spec 8.9
  uint8_t  event_flags = 0;  // 1   bit 0 = follow-up; 7:1 reserved
  uint8_t  hold_source = 0;  // 2   spec 8.4, the hold in force when it occurred
  uint8_t  direction   = 0;  // 3   spec 8.6, UNDETERMINED if not yet classified
  uint8_t  gate_state  = 0;  // 4   spec 8.3, at the moment of the event
  uint8_t  input_bits  = 0;  // 5   raw inputs at the moment of the event
  uint16_t detail      = 0;  // 6   event-specific
  // 8  Monotonic per node per boot, never reused within a ctx_id. This is the
  //    deduplication key on the non-retained MQTT path (spec 16.3): an EVENT has no
  //    ACK and may be retransmitted after a CAD backoff, so HA needs a value it can
  //    use to recognise a repeat.
  uint32_t event_id = 0;
  uint32_t uptime_s = 0;  // 12  node uptime when the event occurred
};

Status serialize(const GateLinkEventV1& v, uint8_t* out, size_t cap, size_t* written);
Status deserialize(const uint8_t* in, size_t len, GateLinkEventV1* out);

}  // namespace schema
}  // namespace lran
