// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// ROLE_GATELINK's tokens and telemetry fields. Task BF-6; Impl Plan 10.2, 10.4; spec 7.2-7.4,
// 8.1-8.14.
//
// ARDUINO-FREE. The protocol behaviour lives in lran-node, and ROLE_GATELINK's application in
// gatelink.cpp as Node::App (node.h); this header holds what the console needs to name things.
//
// EXACT TOKENS. Status reasons, event types and reset causes are the spec 8.7, 8.9 and 8.14
// names, and field
// names are the GateLinkStatusV1 member names, character for character - a simctl script
// types them.

#pragma once

#include <cstddef>
#include <cstdint>

#include "identity.h"
#include "lran/node/names.h"
#include "lran/types.h"

namespace simnode {

// The spec's names, lran-node's since GateLink task L1 (lran/node/names.h).
using lran::node::ack_result_name;
using lran::node::cmd_name;
using lran::node::event_type_name;
using lran::node::parse_event_type;
using lran::node::parse_reset_cause;
using lran::node::parse_status_reason;
using lran::node::reset_cause_name;
using lran::node::status_reason_name;

// `field <hex> <name> <value|na>` - Impl Plan 10.4. `na` writes the root rule 6 sentinel for
// the field's width where one exists.
enum class FieldResult : uint8_t { Ok, UnknownField, BadValue, NoSentinel };
const char* field_result_name(FieldResult r);

FieldResult field_set(GateLinkState* gl, const char* name, const char* value);
size_t      field_count();
const char* field_name(size_t i);
long long   field_value(const GateLinkState& gl, size_t i);

}  // namespace simnode
