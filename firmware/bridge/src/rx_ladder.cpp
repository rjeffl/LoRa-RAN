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

  lran::DecodeCtx ctx;
  ctx.self     = lran::kNodeBridge;
  ctx.counters = counters_;
  // TODO(BF-22): accept N-1 as well (spec 13.1). N only until version tolerance lands.
  ctx.accept_ver_min = lran::kProtoVer;
  ctx.accept_ver_max = lran::kProtoVer;
  // expect_ctx_id stays 0: spec 9.4 step 2 does not apply to the bridge, which has no
  // context of its own (spec 10.1).

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

void RxLadder::tick(uint32_t now_ms) {
  for (Slot& s : slots_) {
    if (s.used) s.reassembler.tick(now_ms);
  }
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
