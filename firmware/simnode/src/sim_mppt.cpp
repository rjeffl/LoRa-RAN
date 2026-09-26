// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "sim_mppt.h"

#include "vedirect/hex.h"

namespace simnode {

namespace {

// Victron section 1, "Battery settings registers" and the device state and charger error
// registers. The scale and unit are the bridge's business (BF-30); here only the width.
const SimRegister kProfile[kSimMpptRegisters] = {
    {0x0201, 1, false, 5},     // device state: FLOAT
    {0xEDDA, 1, false, 0},     // charger error: none
    {0xEDF0, 2, true, 150},    // battery maximum current, 0.1 A: 15.0 A, the 75/15's rating
    {0xEDF1, 1, true, 0xFF},   // battery type: user defined (R-6.1b)
    {0xEDF2, 2, true, 0},      // temperature compensation, 0.01 mV/K: off (R-6.1b)
    {0xEDF4, 2, true, 1420},   // equalisation voltage, 0.01 V; auto equalisation is off
    {0xEDF6, 2, true, 1350},   // float voltage, 0.01 V
    {0xEDF7, 2, true, 1420},   // absorption voltage, 0.01 V
    {0xEDFB, 2, true, 200},    // absorption time limit, 0.01 h
    {0xEDFD, 1, true, 0},      // automatic equalisation mode: off (R-6.1b)
    {0xEDEA, 1, true, 12},     // system voltage, V: the library's SYSTEM_VOLTAGE
    {0xEDE0, 2, true, 500},    // battery low temperature level, 0.01 degC, sn16: 5.00
};

// Victron section 1, note 5: these can be changed only while the battery type is user
// defined. The MPPT refuses the rest as out of range or inconsistent.
bool needs_user_type(uint16_t id) {
  return id == 0xEDF7 || id == 0xEDF6 || id == 0xEDF4 || id == 0xEDF2;
}

size_t reply_reg(vedirect::HexRsp cmd, uint16_t id, uint8_t flags, const SimRegister* r,
                 char* out, size_t cap) {
  uint8_t d[3 + 2] = {static_cast<uint8_t>(id & 0xFF), static_cast<uint8_t>(id >> 8), flags};
  size_t  len      = 3;
  if (r != nullptr) {
    for (uint8_t i = 0; i < r->width; ++i) d[len++] = static_cast<uint8_t>(r->value >> (8 * i));
  }
  return vedirect::encode(static_cast<uint8_t>(cmd), d, len, out, cap);
}

size_t reply_u16(vedirect::HexRsp cmd, uint16_t v, char* out, size_t cap) {
  const uint8_t d[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 8)};
  return vedirect::encode(static_cast<uint8_t>(cmd), d, sizeof(d), out, cap);
}

}  // namespace

void SimMppt::reset() {
  for (size_t i = 0; i < kSimMpptRegisters; ++i) regs_[i] = kProfile[i];
}

const SimRegister* SimMppt::find(uint16_t id) const {
  for (const SimRegister& r : regs_) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

SimRegister* SimMppt::find_mut(uint16_t id) {
  for (SimRegister& r : regs_) {
    if (r.id == id) return &r;
  }
  return nullptr;
}

bool SimMppt::set(uint16_t id, uint32_t value) {
  SimRegister* r = find_mut(id);
  if (r == nullptr) return false;
  if (r->width < 4 && value >= (1ul << (8 * r->width))) return false;
  r->value = value;
  return true;
}

MpptReply SimMppt::answer(const char* req, size_t n, char* out, size_t cap, size_t* out_len) {
  using vedirect::HexCmd;
  using vedirect::HexRsp;
  ++requests_;
  *out_len = 0;

  vedirect::Frame f;
  if (vedirect::decode(req, n, &f) != vedirect::Parse::Ok) {
    // Victron section 1: "An error response with value 0xAAAA is sent on framing errors."
    *out_len = reply_u16(HexRsp::Error, 0xAAAA, out, cap);
    return MpptReply::Answered;
  }

  switch (static_cast<HexCmd>(f.cmd)) {
    case HexCmd::Ping:
      *out_len = reply_u16(HexRsp::Ping, kSimFirmware, out, cap);
      return MpptReply::Answered;
    case HexCmd::AppVersion:
      *out_len = reply_u16(HexRsp::Done, kSimFirmware, out, cap);
      return MpptReply::Answered;
    case HexCmd::ProductId:
      *out_len = reply_u16(HexRsp::Done, kSimProductId, out, cap);
      return MpptReply::Answered;
    case HexCmd::Restart:
      return MpptReply::Silent;  // "Restarts the device, no response is sent."
    case HexCmd::Get: {
      if (f.len != 3) break;
      const uint16_t     id = static_cast<uint16_t>(f.data[0] | (f.data[1] << 8));
      const SimRegister* r  = find(id);
      *out_len = reply_reg(HexRsp::Get, id, r == nullptr ? vedirect::kFlagUnknownId : 0, r, out, cap);
      return MpptReply::Answered;
    }
    case HexCmd::Set: {
      if (f.len < 3) break;
      const uint16_t id = static_cast<uint16_t>(f.data[0] | (f.data[1] << 8));
      SimRegister*   r  = find_mut(id);
      uint8_t        flags = 0;
      if (r == nullptr) {
        flags = vedirect::kFlagUnknownId;
      } else if (!r->writable) {
        flags = vedirect::kFlagNotSupported;
      } else if (f.len - 3 != r->width) {
        flags = vedirect::kFlagParameterError;
      } else if (needs_user_type(id) && find(0xEDF1)->value != 0xFF) {
        flags = vedirect::kFlagParameterError;
      } else {
        uint32_t v = 0;
        for (uint8_t i = 0; i < r->width; ++i) v |= static_cast<uint32_t>(f.data[3 + i]) << (8 * i);
        r->value = v;
        ++writes_;
      }
      // A refused Set still reports the value the register holds, so the reply says what
      // the MPPT will charge to rather than echoing what was asked for.
      *out_len = reply_reg(HexRsp::Set, id, flags, r, out, cap);
      return MpptReply::Answered;
    }
    default:
      // Victron section 1.3: an unsupported command is answered Unknown, with the command.
      *out_len = reply_u16(HexRsp::Unknown, f.cmd, out, cap);
      return MpptReply::Answered;
  }
  *out_len = reply_u16(HexRsp::Error, 0xAAAA, out, cap);
  return MpptReply::Answered;
}

}  // namespace simnode
