// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "lran/counters.h"

namespace lran {

// No `default:` label. Adding a Status enumerator must be a -Werror=switch failure
// here, so a new discard reason cannot ship without a counter to report it under.
void Counters::bump(Status s) {
  switch (s) {
    case Status::Ok:                return;
    case Status::Runt:              ++rx_runt; return;
    case Status::BadCrc:            ++rx_bad_crc; return;
    case Status::BadVersion:        ++rx_bad_ver; return;
    case Status::NotAddressed:      ++rx_not_addressed; return;
    case Status::UnknownHdrExt:     ++rx_unknown_hdr_ext; return;
    case Status::UnknownType:       ++rx_unknown_type; return;
    case Status::UnknownSchema:     ++rx_unknown_schema; return;
    case Status::BadLength:         ++rx_bad_length; return;
    case Status::ReassemblyTimeout: ++reassembly_timeout; return;
    case Status::FragmentOverflow:  ++fragment_overflow; return;
    case Status::BadMac:            ++rx_bad_mac; return;
    case Status::CtxMismatch:       ++rx_ctx_mismatch; return;

    // Caller errors, not wire conditions. Nothing on the link caused them and no
    // spec 14 stage owns them, so they are not counted as drops.
    case Status::BufferTooSmall:    return;
    case Status::NotImplemented:    return;
  }
}

uint32_t Counters::total_dropped() const {
  return rx_crc_err + rx_runt + rx_bad_crc + rx_bad_ver + rx_not_addressed +
         rx_unknown_hdr_ext + rx_unknown_type + rx_unknown_schema + rx_bad_length +
         rx_bad_mac + rx_ctx_mismatch + reassembly_timeout + fragment_overflow;
}

void Counters::reset() { *this = Counters{}; }

}  // namespace lran
