// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Payloads of the schema-free message types. Spec 6.2-6.6, 7.6.
//
// Field-by-field serialization only (spec 4.2). No struct is ever memcpy'd to or
// from the wire: /tools/ builds host-side decoders with a different compiler on a
// different architecture, and layout-dependent code works on two ESP32s right up
// until the bench tooling is written.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config.h"
#include "lran/types.h"

namespace lran {
namespace msg {

// spec 6.2 - COMMAND, 4 bytes, always followed by an 8-byte MAC.
struct Command {
  uint8_t  cmd  = 0;  // spec 8.1
  uint8_t  arg  = 0;  // command-specific; 0 when unused
  uint16_t arg2 = 0;  // command-specific; 0 when unused
};
inline constexpr size_t kCommandLen = 4;

// spec 6.3 - COMMAND_ACK, 6 bytes.
//
// `result = ACCEPTED` means the frame authenticated and the command was dispatched
// to the local actuator. It does NOT mean the gate moved. Motion is confirmed only
// by a subsequent STATUS with status_reason = GATE_STATE_CHANGE.
struct CommandAck {
  Seq     ack_seq = 0;  // seq of the COMMAND being acknowledged
  uint8_t result  = 0;  // spec 8.2
  uint8_t detail  = 0;  // result-specific; 0 when unused
  // uint16 reserved, written 0
};
inline constexpr size_t kCommandAckLen = 6;

// spec 6.4 - POLL, 1 byte.
struct Poll {
  uint8_t poll_flags = 0;  // bit 0 full status, bit 1 config readback, 7:2 reserved
};
inline constexpr size_t kPollLen = 1;

// spec 6.5 - ERROR, 4 bytes.
struct Error {
  uint8_t err_code = 0;  // spec 8.8
  uint8_t detail   = 0;
  Seq     ref_seq  = 0;  // seq of the offending frame, or 0
};
inline constexpr size_t kErrorLen = 4;

// spec 6.6 - PING, 2 + n bytes. `data` is a NON-OWNING view, like Frame::payload.
struct Ping {
  uint8_t        ping_flags = 0;  // bit 0 = PATTERN_FILL
  uint8_t        n          = 0;  // 0..kPingMaxEcho
  const uint8_t* data       = nullptr;
};
inline constexpr size_t kPingHdrLen = 2;

// spec 7.6 - HEX_REQ, 2 + n bytes. The node is transport only: it does not hold a
// register cache, replay writes, or interpret register semantics.
struct HexReq {
  uint8_t        flags = 0;  // bit 0 = write-class (declared, NOT trusted - spec 7.6)
  uint8_t        n     = 0;
  const uint8_t* hex   = nullptr;  // ASCII, verbatim, leading ':' included, no newline
};
inline constexpr size_t kHexReqHdrLen = 2;

// spec 7.6 - HEX_RSP, 2 + n bytes.
//
// The spec 6 type table and the spec 19 summary give this as 3 + n / 21 + n; spec
// 7.6's field table gives status, n, data = 2 + n, and spec 19's own layout line
// agrees with spec 7.6. Implemented as 2 + n; the two 3 + n figures are an erratum.
struct HexRsp {
  uint8_t        status = 0;  // spec 8.13
  uint8_t        n      = 0;  // 0 on error
  const uint8_t* hex    = nullptr;
};
inline constexpr size_t kHexRspHdrLen = 2;

Status serialize(const Command& v, uint8_t* out, size_t cap, size_t* written);
Status serialize(const CommandAck& v, uint8_t* out, size_t cap, size_t* written);
Status serialize(const Poll& v, uint8_t* out, size_t cap, size_t* written);
Status serialize(const Error& v, uint8_t* out, size_t cap, size_t* written);
Status serialize(const Ping& v, uint8_t* out, size_t cap, size_t* written);
Status serialize(const HexReq& v, uint8_t* out, size_t cap, size_t* written);
Status serialize(const HexRsp& v, uint8_t* out, size_t cap, size_t* written);

Status deserialize(const uint8_t* in, size_t len, Command* out);
Status deserialize(const uint8_t* in, size_t len, CommandAck* out);
Status deserialize(const uint8_t* in, size_t len, Poll* out);
Status deserialize(const uint8_t* in, size_t len, Error* out);
Status deserialize(const uint8_t* in, size_t len, Ping* out);   // out->data aliases `in`
Status deserialize(const uint8_t* in, size_t len, HexReq* out); // out->hex aliases `in`
Status deserialize(const uint8_t* in, size_t len, HexRsp* out); // out->hex aliases `in`

// spec 6.6.3 - PATTERN_FILL: data[i] = (uint8)((seq & 0xFF) + i).
//
// The initiator regenerates the pattern from the seq it sent and compares byte by
// byte, so a mismatch reports WHICH offset diverged. The CRC16 already tells you a
// frame is corrupt; the offset is what separates a marginal RF path from a
// fragment-reassembly or buffer-indexing bug.
void ping_fill_pattern(Seq seq, uint8_t* out, size_t n);

// Returns true if `data` matches the pattern for `seq`. On mismatch, *first_bad (if
// non-null) receives the index of the first diverging byte.
bool ping_check_pattern(Seq seq, const uint8_t* data, size_t n, size_t* first_bad);

}  // namespace msg
}  // namespace lran
