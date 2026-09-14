// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Media access for one outgoing frame: CAD before transmitting, a random backoff when the
// channel is busy, and a transmission regardless once the retries are spent. Spec 12.3.
//
// Written for the bridge by BF-16 and moved here on 2026-09-14, when simnode B0 became its
// second user. One implementation of spec 12.3 for every firmware: two copies of a backoff
// rule drift, and the drift shows up as one node starving another on air.
//
// ARDUINO-FREE, and the native build of this library is what proves it. The decision lives
// here; the CAD and the transmission are each firmware's radio driver's. Time and
// randomness are passed in, so a test walks every branch without a radio.
//
// WHY A STATE MACHINE AND NOT A DELAY. A busy channel on this property is usually a node
// talking, and often to the receiver that wants to transmit. A radio task that slept
// through its backoff would be deaf to the very frame that made it back off, for up to
// cad_retries x backoff_max_ms - 7.5 s at the defaults. So a frame waiting out a backoff
// is state, and the radio task keeps receiving while it waits.

#pragma once

#include <cstdint>

#include "lran/counters.h"

namespace lran {
namespace link {

// Root rule 8 - runtime-configurable. These are the spec 12.3 defaults.
struct MediaAccessConfig {
  uint8_t cad_retries = 5;

  // spec 12.3 v0.10 - 1500, raised from 500 by D1's SF9. A maximum PING at SF9 runs
  // 1107 ms (spec 15.1), and a window shorter than the frame it backs off for lands the
  // retry on the same transmission.
  uint32_t backoff_max_ms = 1500;
};

enum class CadResult : uint8_t {
  Free,
  Busy,

  // The CAD itself failed - a driver error or a CAD that never finished. Treated as a
  // busy channel for timing, so a faulty CAD can delay a frame but never unbounds it,
  // and NOT counted in cad_backoffs: that counter is spec 12.3's instrument for the
  // channel, and a driver fault would read as congestion that is not there.
  Error,
};

enum class TxStep : uint8_t {
  Idle,      // no frame waiting
  Wait,      // a backoff is running; keep receiving
  Cad,       // run a CAD now and report it through on_cad()
  Transmit,  // transmit now, then call finish()
};

class MediaAccess {
 public:
  void set_config(const MediaAccessConfig& cfg) { cfg_ = cfg; }
  const MediaAccessConfig& config() const { return cfg_; }

  // A frame is waiting. Its first CAD is due immediately.
  void start(uint32_t now_ms);

  // What the waiting frame needs at `now_ms`.
  //
  // PRECONDITION: now_ms is monotonic apart from wrapping. The elapsed-time arithmetic
  // is unsigned, so a millis() wrap during a backoff does not hold a frame for 49 days.
  TxStep step(uint32_t now_ms) const;

  // The outcome of the CAD step() asked for. Returns Transmit or Wait.
  //
  // spec 12.3: on a busy channel, back off random(0, backoff_max_ms) and retry up to
  // cad_retries times, then transmit regardless - an event push must not be starved
  // indefinitely. With the defaults that is six CADs and five backoffs at most.
  //
  // `random_value` is any uniformly distributed uint32; the window is taken from it
  // modulo backoff_max_ms, whose bias at 1500 over 2^32 is immaterial.
  TxStep on_cad(CadResult result, uint32_t now_ms, uint32_t random_value,
                lran::Counters* counters);

  // The frame went out, or was given up. Back to idle.
  void finish();

  bool    pending() const { return state_ != State::Idle; }
  uint8_t backoffs() const { return backoffs_; }

  // True when the frame waiting now is being transmitted over a channel CAD called busy.
  bool forced() const { return forced_; }

  // CADs that failed outright, since boot. A local diagnostic, not a spec 14.1 counter.
  uint32_t cad_errors() const { return cad_errors_; }

 private:
  enum class State : uint8_t { Idle, Waiting, Ready };

  MediaAccessConfig cfg_{};
  State             state_         = State::Idle;
  uint32_t          wait_start_ms_ = 0;
  uint32_t          wait_ms_       = 0;
  uint8_t           backoffs_      = 0;
  bool              forced_        = false;
  uint32_t          cad_errors_    = 0;
};

}  // namespace link
}  // namespace lran
