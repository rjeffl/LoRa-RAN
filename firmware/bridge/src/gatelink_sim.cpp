// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The bridge-side GateLink simulator. Task BF-27; Impl Plan 6.6.3. See gatelink_sim.h.

#include "gatelink_sim.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "dummy.h"
#include "lran/schema/gatelink_event_v1.h"

namespace bridge {
namespace {

using lran::schema::GateLinkStatusV1;

constexpr float kDayS       = 86400.0f;
constexpr float kSunriseS   = 6.0f * 3600.0f;
constexpr float kDaylightS  = 12.0f * 3600.0f;
constexpr float kStartTodS  = 9.0f * 3600.0f;  // mid-morning, so the first STATUS has sun
constexpr float kPi         = 3.14159265f;

// The site. A 60 W panel, a 100 Ah four-cell LiFePO4 pack, a 150 mA load on the MPPT's load
// output and the node's own 45 mA. Round numbers of the right size, not measurements.
constexpr float   kPanelW      = 60.0f;
constexpr float   kPackMah     = 100000.0f;
constexpr float   kLoadMa      = 150.0f;
constexpr int16_t kNodeMa      = 45;
constexpr float   kStartSocPct = 70.0f;

// Constant per-cell offsets, so the four cells differ without a random source: a
// reproducible run is worth more on the bench than a realistic one.
constexpr int16_t kCellOffsetMv[lran::schema::kMaxCells] = {-4, 2, 5, -3};

uint16_t clamp_u16(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 65535.0f) return UINT16_MAX;
  return static_cast<uint16_t>(std::lround(v));
}

int16_t clamp_i16(float v) {
  // INT16_MIN is the sentinel (root rule 6), so a real value stops one short of it.
  if (v <= -32767.0f) return -32767;
  if (v >= 32767.0f) return INT16_MAX;
  return static_cast<int16_t>(std::lround(v));
}

// spec 8.3 - IN1 is bit 0 and IN2 bit 1 of input_bits; gate_state is derived from them.
uint8_t inputs_for(lran::GateState g) {
  switch (g) {
    case lran::GateState::Moving:        return 0x02;
    case lran::GateState::OpenCountdown: return 0x03;
    case lran::GateState::OpenHeld:      return 0x01;
    default:                             return 0x00;
  }
}

// Wrap-safe: true once `now` has reached `due`.
bool reached(uint32_t now, uint32_t due) { return static_cast<int32_t>(now - due) >= 0; }

bool parse_setting(const char* word, const char* key, uint32_t lo, uint32_t hi, bool zero_ok,
                   uint32_t* out, bool* matched, char* reply, size_t cap) {
  const size_t k = std::strlen(key);
  if (std::strncmp(word, key, k) != 0 || word[k] != '=') return true;
  *matched  = true;
  int64_t v = 0;
  if (!console_int(word + k + 1, &v) || ((v < lo || v > hi) && !(zero_ok && v == 0))) {
    std::snprintf(reply, cap, "sim: %s takes %s%lu..%lu", key, zero_ok ? "0 or " : "",
                  static_cast<unsigned long>(lo), static_cast<unsigned long>(hi));
    return false;
  }
  *out = static_cast<uint32_t>(v);
  return true;
}

constexpr const char* kUsage =
    "sim: help | start <node> [period=<s>] [day=<s>] [gate=<s>] | stop | show";

}  // namespace

GateLinkSim::GateLinkSim() { reset_model(); }

