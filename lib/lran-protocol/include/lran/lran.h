// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Umbrella header. Binding specification: LRAN-Protocol-Specification v0.13, ver = 2.
//
// v0.4 -> v0.6 brought a behavioural change to the codec (11.2: a single-frame frame
// never touches reassembly state). v0.11 brought one to receiver state: 9.4's
// check/record window is answered, and CommandGate (D34, P8) implements it. Nothing
// on the wire has moved since v0.6: no frame layout, header field, enumeration value,
// schema or authentication scope, and no W4 vector regenerated.
//
// This library moves bytes and validates them. It decides nothing: no radio, no
// MQTT, no scheduling, no publication policy, no node behaviour.

#pragma once

#include "lran/bytes.h"
#include "lran/codec.h"
#include "lran/command_gate.h"
#include "lran/config.h"
#include "lran/counters.h"
#include "lran/crc.h"
#include "lran/frame.h"
#include "lran/mac.h"
#include "lran/messages.h"
#include "lran/reassembly.h"
#include "lran/schema/node_config_v1.h"
#include "lran/schema/gatelink_event_v1.h"
#include "lran/schema/gatelink_status_v1.h"
#include "lran/schema/node_health_v1.h"
#include "lran/seq.h"
#include "lran/types.h"
#include "lran/wire.h"
