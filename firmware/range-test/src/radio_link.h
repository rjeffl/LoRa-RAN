// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R2 - the SX1262 wrapper. RadioLib per D32, pin map injected per spec 12.2.
//
// The only file in this firmware that includes RadioLib. Everything above it talks
// to this interface, which is what lets pass 2 be a second BoardRadioConfig rather
// than a rewrite (task R2), and what keeps the sweep's raw frames (R4) and the real
// PING frames (R9) sharing one radio.
//
// EVERY RadioLib CALL IS CHECKED. Repo rule 4 is about frames, but the reasoning
// carries: an unchecked begin() on this board is the failure mode task R2 spends
// half its text on - a radio that reports success and transmits nothing.

#pragma once

#include <cstddef>
#include <cstdint>

#include "board_config.h"
#include "phy_params.h"

namespace rangetest {

class RadioLink {
 public:
  // Brings up SPI and the SX1262 and applies both the spec 12.1 fixed settings and
  // the test point. Returns 0 on success, or the RadioLib error code, negated
  // convention preserved, so the caller can print the actual number.
  int16_t begin(const BoardRadioConfig& board, const TestPoint& tp);

  // Re-applies the swept parameters (R4). The fixed settings do not change.
  int16_t apply(const TestPoint& tp);

  // Blocking transmit. Returns 0 on success.
  int16_t transmit(const uint8_t* data, size_t len);

  // Puts the radio into continuous receive.
  int16_t start_receive();

  // Non-blocking poll. Returns true when a frame was read into `buf`.
  //
  // `*out_len` receives the length. A PHY CRC failure is reported through
  // `*out_crc_error` rather than swallowed: spec 14 stage 1 is the one discard path
  // that cannot be produced at a desk, and observing it at the far edge of the walk
  // is called for by name in the engineering log's header.
  bool poll(uint8_t* buf, size_t cap, size_t* out_len, bool* out_crc_error);

  float last_rssi_dbm() const { return last_rssi_; }
  float last_snr_db() const { return last_snr_; }

  // spec 12.3 - CAD. Exposed now because the sweep needs it in R4 and because W9
  // asks for the 222-byte frame to be checked against the CAD/backoff window while
  // the boards are out. The backoff POLICY is not here; this is the primitive.
  int16_t scan_channel();

  bool ready() const { return ready_; }

 private:
  bool  ready_     = false;
  float last_rssi_ = 0.0f;
  float last_snr_  = 0.0f;
};

}  // namespace rangetest
