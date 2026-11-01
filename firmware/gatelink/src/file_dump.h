// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The console's `file` command and the fault log (PRD R-5.3g, R-5.4e; plan 6.4), the logic
// without the card.
//
// ARDUINO-FREE, so the native suite drives it. log_task owns the card side.
//
// A DUMP ENDS IN A CRC, because the console loses bytes from the middle of lines (engineering
// log, 2026-10-08). A damaged dump has to be visible. The CRC is zlib's, so a saved dump
// checks with `python3 -c "import zlib,sys; print(hex(zlib.crc32(open(sys.argv[1],'rb').read())))"`.

#pragma once

#include <cstddef>
#include <cstdint>

namespace gatelink {

// The fault log's two files. /log/fault.txt rotates to /log/fault.old.txt at this size, so
// the card holds at most about 512 KB of it, and at about 100 B a line that is thousands of
// faults, not a record of normal running. /bms.log is that.
inline constexpr const char* kFaultLogPath    = "/log/fault.txt";
inline constexpr const char* kFaultLogOldPath = "/log/fault.old.txt";
inline constexpr uint32_t    kFaultLogCapBytes = 256UL * 1024;

class Crc32 {
 public:
  void     update(const uint8_t* data, size_t n);
  uint32_t value() const { return ~state_; }

 private:
  uint32_t state_ = 0xFFFFFFFFu;
};

// Finds where the last `lines` lines of a file start, from chunks fed back to front. A
// file's final newline ends its last line and does not count as a line of its own.
class TailScan {
 public:
  TailScan(uint32_t size, uint32_t lines) : pos_(size), size_(size), lines_(lines) {}

  // Feeds the `n` bytes that end at the position next_chunk() described. True once found.
  bool feed(const uint8_t* chunk, size_t n);

  // The next chunk to read, up to `cap` bytes ending at the current position. 0 at the start
  // of the file.
  size_t next_chunk(size_t cap, uint32_t* offset) const;

  uint32_t start() const { return start_; }

 private:
  uint32_t pos_;
  uint32_t size_;
  uint32_t lines_;
  uint32_t seen_  = 0;
  uint32_t start_ = 0;
  bool     found_ = false;
};

// One fault-log line, without the newline: `uptime_s=<n> <text>`. 0 if it does not fit.
size_t format_fault_line(char* out, size_t cap, uint32_t uptime_s, const char* text);

}  // namespace gatelink
