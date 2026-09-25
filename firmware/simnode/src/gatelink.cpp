// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// ROLE_GATELINK - schema 0xFE status, 0x11 events, COMMAND and COMMAND_ACK, 0x12 config.
// Task BF-6; Impl Plan 10.2; spec 6.2-6.4, 7.2-7.4, 9.4, 10.1-10.4.
//
// THE COMMAND PATH follows D34 as amended: CommandGate::check() before dispatch, record()
// before the COMMAND_ACK, and nothing sent for a retry found in flight (spec 9.4, v0.11).
//
// THE SYNTHETIC MARKER IS THE SCHEMA. Every status this role sends is schema 0xFE, never
// 0x10 (spec 7.1, bench only), so a status_reason can be a real one and the bridge's
// handling of it can be tested (decided with the operator 2026-09-14).
//
// A DUPLICATE_CACHED ACK carries the cached result in `detail`. Spec 9.4 step 4 says
// "COMMAND_ACK(DUPLICATE_CACHED) with the cached result" and spec 6.3 has one result byte, so
// the cached result can only ride in `detail`, and the cached detail is lost. Raised for
// spec v0.12 rather than settled here.

#include "gatelink.h"

#include <climits>
#include <cstdlib>
#include <cstring>

#include "lran/codec.h"
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
const lran::schema::NodeConfigV1    kEmptyConfig{};
const lran::schema::NodeConfigAckV1 kEmptyConfigAck{};

struct ReasonName {
  const char*        name;
  lran::StatusReason value;
};
constexpr ReasonName kReasons[] = {
    {"POLL_RESPONSE", lran::StatusReason::PollResponse},
    {"GATE_STATE_CHANGE", lran::StatusReason::GateStateChange},
    {"HOLD_STATE_CHANGE", lran::StatusReason::HoldStateChange},
    {"MPPT_ERROR", lran::StatusReason::MpptError},
    {"VEHICLE_DETECTED", lran::StatusReason::VehicleDetected},
    {"BMS_ALARM", lran::StatusReason::BmsAlarm},
    {"HARD_SHUTDOWN", lran::StatusReason::HardShutdown},
    {"FIRE", lran::StatusReason::Fire},
    {"CONFIG_CHANGE", lran::StatusReason::ConfigChange},
    {"BOOT", lran::StatusReason::Boot},
    {"CHARGE_INHIBITED", lran::StatusReason::ChargeInhibited},
    {"DEBUG_SYNTHETIC", lran::StatusReason::DebugSynthetic},
};

struct EventName {
  const char*     name;
  lran::EventType value;
};
constexpr EventName kEvents[] = {
    {"VEHICLE_WHILE_HELD_OPEN", lran::EventType::VehicleWhileHeldOpen},
    {"FIRE_ASSERTED", lran::EventType::FireAsserted},
    {"HARD_SHUTDOWN", lran::EventType::HardShutdown},
    {"VEHICLE_DETECTED", lran::EventType::VehicleDetected},
    {"GATE_STATE_CHANGE", lran::EventType::GateStateChange},
    {"HOLD_STATE_CHANGE", lran::EventType::HoldStateChange},
    {"BMS_ALARM", lran::EventType::BmsAlarm},
    {"MPPT_ERROR", lran::EventType::MpptError},
    {"CHARGE_INHIBITED", lran::EventType::ChargeInhibited},
    {"BOOT", lran::EventType::Boot},
    {"PHY_REVERTED", lran::EventType::PhyReverted},
};

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
// Tokens
// ---------------------------------------------------------------------------

const char* status_reason_name(lran::StatusReason r) {
  for (const ReasonName& n : kReasons) {
    if (n.value == r) return n.name;
  }
  return "?";
}

bool parse_status_reason(const char* token, lran::StatusReason* out) {
  for (const ReasonName& n : kReasons) {
    if (std::strcmp(token, n.name) == 0) {
      *out = n.value;
      return true;
    }
  }
  return false;
}

const char* event_type_name(lran::EventType t) {
  for (const EventName& n : kEvents) {
    if (n.value == t) return n.name;
  }
  return "?";
}

