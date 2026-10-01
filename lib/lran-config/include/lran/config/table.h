// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The parameter table. One hand-written C++ table per owner, and every other copy derived
// from it by code (D44): firmware defaults, Home Assistant `number` discovery and
// /docs/gatelink-config.md all read this. No generator, no YAML.
//
// A NAME HERE IS PERMANENT. It becomes a Home Assistant object_id (spec 16.7), and an
// entity renamed after it has history is a new entity with none. Changing a name is a
// migration, not an edit.
//
// PType, ParamStatus, ConfigOp and PersistStatus come from lran-protocol rather than being
// declared again here. Protocol Library Plan 4's sketch declared its own PType before the
// codec existed; two enumerations for one concept is exactly the drift D44 exists to stop.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/types.h"

namespace lran {
namespace config {

// D56 - a parameter the node publishes but cannot yet apply answers a SET with READ_ONLY
// (spec 8.12, 12.4). Honest, and it costs one byte per row.
//
// Phy marks spec 12.4's PHY group. It is writable only through a Store whose owner has
// built 12.4's commit-and-revert and says so with Store::enable_phy_trial(); every other
// Store answers it READ_ONLY, exactly as a ReadOnly row. The table cannot know which
// firmware has built the machinery, so the Store is told.
enum class Access : uint8_t { ReadWrite, ReadOnly, Phy };

// D47 - who holds the value, and so which topic sets it (spec 16.7.1).
enum class Owner : uint8_t {
  BridgeGlobal,   // lran/bridge/config/set; never carried by a frame
  BridgePerNode,  // lran/<node>/config/set; applied by the bridge, one value per node
  Node,           // lran/<node>/config/set; sent to the node as CONFIG
};

// Every value travels as int32_t inside the library and is packed to its ptype on the
// wire. That covers u8, u16, i16, i32 and bool outright; for u32 it covers everything
// below 2^31, which freq_hz's 928 MHz ceiling sits well under. A u32 parameter above that
// needs a wider representation here first, and the static_assert below says so.
using Value = int32_t;

struct ParamDef {
  uint16_t    id;           // spec 7.4, D46 - one namespace, a block per owner
  const char* name;         // the HA object_id: permanent once published (spec 16.7)
  Owner       owner;
  Access      access;       // D56 - ReadOnly for a row the firmware cannot apply yet
  PType       type;
  Value       min, max, def;
  const char* unit;         // nullptr if unitless
  const char* doc;          // one line - this IS the documentation
};

// 0x0000-0x00FF - the bridge. Defaults are the firmware's as of BF-19/BF-19a/BF-20;
// ranges are proposed rather than measured (Protocol Library Plan 4).
inline constexpr ParamDef kBridgeParams[] = {
    {0x0001, "simnode_diag_enable", Owner::BridgeGlobal, Access::ReadWrite, PType::Bool, 0,
     1, 0, nullptr, "Publish bench nodes, spec 16.6"},
    {0x0002, "diag_interval_s", Owner::BridgeGlobal, Access::ReadWrite, PType::U16, 10,
     3600, 60, "s", "Diagnostics publication period (BF-19)"},
    {0x0003, "missed_poll_threshold", Owner::BridgeGlobal, Access::ReadWrite, PType::U8, 1,
     20, 3, nullptr, "Unanswered polls before offline, spec 16.5"},
    {0x0004, "poll_reply_timeout_ms", Owner::BridgeGlobal, Access::ReadWrite, PType::U16,
     2000, 30000, 10000, "ms", "Poll outstanding before it counts as missed"},
    {0x0005, "command_ack_timeout_ms", Owner::BridgeGlobal, Access::ReadWrite, PType::U16,
     500, 30000, 3000, "ms", "ACK wait before retry, Impl Plan 6.2"},
    {0x0006, "cmd_retries", Owner::BridgeGlobal, Access::ReadWrite, PType::U8, 0, 10, 3,
     nullptr, "Retries after the first, SAME seq"},
    {0x0007, "cad_retries", Owner::BridgeGlobal, Access::ReadWrite, PType::U8, 0, 10, 5,
     nullptr, "CAD attempts before transmitting regardless, spec 12.3"},
    {0x0008, "backoff_max_ms", Owner::BridgeGlobal, Access::ReadWrite, PType::U16, 100,
     5000, 1500, "ms", "Upper bound of the random CAD backoff, spec 12.3"},
    {0x0009, "frag_reassembly_timeout_ms", Owner::BridgeGlobal, Access::ReadWrite,
     PType::U16, 500, 30000, 5000, "ms", "Fragment set window, spec 11.2"},
    {0x000A, "error_min_interval_ms", Owner::BridgeGlobal, Access::ReadWrite, PType::U16,
     100, 60000, 1000, "ms", "Floor between ERRORs to one peer, spec 14.2"},
    {0x000B, "config_readback_timeout_ms", Owner::BridgeGlobal, Access::ReadWrite,
     PType::U16, 1000, 60000, 15000, "ms",
     "Wait for a split readback to complete, spec 7.4.1 (D57)"},
    {0x000C, "config_ack_timeout_ms", Owner::BridgeGlobal, Access::ReadWrite, PType::U16,
     1000, 60000, 8000, "ms", "CONFIG_ACK wait before the outcome is unknown, spec 7.4"},
    {0x000D, "republish_interval_s", Owner::BridgeGlobal, Access::ReadWrite, PType::U16,
     60, 3600, 900, "s", "Unchanged state republished at most this often, Impl Plan 6.3"},
    {0x000E, "bms_stale_s", Owner::BridgeGlobal, Access::ReadWrite, PType::U16, 30, 3600,
     600, "s", "bms_age_s above this marks BMS entities unavailable, spec 16.4"},
    {0x000F, "cell_mv_deadband", Owner::BridgeGlobal, Access::ReadWrite, PType::U8, 0, 50,
     5, "mV", "Cell voltage change that republishes, spec 16.4; 0 = any"},

    // D59 - the bridge's copy of the PHY group, the fleet's reference (spec 12.4). Set on
    // lran/bridge/config/set alone, under the node rows' names, as cad_retries already
    // is. Every field but the id and the owner must equal the node row of the same name,
    // because the bridge clamps against these rows before it sends a node anything
    // (spec 12.4.1 step 1); phy_rows_agree() below checks it.
    {0x0010, "freq_hz", Owner::BridgeGlobal, Access::Phy, PType::U32, 902000000, 928000000,
     917400000, "Hz", "Channel, spec 12.1 - fleet-wide"},
    {0x0011, "spreading_factor", Owner::BridgeGlobal, Access::Phy, PType::U8, 7, 12, 9,
     nullptr, "SF, spec 12.1 - fleet-wide"},
    {0x0012, "bandwidth_khz", Owner::BridgeGlobal, Access::Phy, PType::U16, 125, 500, 125,
     "kHz", "BW - 125 until an envelope decision, spec 18.2"},
    {0x0013, "coding_rate_denominator", Owner::BridgeGlobal, Access::Phy, PType::U8, 5, 8,
     5, nullptr, "CR 4/N, spec 12.1"},
    {0x0014, "tx_power_dbm", Owner::BridgeGlobal, Access::Phy, PType::I16, -9, -4, -4,
     "dBm", "Conducted; the maximum IS D33's ceiling, spec 18.2"},
    {0x0015, "phy_trial_s", Owner::BridgeGlobal, Access::Phy, PType::U16, 30, 900, 120,
     "s", "Revert window after a PHY change, spec 12.4"},

    // BF-28, BF-29 - the HEX proxy (Bridge PRD 3.5). 0x0016-0x001F stay free beside the
    // PHY rows. The arm timeout is PRD R-3.5b's, name and default. The response wait
    // covers GateLink's own hex_timeout_ms (1000) plus a frame each way at SF9 and a CAD
    // backoff, so a node's TIMEOUT answer arrives before the bridge gives up on it.
    {0x0020, "mppt_write_arm_timeout_s", Owner::BridgeGlobal, Access::ReadWrite, PType::U16,
     30, 3600, 300, "s", "MPPT write-enable auto-expiry, PRD R-3.5b"},
    {0x0021, "hex_rsp_timeout_ms", Owner::BridgeGlobal, Access::ReadWrite, PType::U16, 1500,
     30000, 3000, "ms", "HEX_RSP wait before a read is retried, PRD R-3.5a"},
    // A setting changed behind the bridge, with VictronConnect say, shows in HA only after
    // a pass. Off by default, because a pass is ten HEX exchanges on the air; chosen with
    // the operator on 2026-09-27. 720 h keeps the interval inside millis()'s 49-day wrap.
    {0x0022, "charge_readback_interval_h", Owner::BridgeGlobal, Access::ReadWrite, PType::U16,
     0, 720, 0, "h", "Periodic MPPT charge readback, Impl Plan 6.4; 0 = boot and writes only"},
    {0x0080, "poll_interval_s", Owner::BridgePerNode, Access::ReadWrite, PType::U16, 10,
     3600, 60, "s", "Poll period for this node, BG-4"},
    // D61 - 1 polls and watches the node from boot, so it counts toward spec 12.4.1's fleet
    // even when silent. 0 enrols it once heard, as a bench node is. Clearing it takes
    // effect at the next restart, because enrolment is never undone within a boot.
    {0x0081, "deployed", Owner::BridgePerNode, Access::ReadWrite, PType::Bool, 0, 1, 0,
     nullptr, "Polled from boot, D61"},
};

// 0x0100-0x01FF - every node. dedup_cache_depth is D34's, and is runtime-settable because
// a node that cannot be reflashed without a walk to the gate may not carry a fixed sizing
// constant either (root rule 8). Its max is the compiled cache size.
inline constexpr ParamDef kNodeCommonParams[] = {
    {0x0100, "dedup_cache_depth", Owner::Node, Access::ReadWrite, PType::U8, 1, 32, 8,
     nullptr, "Cached command results, spec 10.4"},
    {0x0101, "frag_reassembly_timeout_ms", Owner::Node, Access::ReadWrite, PType::U16, 500,
     30000, 5000, "ms", "Fragment set window, spec 11.2"},
    {0x0102, "cad_retries", Owner::Node, Access::ReadWrite, PType::U8, 0, 10, 5, nullptr,
     "CAD attempts before transmitting regardless, spec 12.3"},
    {0x0103, "backoff_max_ms", Owner::Node, Access::ReadWrite, PType::U16, 100, 5000, 1500,
     "ms", "Upper bound of the random CAD backoff, spec 12.3"},

    // D56 - the PHY, spec 12.1's table. Every node holds a copy and so does the bridge,
    // because one SX1262 listens on one configuration: a change is a fleet operation.
    // A node's Store answers these READ_ONLY until that node builds spec 12.4's
    // commit-and-revert and enables the trial, so HA can read the working point before it
    // can change it. The defaults are D1's.
    {0x0110, "freq_hz", Owner::Node, Access::Phy, PType::U32, 902000000, 928000000,
     917400000, "Hz", "Channel, spec 12.1 - fleet-wide"},
    {0x0111, "spreading_factor", Owner::Node, Access::Phy, PType::U8, 7, 12, 9,
     nullptr, "SF, spec 12.1 - fleet-wide"},
    {0x0112, "bandwidth_khz", Owner::Node, Access::Phy, PType::U16, 125, 500, 125,
     "kHz", "BW - 125 until an envelope decision, spec 18.2"},
    {0x0113, "coding_rate_denominator", Owner::Node, Access::Phy, PType::U8, 5, 8, 5,
     nullptr, "CR 4/N, spec 12.1"},
    {0x0114, "tx_power_dbm", Owner::Node, Access::Phy, PType::I16, -9, -4, -4, "dBm",
     "Conducted; the maximum IS D33's ceiling, spec 18.2"},
    {0x0115, "phy_trial_s", Owner::Node, Access::Phy, PType::U16, 30, 900, 120, "s",
     "Revert window after a PHY change, spec 12.4"},
};

// 0x1000-0x1FFF - GateLink, task L4. The rows are GateLink PRD's named parameters and
// Impl Plan 4.4's, plus four the PRD requires without naming (operator, 2026-10-01).
// Defaults are the documents'; every range is proposed, none is measured.
//
// Grouped by subsystem, a 0x10 stride apart, so a row added later joins its group without
// renumbering one HA already knows. The readback walks them in id order (spec 7.4.1).
//
// THESE ROWS OUTGROW ONE CONFIG_ACK. With node-common and the PHY group, a GateLink
// readback is 199 bytes against spec 7.4's 193, so it needs spec 7.4.1's MORE_FOLLOWS
// split. readback_bytes() below counts it, and test_table pins the count.
inline constexpr ParamDef kGateLinkParams[] = {
    // Relays. R-3.1.2b's spacing has no name in the PRD; its default equals the pulse.
    {0x1000, "relay_pulse_ms", Owner::Node, Access::ReadWrite, PType::U16, 100, 2000, 500,
     "ms", "Relay pulse width, PRD R-3.1.2a"},
    {0x1001, "relay_min_spacing_ms", Owner::Node, Access::ReadWrite, PType::U16, 100, 5000,
     500, "ms", "Least gap between two relay pulses, PRD R-3.1.2b"},
    {0x1002, "unlock_settle_ms", Owner::Node, Access::ReadWrite, PType::U16, 100, 5000, 500,
     "ms", "K2 to K4 on an immediate close, Impl Plan 6.1"},
    {0x1003, "post_wake_settle_ms", Owner::Node, Access::ReadWrite, PType::U16, 0, 5000, 500,
     "ms", "After a pulse, before state is read, PRD R-3.1.3c"},
    {0x1004, "command_confirm_timeout_s", Owner::Node, Access::ReadWrite, PType::U8, 2, 60, 5,
     "s", "Movement wait before a command fails, PRD R-3.1.3c"},

    // Inputs.
    {0x1010, "input_poll_ms", Owner::Node, Access::ReadWrite, PType::U16, 20, 500, 100, "ms",
     "Input expander poll period, PRD R-3.1.4d"},
    {0x1011, "input_debounce_samples", Owner::Node, Access::ReadWrite, PType::U8, 1, 10, 2,
     nullptr, "Agreeing reads before an input changes, PRD R-3.1.4d"},
    {0x1012, "hold_confirm_ms", Owner::Node, Access::ReadWrite, PType::U16, 500, 10000, 2000,
     "ms", "Stable OPEN without MOVING before a hold, PRD R-3.1.4b"},

    // Detection. u32 because a u16 of ms stops at 65.5 s, barely above the 60 s default.
    {0x1020, "detect_sequence_window_ms", Owner::Node, Access::ReadWrite, PType::U32, 5000,
     300000, 60000, "ms", "Longest EXIT-to-SAFETY traversal, PRD R-3.2.3e"},
    {0x1021, "detect_sequence_idle_ms", Owner::Node, Access::ReadWrite, PType::U16, 1000,
     60000, 10000, "ms", "Quiet time that resets classification, PRD R-3.2.3e"},
    {0x1022, "held_open_alert_repeat_s", Owner::Node, Access::ReadWrite, PType::U16, 0, 3600,
     0, "s", "Held-open alert repeat, PRD R-3.2.3e; 0 = one per detection"},
    {0x1023, "cause_window_ms", Owner::Node, Access::ReadWrite, PType::U16, 1000, 60000,
     10000, "ms", "EXIT edge to gate opening, read as the wand, Impl Plan 6.3"},

    // VE.Direct. hex_timeout_ms stops at 2000 so that it stays under the bridge's
    // hex_rsp_timeout_ms default of 3000, which assumes GateLink answers first.
    {0x1030, "hex_timeout_ms", Owner::Node, Access::ReadWrite, PType::U16, 200, 2000, 1000,
     "ms", "MPPT HEX reply wait before TIMEOUT, PRD R-3.3d"},
    {0x1031, "vedirect_stale_s", Owner::Node, Access::ReadWrite, PType::U8, 2, 60, 5, "s",
     "No complete text block for this long is stale, PRD R-3.3f"},

    // Battery. bms_poll_s's floor keeps R-3.4e's phone access between polls.
    {0x1040, "bms_poll_s", Owner::Node, Access::ReadWrite, PType::U16, 60, 3600, 300, "s",
     "BLE BMS read period, PRD R-3.4a"},
    {0x1041, "charge_inhibit_confirm_s", Owner::Node, Access::ReadWrite, PType::U16, 60, 3600,
     300, "s", "Inferred charge inhibit must hold this long, PRD R-6.4b"},

    // Panel and bench. The buzzer has no requirement yet, so it starts silent.
    {0x1050, "display_timeout_s", Owner::Node, Access::ReadWrite, PType::U16, 10, 3600, 60,
     "s", "Backlight inactivity timeout, PRD R-4.7b"},
    {0x1051, "buzzer_enable", Owner::Node, Access::ReadWrite, PType::Bool, 0, 1, 0, nullptr,
     "Panel buzzer, Impl Plan 5.2 ui_task"},
    {0x1052, "inject_spacing_ms", Owner::Node, Access::ReadWrite, PType::U16, 100, 60000,
     1000, "ms", "Gap between injected inputs, PRD R-5.4b"},
};

// 0x2000-0x2FFF WellLink is declared by its own milestone. W10's count is in Protocol
// Library Plan 4.

inline constexpr size_t kBridgeParamCount     = sizeof(kBridgeParams) / sizeof(ParamDef);
inline constexpr size_t kNodeCommonParamCount = sizeof(kNodeCommonParams) / sizeof(ParamDef);
inline constexpr size_t kGateLinkParamCount   = sizeof(kGateLinkParams) / sizeof(ParamDef);

// One table is at most this many rows across all its blocks. Node-common's 10 plus
// GateLink's 19 leaves room; raising it costs RAM in every Store.
inline constexpr size_t kMaxTableParams = 64;

// A node's own block, by address. The bridge reads it to name, clamp and discover a
// node's rows; a node's firmware adds it to its Table after node-common. Bench nodes
// have none, whatever role they play, because spec 16.6 keeps them out of discovery.
struct ParamBlock {
  const ParamDef* rows;
  size_t          n;
};
constexpr ParamBlock node_block(NodeId node) {
  if (node == kNodeGateLink) return {kGateLinkParams, kGateLinkParamCount};
  return {nullptr, 0};
}

// spec 12.1 - freq_hz's ceiling is what bounds Value at int32_t. A u32 parameter above
// 2^31 needs a wider Value before it can be declared.
static_assert(928000000 < INT32_MAX, "a u32 parameter above 2^31 needs a wider Value");

// D44's one free property: a generator would have given uniqueness and ordering without
// asking. Ascending order is also what spec 7.4.1 asks a GET_ALL walk to produce, so the
// check earns its place twice.
constexpr bool ids_ascending(const ParamDef* p, size_t n) {
  for (size_t i = 1; i < n; ++i) {
    if (!(p[i - 1].id < p[i].id)) return false;
  }
  return true;
}
static_assert(ids_ascending(kBridgeParams, kBridgeParamCount),
              "bridge param ids must be unique and ascending");
static_assert(ids_ascending(kNodeCommonParams, kNodeCommonParamCount),
              "node-common param ids must be unique and ascending");
static_assert(ids_ascending(kGateLinkParams, kGateLinkParamCount),
              "GateLink param ids must be unique and ascending");

constexpr bool in_block(const ParamDef* p, size_t n, uint16_t lo, uint16_t hi) {
  for (size_t i = 0; i < n; ++i) {
    if (p[i].id < lo || p[i].id > hi) return false;
  }
  return true;
}
static_assert(in_block(kBridgeParams, kBridgeParamCount, 0x0001, 0x00FF),
              "D46 - the bridge's block is 0x0000-0x00FF");
static_assert(in_block(kNodeCommonParams, kNodeCommonParamCount, 0x0100, 0x01FF),
              "D46 - node-common is 0x0100-0x01FF");
static_assert(in_block(kGateLinkParams, kGateLinkParamCount, 0x1000, 0x1FFF),
              "D46 - GateLink is 0x1000-0x1FFF");

constexpr bool defaults_in_range(const ParamDef* p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (p[i].min > p[i].max) return false;
    if (p[i].def < p[i].min || p[i].def > p[i].max) return false;
  }
  return true;
}
static_assert(defaults_in_range(kBridgeParams, kBridgeParamCount),
              "a default outside its own range would be clamped on first read");
static_assert(defaults_in_range(kNodeCommonParams, kNodeCommonParamCount),
              "a default outside its own range would be clamped on first read");
static_assert(defaults_in_range(kGateLinkParams, kGateLinkParamCount),
              "a default outside its own range would be clamped on first read");

// spec 12.4 - frequency, SF, BW, CR, TX power and phy_trial_s.
inline constexpr size_t kPhyGroupSize = 6;

// D59 - the bridge's PHY rows and the node's must agree in everything but id and owner.
// A narrower node range would clamp a value the bridge had already accepted, and spec
// 12.4.1 step 4 abandons the change on any clamp, so every set would fail; a wider one
// would let the bridge send a value its own radio could not take.
constexpr bool same_text(const char* a, const char* b) {
  for (; *a != '\0' && *a == *b; ++a, ++b) {}
  return *a == *b;
}
constexpr bool phy_rows_agree(const ParamDef* b, size_t nb, const ParamDef* n, size_t nn) {
  size_t matched = 0;
  for (size_t i = 0; i < nb; ++i) {
    if (b[i].access != Access::Phy) continue;
    bool found = false;
    for (size_t j = 0; j < nn; ++j) {
      if (n[j].access != Access::Phy || !same_text(b[i].name, n[j].name)) continue;
      if (b[i].type != n[j].type || b[i].min != n[j].min || b[i].max != n[j].max ||
          b[i].def != n[j].def) {
        return false;
      }
      found = true;
    }
    if (!found) return false;
    ++matched;
  }
  size_t node_phy = 0;
  for (size_t j = 0; j < nn; ++j) {
    if (n[j].access == Access::Phy) ++node_phy;
  }
  return matched == node_phy && matched == kPhyGroupSize;
}

static_assert(phy_rows_agree(kBridgeParams, kBridgeParamCount, kNodeCommonParams,
                             kNodeCommonParamCount),
              "D59 - the bridge's PHY rows must equal the node's, name for name");

// spec 8.12, 12.4, D64 - a parameter that takes only listed values inside its range. A
// value inside the range and off the list answers INVALID_VALUE and applies nothing; one
// outside the range still clamps, to an endpoint the list must therefore hold. Keyed by
// name, so the bridge's PHY rows and the node's share one list as phy_rows_agree() makes
// them share everything else. bandwidth_khz's are the SX1262's LoRa bandwidths the
// envelope can reach: RadioLib's setBandwidth() refuses 300 only at the retune, after
// the value had been accepted and stored.
struct ParamPoints {
  const char* name;
  Value       points[3];
  size_t      n;
};
inline constexpr ParamPoints kParamPoints[] = {
    {"bandwidth_khz", {125, 250, 500}, 3},
};

// Null when every value in the row's range is allowed.
constexpr const ParamPoints* points_for(const ParamDef& d) {
  for (const ParamPoints& p : kParamPoints) {
    if (same_text(d.name, p.name)) return &p;
  }
  return nullptr;
}

constexpr bool value_allowed(const ParamDef& d, Value v) {
  const ParamPoints* p = points_for(d);
  if (p == nullptr) return true;
  for (size_t i = 0; i < p->n; ++i) {
    if (p->points[i] == v) return true;
  }
  return false;
}

// A default off the list would answer its own readback with a value no SET could
// reproduce, and a clamp must land on a listed value.
constexpr bool points_hold_ends(const ParamDef* p, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (!value_allowed(p[i], p[i].min) || !value_allowed(p[i], p[i].max) ||
        !value_allowed(p[i], p[i].def)) {
      return false;
    }
  }
  return true;
}
static_assert(points_hold_ends(kBridgeParams, kBridgeParamCount),
              "D64 - a listed row's min, max and default must be on its list");
static_assert(points_hold_ends(kNodeCommonParams, kNodeCommonParamCount),
              "D64 - a listed row's min, max and default must be on its list");
static_assert(points_hold_ends(kGateLinkParams, kGateLinkParamCount),
              "D64 - a listed row's min, max and default must be on its list");

// The wire width of a ptype, in bytes. spec 7.4, D55 - `len` is a multiple of it.
constexpr size_t ptype_width(PType t) {
  switch (t) {
    case PType::U8:
    case PType::Bool: return 1;
    case PType::U16:
    case PType::I16: return 2;
    case PType::U32:
    case PType::I32: return 4;
  }
  return 0;
}

// spec 7.4 - the bytes a full readback of these rows takes in CONFIG_ACK results: each
// entry is a 5-byte header and its value. Library Plan 4 counts readbacks with it.
constexpr size_t readback_bytes(const ParamDef* p, size_t n) {
  size_t b = 0;
  for (size_t i = 0; i < n; ++i) b += 5 + ptype_width(p[i].type);
  return b;
}

}  // namespace config
}  // namespace lran
