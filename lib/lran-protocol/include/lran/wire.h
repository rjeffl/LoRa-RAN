// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// The (type, schema) tables. Spec 6, 7.1, 9.2, 19.
//
// Kept apart from codec.h because these are the questions the bridge's diagnostics
// and the vector generator ask without wanting to decode anything.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config.h"
#include "lran/types.h"

namespace lran {

// Returned by fixed_payload_len for the types spec 4.4 declares explicitly variable:
// PING, HEX_REQ/HEX_RSP and CONFIG/CONFIG_ACK. Those carry their own length fields
// and are validated structurally instead.
inline constexpr size_t kVariableLen = static_cast<size_t>(-1);

// spec 6 - stage 6 of the spec 14 ladder.
bool type_is_known(uint8_t type);

// spec 5.7 - schema is meaningful for STATUS, EVENT, CONFIG and CONFIG_ACK, and is
// written 0x00 and ignored for every other type.
bool type_carries_schema(MsgType type);

// spec 7.1 - stage 7. Pairing is validated, not just membership: schema 0x10 is a
// STATUS schema and 0x11 an EVENT schema, so a STATUS announcing 0x11 is an unknown
// (type, schema) combination and is rejected there rather than misparsed at stage 8.
bool schema_is_known(MsgType type, SchemaId schema);

// spec 19 - the expected payload length for a (type, schema), or kVariableLen.
size_t fixed_payload_len(MsgType type, SchemaId schema);

// spec 9.2 - types that ALWAYS carry a MAC. HEX_REQ is deliberately absent: its
// requirement is conditional on the HEX command nibble, see hex_req_is_write_class.
bool type_requires_mac(MsgType type);

// spec 7.6 / 9.2 - a HEX_REQ whose HEX command nibble is Set (0x8) or Restart (0x6)
// MUST carry a valid MAC; Get (0x7) and other reads need none.
//
// The node inspects ONLY the command nibble. `flags` bit 0 is the sender's
// declaration of intent and is not trusted for this decision - otherwise clearing
// one bit would bypass authentication on the MPPT write path, which under LiFePO4 is
// a battery-damage path.
//
// `payload` is the HEX_REQ payload: [flags][n][hex:n], with `hex` the ASCII request
// including its leading ':'. The nibble is the character after the colon.
bool hex_req_is_write_class(const uint8_t* payload, size_t payload_len);

// spec 3.1 - the reassembly cap for a completed set. Schema-bearing and fragmented
// payloads are held to LRAN_MAX_SCHEMA_PAYLOAD so a schema can later gain a MAC
// without any schema exceeding a frame. PING carries no schema and never a MAC, so
// spec 11 caps it at LRAN_MAX_PAYLOAD_PLAIN instead.
constexpr size_t reassembly_cap(MsgType type) {
  return type == MsgType::Ping ? kMaxPayloadPlain : kMaxSchemaPayload;
}

}  // namespace lran
