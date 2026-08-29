// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "lran/schema/node_health_v1.h"

#include "lran/bytes.h"

namespace lran {
namespace schema {

Status serialize(const NodeHealthV1& v, uint8_t* out, size_t cap, size_t* written) {
  if (cap < kNodeHealthV1Len) return Status::BufferTooSmall;
  ByteWriter w(out, cap);
  w.u32(v.uptime_s);       // spec 7.5 off 0
  w.u16(v.boot_count);     // 4
  w.u16(v.rx_frames);      // 6
  w.u16(v.tx_frames);      // 8
  w.u16(v.rx_dropped);     // 10
  w.u16(v.cad_backoffs);   // 12
  w.i16(v.last_rssi_dbm);  // 14
  w.i16(v.last_snr_db10);  // 16
  w.u8(v.proto_ver);       // 18
  w.u8(v.health_flags);    // 19
  if (!w.ok() || w.written() != kNodeHealthV1Len) return Status::BufferTooSmall;
  if (written != nullptr) *written = w.written();
  return Status::Ok;
}

Status deserialize(const uint8_t* in, size_t len, NodeHealthV1* out) {
  if (len != kNodeHealthV1Len) return Status::BadLength;
  ByteReader r(in, len);
  out->uptime_s      = r.u32();
  out->boot_count    = r.u16();
  out->rx_frames     = r.u16();
  out->tx_frames     = r.u16();
  out->rx_dropped    = r.u16();
  out->cad_backoffs  = r.u16();
  out->last_rssi_dbm = r.i16();
  out->last_snr_db10 = r.i16();
  out->proto_ver     = r.u8();
  out->health_flags  = r.u8();
  return (r.ok() && r.read() == kNodeHealthV1Len) ? Status::Ok : Status::BadLength;
}

}  // namespace schema
}  // namespace lran
