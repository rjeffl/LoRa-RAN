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
#include "publish.h"

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

// ---------------------------------------------------------------------------
// GateLink's state - schema 0x10's five documents (BF-24, publish.h).
//
// THE VALUE KEYS ARE publish.cpp's, and they are frozen with the object ids here. A key
// renamed there is an entity here that reads an undefined name and shows unknown forever.
// test_discovery checks every key below against a document the policy rendered.
//
// A BINARY SENSOR READS A JSON BOOLEAN AS `True` OR `False`, which is how Home Assistant's
// template renders one, and a null as `None`, which it treats as unknown. So a null reading
// stays unknown here as it does on a sensor, and never becomes off.
//
// WHICH ROWS EXIST IS A CHOICE, and the documents carry more than this. A key without an
// entity is still on the topic. The rows below are the ones a person looks at, as R-3.5e
// reasons about the MPPT's registers; `uptime_s` in particular has no entity, because it
// changes on every poll and would fill HA's history with nothing.
// ---------------------------------------------------------------------------

constexpr EntityDesc kGateLinkStateEntities[] = {
    {"gate_state", "Gate", "sensor", "gate/state", "state",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},
    {"held_open", "Held open", "binary_sensor", "gate/state", "held_open",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},
    {"hold_source", "Hold source", "sensor", "gate/state", "hold_source",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},
    {"movement_cause", "Movement cause", "sensor", "gate/state", "movement_cause",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},
    {"last_direction", "Last direction", "sensor", "gate/state", "last_direction",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},
    {"fire_input", "FIRE input", "binary_sensor", "gate/state", "in_fire",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},
    {"alarm_input", "ALARM input", "binary_sensor", "gate/state", "in_alarm",
     nullptr, "problem", nullptr, nullptr, nullptr, 0, false},

    {"vehicle_while_held", "Vehicle while held open", "binary_sensor", "detect/state",
     "vehicle_while_held", nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},
    {"last_vehicle", "Last vehicle", "sensor", "detect/state", "last_traversal",
     nullptr, "timestamp", nullptr, nullptr, nullptr, 0, false},
    {"safety_loop", "Safety loop", "binary_sensor", "detect/state", "safety",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},
    {"exit_wand", "Exit wand", "binary_sensor", "detect/state", "exit",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},

    {"mppt_batt_voltage", "Battery voltage (MPPT)", "sensor", "solar/state", "batt_mv",
     "mV", "voltage", "measurement", nullptr, nullptr, 0, false},
    {"mppt_batt_current", "Battery current (MPPT)", "sensor", "solar/state", "batt_ma",
     "mA", "current", "measurement", nullptr, nullptr, 0, false},
    {"pv_voltage", "PV voltage", "sensor", "solar/state", "pv_mv",
     "mV", "voltage", "measurement", nullptr, nullptr, 0, false},
    {"pv_power", "PV power", "sensor", "solar/state", "pv_w",
     "W", "power", "measurement", nullptr, nullptr, 0, false},
    {"load_current", "Load current", "sensor", "solar/state", "load_ma",
     "mA", "current", "measurement", nullptr, nullptr, 0, false},
    {"yield_today", "Solar yield today", "sensor", "solar/state", "yield_today_kwh",
     "kWh", "energy", "total_increasing", nullptr, nullptr, 0, false},
    {"yield_total", "Solar yield total", "sensor", "solar/state", "yield_total_kwh",
     "kWh", "energy", "total_increasing", nullptr, nullptr, 0, false},
    {"mppt_charge_state", "MPPT charge state", "sensor", "solar/state", "charge_state",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, true},
    {"mppt_error", "MPPT error", "sensor", "solar/state", "error",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, true},
    {"mppt_temperature", "MPPT temperature", "sensor", "solar/state", "temp_c",
     "\u00b0C", "temperature", "measurement", nullptr, nullptr, 0, true},

    {"soc", "Battery", "sensor", "battery/state", "soc",
     "%", "battery", "measurement", nullptr, nullptr, 0, false},
    {"pack_voltage", "Pack voltage", "sensor", "battery/state", "pack_mv",
     "mV", "voltage", "measurement", nullptr, nullptr, 0, false},
    {"pack_current", "Pack current", "sensor", "battery/state", "pack_ma",
     "mA", "current", "measurement", nullptr, nullptr, 0, false},
    {"cell1_voltage", "Cell 1 voltage", "sensor", "battery/state", "cell1_mv",
     "mV", "voltage", "measurement", nullptr, nullptr, 0, true},
    {"cell2_voltage", "Cell 2 voltage", "sensor", "battery/state", "cell2_mv",
     "mV", "voltage", "measurement", nullptr, nullptr, 0, true},
    {"cell3_voltage", "Cell 3 voltage", "sensor", "battery/state", "cell3_mv",
     "mV", "voltage", "measurement", nullptr, nullptr, 0, true},
    {"cell4_voltage", "Cell 4 voltage", "sensor", "battery/state", "cell4_mv",
     "mV", "voltage", "measurement", nullptr, nullptr, 0, true},
    {"bms_cycles", "Battery cycles", "sensor", "battery/state", "cycles",
     nullptr, nullptr, "total_increasing", nullptr, nullptr, 0, true},
    {"bms_protection", "Battery protection", "binary_sensor", "battery/state", "protection",
     nullptr, "problem", nullptr, nullptr, nullptr, 0, false},
    {"bms_charge_inhibited", "Charging inhibited (BMS)", "binary_sensor", "battery/state",
     "charge_inhibited", nullptr, nullptr, nullptr, nullptr, nullptr, 0, false},
    {"bms_ble_rssi", "BMS link RSSI", "sensor", "battery/state", "ble_rssi_dbm",
     "dBm", "signal_strength", "measurement", nullptr, nullptr, 0, true},
    {"bms_age", "BMS reading age", "sensor", "battery/state", "age_s",
     "s", "duration", "measurement", nullptr, nullptr, 0, true},

    {"node_voltage", "Node supply voltage", "sensor", "node/state", "node_mv",
     "mV", "voltage", "measurement", nullptr, nullptr, 0, true},
    {"node_current", "Node supply current", "sensor", "node/state", "node_ma",
     "mA", "current", "measurement", nullptr, nullptr, 0, true},
    {"enclosure_temperature", "Enclosure temperature", "sensor", "node/state",
     "enclosure_temp_c", "\u00b0C", "temperature", "measurement", nullptr, nullptr, 0, true},
    {"boot_count", "Boot count", "sensor", "node/state", "boot_count",
     nullptr, nullptr, "total_increasing", nullptr, nullptr, 0, true},
    {"config_persisted", "Configuration persisted", "binary_sensor", "node/state",
     "config_persisted", nullptr, nullptr, nullptr, nullptr, nullptr, 0, true},
    {"dry_run", "Relay dry run", "binary_sensor", "node/state", "dry_run",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, true},
    {"shutdown_latch", "Hard-shutdown latch", "binary_sensor", "node/state",
     "shutdown_latch", nullptr, "problem", nullptr, nullptr, nullptr, 0, false},
    // R-5.2d - a synthetic frame is marked in every document, and this is where a person
    // sees the mark without reading a topic.
    {"synthetic", "Synthetic data", "binary_sensor", "node/state", "synthetic",
     nullptr, nullptr, nullptr, nullptr, nullptr, 0, true},
};
constexpr size_t kGateLinkStateEntityCount =
    sizeof(kGateLinkStateEntities) / sizeof(kGateLinkStateEntities[0]);