void GateLinkSim::reset_model() {
  status_                      = GateLinkStatusV1{};
  GateLinkStatusV1& s          = status_;
  s.gate_state                 = static_cast<uint8_t>(lran::GateState::Closed);
  s.input_bits                 = inputs_for(lran::GateState::Closed);
  s.last_direction             = static_cast<uint8_t>(lran::Direction::Exit);
  s.last_traversal_age_s       = lran::kU32NotAvailable;  // nothing through yet
  s.load_ma                    = static_cast<int16_t>(kLoadMa);
  s.mppt_tracker               = 2;
  s.bms_flags                  = 0x07;  // spec 7.2.7 - valid, both FETs on
  s.cell_count                 = 4;
  s.bms_rssi_neg               = 70;
  s.bms_cycles                 = 12;
  s.bms_capacity_dah           = static_cast<uint16_t>(kPackMah / 100.0f);
  s.bms_age_s                  = 5;
  s.boot_count                 = 1;
  s.node_ma                    = kNodeMa;
  s.node_flags                 = 0x0B;  // spec 7.2.8 - config persisted, SD ok, BMS BLE
  s.status_reason              = static_cast<uint8_t>(lran::StatusReason::DebugSynthetic);
  tod_s_    = kStartTodS;
  soc_mah_  = kPackMah * kStartSocPct / 100.0f;
  wh_today_ = 0;
  wh_total_ = 0;
  carry_ms_ = 0;
  step(0);  // the derived fields, from the state above
}

uint32_t GateLinkSim::time_of_day_s() const { return static_cast<uint32_t>(tod_s_); }

void GateLinkSim::step(uint32_t real_ms) {
  GateLinkStatusV1& s = status_;

  // The node's own clocks run in real seconds, whatever the simulated day does.
  const uint32_t elapsed = real_ms + carry_ms_;
  const uint32_t secs    = elapsed / 1000;
  carry_ms_              = elapsed % 1000;
  s.uptime_s += secs;
  if (s.last_traversal_age_s != lran::kU32NotAvailable) s.last_traversal_age_s += secs;

  // The sun runs in simulated seconds. A midnight rolls today's yield into yesterday's, as
  // VE.Direct's H20 and H22 do.
  const float dt_h = (static_cast<float>(real_ms) / 1000.0f) * (kDayS / day_s_) / 3600.0f;
  tod_s_ += dt_h * 3600.0f;
  while (tod_s_ >= kDayS) {
    tod_s_ -= kDayS;
    s.yield_yest = s.yield_today;
    wh_today_    = 0;
    s.pmax_today = 0;
  }
  const float into = tod_s_ - kSunriseS;
  const float sun  = (into > 0.0f && into < kDaylightS) ? std::sin(kPi * into / kDaylightS) : 0;

  const float soc_pct = 100.0f * soc_mah_ / kPackMah;
  const float pack_v  = 12.8f + 0.008f * soc_pct;  // LiFePO4's flat middle, roughly
  const float pv_w    = kPanelW * sun;
  float       charge  = pv_w / pack_v * 1000.0f;  // mA into the battery terminals
  uint8_t     cs      = 0;                        // VE.Direct CS: off
  if (pv_w >= 1.0f) {
    if (soc_pct >= 99.5f) {
      charge = charge < kLoadMa ? charge : kLoadMa;  // float: the panel carries the load
      cs     = 5;
    } else {
      cs = soc_pct >= 95.0f ? 4 : 3;  // absorption, else bulk
    }
  } else {
    charge = 0;
  }
  const float batt_ma = charge - kLoadMa;
  soc_mah_ += (batt_ma - kNodeMa) * dt_h;
  if (soc_mah_ < 0) soc_mah_ = 0;
  if (soc_mah_ > kPackMah) soc_mah_ = kPackMah;
  wh_today_ += pv_w * dt_h;
  wh_total_ += pv_w * dt_h;

  const float pack_ma = batt_ma - kNodeMa;
  const float pack_mv = pack_v * 1000.0f + pack_ma / 50.0f;  // ~20 mOhm of pack and wiring
  const float ambient = 15.0f + 10.0f * sun;

  s.pv_w          = clamp_u16(pv_w);
  s.pv_cv         = sun > 0.0f ? clamp_u16(1700.0f + 250.0f * sun) : 30;  // 10 mV units
  s.batt_ma       = clamp_i16(batt_ma);
  s.batt_mv       = clamp_u16(pack_mv + 5.0f);
  s.charge_state  = cs;
  s.yield_today   = clamp_u16(wh_today_ / 10.0f);
  s.yield_total   = static_cast<uint32_t>(wh_total_ / 10.0f);
  if (s.pv_w > s.pmax_today) s.pmax_today = s.pv_w;
  s.mppt_temp_c10 = clamp_i16((ambient + 8.0f * sun) * 10.0f);
  s.bms_soc       = static_cast<uint8_t>(std::lround(100.0f * soc_mah_ / kPackMah));
  s.pack_mv       = clamp_u16(pack_mv);
  s.pack_ma       = clamp_i16(pack_ma);
  for (size_t i = 0; i < lran::schema::kMaxCells; ++i) {
    s.cell_mv[i]     = clamp_u16(pack_mv / 4.0f + kCellOffsetMv[i]);
    s.cell_temp_c[i] = static_cast<int8_t>(std::lround(ambient + 2.0f));
  }
  s.node_mv            = clamp_u16(pack_mv - 50.0f);
  s.enclosure_temp_c10 = clamp_i16((ambient + 5.0f * sun) * 10.0f);
}

