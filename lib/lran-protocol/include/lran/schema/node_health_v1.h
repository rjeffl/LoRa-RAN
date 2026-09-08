// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Schema 0xF0 - generic node health, 20 bytes. Spec 7.5.
//
// Emitted by every node type, including the bridge's own self-report and simnode. A
// node type with no application schema yet - a freshly bootstrapped WellLink, a
// simnode - is still fully observable through this alone, which is what makes the
// multi-node bench test possible before WellLink's payload exists.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/types.h"

namespace lran {
namespace schema {

inline constexpr size_t kNodeHealthV1Len = 20;

inline constexpr uint8_t kHealthFlagDebugActive = 0x01;  // spec 7.5, 7:1 reserved

struct NodeHealthV1 {
  uint32_t uptime_s      = 0;  // 0
  uint16_t boot_count    = 0;  // 4
  uint16_t rx_frames     = 0;  // 6
  uint16_t tx_frames     = 0;  // 8
  uint16_t rx_dropped    = 0;  // 10  Counters::total_dropped(), spec 14
  uint16_t cad_backoffs  = 0;  // 12  spec 12.3
  int16_t  last_rssi_dbm = kI16NotAvailable;  // 14
  int16_t  last_snr_db10 = kI16NotAvailable;  // 16  0.1 dB units
  uint8_t  proto_ver     = kProtoVer;         // 18  the ver this node speaks
  uint8_t  health_flags  = 0;                 // 19  bit 0 = any debug mode active
};

Status serialize(const NodeHealthV1& v, uint8_t* out, size_t cap, size_t* written);
Status deserialize(const uint8_t* in, size_t len, NodeHealthV1* out);

}  // namespace schema
}  // namespace lran
