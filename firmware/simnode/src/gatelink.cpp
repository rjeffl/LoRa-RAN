// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// ROLE_GATELINK - schema 0xFE status, 0x11 events, COMMAND and COMMAND_ACK, 0x12 config.
// Task BF-6; Impl Plan 10.2; spec 6.2-6.4, 7.2-7.4, 9.4, 10.1-10.4.
//
// THE PROTOCOL IS LRAN-NODE'S since GateLink task L1: the command path (D34 as amended),
// the CONFIG path, the HEX transport and the BOOT announcement run in lran::node::Engine.
// This file is ROLE_GATELINK's application - what a command does to a simnode, the
// synthetic 0xFE status, the RAM parameter store, the simulated MPPT and the faults.
//
// THE SYNTHETIC MARKER IS THE SCHEMA. Every status this role sends is schema 0xFE, never
// 0x10 (spec 7.1, bench only), so a status_reason can be a real one and the bridge's
// handling of it can be tested (decided with the operator 2026-09-14).
//
#include "gatelink.h"

#include <climits>
#include <cstdlib>
#include <cstring>

#include "lran/messages.h"
#include "node.h"

namespace simnode {
namespace {

using lran::schema::GateLinkStatusV1;

// Resets copy from these rather than assigning a braced temporary. GCC 13.3 (CI's
// ubuntu-24.04 native build) hits an internal compiler error gimplifying
// `x = lran::schema::NodeConfigAckV1{};` - an aggregate whose array member carries
// a default member initializer. Clang and the Xtensa GCC compile it; the copy is the
// same bytes either way.
const GateLinkStatusV1                  kEmptyStatus{};

// ---------------------------------------------------------------------------
// The field table - every GateLinkStatusV1 member but status_reason, which `push` owns.
// ---------------------------------------------------------------------------

enum class FieldType : uint8_t { U8, U16, I16, U32, CellMv, CellTemp };

struct FieldInfo {
  const char* name;
  FieldType   type;
  uint8_t     index;  // CellMv and CellTemp only
  uint8_t GateLinkStatusV1::*  u8;
  uint16_t GateLinkStatusV1::* u16;
  int16_t GateLinkStatusV1::*  i16;
  uint32_t GateLinkStatusV1::* u32;
};

constexpr FieldInfo f8(const char* n, uint8_t GateLinkStatusV1::*m) {
  return {n, FieldType::U8, 0, m, nullptr, nullptr, nullptr};
}
constexpr FieldInfo f16(const char* n, uint16_t GateLinkStatusV1::*m) {
  return {n, FieldType::U16, 0, nullptr, m, nullptr, nullptr};
}
constexpr FieldInfo fi16(const char* n, int16_t GateLinkStatusV1::*m) {
  return {n, FieldType::I16, 0, nullptr, nullptr, m, nullptr};
}
constexpr FieldInfo f32(const char* n, uint32_t GateLinkStatusV1::*m) {
  return {n, FieldType::U32, 0, nullptr, nullptr, nullptr, m};
}
constexpr FieldInfo fcell(const char* n, FieldType t, uint8_t i) {
  return {n, t, i, nullptr, nullptr, nullptr, nullptr};
}

using S = GateLinkStatusV1;
constexpr FieldInfo kFields[] = {
    f8("gate_state", &S::gate_state),
    f8("input_bits", &S::input_bits),
    f8("hold", &S::hold),
    f8("movement_cause", &S::movement_cause),
    f8("last_direction", &S::last_direction),
    f8("detect_flags", &S::detect_flags),
    f32("last_traversal_age_s", &S::last_traversal_age_s),
    f16("batt_mv", &S::batt_mv),
    fi16("batt_ma", &S::batt_ma),
    f16("pv_cv", &S::pv_cv),
    f16("pv_w", &S::pv_w),
    fi16("load_ma", &S::load_ma),
    f16("yield_today", &S::yield_today),
    f16("yield_yest", &S::yield_yest),
    f16("pmax_today", &S::pmax_today),
    f32("yield_total", &S::yield_total),
    f8("charge_state", &S::charge_state),
    f8("mppt_err", &S::mppt_err),
    f8("mppt_tracker", &S::mppt_tracker),
    f8("mppt_flags", &S::mppt_flags),
    fi16("mppt_temp_c10", &S::mppt_temp_c10),
    f8("bms_soc", &S::bms_soc),
    f8("bms_flags", &S::bms_flags),
    f16("pack_mv", &S::pack_mv),
    fi16("pack_ma", &S::pack_ma),
    f8("cell_count", &S::cell_count),
    f8("bms_rssi_neg", &S::bms_rssi_neg),
    fcell("cell_mv0", FieldType::CellMv, 0),
    fcell("cell_mv1", FieldType::CellMv, 1),
    fcell("cell_mv2", FieldType::CellMv, 2),
    fcell("cell_mv3", FieldType::CellMv, 3),
    fcell("cell_temp_c0", FieldType::CellTemp, 0),
    fcell("cell_temp_c1", FieldType::CellTemp, 1),
    fcell("cell_temp_c2", FieldType::CellTemp, 2),
    fcell("cell_temp_c3", FieldType::CellTemp, 3),
    f16("bms_cycles", &S::bms_cycles),
    f16("bms_capacity_dah", &S::bms_capacity_dah),
    f16("bms_alarms", &S::bms_alarms),
    f16("bms_age_s", &S::bms_age_s),
    f32("uptime_s", &S::uptime_s),
    f16("boot_count", &S::boot_count),
    f16("node_mv", &S::node_mv),
    fi16("node_ma", &S::node_ma),
    fi16("enclosure_temp_c10", &S::enclosure_temp_c10),
    f8("node_flags", &S::node_flags),
};
constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

// spec 7.2.8 - the node_flags bits the simnode generates from its own settings.
constexpr uint8_t kNodeFlagDryRun     = 0x04;
constexpr uint8_t kNodeFlagBmsPolling = 0x08;
constexpr uint8_t kNodeFlagDebug      = 0x10;
constexpr uint8_t kNodeFlagAgeNotPersisted = 0x40;

// spec 7.2.6 - mppt_flags bit 2, generated while the node waits on the MPPT.
constexpr uint8_t kMpptFlagHexOutstanding = 0x04;

// spec 8.12 - one CONFIG_ACK result, from the RAM store. The store holds no defaults, so
// every value in it is one a SET wrote, and spec 7.4 marks it OVERRIDE (D68).
lran::schema::ConfigAckEntry result_of(const StoredParam& p, lran::ParamStatus status) {
  lran::schema::ConfigAckEntry a;
  a.param_id    = p.param_id;
  a.status      = status;
  a.is_override = true;
  a.ptype    = p.ptype;
  a.len      = p.len;
  std::memcpy(a.value, p.value, sizeof(a.value));
  return a;
}

StoredParam* find_param(GateLinkState& gl, uint16_t id) {
  for (StoredParam& p : gl.params) {
    if (p.used && p.param_id == id) return &p;
  }
  return nullptr;
}

lran::schema::ConfigAckEntry set_param(GateLinkState& gl, const lran::schema::ConfigEntry& in) {
  lran::schema::ConfigAckEntry a;
  a.param_id = in.param_id;
  a.ptype    = in.ptype;
  a.len      = 0;

  const size_t want = lran::schema::param_value_len(in.ptype);
  if (want == 0 || want != in.len) {
    a.status = lran::ParamStatus::TypeMismatch;
    return a;
  }
  StoredParam* slot = find_param(gl, in.param_id);
  if (slot != nullptr && slot->ptype != in.ptype) {
    // The effective value is the one already held (spec 7.4), so the ACK carries it.
    return result_of(*slot, lran::ParamStatus::TypeMismatch);
  }
  if (slot == nullptr) {
    for (StoredParam& p : gl.params) {
      if (!p.used) {
        slot = &p;
        break;
      }
    }
  }
  if (slot == nullptr) {
    // The store is full. UNKNOWN_PARAM is the nearest spec 8.12 status; the log says why.
    a.status = lran::ParamStatus::UnknownParam;
    return a;
  }
  slot->used     = true;
  slot->param_id = in.param_id;
  slot->ptype    = in.ptype;
  slot->len      = in.len;
  std::memcpy(slot->value, in.value, sizeof(slot->value));
  return result_of(*slot, lran::ParamStatus::Ok);
}

}  // namespace

// ---------------------------------------------------------------------------
// Telemetry
// ---------------------------------------------------------------------------

void reset_gatelink_telemetry(GateLinkStatusV1* s) {
  *s                      = kEmptyStatus;
  s->gate_state           = static_cast<uint8_t>(lran::GateState::Closed);
  s->last_traversal_age_s = 3600;
  s->batt_mv              = 13300;
  s->batt_ma              = 250;
  s->pv_cv                = 1850;
  s->pv_w                 = 5;
  s->load_ma              = 120;
  s->yield_today          = 12;
  s->yield_yest           = 15;
  s->pmax_today           = 9;
  s->yield_total          = 4210;
  s->charge_state         = 3;
  s->mppt_tracker         = 2;
  s->bms_soc              = 87;
  s->pack_mv              = 13280;
  s->pack_ma              = 200;
  s->cell_count           = 4;
  s->bms_rssi_neg         = 70;
  for (uint8_t i = 0; i < lran::schema::kMaxCells; ++i) {
    s->cell_mv[i]     = 3320;
    s->cell_temp_c[i] = 21;
  }
  s->bms_cycles         = 42;
  s->bms_capacity_dah   = 1000;
  s->bms_age_s          = 5;
  s->node_mv            = 5000;
  s->node_ma            = 80;
  s->enclosure_temp_c10 = 215;
  // Nothing on a simnode persists: bit 0 (config persisted) clear, bit 6 set.
  s->node_flags = kNodeFlagAgeNotPersisted;
  // mppt_temp_c10 keeps its sentinel: a SmartSolar 75/15 reports no temperature.
}

size_t build_gatelink_status(const Identity& e, lran::StatusReason reason, uint32_t now_ms,
                             uint8_t* out, size_t cap) {
  GateLinkStatusV1 s = e.gl.status;
  if (!e.gl.uptime_set) s.uptime_s = now_ms / 1000;
  s.node_flags = static_cast<uint8_t>(
      (s.node_flags & ~(kNodeFlagDryRun | kNodeFlagBmsPolling | kNodeFlagDebug)) |
      (e.gl.dry_run ? kNodeFlagDryRun : 0) | (e.gl.bms_polling ? kNodeFlagBmsPolling : 0) |
      (e.gl.debug_modes != 0 ? kNodeFlagDebug : 0));
  s.mppt_flags = static_cast<uint8_t>((s.mppt_flags & ~kMpptFlagHexOutstanding) |
                                      (e.hex_pending.active ? kMpptFlagHexOutstanding : 0));
  s.status_reason = static_cast<uint8_t>(reason);
  size_t n = 0;
  return lran::schema::serialize(s, out, cap, &n) == lran::Status::Ok ? n : 0;
}

lran::schema::GateLinkEventV1 make_event(Identity& e, lran::EventType type, uint32_t now_ms) {
  lran::schema::GateLinkEventV1 ev;
  ev.event_type  = static_cast<uint8_t>(type);
  ev.hold_source = static_cast<uint8_t>((e.gl.status.hold >> 1) & 0x07);  // spec 7.2.1
  ev.direction   = static_cast<uint8_t>(type == lran::EventType::VehicleDetected ||
                                                type == lran::EventType::VehicleWhileHeldOpen
                                            ? lran::Direction::Undetermined
                                            : lran::Direction::None);
  ev.gate_state  = e.gl.status.gate_state;
  ev.input_bits  = e.gl.status.input_bits;
  ev.event_id    = e.gl.next_event_id++;
  ev.uptime_s    = e.gl.uptime_set ? e.gl.status.uptime_s : now_ms / 1000;
  return ev;
}

const char* field_result_name(FieldResult r) {
  switch (r) {
    case FieldResult::Ok:           return "ok";
    case FieldResult::UnknownField: return "unknown field";
    case FieldResult::BadValue:     return "value out of range for the field";
    case FieldResult::NoSentinel:   return "field has no not-available sentinel";
  }
  return "?";
}

size_t field_count() { return kFieldCount; }

const char* field_name(size_t i) { return i < kFieldCount ? kFields[i].name : nullptr; }

long long field_value(const GateLinkState& gl, size_t i) {
  if (i >= kFieldCount) return 0;
  const FieldInfo& f = kFields[i];
  const S&         s = gl.status;
  switch (f.type) {
    case FieldType::U8:       return s.*f.u8;
    case FieldType::U16:      return s.*f.u16;
    case FieldType::I16:      return s.*f.i16;
    case FieldType::U32:      return s.*f.u32;
    case FieldType::CellMv:   return s.cell_mv[f.index];
    case FieldType::CellTemp: return s.cell_temp_c[f.index];
  }
  return 0;
}

FieldResult field_set(GateLinkState* gl, const char* name, const char* value) {
  const FieldInfo* f = nullptr;
  for (const FieldInfo& c : kFields) {
    if (std::strcmp(c.name, name) == 0) {
      f = &c;
      break;
    }
  }
  if (f == nullptr) return FieldResult::UnknownField;
  S& s = gl->status;

  if (std::strcmp(value, "na") == 0) {
    // Root rule 6 - the sentinel for the width. Only bms_soc among the u8 fields has one
    // (spec 7.2.3); the cells have none in the specification.
    switch (f->type) {
      case FieldType::U16: s.*f->u16 = lran::kU16NotAvailable; break;
      case FieldType::I16: s.*f->i16 = lran::kI16NotAvailable; break;
      case FieldType::U32: s.*f->u32 = lran::kU32NotAvailable; break;
      case FieldType::U8:
        if (f->u8 != &S::bms_soc) return FieldResult::NoSentinel;
        s.bms_soc = lran::kSocNotAvailable;
        break;
      case FieldType::CellMv:
      case FieldType::CellTemp:
        return FieldResult::NoSentinel;
    }
    if (f->u32 == &S::uptime_s) gl->uptime_set = true;
    return FieldResult::Ok;
  }

  char*           end = nullptr;
  const long long v   = std::strtoll(value, &end, 0);
  if (*value == '\0' || *end != '\0') return FieldResult::BadValue;

  switch (f->type) {
    case FieldType::U8:
      if (v < 0 || v > 0xFF) return FieldResult::BadValue;
      s.*f->u8 = static_cast<uint8_t>(v);
      break;
    case FieldType::U16:
    case FieldType::CellMv:
      if (v < 0 || v > 0xFFFF) return FieldResult::BadValue;
      if (f->type == FieldType::U16) {
        s.*f->u16 = static_cast<uint16_t>(v);
      } else {
        s.cell_mv[f->index] = static_cast<uint16_t>(v);
      }
      break;
    case FieldType::I16:
      if (v < INT16_MIN || v > INT16_MAX) return FieldResult::BadValue;
      s.*f->i16 = static_cast<int16_t>(v);
      break;
    case FieldType::U32:
      if (v < 0 || v > 0xFFFFFFFFLL) return FieldResult::BadValue;
      s.*f->u32 = static_cast<uint32_t>(v);
      break;
    case FieldType::CellTemp:
      if (v < INT8_MIN || v > INT8_MAX) return FieldResult::BadValue;
      s.cell_temp_c[f->index] = static_cast<int8_t>(v);
      break;
  }
  if (f->u32 == &S::uptime_s) gl->uptime_set = true;
  return FieldResult::Ok;
}


// ---------------------------------------------------------------------------
// Node::App - each role's half of lran-node's engine
// ---------------------------------------------------------------------------

uint8_t Node::App::capabilities(const lran::node::Context& c) const {
  using namespace lran::node;
  switch (as_identity(c).role) {
    case Role::GateLink:
      return kAnswersCommands | kAnswersConfig | kAnswersHex | kRefusesAuthenticated;
    case Role::Range:
    case Role::Health:
      // BF-33 - a CONFIG, for the PHY group only (config_set() holds nothing for them),
      // because the bridge moves every node it polls (spec 12.4.1).
      return kAnswersConfig;
    case Role::Fault:
      return 0;
  }
  return 0;
}

lran::node::RandomFn Node::App::random() const { return node_->ids_->random_fn(); }

bool Node::App::on_poll(lran::node::Engine&, lran::node::Context& c, const lran::Header& hdr,
                        const uint8_t* payload, size_t len, uint32_t now_ms) {
  Identity& e = as_identity(c);
  switch (e.role) {
    case Role::Range:
    case Role::Health:
      node_->answer_poll(e, hdr, now_ms);
      return true;
    case Role::GateLink:
      node_->answer_poll_gatelink(e, hdr, payload, len, now_ms);
      return true;
    case Role::Fault:
      return false;
  }
  return false;
}

bool Node::App::on_ping(lran::node::Engine&, lran::node::Context& c, const lran::Header& hdr,
                        const uint8_t* payload, size_t len, uint8_t fragments, int16_t rssi_dbm,
                        int16_t snr_db10, uint32_t now_ms) {
  return node_->on_ping(as_identity(c), hdr, payload, len, fragments, rssi_dbm, snr_db10, now_ms);
}

lran::node::CommandOutcome Node::App::execute(lran::node::Context& c,
                                              const lran::msg::Command& cmd, uint32_t) {
  using lran::node::AfterAck;
  GateLinkState& gl = as_identity(c).gl;
  // `ack <hex> delay <ms>` stretches the execution window: the engine holds the command in
  // flight and Node::tick() finishes it.
  gl.pending_delay_ms = gl.ack_delay_ms;
  lran::node::CommandOutcome o;
  o.deferred = gl.ack_delay_ms != 0;

  // A simnode has no relay. An actuation command is dispatched to nothing, and counted, so
  // cmd_replay can assert a replay did not dispatch a second time.
  switch (static_cast<lran::Cmd>(cmd.cmd)) {
    case lran::Cmd::Nop:
      break;
    case lran::Cmd::Close:
      if (cmd.arg > 1) {  // spec 8.1 - 0 or 1
        o.result = lran::AckResult::RejectedArg;
        break;
      }
      ++gl.actuations;
      o.result = gl.dry_run ? lran::AckResult::DryRun : lran::AckResult::Accepted;
      break;
    case lran::Cmd::Open:
    case lran::Cmd::HoldOpen:
    case lran::Cmd::ReleaseHold:
      ++gl.actuations;
      o.result = gl.dry_run ? lran::AckResult::DryRun : lran::AckResult::Accepted;
      break;
    case lran::Cmd::RequestStatus:
      o.after = AfterAck::Status;
      break;
    case lran::Cmd::RequestConfig:
      o.after = AfterAck::ConfigReadback;
      break;
    case lran::Cmd::SetDebugMode:
      gl.debug_modes = cmd.arg2;
      break;
    case lran::Cmd::SetRelayDryRun:
      if (cmd.arg > 1) {
        o.result = lran::AckResult::RejectedArg;
        break;
      }
      gl.dry_run = cmd.arg == 1;
      break;
    case lran::Cmd::SetBmsPolling:
      if (cmd.arg > 1) {
        o.result = lran::AckResult::RejectedArg;
        break;
      }
      gl.bms_polling = cmd.arg == 1;
      break;
    case lran::Cmd::RollContext:
      // spec 9.4 - a roll skips steps 4-6, so the engine answers it before the gate and it
      // never reaches here. Refused rather than executed if that ever changes.
      o.result = lran::AckResult::RejectedUnknownCmd;
      break;
    case lran::Cmd::Reboot:
      if (cmd.arg != lran::kRebootGuard) {
        o.result = lran::AckResult::RejectedArg;
        break;
      }
      o.after = AfterAck::Reboot;
      break;
    default:
      o.result = lran::AckResult::RejectedUnknownCmd;
      break;
  }
  return o;
}

size_t Node::App::build_status(const lran::node::Context& c, lran::StatusReason reason,
                               uint32_t now_ms, uint8_t* out, size_t cap, uint8_t* schema) {
  // The synthetic marker is the schema: 0xFE, never 0x10 (spec 7.1).
  *schema = lran::kSchemaSimnodeStatusV1;
  return build_gatelink_status(as_identity(c), reason, now_ms, out, cap);
}

size_t Node::App::build_event(lran::node::Context& c, lran::EventType type, uint16_t detail,
                              uint32_t now_ms, uint8_t* out, size_t cap, uint8_t* schema) {
  Identity&                     e  = as_identity(c);
  lran::schema::GateLinkEventV1 ev = make_event(e, type, now_ms);
  ev.detail                        = detail;
  e.gl.last_event                  = ev;
  e.gl.has_last_event              = true;
  *schema                          = lran::kSchemaGateLinkEventV1;
  size_t n                         = 0;
  return lran::schema::serialize(ev, out, cap, &n) == lran::Status::Ok ? n : 0;
}

// The generic RAM store is ROLE_GATELINK's alone; the other roles hold the PHY group and
// nothing else, so every other row is UNKNOWN_PARAM to them (spec 7.4).
bool Node::App::config_set(lran::node::Context& c, const lran::schema::ConfigEntry& in,
                           lran::schema::ConfigAckEntry* out) {
  Identity& e = as_identity(c);
  if (e.role != Role::GateLink) return false;
  *out = set_param(e.gl, in);
  if (out->status == lran::ParamStatus::UnknownParam) {
    sink_printf(node_->log_, "config %02x: param 0x%04x refused - RAM store full (%u)", e.id,
                static_cast<unsigned>(out->param_id), static_cast<unsigned>(kConfigStoreDepth));
  }
  return true;
}

bool Node::App::config_get(const lran::node::Context& c, uint16_t id,
                           lran::schema::ConfigAckEntry* out) {
  const Identity& e = as_identity(c);
  if (e.role != Role::GateLink) return false;
  for (const StoredParam& p : e.gl.params) {
    if (p.used && p.param_id == id) {
      *out = result_of(p, lran::ParamStatus::Ok);
      return true;
    }
  }
  return false;
}

void Node::App::config_list(const lran::node::Context& c, lran::node::ConfigSink* sink) {
  const Identity& e = as_identity(c);
  if (e.role != Role::GateLink) return;
  for (const StoredParam& p : e.gl.params) {
    if (p.used) sink->add(result_of(p, lran::ParamStatus::Ok));
  }
}

// The generic store has no defaults; restoring them empties it.
void Node::App::config_restore_defaults(lran::node::Context& c) {
  Identity& e = as_identity(c);
  if (e.role != Role::GateLink) return;
  for (StoredParam& p : e.gl.params) p = StoredParam{};
}

// The generic store is RAM, so an override in it is never persisted (D53).
bool Node::App::config_unpersisted(const lran::node::Context& c) const {
  const Identity& e = as_identity(c);
  if (e.role != Role::GateLink) return false;
  for (const StoredParam& p : e.gl.params) {
    if (p.used) return true;
  }
  return false;
}

size_t Node::App::phy_slot(const lran::node::Context& c) const {
  return node_->slot_of(as_identity(c));
}

// BF-36 - the simulated MPPT answers at once, or stays silent, which the engine turns into
// TIMEOUT after hex_timeout_ms.
lran::node::HexReply Node::App::hex_forward(lran::node::Context& c, const char* req, size_t n,
                                            char* rsp, size_t cap, size_t* rsp_n, uint32_t) {
  Identity& e = as_identity(c);
  if (e.gl.hex_timeout_left > 0) {
    --e.gl.hex_timeout_left;
    sink_printf(node_->log_, "hex %02x: timeout fault, MPPT does not answer, %u left", e.id,
                static_cast<unsigned>(e.gl.hex_timeout_left));
    return lran::node::HexReply::Pending;
  }
  return e.gl.mppt.answer(req, n, rsp, cap, rsp_n) == MpptReply::Silent
             ? lran::node::HexReply::Pending
             : lran::node::HexReply::Answered;
}

uint32_t Node::App::hex_timeout_ms(const lran::node::Context& c) const {
  return as_identity(c).gl.hex_timeout_ms;
}

bool Node::App::withhold(lran::node::Context& c, const char* what, const lran::Header& hdr) {
  return node_->silenced(as_identity(c), what, hdr);
}

// The ctx_reject fault (BF-21). The engine asks BEFORE the gate, so a rejected command
// consumes no seq and caches nothing, and the bridge's resync retry meets a second
// rejection rather than a dedup hit - the path this fault exists to reach (spec 10.3 step 3).
bool Node::App::force_reject_ctx(lran::node::Context& c, const lran::Header& hdr) {
  Identity& e = as_identity(c);
  if (e.gl.ctx_reject_left == 0) return false;
  --e.gl.ctx_reject_left;
  ++e.gl.ctx_rejects_forced;
  sink_printf(node_->log_,
              "fault %02x ctx_reject: seq %u answered REJECTED_CTX (own ctx 0x%08lx), %u left",
              e.id, static_cast<unsigned>(hdr.seq), static_cast<unsigned long>(e.ctx_id),
              static_cast<unsigned>(e.gl.ctx_reject_left));
  return true;
}

// ack_suppress and ack_dup act on a fresh result and nowhere else: a retry answered from
// the cache is the path they exist to exercise.
lran::node::AckDelivery Node::App::fresh_ack(lran::node::Context& c, lran::Seq seq) {
  GateLinkState& gl = as_identity(c).gl;
  if (gl.ack_suppress_left > 0) {
    --gl.ack_suppress_left;
    ++gl.acks_suppressed;
    sink_printf(node_->log_, "fault %02x ack_suppress: ACK for seq %u withheld, %u left", c.id,
                static_cast<unsigned>(seq), static_cast<unsigned>(gl.ack_suppress_left));
    return lran::node::AckDelivery::Suppress;
  }
  if (gl.ack_dup_left > 0) {
    --gl.ack_dup_left;
    sink_printf(node_->log_, "fault %02x ack_dup: ACK for seq %u sent twice, %u left", c.id,
                static_cast<unsigned>(seq), static_cast<unsigned>(gl.ack_dup_left));
    return lran::node::AckDelivery::Twice;
  }
  return lran::node::AckDelivery::Send;
}

// ---------------------------------------------------------------------------
// Node - ROLE_GATELINK's emitters and board-wide state
// ---------------------------------------------------------------------------

void Node::answer_poll_gatelink(Identity& e, const lran::Header& hdr, const uint8_t* payload,
                                size_t len, uint32_t now_ms) {
  if (silenced(e, "poll", hdr)) return;
  lran::msg::Poll p;
  const uint8_t flags =
      lran::msg::deserialize(payload, len, &p) == lran::Status::Ok ? p.poll_flags : 0;
  engine_.send_status(e, app_, hdr.src, lran::StatusReason::PollResponse, now_ms);
  // spec 6.4 bit 1 - config readback, the recovery path for a lost CONFIG_ACK (spec 7.4).
  if ((flags & lran::kPollFlagConfigReadback) != 0) engine_.send_config_readback(e, app_, hdr.src);
}

size_t Node::slot_of(const Identity& e) const {
  for (size_t i = 0; i < kMaxIdentities; ++i) {
    if (&ids_->slot(i) == &e) return i;
  }
  return kMaxIdentities;
}

uint8_t Node::phy_members() const {
  uint8_t m = 0;
  for (size_t i = 0; i < kMaxIdentities; ++i) {
    const Identity& e = ids_->slot(i);
    if (e.used && e.enabled && e.role != Role::Fault) m = static_cast<uint8_t>(m | (1u << i));
  }
  return m;
}

void Node::on_phy_revert(RevertCause cause) {
  sink_printf(log_, "phy: REVERTED (%s) to the committed group",
              cause == RevertCause::Reboot ? "reboot during trial" : "window expired");
  for (size_t i = 0; i < kMaxIdentities; ++i) {
    Identity& e = ids_->slot(i);
    if (e.used && e.role == Role::GateLink) lran::node::Engine::note_phy_revert(e, cause);
  }
}

bool Node::send_event(Identity& e, const lran::schema::GateLinkEventV1& ev) {
  uint8_t payload[lran::schema::kGateLinkEventV1Len];
  size_t  n = 0;
  if (lran::schema::serialize(ev, payload, sizeof(payload), &n) != lran::Status::Ok) return false;
  return engine_.send_event(e, lran::kNodeBridge, payload, n, lran::kSchemaGateLinkEventV1);
}

const char* emit_result_name(EmitResult r) {
  switch (r) {
    case EmitResult::Ok:              return "ok";
    case EmitResult::NoIdentity:      return "no such identity";
    case EmitResult::Disabled:        return "identity disabled";
    case EmitResult::WrongRole:       return "needs ROLE_GATELINK";
    case EmitResult::NothingToRepeat: return "no earlier event to repeat";
    case EmitResult::OutboxFull:      return "outbox full";
    case EmitResult::EncodeFailed:    return "encode failed";
  }
  return "?";
}

bool Node::announce_boot(Identity& e, lran::ResetCause cause, uint32_t now_ms) {
  const bool ok = engine_.announce_boot(e, app_, cause, now_ms);  // spec 10.7
  sink_printf(log_, "id %02x boot: ctx 0x%08lx, reset cause %s, boot_count %u%s", e.id,
              static_cast<unsigned long>(e.ctx_id), reset_cause_name(cause),
              static_cast<unsigned>(e.gl.status.boot_count), ok ? "" : " - NOT ALL QUEUED");
  return ok;
}

void Node::on_boot(lran::ResetCause cause, uint16_t boot_count, uint32_t now_ms) {
  boot_count_ = boot_count;
  for (size_t i = 0; i < kMaxIdentities; ++i) {
    Identity& e = ids_->slot(i);
    if (!e.used || !e.enabled || e.role != Role::GateLink) continue;
    e.gl.status.boot_count = boot_count;
    (void)announce_boot(e, cause, now_ms);
  }
}

EmitResult Node::reboot_identity(lran::NodeId id, lran::ResetCause cause, uint32_t now_ms) {
  Identity* e = ids_->find(id);
  if (e == nullptr) return EmitResult::NoIdentity;
  if (!e->enabled) return EmitResult::Disabled;
  if (e->role != Role::GateLink) return EmitResult::WrongRole;
  if (out_->free_slots() < 2) return EmitResult::OutboxFull;
  ids_->new_context(id);
  return announce_boot(*e, cause, now_ms) ? EmitResult::Ok : EmitResult::EncodeFailed;
}

EmitResult Node::push(lran::NodeId id, lran::StatusReason reason, uint32_t now_ms) {
  Identity* e = ids_->find(id);
  if (e == nullptr) return EmitResult::NoIdentity;
  if (!e->enabled) return EmitResult::Disabled;
  if (e->role != Role::GateLink) return EmitResult::WrongRole;
  if (out_->free_slots() < 1) return EmitResult::OutboxFull;
  return engine_.send_status(*e, app_, lran::kNodeBridge, reason, now_ms)
             ? EmitResult::Ok
             : EmitResult::EncodeFailed;
}

EmitResult Node::event(lran::NodeId id, lran::EventType type, EventMode mode, uint32_t now_ms,
                       uint32_t* event_id) {
  Identity* e = ids_->find(id);
  if (e == nullptr) return EmitResult::NoIdentity;
  if (!e->enabled) return EmitResult::Disabled;
  if (e->role != Role::GateLink) return EmitResult::WrongRole;
  if (mode != EventMode::New && !e->gl.has_last_event) return EmitResult::NothingToRepeat;
  if (out_->free_slots() < 1) return EmitResult::OutboxFull;

  lran::schema::GateLinkEventV1 ev;
  switch (mode) {
    case EventMode::New:
      ev = make_event(*e, type, now_ms);
      break;
    case EventMode::Again:
      ev = e->gl.last_event;  // byte-identical payload: the (ctx_id, event_id) dedup case
      break;
    case EventMode::FollowUp:
      // spec 7.3 - same event_id, follow-up bit set, direction now classified.
      ev = e->gl.last_event;
      ev.event_flags |= lran::schema::kEventFlagFollowUp;
      ev.direction = static_cast<uint8_t>(lran::Direction::Entry);
      break;
  }
  e->gl.last_event     = ev;
  e->gl.has_last_event = true;
  if (event_id != nullptr) *event_id = ev.event_id;
  return send_event(*e, ev) ? EmitResult::Ok : EmitResult::EncodeFailed;
}

}  // namespace simnode
