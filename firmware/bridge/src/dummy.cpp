// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The dummy publish. Task BF-27; Impl Plan 6.6.2. See dummy.h.

#include "dummy.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lran/schema/gatelink_event_v1.h"
#include "net_policy.h"
#include "publish.h"
#include "registry.h"

namespace bridge {
namespace {

using lran::schema::GateLinkStatusV1;

// A settable field of the template, by its spec 7.2 name. `na` sets the field's sentinel
// (root rule 6), so a console can show that a sentinel is published as null rather than as
// a number. `has_na` is false for a field whose spec gives it no sentinel.
struct Field {
  const char* name;
  int64_t     min;
  int64_t     max;
  bool        has_na;
  int64_t     na;
  void (*set)(GateLinkStatusV1&, int64_t);
  int64_t (*get)(const GateLinkStatusV1&);
};

#define LRAN_DUMMY_FIELD(f, lo, hi, has, sentinel)                                    \
  Field {                                                                             \
    #f, lo, hi, has, sentinel,                                                        \
        [](GateLinkStatusV1& s, int64_t v) { s.f = static_cast<decltype(s.f)>(v); },  \
        [](const GateLinkStatusV1& s) { return static_cast<int64_t>(s.f); }           \
  }
constexpr int64_t kU8  = UINT8_MAX;
constexpr int64_t kU16 = UINT16_MAX;
constexpr int64_t kU32 = UINT32_MAX;

// Every scalar field of schema 0x10 but status_reason, which is DEBUG_SYNTHETIC and not the
// console's to change (R-5.2d). The cell arrays follow the table.
const Field kFields[] = {
    LRAN_DUMMY_FIELD(gate_state, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(input_bits, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(hold, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(movement_cause, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(last_direction, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(detect_flags, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(last_traversal_age_s, 0, kU32, true, lran::kU32NotAvailable),
    LRAN_DUMMY_FIELD(batt_mv, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(batt_ma, INT16_MIN, INT16_MAX, false, 0),
    LRAN_DUMMY_FIELD(pv_cv, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(pv_w, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(load_ma, INT16_MIN, INT16_MAX, true, lran::kI16NotAvailable),
    LRAN_DUMMY_FIELD(yield_today, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(yield_yest, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(pmax_today, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(yield_total, 0, kU32, false, 0),
    LRAN_DUMMY_FIELD(charge_state, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(mppt_err, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(mppt_tracker, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(mppt_flags, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(mppt_temp_c10, INT16_MIN, INT16_MAX, true, lran::kI16NotAvailable),
    LRAN_DUMMY_FIELD(bms_soc, 0, kU8, true, lran::kSocNotAvailable),
    LRAN_DUMMY_FIELD(bms_flags, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(pack_mv, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(pack_ma, INT16_MIN, INT16_MAX, false, 0),
    LRAN_DUMMY_FIELD(cell_count, 0, lran::schema::kMaxCells, false, 0),
    LRAN_DUMMY_FIELD(bms_rssi_neg, 0, kU8, false, 0),
    LRAN_DUMMY_FIELD(bms_cycles, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(bms_capacity_dah, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(bms_alarms, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(bms_age_s, 0, kU16, true, lran::kU16NotAvailable),
    LRAN_DUMMY_FIELD(uptime_s, 0, kU32, false, 0),
    LRAN_DUMMY_FIELD(boot_count, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(node_mv, 0, kU16, false, 0),
    LRAN_DUMMY_FIELD(node_ma, INT16_MIN, INT16_MAX, false, 0),
    LRAN_DUMMY_FIELD(enclosure_temp_c10, INT16_MIN, INT16_MAX, true, lran::kI16NotAvailable),
    LRAN_DUMMY_FIELD(node_flags, 0, kU8, false, 0),
};

#undef LRAN_DUMMY_FIELD

// The two cell arrays are `cell_mv[i]` and `cell_temp_c[i]` in the struct, and `cellN_mv` and
// `cellN_temp_c` on the console, as in the battery document (Impl Plan 6.3.1).
const char* const kCellMv[]   = {"cell1_mv", "cell2_mv", "cell3_mv", "cell4_mv"};
const char* const kCellTemp[] = {"cell1_temp_c", "cell2_temp_c", "cell3_temp_c",
                                 "cell4_temp_c"};

bool cell_field(const char* name, size_t* index, bool* temp) {
  for (size_t i = 0; i < lran::schema::kMaxCells; ++i) {
    if (std::strcmp(name, kCellMv[i]) == 0) {
      *index = i;
      *temp  = false;
      return true;
    }
    if (std::strcmp(name, kCellTemp[i]) == 0) {
      *index = i;
      *temp  = true;
      return true;
    }
  }
  return false;
}

const Field* find_field(const char* name) {
  for (const Field& f : kFields) {
    if (std::strcmp(name, f.name) == 0) return &f;
  }
  return nullptr;
}

bool parse_int(const char* s, int64_t* out) {
  if (s == nullptr || *s == '\0') return false;
  char*           end = nullptr;
  const long long v   = std::strtoll(s, &end, 0);  // base 0: `0x1F` for a flag byte
  if (end == s || *end != '\0') return false;
  *out = static_cast<int64_t>(v);
  return true;
}

// The node a console names by its spec 16.1 token. Only a registered node: a frame from any
// other address would have been refused at spec 14 stage 9a.
bool find_node(const char* token, lran::NodeId* out) {
  for (const NodeProvision& p : kNodeTable) {
    char name[16];
    if (node_topic_name(p.id, name, sizeof(name)) != 0 && std::strcmp(name, token) == 0) {
      *out = p.id;
      return true;
    }
  }
  return false;
}

// Splits `line` in place on spaces. Returns the word count, at most `max`.
size_t split(char* line, char* words[], size_t max) {
  size_t n = 0;
  char*  p = line;
  while (*p != '\0' && n < max) {
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0') break;
    words[n++] = p;
    while (*p != '\0' && *p != ' ' && *p != '\t') ++p;
    if (*p != '\0') *p++ = '\0';
  }
  return n;
}

constexpr const char* kUsage =
    "dummy: help | show | set <field>=<value>|na ... | status <node> | "
    "event <node> <type> [follow]";

}  // namespace

// A plausible site at midday: gate closed, charging, a healthy four-cell pack. Every block
// valid, so each rule the console exercises starts from a document that is all numbers.
DummyPublisher::DummyPublisher() {
  GateLinkStatusV1& s     = status_;
  s.gate_state            = static_cast<uint8_t>(lran::GateState::Closed);
  s.last_direction        = static_cast<uint8_t>(lran::Direction::Entry);
  s.last_traversal_age_s  = 3600;
  s.batt_mv               = 13250;
  s.batt_ma               = 1200;
  s.pv_cv                 = 1850;
  s.pv_w                  = 20;
  s.load_ma               = 150;
  s.yield_today           = 12;
  s.yield_yest            = 34;
  s.pmax_today            = 45;
  s.yield_total           = 12345;
  s.charge_state          = 3;
  s.mppt_tracker          = 2;
  s.mppt_temp_c10         = 250;
  s.bms_soc               = 80;
  s.bms_flags             = 0x07;  // spec 7.2.7 - valid, both FETs on
  s.pack_mv               = 13240;
  s.pack_ma               = 1100;
  s.cell_count            = 4;
  s.bms_rssi_neg          = 70;
  for (size_t i = 0; i < lran::schema::kMaxCells; ++i) {
    s.cell_mv[i]     = 3310;
    s.cell_temp_c[i] = 22;
  }
  s.bms_cycles            = 12;
  s.bms_capacity_dah      = 1000;
  s.bms_age_s             = 5;
  s.uptime_s              = 3600;
  s.boot_count            = 1;
  s.node_mv               = 13200;
  s.node_ma               = 45;
  s.enclosure_temp_c10    = 285;
  s.node_flags            = 0x0B;  // spec 7.2.8 - config persisted, SD ok, BMS BLE
  s.status_reason         = static_cast<uint8_t>(lran::StatusReason::DebugSynthetic);
}

void DummyPublisher::advance(uint32_t now_ms) {
  if (clock_valid_) {
    const uint32_t elapsed = now_ms - clock_ms_ + carry_ms_;  // unsigned: survives a wrap
    const uint32_t secs    = elapsed / 1000;
    carry_ms_              = elapsed % 1000;
    status_.uptime_s += secs;
    if (status_.last_traversal_age_s != lran::kU32NotAvailable) {
      status_.last_traversal_age_s += secs;
    }
  }
  clock_valid_ = true;
  clock_ms_    = now_ms;
}

bool DummyPublisher::build_status(lran::NodeId node, lran::CtxId ctx_id, uint32_t now_ms,
                                  RxMessage* out) {
  // R-5.2d. Set here as well as in the constructor, so no path to the wire skips it.
  status_.status_reason = static_cast<uint8_t>(lran::StatusReason::DebugSynthetic);
  advance(now_ms);
  *out = RxMessage{};
  size_t n = 0;
  if (lran::schema::serialize(status_, out->payload, sizeof(out->payload), &n) !=
      lran::Status::Ok) {
    return false;
  }
  out->payload_len = n;
  out->hdr.type    = lran::MsgType::Status;
  out->hdr.src     = node;
  out->hdr.dst     = lran::kNodeBridge;
  out->hdr.seq     = ++seq_;
  out->hdr.ctx_id  = ctx_id;
  out->hdr.schema  = lran::kSchemaGateLinkStatusV1;
  out->rx_millis   = now_ms;
  out->dummy       = true;
  return true;
}

bool DummyPublisher::build_event(lran::NodeId node, uint8_t event_type, bool follow_up,
                                 lran::CtxId ctx_id, uint32_t now_ms, RxMessage* out) {
  advance(now_ms);
  lran::schema::GateLinkEventV1 e;
  e.event_type  = event_type;
  e.event_flags = follow_up ? lran::schema::kEventFlagFollowUp : 0;
  e.hold_source = static_cast<uint8_t>((status_.hold >> 1) & 0x07);  // spec 7.2.1
  e.direction   = status_.last_direction;
  e.gate_state  = status_.gate_state;
  e.input_bits  = status_.input_bits;
  // spec 7.3 - a follow-up reuses its first edge's event_id; a new event takes the next.
  e.event_id = follow_up ? event_id_ : ++event_id_;
  e.uptime_s = status_.uptime_s;
  *out = RxMessage{};
  size_t n = 0;
  if (lran::schema::serialize(e, out->payload, sizeof(out->payload), &n) != lran::Status::Ok) {
    return false;
  }
  out->payload_len = n;
  out->hdr.type    = lran::MsgType::Event;
  out->hdr.src     = node;
  out->hdr.dst     = lran::kNodeBridge;
  out->hdr.seq     = ++seq_;
  out->hdr.ctx_id  = ctx_id;
  out->hdr.schema  = lran::kSchemaGateLinkEventV1;
  out->rx_millis   = now_ms;
  out->dummy       = true;
  return true;
}

DummyOutcome DummyPublisher::handle(const char* line, lran::CtxId ctx_id, uint32_t now_ms,
                                    RxMessage* out, char* reply, size_t cap) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%s", line != nullptr ? line : "");
  char*        w[24];
  const size_t n = split(buf, w, sizeof(w) / sizeof(w[0]));
  if (n == 0 || std::strcmp(w[0], "dummy") != 0) {
    if (cap > 0) reply[0] = '\0';
    return DummyOutcome::NotMine;
  }
  const char* verb = n > 1 ? w[1] : "help";

  if (std::strcmp(verb, "help") == 0) {
    std::snprintf(reply, cap, "%s", kUsage);
    return DummyOutcome::Reply;
  }

  if (std::strcmp(verb, "show") == 0) {
    size_t at = 0;
    for (const Field& f : kFields) {
      const int r = std::snprintf(reply + at, at < cap ? cap - at : 0, "%s%s=%lld",
                                  at == 0 ? "" : " ", f.name,
                                  static_cast<long long>(f.get(status_)));
      if (r < 0 || static_cast<size_t>(r) >= cap - at) break;  // a short reply, never a torn one
      at += static_cast<size_t>(r);
    }
    for (size_t i = 0; i < lran::schema::kMaxCells; ++i) {
      const int r = std::snprintf(reply + at, at < cap ? cap - at : 0, " %s=%u %s=%d",
                                  kCellMv[i], static_cast<unsigned>(status_.cell_mv[i]),
                                  kCellTemp[i], static_cast<int>(status_.cell_temp_c[i]));
      if (r < 0 || static_cast<size_t>(r) >= cap - at) break;
      at += static_cast<size_t>(r);
    }
    return DummyOutcome::Reply;
  }

  if (std::strcmp(verb, "set") == 0) {
    if (n < 3) {
      std::snprintf(reply, cap, "dummy: set needs <field>=<value>");
      return DummyOutcome::Refused;
    }
    // Every pair is checked before any is applied, so a typo in the third leaves the first
    // two unset rather than half a change.
    GateLinkStatusV1 next = status_;
    for (size_t i = 2; i < n; ++i) {
      char* eq = std::strchr(w[i], '=');
      if (eq == nullptr) {
        std::snprintf(reply, cap, "dummy: '%s' is not <field>=<value>", w[i]);
        return DummyOutcome::Refused;
      }
      *eq = '\0';
      const char* name  = w[i];
      const char* value = eq + 1;
      if (std::strcmp(name, "status_reason") == 0) {
        std::snprintf(reply, cap, "dummy: status_reason is DEBUG_SYNTHETIC, always (R-5.2d)");
        return DummyOutcome::Refused;
      }
      size_t cell = 0;
      bool   temp = false;
      if (cell_field(name, &cell, &temp)) {
        int64_t v = 0;
        const int64_t lo = temp ? INT8_MIN : 0;
        const int64_t hi = temp ? INT8_MAX : kU16;
        if (!parse_int(value, &v) || v < lo || v > hi) {
          std::snprintf(reply, cap, "dummy: %s takes %lld..%lld", name,
                        static_cast<long long>(lo), static_cast<long long>(hi));
          return DummyOutcome::Refused;
        }
        if (temp) {
          next.cell_temp_c[cell] = static_cast<int8_t>(v);
        } else {
          next.cell_mv[cell] = static_cast<uint16_t>(v);
        }
        continue;
      }
      const Field* f = find_field(name);
      if (f == nullptr) {
        std::snprintf(reply, cap, "dummy: no field '%s' (dummy show lists them)", name);
        return DummyOutcome::Refused;
      }
      int64_t v = 0;
      if (std::strcmp(value, "na") == 0) {
        if (!f->has_na) {
          std::snprintf(reply, cap, "dummy: %s has no sentinel", name);
          return DummyOutcome::Refused;
        }
        v = f->na;
      } else if (!parse_int(value, &v) || v < f->min || v > f->max) {
        std::snprintf(reply, cap, "dummy: %s takes %lld..%lld", name,
                      static_cast<long long>(f->min), static_cast<long long>(f->max));
        return DummyOutcome::Refused;
      }
      f->set(next, v);
    }
    status_ = next;
    std::snprintf(reply, cap, "dummy: set; `dummy status <node>` sends it");
    return DummyOutcome::Reply;
  }

  const bool is_status = std::strcmp(verb, "status") == 0;
  const bool is_event  = std::strcmp(verb, "event") == 0;
  if (!is_status && !is_event) {
    std::snprintf(reply, cap, "%s", kUsage);
    return DummyOutcome::Refused;
  }
  lran::NodeId node = 0;
  if (n < 3 || !find_node(w[2], &node)) {
    std::snprintf(reply, cap, "dummy: name a registered node, e.g. gatelink");
    return DummyOutcome::Refused;
  }
  if (lran::is_bench_node(node)) {
    std::snprintf(reply, cap, "dummy: %s is a bench node; spec 16.6 publishes neither", w[2]);
    return DummyOutcome::Refused;
  }

  if (is_status) {
    if (n != 3 || !build_status(node, ctx_id, now_ms, out)) {
      std::snprintf(reply, cap, "dummy: status <node>");
      return DummyOutcome::Refused;
    }
    std::snprintf(reply, cap, "dummy: STATUS from %s, DEBUG_SYNTHETIC", w[2]);
    return DummyOutcome::Inject;
  }

  // event <node> <type> [follow]
  if (n < 4 || n > 5 || (n == 5 && std::strcmp(w[4], "follow") != 0)) {
    std::snprintf(reply, cap, "dummy: event <node> <type> [follow]");
    return DummyOutcome::Refused;
  }
  int64_t type = -1;
  if (!parse_int(w[3], &type)) {
    for (int v = 0; v <= UINT8_MAX; ++v) {
      const char* name = event_type_name(static_cast<uint8_t>(v));
      if (name != nullptr && std::strcmp(name, w[3]) == 0) {
        type = v;
        break;
      }
    }
  }
  if (type < 0 || type > UINT8_MAX) {
    std::snprintf(reply, cap, "dummy: no event type '%s' (spec 8.9, lower case)", w[3]);
    return DummyOutcome::Refused;
  }
  const bool follow = n == 5;
  if (follow && event_id_ == 0) {
    std::snprintf(reply, cap, "dummy: a follow-up needs an event before it");
    return DummyOutcome::Refused;
  }
  if (!build_event(node, static_cast<uint8_t>(type), follow, ctx_id, now_ms, out)) {
    std::snprintf(reply, cap, "dummy: event did not encode");
    return DummyOutcome::Refused;
  }
  std::snprintf(reply, cap, "dummy: EVENT %s id %lu%s from %s", w[3],
                static_cast<unsigned long>(event_id_), follow ? " follow-up" : "", w[2]);
  return DummyOutcome::Inject;
}

}  // namespace bridge
