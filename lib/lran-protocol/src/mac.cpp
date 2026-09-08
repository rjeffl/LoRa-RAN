// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "lran/mac.h"

namespace lran {

// spec 9.4 step 3 - constant time. Accumulating the XOR keeps the running time
// independent of where the first differing byte is; a memcmp leaks the length of the
// matching prefix, which is enough to forge a MAC byte at a time.
bool ct_equal(const uint8_t* a, const uint8_t* b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; ++i) diff = static_cast<uint8_t>(diff | (a[i] ^ b[i]));
  return diff == 0;
}

}  // namespace lran
