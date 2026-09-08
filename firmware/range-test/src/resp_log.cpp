// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "resp_log.h"

namespace rangetest {
namespace {

constexpr uint16_t kBlobMagic   = 0x524C;  // 'RL'
constexpr uint8_t  kBlobVersion = 1;

// Repo rule 1 - explicit little-endian, byte at a time. This blob is read back by
// host tooling built with a different compiler; a struct write would work on two
// ESP32s and break the moment anything else parses it.
void put_u16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
uint16_t get_u16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}
void put_i16(uint8_t* p, int16_t v) { put_u16(p, static_cast<uint16_t>(v)); }
int16_t get_i16(const uint8_t* p) { return static_cast<int16_t>(get_u16(p)); }

void put_i32(uint8_t* p, int32_t v) {
  const uint32_t u = static_cast<uint32_t>(v);
  p[0] = static_cast<uint8_t>(u & 0xFF);
  p[1] = static_cast<uint8_t>((u >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((u >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((u >> 24) & 0xFF);
}
int32_t get_i32(const uint8_t* p) {
  return static_cast<int32_t>(static_cast<uint32_t>(p[0]) |
                              (static_cast<uint32_t>(p[1]) << 8) |
                              (static_cast<uint32_t>(p[2]) << 16) |
                              (static_cast<uint32_t>(p[3]) << 24));
}

}  // namespace

const PositionSummary* PositionLog::at(size_t i) const {
  if (i >= count_) return nullptr;
  return &entries_[i];
}

const PositionSummary* PositionLog::find(uint16_t position) const {
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].used && entries_[i].position_id == position) return &entries_[i];
  }
  return nullptr;
}

void PositionLog::clear() {
  for (size_t i = 0; i < kPositionLogCapacity; ++i) entries_[i] = PositionSummary{};
  count_      = 0;
  next_       = 0;
  overflowed_ = false;
}

PositionSummary* PositionLog::slot_for(uint16_t position) {
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].used && entries_[i].position_id == position) return &entries_[i];
  }

  if (count_ < kPositionLogCapacity) {
    PositionSummary* s = &entries_[count_];
    *s = PositionSummary{};
    s->position_id = position;
    s->used        = true;
    ++count_;
    next_ = count_ % kPositionLogCapacity;
    return s;
  }

  // Full: overwrite the oldest. Recorded, because a dumped log that has silently
  // dropped its first positions is worse than one that says it did.
  PositionSummary* s = &entries_[next_];
  *s = PositionSummary{};
  s->position_id = position;
  s->used        = true;
  next_          = (next_ + 1) % kPositionLogCapacity;
  overflowed_    = true;
  return s;
}

void PositionLog::record_probe(uint16_t position, int16_t rssi_dbm10,
                               int16_t snr_db10) {
  PositionSummary* s = slot_for(position);
  if (s == nullptr) return;
  ++s->probes_heard;
  s->rssi_dbm10.add(rssi_dbm10);
  s->snr_db10.add(snr_db10);
}

void PositionLog::record_echo(uint16_t position) {
  PositionSummary* s = slot_for(position);
  if (s == nullptr) return;
  ++s->echoes_sent;
}

size_t PositionLog::serialize(uint8_t* out, size_t cap) const {
  if (out == nullptr) return 0;
  const size_t need = kBlobHeaderLen + count_ * kBlobEntryLen;
  if (need > cap) return 0;

  put_u16(out, kBlobMagic);
  out[2] = kBlobVersion;
  out[3] = static_cast<uint8_t>(count_);

  size_t off = kBlobHeaderLen;
  for (size_t i = 0; i < count_; ++i) {
    const PositionSummary& e = entries_[i];
    put_u16(out + off + 0,  e.position_id);
    put_u16(out + off + 2,  e.probes_heard);
    put_u16(out + off + 4,  e.echoes_sent);
    put_i32(out + off + 6,  e.rssi_dbm10.sum);
    put_i16(out + off + 10, e.rssi_dbm10.min);
    put_i16(out + off + 12, e.rssi_dbm10.max);
    put_u16(out + off + 14, e.rssi_dbm10.count);
    put_i32(out + off + 16, e.snr_db10.sum);
    put_i16(out + off + 20, e.snr_db10.min);
    put_i16(out + off + 22, e.snr_db10.max);
    // SNR shares RSSI's sample count - every heard probe contributes exactly one of
    // each - so the count is stored once. min and max are NOT derivable from each
    // other and both are stored; an earlier draft kept only min and rebuilt max from
    // it, which silently discarded the reading.
    off += kBlobEntryLen;
  }
  return off;
}

bool PositionLog::deserialize(const uint8_t* in, size_t len) {
  clear();
  if (in == nullptr || len < kBlobHeaderLen) return false;
  if (get_u16(in) != kBlobMagic) return false;
  if (in[2] != kBlobVersion) return false;

  const size_t n = in[3];
  if (n > kPositionLogCapacity) return false;
  if (len < kBlobHeaderLen + n * kBlobEntryLen) return false;

  size_t off = kBlobHeaderLen;
  for (size_t i = 0; i < n; ++i) {
    PositionSummary& e = entries_[i];
    e = PositionSummary{};
    e.used              = true;
    e.position_id       = get_u16(in + off + 0);
    e.probes_heard      = get_u16(in + off + 2);
    e.echoes_sent       = get_u16(in + off + 4);
    e.rssi_dbm10.sum    = get_i32(in + off + 6);
    e.rssi_dbm10.min    = get_i16(in + off + 10);
    e.rssi_dbm10.max    = get_i16(in + off + 12);
    e.rssi_dbm10.count  = get_u16(in + off + 14);
    e.snr_db10.sum      = get_i32(in + off + 16);
    e.snr_db10.min      = get_i16(in + off + 20);
    e.snr_db10.max      = get_i16(in + off + 22);
    e.snr_db10.count    = e.rssi_dbm10.count;   // shared - see serialize()
    off += kBlobEntryLen;
  }
  count_ = n;
  next_  = count_ % kPositionLogCapacity;
  return true;
}

}  // namespace rangetest