bool parse_event_type(const char* token, lran::EventType* out) {
  for (const EventName& n : kEvents) {
    if (std::strcmp(token, n.name) == 0) {
      *out = n.value;
      return true;
    }
  }
  return false;
}

const char* ack_result_name(lran::AckResult r) {
  switch (r) {
    case lran::AckResult::Accepted:             return "ACCEPTED";
    case lran::AckResult::RejectedMac:          return "REJECTED_MAC";
    case lran::AckResult::RejectedSeq:          return "REJECTED_SEQ";
    case lran::AckResult::RejectedCtx:          return "REJECTED_CTX";
    case lran::AckResult::RejectedUnknownCmd:   return "REJECTED_UNKNOWN_CMD";
    case lran::AckResult::RejectedArg:          return "REJECTED_ARG";
    case lran::AckResult::RejectedNotSupported: return "REJECTED_NOT_SUPPORTED";
    case lran::AckResult::DuplicateCached:      return "DUPLICATE_CACHED";
    case lran::AckResult::DryRun:               return "DRY_RUN";
    case lran::AckResult::ActuatorBusy:         return "ACTUATOR_BUSY";
    case lran::AckResult::RejectedUnsafe:       return "REJECTED_UNSAFE";
  }
  return "?";
}

const char* cmd_name(uint8_t cmd) {
  switch (static_cast<lran::Cmd>(cmd)) {
    case lran::Cmd::Nop:            return "NOP";
    case lran::Cmd::Open:           return "OPEN";
    case lran::Cmd::Close:          return "CLOSE";
    case lran::Cmd::HoldOpen:       return "HOLD_OPEN";
    case lran::Cmd::ReleaseHold:    return "RELEASE_HOLD";
    case lran::Cmd::RequestStatus:  return "REQUEST_STATUS";
    case lran::Cmd::RequestConfig:  return "REQUEST_CONFIG";
    case lran::Cmd::RollContext:    return "ROLL_CONTEXT";
    case lran::Cmd::SetDebugMode:   return "SET_DEBUG_MODE";
    case lran::Cmd::SetRelayDryRun: return "SET_RELAY_DRY_RUN";
    case lran::Cmd::SetBmsPolling:  return "SET_BMS_POLLING";
    case lran::Cmd::Reboot:         return "REBOOT";
  }
  return "?";
}

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
// Node - ROLE_GATELINK's receive and send paths
// ---------------------------------------------------------------------------

void Node::refuse_authenticated(Identity& e, const lran::Header& hdr, lran::Status why) {
  // spec 9.4 steps 2-3 - answered with COMMAND_ACK carrying this node's own ctx_id (spec
  // 10.3), for both authenticated types. Only a frame addressed to this identity.
  if (hdr.dst != e.id) return;
  if (hdr.type != lran::MsgType::Command && hdr.type != lran::MsgType::Config) return;
  const lran::AckResult r =
      why == lran::Status::RejectedCtx ? lran::AckResult::RejectedCtx : lran::AckResult::RejectedMac;
  sink_printf(log_, "cmd %02x <- %02x seq %u: %s (frame ctx 0x%08lx, own 0x%08lx)", e.id, hdr.src,
              static_cast<unsigned>(hdr.seq), ack_result_name(r),
              static_cast<unsigned long>(hdr.ctx_id), static_cast<unsigned long>(e.ctx_id));
  send_ack(e, hdr.src, hdr.seq, r, 0);
}

bool Node::send_ack(Identity& e, lran::NodeId peer, lran::Seq ack_seq, lran::AckResult result,
                    uint8_t detail) {
  const lran::msg::CommandAck ack{ack_seq, static_cast<uint8_t>(result), detail};
  uint8_t                     payload[lran::msg::kCommandAckLen];
  size_t                      n = 0;
  if (lran::msg::serialize(ack, payload, sizeof(payload), &n) != lran::Status::Ok) return false;

  lran::Header h;
  h.ver    = e.proto_ver;
  h.type   = lran::MsgType::CommandAck;
  h.src    = e.id;
  h.dst    = peer;
  h.seq    = e.tx_seq++;
  h.ctx_id = e.ctx_id;
  h.schema = lran::kSchemaNone;
  if (!send(e, h, payload, n, 0)) {
    ++answers_dropped_;
    sink_printf(log_, "cmd %02x seq %u: COMMAND_ACK not queued", e.id, static_cast<unsigned>(ack_seq));
    return false;
  }
  return true;
}

