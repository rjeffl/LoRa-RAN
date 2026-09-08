// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Schema 0x12 - GateLink config v1, variable. Spec 7.4.
//
// Carried by both CONFIG and CONFIG_ACK. param_id values, types, ranges and defaults
// are declared once in /lib/lran-config/ and are deliberately NOT enumerated here -
// three hand-maintained copies would drift.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config.h"
#include "lran/types.h"

namespace lran {
namespace schema {

// spec 7.4 - the widest ptype is u32/i32, so four value bytes covers every entry.
inline constexpr size_t kMaxParamValueLen = 4;

// Entry sizes on the wire: a CONFIG entry is 4 + len, a CONFIG_ACK result is 5 + len.
inline constexpr size_t kConfigEntryHdrLen    = 4;  // param_id:2, ptype, len
inline constexpr size_t kConfigAckEntryHdrLen = 5;  // param_id:2, status, ptype, len
inline constexpr size_t kConfigHdrLen         = 2;  // op, count
inline constexpr size_t kConfigAckHdrLen      = 3;  // op, persist_status, count

// Derived from kMaxSchemaPayload rather than chosen, so the caps move with the frame
// size and cannot drift out of agreement with it. Worst case is the smallest entry,
// a one-byte value.
inline constexpr size_t kMaxConfigEntries =
    (kMaxSchemaPayload - kConfigHdrLen) / (kConfigEntryHdrLen + 1);  // 38
inline constexpr size_t kMaxConfigAckEntries =
    (kMaxSchemaPayload - kConfigAckHdrLen) / (kConfigAckEntryHdrLen + 1);  // 32

struct ConfigEntry {
  uint16_t param_id = 0;
  PType    ptype    = PType::U8;
  uint8_t  len      = 0;  // value length in bytes, <= kMaxParamValueLen
  uint8_t  value[kMaxParamValueLen] = {0, 0, 0, 0};  // little-endian
};

// spec 7.4 - the ACK carries the EFFECTIVE value, not the requested one. An
// out-of-range value is clamped to the documented range and the clamp is reported
// (status = CLAMPED) rather than applied quietly. Unknown keys are rejected
// individually with a reason, never silently ignored, and the rest of the set
// still applies.
struct ConfigAckEntry {
  uint16_t    param_id = 0;
  ParamStatus status   = ParamStatus::Ok;  // spec 8.12
  PType       ptype    = PType::U8;
  uint8_t     len      = 0;
  uint8_t     value[kMaxParamValueLen] = {0, 0, 0, 0};
};

struct GateLinkConfigV1 {
  ConfigOp op    = ConfigOp::Get;  // spec 8.10
  uint8_t  count = 0;
  ConfigEntry entries[kMaxConfigEntries] = {};
};

struct GateLinkConfigAckV1 {
  ConfigOp      op = ConfigOp::Get;
  // spec 7.4 - honest. A node with no usable microSD still applies and still ACKs
  // the change, with APPLIED_NOT_PERSISTED. HA must never be told a value was saved
  // when it was not.
  PersistStatus persist_status = PersistStatus::Persisted;  // spec 8.11
  uint8_t       count = 0;
  ConfigAckEntry entries[kMaxConfigAckEntries] = {};
};

// Value-width helpers. The wire carries `len` bytes little-endian; these pack and
// unpack that against the declared ptype.
size_t   param_value_len(PType t);
bool     entry_pack(ConfigEntry* e, uint16_t param_id, PType t, uint32_t raw);
bool     entry_pack(ConfigAckEntry* e, uint16_t param_id, ParamStatus s, PType t,
                    uint32_t raw);
uint32_t entry_raw(const uint8_t* value, uint8_t len);
int32_t  entry_signed(const uint8_t* value, uint8_t len, PType t);

Status serialize(const GateLinkConfigV1& v, uint8_t* out, size_t cap, size_t* written);
Status deserialize(const uint8_t* in, size_t len, GateLinkConfigV1* out);
Status serialize(const GateLinkConfigAckV1& v, uint8_t* out, size_t cap,
                 size_t* written);
Status deserialize(const uint8_t* in, size_t len, GateLinkConfigAckV1* out);

}  // namespace schema
}  // namespace lran
