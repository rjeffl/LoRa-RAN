// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Whether a frame is arriving, so that a CAD is not started over it. Spec 12.3.
//
// A CAD takes the SX1262 out of receive, and a frame arriving at that moment is lost
// without a trace: no RX_DONE, no CRC error, no counter. Both radio drivers therefore
// defer a CAD while a frame is arriving, and report the deferral as the busy CAD it
// stands in for.
//
// THE PREAMBLE COUNTS, NOT ONLY THE HEADER. Until 2026-09-26 both drivers looked for
// HEADER_VALID alone. That left the window from a preamble's detection to its header's
// decode, about 88 ms at SF9, in which a CAD still destroyed the frame. A simnode's BOOT
// event was lost that way on the bench: the bridge's readback POLL ran its CAD while the
// event's preamble was on the air (bridge engineering log, 2026-09-26, *the lost BOOT
// event*). Each driver has to enable PREAMBLE_DETECTED in its receive IRQ flags for this
// class to see it.
//
// A FLAG IS TRUSTED FOR A BOUNDED TIME. A preamble with no header behind it is a false
// detection or a reception that died, and a header with no RX_DONE behind it is a
// reception that died. Either flag stays set until receive restarts, so a flag older
// than its bound reads as Stale. The driver then restarts receive, which clears the
// register, rather than deferring every CAD after it.
//
// ARDUINO-FREE, like media_access.h. The driver reads the IRQ register and passes in two
// bits and the time.

#pragma once

#include <cstdint>

#include "lran/link/radio_config.h"

namespace lran {
namespace link {

// The longest a detected preamble can precede its valid header on `phy`: the preamble
// with its 4.25-symbol sync interval (spec 15.1), the 8 symbols carrying the explicit
// header, and one symbol for rounding. 88 ms at SF9 / BW 125 kHz with 8 preamble
// symbols. Derived from the PHY in use rather than fixed, because a PHY change (spec
// 12.4) can move it and root rule 8 forbids a fixed timing constant.
uint32_t preamble_to_header_ms(const PhyConfig& phy);

enum class RxArrivalState : uint8_t {
  Idle,      // neither flag set
  Arriving,  // a flag set, and younger than its bound: defer the CAD
  Stale,     // a flag set, but older than its bound: restart receive, then CAD
};

class RxArrival {
 public:
  // `preamble_ms` from preamble_to_header_ms(); `frame_ms` covers the longest frame.
  void set_bounds(uint32_t preamble_ms, uint32_t frame_ms) {
    preamble_ms_ = preamble_ms;
    frame_ms_    = frame_ms;
  }

  // Records the two IRQ bits read at `now_ms` and says what they mean. A flag's age is
  // counted from the first read that saw it, so a flag read late is trusted for longer,
  // never for less.
  RxArrivalState observe(bool preamble, bool header, uint32_t now_ms);

  // What the last observe() recorded, aged to `now_ms`, without reading the register.
  // For callers that must not spend an SPI transaction or change what observe() saw.
  bool arriving(uint32_t now_ms) const;

  // Receive restarted or a frame was read: the register is clear.
  void reset() {
    preamble_seen_ = false;
    header_seen_   = false;
  }

 private:
  uint32_t preamble_ms_      = 0;
  uint32_t frame_ms_         = 0;
  bool     preamble_seen_    = false;
  uint32_t preamble_seen_ms_ = 0;
  bool     header_seen_      = false;
  uint32_t header_seen_ms_   = 0;
};

}  // namespace link
}  // namespace lran
