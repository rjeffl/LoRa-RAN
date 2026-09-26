// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-33; see phy_blob.h.

#include "lran/config/phy_blob.h"

namespace lran {
namespace config {
namespace {

void put_u16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFFu);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void put_i32(uint8_t* p, int32_t v) {
  const uint32_t u = static_cast<uint32_t>(v);
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(u >> (8 * i));
}

uint16_t get_u16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

int32_t get_i32(const uint8_t* p) {
  uint32_t u = 0;
  for (int i = 0; i < 4; ++i) u |= static_cast<uint32_t>(p[i]) << (8 * i);
  return static_cast<int32_t>(u);
}

}  // namespace

size_t phy_blob_encode(const PhyBlob& b, uint8_t* out, size_t cap) {
  if (out == nullptr || b.n > kPhyGroupSize) return 0;
  const size_t len = 5 + b.n * 6;
  if (cap < len) return 0;
  out[0] = kPhyBlobVersion;
  out[1] = static_cast<uint8_t>((b.trial_open ? 0x01 : 0x00) | (b.ack_owed ? 0x02 : 0x00));
  put_u16(&out[2], b.ack_rows);
  out[4] = static_cast<uint8_t>(b.n);
  for (size_t i = 0; i < b.n; ++i) {
    put_u16(&out[5 + i * 6], b.ids[i]);
    put_i32(&out[7 + i * 6], b.values[i]);
  }
  return len;
}

bool phy_blob_decode(const uint8_t* in, size_t len, PhyBlob* out) {
  if (in == nullptr || out == nullptr || len < 3) return false;
  // Version 1 has no ack_rows, so its entries start two bytes earlier.
  size_t hdr = 0;
  if (in[0] == 1) {
    hdr = 3;
  } else if (in[0] == kPhyBlobVersion && len >= 5) {
    hdr = 5;
  } else {
    return false;
  }
  const size_t n = in[hdr - 1];
  if (n > kPhyGroupSize || len != hdr + n * 6) return false;
  PhyBlob b;
  b.trial_open = (in[1] & 0x01) != 0;
  if (hdr == 5) {
    b.ack_owed = (in[1] & 0x02) != 0;
    b.ack_rows = get_u16(&in[2]);
  }
  b.n = n;
  for (size_t i = 0; i < n; ++i) {
    b.ids[i]    = get_u16(&in[hdr + i * 6]);
    b.values[i] = get_i32(&in[hdr + 2 + i * 6]);
  }
  *out = b;
  return true;
}

}  // namespace config
}  // namespace lran
