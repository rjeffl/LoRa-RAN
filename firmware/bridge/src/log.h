// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The leveled log's message and its formatting. Task BF-11a; Impl Plan 5.2.
//
// WHY A QUEUE AND NOT Serial.printf. A serial write blocks its caller while the UART's
// buffer is full, and Arduino's printf takes a heap block for any line over 64 bytes.
// sched_task and lora_task are the two tasks that must not wait on either. A caller
// formats one of these on its own stack and queues it without waiting, and log_task,
// beneath everything, does the write. A full queue drops the newest line and counts it
// as `q_log_dropped`, like every other queue here (queues.h).
//
// ARDUINO-FREE, like tasks.h, so the native tests can check the formatting.
// task_runtime.cpp owns the queue and log_printf().

#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace bridge {

// Error: something failed that a reader must act on or explain. Warn: the bridge
// recovered, and a reader will want to know it happened. Info: everything else.
enum class LogLevel : uint8_t {
  Error = 0,
  Warn,
  Info,
};

// 192, because the longest line on the queue is `levers:`, which reaches 182 characters
// with every field at its widest (test_log). 160 cut it. A longer line is cut and ends
// in "...".
inline constexpr size_t kLogTextLen = 192;

// 193 bytes a slot, so kLogQueueDepth's 16 slots cost about 3.1 KB of static RAM.
struct LogMessage {
  LogLevel level = LogLevel::Info;
  char     text[kLogTextLen] = {};
};

// Formats `fmt` into msg->text and drops one trailing newline, because log_task ends
// every line itself and the call sites it replaced ended theirs. Returns false when the
// text was cut to fit. A cut line ends in "..." so that a reader can see it was cut.
bool log_format(LogMessage* msg, LogLevel level, const char* fmt, va_list ap);

// The line log_task prints. An Info line is its text alone, so a line reads as it did
// when its caller printed it. A Warn or Error line starts with `WARN ` or `ERROR `.
// Returns the length written, or 0 when `cap` cannot hold the whole line.
size_t log_render(const LogMessage& msg, char* out, size_t cap);

}  // namespace bridge
