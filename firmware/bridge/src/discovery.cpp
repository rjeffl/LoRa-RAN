// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The discovery configs. Task BF-23; see discovery.h.

#include "discovery.h"

#include <cstdio>
#include <cstring>

#include "command.h"
#include "json_writer.h"
#include "net_policy.h"
#include "node_availability.h"

namespace bridge {
namespace {

// The manufacturer and the model strings HA shows on the device page. "LRAN" is the
// project, not a company: spec 18.2 and D33 forbid representing any node as FCC
// certified, and a manufacturer field naming a vendor would read as exactly that claim
// on the one screen a visitor looks at.
constexpr const char* kManufacturer = "LRAN";

// ---------------------------------------------------------------------------
// The bridge's own entities.
//
// These read the two documents the bridge already publishes (BF-13's version topic and
// BF-19's diagnostics). Nothing here adds a publication - BF-24 owns the publication
// policy, and an entity whose topic nobody writes is an entity that never updates.
// ---------------------------------------------------------------------------

constexpr EntityDesc kBridgeEntities[] = {
    {"version", "Firmware version", "sensor", "version", "version",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, true},
    {"git", "Firmware commit", "sensor", "version", "git",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, true},
    {"ota_slot", "OTA slot", "sensor", "version", "slot",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, true},

    // Spec 14.1's two totals. The itemised counters are deliberately NOT entities: 22
    // rows per node in HA's registry is a cost paid forever for numbers read during a
    // bench session, and they stay readable on the topic either way. R-3.5e reasons the
    // same way about the MPPT's registers.
    {"rx_frames", "Frames received", "sensor", "diag/state", "rx_frames",
     "frames", nullptr, "total_increasing", nullptr, nullptr, 0, true},
    {"rx_dropped", "Frames discarded", "sensor", "diag/state", "rx_dropped",
     "frames", nullptr, "total_increasing", nullptr, nullptr, 0, true},
    {"tx_frames", "Frames transmitted", "sensor", "diag/radio/state", "tx_frames",
     "frames", nullptr, "total_increasing", nullptr, nullptr, 0, true},
};
constexpr size_t kBridgeEntityCount = sizeof(kBridgeEntities) / sizeof(kBridgeEntities[0]);

// ---------------------------------------------------------------------------
// Every registered node's link, as the bridge last heard it.
//
// These are spec 16.2.1's `lran/<node>/diag/state` keys, and that section fixes the
// payload, so a value template here cannot drift from what the bridge publishes without
// the specification changing first.
//
// AN UNAVAILABLE VALUE IS `null` (spec 16.2.1, root rule 6), and the templates say so:
// HA renders a null as `unknown` rather than as a reading of zero, which is the whole
// point of publishing a sentinel as null in the first place.
// ---------------------------------------------------------------------------

constexpr EntityDesc kNodeLinkEntities[] = {
    {"rssi", "RSSI", "sensor", "diag/state", "rssi_dbm",
     "dBm", "signal_strength", "measurement", nullptr, nullptr, 0, true},
    {"snr", "SNR", "sensor", "diag/state", "snr_db",
     "dB", nullptr, "measurement", nullptr, nullptr, 0, true},
    {"last_seen", "Last seen", "sensor", "diag/state", "last_seen_s",
     "s", "duration", "measurement", nullptr, nullptr, 0, true},
    {"missed_polls", "Missed polls", "sensor", "diag/state", "missed_polls",
     nullptr, nullptr, "measurement", nullptr, nullptr, 0, true},
    {"proto_ver", "Protocol version", "sensor", "diag/state", "proto_ver",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, true},

    // R-3.1f (BF-22). Without an entity this reason is readable only on the topic, and
    // the case it describes is a node that has fallen silent - when nobody is looking at
    // a topic.
    {"unsupported_ver", "Unsupported version seen", "sensor", "diag/state",
     "unsupported_ver", nullptr, nullptr, nullptr, nullptr, nullptr, 0, true},
};
constexpr size_t kNodeLinkEntityCount =
    sizeof(kNodeLinkEntities) / sizeof(kNodeLinkEntities[0]);

// ---------------------------------------------------------------------------
// Command buttons - spec 8.1, over BF-18's `lran/<node>/cmd/<action>/set`.
//
// WHICH ROWS APPLY IS command_allowed()'s ANSWER, not a table per node type. That is the
// same capability filter the command path uses, so a button cannot exist for a command
// the bridge would refuse to send - and adding a node type still touches a registry row,
// a decoder and a template, never this list (BG-2).
//
// THE ACTION TOKENS ARE net_policy.cpp's, and they are frozen (net_policy.h). A token
// spelled differently here is a button that publishes to a topic the bridge does not
// parse, which fails silently at the one end nobody watches.
//
// ONLY THE COMMANDS THAT NEED NO ARGUMENT ARE BUTTONS. `set_debug_mode` and its
// neighbours carry a bitmask in `arg2`, and a button cannot express one; they are
// reachable from the topic and belong to the configuration work, not here.
//
// REBOOT IS NOT HERE AT ALL. Spec 8.1 guards it with 0xA5 in `arg` precisely so that it
// cannot be issued by accident, and a button on a dashboard is the accident that guard
// describes. It stays reachable by publishing to the topic.
// ---------------------------------------------------------------------------

constexpr EntityDesc kNodeCommandEntities[] = {
    {"cmd_open", "Open", "button", nullptr, nullptr, nullptr, nullptr, nullptr,
     "cmd/open/set", "PRESS", static_cast<uint8_t>(lran::Cmd::Open), false},
    {"cmd_close", "Close", "button", nullptr, nullptr, nullptr, nullptr, nullptr,
     "cmd/close/set", "PRESS", static_cast<uint8_t>(lran::Cmd::Close), false},
    {"cmd_hold_open", "Hold open", "button", nullptr, nullptr, nullptr, nullptr, nullptr,
     "cmd/hold_open/set", "PRESS", static_cast<uint8_t>(lran::Cmd::HoldOpen), false},
    {"cmd_release_hold", "Release hold", "button", nullptr, nullptr, nullptr, nullptr,
     nullptr, "cmd/release_hold/set", "PRESS",
     static_cast<uint8_t>(lran::Cmd::ReleaseHold), false},
    {"cmd_request_status", "Request status", "button", nullptr, nullptr, nullptr, nullptr,
     nullptr, "cmd/request_status/set", "PRESS",
     static_cast<uint8_t>(lran::Cmd::RequestStatus), false},
    {"cmd_request_config", "Request configuration", "button", nullptr, nullptr, nullptr,
     nullptr, nullptr, "cmd/request_config/set", "PRESS",
     static_cast<uint8_t>(lran::Cmd::RequestConfig), false},
};
constexpr size_t kNodeCommandEntityCount =
    sizeof(kNodeCommandEntities) / sizeof(kNodeCommandEntities[0]);

// A node's whole set, in one order: its link first, then its buttons.
const EntityDesc* node_entity(size_t index) {
  if (index < kNodeLinkEntityCount) return &kNodeLinkEntities[index];
  const size_t i = index - kNodeLinkEntityCount;
  if (i < kNodeCommandEntityCount) return &kNodeCommandEntities[i];
  return nullptr;
}

// True when this row belongs to this node type. A link entity always does; a button does
// only if the node implements the command.
bool applies(const EntityDesc& d, NodeType type) {
  if (d.command_suffix == nullptr) return true;
  return command_allowed(type, d.cmd);
}

// `lran_<token>` - the device identifier and the unique_id prefix (R-3.3c).
size_t device_id(lran::NodeId node_id, char* out, size_t cap) {
  char token[32];
  if (node_id == lran::kNodeBridge) {
    std::snprintf(token, sizeof(token), "bridge");
  } else if (node_topic_name(node_id, token, sizeof(token)) == 0) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  const int n = std::snprintf(out, cap, "lran_%s", token);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

// The `~` base topic: `lran/<token>`.
size_t base_topic(lran::NodeId node_id, char* out, size_t cap) {
  char token[32];
  if (node_id == lran::kNodeBridge) {
    std::snprintf(token, sizeof(token), "bridge");
  } else if (node_topic_name(node_id, token, sizeof(token)) == 0) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  const int n = std::snprintf(out, cap, "%s/%s", kTopicRoot, token);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

}  // namespace

const char* device_name(lran::NodeId node_id) {
  // "LoRa Bridge" stays, although the documents call the node "Bridge Node" - D17's
  // amendment, 2026-09-10. Renaming a device Home Assistant already knows is not free.
  if (node_id == lran::kNodeBridge) return "LoRa Bridge";
  if (node_id == lran::kNodeGateLink) return "GateLink";
  if (node_id == lran::kNodeWellLink) return "WellLink";
  if (node_id >= lran::kNodeSim0 && node_id <= lran::kNodeSim3) {
    static const char* const kBench[] = {"Simnode 0", "Simnode 1", "Simnode 2",
                                         "Simnode 3"};
    return kBench[node_id - lran::kNodeSim0];
  }
  return nullptr;
}

bool discovery_next(DiscoveryCursor* cur, const NodeInfo* nodes, size_t node_count,
                    bool simnode_diag_enable, DiscoveryItem* out) {
  if (cur == nullptr || out == nullptr) return false;
  *out = DiscoveryItem{};

  // The bridge's own entities come first, at index node_count.
  while (true) {
    if (cur->node > node_count) return false;

    if (cur->node == node_count) {
      if (cur->entity >= kBridgeEntityCount) {
        ++cur->node;  // past the end; the next call returns false
        return false;
      }
      out->valid   = true;
      out->node_id = lran::kNodeBridge;
      out->desc    = &kBridgeEntities[cur->entity];
      ++cur->entity;
      return true;
    }

    const NodeInfo& info = nodes[cur->node];

    // Spec 16.6 - a bench node's publication is gated, and discovery is publication.
    // WITHOUT THIS, four simnode devices and their entities enter HA's registry, which
    // remembers a unique_id forever, on a bridge that never publishes their state.
    if (!bench_publication_allowed(info, simnode_diag_enable)) {
      ++cur->node;
      cur->entity = 0;
      continue;
    }

    const EntityDesc* d = node_entity(cur->entity);
    if (d == nullptr) {
      ++cur->node;
      cur->entity = 0;
      continue;
    }
    ++cur->entity;
    if (!applies(*d, info.type)) continue;

    out->valid   = true;
    out->node_id = info.id;
    out->desc    = d;
    return true;
  }
}

size_t discovery_topic(const DiscoveryItem& item, char* out, size_t cap) {
  if (!item.valid || item.desc == nullptr || out == nullptr || cap == 0) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  char dev[48];
  if (device_id(item.node_id, dev, sizeof(dev)) == 0) {
    out[0] = '\0';
    return 0;
  }
  const int n = std::snprintf(out, cap, "%s/%s/%s_%s/config", kDiscoveryPrefix,
                              item.desc->component, dev, item.desc->object_id);
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t discovery_config_json(const DiscoveryItem& item, char* out, size_t cap) {
  if (!item.valid || item.desc == nullptr) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  const EntityDesc& d = *item.desc;

  char dev[48];
  char base[64];
  const char* dname = device_name(item.node_id);
  if (device_id(item.node_id, dev, sizeof(dev)) == 0 ||
      base_topic(item.node_id, base, sizeof(base)) == 0 || dname == nullptr) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }

  char uniq[80];
  std::snprintf(uniq, sizeof(uniq), "%s_%s", dev, d.object_id);

  char topic[64];
  char tmpl[64];

  JsonObject j(out, cap);
  j.str("~", base);
  j.str("name", d.name);
  j.str("uniq_id", uniq);

  if (d.state_suffix != nullptr) {
    std::snprintf(topic, sizeof(topic), "~/%s", d.state_suffix);
    j.str("stat_t", topic);
  }
  if (d.value_key != nullptr) {
    // `value_json.<key>` and nothing more. A default or a filter here would turn an
    // absent reading into a number, which spec 16.2.1's null exists to prevent.
    std::snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s }}", d.value_key);
    j.str("val_tpl", tmpl);
  }
  if (d.command_suffix != nullptr) {
    std::snprintf(topic, sizeof(topic), "~/%s", d.command_suffix);
    j.str("cmd_t", topic);
    j.str("payload_press", d.press_payload);
  }

