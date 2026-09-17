// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The bridge's receive ladder. Task BF-16; see rx_ladder.h.

#include "rx_ladder.h"

namespace bridge {

RxLadder::RxLadder(lran::Counters* counters) : counters_(counters) {
  for (Slot& s : slots_) {
    s.reassembler = lran::Reassembler(counters);
  }
}

void RxLadder::set_auth(lran::IMac* mac, const PeerKeys* keys) {
  mac_  = mac;
  keys_ = keys;
}

void RxLadder::set_frag_timeout_ms(uint32_t ms) {
  for (Slot& s : slots_) {
    s.reassembler.set_timeout_ms(ms);
  }
}

void RxLadder::on_phy_crc_error() {
  if (counters_ == nullptr) return;
  ++counters_->rx_frames;
  ++counters_->rx_crc_err;
}

bool RxLadder::accept(const uint8_t* buf, size_t len, uint32_t now_ms, RxDelivery* out) {
  if (counters_ != nullptr) ++counters_->rx_frames;
  last_src_ = 0;
  last_seq_ = 0;

  lran::DecodeCtx ctx;
  ctx.self     = lran::kNodeBridge;
  ctx.counters = counters_;
  // spec 13.1, R-3.1e - the bridge accepts N AND N-1 and decodes both (BF-22). This is
  // what makes a protocol rollout incremental instead of a flag day, on a fleet where a
  // flag day means a walk to the gate with a laptop for every node.
  //
  // N-1 ONLY, NEVER N-2. The range is a promise about what the codec can parse without
  // guessing: spec 13.2 allows a field to change meaning across two versions, so
  // best-effort parsing of N-2 would decode a frame into the wrong shape and publish it
  // as though it were current. A rejection is recoverable; a plausible wrong number is
  // not.
  ctx.accept_ver_min = static_cast<uint8_t>(lran::kProtoVer - 1);
  ctx.accept_ver_max = lran::kProtoVer;
  // expect_ctx_id stays 0: spec 9.4 step 2 does not apply to the bridge, which has no
  // context of its own (spec 10.1).

  // BF-19a - THE OFFENDING FRAME'S `src` AND `seq`, READ FROM THE BUFFER, not from the
  // decode that is about to fail. A frame rejected at stage 5a or 6 has a header on the
  // wire but no guarantee that decode_header finished filling `f.hdr`, and spec 14.2's
  // reply needs a `dst` and a `ref_seq` for exactly those frames. Explicit offsets and an
  // explicit little-endian read (spec 4.2, root rule 1): `src` is byte 2 and `seq` bytes
  // 4-5 (spec 5).
  if (len >= lran::kHdrLen) {
    last_src_ = buf[2];
    last_seq_ = static_cast<lran::Seq>(static_cast<uint16_t>(buf[4]) |
                                       (static_cast<uint16_t>(buf[5]) << 8));
    // BF-22 - `ver` is byte 0 (spec 5), read the same way and for a related reason. A
    // frame rejected at stage 4 never reaches the registry, so observe() never sees the
    // version that caused it; without this the node simply falls silent and goes offline
    // after three missed polls, which R-3.1f says it must NOT do. This is how the version
    // survives the discard.
    last_ver_ = buf[0];
  }

  lran::Frame f;
  last_ = lran::decode_header(buf, len, ctx, &f);  // stages 2 to 6
  if (last_ != lran::Status::Ok) return false;

  ctx.mac      = mac_;
  ctx.node_key = keys_ != nullptr ? keys_->key_for(f.hdr.src) : nullptr;
  last_        = lran::decode_payload(buf, len, ctx, &f);  // stages 7 to 9
  if (last_ != lran::Status::Ok) return false;

  // STAGE 9, THE HALF THE CODEC LEAVES TO ITS CALLER. decode_payload verifies a MAC only
  // when it is given both an IMac and a key, and otherwise returns Ok with mac_verified
  // false - correct for the bench and the vector generator, and a forged-COMMAND hole in
  // a production receiver. A MAC the bridge cannot check is a MAC that failed.
  if (lran::frame_has_mac(f.hdr.type, f.payload, f.payload_len) && !f.mac_verified) {
    if (counters_ != nullptr) counters_->bump(lran::Status::RejectedMac);
    last_ = lran::Status::RejectedMac;
    return false;
  }

  // STAGE 9a - A SOURCE THE REGISTRY DOES NOT KNOW GOES NO FURTHER, AND IS NEVER
  // ANSWERED (spec 14 stage 9a, 14.2). After stage 9, so every stage before it still
  // counts an unregistered sender's faults as it would a registered one's, and so the
  // ladder's ordering does not reveal which addresses the bridge knows. Before stage 10,
  // because spec 11.3 sizes reassembly per provisioned node: an unprovisioned transmitter
  // must not take a slot, let alone displace a live set. A single frame is refused here
  // too, so it never costs an RX queue slot.
  if (keys_ == nullptr || !keys_->is_registered(f.hdr.src)) {
    if (counters_ != nullptr) counters_->bump(lran::Status::UnknownSrc);
    last_ = lran::Status::UnknownSrc;
    return false;
  }

  // spec 11.2 - a single-frame frame never touches reassembly state. It is delivered
  // from the caller's buffer and no slot is chosen, so it cannot begin, join, displace
  // or expire a set - and cannot claim a slot from a peer that needs one.
  if (f.hdr.frag_total() == 1) {
    out->hdr          = f.hdr;
    out->payload      = f.payload;
    out->payload_len  = f.payload_len;
    out->fragments    = 1;
    out->mac_verified = f.mac_verified;
    return true;
  }

  Slot* slot = slot_for(f.hdr.src, now_ms);
  last_      = slot->reassembler.accept(f, now_ms);  // stage 10
  if (last_ != lran::Status::Ok || !slot->reassembler.complete()) return false;

  out->hdr          = f.hdr;
  out->payload      = slot->reassembler.data();
  out->payload_len  = slot->reassembler.len();
  out->fragments    = f.hdr.frag_total();
  out->mac_verified = f.mac_verified;
  return true;
}

// BF-19a - WHAT EXPIRED, NOT JUST THAT SOMETHING DID. A set's peer and seq are read
// BEFORE the tick and reported only if the set was live and is not afterwards, which is
// exactly the transition that bumps rx_reassembly_timeout.
//
// THE ORDER IS THE CONTRACT, NOT AN OPTIMISATION. reassembly.h documents src() and seq()
// as valid "while active() or a set has completed", and after an expiry neither holds.
// Reading them afterwards happens to return the same values today, because reset() clears
// the set's state and not its key - which is why no test here distinguishes the two
// orders. That is an implementation detail of a library this file does not own, and the
// day it changes, the ERROR goes to whatever the slot last held.
size_t RxLadder::tick(uint32_t now_ms, ExpiredSet* out, size_t cap) {
  size_t expired = 0;
  for (Slot& s : slots_) {
    if (!s.used) continue;
    const bool         was_active = s.reassembler.active();
    const lran::NodeId src        = s.reassembler.src();
    const lran::Seq    seq        = s.reassembler.seq();

    s.reassembler.tick(now_ms);

    if (was_active && !s.reassembler.active()) {
      if (out != nullptr && expired < cap) {
        out[expired].src = src;
        out[expired].seq = seq;
      }
      ++expired;
    }
  }
  return expired;
}

bool RxLadder::any_set_active() const {
  for (const Slot& s : slots_) {
    if (s.used && s.reassembler.active()) return true;
  }
  return false;
}

// One slot per peer (spec 11.3), in this order of preference: the peer's own slot, an
// unused slot, the least recently used slot holding no live set, and last the least
// recently used slot outright. Only the last destroys work in progress, and it counts it.
RxLadder::Slot* RxLadder::slot_for(lran::NodeId src, uint32_t now_ms) {
  Slot* free_slot = nullptr;
  Slot* idle_lru  = nullptr;
  Slot* any_lru   = nullptr;

  for (Slot& s : slots_) {
    if (s.used && s.src == src) {
      s.last_ms = now_ms;
      return &s;
    }
    if (!s.used) {
      if (free_slot == nullptr) free_slot = &s;
      continue;
    }
    // Unsigned ages, so a millis() wrap does not make the oldest slot look newest.
    const uint32_t age = now_ms - s.last_ms;
    if (any_lru == nullptr || age > static_cast<uint32_t>(now_ms - any_lru->last_ms)) {
      any_lru = &s;
    }
    if (!s.reassembler.active() &&
        (idle_lru == nullptr || age > static_cast<uint32_t>(now_ms - idle_lru->last_ms))) {
      idle_lru = &s;
    }
  }

  Slot* chosen = free_slot != nullptr ? free_slot : idle_lru;
  if (chosen == nullptr) {
    // spec 11.3 - a live set displaced for capacity. The diagnosis is "the receiver is
    // undersized or a peer is interleaving sets", which is rx_reassembly_abandoned's
    // meaning exactly, and never rx_reassembly_timeout's.
    chosen = any_lru;
    if (counters_ != nullptr) counters_->bump(lran::Status::ReassemblyAbandoned);
  }

  // A reassigned slot forgets its previous peer entirely, including the retained key of
  // that peer's last completed set: it can never match the new peer's fragments anyway.
  chosen->reassembler.reset();
  chosen->reassembler.forget_completed();
  chosen->src     = src;
  chosen->used    = true;
  chosen->last_ms = now_ms;
  return chosen;
}

}  // namespace bridge
