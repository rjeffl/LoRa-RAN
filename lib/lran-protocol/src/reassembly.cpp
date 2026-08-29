// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "lran/reassembly.h"

#include "lran/wire.h"

namespace lran {
namespace {
Status count(Counters* c, Status s) {
  if (c != nullptr) c->bump(s);
  return s;
}
}  // namespace

void Reassembler::reset() {
  active_     = false;
  complete_   = false;
  got_mask_   = 0;
  stage_used_ = 0;
  out_len_    = 0;
  total_      = 0;
}

bool Reassembler::same_set(const Frame& f) const {
  // spec 11 - the receiver reassembles by (src, ctx_id, seq, schema). `type` is
  // included because a PING set and a STATUS set are capped differently and must
  // never merge if a peer reuses a seq across types.
  return f.hdr.src == src_ && f.hdr.ctx_id == ctx_id_ && f.hdr.seq == seq_ &&
         f.hdr.schema == schema_ && f.hdr.type == type_;
}

void Reassembler::begin(const Frame& f, uint32_t now_ms) {
  reset();
  active_   = true;
  start_ms_ = now_ms;
  src_      = f.hdr.src;
  ctx_id_   = f.hdr.ctx_id;
  seq_      = f.hdr.seq;
  schema_   = f.hdr.schema;
  type_     = f.hdr.type;
  total_    = f.hdr.frag_total();
}

void Reassembler::assemble() {
  size_t off = 0;
  for (uint8_t i = 0; i < total_; ++i) {
    for (uint16_t j = 0; j < frag_len_[i]; ++j) out_[off + j] = stage_[frag_off_[i] + j];
    off += frag_len_[i];
  }
  out_len_  = off;
  complete_ = true;
  active_   = false;
}

void Reassembler::tick(uint32_t now_ms) {
  if (!active_ || complete_) return;
  // Unsigned subtraction, so a millisecond counter wrapping through zero does not
  // resurrect an expired set.
  if (now_ms - start_ms_ >= timeout_ms_) {
    count(counters_, Status::ReassemblyTimeout);
    reset();
  }
}

Status Reassembler::accept(const Frame& f, uint32_t now_ms) {
  if (f.payload == nullptr) return count(counters_, Status::BufferTooSmall);

  tick(now_ms);
  if (complete_) reset();  // the previous set was consumed; this frame starts anew

  // spec 14 stage 5b - the decode path rejects a total of 0 before a frame ever
  // reaches here, but the Reassembler is also driven directly by the bench and by
  // the vector suite, and the two paths must name the same condition.
  const uint8_t total = f.hdr.frag_total();
  const uint8_t index = f.hdr.frag_index();
  if (total == 0) return count(counters_, Status::BadFrag);
  if (total > kMaxFragments || index >= total) {
    return count(counters_, Status::FragmentOverflow);
  }

  const size_t cap = reassembly_cap(f.hdr.type);

  // spec 5.6 - 0x01 is a single unfragmented frame. Handled here so a caller can
  // route everything through the reassembler without branching on frag first.
  if (total == 1) {
    if (f.payload_len > sizeof(out_) || f.payload_len > cap) {
      return count(counters_, Status::FragmentOverflow);
    }
    begin(f, now_ms);
    for (size_t i = 0; i < f.payload_len; ++i) out_[i] = f.payload[i];
    out_len_  = f.payload_len;
    got_mask_ = 1;
    complete_ = true;
    active_   = false;
    return Status::Ok;
  }

  if (active_ && !same_set(f)) {
    // A different set arrived while this one was still incomplete. Only one set is
    // held, so the old one is abandoned - counted under reassembly_timeout because
    // from that set's point of view it never completed within its window. Never
    // silent (repo rule 4).
    count(counters_, Status::ReassemblyTimeout);
    reset();
  }
  if (!active_) begin(f, now_ms);

  // spec 11 - all fragments of a set share the total. A disagreement means two
  // different sets collided on one key, and neither can be trusted.
  if (total != total_) {
    count(counters_, Status::FragmentOverflow);
    reset();
    return Status::FragmentOverflow;
  }

  // A retransmit after a CAD backoff (spec 12.3) is ordinary traffic, not an error.
  if (got_mask_ & static_cast<uint16_t>(1u << index)) return Status::Ok;

  // spec 11 - a set exceeding its cap on reassembly is ERROR(FRAGMENT_OVERFLOW).
  // Checked incrementally so an oversized set is rejected at the fragment that
  // overruns rather than after the buffer has already been overrun.
  if (f.payload_len > sizeof(stage_) ||
      static_cast<size_t>(stage_used_) + f.payload_len > cap) {
    count(counters_, Status::FragmentOverflow);
    reset();
    return Status::FragmentOverflow;
  }

  frag_off_[index] = stage_used_;
  frag_len_[index] = static_cast<uint16_t>(f.payload_len);
  for (size_t i = 0; i < f.payload_len; ++i) stage_[stage_used_ + i] = f.payload[i];
  stage_used_ = static_cast<uint16_t>(stage_used_ + f.payload_len);
  got_mask_ |= static_cast<uint16_t>(1u << index);

  const uint16_t want_mask = static_cast<uint16_t>((1u << total_) - 1u);
  if (got_mask_ == want_mask) assemble();

  return Status::Ok;
}

}  // namespace lran
