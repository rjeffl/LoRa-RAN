// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Reconnect timing and topic grammar. Task BF-12.

#include "net_policy.h"

#include <cstdio>
#include <cstring>

namespace bridge {

uint32_t reconnect_delay_ms(uint32_t attempt) {
  if (attempt == 0) {
    return 0;  // the initial connect waits for nothing
  }
  uint32_t delay = kReconnectFirstDelayMs;
  for (uint32_t i = 1; i < attempt; ++i) {
    if (delay >= kReconnectMaxDelayMs / 2) {
      return kReconnectMaxDelayMs;  // doubling again would overshoot or overflow
    }
    delay *= 2;
  }
  return delay > kReconnectMaxDelayMs ? kReconnectMaxDelayMs : delay;
}

namespace {

// snprintf returns what it WOULD have written, so a truncated topic is detectable
// rather than published in a shortened form. A truncated topic is worse than none:
// it subscribes or publishes somewhere real and wrong.
size_t write_topic(char* out, size_t cap, const char* fmt, const char* arg) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  const int n = arg == nullptr ? std::snprintf(out, cap, "%s", fmt)
                               : std::snprintf(out, cap, fmt, arg);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

}  // namespace

size_t topic_availability(const char* node, char* out, size_t cap) {
  if (node == nullptr || node[0] == '\0') {
    if (out != nullptr && cap > 0) {
      out[0] = '\0';
    }
    return 0;
  }
  return write_topic(out, cap, "lran/%s/availability", node);
}

size_t topic_bridge_version(char* out, size_t cap) {
  return write_topic(out, cap, "lran/bridge/version", nullptr);
}

size_t node_topic_name(uint8_t node_id, char* out, size_t cap) {
  // spec 5.3's addresses, spec 16.1's tokens. The bench token counts from 0xF0.
  static const char* const kBench[] = {"simnode0", "simnode1", "simnode2", "simnode3"};
  const char*              name     = nullptr;
  if (node_id == 0x01) {
    name = "gatelink";
  } else if (node_id == 0x02) {
    name = "welllink";
  } else if (node_id >= 0xF0 && node_id <= 0xF3) {
    name = kBench[node_id - 0xF0];
  }
  if (name == nullptr) {
    if (out != nullptr && cap > 0) {
      out[0] = '\0';
    }
    return 0;
  }
  return write_topic(out, cap, name, nullptr);
}

bool is_event_topic(const char* topic) {
  if (topic == nullptr) {
    return false;
  }
  // `lran/<node>/event/...` - match the segment, not the substring. A topic with
  // "event" inside a node name is not an event topic, and a rule that cannot tell
  // the difference gets switched off by whoever it first annoys.
  static const char kPrefix[] = "lran/";
  const size_t      plen      = sizeof(kPrefix) - 1;
  if (std::strncmp(topic, kPrefix, plen) != 0) {
    return false;
  }
  const char* node_end = std::strchr(topic + plen, '/');
  if (node_end == nullptr) {
    return false;
  }
  return std::strncmp(node_end, "/event/", 7) == 0;
}

bool retain_is_permitted(const char* topic, bool retain) {
  if (!retain) {
    return true;
  }
  return !is_event_topic(topic);
}

}  // namespace bridge
