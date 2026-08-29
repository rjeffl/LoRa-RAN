// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "lran/messages.h"

#include "lran/bytes.h"

namespace lran {
namespace msg {
namespace {
Status finish(ByteWriter& w, size_t* written) {
  if (!w.ok()) return Status::BufferTooSmall;
  if (written != nullptr) *written = w.written();
  return Status::Ok;
}
}  // namespace

Status serialize(const Command& v, uint8_t* out, size_t cap, size_t* written) {
  ByteWriter w(out, cap);
  w.u8(v.cmd);    // spec 6.2 off 0
  w.u8(v.arg);    // off 1
  w.u16(v.arg2);  // off 2..3
  return finish(w, written);
}

Status serialize(const CommandAck& v, uint8_t* out, size_t cap, size_t* written) {
  ByteWriter w(out, cap);
  w.u16(v.ack_seq);  // spec 6.3 off 0..1
  w.u8(v.result);    // off 2
  w.u8(v.detail);    // off 3
  w.skip(2);         // off 4..5 reserved, written 0
  return finish(w, written);
}

Status serialize(const Poll& v, uint8_t* out, size_t cap, size_t* written) {
  ByteWriter w(out, cap);
  w.u8(v.poll_flags);  // spec 6.4 off 0
  return finish(w, written);
}

Status serialize(const Error& v, uint8_t* out, size_t cap, size_t* written) {
  ByteWriter w(out, cap);
  w.u8(v.err_code);  // spec 6.5 off 0
  w.u8(v.detail);    // off 1
  w.u16(v.ref_seq);  // off 2..3
  return finish(w, written);
}

Status serialize(const Ping& v, uint8_t* out, size_t cap, size_t* written) {
  if (v.n > kPingMaxEcho) return Status::BadLength;  // spec 6.6.1
  if (v.n > 0 && v.data == nullptr) return Status::BufferTooSmall;
  ByteWriter w(out, cap);
  w.u8(v.ping_flags);  // spec 6.6 off 0
  w.u8(v.n);           // off 1
  w.bytes(v.data, v.n);
  return finish(w, written);
}

Status serialize(const HexReq& v, uint8_t* out, size_t cap, size_t* written) {
  if (v.n > 0 && v.hex == nullptr) return Status::BufferTooSmall;
  ByteWriter w(out, cap);
  w.u8(v.flags);  // spec 7.6 off 0
  w.u8(v.n);      // off 1
  w.bytes(v.hex, v.n);
  return finish(w, written);
}

Status serialize(const HexRsp& v, uint8_t* out, size_t cap, size_t* written) {
  if (v.n > 0 && v.hex == nullptr) return Status::BufferTooSmall;
  ByteWriter w(out, cap);
  w.u8(v.status);  // spec 7.6 off 0
  w.u8(v.n);       // off 1
  w.bytes(v.hex, v.n);
  return finish(w, written);
}

Status deserialize(const uint8_t* in, size_t len, Command* out) {
  if (len != kCommandLen) return Status::BadLength;
  ByteReader r(in, len);
  out->cmd  = r.u8();
  out->arg  = r.u8();
  out->arg2 = r.u16();
  return r.ok() ? Status::Ok : Status::BadLength;
}

Status deserialize(const uint8_t* in, size_t len, CommandAck* out) {
  if (len != kCommandAckLen) return Status::BadLength;
  ByteReader r(in, len);
  out->ack_seq = r.u16();
  out->result  = r.u8();
  out->detail  = r.u8();
  r.skip(2);  // spec 4.3 - reserved, ignored on receive
  return r.ok() ? Status::Ok : Status::BadLength;
}

Status deserialize(const uint8_t* in, size_t len, Poll* out) {
  if (len != kPollLen) return Status::BadLength;
  out->poll_flags = in[0];
  return Status::Ok;
}

Status deserialize(const uint8_t* in, size_t len, Error* out) {
  if (len != kErrorLen) return Status::BadLength;
  ByteReader r(in, len);
  out->err_code = r.u8();
  out->detail   = r.u8();
  out->ref_seq  = r.u16();
  return r.ok() ? Status::Ok : Status::BadLength;
}

Status deserialize(const uint8_t* in, size_t len, Ping* out) {
  if (len < kPingHdrLen) return Status::BadLength;
  out->ping_flags = in[0];
  out->n          = in[1];
  out->data       = (out->n > 0) ? in + kPingHdrLen : nullptr;
  if (out->n > kPingMaxEcho) return Status::BadLength;
  return (len == kPingHdrLen + static_cast<size_t>(out->n)) ? Status::Ok
                                                            : Status::BadLength;
}

Status deserialize(const uint8_t* in, size_t len, HexReq* out) {
  if (len < kHexReqHdrLen) return Status::BadLength;
  out->flags = in[0];
  out->n     = in[1];
  out->hex   = (out->n > 0) ? in + kHexReqHdrLen : nullptr;
  return (len == kHexReqHdrLen + static_cast<size_t>(out->n)) ? Status::Ok
                                                              : Status::BadLength;
}

Status deserialize(const uint8_t* in, size_t len, HexRsp* out) {
  if (len < kHexRspHdrLen) return Status::BadLength;
  out->status = in[0];
  out->n      = in[1];
  out->hex    = (out->n > 0) ? in + kHexRspHdrLen : nullptr;
  return (len == kHexRspHdrLen + static_cast<size_t>(out->n)) ? Status::Ok
                                                              : Status::BadLength;
}

void ping_fill_pattern(Seq seq, uint8_t* out, size_t n) {
  const uint8_t base = static_cast<uint8_t>(seq & 0xFF);
  for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(base + i);
}

bool ping_check_pattern(Seq seq, const uint8_t* data, size_t n, size_t* first_bad) {
  const uint8_t base = static_cast<uint8_t>(seq & 0xFF);
  for (size_t i = 0; i < n; ++i) {
    if (data[i] != static_cast<uint8_t>(base + i)) {
      if (first_bad != nullptr) *first_bad = i;
      return false;
    }
  }
  return true;
}

}  // namespace msg
}  // namespace lran