bool GateLinkSim::make_event(lran::EventType type, uint32_t now_ms, RxMessage* out) {
  lran::schema::GateLinkEventV1 e;
  e.event_type  = static_cast<uint8_t>(type);
  e.event_flags = 0;
  e.hold_source = static_cast<uint8_t>((status_.hold >> 1) & 0x07);  // spec 7.2.1
  e.direction   = status_.last_direction;
  e.gate_state  = status_.gate_state;
  e.input_bits  = status_.input_bits;
  e.event_id    = ++event_id_;
  e.uptime_s    = status_.uptime_s;
  return synthetic_event(e, node_, ctx_id_, ++seq_, now_ms, out);
}

// One cycle: a vehicle, the gate opens, counts down, closes. Five edges, each its own frame.
bool GateLinkSim::gate_edge(uint32_t now_ms, RxMessage* out) {
  if (gate_s_ == 0) return false;
  static constexpr uint32_t kEdgeAtS[] = {0, 0, kSimOpenAtS, kSimClosingAtS, kSimClosedAtS};
  if (!reached(now_ms, cycle_ms_ + kEdgeAtS[cycle_edge_] * 1000)) return false;

  GateLinkStatusV1& s    = status_;
  const uint8_t     edge = cycle_edge_;
  lran::GateState   next = static_cast<lran::GateState>(s.gate_state);
  if (edge == 0) {
    // Alternate the direction, so both of the detect document's values are seen.
    const bool exit = s.last_direction != static_cast<uint8_t>(lran::Direction::Exit);
    s.last_direction       = static_cast<uint8_t>(exit ? lran::Direction::Exit
                                                       : lran::Direction::Entry);
    s.movement_cause       = static_cast<uint8_t>(exit ? lran::MovementCause::ExitWand
                                                       : lran::MovementCause::ExternalMomentary);
    s.last_traversal_age_s = 0;
  } else if (edge == 1 || edge == 3) {
    next = lran::GateState::Moving;
  } else if (edge == 2) {
    next = lran::GateState::OpenCountdown;
  } else {
    next = lran::GateState::Closed;
  }
  s.gate_state = static_cast<uint8_t>(next);
  s.input_bits = inputs_for(next);

  if (++cycle_edge_ == sizeof(kEdgeAtS) / sizeof(kEdgeAtS[0])) {
    cycle_edge_ = 0;
    cycle_ms_ += gate_s_ * 1000;
  }
  return make_event(edge == 0 ? lran::EventType::VehicleDetected
                              : lran::EventType::GateStateChange,
                    now_ms, out);
}

bool GateLinkSim::poll(uint32_t now_ms, RxMessage* out) {
  if (!running_) return false;
  step(now_ms - last_ms_);
  last_ms_ = now_ms;
  if (gate_edge(now_ms, out)) return true;
  if (!reached(now_ms, next_status_ms_)) return false;
  next_status_ms_ += period_s_ * 1000;
  // A poll() that ran late does not send a burst to catch up: one STATUS, then the cadence.
  if (reached(now_ms, next_status_ms_)) next_status_ms_ = now_ms + period_s_ * 1000;
  return synthetic_status(status_, node_, ctx_id_, ++seq_, now_ms, out);
}

