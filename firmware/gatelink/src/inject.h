// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL3 - input injection (PRD R-5.4b; GateLink Impl Plan 6.6). A script of synthetic input
// changes, applied below the debounce so the debounce is exercised too.
//
// ARDUINO-FREE, time passed in. app_task plays a script; io_task applies its result.
//
// A SCRIPT is tokens separated by spaces:
//   +S -S +E -E +O -O +M -M +F -F +A -A   assert or release SAFETY, EXIT, OPEN (IN1),
//                                         MOVING (IN2), FIRE, ALARM, in spec 7.2.1's sense
//   +O,-M                                 a comma joins changes into one step
//   @<ms>                                 the wait before the next step
// A step with no @ before it waits inject_spacing_ms (lran-config 0x1052). The first step
// applies at once. An injected input keeps its value after the script ends, until `inject
// off` hands every input back to the hardware.

#pragma once

#include <cstddef>
#include <cstdint>

namespace gatelink {

class InjectScript {
 public:
  static constexpr size_t kMaxSteps = 16;

  // False if any token is not understood, with *bad pointing at it. A failed parse leaves
  // the script empty.
  bool parse(const char* text, const char** bad);
  void start(uint32_t now_ms);
  void stop() { running_ = false; }

  // Applies every step now due to *mask and *value. True while steps remain.
  bool service(uint32_t now_ms, uint32_t spacing_ms, uint8_t* mask, uint8_t* value);

  bool   running() const { return running_; }
  size_t next_step() const { return next_; }
  size_t steps() const { return n_; }

 private:
  struct Step {
    uint8_t  set     = 0;  // inputs asserted by the step
    uint8_t  clear   = 0;  // inputs released by the step
    bool     timed   = false;
    uint32_t wait_ms = 0;
  };
  Step     steps_[kMaxSteps];
  size_t   n_       = 0;
  size_t   next_    = 0;
  bool     running_ = false;
  uint32_t last_ms_ = 0;
};

// The bench's named scripts, or null. Each covers a case R-5.4b names or GL3 checks.
const char* inject_canned(const char* name);
// The names, space separated, for the console's help.
const char* inject_canned_names();

}  // namespace gatelink
