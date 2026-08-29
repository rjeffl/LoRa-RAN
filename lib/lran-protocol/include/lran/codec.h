// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Frame encode and decode. Spec 4, 9.3, 9.4, 14.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config.h"
#include "lran/counters.h"
#include "lran/frame.h"
#include "lran/mac.h"
#include "lran/types.h"

namespace lran {

struct DecodeCtx {
  NodeId self = kNodeBridge;

  // spec 13.1 - the bridge accepts N and N-1 and decodes both. A node accepts only
  // its own version. Inclusive range.
  uint8_t accept_ver_min = kProtoVer;
  uint8_t accept_ver_max = kProtoVer;

  // nullptr skips MAC verification entirely. Bench and vector-generation use only:
  // a production receiver that leaves this null accepts forged COMMANDs.
  IMac*          mac      = nullptr;
  const uint8_t* node_key = nullptr;  // key of the PEER, kNodeKeyLen bytes

  // spec 9.4 step 2 - a node checks that an authenticated frame carries its own
  // ctx_id, BEFORE verifying the MAC. Zero means "do not check": that is the
  // bridge's position, since the bridge has no context of its own and is the party
  // that tracks everyone else's (spec 10.1).
  CtxId expect_ctx_id = 0;

  Counters* counters = nullptr;  // bumped on every failure; may be null
};

struct EncodeCtx {
  IMac*          mac      = nullptr;
  const uint8_t* node_key = nullptr;  // kNodeKeyLen bytes
};

// Phase 1 - spec 14 stages 2 to 6: length, application CRC16, `ver`, `dst`,
// `hdr_flags` bit 7, `type`. Fills out->hdr and leaves out->payload null.
//
// Does NOT validate schema, payload length or MAC.
//
// Splitting decode in two is what lets a receiver count and log a frame it cannot
// fully parse. That is the difference between a version- or schema-skew problem
// being diagnosable and it merely showing up as a rising discard count - and on a
// fleet with no OTA, skew is a designed-for field condition (spec 13.1).
Status decode_header(const uint8_t* buf, size_t len, const DecodeCtx& ctx, Frame* out);

// Phase 2 - spec 14 stages 7 to 9 and spec 9.4 steps 2 and 3: schema known, payload
// length matches (type, schema), ctx_id, and the MAC for authenticated types. Fills
// inout->payload, inout->payload_len and inout->mac.
//
// Call only after decode_header returned Ok, on the same buffer.
//
// For a fragment of a multi-fragment set (frag_total > 1) the exact length check is
// deferred: it belongs to the reassembled set, not to a piece of it. See Reassembler.
Status decode_payload(const uint8_t* buf, size_t len, const DecodeCtx& ctx, Frame* inout);

// Writes a complete frame into `buf`; `out_len` receives the byte count.
//
// Zeroes the three reserved bytes and `hdr_flags` bits 6:0 (spec 4.3). Those bytes
// are inside the MAC (spec 9.3), so leaving them uninitialized produces a frame that
// fails verification intermittently depending on stack contents. Do not optimize the
// zeroing away.
//
// An authenticated type encoded with ctx.mac == nullptr returns NotImplemented
// rather than emitting an unauthenticated frame. Silently dropping the MAC from a
// COMMAND would put an unauthenticated relay pulse on the wire.
Status encode(const Header& hdr, const uint8_t* payload, size_t payload_len,
              const EncodeCtx& ctx, uint8_t* buf, size_t buf_cap, size_t* out_len);

// True when a frame of this type and payload carries a MAC. Payload may be null for
// every type except HEX_REQ, whose requirement is content-dependent (spec 7.6).
bool frame_has_mac(MsgType type, const uint8_t* payload, size_t payload_len);

// Total on-wire size of a frame, or 0 if it would exceed LRAN_MAX_FRAME.
size_t frame_len(size_t payload_len, bool has_mac);

}  // namespace lran
