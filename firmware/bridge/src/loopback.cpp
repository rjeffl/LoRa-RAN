// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Internal packet loopback. Task BF-27; Impl Plan 6.6.3. See loopback.h.

#include "loopback.h"

#include <cstdio>
#include <cstring>

#include "dummy.h"
#include "lran/codec.h"

namespace bridge {

Loopback::Loopback() : ladder_(&counters_) {}

LoopbackOutcome Loopback::handle(const char* line, char* reply, size_t cap) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s", line != nullptr ? line : "");
  char*        w[4];
  const size_t n = console_split(buf, w, sizeof(w) / sizeof(w[0]));
  if (n == 0 || std::strcmp(w[0], "loopback") != 0) {
    if (cap > 0) reply[0] = '\0';
    return LoopbackOutcome::NotMine;
  }
  const char* verb = n > 1 ? w[1] : "show";
  if (std::strcmp(verb, "on") == 0) {
    enabled_ = true;
  } else if (std::strcmp(verb, "off") == 0) {
    enabled_      = false;
    corrupt_next_ = false;
  } else if (std::strcmp(verb, "corrupt") == 0) {
    if (!enabled_) {
      std::snprintf(reply, cap, "loopback: off; `loopback on` first");
      return LoopbackOutcome::Refused;
    }
    corrupt_next_ = true;
  } else if (std::strcmp(verb, "show") != 0) {
    std::snprintf(reply, cap, "loopback: on | off | corrupt | show");
    return LoopbackOutcome::Refused;
  }
  std::snprintf(reply, cap, "loopback: %s%s passed=%lu refused=%lu last=%s",
                enabled_ ? "on" : "off", corrupt_next_ ? ", next frame corrupt," : "",
                static_cast<unsigned long>(passed_), static_cast<unsigned long>(refused_),
                lran::to_string(last_));
  return LoopbackOutcome::Reply;
}

bool Loopback::pass(RxMessage* inout, uint32_t now_ms, char* reply, size_t cap) {
  uint8_t         frame[lran::kMaxFrame];
  size_t          len = 0;
  lran::EncodeCtx ectx;  // no MAC: every type a node sends is unauthenticated (spec 9.2)
  last_ = lran::encode(inout->hdr, inout->payload, inout->payload_len, ectx, frame,
                       sizeof(frame), &len);
  if (last_ != lran::Status::Ok) {
    ++refused_;
    std::snprintf(reply, cap, "loopback: encode refused it: %s", lran::to_string(last_));
    return false;
  }
  if (corrupt_next_) {
    // One bit in the middle of the payload: stage 3's CRC16 is what should catch it.
    frame[len / 2] ^= 0x01;
    corrupt_next_ = false;
  }

  RxDelivery d;
  if (!ladder_.accept(frame, len, now_ms, &d)) {
    last_ = ladder_.last_status();
    ++refused_;
    std::snprintf(reply, cap, "loopback: the ladder refused it: %s", lran::to_string(last_));
    return false;
  }
  // The ladder's copy must be the tool's. A difference is a codec defect, and publishing
  // either version would hide it.
  if (d.payload_len != inout->payload_len ||
      std::memcmp(d.payload, inout->payload, d.payload_len) != 0) {
    last_ = lran::Status::Ok;
    ++refused_;
    std::snprintf(reply, cap, "loopback: payload came back different (%u of %u bytes)",
                  static_cast<unsigned>(d.payload_len),
                  static_cast<unsigned>(inout->payload_len));
    return false;
  }
  inout->hdr          = d.hdr;
  inout->fragments    = d.fragments;
  inout->mac_verified = d.mac_verified;
  // payload bytes already equal; `dummy` and `rx_millis` are the tool's and stay.
  last_ = lran::Status::Ok;
  ++passed_;
  std::snprintf(reply, cap, "loopback: %u-byte frame passed", static_cast<unsigned>(len));
  return true;
}

}  // namespace bridge
