// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Words drawn from a true entropy source and held for a later ctx_id roll. Spec 10.1, D87,
// PRD R-3.5h: BLE runs only inside a window, so a roll between windows cannot draw one then.
//
// ARDUINO-FREE, and not thread safe: the caller guards it. Each word is spent once, because
// a repeated ctx_id defeats replay protection (spec 10.1). 0 means empty, which costs nothing:
// new_ctx_id() never uses 0.

#pragma once

#include <cstddef>
#include <cstdint>

namespace gatelink {

class RollEntropy {
 public:
  // Boot fills it, and a window tops it up, so four rolls can pass with no window at all.
  static constexpr size_t kCap = 4;

  // False when full or `word` is 0; the word is dropped, and the held ones stay unspent.
  bool put(uint32_t word) {
    if (word == 0 || n_ == kCap) return false;
    words_[n_++] = word;
    return true;
  }

  // The oldest held word, or 0. The array moves down, and the spent word is erased.
  uint32_t take() {
    if (n_ == 0) return 0;
    const uint32_t v = words_[0];
    for (size_t i = 1; i < n_; ++i) words_[i - 1] = words_[i];
    words_[--n_] = 0;
    return v;
  }

  size_t size() const { return n_; }

 private:
  uint32_t words_[kCap] = {};
  size_t   n_           = 0;
};

}  // namespace gatelink
