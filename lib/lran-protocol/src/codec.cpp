// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "lran/codec.h"

#include "lran/bytes.h"
#include "lran/crc.h"
#include "lran/wire.h"

namespace lran {
namespace {

// Every exit from a decode path goes through here, so a discard is never silent
// (repo rule 4) and the Status -> counter mapping stays in one place.
Status fail(const DecodeCtx& ctx, Status s) {
  if (ctx.counters != nullptr) ctx.counters->bump(s);
  return s;
}

void write_header(ByteWriter& w, const Header& h) {
  w.u8(h.ver);                              // 0
  w.u8(static_cast<uint8_t>(h.type));       // 1
  w.u8(h.src);                              // 2
  w.u8(h.dst);                              // 3
  w.u16(h.seq);                             // 4..5
  w.u32(h.ctx_id);                          // 6..9
  w.u8(h.frag);                             // 10
  w.u8(h.schema);                           // 11
  // spec 4.3 / 5.8 - bits 6:0 are reserved and written zero. Only bit 7 has an
  // assigned meaning today, so only bit 7 survives an encode.
  w.u8(static_cast<uint8_t>(h.hdr_flags & kHdrFlagCriticalExt));  // 12
  w.skip(3);                                // 13..15, spec 5.9
}

void read_header(ByteReader& r, Header* h) {
  h->ver       = r.u8();
  h->type      = static_cast<MsgType>(r.u8());
  h->src       = r.u8();
  h->dst       = r.u8();
  h->seq       = r.u16();
  h->ctx_id    = r.u32();
  h->frag      = r.u8();
  h->schema    = r.u8();
  h->hdr_flags = r.u8();
  // spec 4.3 - reserved bytes are read for completeness and otherwise ignored. They
  // are NOT validated as zero: that would break forward compatibility within a
  // version, which is the whole point of spec 5.9's extension space.
  r.bytes(h->reserved, 3);
}

// spec 4.4 - the types that carry their own length fields validate structurally.
Status check_variable_payload(MsgType type, const uint8_t* p, size_t len) {
  switch (type) {
    case MsgType::Ping: {  // spec 6.6 - [ping_flags][n][data:n]
      if (len < 2) return Status::BadLength;
      const uint8_t n = p[1];
      if (n > kPingMaxEcho) return Status::BadLength;
      return (len == static_cast<size_t>(2) + n) ? Status::Ok : Status::BadLength;
    }
    case MsgType::HexReq:  // spec 7.6 - [flags][n][hex:n]
    case MsgType::HexRsp: {  // spec 7.6 - [status][n][hex:n]
      if (len < 2) return Status::BadLength;
      return (len == static_cast<size_t>(2) + p[1]) ? Status::Ok : Status::BadLength;
    }
    case MsgType::Config: {  // spec 7.4 - [op][count][ entries... ]
      if (len < 2) return Status::BadLength;
      size_t off = 2;
      for (uint8_t i = 0; i < p[1]; ++i) {
        // entry: [param_id:2][ptype][len][value:len]
        if (off + 4 > len) return Status::BadLength;
        const size_t vlen = p[off + 3];
        if (off + 4 + vlen > len) return Status::BadLength;
        off += 4 + vlen;
      }
      return (off == len) ? Status::Ok : Status::BadLength;
    }
    case MsgType::ConfigAck: {  // spec 7.4 - [op][persist_status][count][ results... ]
      if (len < 3) return Status::BadLength;
      size_t off = 3;
      for (uint8_t i = 0; i < p[2]; ++i) {
        // result: [param_id:2][status][ptype][len][value:len]
        if (off + 5 > len) return Status::BadLength;
        const size_t vlen = p[off + 4];
        if (off + 5 + vlen > len) return Status::BadLength;
        off += 5 + vlen;
      }
      return (off == len) ? Status::Ok : Status::BadLength;
    }
    default:
      return Status::Ok;
  }
}

}  // namespace

bool frame_has_mac(MsgType type, const uint8_t* payload, size_t payload_len) {
  if (type_requires_mac(type)) return true;
  if (type == MsgType::HexReq) return hex_req_is_write_class(payload, payload_len);
  return false;
}

size_t frame_len(size_t payload_len, bool has_mac) {
  const size_t total = kHdrLen + payload_len + (has_mac ? kMacLen : 0) + kCrcLen;
  return total > kMaxFrame ? 0 : total;
}

Status decode_header(const uint8_t* buf, size_t len, const DecodeCtx& ctx, Frame* out) {
  if (buf == nullptr || out == nullptr) return fail(ctx, Status::BufferTooSmall);

  *out = Frame{};

  // spec 14 stage 2 - a frame shorter than header + CRC cannot be parsed at all.
  if (len < kMinFrame) return fail(ctx, Status::Runt);

  // spec 14 stage 2a - the PHY can hand up 255 bytes, so an oversize frame is a
  // counted runtime discard, not an assertion. rx_oversize is deliberately separate
  // from rx_bad_length: this says a foreign transmitter or a misconfigured PHY, that
  // says a peer's encoder is wrong (spec 14).
  if (len > kMaxFrame) return fail(ctx, Status::Oversize);

  // spec 14 stage 3 - the application CRC16, over header || payload || mac, stored
  // little-endian in the final two bytes. This is rx_bad_crc, NOT rx_crc_err.
  const uint16_t want = crc16_ccitt_false(buf, len - kCrcLen);
  const uint16_t got  = static_cast<uint16_t>(buf[len - 2]) |
                        static_cast<uint16_t>(static_cast<uint16_t>(buf[len - 1]) << 8);
  if (want != got) return fail(ctx, Status::BadCrc);

  ByteReader r(buf, len);
  read_header(r, &out->hdr);

  // spec 5.1, 13.1 - no best-effort parsing of an unknown version.
  if (out->hdr.ver < ctx.accept_ver_min || out->hdr.ver > ctx.accept_ver_max) {
    return fail(ctx, Status::BadVersion);
  }

  // spec 5.3 stage 5
  if (out->hdr.dst != ctx.self && out->hdr.dst != kNodeBroadcast) {
    return fail(ctx, Status::NotAddressed);
  }

  // spec 5.8 stage 5a - the one reserved bit that is validated. No header extension
  // is defined yet (open item W8), so any frame asserting CRITICAL_EXT denotes an
  // extension this build cannot implement, and best-effort parsing is forbidden.
  if (out->hdr.critical_ext()) return fail(ctx, Status::UnknownHdrExt);

  // spec 14 stage 5b AND spec 11.2 - READ BOTH BEFORE CHANGING THIS.
  //
  // Stage 5b validates the declared TOTAL only: a total of 0 is a malformed header,
  // not a single-frame marker (0x01 is). It answers ERROR(BAD_LENGTH) and counts
  // rx_bad_frag - the wire error and the counter deliberately differ, because the
  // counter is the diagnosis and BAD_LENGTH is the nearest existing err_code.
  //
  // An index at or past the total is spec 11.2's condition, not stage 5b's: it
  // answers ERROR(FRAGMENT_OVERFLOW) and counts rx_fragment_overflow. A total of 0
  // means the sender's framing is broken; an out-of-range index means one fragment of
  // an otherwise plausible set has nowhere to land. Two faults, two fixes.
  //
  // v0.4's stage 5b row read "total >= 1, index < total", which contradicted §11.2
  // and led an independent implementation to the other answer. v0.5 corrected the row
  // to the total alone. Both checks live here so a frame malformed in its `frag` byte
  // reports that rather than a later stage.
  if (out->hdr.frag_total() == 0) return fail(ctx, Status::BadFrag);
  if (out->hdr.frag_index() >= out->hdr.frag_total()) {
    return fail(ctx, Status::FragmentOverflow);
  }

  // spec 6 stage 6
  if (!type_is_known(static_cast<uint8_t>(out->hdr.type))) {
    return fail(ctx, Status::UnknownType);
  }

  return Status::Ok;
}

Status decode_payload(const uint8_t* buf, size_t len, const DecodeCtx& ctx, Frame* inout) {
  if (buf == nullptr || inout == nullptr) return fail(ctx, Status::BufferTooSmall);

  // decode_header has already established this, but the arithmetic below subtracts
  // from `len` and a caller that skipped phase 1 would read off the end of the
  // buffer rather than get an error.
  if (len < kMinFrame || len > kMaxFrame) return fail(ctx, Status::Runt);

  const Header& h = inout->hdr;

  // spec 14 stage 5b - decode_header has already rejected both of these, but phase 2
  // subtracts from `len` using the nibbles below and a caller that skipped phase 1
  // would read off the end of the buffer rather than get an error.
  const uint8_t frag_total = h.frag_total();
  const uint8_t frag_index = h.frag_index();
  if (frag_total == 0) return fail(ctx, Status::BadFrag);
  if (frag_total > kMaxFragments || frag_index >= frag_total) {
    return fail(ctx, Status::FragmentOverflow);
  }

  const size_t body_len = len - kHdrLen - kCrcLen;  // payload plus MAC, if any

  // Work out whether a MAC is present before the payload boundary is known.
  // Everything except HEX_REQ is decided by type alone (spec 9.2).
  bool   mac_present = type_requires_mac(h.type);
  size_t payload_len = 0;

  if (h.type == MsgType::HexReq) {
    // spec 7.6 - MAC presence is content-dependent, so the payload's own `n` field
    // fixes the boundary and whatever remains must be exactly a MAC or nothing.
    if (frag_total > 1) return fail(ctx, Status::NotFragmentable);  // spec 14 stage 8a
    if (body_len < 2) return fail(ctx, Status::BadLength);
    payload_len = static_cast<size_t>(2) + buf[kHdrLen + 1];
    if (payload_len > body_len) return fail(ctx, Status::BadLength);
    const size_t rem = body_len - payload_len;
    if (rem == kMacLen)   mac_present = true;
    else if (rem != 0)    return fail(ctx, Status::BadLength);
  } else {
    if (mac_present && body_len < kMacLen) return fail(ctx, Status::BadLength);
    payload_len = body_len - (mac_present ? kMacLen : 0);
  }

  // spec 7.1 stage 7
  if (!schema_is_known(h.type, h.schema)) return fail(ctx, Status::UnknownSchema);

  // spec 14 stage 8a - the type must be fragmentable if `frag` declares a total > 1.
  // spec 11.4 rules HEX_REQ and HEX_RSP out in v1 rather than leaving them undefined,
  // and keeps COMMAND and the other fixed small types single-frame. The wire answer
  // is ERROR(BAD_LENGTH) per spec 11.4; the counter is rx_not_fragmentable.
  if (frag_total > 1 && !type_is_fragmentable(h.type)) {
    return fail(ctx, Status::NotFragmentable);
  }

  // spec 14 stage 8. A fragment is a piece of a payload, so the (type, schema)
  // length belongs to the reassembled set; here only the per-frame cap applies.
  if (frag_total > 1) {
    if (payload_len == 0 || payload_len > reassembly_cap(h.type)) {
      return fail(ctx, Status::FragmentOverflow);
    }
  } else {
    const size_t want = fixed_payload_len(h.type, h.schema);
    if (want == kVariableLen) {
      const Status vs = check_variable_payload(h.type, buf + kHdrLen, payload_len);
      if (vs != Status::Ok) return fail(ctx, vs);
    } else if (payload_len != want) {
      return fail(ctx, Status::BadLength);
    }
  }

  inout->payload     = buf + kHdrLen;
  inout->payload_len = payload_len;
  inout->mac         = mac_present ? buf + kHdrLen + payload_len : nullptr;

  // spec 9.4 step 2 - ctx_id before the MAC, and only for the authenticated types,
  // which are the ones spec 9.4 governs. A mismatch is the node's cue to answer
  // COMMAND_ACK(REJECTED_CTX) carrying its own ctx_id so the bridge can resync.
  if (ctx.expect_ctx_id != 0 && frame_has_mac(h.type, inout->payload, payload_len) &&
      h.ctx_id != ctx.expect_ctx_id) {
    return fail(ctx, Status::RejectedCtx);
  }

  // spec 9.4 step 3. Deliberately after the ctx check and before any sequence
  // handling the caller does: `seq` is attacker-visible, so checking it before
  // authenticating would leak the high-water mark through timing.
  if (h.type == MsgType::HexReq && !mac_present &&
      hex_req_is_write_class(inout->payload, payload_len)) {
    // spec 7.6 - a write-class HEX request with no MAC at all. The node answers
    // HEX_RSP(REJECTED_UNAUTHENTICATED); to the codec it is a failed authentication.
    return fail(ctx, Status::RejectedMac);
  }

  if (mac_present && ctx.mac != nullptr && ctx.node_key != nullptr) {
    uint8_t want_mac[kMacLen];
    // spec 9.3 - the entire 16-byte header followed by the entire payload. The CRC16
    // sits after the MAC and is not covered by it.
    ctx.mac->hmac_sha256_trunc(ctx.node_key, kNodeKeyLen, buf, kHdrLen + payload_len,
                               want_mac);
    if (!ct_equal(want_mac, inout->mac, kMacLen)) return fail(ctx, Status::RejectedMac);
    inout->mac_verified = true;
  }

  return Status::Ok;
}

Status encode(const Header& hdr, const uint8_t* payload, size_t payload_len,
              const EncodeCtx& ctx, uint8_t* buf, size_t buf_cap, size_t* out_len) {
  if (buf == nullptr || out_len == nullptr) return Status::BufferTooSmall;
  if (payload_len > 0 && payload == nullptr) return Status::BufferTooSmall;
  *out_len = 0;

  if (!type_is_known(static_cast<uint8_t>(hdr.type))) return Status::UnknownType;

  // spec 5.6 - the same split the receive path makes at stage 5b, so a sender bug
  // and the receiver's report of it name the same condition.
  const uint8_t frag_total = hdr.frag_total();
  const uint8_t frag_index = hdr.frag_index();
  if (frag_total == 0) return Status::BadFrag;
  if (frag_total > kMaxFragments || frag_index >= frag_total) {
    return Status::FragmentOverflow;
  }

  // spec 11.4 - "A sender MUST NOT fragment these types." A refusal to emit a frame
  // the specification rules out, not a gap in this library.
  if (frag_total > 1 && !type_is_fragmentable(hdr.type)) return Status::NotFragmentable;

  if (!schema_is_known(hdr.type, hdr.schema)) return Status::UnknownSchema;

  if (frag_total == 1) {
    const size_t want = fixed_payload_len(hdr.type, hdr.schema);
    if (want == kVariableLen) {
      const Status vs = check_variable_payload(hdr.type, payload, payload_len);
      if (vs != Status::Ok) return vs;
    } else if (payload_len != want) {
      return Status::BadLength;
    }
  } else if (payload_len == 0 || payload_len > reassembly_cap(hdr.type)) {
    return Status::FragmentOverflow;
  }

  const bool   mac_present = frame_has_mac(hdr.type, payload, payload_len);
  const size_t total       = frame_len(payload_len, mac_present);
  if (total == 0) return Status::BadLength;         // exceeds LRAN_MAX_FRAME
  if (total > buf_cap) return Status::BufferTooSmall;

  // spec 9.2 - "An encoder MUST NOT emit an authenticated type without a MAC." A
  // build with no key material fails the send; it does not fall back. MissingMac
  // rather than NotImplemented because this is a MISCONFIGURATION - the caller wired
  // the library up wrong or shipped without key material - and a field log has to be
  // able to tell it from a library gap.
  if (mac_present && (ctx.mac == nullptr || ctx.node_key == nullptr)) {
    return Status::MissingMac;
  }

  ByteWriter w(buf, buf_cap);
  write_header(w, hdr);
  w.bytes(payload, payload_len);
  if (!w.ok()) return Status::BufferTooSmall;

  if (mac_present) {
    uint8_t mac[kMacLen];
    ctx.mac->hmac_sha256_trunc(ctx.node_key, kNodeKeyLen, buf, kHdrLen + payload_len,
                               mac);
    w.bytes(mac, kMacLen);
  }

  // spec 2.1 - CRC16 over header || payload || mac, appended little-endian.
  const uint16_t crc = crc16_ccitt_false(buf, w.written());
  w.u16(crc);
  if (!w.ok()) return Status::BufferTooSmall;

  *out_len = w.written();
  return Status::Ok;
}

uint8_t fragment_count(size_t payload_len, size_t chunk) {
  if (chunk == 0) return 0;
  // spec 11.1 - the final fragment "MUST NOT be empty unless the entire payload is",
  // so an empty payload is one empty fragment rather than none.
  if (payload_len == 0) return 1;
  const size_t n = (payload_len + chunk - 1) / chunk;  // ceil
  return n > kMaxFragments ? 0 : static_cast<uint8_t>(n);
}

Status encode_fragment(const Header& hdr, const uint8_t* payload, size_t payload_len,
                       uint8_t index, size_t frag_chunk, const EncodeCtx& ctx,
                       uint8_t* buf, size_t buf_cap, size_t* out_len) {
  if (out_len == nullptr) return Status::BufferTooSmall;
  *out_len = 0;
  if (payload_len > 0 && payload == nullptr) return Status::BufferTooSmall;

  if (!type_is_known(static_cast<uint8_t>(hdr.type))) return Status::UnknownType;

  const size_t chunk = (frag_chunk == 0) ? default_frag_chunk(hdr.type) : frag_chunk;
  const uint8_t total = fragment_count(payload_len, chunk);
  if (total == 0) return Status::FragmentOverflow;  // more than kMaxFragments
  if (index >= total) return Status::FragmentOverflow;

  // spec 11.4, checked before any bytes are laid out so the refusal is unambiguous.
  if (total > 1 && !type_is_fragmentable(hdr.type)) return Status::NotFragmentable;

  // spec 11.1 - uniform chunks, the remainder in the highest index. The offset is
  // index * chunk precisely because the SENDER is the party constrained to uniform
  // chunking; spec 11.2 forbids the RECEIVER computing a placement this way.
  const size_t off  = static_cast<size_t>(index) * chunk;
  const size_t here = (payload_len - off < chunk) ? payload_len - off : chunk;

  Header h = hdr;
  h.set_frag(index, total);
  return encode(h, payload + off, here, ctx, buf, buf_cap, out_len);
}

}  // namespace lran