// The ACK for a result this identity just produced. ack_suppress and ack_dup act here and
// nowhere else: a retry answered from the cache is the path they exist to exercise.
void Node::send_fresh_ack(Identity& e, lran::NodeId peer, lran::Seq seq, lran::AckResult result,
                          uint8_t detail) {
  GateLinkState& gl = e.gl;
  if (gl.ack_suppress_left > 0) {
    --gl.ack_suppress_left;
    ++gl.acks_suppressed;
    sink_printf(log_, "fault %02x ack_suppress: ACK for seq %u withheld, %u left", e.id,
                static_cast<unsigned>(seq), static_cast<unsigned>(gl.ack_suppress_left));
    return;
  }
  send_ack(e, peer, seq, result, detail);
  if (gl.ack_dup_left > 0) {
    --gl.ack_dup_left;
    sink_printf(log_, "fault %02x ack_dup: ACK for seq %u sent twice, %u left", e.id,
                static_cast<unsigned>(seq), static_cast<unsigned>(gl.ack_dup_left));
    send_ack(e, peer, seq, result, detail);
  }
}

bool Node::send_status(Identity& e, lran::NodeId dst, lran::StatusReason reason, uint32_t now_ms) {
  uint8_t      payload[lran::schema::kGateLinkStatusV1Len];
  const size_t n = build_gatelink_status(e, reason, now_ms, payload, sizeof(payload));
  lran::Header h;
  h.ver    = e.proto_ver;
  h.type   = lran::MsgType::Status;
  h.src    = e.id;
  h.dst    = dst;
  h.seq    = e.tx_seq++;
  h.ctx_id = e.ctx_id;
  h.schema = lran::kSchemaSimnodeStatusV1;
  if (n == 0 || !send(e, h, payload, n, 0)) {
    ++answers_dropped_;
    sink_printf(log_, "status %02x: 0xFE %s not queued", e.id, status_reason_name(reason));
    return false;
  }
  return true;
}

bool Node::send_event(Identity& e, lran::NodeId dst, const lran::schema::GateLinkEventV1& ev) {
  uint8_t payload[lran::schema::kGateLinkEventV1Len];
  size_t  n = 0;
  if (lran::schema::serialize(ev, payload, sizeof(payload), &n) != lran::Status::Ok) return false;
  lran::Header h;
  h.ver    = e.proto_ver;
  h.type   = lran::MsgType::Event;
  h.src    = e.id;
  h.dst    = dst;
  h.seq    = e.tx_seq++;
  h.ctx_id = e.ctx_id;
  h.schema = lran::kSchemaGateLinkEventV1;
  return send(e, h, payload, n, 0);
}

bool Node::send_config_ack(Identity& e, lran::NodeId dst,
                           const lran::schema::NodeConfigAckV1& ack, uint32_t reply_seq) {
  uint8_t payload[lran::kMaxSchemaPayload];
  size_t  n = 0;
  if (lran::schema::serialize(ack, payload, sizeof(payload), &n) != lran::Status::Ok) {
    sink_printf(log_, "config %02x: CONFIG_ACK did not serialize", e.id);
    return false;
  }
  lran::Header h;
  h.ver    = e.proto_ver;
  h.type   = lran::MsgType::ConfigAck;
  h.src    = e.id;
  h.dst    = dst;
  // spec 7.4.1 - the request's `seq` when this answers one, the status space when it
  // answers nothing. A solicited answer carrying a status seq cannot be correlated at
  // all: the bridge is waiting on the seq it sent, and an answer under another number
  // reads as an ACK for something else. Found on the bench on 2026-09-21, when the
  // bridge's BF-32 path reported `unknown` for a CONFIG the simnode had already applied
  // and answered.
  h.seq    = reply_seq == kUseStatusSeq ? e.tx_seq++ : static_cast<lran::Seq>(reply_seq);
  h.ctx_id = e.ctx_id;
  h.schema = lran::kSchemaNodeConfigV1;
  if (!send(e, h, payload, n, 0)) {
    ++answers_dropped_;
    sink_printf(log_, "config %02x: CONFIG_ACK not queued", e.id);
    return false;
  }
  return true;
}

