// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "lran/schema/gatelink_status_v1.h"

#include "lran/bytes.h"

namespace lran {
namespace schema {

// The write order below IS the spec 7.2 offset table. It is verified field by field
// against that table by the schema tests, not merely round-tripped: a symmetric
// encoder and decoder agree perfectly on a wrong offset.
Status serialize(const GateLinkStatusV1& v, uint8_t* out, size_t cap, size_t* written) {
  if (cap < kGateLinkStatusV1Len) return Status::BufferTooSmall;
  ByteWriter w(out, cap);

  // Gate block, spec 7.2.1
  w.u8(v.gate_state);              // 0
  w.u8(v.input_bits);              // 1
  w.u8(v.hold);                    // 2
  w.u8(v.movement_cause);          // 3
  w.u8(v.last_direction);          // 4
  w.u8(v.detect_flags);            // 5
  w.u32(v.last_traversal_age_s);   // 6

  // MPPT block, spec 7.2.2
  w.u16(v.batt_mv);                // 10
  w.i16(v.batt_ma);                // 12
  w.u16(v.pv_cv);                  // 14
  w.u16(v.pv_w);                   // 16
  w.i16(v.load_ma);                // 18
  w.u16(v.yield_today);            // 20
  w.u16(v.yield_yest);             // 22
  w.u16(v.pmax_today);             // 24
  w.u32(v.yield_total);            // 26
  w.u8(v.charge_state);            // 30
  w.u8(v.mppt_err);                // 31
  w.u8(v.mppt_tracker);            // 32
  w.u8(v.mppt_flags);              // 33
  w.i16(v.mppt_temp_c10);          // 34

  // BMS block, spec 7.2.3
  w.u8(v.bms_soc);                 // 36
  w.u8(v.bms_flags);               // 37
  w.u16(v.pack_mv);                // 38
  w.i16(v.pack_ma);                // 40
  w.u8(v.cell_count);              // 42
  w.u8(v.bms_rssi_neg);            // 43
  w.u16(v.cell_mv[0]);             // 44
  w.u16(v.cell_mv[1]);             // 46
  w.u16(v.cell_mv[2]);             // 48
  w.u16(v.cell_mv[3]);             // 50
  w.i8(v.cell_temp_c[0]);          // 52
  w.i8(v.cell_temp_c[1]);          // 53
  w.i8(v.cell_temp_c[2]);          // 54
  w.i8(v.cell_temp_c[3]);          // 55
  w.u16(v.bms_cycles);             // 56
  w.u16(v.bms_capacity_dah);       // 58
  w.u16(v.bms_alarms);             // 60
  w.u16(v.bms_age_s);              // 62

  // Node block, spec 7.2.4
  w.u32(v.uptime_s);               // 64
  w.u16(v.boot_count);             // 68
  w.u16(v.node_mv);                // 70
  w.i16(v.node_ma);                // 72
  w.i16(v.enclosure_temp_c10);     // 74
  w.u8(v.node_flags);              // 76
  w.u8(v.status_reason);           // 77

  if (!w.ok() || w.written() != kGateLinkStatusV1Len) return Status::BufferTooSmall;
  if (written != nullptr) *written = w.written();
  return Status::Ok;
}

Status deserialize(const uint8_t* in, size_t len, GateLinkStatusV1* out) {
  if (len != kGateLinkStatusV1Len) return Status::BadLength;
  ByteReader r(in, len);

  out->gate_state            = r.u8();
  out->input_bits            = r.u8();
  out->hold                  = r.u8();
  out->movement_cause        = r.u8();
  out->last_direction        = r.u8();
  out->detect_flags          = r.u8();
  out->last_traversal_age_s  = r.u32();

  out->batt_mv               = r.u16();
  out->batt_ma               = r.i16();
  out->pv_cv                 = r.u16();
  out->pv_w                  = r.u16();
  out->load_ma               = r.i16();
  out->yield_today           = r.u16();
  out->yield_yest            = r.u16();
  out->pmax_today            = r.u16();
  out->yield_total           = r.u32();
  out->charge_state          = r.u8();
  out->mppt_err              = r.u8();
  out->mppt_tracker          = r.u8();
  out->mppt_flags            = r.u8();
  out->mppt_temp_c10         = r.i16();

  out->bms_soc               = r.u8();
  out->bms_flags             = r.u8();
  out->pack_mv               = r.u16();
  out->pack_ma               = r.i16();
  out->cell_count            = r.u8();
  out->bms_rssi_neg          = r.u8();
  for (uint8_t i = 0; i < kMaxCells; ++i) out->cell_mv[i] = r.u16();
  for (uint8_t i = 0; i < kMaxCells; ++i) out->cell_temp_c[i] = r.i8();
  out->bms_cycles            = r.u16();
  out->bms_capacity_dah      = r.u16();
  out->bms_alarms            = r.u16();
  out->bms_age_s             = r.u16();

  out->uptime_s              = r.u32();
  out->boot_count            = r.u16();
  out->node_mv               = r.u16();
  out->node_ma               = r.i16();
  out->enclosure_temp_c10    = r.i16();
  out->node_flags            = r.u8();
  out->status_reason         = r.u8();

  return (r.ok() && r.read() == kGateLinkStatusV1Len) ? Status::Ok : Status::BadLength;
}

}  // namespace schema
}  // namespace lran
