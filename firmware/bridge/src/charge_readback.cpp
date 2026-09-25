// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "charge_readback.h"

#include "json_writer.h"

namespace bridge {

const ChargeRegister* ChargeReadback::next() const {
  for (size_t i = 0; i < kChargeRegisterCount; ++i) {
    if ((pending_ & (1u << i)) != 0) return &kChargeRegisters[i];
  }
  return nullptr;
}

bool ChargeReadback::on_answer(uint16_t reg, const char* hex, size_t n) {
  size_t i = 0;
  while (i < kChargeRegisterCount && kChargeRegisters[i].id != reg) ++i;
  if (i == kChargeRegisterCount) return false;
  pending_ = static_cast<uint16_t>(pending_ & ~(1u << i));

  const ChargeRegister& r      = kChargeRegisters[i];
  bool                  ok     = false;
  int32_t               parsed = 0;
  vedirect::Frame       f;
  vedirect::RegReply    rr;
  if (hex != nullptr && vedirect::decode(hex, n, &f) == vedirect::Parse::Ok &&
      vedirect::reg_reply(f, &rr) && rr.cmd == vedirect::HexRsp::Get && rr.reg == reg &&
      rr.flags == 0 && rr.width == r.width) {
    ok     = true;
    parsed = r.is_signed && r.width == 2 ? static_cast<int16_t>(rr.value)
                                         : static_cast<int32_t>(rr.value);
  }

  const bool had     = have(i);
  const bool changed = had != ok || (ok && values_[i] != parsed);
  if (ok) {
    have_       = static_cast<uint16_t>(have_ | (1u << i));
    values_[i]  = parsed;
  } else {
    have_ = static_cast<uint16_t>(have_ & ~(1u << i));
  }
  return changed;
}

size_t ChargeReadback::json(char* out, size_t cap) const {
  JsonObject j(out, cap);
  for (size_t i = 0; i < kChargeRegisterCount; ++i) {
    const ChargeRegister& r = kChargeRegisters[i];
    if (have(i)) {
      j.decimal(r.key, values_[i], r.decimals);
    } else {
      j.null(r.key);
    }
  }
  return j.finish();
}

}  // namespace bridge
