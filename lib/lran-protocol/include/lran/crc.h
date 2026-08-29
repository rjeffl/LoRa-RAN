// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Application CRC16. Spec 2.1.

#pragma once

#include <cstddef>
#include <cstdint>

namespace lran {

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.
//
// spec 2.1 - computed over header || payload || mac and appended little-endian. It
// is NOT covered by the MAC: it is a transport check, not a security control.
//
// This exists alongside the SX1262 hardware CRC because a reassembled fragment set
// is an assembly of separately-CRC'd pieces that nothing else checks, and because
// internal loopback, microSD replay and the committed test vectors carry frames that
// never touch a PHY CRC.
uint16_t crc16_ccitt_false(const uint8_t* data, size_t len);

}  // namespace lran
