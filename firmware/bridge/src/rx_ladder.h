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
// registry tracks. TODO(BF-19a): ERROR replies, after spec v0.12 says whether the bridge MUST
// answer and to which src and ctx_id when the header is not yet authenticated.

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

// Which peers the bridge knows, and their keys. registry.h implements this (BF-15).
//
// Until something does, every frame is refused: a source the bridge does not know goes no
// further, and a MAC the bridge cannot check is a MAC that failed (spec 9.4 step 3).
class PeerKeys {
 public:
  virtual ~PeerKeys() = default;

  // The kNodeKeyLen-byte key for `src`, or nullptr when the bridge holds none.
  virtual const uint8_t* key_for(lran::NodeId src) const = 0;

  // True when `src` is a provisioned node.
  virtual bool is_registered(lran::NodeId src) const = 0;
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
// Slots go to registered nodes only, so an unprovisioned transmitter cannot occupy one or
// displace a live set. registry.h asserts that the node table fits.
inline constexpr size_t kReassemblySlots = 8;

// One expired reassembly set, for the ERROR its peer is owed (spec 14 stage 10).
struct ExpiredSet {
  lran::NodeId src = 0;
  lran::Seq    seq = 0;
};

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
  // Expires stale reassembly sets (spec 11.2). BF-19a: the peers whose sets expired are
  // reported, because spec 14 stage 10 answers a timeout with ERROR(REASSEMBLY_TIMEOUT)
  // and the answer needs a `dst` the tick is the only place that knows. Returns how many
  // expired, and fills up to `cap` of them; passing no buffer expires silently, which is
  // what a caller that sends no ERROR wants.
  size_t tick(uint32_t now_ms, ExpiredSet* out = nullptr, size_t cap = 0);

  // True while any peer's set is incomplete. One of lora_task's idle conditions.
  bool any_set_active() const;

  // Why the last accept() delivered nothing, or Ok. For the raw frame log (BF-27). Ok
  // with nothing delivered is an incomplete set. A frame from a source the registry does
  // not know reads Status::UnknownSrc - spec 14 stage 9a since v0.12, counted
  // rx_unknown_src in the codec's own Counters and summed into rx_dropped. Through v0.11
  // it was a bridge-local diagnostic named unregistered_src, because spec 14 had no
  // stage to map it to (BF-15a).
  lran::Status last_status() const { return last_; }

  // The offending frame's `src` and `seq`, valid when the header decoded - which is every
  // stage from 4 on, and so every stage that names an ERROR (spec 14.2, BF-19a). Both
  // read 0 when the frame was too short or too long to have a readable header, which are
  // the stages spec 14 answers with silence anyway.
  lran::NodeId last_src() const { return last_src_; }
  lran::Seq    last_seq() const { return last_seq_; }

  // BF-22 - the `ver` of the frame last offered, whether or not it was accepted. Read
  // with `last_status() == Status::BadVersion` it names the version a node is running
  // that this bridge cannot parse, which is what R-3.1f's distinct reason needs.
  uint8_t last_ver() const { return last_ver_; }

  // BF-27 - `type`, `schema` and `frag` of the frame last offered, off the wire, for the
  // same reason and by the same route as `last_ver()`. Impl Plan 6.6 asks the raw frame
  // log to carry the type and schema of EVERY frame including a discarded one, and a
  // frame refused before stage 6 has no filled lran::Header to read them from. All three
  // read 0 when the frame was too short to have a header, which `last_status()` says.
  //
  // RAW BYTES, NOT DECODED ENUMS. A frame discarded at stage 6 carries a `type` this
  // build has no name for, and that byte is the whole diagnosis - casting it to MsgType
  // to store it would be casting it to a value the enumeration does not have.
  uint8_t last_type() const { return last_type_; }
  uint8_t last_schema() const { return last_schema_; }
  uint8_t last_frag() const { return last_frag_; }

  // Whether the registry holds a key for this address (spec 9.1). BF-19a asks before
  // answering anything, because spec 14.2 sends an ERROR to a registered source only.
  bool registered(lran::NodeId src) const {
    return keys_ != nullptr && keys_->is_registered(src);
  }

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
  lran::NodeId    last_src_ = 0;
  lran::Seq       last_seq_ = 0;
  uint8_t         last_ver_ = 0;
  uint8_t         last_type_   = 0;
  uint8_t         last_schema_ = 0;
  uint8_t         last_frag_   = 0;
};

}  // namespace bridge
