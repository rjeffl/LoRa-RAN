// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "lran/reassembly.h"

#include "lran/codec.h"
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
  set_len_    = 0;
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

  // spec 11.4 - HEX_REQ and HEX_RSP are single-frame in v1. A fragmented one is
  // discarded at spec 14 stage 8a; the decode path rejects it first, this is the
  // same rule enforced for a caller driving the Reassembler directly.
  if (total > 1 && !type_is_fragmentable(f.hdr.type)) {
    return count(counters_, Status::NotFragmentable);
  }

  // spec 9.4 / 14 - stage 9 precedes stage 10. A fragment of an authenticated type
  // that was never verified must not occupy a slot: under v0.3's ordering the
  // forgery would be detected only once the set completed, which an attacker simply
  // never allows. Single-frame frames are exempt - nothing is held, and the caller
  // has the frame in hand either way.
  if (total > 1 && frame_has_mac(f.hdr.type, f.payload, f.payload_len) &&
      !f.mac_verified) {
    return count(counters_, Status::BadMac);
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
    // spec 11.3 - a different set arrived while this one was still incomplete and
    // this object holds one set. The displaced set is ABANDONED, which is a
    // different diagnosis from a timeout: a timeout means the RF path dropped a
    // fragment, an abandonment means the receiver is undersized or a peer is
    // interleaving sets. A bridge holding one Reassembler per provisioned node
    // (spec 11.3) should see this counter stay at zero; if it rises, the fix is
    // capacity, not RF.
    count(counters_, Status::ReassemblyAbandoned);
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

  const bool     duplicate = (got_mask_ & static_cast<uint16_t>(1u << index)) != 0;
  const uint16_t old_len   = duplicate ? frag_len_[index] : 0;

  // spec 11.2 - the reassembled length is the sum of the fragment payload lengths,
  // and a set exceeding its cap is ERROR(FRAGMENT_OVERFLOW). Checked incrementally so
  // an oversized set is rejected at the fragment that overruns it rather than after
  // the buffer has been overrun. `set_len_` is the set's true length and is NOT
  // stage_used_: an overwrite of differing length leaves dead bytes in staging, and
  // charging those against the cap would reject a set that fits.
  if (static_cast<size_t>(set_len_) - old_len + f.payload_len > cap) {
    count(counters_, Status::FragmentOverflow);
    reset();
    return Status::FragmentOverflow;
  }

  if (duplicate) {
    // spec 11.2 - a duplicate index within a live set OVERWRITES the stored fragment
    // and is counted rx_frag_duplicate. Retransmission after a CAD backoff
    // (spec 12.3) and RF echo both produce it, so it is ordinary traffic rather than
    // an error, and the counter records an overwrite rather than a discard.
    if (counters_ != nullptr) ++counters_->rx_frag_duplicate;

    // Same length is the case that actually occurs and overwrites in place. A
    // different length means the peer has contradicted itself about a fragment it
    // already sent; the new bytes still win, but they must be appended because
    // staging holds fragments at arrival-order offsets, and the old copy is left as
    // dead space.
    if (f.payload_len == old_len) {
      for (size_t i = 0; i < f.payload_len; ++i) {
        stage_[frag_off_[index] + i] = f.payload[i];
      }
      return Status::Ok;
    }
  }

  // The staging buffer is a separate resource from the cap: it also holds whatever
  // dead space earlier overwrites left behind. Exhausting it fails loudly rather
  // than misassembling.
  if (f.payload_len > sizeof(stage_) ||
      static_cast<size_t>(stage_used_) + f.payload_len > sizeof(stage_)) {
    count(counters_, Status::FragmentOverflow);
    reset();
    return Status::FragmentOverflow;
  }

  frag_off_[index] = stage_used_;
  frag_len_[index] = static_cast<uint16_t>(f.payload_len);
  for (size_t i = 0; i < f.payload_len; ++i) stage_[stage_used_ + i] = f.payload[i];
  stage_used_ = static_cast<uint16_t>(stage_used_ + f.payload_len);
  set_len_ =
      static_cast<uint16_t>(static_cast<size_t>(set_len_) - old_len + f.payload_len);
  got_mask_ |= static_cast<uint16_t>(1u << index);

  const uint16_t want_mask = static_cast<uint16_t>((1u << total_) - 1u);
  if (got_mask_ == want_mask) assemble();

  return Status::Ok;
}

}  // namespace lran
