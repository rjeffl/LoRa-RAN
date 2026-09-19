// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// See capture_config.h.

#include "capture_config.h"

#include <cstring>

namespace chancap {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Decimal digits only, no sign, no suffix. "917.2e6" or "917200000Hz" is refused rather than
// guessed at: a frequency parsed from a typo is a night of data on the wrong channel.
bool parse_hz(const char* s, size_t n, uint32_t* out) {
  if (n == 0 || n > 10) return false;
  uint64_t v = 0;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + static_cast<uint64_t>(s[i] - '0');
  }
  if (v > UINT32_MAX) return false;
  *out = static_cast<uint32_t>(v);
  return true;
}

}  // namespace

bool freq_in_band(uint32_t hz) { return hz >= kFreqMinHz && hz <= kFreqMaxHz; }

FreqChoice choose_freq(bool has_stored, uint32_t stored_hz, uint32_t default_hz) {
  if (!has_stored) return {default_hz, FreqSource::Default};
  if (!freq_in_band(stored_hz)) return {default_hz, FreqSource::Rejected};
  return {stored_hz, FreqSource::Stored};
}

const char* freq_source_name(FreqSource s) {
  switch (s) {
    case FreqSource::Default:
      return "default";
    case FreqSource::Stored:
      return "stored";
    case FreqSource::Rejected:
      return "default - the stored value was outside the band";
  }
  return "?";
}

Command parse_command(const char* line) {
  if (line == nullptr) return {CommandKind::Empty, 0};

  size_t b = 0;
  size_t e = std::strlen(line);
  while (b < e && is_space(line[b])) ++b;
  while (e > b && is_space(line[e - 1])) --e;
  if (b == e) return {CommandKind::Empty, 0};

  const char* s = line + b;
  const size_t n = e - b;

  // The verb ends at the first space.
  size_t v = 0;
  while (v < n && !is_space(s[v])) ++v;

  if (v == 4 && std::strncmp(s, "help", 4) == 0 && v == n) return {CommandKind::Help, 0};

  if (v == 4 && std::strncmp(s, "freq", 4) == 0) {
    if (v == n) return {CommandKind::ShowFreq, 0};
    size_t a = v;
    while (a < n && is_space(s[a])) ++a;
    uint32_t hz = 0;
    if (!parse_hz(s + a, n - a, &hz) || !freq_in_band(hz)) return {CommandKind::BadFreq, 0};
    return {CommandKind::SetFreq, hz};
  }

  return {CommandKind::Unknown, 0};
}

}  // namespace chancap
