// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Home Assistant MQTT discovery. Task BF-23; Impl Plan 4.4; PRD R-3.3b/c/d; spec 16.1.
//
// ARDUINO-FREE, so that what Home Assistant will read is checked at a desk. A discovery
// config that is wrong does not fail loudly: the entity appears and never updates, or
// does not appear and nothing says why.
//
// WHAT IS PUBLISHED, AND WHEN. One device per registered node plus the bridge, on boot
// AND ON EVERY BROKER RECONNECT (R-3.3b). The reconnect path is the one that gets
// skipped in development and the one that runs unattended, so it is not a separate code
// path here: `publish_all` is called from the tick after each connect, exactly as the
// diagnostic documents are, and there is no "first time" flag to get wrong.
//
// THE CONFIGS ARE RETAINED, and that has a consequence worth knowing before the first
// bench run: a retained discovery config survives a reflash, and Home Assistant's entity
// registry remembers every unique_id it has ever seen (firmware/bridge/CLAUDE.md).
// Develop against the dev HA VM and dev broker until B6.
//
// NO GATE KNOWLEDGE (BG-2). A node's entity set comes from its NodeType through the
// tables in discovery.cpp, and its buttons come from command_allowed() - the same
// capability filter the command path uses. Nothing here branches on a node being a gate.
//
// ABBREVIATED KEYS, DELIBERATELY. Home Assistant's discovery schema defines short forms
// (`stat_t`, `uniq_id`, `dev_cla`) and they are as stable as the long ones. They are used
// here because kMaxPayloadLen is 768 and the long forms put a node's sensor config within
// a hundred bytes of it - and growing that buffer costs RAM in every publish queue slot
// (Impl Plan 4.3.2 grew it once already). The `~` base topic earns its place twice over
// for the same reason.

#pragma once

#include <cstddef>
#include <cstdint>

#include "registry.h"

namespace bridge {

// The discovery prefix, spec 16.1's default. Not runtime-configurable: a bridge whose
// prefix is wrong produces no entities at all, which is a walk to the board either way.
inline constexpr const char* kDiscoveryPrefix = "homeassistant";

// Long enough for the longest config a table row below produces, with the device block.
// A config that does not fit is refused, counted and skipped rather than truncated, so
// this being too small costs entities rather than corrupting them.
inline constexpr size_t kMaxDiscoveryPayload = 768;
static_assert(kMaxDiscoveryPayload <= 768, "a discovery config rides a PublishMessage");

// ---------------------------------------------------------------------------
// One entity, as a table row.
//
// EVERY FIELD THAT IS NULL IS OMITTED FROM THE CONFIG, rather than written as JSON null.
// Home Assistant reads an absent key as "use the default" and an explicit null as a
// value, so a row that leaves `unit` unset must produce a config with no `unit_of_meas`.
// ---------------------------------------------------------------------------

struct EntityDesc {
  // The `unique_id` suffix and the discovery topic's object id. Prefixed per node into
  // `lran_<node>_<object_id>` (R-3.3c), so it is stable and non-colliding as the fleet
  // grows. A TOKEN PUBLISHED IS A TOKEN FROZEN - HA's registry remembers it forever.
  const char* object_id;

  const char* name;       // the entity name HA shows under the device
  const char* component;  // `sensor`, `binary_sensor`, `button` - HA's discovery segment

  // The state topic, relative to the device's `~` base. Null for a button, which has a
  // command topic instead.
  const char* state_suffix;

  // The key to read out of the state topic's JSON document, or null to use the payload
  // whole. Rendered as a value template.
  const char* value_key;

  const char* unit;          // `unit_of_meas`
  const char* device_class;  // `dev_cla`
  const char* state_class;   // `stat_cla`

  // The command topic for a button, relative to `~`, and what to publish on a press.
  const char* command_suffix;
  const char* press_payload;

  // Spec 8.1's `cmd` this button sends, so the capability filter decides whether the row
  // applies to a node type. 0 (`NOP`) for a row that is not a button.
  uint8_t cmd;

  // `ent_cat: diagnostic`. A counter belongs under the device's diagnostics rather than
  // on a dashboard beside the gate's own state.
  bool diagnostic;
};

// ---------------------------------------------------------------------------
// Walking every config the bridge publishes.
//
// A CURSOR RATHER THAN A LIST, because there is no allocation here (root rule 3) and
// mqtt_task publishes one message at a time through a fixed-size queue. The caller steps
// the cursor, builds each config into its own buffer and queues it, so the whole set
// never exists at once.
//
// The order is stable: the bridge's own entities first, then each registered node in
// kNodeTable order. A run that stops halfway is therefore readable from what appeared.
// ---------------------------------------------------------------------------

struct DiscoveryCursor {
  size_t node    = 0;  // kNodeCount means the bridge's own entities
  size_t entity  = 0;
};

// What one step produced.
struct DiscoveryItem {
  bool         valid = false;
  lran::NodeId node_id = 0;   // the bridge's own address for a bridge entity
  const EntityDesc* desc = nullptr;
};

// Advances `cur` to the next entity that applies, and reports it. False when the set is
// exhausted. Skips a node whose publication is gated (spec 16.6) and a button whose
// `cmd` the node type does not implement.
//
// `simnode_diag_enable` is BF-26's toggle, passed in rather than read, for the reason
// node_availability.h gives: the gate is on publication and nowhere else.
bool discovery_next(DiscoveryCursor* cur, const NodeInfo* nodes, size_t node_count,
                    bool simnode_diag_enable, DiscoveryItem* out);

// `homeassistant/<component>/<object>/config` for one item (spec 16.1). Returns the
// length written, or 0 for a short `cap` or a node spec 16.1 gives no token.
size_t discovery_topic(const DiscoveryItem& item, char* out, size_t cap);

// The config document. Returns the length written, or 0 when it did not fit - in which
// case `out` is left empty and the caller drops that entity rather than publishing a
// truncated document.
//
// R-3.3d IS ENFORCED HERE: a node entity's `avty_t` is that node's own availability
// topic, never the bridge's LWT. A node that has gone offline must show as unavailable
// while the bridge is still connected and publishing, which is precisely the case the
// bridge's own LWT cannot describe.
size_t discovery_config_json(const DiscoveryItem& item, char* out, size_t cap);

// The device name Home Assistant shows, for one node. `bridge` is "LoRa Bridge", which
// is deliberate and not a leftover: D17's amendment retired `LoRaBridge` as the node's
// name in the documents while keeping "LoRa Bridge" as the HA device name, because
// renaming a device in HA's registry is not free.
const char* device_name(lran::NodeId node_id);

}  // namespace bridge
