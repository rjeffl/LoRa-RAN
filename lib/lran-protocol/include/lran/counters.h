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

  // spec 14 stage 8a - a fragmented type spec 11.4 rules out. Stage 8a shares
  // stage 8's wire error, ERROR(BAD_LENGTH), but not its counter: one says a peer
  // fragmented something it may not fragment, the other says a peer's encoder got
  // a length wrong. Different faults, different fixes.
  uint32_t rx_not_fragmentable = 0;
  uint32_t rx_bad_mac         = 0;  // spec 9.4 step 3
  uint32_t rx_ctx_mismatch    = 0;  // spec 10.1

  uint32_t reassembly_timeout = 0;  // stage 10, spec 11.2

  // spec 11.3 - a new (src, ctx_id, seq, schema) displaced a live set with no slot
  // free. Held apart from reassembly_timeout because the two have different
  // diagnoses: a timeout means the RF path dropped a fragment, an abandonment means
  // the receiver is undersized or a peer is interleaving sets. Neither may be silent.
  uint32_t rx_reassembly_abandoned = 0;

  uint32_t fragment_overflow  = 0;  // stage 10, spec 11.2

  // spec 11.2 - a duplicate index within a live set OVERWRITES the stored fragment.
  // Retransmission and RF echo both produce it, so it is not an error and not a
  // discard: it counts an overwrite, and is deliberately NOT summed into
  // total_dropped / schema 0xF0's rx_dropped. No Status maps to it for that reason -
  // the Reassembler increments it directly.
  uint32_t rx_frag_duplicate = 0;

  uint32_t cad_backoffs = 0;  // spec 12.3 - bumped by the radio driver, not a discard

  void bump(Status s);

  // spec 7.5 - feeds schema 0xF0 `rx_dropped`, the sum of all spec 14 discard
  // counters. cad_backoffs is not a discard and rx_frames/tx_frames are not drops.
  uint32_t total_dropped() const;

  void reset();
};

}  // namespace lran