void Node::apply_config(Identity& e, const lran::schema::NodeConfigV1& in,
                        lran::schema::NodeConfigAckV1* out, uint32_t now_ms) {
  namespace sc = lran::schema;
  *out    = kEmptyConfigAck;
  out->op = in.op;

  // Results are added while they fit one CONFIG_ACK (spec 11.4 - single-frame). The RAM
  // store and the PHY group together can outgrow one; spec 7.4.1's split is not built
  // here, so the overflow is logged, never silent.
  size_t used    = sc::kConfigAckHdrLen;
  size_t dropped = 0;
  auto   add     = [&](const sc::ConfigAckEntry& a) {
    const size_t need = sc::kConfigAckEntryHdrLen + a.len;
    if (out->count >= sc::kMaxConfigAckEntries || used + need > lran::kMaxSchemaPayload) {
      ++dropped;
      return;
    }
    out->entries[out->count++] = a;
    used += need;
  };

  // BF-33 - every role but ROLE_FAULT holds the board's PHY group; only ROLE_GATELINK
  // holds the generic RAM store as well. A row outside both is UNKNOWN_PARAM (spec 7.4).
  const bool generic = e.role == Role::GateLink;
  auto unknown = [](uint16_t id, lran::PType t) {
    sc::ConfigAckEntry a;
    a.param_id = id;
    a.status   = lran::ParamStatus::UnknownParam;
    a.ptype    = t;
    a.len      = 0;
    return a;
  };

  // spec 7.4, D53 - persist_status after a write says what was applied; after a read,
  // whether the current overrides are persisted. The generic store is RAM, so its
  // overrides never are; the PHY group reports its own, which a trial makes
  // APPLIED_NOT_PERSISTED (D60).
  auto current = [&]() {
    if (generic) {
      for (const StoredParam& p : e.gl.params) {
        if (p.used) return lran::PersistStatus::AppliedNotPersisted;
      }
    }
    return phy_->read_persist_status();
  };
  auto list_all = [&]() {
    if (generic) {
      for (const StoredParam& p : e.gl.params) {
        if (p.used) add(result_of(p, lran::ParamStatus::Ok));
      }
    }
    for (const lran::config::ParamDef& d : lran::config::kNodeCommonParams) {
      if (PhyTrial::is_phy(d.id)) add(phy_->get(d.id));
    }
  };

  switch (in.op) {
    case lran::ConfigOp::Set: {
      bool applied   = false;
      bool phy_named = false;
      bool phy_ok    = true;
      for (uint8_t i = 0; i < in.count; ++i) {
        const sc::ConfigEntry& entry = in.entries[i];
        sc::ConfigAckEntry     a;
        if (PhyTrial::is_phy(entry.param_id)) {
          bool took = false;
          a         = phy_->set(entry, &took);
          phy_named = true;
          // spec 12.4.1 step 4 - the bridge abandons on a refusal or a clamp, so only an
          // OK entry counts toward the board's retune.
          phy_ok    = phy_ok && a.status == lran::ParamStatus::Ok;
        } else if (generic) {
          a = set_param(e.gl, entry);
          if (a.status == lran::ParamStatus::UnknownParam) {
            sink_printf(log_, "config %02x: param 0x%04x refused - RAM store full (%u)", e.id,
                        static_cast<unsigned>(a.param_id),
                        static_cast<unsigned>(kConfigStoreDepth));
          }
        } else {
          a = unknown(entry.param_id, entry.ptype);
        }
        applied = applied || a.status == lran::ParamStatus::Ok ||
                  a.status == lran::ParamStatus::Clamped;
        add(a);
      }
      if (phy_named) {
        phy_->on_set(slot_of(e), phy_ok, phy_members(), now_ms);
        sink_printf(log_, "phy %02x: SET %s, board %s, accepted 0x%02x of 0x%02x", e.id,
                    phy_ok ? "accepted" : "NOT accepted", phy_state_name(phy_->state()),
                    static_cast<unsigned>(phy_->accepted_mask()),
                    static_cast<unsigned>(phy_members()));
      }
      // D53 - NOT_APPLIED only when nothing in the set took effect.
      out->persist_status =
          applied ? lran::PersistStatus::AppliedNotPersisted : lran::PersistStatus::NotApplied;
      break;
    }
    case lran::ConfigOp::Get:
      for (uint8_t i = 0; i < in.count; ++i) {
        const uint16_t id = in.entries[i].param_id;
        if (PhyTrial::is_phy(id)) {
          add(phy_->get(id));
          continue;
        }
        const StoredParam* p = generic ? find_param(e.gl, id) : nullptr;
        add(p != nullptr ? result_of(*p, lran::ParamStatus::Ok)
                         : unknown(id, in.entries[i].ptype));
      }
      out->persist_status = current();
      break;
    case lran::ConfigOp::GetAll:
      list_all();
      out->persist_status = current();
      break;
    case lran::ConfigOp::RestoreDefaults:
      // The generic store has no defaults; restoring them empties it. The PHY group is
      // kept, as lran-config's Store::restore_defaults() keeps it (D60). D52 - answered
      // with the full effective configuration, as GET_ALL is.
      if (generic) {
        for (StoredParam& p : e.gl.params) p = StoredParam{};
      }
      list_all();
      out->persist_status = current();
      break;
    default:
      out->persist_status = lran::PersistStatus::NotApplied;
      sink_printf(log_, "config %02x: unknown op 0x%02x, not applied", e.id,
                  static_cast<unsigned>(in.op));
      break;
  }
  if (dropped > 0) {
    sink_printf(log_, "config %02x: CONFIG_ACK holds %u result(s); %u more did not fit %u B "
                      "(spec 3.1) - resolve by readback",
                e.id, static_cast<unsigned>(out->count), static_cast<unsigned>(dropped),
                static_cast<unsigned>(lran::kMaxSchemaPayload));
  }
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
    if (e.used && e.role == Role::GateLink) e.gl.phy_revert_detail = static_cast<uint16_t>(cause);
  }
}

