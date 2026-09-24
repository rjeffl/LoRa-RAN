// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The publication policy. Task BF-24; see publish.h.

#include "publish.h"

#include <cstdio>
#include <cstring>

#include "json_writer.h"
#include "lran/schema/gatelink_status_v1.h"
#include "lran/schema/node_health_v1.h"
#include "net_policy.h"

namespace bridge {
namespace {

// spec 7.2.1 input_bits, 7.2.5 detect_flags, 7.2.6 mppt_flags, 7.2.7 bms_flags and
// 7.2.8 node_flags. The library carries the fields and not their bits, so they are named
// here, where the documents read them.
constexpr uint8_t kInOpen   = 0x01;
constexpr uint8_t kInMoving = 0x02;
constexpr uint8_t kInSafety = 0x04;
constexpr uint8_t kInExit   = 0x08;
constexpr uint8_t kInFire   = 0x10;
constexpr uint8_t kInAlarm  = 0x20;

constexpr uint8_t kHoldHeldOpen = 0x01;  // bits 3:1 are hold_source

constexpr uint8_t kDetSafety       = 0x01;
constexpr uint8_t kDetExit         = 0x02;
constexpr uint8_t kDetClassifying  = 0x04;
constexpr uint8_t kDetVehicleHeld  = 0x08;
constexpr uint8_t kDetSuppressed   = 0x10;

constexpr uint8_t kMpptLoadOn          = 0x01;
constexpr uint8_t kMpptStale           = 0x02;
constexpr uint8_t kMpptHexPending      = 0x04;
constexpr uint8_t kMpptChargeInhibited = 0x08;

constexpr uint8_t kBmsValid           = 0x01;
constexpr uint8_t kBmsChargeFet       = 0x02;
constexpr uint8_t kBmsDischargeFet    = 0x04;
constexpr uint8_t kBmsChargeInhibited = 0x08;
constexpr uint8_t kBmsProtection      = 0x10;
constexpr uint8_t kBmsBalancing       = 0x20;

constexpr uint8_t kNodeConfigPersisted    = 0x01;
constexpr uint8_t kNodeSdOk               = 0x02;
constexpr uint8_t kNodeDryRun             = 0x04;
constexpr uint8_t kNodeBmsBle             = 0x08;
constexpr uint8_t kNodeDebug              = 0x10;
constexpr uint8_t kNodeShutdownLatch      = 0x20;
constexpr uint8_t kNodeTraversalVolatile  = 0x40;

// spec 7.2.9 - the bridge turns an age into an absolute time. The node's age is whole
// seconds, and the frame spends a fraction of one in the air and in the queues, so the
// computed time moves by a second between two polls with no traversal between them. A
// move this small is that jitter; a new traversal moves it by at least a poll interval.
constexpr UtcSeconds kTraversalJitterS = 2;

// Before this the clock is the ESP32's power-on default, not SNTP's answer. 2026-01-01.
constexpr UtcSeconds kUtcPlausible = 1767225600;

// Spec 8.3-8.7's names, lowercased so one term names one concept from HA to the wire, as
// net_policy.h's command tokens are. A value the table does not list is null, never a
// guess: a newer node's value reads as unknown until this bridge learns it, and
// `input_bits` carries the raw evidence meanwhile.
const char* gate_state_name(uint8_t v) {
  switch (static_cast<lran::GateState>(v)) {
    case lran::GateState::Unknown:       return "unknown";
    case lran::GateState::Closed:        return "closed";
    case lran::GateState::Moving:        return "moving";
    case lran::GateState::OpenCountdown: return "open_countdown";
    case lran::GateState::OpenHeld:      return "open_held";
    case lran::GateState::Fault:         return "fault";
  }
  return nullptr;
}

const char* hold_source_name(uint8_t v) {
  switch (static_cast<lran::HoldSource>(v)) {
    case lran::HoldSource::None:       return "none";
    case lran::HoldSource::Lran:       return "lran";
    case lran::HoldSource::Manual:     return "manual";
    case lran::HoldSource::KeypadFire: return "keypad_fire";
    case lran::HoldSource::Unknown:    return "unknown";
  }
  return nullptr;
}

const char* movement_cause_name(uint8_t v) {
  switch (static_cast<lran::MovementCause>(v)) {
    case lran::MovementCause::Unknown:           return "unknown";
    case lran::MovementCause::ExitWand:          return "exit_wand";
    case lran::MovementCause::LranCommand:       return "lran_command";
    case lran::MovementCause::KeypadFire:        return "keypad_fire";
    case lran::MovementCause::ManualHold:        return "manual_hold";
    case lran::MovementCause::ExternalMomentary: return "external_momentary";
  }
  return nullptr;
}

const char* direction_name(uint8_t v) {
  switch (static_cast<lran::Direction>(v)) {
    case lran::Direction::None:         return "none";
    case lran::Direction::Entry:        return "entry";
    case lran::Direction::Exit:         return "exit";
    case lran::Direction::Undetermined: return "undetermined";
  }
  return nullptr;
}

const char* status_reason_name(uint8_t v) {
  switch (static_cast<lran::StatusReason>(v)) {
    case lran::StatusReason::PollResponse:    return "poll_response";
    case lran::StatusReason::GateStateChange: return "gate_state_change";
    case lran::StatusReason::HoldStateChange: return "hold_state_change";
    case lran::StatusReason::MpptError:       return "mppt_error";
    case lran::StatusReason::VehicleDetected: return "vehicle_detected";
    case lran::StatusReason::BmsAlarm:        return "bms_alarm";
    case lran::StatusReason::HardShutdown:    return "hard_shutdown";
    case lran::StatusReason::Fire:            return "fire";
    case lran::StatusReason::ConfigChange:    return "config_change";
    case lran::StatusReason::Boot:            return "boot";
    case lran::StatusReason::ChargeInhibited: return "charge_inhibited";
    case lran::StatusReason::DebugSynthetic:  return "debug_synthetic";
  }
  return nullptr;
}

// spec 7.2.7 bits 7:6.
const char* soc_source_name(uint8_t bms_flags) {
  switch (bms_flags >> 6) {
    case 0: return "bms_ble";
    case 1: return "smartshunt";
    case 2: return "voltage_coarse";
    default: return "unknown";
  }
}

// A string or JSON null. JsonObject::str() omits a null value, and a key that comes and
// goes would make a value template read an undefined name.
void str_or_null(JsonObject& j, const char* key, const char* v) {
  if (v == nullptr) {
    j.null(key);
  } else {
    j.str(key, v);
  }
}

void i16_or_null(JsonObject& j, const char* key, int16_t v) {
  if (v == lran::kI16NotAvailable) {
    j.null(key);
  } else {
    j.i32(key, v);
  }
}

void tenths_or_null(JsonObject& j, const char* key, int16_t v) {
  if (v == lran::kI16NotAvailable) {
    j.null(key);
  } else {
    j.decimal(key, v, 1);
  }
}

uint32_t fnv1a(const char* s, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; ++i) {
    h ^= static_cast<uint8_t>(s[i]);
    h *= 16777619u;
  }
  return h;
}

size_t node_index(lran::NodeId id) {
  for (size_t i = 0; i < kNodeCount; ++i) {
    if (kNodeTable[i].id == id) return i;
  }
  return kNodeCount;
}

// Howard Hinnant's days_from_civil inverse, for the one conversion this file needs. gmtime
// would do it on both targets, but its result lives in shared static storage on one of them.
void civil_from_days(int64_t z, int64_t* y, unsigned* m, unsigned* d) {
  z += 719468;
  const int64_t  era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp  = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = static_cast<int64_t>(yoe) + era * 400 + (*m <= 2 ? 1 : 0);
}

}  // namespace

