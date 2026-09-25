// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// VE.Direct HEX frames: build one, check one, and read a Get or Set reply. Victron's
// "BlueSolar HEX protocol" document, section 1, is the source for every value here; the
// LRAN specification transports these strings verbatim and defines none of them (spec 6.7,
// 7.6).
//
// WHY A LIBRARY. Two firmwares speak HEX before GateLink exists: the simnode's simulated
// MPPT answers it (BF-36) and the bridge reads charge parameters back from it (BF-30).
// Two checksum routines drift, and the drift shows as a simulator that accepts what a real
// MPPT refuses. GateLink's own VE.Direct code joins this library later (GateLink Impl Plan,
// /lib/vedirect/).
//
// ARDUINO-FREE, and the native build proves it. No allocation (root rule 3).
//
// THE FRAME. ':' then the command as ONE hex digit, then each data byte as two hex digits,
// then a check byte as two. The command nibble, the data bytes and the check byte sum to
// 0x55. Digits are uppercase; Victron says they must be, so a lowercase digit is refused
// rather than accepted here and refused by the MPPT. The newline that ends a frame on the
// UART is not part of the string: spec 7.6 carries the request without it.

#pragma once

#include <cstddef>
#include <cstdint>

namespace vedirect {

// Victron section 1 - the command nibble of a request.
enum class HexCmd : uint8_t {
  Ping       = 0x1,
  AppVersion = 0x3,
  ProductId  = 0x4,
  Restart    = 0x6,  // no response is sent
  Get        = 0x7,
  Set        = 0x8,
  Async      = 0xA,
};

// Victron section 1 - the command nibble of a response. Get and Set answer with their own.
enum class HexRsp : uint8_t {
  Done    = 0x1,
  Unknown = 0x3,  // unknown command; the data is the command
  Error   = 0x4,  // frame error, data 0xAAAA
  Ping    = 0x5,
  Get     = 0x7,
  Set     = 0x8,
};

// Victron section 1 - the flags byte of a Get or Set reply.
inline constexpr uint8_t kFlagUnknownId      = 0x01;
inline constexpr uint8_t kFlagNotSupported   = 0x02;  // a write to a read-only value
inline constexpr uint8_t kFlagParameterError = 0x04;  // out of range or inconsistent

// The data carried by one frame. Victron's longest values are strings of a few tens of
// bytes; a frame in a HEX_REQ is capped well above that by spec 3.1 anyway.
inline constexpr size_t kMaxData = 64;

// ':' + command + two digits per byte + two for the check.
inline constexpr size_t kMaxChars = 2 + 2 * kMaxData + 2;

struct Frame {
  uint8_t cmd              = 0;
  uint8_t data[kMaxData]   = {0};
  size_t  len              = 0;
};

enum class Parse : uint8_t {
  Ok,
  NoColon,      // the first character is not ':'
  TooShort,     // no command and check byte
  BadChar,      // anything but 0-9 and A-F after the colon, lowercase included
  OddLength,    // the data and check are not whole bytes
  TooLong,      // more than kMaxData bytes
  BadChecksum,  // the sum is not 0x55
};
const char* parse_name(Parse p);

// The check byte that makes `cmd` and `data` sum to 0x55.
uint8_t check_byte(uint8_t cmd, const uint8_t* data, size_t len);

// Writes the frame as characters, without a newline and without a terminator. Returns the
// count, or 0 when `cmd` is above 0xF, `len` above kMaxData or `cap` too small.
size_t encode(uint8_t cmd, const uint8_t* data, size_t len, char* out, size_t cap);

Parse decode(const char* s, size_t n, Frame* out);

// A Get of `reg`, flags zero as Victron asks.
size_t encode_get(uint16_t reg, char* out, size_t cap);

// A Set of `reg` to `value`, sent in `width` bytes (1, 2 or 4), little-endian.
size_t encode_set(uint16_t reg, uint32_t value, uint8_t width, char* out, size_t cap);

// A Get or Set reply: register, flags and a value of 0 to 4 bytes. False for any other
// response, or for a reply too short to hold a register and flags.
struct RegReply {
  HexRsp   cmd   = HexRsp::Get;
  uint16_t reg   = 0;
  uint8_t  flags = 0;
  uint32_t value = 0;
  uint8_t  width = 0;  // bytes of value; 0 when the reply carries none
};
bool reg_reply(const Frame& f, RegReply* out);

}  // namespace vedirect