  // R-3.3d - THAT NODE'S availability topic, never the bridge's LWT. For the bridge's own
  // entities the two are the same topic, and that is a coincidence of address rather than
  // a shortcut: the expression below is the node's topic in both cases.
  std::snprintf(topic, sizeof(topic), "~/availability");
  j.str("avty_t", topic);
  j.str("pl_avail", kPayloadOnline);
  j.str("pl_not_avail", kPayloadOffline);

  j.str("unit_of_meas", d.unit);
  j.str("dev_cla", d.device_class);
  j.str("stat_cla", d.state_class);
  // Spec 16.6 axis 3 - every bench entity is diagnostic, its buttons included, so a
  // simnode's Open never lands on a dashboard beside the gate's.
  if (d.diagnostic || lran::is_bench_node(item.node_id)) j.str("ent_cat", "diagnostic");

  // The device block. `via_device` puts every node under the bridge in HA's device tree,
  // which is true of the topology and is also how a reader tells a silent node from a
  // silent bridge at a glance.
  char device[256];
  {
    JsonObject dj(device, sizeof(device));
    char ids[64];
    std::snprintf(ids, sizeof(ids), "[\"%s\"]", dev);
    dj.raw("ids", ids);
    dj.str("name", dname);
    dj.str("mf", kManufacturer);
    dj.str("mdl", dname);
    if (item.node_id != lran::kNodeBridge) dj.str("via_device", "lran_bridge");
    if (dj.finish() == 0) {
      if (out != nullptr && cap > 0) out[0] = '\0';
      return 0;
    }
  }
  j.raw("dev", device);

  return j.finish();
}

}  // namespace bridge