// A node type's state rows (BG-2): the template half of "a registry row, a decoder and a
// template". WellLink's schema 0x20 is reserved and undefined (spec 7.1), and a bench node
// publishes no state (spec 16.6), so both have none.
size_t state_entity_count(NodeType type) {
  return type == NodeType::GateLink ? kGateLinkStateEntityCount : 0;
}
const EntityDesc* state_entities(NodeType type) {
  return type == NodeType::GateLink ? kGateLinkStateEntities : nullptr;
}

// A node's whole set, in one order: its link first, then its state, then its buttons.
const EntityDesc* node_entity(NodeType type, size_t index) {
  if (index < kNodeLinkEntityCount) return &kNodeLinkEntities[index];
  size_t i = index - kNodeLinkEntityCount;
  if (i < state_entity_count(type)) return &state_entities(type)[i];
  i -= state_entity_count(type);
  if (i < kNodeCommandEntityCount) return &kNodeCommandEntities[i];
  return nullptr;
}
size_t node_entity_count(NodeType type) {
  return kNodeLinkEntityCount + state_entity_count(type) + kNodeCommandEntityCount;
}

// ---------------------------------------------------------------------------
// BF-35 - the configuration table's rows, as controls.
//
// NOTHING HERE LISTS A PARAMETER. The rows are /lib/lran-config/'s (D44), walked in table
// order, so a row added there gains its control here without an edit, and a control can
// never offer a range the bridge would clamp.
// ---------------------------------------------------------------------------

