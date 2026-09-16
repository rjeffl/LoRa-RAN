// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "lran/sim/frame_patch.h"

#include <cstring>

#include "lran/crc.h"
#include "lran/mac.h"
#include "lran/wire.h"

namespace lran {
namespace sim {

Status FramePatch::adopt(Status s, const Header& hdr, size_t out_len, const EncodeCtx& ctx) {
  encoded_   = (s == Status::Ok);
  sealed_    = encoded_;  // encode() output is a sealed frame
  truncated_ = false;
  len_       = encoded_ ? out_len : 0;
  ctx_       = ctx;
  if (!encoded_) {
    payload_len_ = 0;
    has_mac_     = false;
    return s;
  }
  // encode() wrote header || payload || [mac] || crc, so the layout follows from out_len
  // once MAC presence is known. The codec decides that by type (spec 9.2), except for
  // HEX_REQ, where it depends on the command nibble (spec 7.6). encode() has already
  // checked a HEX_REQ payload's own `n`, so the payload is 2 + n bytes and whatever sits
  // between it and the CRC is the MAC.
  const size_t body = out_len - kHdrLen - kCrcLen;
  if (hdr.type == MsgType::HexReq) {
    payload_len_ = static_cast<size_t>(2) + buf_[kHdrLen + 1];
    has_mac_     = (body - payload_len_ == kMacLen);
  } else {
    has_mac_     = type_requires_mac(hdr.type);
    payload_len_ = body - (has_mac_ ? kMacLen : 0);
  }
  return s;
}

Status FramePatch::encode(const Header& hdr, const uint8_t* payload, size_t payload_len,
                          const EncodeCtx& ctx) {
  size_t out_len = 0;
  const Status s = lran::encode(hdr, payload, payload_len, ctx, buf_, cap_, &out_len);
  return adopt(s, hdr, out_len, ctx);
}

Status FramePatch::encode_fragment(const Header& hdr, const uint8_t* payload,
                                   size_t payload_len, uint8_t index, size_t frag_chunk,
                                   const EncodeCtx& ctx) {
  size_t out_len = 0;
  const Status s = lran::encode_fragment(hdr, payload, payload_len, index, frag_chunk, ctx,
                                         buf_, cap_, &out_len);
  return adopt(s, hdr, out_len, ctx);
}

PatchStatus FramePatch::set_header(HdrByte field, uint8_t value) {
  if (!encoded_) return PatchStatus::NoFrame;
  const size_t at = static_cast<size_t>(field);
  if (at >= len_ - kCrcLen) return PatchStatus::OutOfRange;  // only after truncate_body()
  buf_[at] = value;
  return patched();
}

PatchStatus FramePatch::set_frag(uint8_t index, uint8_t total) {
  Header h;
  h.set_frag(index, total);
  return set_header(HdrByte::Frag, h.frag);
}

PatchStatus FramePatch::set_payload(size_t index, uint8_t value) {
  if (!encoded_) return PatchStatus::NoFrame;
  if (truncated_) return PatchStatus::Truncated;
  if (index >= payload_len_) return PatchStatus::OutOfRange;
  buf_[kHdrLen + index] = value;
  return patched();
}

PatchStatus FramePatch::resize_payload(size_t payload_len, uint8_t fill) {
  if (!encoded_) return PatchStatus::NoFrame;
  if (truncated_) return PatchStatus::Truncated;
  const size_t mac_len = has_mac_ ? kMacLen : 0;
  const size_t total   = kHdrLen + payload_len + mac_len + kCrcLen;
  if (total > limit()) return PatchStatus::OutOfRange;

  // Byte buffers, not structs - root rule 1 is about layouts, and these are wire bytes.
  const size_t new_mac_at = kHdrLen + payload_len;
  std::memmove(buf_ + new_mac_at, buf_ + mac_at(), mac_len);
  if (payload_len > payload_len_) {
    std::memset(buf_ + mac_at(), fill, payload_len - payload_len_);
  }
  payload_len_ = payload_len;
  len_         = total;
  std::memset(buf_ + len_ - kCrcLen, 0, kCrcLen);  // the old CRC is meaningless now
  return patched();
}

PatchStatus FramePatch::strip_mac() {
  if (!encoded_) return PatchStatus::NoFrame;
  if (truncated_) return PatchStatus::Truncated;
  if (!has_mac_) return PatchStatus::NoMac;
  has_mac_ = false;
  len_ -= kMacLen;
  std::memset(buf_ + len_ - kCrcLen, 0, kCrcLen);
  return patched();
}

PatchStatus FramePatch::flip_mac(size_t index, uint8_t mask) {
  if (!encoded_) return PatchStatus::NoFrame;
  if (truncated_) return PatchStatus::Truncated;
  if (!has_mac_) return PatchStatus::NoMac;
  if (index >= kMacLen) return PatchStatus::OutOfRange;
  buf_[mac_at() + index] ^= mask;
  return patched();
}

PatchStatus FramePatch::truncate_body(size_t body_len) {
  if (!encoded_) return PatchStatus::NoFrame;
  if (body_len > len_ - kCrcLen) return PatchStatus::OutOfRange;
  len_       = body_len + kCrcLen;
  truncated_ = true;
  std::memset(buf_ + body_len, 0, kCrcLen);
  return patched();
}

PatchStatus FramePatch::seal(Seal how) {
  if (!encoded_) return PatchStatus::NoFrame;
  if (how == Seal::MacAndCrc) {
    if (truncated_) return PatchStatus::Truncated;
    if (!has_mac_) return PatchStatus::NoMac;
    if (ctx_.mac == nullptr || ctx_.node_key == nullptr) return PatchStatus::MissingKey;
    // spec 9.3 - the whole 16-byte header, then the whole payload.
    ctx_.mac->hmac_sha256_trunc(ctx_.node_key, kNodeKeyLen, buf_, mac_at(), buf_ + mac_at());
  }
  // spec 2.1 - CRC16 over everything before it, little-endian.
  const uint16_t crc = crc16_ccitt_false(buf_, len_ - kCrcLen);
  buf_[len_ - 2] = static_cast<uint8_t>(crc & 0xFF);
  buf_[len_ - 1] = static_cast<uint8_t>(crc >> 8);
  sealed_ = true;
  return PatchStatus::Ok;
}

PatchStatus FramePatch::flip_crc(size_t index, uint8_t mask) {
  if (!encoded_) return PatchStatus::NoFrame;
  if (!sealed_) return PatchStatus::NotSealed;
  if (index >= kCrcLen) return PatchStatus::OutOfRange;
  buf_[len_ - kCrcLen + index] ^= mask;
  return PatchStatus::Ok;
}

}  // namespace sim
}  // namespace lran
