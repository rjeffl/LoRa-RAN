// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Internal packet loopback. Task BF-27; Impl Plan 6.6.3; PRD R-5.4a.
//
// ARDUINO-FREE. With the loopback on, a frame the dummy or the simulator injects is first
// encoded to wire bytes by the library's codec, then run through spec 14's receive ladder,
// and app_task gets what the ladder delivered rather than what the tool built. The path from
// the codec's output to the publication policy then runs on the target, in the image that
// ships, with no radio.
//
// ITS OWN LADDER, NOT lora_task's. The live ladder belongs to lora_task and is not locked
// (rx_ladder.h). A second one here costs its reassembly pool, about 4 KB of static storage,
// and leaves the live ladder's counters, and so the diagnostics HA reads, counting only what
// the radio heard. It reads the registry's PeerKeys, which are fixed before the tasks start.
//
// `loopback corrupt` flips one bit of the next frame, so the refusal and the named spec 14
// status can be seen on the console as well as the pass. Only the internal half of R-5.4a
// is here: an RF echo needs the bridge to answer PING (spec 6.6), which it does not.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/counters.h"
#include "lran/types.h"
#include "queues.h"
#include "rx_ladder.h"

namespace bridge {

enum class LoopbackOutcome : uint8_t {
  NotMine,  // not a `loopback` line
  Reply,    // answered on the console
  Refused,  // the reply says why
};

class Loopback {
 public:
  Loopback();

  // The registry. Until this is set every frame is refused at stage 9a, as the live ladder
  // refuses one before registry_begin().
  void set_keys(const PeerKeys* keys) { ladder_.set_auth(nullptr, keys); }

  //   loopback on | off | corrupt | show
  LoopbackOutcome handle(const char* line, char* reply, size_t cap);

  bool enabled() const { return enabled_; }

  // Encodes *inout to a frame, runs it through this loopback's ladder, and on delivery
  // rewrites *inout from what the ladder delivered, keeping `dummy` and `rx_millis`. False
  // when the frame did not come back whole; `reply` names the stage.
  bool pass(RxMessage* inout, uint32_t now_ms, char* reply, size_t cap);

  uint32_t passed() const { return passed_; }
  uint32_t refused() const { return refused_; }
  lran::Status last_status() const { return last_; }

 private:
  lran::Counters counters_;
  RxLadder       ladder_;
  bool           enabled_      = false;
  bool           corrupt_next_ = false;
  uint32_t       passed_       = 0;
  uint32_t       refused_      = 0;
  lran::Status   last_         = lran::Status::Ok;
};

}  // namespace bridge