const char* domain_path(Domain d) {
  switch (d) {
    case Domain::Gate:    return "gate";
    case Domain::Detect:  return "detect";
    case Domain::Solar:   return "solar";
    case Domain::Battery: return "battery";
    case Domain::Node:    return "node";
    case Domain::Health:  return "node/health";
    case Domain::kCount:  break;
  }
  return nullptr;
}

bool domain_carries_availability(Domain d) {
  return d == Domain::Solar || d == Domain::Battery;
}

size_t format_utc(UtcSeconds t, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  const int64_t days = (t >= 0 ? t : t - 86399) / 86400;
  const int64_t sod  = t - days * 86400;
  int64_t  y;
  unsigned m;
  unsigned d;
  civil_from_days(days, &y, &m, &d);
  const int n = std::snprintf(out, cap, "%04lld-%02u-%02uT%02u:%02u:%02uZ",
                              static_cast<long long>(y), m, d,
                              static_cast<unsigned>(sod / 3600),
                              static_cast<unsigned>((sod / 60) % 60),
                              static_cast<unsigned>(sod % 60));
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t publish_stats_json(const PublishStats& s, char* out, size_t cap) {
  JsonObject j(out, cap);
  j.u32("status_frames", s.status_frames);
  j.u32("documents", s.documents);
  j.u32("unchanged", s.unchanged);
  j.u32("heartbeats", s.heartbeats);
  j.u32("bench_withheld", s.bench_withheld);
  j.u32("queue_refused", s.queue_refused);
  j.u32("undecodable", s.undecodable);
  return j.finish();
}

void PublicationPolicy::forget_published() {
  for (auto& node : slots_) {
    for (auto& s : node) s.valid = false;
  }
}

void PublicationPolicy::offer(size_t ni, Domain d, const char* node_token, size_t len,
                              uint32_t now_ms, PublishSink& sink) {
  if (len == 0) return;  // did not fit; JsonObject left it empty rather than truncated
  Slot&          slot = slots_[ni][static_cast<size_t>(d)];
  const uint32_t h    = fnv1a(doc_, len);

  // Impl Plan 6.3 - publish on change, with a heartbeat. The hash stands in for the last
  // document, which would cost a kilobyte per node and domain to keep. Two documents that
  // collide leave a change unpublished until the heartbeat, one time in 2^32.
  const bool     same     = slot.valid && slot.hash == h;
  const uint32_t interval = static_cast<uint32_t>(levers_.republish_interval_s) * 1000u;
  if (same && now_ms - slot.at_ms < interval) {
    ++stats_.unchanged;
    return;
  }

  char topic[kMaxTopicLen];
  if (topic_domain_state(node_token, domain_path(d), topic, sizeof(topic)) == 0) return;
  if (!sink.emit(topic, doc_, true)) {
    ++stats_.queue_refused;
    return;
  }
  if (same) ++stats_.heartbeats;
  ++stats_.documents;
  slot.valid = true;
  slot.hash  = h;
  slot.at_ms = now_ms;
}

void PublicationPolicy::on_status(const NodeInfo& info, const lran::Header& hdr,
                                  const uint8_t* payload, size_t payload_len,
                                  uint32_t now_ms, UtcSeconds utc_at_rx, PublishSink& sink) {
  if (hdr.type != lran::MsgType::Status) return;
  ++stats_.status_frames;

  const size_t ni = node_index(info.id);
  char         token[16];
  if (ni >= kNodeCount || node_topic_name(info.id, token, sizeof(token)) == 0) {
    ++stats_.undecodable;
    return;
  }

  if (hdr.schema == lran::kSchemaNodeHealthV1) {
    lran::schema::NodeHealthV1 h;
    if (lran::schema::deserialize(payload, payload_len, &h) != lran::Status::Ok) {
      ++stats_.undecodable;
      return;
    }
    if (info.is_bench) {
      ++stats_.bench_withheld;
      return;
    }
    JsonObject j(doc_, sizeof(doc_));
    j.u32("uptime_s", h.uptime_s);
    // spec 7.5 carries no sentinel for boot_count; spec 7.2.4's `0 if unavailable` is the
    // same field's rule in the node block, and a node restarting from zero boots is 1.
    if (h.boot_count == 0) j.null("boot_count"); else j.u32("boot_count", h.boot_count);
    j.u32("rx_frames", h.rx_frames);
    j.u32("tx_frames", h.tx_frames);
    j.u32("rx_dropped", h.rx_dropped);
    j.u32("cad_backoffs", h.cad_backoffs);
    i16_or_null(j, "last_rssi_dbm", h.last_rssi_dbm);
    tenths_or_null(j, "last_snr_db", h.last_snr_db10);
    j.u32("proto_ver", h.proto_ver);
    j.boolean("debug", (h.health_flags & lran::schema::kHealthFlagDebugActive) != 0);
    offer(ni, Domain::Health, token, j.finish(), now_ms, sink);
    return;
  }

  if (hdr.schema != lran::kSchemaGateLinkStatusV1 &&
      hdr.schema != lran::kSchemaSimnodeStatusV1) {
    ++stats_.undecodable;  // 0x20 is reserved and undefined (spec 7.1)
    return;
  }
  lran::schema::GateLinkStatusV1 s;
  if (lran::schema::deserialize(payload, payload_len, &s) != lran::Status::Ok) {
    ++stats_.undecodable;
    return;
  }
  // Spec 16.6 axis 1. 0xFE is bench-only by definition, and a bench node's 0x10 is still a
  // bench node's. make_publish() refuses these topics as well (bench_topic_forbidden).
  if (info.is_bench) {
    ++stats_.bench_withheld;
    return;
  }

  // R-5.2d and spec 8.7 - every document from a synthetic frame says so, because each
  // document is a separate history in Home Assistant.
  const bool synthetic =
      s.status_reason == static_cast<uint8_t>(lran::StatusReason::DebugSynthetic);

  // --- gate, spec 7.2.1 ---
  {
    JsonObject j(doc_, sizeof(doc_));
    str_or_null(j, "state", gate_state_name(s.gate_state));
    j.boolean("held_open", (s.hold & kHoldHeldOpen) != 0);
    str_or_null(j, "hold_source", hold_source_name(static_cast<uint8_t>((s.hold >> 1) & 0x07)));
    str_or_null(j, "movement_cause", movement_cause_name(s.movement_cause));
    str_or_null(j, "last_direction", direction_name(s.last_direction));
    j.boolean("in_open", (s.input_bits & kInOpen) != 0);
    j.boolean("in_moving", (s.input_bits & kInMoving) != 0);
    j.boolean("in_safety", (s.input_bits & kInSafety) != 0);
    j.boolean("in_exit", (s.input_bits & kInExit) != 0);
    j.boolean("in_fire", (s.input_bits & kInFire) != 0);
    j.boolean("in_alarm", (s.input_bits & kInAlarm) != 0);
    j.u32("input_bits", s.input_bits);
    j.boolean("synthetic", synthetic);
    offer(ni, Domain::Gate, token, j.finish(), now_ms, sink);
  }

  // --- detect, spec 7.2.5 and 7.2.9 ---
  {
    Held& held = held_[ni];
    // spec 7.2.9 - an age becomes a time only with a wall clock to subtract it from.
    // Without one the time is null rather than a guess, and it stays null until SNTP answers.
    if (s.last_traversal_age_s == lran::kU32NotAvailable || utc_at_rx < kUtcPlausible) {
      held.traversal_valid = false;
    } else {
      const UtcSeconds t = utc_at_rx - static_cast<UtcSeconds>(s.last_traversal_age_s);
      const UtcSeconds d = held.traversal_valid ? t - held.traversal : 0;
      if (!held.traversal_valid || d > kTraversalJitterS || d < -kTraversalJitterS) {
        held.traversal       = t;
        held.traversal_valid = true;
      }
    }
    char when[24];
    JsonObject j(doc_, sizeof(doc_));
    j.boolean("safety", (s.detect_flags & kDetSafety) != 0);
    j.boolean("exit", (s.detect_flags & kDetExit) != 0);
    j.boolean("classifying", (s.detect_flags & kDetClassifying) != 0);
    j.boolean("vehicle_while_held", (s.detect_flags & kDetVehicleHeld) != 0);
    j.boolean("suppressed", (s.detect_flags & kDetSuppressed) != 0);
    if (held.traversal_valid && format_utc(held.traversal, when, sizeof(when)) > 0) {
      j.str("last_traversal", when);
    } else {
      j.null("last_traversal");
    }
    j.boolean("traversal_persisted", (s.node_flags & kNodeTraversalVolatile) == 0);
    j.boolean("synthetic", synthetic);
    offer(ni, Domain::Detect, token, j.finish(), now_ms, sink);
  }

  // --- solar, spec 7.2.2 and 7.2.6 ---
  {
    // Spec 7.2.6 bit 1 and spec 16.4: a stale VE.Direct link marks every MPPT entity
    // unavailable. Every reading is null with it, so a template that ignores `available`
    // still cannot show the last good value as current (R-5.2b).
    const bool ok = (s.mppt_flags & kMpptStale) == 0;
    JsonObject j(doc_, sizeof(doc_));
    j.boolean("available", ok);
    if (ok) {
      j.u32("batt_mv", s.batt_mv);
      j.i32("batt_ma", s.batt_ma);
      j.u32("pv_mv", static_cast<uint32_t>(s.pv_cv) * 10u);  // 10 mV units on the wire
      j.u32("pv_w", s.pv_w);
      i16_or_null(j, "load_ma", s.load_ma);
      j.decimal("yield_today_kwh", s.yield_today, 2);  // 10 Wh units = 0.01 kWh
      j.decimal("yield_yesterday_kwh", s.yield_yest, 2);
      j.u32("pmax_today_w", s.pmax_today);
      j.decimal("yield_total_kwh", s.yield_total, 2);
      j.u32("charge_state", s.charge_state);  // spec 7.2.2 - Victron's, passed through
      j.u32("error", s.mppt_err);
      j.u32("tracker", s.mppt_tracker);
      j.boolean("load_on", (s.mppt_flags & kMpptLoadOn) != 0);
      j.boolean("charge_inhibited", (s.mppt_flags & kMpptChargeInhibited) != 0);
      tenths_or_null(j, "temp_c", s.mppt_temp_c10);
    } else {
      static const char* const kKeys[] = {
          "batt_mv", "batt_ma", "pv_mv", "pv_w", "load_ma", "yield_today_kwh",
          "yield_yesterday_kwh", "pmax_today_w", "yield_total_kwh", "charge_state", "error",
          "tracker", "load_on", "charge_inhibited", "temp_c"};
      for (const char* k : kKeys) j.null(k);
    }
    // Not a reading: whether the bridge's own HEX transaction is outstanding is true of the
    // node whatever the link to the MPPT is doing.
    j.boolean("hex_pending", (s.mppt_flags & kMpptHexPending) != 0);
    j.boolean("synthetic", synthetic);
    offer(ni, Domain::Solar, token, j.finish(), now_ms, sink);
  }

  // --- battery, spec 7.2.3 and 7.2.7 ---
  {
    // Spec 16.4 - `bms_age_s` above the threshold marks BMS entities unavailable. So does
    // no successful read at all (bit 0), and so does the sentinel, which is an age too
    // large to report.
    const bool ok = (s.bms_flags & kBmsValid) != 0 && s.bms_age_s != lran::kU16NotAvailable &&
                    s.bms_age_s <= levers_.bms_stale_s;
    Held& held = held_[ni];
    if (!ok) held.cells_valid = false;

    JsonObject j(doc_, sizeof(doc_));
    j.boolean("available", ok);
    char key[16];
    if (ok) {
      if (s.bms_soc == lran::kSocNotAvailable) j.null("soc"); else j.u32("soc", s.bms_soc);
      j.str("soc_source", soc_source_name(s.bms_flags));
      j.u32("pack_mv", s.pack_mv);
      // TODO(W6): pack_ma's sign convention is unconfirmed, and it is published as the node
      // sends it. The library's own TODO(W6) says what capture settles it.
      j.i32("pack_ma", s.pack_ma);
      j.u32("cell_count", s.cell_count);

      // R-5.2a - a cell voltage moves in the document only when it has moved at least
      // cell_mv_deadband from the value last published. 1-2 mV of ADC jitter a poll
      // (spec 7.2.3) then never reaches HA's history. 0 publishes every change.
      const uint8_t cells = s.cell_count < lran::schema::kMaxCells ? s.cell_count
                                                                   : lran::schema::kMaxCells;
      for (uint8_t c = 0; c < lran::schema::kMaxCells; ++c) {
        const uint16_t v = s.cell_mv[c];
        uint16_t&      p = held.cell_mv[c];
        const int32_t  d = static_cast<int32_t>(v) - static_cast<int32_t>(p);
        if (!held.cells_valid || d >= levers_.cell_mv_deadband ||
            -d >= levers_.cell_mv_deadband) {
          p = v;
        }
        std::snprintf(key, sizeof(key), "cell%u_mv", static_cast<unsigned>(c + 1));
        if (c < cells) j.u32(key, p); else j.null(key);
      }
      held.cells_valid = true;
      for (uint8_t c = 0; c < lran::schema::kMaxCells; ++c) {
        std::snprintf(key, sizeof(key), "cell%u_temp_c", static_cast<unsigned>(c + 1));
        if (c < cells) j.i32(key, s.cell_temp_c[c]); else j.null(key);
      }
      j.u32("cycles", s.bms_cycles);
      j.decimal("capacity_ah", s.bms_capacity_dah, 1);  // 0.1 Ah on the wire
      j.u32("alarms", s.bms_alarms);
      j.boolean("charge_fet", (s.bms_flags & kBmsChargeFet) != 0);
      j.boolean("discharge_fet", (s.bms_flags & kBmsDischargeFet) != 0);
      j.boolean("charge_inhibited", (s.bms_flags & kBmsChargeInhibited) != 0);
      j.boolean("protection", (s.bms_flags & kBmsProtection) != 0);
      j.boolean("balancing", (s.bms_flags & kBmsBalancing) != 0);
    } else {
      static const char* const kKeys[] = {
          "soc", "soc_source", "pack_mv", "pack_ma", "cell_count", "cell1_mv", "cell2_mv",
          "cell3_mv", "cell4_mv", "cell1_temp_c", "cell2_temp_c", "cell3_temp_c",
          "cell4_temp_c", "cycles", "capacity_ah", "alarms", "charge_fet", "discharge_fet",
          "charge_inhibited", "protection", "balancing"};
      for (const char* k : kKeys) j.null(k);
    }
    // The link and its age stay readable while the block is stale, because they say why.
    // spec 7.2.3 - 0 is no link; otherwise the magnitude of a negative RSSI.
    if (s.bms_rssi_neg == 0) j.null("ble_rssi_dbm"); else j.i32("ble_rssi_dbm", -s.bms_rssi_neg);
    if (s.bms_age_s == lran::kU16NotAvailable) j.null("age_s"); else j.u32("age_s", s.bms_age_s);
    j.boolean("synthetic", synthetic);
    offer(ni, Domain::Battery, token, j.finish(), now_ms, sink);
  }

  // --- node, spec 7.2.4 and 7.2.8 ---
  {
    JsonObject j(doc_, sizeof(doc_));
    j.u32("uptime_s", s.uptime_s);
    if (s.boot_count == 0) j.null("boot_count"); else j.u32("boot_count", s.boot_count);
    j.u32("node_mv", s.node_mv);
    j.i32("node_ma", s.node_ma);
    tenths_or_null(j, "enclosure_temp_c", s.enclosure_temp_c10);
    j.boolean("config_persisted", (s.node_flags & kNodeConfigPersisted) != 0);
    j.boolean("sd_ok", (s.node_flags & kNodeSdOk) != 0);
    j.boolean("dry_run", (s.node_flags & kNodeDryRun) != 0);
    j.boolean("bms_ble", (s.node_flags & kNodeBmsBle) != 0);
    j.boolean("debug", (s.node_flags & kNodeDebug) != 0);
    j.boolean("shutdown_latch", (s.node_flags & kNodeShutdownLatch) != 0);
    str_or_null(j, "reason", status_reason_name(s.status_reason));
    j.boolean("synthetic", synthetic);
    offer(ni, Domain::Node, token, j.finish(), now_ms, sink);
  }
}

}  // namespace bridge
