// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "lran/crc.h"

namespace lran {

// spec 2.1 - CRC-16/CCITT-FALSE. Bitwise rather than table-driven: a 512-byte table
// buys microseconds on frames of at most 222 bytes, against a link budget where a
// STATUS frame occupies the channel for 534 ms at SF9 (spec 15.1).
uint16_t crc16_ccitt_false(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;  // init, no reflection, no final XOR
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

}  // namespace lran
