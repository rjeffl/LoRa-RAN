// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL5; see charge_inhibit.h.

#include "charge_inhibit.h"

#include "lran/types.h"

namespace gatelink {
namespace {

// Victron's CS codes for the two states in which the charger is trying to charge (R-6.4b).
constexpr uint8_t kCsBulk       = 3;
constexpr uint8_t kCsAbsorption = 4;

// A BMS read older than this many polls no longer decides. One missed window is common
// (BmsEnd counts them); three in a row means BLE is unavailable, which is R-6.4b's case.
constexpr uint32_t kBmsFreshPolls = 3;

bool reached(uint32_t now, uint32_t since, uint32_t span) { return now - since >= span; }

// spec 8.9, D81 - the event's `detail` names the path that decided it (PRD R-6.4e).
lran::ChargeInhibitPath wire_path(ChargeSource s) {
  switch (s) {
    case ChargeSource::Bms:      return lran::ChargeInhibitPath::Bms;
    case ChargeSource::VeDirect: return lran::ChargeInhibitPath::VeDirect;
    case ChargeSource::None:     break;
  }
  return lran::ChargeInhibitPath::NotStated;
}

}  // namespace

const char* charge_source_name(ChargeSource s) {
  switch (s) {
    case ChargeSource::None:     return "none";
    case ChargeSource::Bms:      return "bms";
    case ChargeSource::VeDirect: return "vedirect";
  }
  return "?";
}

bool ChargeInhibit::ved_update(const MpptSnapshot& m, uint32_t now_ms, const ChargeParams& p) {
  const int32_t ma = m.batt_ma;
  bool met = (m.mppt_flags & kMpptFlagStale) == 0 &&
             m.pv_w != lran::kU16NotAvailable && m.pv_w > 0 &&
             (m.charge_state == kCsBulk || m.charge_state == kCsAbsorption) &&
             m.batt_ma != lran::kI16NotAvailable &&
             static_cast<uint32_t>(ma < 0 ? -ma : ma) <= p.band_ma &&
             m.batt_mv != lran::kU16NotAvailable;
  // A rise past rise_mv since the condition began means the battery is taking charge.
  if (met && ved_met_ && m.batt_mv > ved_ref_mv_ + p.rise_mv) met = false;

  if (met != ved_met_) {
    ved_met_      = met;
    ved_since_ms_ = now_ms;
    ved_ref_mv_   = m.batt_mv;
  } else if (met && m.batt_mv < ved_ref_mv_) {
    ved_ref_mv_ = m.batt_mv;
  }
  if (ved_met_ != ved_inferred_ && reached(now_ms, ved_since_ms_, p.confirm_ms)) {
    ved_inferred_ = ved_met_;
  }
  return ved_inferred_;
}

bool ChargeInhibit::update(const MpptSnapshot& m, const BmsSnapshot& b, uint32_t now_ms,
                           const ChargeParams& p, const GateContext& g, EventIds* ids,
                           PushList* out) {
  // Run every pass, so the inference is warm when the BMS goes quiet.
  const bool ved = ved_update(m, now_ms, p);

  const bool bms_fresh = b.have && b.data.temp_count > 0 &&
                         !reached(now_ms, b.read_ms, kBmsFreshPolls * p.bms_poll_s * 1000 + 1);
  bool next = false;
  if (bms_fresh) {
    path_ = ChargeSource::Bms;
    int16_t coldest = b.data.temp_dc[0];
    for (uint8_t i = 1; i < b.data.temp_count && i < bms::kMaxTemps; ++i) {
      if (b.data.temp_dc[i] < coldest) coldest = b.data.temp_dc[i];
    }
    // The pack reads in 0.1 A, so band_ma below 100 asks for an exact zero.
    next = coldest <= static_cast<int32_t>(p.temp_c) * 10 &&
           b.data.current_ma <= static_cast<int32_t>(p.band_ma);
  } else if ((m.mppt_flags & kMpptFlagStale) == 0) {
    path_ = ChargeSource::VeDirect;
    next  = ved;
  } else {
    path_ = ChargeSource::None;
  }

  if (next == inhibited_) return false;
  inhibited_ = next;
  source_    = next ? path_ : ChargeSource::None;

  // spec 8.7 - the STATUS carries bit 3 itself, so it is sent for both edges.
  Push st;
  st.reason = lran::StatusReason::ChargeInhibited;
  out->add(st);
  if (next) {
    Push ev;
    ev.kind              = Push::Kind::Event;
    ev.event.event_type  = static_cast<uint8_t>(lran::EventType::ChargeInhibited);
    ev.event.hold_source = static_cast<uint8_t>(g.hold_source);
    ev.event.gate_state  = static_cast<uint8_t>(g.gate_state);
    ev.event.input_bits  = g.input_bits;
    ev.event.uptime_s    = g.uptime_s;
    ev.event.detail      = static_cast<uint16_t>(wire_path(source_));
    ev.event.event_id    = ids->next();
    out->add(ev);
  }
  return true;
}

}  // namespace gatelink