void Node::send_phy_reverted(Identity& e, lran::NodeId dst, uint32_t now_ms) {
  lran::schema::GateLinkEventV1 ev = make_event(e, lran::EventType::PhyReverted, now_ms);
  ev.detail                        = e.gl.phy_revert_detail;
  e.gl.phy_revert_detail           = 0;
  e.gl.last_event                  = ev;
  e.gl.has_last_event              = true;
  if (!send_event(e, dst, ev)) {
    ++answers_dropped_;
    sink_printf(log_, "phy %02x: PHY_REVERTED not queued", e.id);
    return;
  }
  sink_printf(log_, "phy %02x: EVENT PHY_REVERTED detail 0x%04x, event_id %lu", e.id,
              static_cast<unsigned>(ev.detail), static_cast<unsigned long>(ev.event_id));
}

bool Node::send_config_readback(Identity& e, lran::NodeId dst) {
  cfg_rx_    = kEmptyConfig;
  cfg_rx_.op = lran::ConfigOp::GetAll;
  apply_config(e, cfg_rx_, &cfg_ack_, 0);
  // Unsolicited: it answers a POLL bit 1 or a REQUEST_CONFIG and correlates to no
  // request at all (D45).
  return send_config_ack(e, dst, cfg_ack_, kUseStatusSeq);
}

void Node::answer_poll_gatelink(Identity& e, const lran::Header& hdr, const uint8_t* payload,
                                size_t len, uint32_t now_ms) {
  if (silenced(e, "poll", hdr)) return;
  lran::msg::Poll p;
  const uint8_t flags =
      lran::msg::deserialize(payload, len, &p) == lran::Status::Ok ? p.poll_flags : 0;
  send_status(e, hdr.src, lran::StatusReason::PollResponse, now_ms);
  // spec 6.4 bit 1 - config readback, the recovery path for a lost CONFIG_ACK (spec 7.4).
  if ((flags & lran::kPollFlagConfigReadback) != 0) send_config_readback(e, hdr.src);
}

