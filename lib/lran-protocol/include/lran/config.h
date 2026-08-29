// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Derived size constants. Spec 3.1 and 19.

#pragma once

#include <cstddef>
#include <cstdint>

namespace lran {

// spec 3.1 - the only hand-chosen number in the whole size system. The SX126x PHY
// allows 255; the cap leaves headroom and makes every buffer static.
inline constexpr size_t kMaxFrame = 222;

inline constexpr size_t kHdrLen = 16;  // spec 5
inline constexpr size_t kCrcLen = 2;   // spec 2.1
inline constexpr size_t kMacLen = 8;   // spec 9.3

// spec 3.1 - every cap below is computed, never independently declared. v0.2 declared
// a flat 200-byte payload cap beside a 222-byte frame cap and the two did not
// reconcile once the header grew.
inline constexpr size_t kMaxPayloadAuth   = kMaxFrame - kHdrLen - kMacLen - kCrcLen;  // 196
inline constexpr size_t kMaxPayloadPlain  = kMaxFrame - kHdrLen - kCrcLen;            // 204
inline constexpr size_t kMaxSchemaPayload = kMaxPayloadAuth;                          // 196
inline constexpr size_t kPingMaxEcho      = kMaxPayloadPlain - 2;                     // 202

// spec 14 stage 2 - a frame shorter than a bare header plus CRC cannot be parsed.
inline constexpr size_t kMinFrame = kHdrLen + kCrcLen;  // 18

inline constexpr uint8_t kProtoVer     = 2;   // spec 5.1
inline constexpr uint8_t kMaxFragments = 15;  // spec 11 - low nibble of `frag`

// spec 11 - default reassembly expiry. Runtime-configurable (repo rule 8); this is
// only the value a Reassembler starts with.
inline constexpr uint32_t kDefaultFragTimeoutMs = 5000;

// A header resize is a `ver` bump (spec 13.2), and on a fleet with no OTA a `ver`
// bump is a physical visit to every node. Deliberate friction: you cannot do it by
// editing one number without reading this line.
static_assert(kHdrLen == 16, "header layout changed - bump ver, see spec 13.2");
static_assert(kMaxPayloadAuth + kHdrLen + kMacLen + kCrcLen == kMaxFrame, "");
static_assert(kMaxPayloadPlain + kHdrLen + kCrcLen == kMaxFrame, "");
static_assert(kMaxPayloadAuth == 196 && kMaxPayloadPlain == 204 && kPingMaxEcho == 202,
              "spec 19 reference constants");

}  // namespace lran
