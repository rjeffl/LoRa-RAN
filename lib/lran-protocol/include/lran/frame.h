// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// The frame view. Spec 3, 5.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config.h"
#include "lran/types.h"

namespace lran {

// spec 5 - the 16-byte header, in wire order.
//
//   0    1    2    3    4  5    6  7  8  9   10    11     12       13 14 15
//  ver type src dst  seq:2      ctx_id:4    frag schema hdr_flags   rsv:3
struct Header {
  uint8_t  ver       = kProtoVer;
  MsgType  type      = MsgType::Poll;
  NodeId   src       = 0;
  NodeId   dst       = 0;
  Seq      seq       = 0;
  CtxId    ctx_id    = 0;
  uint8_t  frag      = 0x01;  // spec 5.6 - 0x01 is a single unfragmented frame
  SchemaId schema    = kSchemaNone;
  uint8_t  hdr_flags = 0;
  uint8_t  reserved[3] = {0, 0, 0};  // spec 5.9 - written 0, ignored on receive

  // spec 5.6 - high nibble = index (0-based), low nibble = total (1-based).
  uint8_t frag_index() const { return static_cast<uint8_t>(frag >> 4); }
  uint8_t frag_total() const { return static_cast<uint8_t>(frag & 0x0F); }
  bool    critical_ext() const { return (hdr_flags & kHdrFlagCriticalExt) != 0; }

  void set_frag(uint8_t index, uint8_t total) {
    frag = static_cast<uint8_t>((index << 4) | (total & 0x0F));
  }
};

// A NON-OWNING view into a caller-supplied buffer. Never allocates, never copies.
//
// `payload` points into the RX buffer the radio filled. That is what keeps the
// no-dynamic-allocation rule satisfiable, but it means a Frame MUST NOT outlive the
// buffer it was decoded from. A consumer that queues a frame across a task boundary
// copies first; that copy is the task's responsibility, not the library's.
//
// Deliberately no operator==. Comparing two Frames in a test passes trivially when
// both sides came from the same encoder. Compare against hex from the vector file.
struct Frame {
  Header         hdr{};
  const uint8_t* payload     = nullptr;
  size_t         payload_len = 0;

  // Set by decode_payload for authenticated types; nullptr when the frame carries
  // no MAC. Points at the 8 MAC bytes inside the same caller-owned buffer.
  const uint8_t* mac = nullptr;
};

}  // namespace lran
