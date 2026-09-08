// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
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
#include <climits>

#include "board_config.h"
#include "pa_config.h"
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

  // Output power alone, without disturbing frequency, SF or CR.
  //
  // Exists for the responder's echo. Round-trip PER is only a measurement of the link
  // if both legs run at the same power - echoing at the ceiling while the probe went
  // out at the SX1262 floor makes the return leg 6 dB stronger and flatters the
  // metric at exactly the low-power points the D33 ceiling forces the sweep to care
  // about. The value must still come from the clamp; this only applies it.
  int16_t set_power(int8_t conducted_dbm);

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

  // Handoff 6 requirement 7 - the PA configuration behind the power now applied.
  //
  // Derived from the power the driver ACCEPTED, not from the one it was asked for:
  // set_power() and apply() record it only after RadioLib returns success, so a
  // rejected setting cannot leave a config here describing air that was never used.
  // Before the first successful set the entry reads `in_range = false`, which
  // format_pa_config() prints as `pa_entry=none` rather than as entry 0.
  PaConfig applied_pa_config() const {
    return pa_config_for(last_power_dbm_, kPaOptimize);
  }

  float last_rssi_dbm() const { return last_rssi_; }
  float last_snr_db() const { return last_snr_; }

  // R8 - retune only, leaving SF, CR and power untouched.
  //
  // Separate from apply() because the ambient survey walks 130 frequencies and
  // changes nothing else, and because apply() would re-assert an output power the
  // survey must never engage: R8 LISTENS. Nothing in survey mode transmits.
  int16_t set_frequency(uint32_t freq_hz);

  // R8 - INSTANTANEOUS RSSI, not the RSSI of the last packet.
  //
  // The distinction is the whole measurement. RadioLib's default getRSSI() reads the
  // packet-status register, which holds the last *received frame's* RSSI and is not
  // cleared - the same trap poll() fell into with getPacketLength(). On an empty bin
  // there is no frame, so it would report a stale reading from a bin scanned minutes
  // ago, and the survey would be a picture of its own memory.
  //
  // Requires the radio to be IN RECEIVE: the SX1262's GET_RSSI_INST is meaningless in
  // standby. Callers go through start_receive() first and honour the settle time.
  float instant_rssi_dbm();

  // spec 12.3 - CAD. Exposed now because the sweep needs it in R4 and because W9
  // asks for the 222-byte frame to be checked against the CAD/backoff window while
  // the boards are out. The backoff POLICY is not here; this is the primitive.
  int16_t scan_channel();

  bool ready() const { return ready_; }

 private:
  bool  ready_     = false;
  float last_rssi_ = 0.0f;
  float last_snr_  = 0.0f;

  // Deliberately outside the SX1262's -9..+22 range until a power is successfully
  // applied, so applied_pa_config() reports "no entry" rather than a plausible one.
  int8_t last_power_dbm_ = INT8_MIN;
};

}  // namespace rangetest