using lran::config::Access;
using lran::config::Owner;
using lran::config::ParamDef;

// The bridge's global rows, set on lran/bridge/config/set (spec 16.7.1).
const ParamDef* bridge_param(size_t index) {
  size_t k = 0;
  for (size_t i = 0; i < lran::config::kBridgeParamCount; ++i) {
    const ParamDef& d = lran::config::kBridgeParams[i];
    if (d.owner != Owner::BridgeGlobal) continue;
    if (k++ == index) return &d;
  }
  return nullptr;
}

// A node's rows, in the order its `config/state` carries them: the bridge's per-node rows,
// then the node's own. GateLink's and WellLink's own blocks join here when their
// milestones declare them.
const ParamDef* node_param(size_t index) {
  size_t k = 0;
  for (size_t i = 0; i < lran::config::kBridgeParamCount; ++i) {
    const ParamDef& d = lran::config::kBridgeParams[i];
    if (d.owner != Owner::BridgePerNode) continue;
    if (k++ == index) return &d;
  }
  const size_t i = index - k;
  return i < lran::config::kNodeCommonParamCount ? &lran::config::kNodeCommonParams[i]
                                                 : nullptr;
}

// The SX1262's LoRa bandwidths that are whole kilohertz, which is every one the table's
// kHz unit can carry (spec 12.1). A select offers those inside the row's own range.
constexpr int32_t kLoraBandwidthsKhz[] = {125, 250, 500};

