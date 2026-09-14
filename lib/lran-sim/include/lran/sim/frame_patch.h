// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The patch-after-encode primitive. Bridge Impl Plan 10.5.2 and 10.6 rule 1.
//
// Some of the 10.5 faults are frames that encode() refuses to emit: `oversize` is longer
// than LRAN_MAX_FRAME, `frag_zero` carries a `frag` total of 0, and `frag_command` is a
// COMMAND with a `frag` total above 1. FramePatch reaches them without a second
// serializer. It takes a frame from lran::encode() or lran::encode_fragment(), changes
// named bytes of that frame, and then reseals it.
//
// The rule it enforces: a frame is produced by the real encoder and then broken in one
// stated way. FramePatch writes no header layout of its own. The only layout knowledge
// here is the byte offset of each single-byte header field (spec 5), and
// test_frame_patch proves each offset against decode_header() rather than trusting it.
//
// Every patch unseals the frame, and frame() returns nullptr until seal() runs. The
// caller chooses what the CRC and MAC say. A forgotten reseal therefore produces no
// frame at all, instead of a frame that also fails its CRC, which would be counted at
// stage 3 and would hide the stage the fault was meant to reach.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/codec.h"
#include "lran/config.h"
#include "lran/frame.h"
#include "lran/types.h"

namespace lran {
namespace sim {

// The SX1262 payload length register is 8 bits, so the PHY can put up to 255 bytes on
// the air. That is what makes spec 14 stage 2a reachable at all. A buffer this size
// holds any frame FramePatch can produce.
inline constexpr size_t kPhyMaxFrame = 255;

// spec 5 - the single-byte header fields, at their wire offsets. `seq` and `ctx_id` are
// absent on purpose: the faults that need them (ctx_jump, seq_jump, seq_wrap) are
// correct frames, and encode() emits them from a Header directly.
enum class HdrByte : uint8_t {
  Ver       = 0,
  Type      = 1,
  Src       = 2,
  Dst       = 3,
  Frag      = 10,
  Schema    = 11,
  HdrFlags  = 12,
  Reserved0 = 13,
  Reserved1 = 14,
  Reserved2 = 15,
};

enum class Seal : uint8_t {
  Crc,        // recompute the CRC16 only; a MAC, if present, keeps its current bytes
  MacAndCrc,  // recompute the MAC over the patched header and payload, then the CRC16
};

enum class PatchStatus : uint8_t {
  Ok,
  NoFrame,     // nothing encoded yet, or the last encode failed
  OutOfRange,  // an index or length outside the frame, the buffer or kPhyMaxFrame
  NoMac,       // a MAC operation on a frame that carries no MAC
  MissingKey,  // Seal::MacAndCrc with no IMac or no key in the EncodeCtx
  Truncated,   // truncate_body() has run; payload and MAC positions no longer exist
  NotSealed,   // flip_crc() before seal(): there is no valid CRC to corrupt yet
};

class FramePatch {
 public:
  // `buf` is caller-owned (repo rule 3). Use kPhyMaxFrame bytes to allow any resize.
  FramePatch(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}

  // Encode a correct frame to start from. Same arguments and Status as the codec. The
  // EncodeCtx is kept for Seal::MacAndCrc, so its key must outlive this object.
  Status encode(const Header& hdr, const uint8_t* payload, size_t payload_len,
                const EncodeCtx& ctx);
  Status encode_fragment(const Header& hdr, const uint8_t* payload, size_t payload_len,
                         uint8_t index, size_t frag_chunk, const EncodeCtx& ctx);

  // Header byte patches. A byte past the end of a truncated body is OutOfRange.
  PatchStatus set_header(HdrByte field, uint8_t value);
  // Packs through Header::set_frag, so the nibble order comes from the codec.
  // set_frag(0, 0) is `frag_zero`.
  PatchStatus set_frag(uint8_t index, uint8_t total);

  // Payload patches. The payload and MAC positions are the ones encode() laid out;
  // patching `type` afterwards does not move them.
  PatchStatus set_payload(size_t index, uint8_t value);
  // Grows or shrinks the payload. The MAC and CRC move with the end of the payload, and
  // new payload bytes take `fill`. Covers `oversize` and `bad_length`.
  PatchStatus resize_payload(size_t payload_len, uint8_t fill);

  // MAC patches.
  PatchStatus strip_mac();
  PatchStatus flip_mac(size_t index, uint8_t mask);

  // Keeps the first `body_len` bytes and puts a CRC slot after them. This produces
  // `runt`. After it runs, only set_header() inside the body, seal(Seal::Crc) and
  // flip_crc() apply.
  PatchStatus truncate_body(size_t body_len);

  PatchStatus seal(Seal how);

  // Corrupts the CRC of a sealed frame and leaves it readable. This produces `bad_crc`.
  // `index` 0 is the low byte, which is written first (spec 2.1).
  PatchStatus flip_crc(size_t index, uint8_t mask);

  // nullptr and 0 unless the frame is sealed.
  const uint8_t* frame() const { return sealed_ ? buf_ : nullptr; }
  size_t         len() const { return sealed_ ? len_ : 0; }

  bool   sealed() const { return sealed_; }
  bool   has_mac() const { return has_mac_; }
  size_t payload_len() const { return payload_len_; }

 private:
  Status adopt(Status s, const Header& hdr, size_t out_len, const EncodeCtx& ctx);
  size_t limit() const { return cap_ < kPhyMaxFrame ? cap_ : kPhyMaxFrame; }
  size_t mac_at() const { return kHdrLen + payload_len_; }
  PatchStatus patched() {
    sealed_ = false;
    return PatchStatus::Ok;
  }

  uint8_t*  buf_;
  size_t    cap_;
  size_t    len_         = 0;
  size_t    payload_len_ = 0;
  bool      has_mac_     = false;
  bool      encoded_     = false;
  bool      truncated_   = false;
  bool      sealed_      = false;
  EncodeCtx ctx_{};
};

}  // namespace sim
}  // namespace lran
