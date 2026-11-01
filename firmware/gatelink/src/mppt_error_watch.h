// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The charger's VE.Direct ERR, sent when it changes. PRD R-3.5b; spec 7.2.2, 8.7, 8.9; D79,
// D86.
//
// ARDUINO-FREE, so the native suite drives it. app_task owns one and queues its pushes.
//
// THE FIRST READ IS NOT A BASELINE, unlike AlarmWatch's: no boot re-send covers a charger
// already in error, so an error present at boot is an edge from 0. 0xFF is "not available",
// not an error (D79), and a stale snapshot says nothing new, so neither moves the state.
//
// EACH CHANGE SENDS A STATUS and an MPPT_ERROR EVENT, with `detail` = 0 when the error
// clears (D86).

#pragma once

#include <cstdint>

#include "detect.h"
#include "ved_link.h"

namespace gatelink {

class MpptErrorWatch {
 public:
  // True when ERR changed, having added a STATUS and an EVENT to `out`.
  bool update(const MpptSnapshot& m, const GateContext& g, EventIds* ids, PushList* out);

 private:
  uint8_t last_ = 0;
};

}  // namespace gatelink
