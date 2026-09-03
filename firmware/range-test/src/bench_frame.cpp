// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "bench_frame.h"

namespace rangetest {
namespace {

// Repo rule 1. Explicit, little-endian, one byte at a time.
void put_u16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

uint16_t get_u16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

void put_i16(uint8_t* p, int16_t v) { put_u16(p, static_cast<uint16_t>(v)); }
int16_t get_i16(const uint8_t* p) { return static_cast<int16_t>(get_u16(p)); }

uint8_t filler_byte(uint16_t seq, size_t i) {
  return static_cast<uint8_t>((seq & 0xFF) + i);
}

}  // namespace

size_t bench_serialize(const BenchFrame& f, uint8_t* out, size_t cap,
                       size_t total_len) {
  if (out == nullptr) return 0;
  if (total_len < kBenchHeaderLen || total_len > cap) return 0;

  put_u16(out + 0, kBenchMagic);
  out[2] = kBenchVersion;
  out[3] = static_cast<uint8_t>(f.kind);
  put_u16(out + 4, f.position_id);
  put_u16(out + 6, f.tp_index);
  put_u16(out + 8, f.probe_seq);
  put_i16(out + 10, f.resp_rssi_dbm10);
  put_i16(out + 12, f.resp_snr_db10);
  put_u16(out + 14, f.resp_heard);

  for (size_t i = kBenchHeaderLen; i < total_len; ++i) {
    out[i] = filler_byte(f.probe_seq, i - kBenchHeaderLen);
  }
  return total_len;
}

bool bench_parse(const uint8_t* in, size_t len, BenchFrame* out) {
  if (in == nullptr || out == nullptr) return false;
  if (len < kBenchHeaderLen) return false;
  if (get_u16(in) != kBenchMagic) return false;
  if (in[2] != kBenchVersion) return false;

  const uint8_t kind = in[3];
  if (kind != static_cast<uint8_t>(BenchKind::Probe) &&
      kind != static_cast<uint8_t>(BenchKind::Echo) &&
      kind != static_cast<uint8_t>(BenchKind::WarmupProbe) &&
      kind != static_cast<uint8_t>(BenchKind::ArmedBeacon)) {
    return false;
  }

  out->kind            = static_cast<BenchKind>(kind);
  out->position_id     = get_u16(in + 4);
  out->tp_index        = get_u16(in + 6);
  out->probe_seq       = get_u16(in + 8);
  out->resp_rssi_dbm10 = get_i16(in + 10);
  out->resp_snr_db10   = get_i16(in + 12);
  out->resp_heard      = get_u16(in + 14);
  return true;
}

bool bench_check_filler(const uint8_t* in, size_t len, uint16_t probe_seq,
                        size_t* first_bad) {
  if (in == nullptr || len < kBenchHeaderLen) return false;
  for (size_t i = kBenchHeaderLen; i < len; ++i) {
    if (in[i] != filler_byte(probe_seq, i - kBenchHeaderLen)) {
      if (first_bad != nullptr) *first_bad = i;
      return false;
    }
  }
  return true;
}

}  // namespace rangetest