lran::AckResult Node::execute(Identity& e, const lran::msg::Command& c, AfterAck* after) {
  GateLinkState& gl = e.gl;
  *after            = AfterAck::None;
  ++gl.executions;

  // A simnode has no relay. An actuation command is dispatched to nothing, and counted, so
  // cmd_replay can assert a replay did not dispatch a second time.
  switch (static_cast<lran::Cmd>(c.cmd)) {
    case lran::Cmd::Nop:
      return lran::AckResult::Accepted;
    case lran::Cmd::Close:
      if (c.arg > 1) return lran::AckResult::RejectedArg;  // spec 8.1 - 0 or 1
      ++gl.actuations;
      return gl.dry_run ? lran::AckResult::DryRun : lran::AckResult::Accepted;
    case lran::Cmd::Open:
    case lran::Cmd::HoldOpen:
    case lran::Cmd::ReleaseHold:
      ++gl.actuations;
      return gl.dry_run ? lran::AckResult::DryRun : lran::AckResult::Accepted;
    case lran::Cmd::RequestStatus:
      *after = AfterAck::Status;
      return lran::AckResult::Accepted;
    case lran::Cmd::RequestConfig:
      *after = AfterAck::ConfigReadback;
      return lran::AckResult::Accepted;
    case lran::Cmd::SetDebugMode:
      gl.debug_modes = c.arg2;
      return lran::AckResult::Accepted;
    case lran::Cmd::SetRelayDryRun:
      if (c.arg > 1) return lran::AckResult::RejectedArg;
      gl.dry_run = c.arg == 1;
      return lran::AckResult::Accepted;
    case lran::Cmd::SetBmsPolling:
      if (c.arg > 1) return lran::AckResult::RejectedArg;
      gl.bms_polling = c.arg == 1;
      return lran::AckResult::Accepted;
    case lran::Cmd::RollContext:
      // spec 9.4 - a roll skips steps 4-6, so on_command() answers it before the gate and
      // it never reaches here. Refused rather than executed if that ever changes.
      return lran::AckResult::RejectedUnknownCmd;
    case lran::Cmd::Reboot:
      if (c.arg != lran::kRebootGuard) return lran::AckResult::RejectedArg;
      *after = AfterAck::Reboot;
      return lran::AckResult::Accepted;
  }
  return lran::AckResult::RejectedUnknownCmd;
}

void Node::finish_command(Identity& e, const PendingAck& p, uint32_t now_ms) {
  if (!e.gate.record(p.seq, p.result, p.detail)) {
    // Evicted while in flight (command_gate.h). A retry will read REJECTED_SEQ for a command
    // that ran, so say so here.
    sink_printf(log_, "cmd %02x seq %u: record() refused - evicted in flight", e.id,
                static_cast<unsigned>(p.seq));
  }
  sink_printf(log_, "cmd %02x <- %02x seq %u: %s %s", e.id, p.peer, static_cast<unsigned>(p.seq),
              cmd_name(p.cmd), ack_result_name(p.result));
  send_fresh_ack(e, p.peer, p.seq, p.result, p.detail);

  switch (p.after) {
    case AfterAck::None:
      break;
    case AfterAck::Status:
      send_status(e, p.peer, lran::StatusReason::PollResponse, now_ms);
      break;
    case AfterAck::ConfigReadback:
      send_config_readback(e, p.peer);
      break;
    case AfterAck::Reboot:
      // spec 10.1 - a reboot is a new context. The ACK above went out under the old one.
      ids_->new_context(e.id);
      sink_printf(log_, "id %02x rebooted: ctx 0x%08lx", e.id, static_cast<unsigned long>(e.ctx_id));
      send_status(e, p.peer, lran::StatusReason::Boot, now_ms);
      break;
  }
}

