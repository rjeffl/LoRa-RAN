// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Schema 0x12 - node config v1, variable. Spec 7.4. Registered as "GateLink config v1" until
// spec v0.13, which made it any node's without changing a byte (D46).
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

// spec 7.4, D55 - `len` is the total value bytes and a multiple of the ptype's unit
// width, so `len / width` units travel. Every parameter defined today is one unit, and
// four bytes covers the widest (u32/i32). A longer value - an array, or a string as u8
// bytes - is READ past and answered TYPE_MISMATCH per entry rather than discarding the
// frame (D51). Raising this cap is what a string parameter costs, when one exists.
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

// spec 7.4.1, D57 - MORE_FOLLOWS, bit 7 of the CONFIG_ACK `count` byte. The static_assert
// is the whole argument for putting it there: a result count that could reach 128 would
// collide with the marker, and a payload cap raised far enough to allow that has to be
// read against this rule rather than around it.
inline constexpr uint8_t kConfigAckMoreFollows = 0x80;
static_assert(kMaxConfigAckEntries < kConfigAckMoreFollows,
              "spec 7.4.1 - count bit 7 is MORE_FOLLOWS and must stay unreachable");

// spec 7.4.1, D57 - a node sends at most this many messages in one answer, so a bridge
// staging one has a termination condition that does not depend on the node.
inline constexpr uint8_t kMaxConfigAckMessages = 4;

struct ConfigEntry {
  uint16_t param_id = 0;
  PType    ptype    = PType::U8;
  uint8_t  len      = 0;  // total value bytes (D55); this build stores <= kMaxParamValueLen
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

struct NodeConfigV1 {
  ConfigOp op    = ConfigOp::Get;  // spec 8.10
  uint8_t  count = 0;
  ConfigEntry entries[kMaxConfigEntries] = {};
};

struct NodeConfigAckV1 {
  ConfigOp      op = ConfigOp::Get;
  // spec 7.4.1, D57 - bit 7 of the wire `count`. An answer too large for one frame is
  // several CONFIG_ACK messages, every one but the last marked. It rides in `count`
  // because 193 bytes of payload hold at most 32 results, so the top two bits of that
  // byte are unreachable and schema 0x12 keeps every offset it had.
  bool          more_follows = false;
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

Status serialize(const NodeConfigV1& v, uint8_t* out, size_t cap, size_t* written);
Status deserialize(const uint8_t* in, size_t len, NodeConfigV1* out);
Status serialize(const NodeConfigAckV1& v, uint8_t* out, size_t cap,
                 size_t* written);
Status deserialize(const uint8_t* in, size_t len, NodeConfigAckV1* out);

}  // namespace schema
}  // namespace lran
