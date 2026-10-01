// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink task L1; see outbox.h.

#include "lran/node/outbox.h"

#include <cstring>

namespace lran::node {

bool Outbox::push(const uint8_t* bytes, size_t len) {
  if (count_ == kOutboxDepth || len > kOutFrameMax) return false;
  OutFrame& f = frames_[(head_ + count_) % kOutboxDepth];
  std::memcpy(f.bytes, bytes, len);  // bytes already encoded; not a struct
  f.len = len;
  ++count_;
  return true;
}

bool Outbox::pop(OutFrame* out) {
  if (count_ == 0) return false;
  const OutFrame& f = frames_[head_];
  std::memcpy(out->bytes, f.bytes, f.len);
  out->len = f.len;
  head_    = (head_ + 1) % kOutboxDepth;
  --count_;
  return true;
}

}  // namespace lran::node