SimOutcome GateLinkSim::handle(const char* line, lran::CtxId ctx_id, uint32_t now_ms,
                               char* reply, size_t cap) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%s", line != nullptr ? line : "");
  char*        w[8];
  const size_t n = console_split(buf, w, sizeof(w) / sizeof(w[0]));
  if (n == 0 || std::strcmp(w[0], "sim") != 0) {
    if (cap > 0) reply[0] = '\0';
    return SimOutcome::NotMine;
  }
  const char* verb = n > 1 ? w[1] : "help";

  if (std::strcmp(verb, "help") == 0) {
    std::snprintf(reply, cap, "%s", kUsage);
    return SimOutcome::Reply;
  }

  if (std::strcmp(verb, "stop") == 0) {
    std::snprintf(reply, cap, running_ ? "sim: stopped" : "sim: not running");
    running_ = false;
    return SimOutcome::Reply;
  }

  if (std::strcmp(verb, "show") == 0) {
    const GateLinkStatusV1& s   = status_;
    const uint32_t          tod = time_of_day_s();
    std::snprintf(reply, cap,
                  "sim: %s node %02x period=%lu day=%lu gate=%lu | %02lu:%02lu pv_w=%u "
                  "batt_ma=%d soc=%u%% cs=%u gate_state=%u seq=%lu events=%lu",
                  running_ ? "running" : "stopped", static_cast<unsigned>(node_),
                  static_cast<unsigned long>(period_s_), static_cast<unsigned long>(day_s_),
                  static_cast<unsigned long>(gate_s_), static_cast<unsigned long>(tod / 3600),
                  static_cast<unsigned long>((tod / 60) % 60), static_cast<unsigned>(s.pv_w),
                  static_cast<int>(s.batt_ma), static_cast<unsigned>(s.bms_soc),
                  static_cast<unsigned>(s.charge_state), static_cast<unsigned>(s.gate_state),
                  static_cast<unsigned long>(seq_), static_cast<unsigned long>(event_id_));
    return SimOutcome::Reply;
  }

  if (std::strcmp(verb, "start") != 0) {
    std::snprintf(reply, cap, "%s", kUsage);
    return SimOutcome::Refused;
  }
  lran::NodeId node = 0;
  if (n < 3 || !console_node(w[2], &node)) {
    std::snprintf(reply, cap, "sim: name a registered node, e.g. gatelink");
    return SimOutcome::Refused;
  }
  if (lran::is_bench_node(node)) {
    std::snprintf(reply, cap, "sim: %s is a bench node; spec 16.6 publishes neither", w[2]);
    return SimOutcome::Refused;
  }
  // Every setting is checked before any is applied, as `dummy set` does.
  uint32_t period = kSimPeriodDefaultS;
  uint32_t day    = kSimDayDefaultS;
  uint32_t gate   = kSimGateDefaultS;
  for (size_t i = 3; i < n; ++i) {
    bool matched = false;
    if (!parse_setting(w[i], "period", 1, 3600, false, &period, &matched, reply, cap) ||
        !parse_setting(w[i], "day", 60, 86400, false, &day, &matched, reply, cap) ||
        !parse_setting(w[i], "gate", kSimGateMinS, 86400, true, &gate, &matched, reply, cap)) {
      return SimOutcome::Refused;
    }
    if (!matched) {
      std::snprintf(reply, cap, "sim: '%s' is not period=, day= or gate=", w[i]);
      return SimOutcome::Refused;
    }
  }

  node_     = node;
  ctx_id_   = ctx_id;
  seq_      = 0;
  event_id_ = 0;
  period_s_ = period;
  day_s_    = day;
  gate_s_   = gate;
  reset_model();
  last_ms_        = now_ms;
  next_status_ms_ = now_ms;  // the first STATUS at once, so a start is visible
  cycle_ms_       = now_ms + gate * 1000;
  cycle_edge_     = 0;
  running_        = true;
  std::snprintf(reply, cap, "sim: %s every %lus, day %lus, %s; synthetic", w[2],
                static_cast<unsigned long>(period), static_cast<unsigned long>(day),
                gate == 0 ? "no events" : "gate events on");
  return SimOutcome::Reply;
}

}  // namespace bridge
