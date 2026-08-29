// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Discard counters. Spec 14, 7.5.

#pragma once

#include <cstdint>

#include "lran/types.h"

namespace lran {

// Every discard has a name. bump(Status) being the ONLY place the Status -> counter
// mapping exists is what guarantees the bridge and every node report the same thing
// under the same name (spec 14).
struct Counters {
  uint32_t rx_frames = 0;
  uint32_t tx_frames = 0;

  // spec 14 stage 1. The PHY CRC, bumped by the RADIO DRIVER, never by the codec -
  // it is the one counter this library cannot own, and the one discard path that
  // cannot be tested at a desk. Do not conflate it with rx_bad_crc: doing so during
  // bring-up produces a confident and wrong conclusion about whether a link problem
  // is RF or software.
  uint32_t rx_crc_err = 0;

  uint32_t rx_runt            = 0;  // stage 2

  // spec 14 stage 2a. Deliberately NOT folded into rx_bad_length: oversize means a
  // foreign transmitter on the band or a misconfigured PHY, bad length means a
  // peer's encoder is wrong. Different faults, different fixes, and at the gate the
  // counter is the entire diagnosis.
  uint32_t rx_oversize        = 0;

  uint32_t rx_bad_crc         = 0;  // stage 3 - the APPLICATION CRC16
  uint32_t rx_bad_ver         = 0;  // stage 4
  uint32_t rx_not_addressed   = 0;  // stage 5
  uint32_t rx_unknown_hdr_ext = 0;  // stage 5a
  uint32_t rx_bad_frag        = 0;  // stage 5b - spec 5.6, a `frag` total of 0
  uint32_t rx_unknown_type    = 0;  // stage 6
  uint32_t rx_unknown_schema  = 0;  // stage 7
  uint32_t rx_bad_length      = 0;  // stage 8
  uint32_t rx_bad_mac         = 0;  // spec 9.4 step 3
  uint32_t rx_ctx_mismatch    = 0;  // spec 10.1

  uint32_t reassembly_timeout = 0;  // stage 9, spec 11
  uint32_t fragment_overflow  = 0;  // stage 9, spec 11

  uint32_t cad_backoffs = 0;  // spec 12.3 - bumped by the radio driver, not a discard

  void bump(Status s);

  // spec 7.5 - feeds schema 0xF0 `rx_dropped`, the sum of all spec 14 discard
  // counters. cad_backoffs is not a discard and rx_frames/tx_frames are not drops.
  uint32_t total_dropped() const;

  void reset();
};

}  // namespace lran
