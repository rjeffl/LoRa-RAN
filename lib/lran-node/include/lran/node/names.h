// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The specification's names for the values a node logs and an operator types. Spec 6.3,
// 8.1, 8.7, 8.9, 8.14. Moved from the simnode's gatelink.cpp by GateLink task L1.
//
// EXACT TOKENS. Each name is the specification's, character for character: a simctl script
// types the status reasons, event types and reset causes, and a log reader searches for
// them.

#pragma once

#include <cstdint>

#include "lran/types.h"

namespace lran::node {

const char* status_reason_name(StatusReason r);
bool        parse_status_reason(const char* token, StatusReason* out);
const char* event_type_name(EventType t);
bool        parse_event_type(const char* token, EventType* out);
// spec 8.14 - a BOOT event's reset cause.
const char* reset_cause_name(ResetCause c);
bool        parse_reset_cause(const char* token, ResetCause* out);
const char* ack_result_name(AckResult r);
const char* cmd_name(uint8_t cmd);

}  // namespace lran::node