void Node::on_command(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
                      uint32_t now_ms) {
  if (silenced(e, "command", hdr)) return;
  lran::msg::Command c;
  if (lran::msg::deserialize(payload, len, &c) != lran::Status::Ok) {
    // The codec checked the 4-byte length at stage 8, so this cannot happen; logged anyway.
    ++e.unhandled;
    sink_printf(log_, "cmd %02x <- %02x seq %u: payload did not deserialize", e.id, hdr.src,
                static_cast<unsigned>(hdr.seq));
    return;
  }

  // The ctx_reject fault (BF-21), and it acts BEFORE the gate. Spec 9.4 puts the context
  // check at step 2 and the dedup gate at steps 4-6, so a node that refuses on context has
  // not looked at the sequence space: the seq is not consumed, nothing is cached, and the
  // command is never executed. Answering after the gate would cache a result the node never
  // produced, and the bridge's resync retry would then meet a dedup hit instead of a second
  // rejection - which is the very path this fault exists to reach (spec 10.3 step 3).
  if (e.gl.ctx_reject_left > 0) {
    --e.gl.ctx_reject_left;
    ++e.gl.ctx_rejects_forced;
    sink_printf(log_,
                "fault %02x ctx_reject: seq %u answered REJECTED_CTX (own ctx 0x%08lx), %u left",
                e.id, static_cast<unsigned>(hdr.seq), static_cast<unsigned long>(e.ctx_id),
                static_cast<unsigned>(e.gl.ctx_reject_left));
    send_ack(e, hdr.src, hdr.seq, lran::AckResult::RejectedCtx, 0);
    return;
  }

  // spec 9.4, 10.6 - a roll skips steps 4-6. The bridge sends it because its own seq
  // cannot be trusted after a restart, so the gate must not judge that seq.
  if (c.cmd == static_cast<uint8_t>(lran::Cmd::RollContext)) {
    on_roll(e, hdr, c);
    return;
  }

  const lran::GateResult g = e.gate.check(hdr.seq);  // spec 9.4 steps 4-6, D34
  switch (g.verdict) {
    case lran::Verdict::Execute: {
      phy_->on_authenticated();  // spec 12.4.2 step 5, as in on_config()
      if (e.gl.pending.active) {
        // One execution at a time, as one relay board. The seq is consumed either way.
        e.gate.record(hdr.seq, lran::AckResult::ActuatorBusy, 0);
        sink_printf(log_, "cmd %02x <- %02x seq %u: %s ACTUATOR_BUSY (seq %u executing)", e.id,
                    hdr.src, static_cast<unsigned>(hdr.seq), cmd_name(c.cmd),
                    static_cast<unsigned>(e.gl.pending.seq));
        send_fresh_ack(e, hdr.src, hdr.seq, lran::AckResult::ActuatorBusy, 0);
        return;
      }
      PendingAck p;
      p.active   = true;
      p.peer     = hdr.src;
      p.seq      = hdr.seq;
      p.cmd      = c.cmd;
      p.start_ms = now_ms;
      p.delay_ms = e.gl.ack_delay_ms;
      p.result   = execute(e, c, &p.after);
      if (p.delay_ms == 0) {
        finish_command(e, p, now_ms);
      } else {
        e.gl.pending = p;
        sink_printf(log_, "cmd %02x <- %02x seq %u: %s executing, ACK in %lu ms", e.id, hdr.src,
                    static_cast<unsigned>(hdr.seq), cmd_name(c.cmd),
                    static_cast<unsigned long>(p.delay_ms));
      }
      return;
    }
    case lran::Verdict::ReturnCached:
      sink_printf(log_, "cmd %02x <- %02x seq %u: dedup hit, DUPLICATE_CACHED (%s), not executed",
                  e.id, hdr.src, static_cast<unsigned>(hdr.seq), ack_result_name(g.cached_result));
      send_ack(e, hdr.src, hdr.seq, lran::AckResult::DuplicateCached,
               static_cast<uint8_t>(g.cached_result));
      return;
    case lran::Verdict::InFlight:
      sink_printf(log_, "cmd %02x <- %02x seq %u: retry in flight, not answered (spec 9.4)", e.id,
                  hdr.src, static_cast<unsigned>(hdr.seq));
      return;
    case lran::Verdict::Reject:
      sink_printf(log_, "cmd %02x <- %02x seq %u: REJECTED_SEQ, high water %u", e.id, hdr.src,
                  static_cast<unsigned>(hdr.seq), static_cast<unsigned>(e.gate.high_water()));
      send_ack(e, hdr.src, hdr.seq, lran::AckResult::RejectedSeq, 0);
      return;
  }
}