bool is_select(const ParamDef& p) {
  return p.owner != Owner::Node && std::strcmp(p.name, "bandwidth_khz") == 0;
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

// The device block. `via_device` puts every node under the bridge in HA's device tree,
// which is true of the topology and is also how a reader tells a silent node from a
// silent bridge at a glance.
size_t device_block(lran::NodeId node_id, const char* dev, const char* dname, char* out,
                    size_t cap) {
  JsonObject dj(out, cap);
  char ids[64];
  std::snprintf(ids, sizeof(ids), "[\"%s\"]", dev);
  dj.raw("ids", ids);
  dj.str("name", dname);
  dj.str("mf", kManufacturer);
  dj.str("mdl", dname);
  if (node_id != lran::kNodeBridge) dj.str("via_device", "lran_bridge");
  return dj.finish();
}

// BF-35 - one table row as a control, or as a sensor for a node's PHY row.
//
// IT WRITES WHAT SPEC 16.7.2 SAYS HA WRITES, `{"set": {<name>: <value>}}`, on the topic
// spec 16.7.1 names for the row's owner, and reads `value_json.<name>.value` from the
// matching `config/state` (16.7.4). `~` is that topic's device, so the bridge's rows land on
// lran/bridge and a node's on lran/<node>.
//
// THE NAME HA SHOWS IS THE TABLE'S NAME. HA derives a new entity's id from the device name
// and the entity name, so `number.gatelink_poll_interval_s` reads as the key a person would
// publish by hand. The name can be changed in HA; the unique_id cannot.
size_t param_config_json(lran::NodeId node_id, const ParamDef& p, char* out, size_t cap) {
  char dev[48];
  char base[64];
  const char* dname = device_name(node_id);
  if (device_id(node_id, dev, sizeof(dev)) == 0 || base_topic(node_id, base, sizeof(base)) == 0 ||
      dname == nullptr) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  const char* component = param_component(p);
  const bool  control   = std::strcmp(component, "sensor") != 0;

  char uniq[80];
  std::snprintf(uniq, sizeof(uniq), "%s_%s", dev, p.name);
  char tmpl[80];

  JsonObject j(out, cap);
  j.str("~", base);
  j.str("name", p.name);
  j.str("uniq_id", uniq);
  j.str("stat_t", "~/config/state");
  // `.value` and nothing more. A value the bridge has never read back is null (spec
  // 16.7.4), which renders `None`, and HA's number, select and switch all read that as
  // unknown rather than as zero.
  std::snprintf(tmpl, sizeof(tmpl), "{{ value_json.%s.value }}", p.name);
  j.str("val_tpl", tmpl);

  if (control) {
    j.str("cmd_t", "~/config/set");
    if (p.type == lran::PType::Bool) {
      // Spec 16.7.2 takes `true` and `false` for a bool; config/state reports 1 and 0.
      char on[80], off[80];
      std::snprintf(on, sizeof(on), "{\"set\": {\"%s\": true}}", p.name);
      std::snprintf(off, sizeof(off), "{\"set\": {\"%s\": false}}", p.name);
      j.str("pl_on", on);
      j.str("pl_off", off);
      j.str("stat_on", "1");
      j.str("stat_off", "0");
    } else {
      std::snprintf(tmpl, sizeof(tmpl), "{\"set\": {\"%s\": {{ value }}}}", p.name);
      j.str("cmd_tpl", tmpl);
    }
    if (is_select(p)) {
      char ops[64];
      size_t n = 0;
      ops[n++] = '[';
      for (int32_t bw : kLoraBandwidthsKhz) {
        if (bw < p.min || bw > p.max) continue;
        const int w = std::snprintf(ops + n, sizeof(ops) - n, "%s\"%ld\"",
                                    n > 1 ? "," : "", static_cast<long>(bw));
        if (w < 0 || static_cast<size_t>(w) >= sizeof(ops) - n) {
          if (out != nullptr && cap > 0) out[0] = '\0';
          return 0;
        }
        n += static_cast<size_t>(w);
      }
      if (n + 2 > sizeof(ops)) {
        if (out != nullptr && cap > 0) out[0] = '\0';
        return 0;
      }
      ops[n++] = ']';
      ops[n]   = '\0';
      j.raw("options", ops);
    } else if (p.type != lran::PType::Bool) {
      // The table's own range, so HA refuses what the bridge would clamp. Box mode, so a
      // slider cannot be dragged through a fleet-wide PHY change on its way to a value.
      j.i32("min", p.min);
      j.i32("max", p.max);
      j.str("mode", "box");
    }
  }

  // R-3.3d - a node's own rows follow the node's availability. The bridge's rows follow
  // the bridge's, INCLUDING the per-node ones shown on a node's device: the bridge applies
  // them, and `deployed` must be settable on a node that has never been heard (D61), which
  // is exactly when that node reads offline.
  if (p.owner == Owner::Node) {
    j.str("avty_t", "~/availability");
  } else {
    char avty[64];
    std::snprintf(avty, sizeof(avty), "%s/bridge/availability", kTopicRoot);
    j.str("avty_t", avty);
  }
  j.str("pl_avail", kPayloadOnline);
  j.str("pl_not_avail", kPayloadOffline);

  // A select and a switch take no unit in HA's schema; a number and a sensor do.
  if (!control || std::strcmp(component, "number") == 0) j.str("unit_of_meas", p.unit);
  j.str("ent_cat", control ? "config" : "diagnostic");

  char device[256];
  if (device_block(node_id, dev, dname, device, sizeof(device)) == 0) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  j.raw("dev", device);
  return j.finish();
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
      out->node_id = lran::kNodeBridge;
      if (cur->entity < kBridgeEntityCount) {
        out->desc = &kBridgeEntities[cur->entity];
      } else if ((out->param = bridge_param(cur->entity - kBridgeEntityCount)) == nullptr) {
        ++cur->node;  // past the end; the next call returns false
        return false;
      }
      out->valid = true;
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

    const size_t      fixed = node_entity_count(info.type);
    const EntityDesc* d     = cur->entity < fixed ? node_entity(info.type, cur->entity) : nullptr;
    const ParamDef*   p     = nullptr;
    if (d == nullptr && !info.is_bench) p = node_param(cur->entity - fixed);
    if (d == nullptr && p == nullptr) {
      ++cur->node;
      cur->entity = 0;
      continue;
    }
    ++cur->entity;
    if (d != nullptr && !applies(*d, info.type)) continue;

    out->valid   = true;
    out->node_id = info.id;
    out->desc    = d;
    out->param   = p;
    return true;
  }
}

