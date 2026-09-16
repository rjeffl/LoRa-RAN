// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// ROLE_GATELINK's tokens and telemetry fields. Task BF-6; Impl Plan 10.2, 10.4; spec 7.2-7.4,
// 8.1-8.12.
//
// ARDUINO-FREE. The protocol behaviour lives in gatelink.cpp as Node members (node.h); this
// header holds what the console needs to name things.
//
// EXACT TOKENS. Status reasons and event types are the spec 8.7 and 8.9 names, and field
// names are the GateLinkStatusV1 member names, character for character - a simctl script
// types them.

#pragma once

#include <cstddef>
#include <cstdint>

#include "identity.h"
#include "lran/types.h"

namespace simnode {

const char* status_reason_name(lran::StatusReason r);
bool        parse_status_reason(const char* token, lran::StatusReason* out);
const char* event_type_name(lran::EventType t);
bool        parse_event_type(const char* token, lran::EventType* out);
const char* ack_result_name(lran::AckResult r);
const char* cmd_name(uint8_t cmd);

// `field <hex> <name> <value|na>` - Impl Plan 10.4. `na` writes the root rule 6 sentinel for
// the field's width where one exists.
enum class FieldResult : uint8_t { Ok, UnknownField, BadValue, NoSentinel };
const char* field_result_name(FieldResult r);

FieldResult field_set(GateLinkState* gl, const char* name, const char* value);
size_t      field_count();
const char* field_name(size_t i);
long long   field_value(const GateLinkState& gl, size_t i);

}  // namespace simnode
