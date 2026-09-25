// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "lran/schema/node_config_v1.h"

#include "lran/bytes.h"

namespace lran {
namespace schema {

size_t param_value_len(PType t) {
  switch (t) {
    case PType::U8:   return 1;
    case PType::U16:  return 2;
    case PType::U32:  return 4;
    case PType::I16:  return 2;
    case PType::I32:  return 4;
    case PType::Bool: return 1;
  }
  return 0;  // unknown ptype - the caller reports TYPE_MISMATCH (spec 8.12)
}

namespace {
void pack_value(uint8_t* dst, size_t n, uint32_t raw) {
  for (size_t i = 0; i < n; ++i) dst[i] = static_cast<uint8_t>((raw >> (8 * i)) & 0xFF);
}
}  // namespace

bool entry_pack(ConfigEntry* e, uint16_t param_id, PType t, uint32_t raw) {
  const size_t n = param_value_len(t);
  if (n == 0 || n > kMaxParamValueLen) return false;
  e->param_id = param_id;
  e->ptype    = t;
  e->len      = static_cast<uint8_t>(n);
  for (size_t i = 0; i < kMaxParamValueLen; ++i) e->value[i] = 0;
  pack_value(e->value, n, raw);
  return true;
}

bool entry_pack(ConfigAckEntry* e, uint16_t param_id, ParamStatus s, PType t,
                uint32_t raw) {
  const size_t n = param_value_len(t);
  if (n == 0 || n > kMaxParamValueLen) return false;
  e->param_id = param_id;
  e->status   = s;
  e->is_override = false;
  e->ptype    = t;
  e->len      = static_cast<uint8_t>(n);
  for (size_t i = 0; i < kMaxParamValueLen; ++i) e->value[i] = 0;
  pack_value(e->value, n, raw);
  return true;
}

uint32_t entry_raw(const uint8_t* value, uint8_t len) {
  uint32_t v = 0;
  for (uint8_t i = 0; i < len && i < kMaxParamValueLen; ++i) {
    v |= static_cast<uint32_t>(value[i]) << (8 * i);
  }
  return v;
}

// spec 4.5 - two's complement. Sign-extends from the declared width so an i16 of
// -1 arrives as -1 rather than 65535.
int32_t entry_signed(const uint8_t* value, uint8_t len, PType t) {
  const uint32_t raw = entry_raw(value, len);
  if (t == PType::I16) return static_cast<int32_t>(static_cast<int16_t>(raw));
  return static_cast<int32_t>(raw);
}

Status serialize(const NodeConfigV1& v, uint8_t* out, size_t cap, size_t* written) {
  if (v.count > kMaxConfigEntries) return Status::BadLength;
  ByteWriter w(out, cap);
  w.u8(static_cast<uint8_t>(v.op));  // spec 7.4 off 0
  w.u8(v.count);                     // off 1
  for (uint8_t i = 0; i < v.count; ++i) {
    const ConfigEntry& e = v.entries[i];
    // This build stores at most one 4-byte unit, so it cannot PRODUCE a wider value even
    // though it reads past one (spec 7.4, D55). A caller asking for one is a bug here.
    if (e.len > kMaxParamValueLen) return Status::BadLength;
    w.u16(e.param_id);                     // entry off 0
    w.u8(static_cast<uint8_t>(e.ptype));   // entry off 2
    w.u8(e.len);                           // entry off 3
    w.bytes(e.value, e.len);               // entry off 4
  }
  if (!w.ok()) return Status::BufferTooSmall;
  if (w.written() > kMaxSchemaPayload) return Status::BadLength;
  if (written != nullptr) *written = w.written();
  return Status::Ok;
}

Status deserialize(const uint8_t* in, size_t len, NodeConfigV1* out) {
  if (len < kConfigHdrLen) return Status::BadLength;
  ByteReader r(in, len);
  out->op    = static_cast<ConfigOp>(r.u8());
  out->count = r.u8();
  if (out->count > kMaxConfigEntries) return Status::BadLength;
  for (uint8_t i = 0; i < out->count; ++i) {
    ConfigEntry& e = out->entries[i];
    e.param_id = r.u16();
    e.ptype    = static_cast<PType>(r.u8());
    e.len      = r.u8();
    if (!r.ok()) return Status::BadLength;
    for (size_t j = 0; j < kMaxParamValueLen; ++j) e.value[j] = 0;
    if (e.len > kMaxParamValueLen) {
      // spec 7.4 (D55, D51) - a value this build cannot store costs THE ENTRY, not the
      // frame. `len` delimits it, so the bytes are skipped and the rest of the set still
      // parses; the caller answers TYPE_MISMATCH for this param_id. That is what lets a
      // node built before a wider type or an array parameter existed read a set that uses
      // one. No value is stored, so the caller never acts on one it only partly read.
      r.skip(e.len);
    } else {
      r.bytes(e.value, e.len);
    }
  }
  return (r.ok() && r.read() == len) ? Status::Ok : Status::BadLength;
}

Status serialize(const NodeConfigAckV1& v, uint8_t* out, size_t cap,
                 size_t* written) {
  if (v.count > kMaxConfigAckEntries) return Status::BadLength;
  ByteWriter w(out, cap);
  w.u8(static_cast<uint8_t>(v.op));              // spec 7.4 off 0
  w.u8(static_cast<uint8_t>(v.persist_status));  // off 1
  // spec 7.4.1, D57 - bit 7 is MORE_FOLLOWS, bits 6:0 the result count. The count is
  // already bounded at 32 above, so the marker never collides with a value.
  w.u8(static_cast<uint8_t>(v.count | (v.more_follows ? kConfigAckMoreFollows : 0)));
  for (uint8_t i = 0; i < v.count; ++i) {
    const ConfigAckEntry& e = v.entries[i];
    if (e.len > kMaxParamValueLen) return Status::BadLength;
    w.u16(e.param_id);                     // entry off 0
    // entry off 2 - spec 7.4, D68: 8.12's value in bits 6:0, OVERRIDE in bit 7
    w.u8(static_cast<uint8_t>((static_cast<uint8_t>(e.status) & ~kConfigStatusOverride) |
                              (e.is_override ? kConfigStatusOverride : 0)));
    w.u8(static_cast<uint8_t>(e.ptype));   // entry off 3
    w.u8(e.len);                           // entry off 4
    w.bytes(e.value, e.len);               // entry off 5
  }
  if (!w.ok()) return Status::BufferTooSmall;
  if (w.written() > kMaxSchemaPayload) return Status::BadLength;
  if (written != nullptr) *written = w.written();
  return Status::Ok;
}

Status deserialize(const uint8_t* in, size_t len, NodeConfigAckV1* out) {
  if (len < kConfigAckHdrLen) return Status::BadLength;
  ByteReader r(in, len);
  out->op             = static_cast<ConfigOp>(r.u8());
  out->persist_status = static_cast<PersistStatus>(r.u8());
  const uint8_t count_byte = r.u8();  // spec 7.4.1, D57
  out->more_follows        = (count_byte & kConfigAckMoreFollows) != 0;
  out->count               = static_cast<uint8_t>(count_byte & ~kConfigAckMoreFollows);
  if (out->count > kMaxConfigAckEntries) return Status::BadLength;
  for (uint8_t i = 0; i < out->count; ++i) {
    ConfigAckEntry& e = out->entries[i];
    e.param_id = r.u16();
    const uint8_t status_byte = r.u8();  // spec 7.4, D68
    e.status      = static_cast<ParamStatus>(status_byte & ~kConfigStatusOverride);
    e.is_override = (status_byte & kConfigStatusOverride) != 0;
    e.ptype    = static_cast<PType>(r.u8());
    e.len      = r.u8();
    if (!r.ok()) return Status::BadLength;
    for (size_t j = 0; j < kMaxParamValueLen; ++j) e.value[j] = 0;
    // The same rule as the CONFIG side: a result this build cannot store costs the
    // result, not the frame (spec 7.4, D55).
    if (e.len > kMaxParamValueLen) {
      r.skip(e.len);
    } else {
      r.bytes(e.value, e.len);
    }
  }
  return (r.ok() && r.read() == len) ? Status::Ok : Status::BadLength;
}

}  // namespace schema
}  // namespace lran
