// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Discard counters. Spec 14.1 is the normative registry; spec 7.5 consumes the sum.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/types.h"

namespace lran {

// Every discard has a name, and spec 14.1 fixes what that name is. bump(Status) being
// the ONLY place the Status -> counter mapping exists is what guarantees the bridge
// and every node report the same thing under the same name.
//
// FIELD NAMES ARE NORMATIVE (spec 14.1). Two of them - rx_reassembly_timeout and
// rx_fragment_overflow - shipped through P1-P5 without the `rx_` prefix the prose
// used, and the slip survived every review because nothing listed the names together.
// It was caught only when an independent implementation compared them against the
// specification. kCounterRegistry below is that list; keep it and the struct in step.
struct Counters {
  // Totals, not discards. Absent from spec 14.1 and from rx_dropped.
  uint32_t rx_frames = 0;
  uint32_t tx_frames = 0;

  // spec 14 stage 1. The PHY CRC, bumped by the RADIO DRIVER, never by the codec -
  // it is the one counter this library cannot own, and the one discard path that
  // cannot be tested at a desk. Do not conflate it with rx_bad_crc: doing so during
  // bring-up produces a confident and wrong conclusion about whether a link problem
  // is RF or software.
  uint32_t rx_crc_err = 0;

  uint32_t rx_runt             = 0;  // stage 2
  uint32_t rx_oversize         = 0;  // stage 2a - a foreign transmitter or bad PHY
  uint32_t rx_bad_crc          = 0;  // stage 3 - the APPLICATION CRC16
  uint32_t rx_bad_ver          = 0;  // stage 4
  uint32_t rx_not_addressed    = 0;  // stage 5
  uint32_t rx_unknown_hdr_ext  = 0;  // stage 5a
  uint32_t rx_bad_frag         = 0;  // stage 5b - spec 5.6, a `frag` total of 0
  uint32_t rx_unknown_type     = 0;  // stage 6
  uint32_t rx_unknown_schema   = 0;  // stage 7
  uint32_t rx_bad_length       = 0;  // stage 8
  uint32_t rx_not_fragmentable = 0;  // stage 8a - spec 11.4
  uint32_t rx_rejected_ctx     = 0;  // stage 9, spec 9.4 step 2
  uint32_t rx_rejected_mac     = 0;  // stage 9, spec 9.4 step 3

  uint32_t rx_reassembly_timeout   = 0;  // spec 11.2 - incomplete set expired
  uint32_t rx_fragment_overflow    = 0;  // spec 11.2 - index >= total, cap, staging
  uint32_t rx_reassembly_abandoned = 0;  // spec 11.3 - live set displaced

  // spec 14 stage 11 / spec 9.4 steps 4-5. NOT raised by this library: replay and
  // dedup are node behaviour and live outside it (Implementation Plan section 1,
  // open item W12). Carried here anyway because Counters is the aggregate that
  // reaches schema 0xF0, and an rx_dropped missing the replay rejections would
  // understate drops on exactly the frames that matter most - the ones that move a
  // gate. Whoever implements W12 increments these.
  uint32_t rx_rejected_seq = 0;  // step 5 - seq not newer than the high-water mark
  uint32_t rx_dup_command  = 0;  // step 4 - dedup cache hit, the cached ACK is resent

  // spec 14.1 - counted but EXCLUDED from rx_dropped. Each is normal traffic rather
  // than a fault: an overwrite is not a discard at all, a late fragment is what an RF
  // echo looks like, and a dedup hit is the retry mechanism working exactly as
  // spec 10.4 requires. Summing them would make rx_dropped climb during correct
  // operation, which is the one thing a health metric must not do.
  uint32_t rx_frag_duplicate = 0;  // spec 11.2 - duplicate index within a live set
  uint32_t rx_frag_late      = 0;  // spec 11.2 - matches the last completed set

  uint32_t cad_backoffs = 0;  // spec 12.3 - the radio driver's, and not a discard

  void bump(Status s);

  // spec 7.5 / 14.1 - feeds schema 0xF0's `rx_dropped`: the sum of the counters
  // spec 14.1 marks yes, and only those. Computed from kCounterRegistry so the sum
  // cannot drift from the registry the way the names once did.
  uint32_t total_dropped() const;

  void reset();
};

// spec 14.1 - the registry, in the specification's own order. The bridge publishes
// per-node counters by these names (spec 16), so this table is the single place a
// name is written down; nothing should spell one as a literal anywhere else.
struct CounterField {
  const char*          name;
  uint32_t Counters::* field;
  bool                 in_dropped;  // spec 14.1's third column
};

inline constexpr size_t kCounterRegistryLen = 21;
extern const CounterField kCounterRegistry[kCounterRegistryLen];

// A field added to Counters without a registry entry is a counter the bridge cannot
// publish and rx_dropped may silently ignore. 21 registry counters plus rx_frames,
// tx_frames and cad_backoffs, which spec 14.1 deliberately excludes.
static_assert(sizeof(Counters) == 24 * sizeof(uint32_t),
              "Counters changed - update kCounterRegistry and spec 14.1 together");

}  // namespace lran
