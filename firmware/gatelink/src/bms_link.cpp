// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL5; see bms_link.h.

#include "bms_link.h"

#include <climits>

#include "lran/config.h"
#include "lran/types.h"

namespace gatelink {
namespace {

// spec 7.2.7
constexpr uint8_t kBmsFlagValid         = 0x01;
constexpr uint8_t kBmsSocSourceBle      = 0x00;  // bits 7:6 = 0, bms_ble
constexpr uint8_t kBmsSocSourceUnknown  = 0xC0;  // bits 7:6 = 3

// 0.1 C to whole degrees, rounded half away from zero.
int8_t whole_c(int16_t dc) {
  const int32_t c = dc >= 0 ? (dc + 5) / 10 : (dc - 5) / 10;
  if (c > INT8_MAX) return INT8_MAX;
  if (c <= INT8_MIN) return INT8_MIN + 1;  // INT8_MIN is the sentinel
  return static_cast<int8_t>(c);
}

}  // namespace

void fill_bms_block(const BmsSnapshot& b, uint32_t now_ms, lran::schema::GateLinkStatusV1* s) {
  namespace sch = lran::schema;
  s->bms_alarms = lran::kU16NotAvailable;
  if (!b.have) {
    s->bms_soc          = lran::kSocNotAvailable;
    s->bms_flags        = kBmsSocSourceUnknown;
    s->pack_mv          = lran::kU16NotAvailable;
    s->pack_ma          = lran::kI16NotAvailable;
    s->cell_count       = 0;
    s->bms_rssi_neg     = 0;
    s->bms_cycles       = lran::kU16NotAvailable;
    s->bms_capacity_dah = lran::kU16NotAvailable;
    s->bms_age_s        = lran::kU16NotAvailable;
    for (uint8_t i = 0; i < sch::kMaxCells; ++i) {
      s->cell_mv[i]     = lran::kU16NotAvailable;
      s->cell_temp_c[i] = INT8_MIN;
    }
    return;
  }

  const bms::BmsData& d = b.data;
  s->bms_soc   = d.soc_pct <= 100 ? d.soc_pct : lran::kSocNotAvailable;
  s->bms_flags = kBmsFlagValid | kBmsSocSourceBle;
  s->pack_mv   = d.pack_mv < lran::kU16NotAvailable ? static_cast<uint16_t>(d.pack_mv)
                                                    : lran::kU16NotAvailable - 1;
  // spec 7.2.3 - int16 mA holds 32.7 A. Clamp rather than wrap; INT16_MIN stays the
  // sentinel.
  int32_t ma = d.current_ma;
  if (ma > INT16_MAX) ma = INT16_MAX;
  if (ma <= INT16_MIN) ma = INT16_MIN + 1;
  s->pack_ma = static_cast<int16_t>(ma);

  // spec 7.2.3 - cell_count is what the pack reported, even past the four this schema
  // carries, so a wrong pack shows as a count rather than as missing cells.
  s->cell_count = d.cell_count;
  const int16_t rssi = b.rssi_dbm;
  s->bms_rssi_neg = rssi < 0 && rssi >= -255 ? static_cast<uint8_t>(-rssi) : 0;
  for (uint8_t i = 0; i < sch::kMaxCells; ++i) {
    s->cell_mv[i]     = i < d.cell_count ? d.cell_mv[i] : lran::kU16NotAvailable;
    s->cell_temp_c[i] = i < d.temp_count ? whole_c(d.temp_dc[i]) : INT8_MIN;
  }
  s->bms_cycles       = d.cycles;
  s->bms_capacity_dah = d.nominal_dAh;

  const uint32_t age_s = (now_ms - b.read_ms) / 1000;
  s->bms_age_s = age_s < lran::kU16NotAvailable ? static_cast<uint16_t>(age_s)
                                                : lran::kU16NotAvailable;
}

bool reply_awaited(const uint8_t* frame, size_t len) {
  if (frame == nullptr || len < lran::kHdrLen) return false;
  // spec 3 - type is header byte 1, schema byte 11, and the payload starts at 16.
  switch (static_cast<lran::MsgType>(frame[1])) {
    case lran::MsgType::CommandAck:
    case lran::MsgType::ConfigAck:
    case lran::MsgType::HexRsp:
      return true;
    case lran::MsgType::Status: {
      if (frame[11] != lran::kSchemaGateLinkStatusV1) return true;
      constexpr size_t kReasonAt = lran::kHdrLen + 77;  // spec 7.2.4
      return len > kReasonAt &&
             frame[kReasonAt] == static_cast<uint8_t>(lran::StatusReason::PollResponse);
    }
    default:
      return false;
  }
}

TxGate tx_gate(bool ble_holds, uint32_t window_ms, uint32_t cap_ms, bool awaited) {
  if (!ble_holds) return TxGate::Go;
  if (window_ms >= cap_ms) return TxGate::Overrun;
  return awaited ? TxGate::WaitAbort : TxGate::Wait;
}

const char* bms_end_name(BmsEnd e) {
  switch (e) {
    case BmsEnd::Read:          return "read";
    case BmsEnd::Aborted:       return "aborted";
    case BmsEnd::Cap:           return "cap";
    case BmsEnd::NotFound:      return "not_found";
    case BmsEnd::ConnectFailed: return "connect";
    case BmsEnd::Handshake:     return "handshake";
    case BmsEnd::Subscribe:     return "subscribe";
    case BmsEnd::Request:       return "request";
    case BmsEnd::NoAnswer:      return "no_answer";
    case BmsEnd::InitFailed:    return "init";
    case BmsEnd::Count:         break;
  }
  return "?";
}

}  // namespace gatelink
