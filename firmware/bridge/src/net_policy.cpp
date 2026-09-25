// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Reconnect timing and topic grammar. Task BF-12.

#include "net_policy.h"

#include <cstdio>
#include <cstring>

#include "lran/types.h"

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

size_t topic_diag(const char* node, const char* item, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  if (node == nullptr || node[0] == '\0' || (item != nullptr && item[0] == '\0')) {
    out[0] = '\0';
    return 0;
  }
  const int n = item == nullptr ? std::snprintf(out, cap, "lran/%s/diag/state", node)
                                : std::snprintf(out, cap, "lran/%s/diag/%s/state", node, item);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t topic_diag_log(const char* node, const char* item, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  if (node == nullptr || node[0] == '\0' || item == nullptr || item[0] == '\0') {
    out[0] = '\0';
    return 0;
  }
  const int n = std::snprintf(out, cap, "lran/%s/diag/%s/log", node, item);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t topic_domain_state(const char* node, const char* domain, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  if (node == nullptr || node[0] == '\0' || domain == nullptr || domain[0] == '\0') {
    out[0] = '\0';
    return 0;
  }
  const int n = std::snprintf(out, cap, "lran/%s/%s/state", node, domain);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t topic_event(const char* node, const char* name, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  if (node == nullptr || node[0] == '\0' || name == nullptr || name[0] == '\0') {
    out[0] = '\0';
    return 0;
  }
  const int n = std::snprintf(out, cap, "lran/%s/event/%s", node, name);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
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

namespace {

// The inverse of node_topic_name(). One table would be better than two functions that
// must agree, but node_topic_name() formats and this one matches, and a shared table
// would still need each direction written. test_net asserts they agree.
bool node_id_from_token(const char* tok, size_t len, uint8_t* out) {
  struct Row {
    const char* token;
    uint8_t     id;
  };
  static const Row kRows[] = {
      {"gatelink", 0x01}, {"welllink", 0x02}, {"simnode0", 0xF0},
      {"simnode1", 0xF1}, {"simnode2", 0xF2}, {"simnode3", 0xF3},
  };
  for (const Row& r : kRows) {
    if (std::strlen(r.token) == len && std::strncmp(r.token, tok, len) == 0) {
      *out = r.id;
      return true;
    }
  }
  return false;
}

// Spec 8.1's names, lowercased. See net_policy.h on why the bridge names these and why
// renaming one after B4 is breaking.
bool cmd_from_action(const char* tok, size_t len, uint8_t* out) {
  struct Row {
    const char* action;
    lran::Cmd   cmd;
  };
  static const Row kRows[] = {
      {"nop", lran::Cmd::Nop},
      {"open", lran::Cmd::Open},
      {"close", lran::Cmd::Close},
      {"hold_open", lran::Cmd::HoldOpen},
      {"release_hold", lran::Cmd::ReleaseHold},
      {"request_status", lran::Cmd::RequestStatus},
      {"request_config", lran::Cmd::RequestConfig},
      {"set_debug_mode", lran::Cmd::SetDebugMode},
      {"set_relay_dry_run", lran::Cmd::SetRelayDryRun},
      {"set_bms_polling", lran::Cmd::SetBmsPolling},
      {"reboot", lran::Cmd::Reboot},
  };
  for (const Row& r : kRows) {
    if (std::strlen(r.action) == len && std::strncmp(r.action, tok, len) == 0) {
      *out = static_cast<uint8_t>(r.cmd);
      return true;
    }
  }
  return false;
}

// `out` receives the segment's start and length. False when there is no `n`th segment.
bool segment(const char* topic, size_t index, const char** out, size_t* len) {
  const char* p = topic;
  for (size_t i = 0; i < index; ++i) {
    const char* slash = std::strchr(p, '/');
    if (slash == nullptr) return false;
    p = slash + 1;
  }
  const char* slash = std::strchr(p, '/');
  *out              = p;
  *len              = slash == nullptr ? std::strlen(p) : static_cast<size_t>(slash - p);
  return true;
}

bool segment_is(const char* topic, size_t index, const char* want) {
  const char* seg = nullptr;
  size_t      len = 0;
  if (!segment(topic, index, &seg, &len)) return false;
  return std::strlen(want) == len && std::strncmp(seg, want, len) == 0;
}

// A bounded decimal. Refuses an empty field, a non-digit, and anything above `max` -
// see net_policy.h on why an unreadable payload is refused rather than defaulted.
bool parse_uint(const char* p, size_t len, uint32_t max, uint32_t* out) {
  if (len == 0 || len > 5) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < len; ++i) {
    if (p[i] < '0' || p[i] > '9') return false;
    v = v * 10 + static_cast<uint32_t>(p[i] - '0');
    if (v > max) return false;
  }
  *out = v;
  return true;
}

}  // namespace

bool parse_cmd_topic(const char* topic, CmdTopic* out) {
  if (topic == nullptr || out == nullptr) return false;

  // Exactly `lran/<node>/cmd/<action>/set` - five segments, matched by position. A
  // broker that honours the filter cannot deliver anything else, but the bridge does
  // not act on a gate command because a broker was well behaved.
  const char* node_seg   = nullptr;
  size_t      node_len   = 0;
  const char* action_seg = nullptr;
  size_t      action_len = 0;
  if (!segment_is(topic, 0, kTopicRoot) || !segment_is(topic, 2, "cmd") ||
      !segment_is(topic, 4, "set")) {
    return false;
  }
  const char* extra     = nullptr;
  size_t      extra_len = 0;
  if (segment(topic, 5, &extra, &extra_len)) return false;  // a sixth segment
  if (!segment(topic, 1, &node_seg, &node_len)) return false;
  if (!segment(topic, 3, &action_seg, &action_len)) return false;

  CmdTopic parsed;
  if (!node_id_from_token(node_seg, node_len, &parsed.node_id)) return false;
  if (!cmd_from_action(action_seg, action_len, &parsed.cmd)) return false;
  *out = parsed;
  return true;
}

bool parse_config_topic(const char* topic, ConfigTopic* out) {
  if (topic == nullptr || out == nullptr) return false;

  // Exactly `lran/<node>/config/set` - four segments, matched by position, same rule
  // as parse_cmd_topic: a well-behaved broker cannot deliver anything else, and the
  // bridge does not act on a configuration change because a broker was well behaved.
  if (!segment_is(topic, 0, kTopicRoot) || !segment_is(topic, 2, "config") ||
      !segment_is(topic, 3, "set")) {
    return false;
  }
  const char* extra     = nullptr;
  size_t      extra_len = 0;
  if (segment(topic, 4, &extra, &extra_len)) return false;  // a fifth segment

  const char* node_seg = nullptr;
  size_t      node_len = 0;
  if (!segment(topic, 1, &node_seg, &node_len)) return false;

  ConfigTopic parsed;
  if (std::strlen(kTopicBridgeToken) == node_len &&
      std::strncmp(kTopicBridgeToken, node_seg, node_len) == 0) {
    parsed.is_bridge = true;
    *out             = parsed;
    return true;
  }
  if (!node_id_from_token(node_seg, node_len, &parsed.node_id)) return false;
  *out = parsed;
  return true;
}

bool parse_vedirect_topic(const char* topic, VedirectTopic* out) {
  if (topic == nullptr || out == nullptr) return false;
  // Five segments, matched by position, as parse_config_topic() does.
  if (!segment_is(topic, 0, kTopicRoot) || !segment_is(topic, 2, "vedirect")) return false;
  const char* extra     = nullptr;
  size_t      extra_len = 0;
  if (segment(topic, 5, &extra, &extra_len)) return false;  // a sixth segment

  VedirectTopic parsed;
  if (segment_is(topic, 3, "hex") && segment_is(topic, 4, "request")) {
    parsed.kind = VedirectInbound::HexRequest;
  } else if (segment_is(topic, 3, "write_enable") && segment_is(topic, 4, "set")) {
    parsed.kind = VedirectInbound::WriteEnableSet;
  } else {
    return false;
  }
  const char* node_seg = nullptr;
  size_t      node_len = 0;
  if (!segment(topic, 1, &node_seg, &node_len)) return false;
  if (!node_id_from_token(node_seg, node_len, &parsed.node_id)) return false;
  *out = parsed;
  return true;
}

size_t topic_vedirect(const char* node, const char* rest, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  if (node == nullptr || node[0] == '\0' || rest == nullptr || rest[0] == '\0') {
    out[0] = '\0';
    return 0;
  }
  const int n = std::snprintf(out, cap, "lran/%s/vedirect/%s", node, rest);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t topic_config(const char* node, const char* leaf, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  if (node == nullptr || node[0] == '\0' || leaf == nullptr || leaf[0] == '\0') {
    out[0] = '\0';
    return 0;
  }
  const int n = std::snprintf(out, cap, "lran/%s/config/%s", node, leaf);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

bool parse_cmd_payload(const char* payload, size_t len, uint8_t* arg, uint16_t* arg2) {
  if (arg == nullptr || arg2 == nullptr) return false;
  *arg  = 0;
  *arg2 = 0;
  if (payload == nullptr || len == 0) return true;  // a button press

  auto equals = [&](const char* want) {
    return std::strlen(want) == len && std::strncmp(payload, want, len) == 0;
  };
  if (equals("PRESS")) return true;
  if (equals("OFF")) return true;
  if (equals("ON")) {
    *arg = 1;
    return true;
  }

  const char* comma = static_cast<const char*>(std::memchr(payload, ',', len));
  const size_t first_len = comma == nullptr ? len : static_cast<size_t>(comma - payload);

  uint32_t a = 0;
  if (!parse_uint(payload, first_len, UINT8_MAX, &a)) return false;
  *arg = static_cast<uint8_t>(a);

  if (comma == nullptr) return true;
  uint32_t b = 0;
  if (!parse_uint(comma + 1, len - first_len - 1, UINT16_MAX, &b)) return false;
  *arg2 = static_cast<uint16_t>(b);
  return true;
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

bool bench_topic_forbidden(const char* topic) {
  static const char kPrefix[] = "lran/simnode";
  const size_t      plen      = sizeof(kPrefix) - 1;
  if (topic == nullptr || std::strncmp(topic, kPrefix, plen) != 0) return false;
  // The segment after the node token, matched whole, as is_event_topic() does.
  const char* node_end = std::strchr(topic + plen, '/');
  if (node_end == nullptr) return false;
  static const char* const kForbidden[] = {"/gate/", "/detect/", "/battery/", "/solar/",
                                           "/event/"};
  for (const char* seg : kForbidden) {
    if (std::strncmp(node_end, seg, std::strlen(seg)) == 0) return true;
  }
  return false;
}

}  // namespace bridge
