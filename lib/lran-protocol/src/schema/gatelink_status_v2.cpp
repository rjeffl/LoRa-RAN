// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "lran/schema/gatelink_status_v2.h"

#include "lran/bytes.h"

namespace lran {
namespace schema {

Status serialize(const GateLinkStatusV2& v, uint8_t* out, size_t cap, size_t* written) {
  if (cap < kGateLinkStatusV2Len) return Status::BufferTooSmall;
  size_t head = 0;
  const Status s = serialize(v.v1, out, cap, &head);
  if (s != Status::Ok) return s;

  ByteWriter w(out + head, cap - head);
  w.u16(v.ah_drawn_dah);  // 78, spec 7.2.10
  if (!w.ok() || head + w.written() != kGateLinkStatusV2Len) return Status::BufferTooSmall;
  if (written != nullptr) *written = kGateLinkStatusV2Len;
  return Status::Ok;
}

Status deserialize(const uint8_t* in, size_t len, GateLinkStatusV2* out) {
  // spec 7.2.10 - the schema ID says which layout a frame carries, never the length,
  // so 78 bytes under 0x13 is refused rather than read as a v1.
  if (len != kGateLinkStatusV2Len) return Status::BadLength;
  const Status s = deserialize(in, kGateLinkStatusV1Len, &out->v1);
  if (s != Status::Ok) return s;

  ByteReader r(in + kGateLinkStatusV1Len, len - kGateLinkStatusV1Len);
  out->ah_drawn_dah = r.u16();  // 78
  return (r.ok() && r.read() == len - kGateLinkStatusV1Len) ? Status::Ok : Status::BadLength;
}

}  // namespace schema
}  // namespace lran
