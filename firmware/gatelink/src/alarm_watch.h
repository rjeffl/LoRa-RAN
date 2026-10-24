// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// FIRE and the hard-shutdown latch, sent on their edges. PRD G-9, G-10, R-3.2.5a; spec 8.7,
// 8.9; D83.
//
// ARDUINO-FREE, so the native suite drives it. app_task owns one, feeds it the debounced
// inputs each pass, and queues the pushes it asks for.
//
// THE FIRST READ IS A BASELINE, NOT AN EDGE. An input already asserted at boot belongs to
// R-3.5i's re-send, which lora_task makes after the BOOT status (spec 10.7, D72). Taking it
// here as well would send the alert twice in the same context.
//
// BOTH EDGES SEND A STATUS, because input_bits and node_flags bit 5 carry the state and HA
// reads it from the STATUS. The rising edge also sends the EVENT, which is what HA routes:
// FIRE_ASSERTED at the highest priority, on its own topic (spec 8.9).

#pragma once

#include <cstdint>

#include "detect.h"

namespace gatelink {

class AlarmWatch {
 public:
  // One pass over the debounced inputs, spec 7.2.1's input_bits. Returns true when IN5 or
  // IN6 changed, having added to `out` a STATUS for each change, with reason FIRE or
  // HARD_SHUTDOWN, and an EVENT for each rising edge. `ids` draws the events' ids.
  bool update(uint8_t inputs, const GateContext& g, EventIds* ids, PushList* out);

 private:
  bool    seen_ = false;
  uint8_t last_ = 0;  // IN5 and IN6 at the previous pass
};

}  // namespace gatelink