void Node::on_config(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
                     uint32_t now_ms) {
  if (silenced(e, "config", hdr)) return;

  // spec 9.4 applies steps 4-6 to every authenticated type, so a CONFIG shares the command
  // seq space and the gate. The step 4 and 5 answers are COMMAND_ACK, as 9.4 words them.
  const lran::GateResult g = e.gate.check(hdr.seq);
  switch (g.verdict) {
    case lran::Verdict::Execute:
      // spec 12.4.2 step 5 - through stage 11, so this CONFIG confirms a PHY trial. First,
      // so a confirming GET answers with the group committed.
      phy_->on_authenticated();
      break;
    case lran::Verdict::ReturnCached:
      sink_printf(log_, "config %02x <- %02x seq %u: dedup hit, DUPLICATE_CACHED, not applied",
                  e.id, hdr.src, static_cast<unsigned>(hdr.seq));
      send_ack(e, hdr.src, hdr.seq, lran::AckResult::DuplicateCached,
               static_cast<uint8_t>(g.cached_result));
      return;
    case lran::Verdict::InFlight:
      sink_printf(log_, "config %02x <- %02x seq %u: in flight, not answered", e.id, hdr.src,
                  static_cast<unsigned>(hdr.seq));
      return;
    case lran::Verdict::Reject:
      send_ack(e, hdr.src, hdr.seq, lran::AckResult::RejectedSeq, 0);
      return;
  }

  if (lran::schema::deserialize(payload, len, &cfg_rx_) != lran::Status::Ok) {
    e.gate.record(hdr.seq, lran::AckResult::RejectedArg, 0);
    cfg_ack_                = kEmptyConfigAck;
    cfg_ack_.op             = static_cast<lran::ConfigOp>(len > 0 ? payload[0] : 0);
    cfg_ack_.persist_status = lran::PersistStatus::NotApplied;
    sink_printf(log_, "config %02x <- %02x seq %u: body did not parse, NOT_APPLIED", e.id, hdr.src,
                static_cast<unsigned>(hdr.seq));
    send_config_ack(e, hdr.src, cfg_ack_, hdr.seq);
    return;
  }

  ++e.gl.executions;
  apply_config(e, cfg_rx_, &cfg_ack_, now_ms);
  e.gate.record(hdr.seq, lran::AckResult::Accepted, 0);
  sink_printf(log_, "config %02x <- %02x seq %u: op %u, %u entr%s, %u result(s)", e.id, hdr.src,
              static_cast<unsigned>(hdr.seq), static_cast<unsigned>(cfg_rx_.op),
              static_cast<unsigned>(cfg_rx_.count), cfg_rx_.count == 1 ? "y" : "ies",
              static_cast<unsigned>(cfg_ack_.count));
  send_config_ack(e, hdr.src, cfg_ack_, hdr.seq);
}

void Node::tick_gatelink(Identity& e, uint32_t now_ms) {
  PendingAck& p = e.gl.pending;
  if (!p.active || !e.enabled || now_ms - p.start_ms < p.delay_ms) return;
  const PendingAck done = p;
  p.active              = false;
  finish_command(e, done, now_ms);
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

EmitResult Node::push(lran::NodeId id, lran::StatusReason reason, uint32_t now_ms) {
  Identity* e = ids_->find(id);
  if (e == nullptr) return EmitResult::NoIdentity;
  if (!e->enabled) return EmitResult::Disabled;
  if (e->role != Role::GateLink) return EmitResult::WrongRole;
  if (out_->free_slots() < 1) return EmitResult::OutboxFull;
  return send_status(*e, lran::kNodeBridge, reason, now_ms) ? EmitResult::Ok
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
  return send_event(*e, lran::kNodeBridge, ev) ? EmitResult::Ok : EmitResult::EncodeFailed;
}

}  // namespace simnode
