// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "lran/bytes.h"

namespace lran {

bool ByteWriter::u8(uint8_t v) {
  if (!ok_ || pos_ + 1 > cap_) { ok_ = false; return false; }
  buf_[pos_++] = v;
  return true;
}

// spec 4.1 - little-endian, written by explicit shift. A cast to uint16_t* would
// work on the ESP32-S3 and on the host and still be the wrong code.
bool ByteWriter::u16(uint16_t v) {
  if (!ok_ || pos_ + 2 > cap_) { ok_ = false; return false; }
  buf_[pos_++] = static_cast<uint8_t>(v & 0xFF);
  buf_[pos_++] = static_cast<uint8_t>((v >> 8) & 0xFF);
  return true;
}

bool ByteWriter::u32(uint32_t v) {
  if (!ok_ || pos_ + 4 > cap_) { ok_ = false; return false; }
  buf_[pos_++] = static_cast<uint8_t>(v & 0xFF);
  buf_[pos_++] = static_cast<uint8_t>((v >> 8) & 0xFF);
  buf_[pos_++] = static_cast<uint8_t>((v >> 16) & 0xFF);
  buf_[pos_++] = static_cast<uint8_t>((v >> 24) & 0xFF);
  return true;
}

// spec 4.5 - two's complement. The conversion below is the identity on every
// platform C++20 onward and on every platform this project targets.
bool ByteWriter::i8(int8_t v)   { return u8(static_cast<uint8_t>(v)); }
bool ByteWriter::i16(int16_t v) { return u16(static_cast<uint16_t>(v)); }
bool ByteWriter::i32(int32_t v) { return u32(static_cast<uint32_t>(v)); }

bool ByteWriter::bytes(const uint8_t* src, size_t n) {
  if (!ok_ || pos_ + n > cap_) { ok_ = false; return false; }
  for (size_t i = 0; i < n; ++i) buf_[pos_++] = src[i];
  return true;
}

// spec 4.3 - reserved fields are written as zero. The MAC covers them (spec 9.3), so
// an encoder that leaves them uninitialized produces a frame that fails verification
// intermittently depending on stack contents.
bool ByteWriter::skip(size_t n) {
  if (!ok_ || pos_ + n > cap_) { ok_ = false; return false; }
  for (size_t i = 0; i < n; ++i) buf_[pos_++] = 0;
  return true;
}

uint8_t ByteReader::u8() {
  if (!ok_ || pos_ + 1 > len_) { ok_ = false; return 0; }
  return buf_[pos_++];
}

uint16_t ByteReader::u16() {
  if (!ok_ || pos_ + 2 > len_) { ok_ = false; return 0; }
  const uint16_t v = static_cast<uint16_t>(buf_[pos_]) |
                     static_cast<uint16_t>(static_cast<uint16_t>(buf_[pos_ + 1]) << 8);
  pos_ += 2;
  return v;
}

uint32_t ByteReader::u32() {
  if (!ok_ || pos_ + 4 > len_) { ok_ = false; return 0; }
  const uint32_t v = static_cast<uint32_t>(buf_[pos_]) |
                     (static_cast<uint32_t>(buf_[pos_ + 1]) << 8) |
                     (static_cast<uint32_t>(buf_[pos_ + 2]) << 16) |
                     (static_cast<uint32_t>(buf_[pos_ + 3]) << 24);
  pos_ += 4;
  return v;
}

int8_t  ByteReader::i8()  { return static_cast<int8_t>(u8()); }
int16_t ByteReader::i16() { return static_cast<int16_t>(u16()); }
int32_t ByteReader::i32() { return static_cast<int32_t>(u32()); }

bool ByteReader::bytes(uint8_t* dst, size_t n) {
  if (!ok_ || pos_ + n > len_) { ok_ = false; return false; }
  for (size_t i = 0; i < n; ++i) dst[i] = buf_[pos_++];
  return true;
}

bool ByteReader::skip(size_t n) {
  if (!ok_ || pos_ + n > len_) { ok_ = false; return false; }
  pos_ += n;
  return true;
}

}  // namespace lran
