// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Where the simnode's console output goes. Arduino-free: the target writes to Serial, and
// the host tests capture lines so they can assert on what an operator would read.

#pragma once

#include <cstdarg>
#include <cstdio>

namespace simnode {

class Sink {
 public:
  virtual ~Sink() = default;

  // One complete line, without its newline.
  virtual void line(const char* text) = 0;
};

// A formatted line. Anything longer than the buffer is cut and marked, never silently.
inline void sink_printf(Sink* sink, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

inline void sink_printf(Sink* sink, const char* fmt, ...) {
  if (sink == nullptr) return;
  char    buf[192];
  va_list args;
  va_start(args, fmt);
  const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n >= static_cast<int>(sizeof(buf))) {
    buf[sizeof(buf) - 4] = '.';
    buf[sizeof(buf) - 3] = '.';
    buf[sizeof(buf) - 2] = '.';
  }
  sink->line(buf);
}

}  // namespace simnode
