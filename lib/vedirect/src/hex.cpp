// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "vedirect/hex.h"

namespace vedirect {

namespace {

constexpr char kDigits[] = "0123456789ABCDEF";

// Uppercase only - Victron section 1.
int digit_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

const char* parse_name(Parse p) {
  switch (p) {
    case Parse::Ok:          return "ok";
    case Parse::NoColon:     return "no_colon";
    case Parse::TooShort:    return "too_short";
    case Parse::BadChar:     return "bad_char";
    case Parse::OddLength:   return "odd_length";
    case Parse::TooLong:     return "too_long";
    case Parse::BadChecksum: return "bad_checksum";
  }
  return "?";
}

uint8_t check_byte(uint8_t cmd, const uint8_t* data, size_t len) {
  uint8_t sum = cmd;
  for (size_t i = 0; i < len; ++i) sum = static_cast<uint8_t>(sum + data[i]);
  return static_cast<uint8_t>(0x55 - sum);
}

size_t encode(uint8_t cmd, const uint8_t* data, size_t len, char* out, size_t cap) {
  if (cmd > 0xF || len > kMaxData || out == nullptr) return 0;
  if (len > 0 && data == nullptr) return 0;
  const size_t need = 2 + 2 * len + 2;
  if (cap < need) return 0;

  size_t o = 0;
  out[o++] = ':';
  out[o++] = kDigits[cmd];
  for (size_t i = 0; i < len; ++i) {
    out[o++] = kDigits[data[i] >> 4];
    out[o++] = kDigits[data[i] & 0xF];
  }
  const uint8_t chk = check_byte(cmd, data, len);
  out[o++] = kDigits[chk >> 4];
  out[o++] = kDigits[chk & 0xF];
  return o;
}

Parse decode(const char* s, size_t n, Frame* out) {
  if (s == nullptr || out == nullptr || n == 0 || s[0] != ':') return Parse::NoColon;
  if (n < 4) return Parse::TooShort;  // ':' + command + a check byte

  for (size_t i = 1; i < n; ++i) {
    if (digit_value(s[i]) < 0) return Parse::BadChar;
  }
  // After ':' and the command nibble, the rest is whole bytes: the data and the check.
  const size_t rest = n - 2;
  if (rest % 2 != 0) return Parse::OddLength;
  const size_t bytes = rest / 2;  // data plus check
  if (bytes - 1 > kMaxData) return Parse::TooLong;

  out->cmd = static_cast<uint8_t>(digit_value(s[1]));
  out->len = bytes - 1;
  uint8_t sum = out->cmd;
  for (size_t b = 0; b < bytes; ++b) {
    const uint8_t v = static_cast<uint8_t>((digit_value(s[2 + 2 * b]) << 4) |
                                           digit_value(s[3 + 2 * b]));
    if (b < out->len) out->data[b] = v;
    sum = static_cast<uint8_t>(sum + v);
  }
  return sum == 0x55 ? Parse::Ok : Parse::BadChecksum;
}

size_t encode_get(uint16_t reg, char* out, size_t cap) {
  const uint8_t d[3] = {static_cast<uint8_t>(reg & 0xFF), static_cast<uint8_t>(reg >> 8), 0};
  return encode(static_cast<uint8_t>(HexCmd::Get), d, sizeof(d), out, cap);
}

size_t encode_set(uint16_t reg, uint32_t value, uint8_t width, char* out, size_t cap) {
  if (width != 1 && width != 2 && width != 4) return 0;
  uint8_t d[7] = {static_cast<uint8_t>(reg & 0xFF), static_cast<uint8_t>(reg >> 8), 0};
  for (uint8_t i = 0; i < width; ++i) d[3 + i] = static_cast<uint8_t>(value >> (8 * i));
  return encode(static_cast<uint8_t>(HexCmd::Set), d, 3u + width, out, cap);
}

bool reg_reply(const Frame& f, RegReply* out) {
  if (out == nullptr) return false;
  if (f.cmd != static_cast<uint8_t>(HexRsp::Get) && f.cmd != static_cast<uint8_t>(HexRsp::Set)) {
    return false;
  }
  if (f.len < 3 || f.len > 3 + 4) return false;
  out->cmd   = static_cast<HexRsp>(f.cmd);
  out->reg   = static_cast<uint16_t>(f.data[0] | (f.data[1] << 8));
  out->flags = f.data[2];
  out->width = static_cast<uint8_t>(f.len - 3);
  out->value = 0;
  for (uint8_t i = 0; i < out->width; ++i) {
    out->value |= static_cast<uint32_t>(f.data[3 + i]) << (8 * i);
  }
  return true;
}

}  // namespace vedirect
