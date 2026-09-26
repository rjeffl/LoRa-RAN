// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The leveled log's formatting. Task BF-11a.

#include "log.h"

#include <cstdio>
#include <cstring>

namespace bridge {

bool log_format(LogMessage* msg, LogLevel level, const char* fmt, va_list ap) {
  msg->level = level;
  const int n = vsnprintf(msg->text, sizeof(msg->text), fmt, ap);
  if (n < 0) {
    msg->text[0] = '\0';
    return false;
  }
  const bool cut = static_cast<size_t>(n) >= sizeof(msg->text);
  if (cut) {
    std::memcpy(&msg->text[sizeof(msg->text) - 4], "...", 4);
  }
  const size_t len = std::strlen(msg->text);
  if (len > 0 && msg->text[len - 1] == '\n') msg->text[len - 1] = '\0';
  return !cut;
}

size_t log_render(const LogMessage& msg, char* out, size_t cap) {
  const char* prefix = "";
  switch (msg.level) {
    case LogLevel::Error: prefix = "ERROR "; break;
    case LogLevel::Warn:  prefix = "WARN ";  break;
    case LogLevel::Info:  break;
  }
  const int n = snprintf(out, cap, "%s%s", prefix, msg.text);
  if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
  return static_cast<size_t>(n);
}

}  // namespace bridge
