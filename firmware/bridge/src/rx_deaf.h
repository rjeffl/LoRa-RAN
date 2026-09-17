// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// How long the transmit path held the radio out of receive. The receive path's 1 s knee
// (engineering log, 2026-09-17); root rule 4.
//
// ARDUINO-FREE AND HEADER-ONLY, for the reason rx_wake.h gives: lora_link.cpp reaches
// RadioLib and cannot be built at a desk, so the decision worth getting right is lifted
// out of it and tested in test_lora.
//
// WHY THIS EXISTS. Every transmission is preceded by a CAD, and a CAD takes the radio out
// of receive whatever it reports. `cad_backoffs` counts only the CADs that came back BUSY
// (spec 12.3 - it is the channel's instrument, not the radio's), so a run where every CAD
// returned free records nothing at all and reads as a radio that never stopped listening.
// No run on record can say whether the bridge was in receive when a frame went missing.
//
// A COUNT OF CADs WOULD NOT SETTLE IT EITHER. Bridge transmissions were already tested
// against losses burst by burst and predicted nothing (engineering log, 2026-09-17), and a
// CAD count is the same kind of correlation. What decides the question is the DURATION:
// milliseconds outside receive against milliseconds in the measurement window, set beside
// the measured PER. If the radio was deaf for 0.4 % of a window that lost 2.67 % of its
// frames, the transmit path is ruled out by arithmetic rather than by a weak correlation.
//
// THE INTERVAL MEASURED IS THE REAL ONE, and this is the part a count cannot reach. It
// runs from the moment the radio leaves receive to the moment startReceive() re-arms it,
// so it carries lora_task's own latency in noticing CAD_DONE and re-arming - which is a
// suspect in its own right, and larger than the CAD's few 4.1 ms symbols if the task is
// late.
//
// MODE Down IS DELIBERATELY NOT ACCUMULATED. A radio that failed begin() is deaf, but it
// is counted by `begin_failures` and a window containing one is not a measurement. Adding
// it here would put a dead radio and a busy one in the same number.

#pragma once

#include <cstdint>

namespace bridge {

// lora_task's view of the radio, lifted out of lora_link.cpp so that deaf() below is one
// documented fact rather than a classification repeated at each call site.
enum class RadioMode : uint8_t {
  Down,      // begin() failed; retrying
  Receive,   // armed, and the only mode that can hear a frame
  Cad,       // a channel scan is running
  Transmit,  // a frame is going out
};

// True while the transmit path holds the radio out of receive. Down is excluded for the
// reason at the top of this file.
constexpr bool deaf(RadioMode m) {
  return m == RadioMode::Cad || m == RadioMode::Transmit;
}

// The milliseconds to add when the radio leaves `from` at `now_ms`, having entered it at
// `mode_start_ms`. Zero unless it was deaf, so a caller may call it on every transition
// without asking first.
//
// Unsigned subtraction, correct across a millis() wrap - the same arithmetic
// lora_link.cpp's elapsed() does, restated here so this header needs nothing from it.
constexpr uint32_t deaf_elapsed(RadioMode from, uint32_t mode_start_ms, uint32_t now_ms) {
  return deaf(from) ? (now_ms - mode_start_ms) : 0;
}

}  // namespace bridge
