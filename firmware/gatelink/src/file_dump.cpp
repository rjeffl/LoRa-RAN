// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// See file_dump.h.

#include "file_dump.h"

#include <cstdio>

namespace gatelink {

void Crc32::update(const uint8_t* data, size_t n) {
  uint32_t c = state_;
  for (size_t i = 0; i < n; ++i) {
    c ^= data[i];
    // Bitwise, with no table: the console reads a file at the speed of USB CDC, and 1 KB of
    // table is RAM this node keeps for nothing else.
    for (int b = 0; b < 8; ++b) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  state_ = c;
}

size_t TailScan::next_chunk(size_t cap, uint32_t* offset) const {
  if (found_ || pos_ == 0) return 0;
  const size_t n = pos_ < cap ? pos_ : cap;
  *offset        = pos_ - static_cast<uint32_t>(n);
  return n;
}

bool TailScan::feed(const uint8_t* chunk, size_t n) {
  if (found_) return true;
  for (size_t i = n; i > 0; --i) {
    const uint32_t at = pos_ - static_cast<uint32_t>(n - i) - 1;  // the byte's file offset
    if (chunk[i - 1] != '\n' || at + 1 == size_) continue;
    if (++seen_ == lines_) {
      start_ = at + 1;
      found_ = true;
      return true;
    }
  }
  pos_ -= static_cast<uint32_t>(n);
  if (pos_ == 0) found_ = true;  // fewer lines than asked for: the whole file, start_ 0
  return found_;
}

size_t format_fault_line(char* out, size_t cap, uint32_t uptime_s, const char* text) {
  const int w = std::snprintf(out, cap, "uptime_s=%lu %s", static_cast<unsigned long>(uptime_s), text);
  return w > 0 && static_cast<size_t>(w) < cap ? static_cast<size_t>(w) : 0;
}

}  // namespace gatelink
