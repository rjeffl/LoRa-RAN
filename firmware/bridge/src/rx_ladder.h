// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Spec 14 stages 1 to 10 for every frame the bridge's radio hands over. Task BF-16;
// spec 9.4 steps 1 and 3, 11.2, 11.3, 14; Impl Plan 5.2.
//
// ARDUINO-FREE, and the reason lora_task's work can be tested at a desk: everything
// between "the radio produced some bytes" and "a complete payload is ready for app_task"
// is here. lora_link.cpp only moves bytes between this and the SX1262.
//
// STAGE 11 IS NOT HERE. spec 9.4 steps 4 to 6 apply to authenticated types, and every
// authenticated type is bridge -> node (spec 9.2), so on the bridge they apply to an
// empty set. Status seq is advisory and MUST NOT reject (spec 10.2).
//
// DISCARDS ARE COUNTED, NOT ANSWERED. spec 14 answers several stages with an ERROR
// frame, and the bridge addresses a frame to a node by that node's ctx_id, which the
// registry tracks. TODO(BF-19): ERROR replies, once the registry (BF-15) holds ctx_id.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/codec.h"
#include "lran/counters.h"
#include "lran/frame.h"
#include "lran/mac.h"
#include "lran/reassembly.h"
#include "lran/types.h"

namespace bridge {

// Where a peer's key comes from. BF-15's registry implements this.
//
// Until something does, every authenticated frame is refused: a MAC the bridge cannot
// check is a MAC that failed (spec 9.4 step 3).
class PeerKeys {
 public:
  virtual ~PeerKeys() = default;

  // The kNodeKeyLen-byte key for `src`, or nullptr when the bridge holds none.
  virtual const uint8_t* key_for(lran::NodeId src) const = 0;
};

// A complete payload: a single frame's, or a reassembled set's.
struct RxDelivery {
  // The header of the frame that completed the payload. For a reassembled set every
  // field but `frag` is the set's (spec 11.1); `frag` describes that last fragment,
  // which is why `fragments` is carried separately.
  lran::Header hdr{};

  // Points into the buffer passed to accept() for a single frame, or into the
  // reassembler for a set. Valid until the next accept() or tick(); copy it first.
  const uint8_t* payload     = nullptr;
  size_t         payload_len = 0;

  uint8_t fragments    = 1;
  bool    mac_verified = false;
};

// spec 11.3 - at least one reassembly set per peer the bridge can receive from. Six
// identities are provisioned today (GateLink, WellLink and four simnode identities),
// so eight leaves two spare. One set is ~520 B, so the pool is ~4 KB of static storage.
//
// TODO(BF-15): assign slots to registered nodes only. Until the registry exists a slot
// goes to any `src`, so an unprovisioned transmitter can occupy one - counted as
// rx_reassembly_abandoned when it displaces a live set, never silent.
inline constexpr size_t kReassemblySlots = 8;

class RxLadder {
 public:
  explicit RxLadder(lran::Counters* counters);

  // Both are needed to verify a MAC; either being null refuses every authenticated frame.
  void set_auth(lran::IMac* mac, const PeerKeys* keys);

  // spec 11.2 - frag_reassembly_timeout_ms. Runtime-configurable (root rule 8).
  void set_frag_timeout_ms(uint32_t ms);

  // spec 14 stage 1. The PHY CRC is the driver's to report: the codec never sees the
  // frame. Kept apart from rx_bad_crc, which is the application CRC16 - confusing the
  // two during bring-up produces a confident and wrong verdict on RF versus software.
  void on_phy_crc_error();

  // Stages 2 to 10 for one received frame. True when a complete payload is ready in *out.
  //
  // PRECONDITION: now_ms is monotonic apart from wrapping (lran::Reassembler).
  bool accept(const uint8_t* buf, size_t len, uint32_t now_ms, RxDelivery* out);

  // Expires stale sets. Call from lora_task's loop, not only on arrival: a set whose
  // remaining fragments never come is otherwise never counted (spec 11.2).
  void tick(uint32_t now_ms);

  // True while any peer's set is incomplete. One of lora_task's idle conditions.
  bool any_set_active() const;

  // Why the last accept() delivered nothing, or Ok. For the raw frame log (BF-27).
  lran::Status last_status() const { return last_; }

 private:
  struct Slot {
    lran::Reassembler reassembler;
    lran::NodeId      src     = 0;
    bool              used    = false;
    uint32_t          last_ms = 0;
  };

  Slot* slot_for(lran::NodeId src, uint32_t now_ms);

  lran::Counters* counters_;
  lran::IMac*     mac_  = nullptr;
  const PeerKeys* keys_ = nullptr;
  Slot            slots_[kReassemblySlots];
  lran::Status    last_ = lran::Status::Ok;
};

}  // namespace bridge
