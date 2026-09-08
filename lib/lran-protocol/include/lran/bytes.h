// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Bounds-checked little-endian serialization primitives. Spec 4.1, 4.2.

#pragma once

#include <cstddef>
#include <cstdint>

namespace lran {

// Writes little-endian, bounds-checked, never allocating.
//
// Every call returns bool AND latches an ok() flag, so a schema is written as a
// straight run of calls with one check at the end. Checking every call individually
// produces schema code so noisy that offsets get transposed in the noise - and
// offset transposition is the most likely bug in this library.
//
// Shifts are explicit (repo rule 1, spec 4.2). A pointer cast would work on the
// ESP32-S3 and on the host - both little-endian - and be wrong.
class ByteWriter {
 public:
  ByteWriter(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}

  bool u8(uint8_t v);
  bool u16(uint16_t v);
  bool u32(uint32_t v);
  bool i8(int8_t v);
  bool i16(int16_t v);
  bool i32(int32_t v);
  bool bytes(const uint8_t* src, size_t n);
  bool skip(size_t n);  // writes n zero bytes - reserved fields, spec 4.3

  size_t written() const { return pos_; }
  size_t remaining() const { return cap_ - pos_; }
  bool   ok() const { return ok_; }

 private:
  uint8_t* buf_;
  size_t   cap_;
  size_t   pos_ = 0;
  bool     ok_  = true;
};

// Symmetric reader. Same latching contract: read the run, check ok() once.
class ByteReader {
 public:
  ByteReader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {}

  uint8_t  u8();
  uint16_t u16();
  uint32_t u32();
  int8_t   i8();
  int16_t  i16();
  int32_t  i32();
  bool     bytes(uint8_t* dst, size_t n);
  bool     skip(size_t n);  // ignores n bytes - reserved fields, spec 4.3

  size_t read() const { return pos_; }
  size_t remaining() const { return ok_ ? len_ - pos_ : 0; }
  bool   ok() const { return ok_; }

 private:
  const uint8_t* buf_;
  size_t         len_;
  size_t         pos_ = 0;
  bool           ok_  = true;
};

}  // namespace lran