const char* discovery_object_id(const DiscoveryItem& item) {
  if (item.desc != nullptr) return item.desc->object_id;
  return item.param != nullptr ? item.param->name : nullptr;
}

const char* param_component(const ParamDef& p) {
  if (p.owner == Owner::Node && p.access == Access::Phy) return "sensor";
  if (p.type == lran::PType::Bool) return "switch";
  if (is_select(p)) return "select";
  return "number";
}

size_t discovery_topic(const DiscoveryItem& item, char* out, size_t cap) {
  if (!item.valid || (item.desc == nullptr) == (item.param == nullptr) || out == nullptr ||
      cap == 0) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  char dev[48];
  if (device_id(item.node_id, dev, sizeof(dev)) == 0) {
    out[0] = '\0';
    return 0;
  }
  const char* component =
      item.desc != nullptr ? item.desc->component : param_component(*item.param);
  const int n = std::snprintf(out, cap, "%s/%s/%s_%s/config", kDiscoveryPrefix, component,
                              dev, discovery_object_id(item));
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t discovery_config_json(const DiscoveryItem& item, char* out, size_t cap) {
  if (!item.valid || (item.desc == nullptr) == (item.param == nullptr)) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  if (item.param != nullptr) return param_config_json(item.node_id, *item.param, out, cap);
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

  if (std::strcmp(d.component, "binary_sensor") == 0) {
    // A JSON boolean renders as `True` or `False`, and null as `None`, which HA reads as
    // unknown. Matching the rendering keeps a null reading from becoming `off`.
    j.str("pl_on", "True");
    j.str("pl_off", "False");
  }

  // R-3.3d - THAT NODE'S availability topic, never the bridge's LWT. For the bridge's own
  // entities the two are the same topic, and that is a coincidence of address rather than
  // a shortcut: the expression below is the node's topic in both cases.
  //
  // R-5.2b (BF-24). A reading from a block that can go stale also follows that block's
  // `available`, so a stale VE.Direct or BMS link shows its entities as unavailable while
  // the node is still online. HA takes an entity as available only when every topic in the
  // list says so (`avty_mode` all). `avty_t` and `avty` cannot both be given. A list entry
  // takes HA's default payloads, `online` and `offline`, which are spec 16.5's.
  if (state_carries_availability(d.state_suffix) && !key_survives_staleness(d.value_key)) {
    char list[192];
    std::snprintf(list, sizeof(list),
                  "[{\"t\":\"~/availability\"},{\"t\":\"~/%s\",\"val_tpl\":"
                  "\"{{ 'online' if value_json.available else 'offline' }}\"}]",
                  d.state_suffix);
    j.raw("avty", list);
    j.str("avty_mode", "all");
  } else {
    std::snprintf(topic, sizeof(topic), "~/availability");
    j.str("avty_t", topic);
    j.str("pl_avail", kPayloadOnline);
    j.str("pl_not_avail", kPayloadOffline);
  }

  j.str("unit_of_meas", d.unit);
  j.str("dev_cla", d.device_class);
  j.str("stat_cla", d.state_class);
  // Spec 16.6 axis 3 - every bench entity is diagnostic, its buttons included, so a
  // simnode's Open never lands on a dashboard beside the gate's.
  if (d.diagnostic || lran::is_bench_node(item.node_id)) j.str("ent_cat", "diagnostic");

  char device[256];
  if (device_block(item.node_id, dev, dname, device, sizeof(device)) == 0) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  j.raw("dev", device);

  return j.finish();
}

}  // namespace bridge
