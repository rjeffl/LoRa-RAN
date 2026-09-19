// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Which frequency a listen-only receiver samples, and the one console command that sets
// it. D1 frequency change brief 5; M25.
//
// ARDUINO-FREE AND HOST-TESTED. The frequency is the one fact that decides which channel a
// capture file describes, so the rules that choose it are tested at a desk rather than
// discovered in a ten-hour file.
//
// WHY RUNTIME, NOT A BUILD FLAG. Brief 5 rotates each receiver across the candidate
// frequencies on successive nights, so that a board's own bias cannot pass for a channel's.
// Three boards times three frequencies as build environments is nine images, and flashing
// the wrong one is silent until someone reads CHAN-BOOT. So the image is one per board, the
// frequency is stored in NVS, and every boot states it in CHAN-BOOT.
//
// A CHANGE TAKES EFFECT AT THE NEXT BOOT, NEVER MID-RUN. A capture file is split into
// segments at CHAN-BOOT lines (tools/simctl/rssi_analyze.py), so a frequency that changed
// without one would pool two channels into one segment under the first one's header.

#pragma once

#include <cstddef>
#include <cstdint>

namespace chancap {

// The receiver's 125 kHz channel must sit inside 902-928 MHz, so its centre may come no
// closer than half a bandwidth to either edge. Listen-only, so this is about measuring the
// band rather than any emission rule: a centre outside it measures something else.
inline constexpr uint32_t kHalfBandwidthHz = 62500;
inline constexpr uint32_t kFreqMinHz       = 902000000u + kHalfBandwidthHz;
inline constexpr uint32_t kFreqMaxHz       = 928000000u - kHalfBandwidthHz;

bool freq_in_band(uint32_t hz);

// Where the frequency came from, printed at boot beside it.
enum class FreqSource : uint8_t {
  Default,   // nothing stored; the fleet's kPhy.freq_hz
  Stored,    // NVS
  Rejected,  // NVS held a value outside the band; the default was used instead
};

struct FreqChoice {
  uint32_t   hz;
  FreqSource source;
};

// The stored value if there is one and it is in the band, else the default.
FreqChoice choose_freq(bool has_stored, uint32_t stored_hz, uint32_t default_hz);

const char* freq_source_name(FreqSource s);

// One console line. Commands are few on purpose: this image has no job but sampling.
//
//   freq            print the frequency this boot is sampling
//   freq <hz>       store <hz> for the next boot, then reboot
//   help
enum class CommandKind : uint8_t {
  Empty,
  ShowFreq,
  SetFreq,
  Help,
  BadFreq,   // `freq` with an argument that is not a whole number of hertz in the band
  Unknown,
};

struct Command {
  CommandKind kind;
  uint32_t    hz;  // SetFreq only
};

// Leading and trailing spaces, a trailing CR and a trailing LF are ignored.
Command parse_command(const char* line);

}  // namespace chancap
