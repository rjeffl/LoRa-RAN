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

inline constexpr const char* kPayloadOnline  = "online";
inline constexpr const char* kPayloadOffline = "offline";

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
