// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "lran/schema/gatelink_event_v1.h"

#include "lran/bytes.h"

namespace lran {
namespace schema {

Status serialize(const GateLinkEventV1& v, uint8_t* out, size_t cap, size_t* written) {
  if (cap < kGateLinkEventV1Len) return Status::BufferTooSmall;
  ByteWriter w(out, cap);
  w.u8(v.event_type);   // spec 7.3 off 0
  w.u8(v.event_flags);  // 1
  w.u8(v.hold_source);  // 2
  w.u8(v.direction);    // 3
  w.u8(v.gate_state);   // 4
  w.u8(v.input_bits);   // 5
  w.u16(v.detail);      // 6
  w.u32(v.event_id);    // 8
  w.u32(v.uptime_s);    // 12
  if (!w.ok() || w.written() != kGateLinkEventV1Len) return Status::BufferTooSmall;
  if (written != nullptr) *written = w.written();
  return Status::Ok;
}

Status deserialize(const uint8_t* in, size_t len, GateLinkEventV1* out) {
  if (len != kGateLinkEventV1Len) return Status::BadLength;
  ByteReader r(in, len);
  out->event_type  = r.u8();
  out->event_flags = r.u8();
  out->hold_source = r.u8();
  out->direction   = r.u8();
  out->gate_state  = r.u8();
  out->input_bits  = r.u8();
  out->detail      = r.u16();
  out->event_id    = r.u32();
  out->uptime_s    = r.u32();
  return (r.ok() && r.read() == kGateLinkEventV1Len) ? Status::Ok : Status::BadLength;
}

}  // namespace schema
}  // namespace lran
