// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Schema 0x13 - GateLink status v2, 80 bytes. Spec 7.2.10, D82.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/schema/gatelink_status_v1.h"
#include "lran/types.h"

namespace lran {
namespace schema {

inline constexpr size_t kGateLinkStatusV2Len = 80;  // spec 7.1 registry

// spec 7.2.10 - offsets 0-77 are schema 0x10's byte for byte, so v2 holds a v1 rather
// than repeating its fields. A field added to 0x10's layout would have to be added
// here too, and a second copy is where that would be missed.
struct GateLinkStatusV2 {
  GateLinkStatusV1 v1;
  // 78  0.1 Ah drawn since charging was last active (GateLink PRD R-6.4c). Same unit
  //     as bms_capacity_dah. UINT16_MAX until the node has a total it can stand behind:
  //     a reset never restarts the count from 0, which would understate the reserve used.
  uint16_t ah_drawn_dah = kU16NotAvailable;
};

Status serialize(const GateLinkStatusV2& v, uint8_t* out, size_t cap, size_t* written);
Status deserialize(const uint8_t* in, size_t len, GateLinkStatusV2* out);

}  // namespace schema
}  // namespace lran
