// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Encoded frames waiting for the radio. Moved from the simnode's node.h by GateLink task L1.
//
// Fixed storage (root rule 3). The engine checks free_slots() before it encodes, so a
// fragmented set is queued whole or not at all: a set missing its last fragments would
// time out at the far end and read as an RF loss.

#pragma once

#include <cstddef>
#include <cstdint>

namespace lran::node {

// A fragmented PING's full set is 15 frames (spec 6.6.2). One more for a health answer.
inline constexpr size_t kOutboxDepth = 16;

// Not LRAN_MAX_FRAME: the SX1262 transmits up to 255 bytes, and the simnode's `oversize`
// fault puts one on the air. The simnode's fault.cpp asserts this equals lran-sim's
// kPhyMaxFrame.
inline constexpr size_t kOutFrameMax = 255;

struct OutFrame {
  uint8_t bytes[kOutFrameMax] = {0};
  size_t  len                 = 0;
};

class Outbox {
 public:
  size_t free_slots() const { return kOutboxDepth - count_; }
  size_t size() const { return count_; }
  bool   push(const uint8_t* bytes, size_t len);
  bool   pop(OutFrame* out);

 private:
  OutFrame frames_[kOutboxDepth];
  size_t   head_  = 0;
  size_t   count_ = 0;
};

}  // namespace lran::node
