// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Reconnect timing and topic grammar. Task BF-12.
//
// ARDUINO-FREE, and that is what makes R-3.2b testable. The requirement says the
// bridge reconnects automatically and "SHALL NOT block LoRa receive while doing so";
// the half of that which is a POLICY - when to retry, how long to wait - is here and
// has host tests. The half that is a call into the WiFi stack is in wifi_link.cpp.

#pragma once

#include <cstddef>
#include <cstdint>

namespace bridge {

// ---------------------------------------------------------------------------
// Reconnect backoff
//
// Capped exponential, from 1 s to 30 s. Two failure modes bound it at each end:
//
//   Too eager - a bridge that retries every 250 ms through a router reboot spends
//   the outage hammering a WiFi stack that shares the chip with nothing else useful,
//   and the logs it produces bury whatever else happened.
//
//   Too patient - an AP that comes back 40 seconds into a 5-minute wait leaves the
//   property's telemetry dark for the remainder, for no reason. 30 s is short enough
//   that a recovery is noticed within one poll interval at the fleet's own cadence
//   (1-5 minutes, Impl Plan 6.1).
//
// NO JITTER, DELIBERATELY. Jitter exists to stop a fleet of clients retrying in
// lockstep. There is one bridge. A deterministic sequence is worth more here: a log
// showing 1, 2, 4, 8, 16, 30, 30 is a reconnect loop anyone can recognize at a glance.
// ---------------------------------------------------------------------------

inline constexpr uint32_t kReconnectFirstDelayMs = 1000;
inline constexpr uint32_t kReconnectMaxDelayMs   = 30000;

// Delay before attempt number `attempt`, counting the first retry as 1. Attempt 0 is
// the initial connect and waits not at all.
uint32_t reconnect_delay_ms(uint32_t attempt);

// ---------------------------------------------------------------------------
// Topic grammar - spec 16.1. `lran/<node>/<domain>[/<item>]/<leaf>`
//
// These are EXACT TOKENS and they are the interface Home Assistant sees (this node's
// CLAUDE.md). A topic renamed for readability is a topic that no longer matches the
// specification, and an entity that silently stops updating.
//
// Only the topics BF-12 needs are built here. Discovery (BF-23) and the publication
// policy (BF-24) own theirs, and each should extend this file rather than formatting
// a topic at its call site.
// ---------------------------------------------------------------------------

inline constexpr const char* kTopicRoot = "lran";

// `lran/<node>/availability` - retained, `online` / `offline` (spec 16.2, 16.5).
// Returns the length written, or 0 if `cap` was too small.
size_t topic_availability(const char* node, char* out, size_t cap);

// `lran/bridge/version` - retained (spec 16.2).
size_t topic_bridge_version(char* out, size_t cap);

// A node's `<node>` token (spec 16.1): `gatelink`, `welllink`, `simnode0`-`simnode3`. BF-20.
// Returns the length written, or 0 for an address spec 16.1 gives no token or a short
// `cap`. A node without a token has no topics, rather than a topic invented for it.
size_t node_topic_name(uint8_t node_id, char* out, size_t cap);

// `lran/<node>/diag/state`, or `lran/<node>/diag/<item>/state` when `item` is not null
// (spec 16.1's optional item). BF-19. Returns the length written, or 0.
size_t topic_diag(const char* node, const char* item, char* out, size_t cap);

inline constexpr const char* kPayloadOnline  = "online";
inline constexpr const char* kPayloadOffline = "offline";

// ---------------------------------------------------------------------------
// The inbound command topic - `lran/<node>/cmd/<action>/set` (spec 16.2). BF-18.
//
// THE ACTION TOKENS ARE THE BRIDGE'S TO CHOOSE, and this is where they are chosen.
// Spec 16.2.1 leaves every payload but two to the bridge under 16.4, and the same is
// true of `<action>`: 16.1 fixes the shape, not the vocabulary. They are spec 8.1's
// `cmd` names lowercased - `open`, `hold_open`, `request_status` - so one term names
// one concept from Home Assistant to the wire, and a reader needs no table to get
// from a topic to a spec row.
//
// A TOKEN PUBLISHED IS A TOKEN FROZEN. Home Assistant's entity registry remembers
// every unique_id it has seen, so these are breaking to rename after B4 builds
// discovery on them (this node's CLAUDE.md).
// ---------------------------------------------------------------------------

// What the bridge subscribes to, once, covering every node and action.
inline constexpr const char* kTopicCmdFilter = "lran/+/cmd/+/set";

// What arrived on one. `cmd` is spec 8.1's value; `arg` and `arg2` come from the
// payload (parse_cmd_payload).
struct CmdTopic {
  uint8_t node_id = 0;
  uint8_t cmd     = 0;
};

// Parses `lran/<node>/cmd/<action>/set` into an address and a spec 8.1 `cmd`. False
// for any topic that is not exactly that shape, an unknown node token, or an action
// this bridge does not name - each of which is a subscription the bridge should not
// have received, and none of which may be guessed at.
bool parse_cmd_topic(const char* topic, CmdTopic* out);

// The payload. Deliberately small, and NOT JSON: these arrive from a Home Assistant
// button or switch, whose native payloads are exactly these shapes.
//
//   empty, or `PRESS`   arg 0, arg2 0   - a button
//   `ON` / `OFF`        arg 1 / 0       - a switch
//   `N`                 arg N           - spec 8.1's arg, e.g. CLOSE's release-only
//   `N,M`               arg N, arg2 M   - e.g. SET_DEBUG_MODE's bitmask in arg2
//
// False on anything else, INCLUDING a value out of range. A payload the bridge
// cannot read is refused rather than defaulted to 0: `arg` carries REBOOT's 0xA5
// confirmation guard, and a default that silently became 0 would turn an unreadable
// payload into a command that means something else.
bool parse_cmd_payload(const char* payload, size_t len, uint8_t* arg, uint16_t* arg2);

// ---------------------------------------------------------------------------
// The retain rule, enforced where every publication passes rather than trusted.
//
// Spec 16.3 is a HARD RULE: every topic under `lran/<node>/event/` is published with
// retain clear. A retained event replays on every HA restart and on every discovery
// refresh, and these events drive email and SMS - so the failure is a 2 AM message
// about a gate that was held open last Tuesday.
//
// It is checked in the transport because "remember not to retain events" is exactly
// the kind of rule that survives review and dies in a refactor.
// ---------------------------------------------------------------------------

bool is_event_topic(const char* topic);

// False when this publication would violate spec 16.3. The caller counts the refusal
// and drops the publication rather than silently clearing the flag: a retain flag set
// on an event topic means the caller believes something untrue, and clearing it
// quietly leaves that belief in place.
bool retain_is_permitted(const char* topic, bool retain);

}  // namespace bridge
