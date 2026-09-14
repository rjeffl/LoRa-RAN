// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The identity table - up to four logical nodes on one board. Task BF-3; Impl Plan 10.3;
// spec 5.3, 9.1, 10.1, 10.2.
//
// ARDUINO-FREE, so every rule here is host-tested.
//
// FROM THE BRIDGE'S SIDE THIS MUST BE INDISTINGUISHABLE FROM FOUR PHYSICAL NODES. So
// everything a physical node owns, an identity owns separately: its key, its context, its
// sequence space, its counters, its command gate and its reassembly state. Only the radio
// is shared. If the bridge behaves differently towards four identities on one radio than
// towards four radios, the bridge is keyed on the wrong thing - report it, do not
// compensate here.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/command_gate.h"
#include "lran/config.h"
#include "lran/counters.h"
#include "lran/mac.h"
#include "lran/reassembly.h"
#include "lran/types.h"

namespace simnode {

// Impl Plan 10.2. A role is behaviour, assigned per identity at runtime.
enum class Role : uint8_t { Range, Health, GateLink, Fault };

// The exact console tokens - ROLE_RANGE, ROLE_HEALTH, ROLE_GATELINK, ROLE_FAULT. Typed by
// an operator or a simctl script, so matched character for character.
const char* role_name(Role r);
bool        parse_role(const char* token, Role* out);

inline constexpr size_t kMaxIdentities = 4;

// Impl Plan 10.3 - 0xF0 to 0xF3, the four provisioned bench addresses (spec 5.3). The rest
// of the bench range is reserved and unkeyed on the bridge.
constexpr bool is_simnode_id(lran::NodeId id) {
  return id >= lran::kNodeSim0 && id <= lran::kNodeSim3;
}

// A PING this identity sent and has not yet seen echoed.
struct PendingPing {
  bool         active    = false;
  lran::NodeId peer      = 0;
  lran::Seq    seq       = 0;
  uint8_t      flags     = 0;
  uint8_t      n         = 0;
  uint8_t      fragments = 1;
  uint32_t     sent_ms   = 0;
};

struct Identity {
  Identity() = default;

  // The gate and the reassembler hold a pointer to `counters` below. A copy would point
  // them at another identity's counters.
  Identity(const Identity&)            = delete;
  Identity& operator=(const Identity&) = delete;

  bool         used      = false;
  lran::NodeId id        = 0;
  Role         role      = Role::Range;
  bool         enabled   = true;  // a disabled identity neither hears nor answers
  uint8_t      proto_ver = lran::kProtoVer;  // V-B10: N and N-1 on air at once

  uint8_t key[lran::kNodeKeyLen] = {0};  // spec 9.1, derived exactly as the bridge does

  lran::CtxId ctx_id = 0;  // spec 10.1 - random, non-zero, per identity
  lran::Seq   tx_seq = 1;  // spec 10.2 - this node's status seq space

  lran::Counters    counters;
  lran::CommandGate gate{&counters};         // spec 9.4 steps 4-5; its high-water is cmd_seq
  lran::Reassembler reassembler{&counters};  // one peer: whoever addresses this identity

  // The chunk the peer fragmented its current set with, inferred from the longest
  // fragment: spec 11.1 fixes every non-final fragment to one length. A fragmented PING is
  // echoed with the same chunk (spec 6.6.2).
  uint8_t rx_chunk = 0;

  bool    heard         = false;
  int16_t last_rssi_dbm = lran::kI16NotAvailable;
  int16_t last_snr_db10 = lran::kI16NotAvailable;

  // Frames that decoded but that this identity's role does not answer. A local diagnostic,
  // not a spec 14.1 counter: the frame was valid.
  uint32_t unhandled = 0;

  PendingPing ping;
};

enum class AddResult : uint8_t { Ok, BadId, Exists, Full, NotReady };

// Any uniformly distributed uint32. esp_random() on the board.
using RandomFn = uint32_t (*)();

class IdentityTable {
 public:
  // The master key is copied: the table derives a key each time an identity is added.
  void init(const uint8_t master[lran::kMasterKeyLen], lran::IKdf* kdf, RandomFn random);

  // A fresh node: new key, new context, status seq 1, zero counters, enabled, ver N.
  AddResult add(lran::NodeId id, Role role);
  bool      remove(lran::NodeId id);

  Identity*       find(lran::NodeId id);
  const Identity* find(lran::NodeId id) const;

  // Slot access for iteration; check `used`.
  Identity&       slot(size_t i) { return slots_[i]; }
  const Identity& slot(size_t i) const { return slots_[i]; }
  size_t          count() const;

  // A simulated reboot of one identity's context (spec 10.1): a new random ctx_id, status
  // seq back to 1 (spec 10.2), and the command gate's cache and mark cleared (spec 10.3).
  // Counters survive, as they would not on a real reboot - kept so a resync test can read
  // what the old context counted.
  bool new_context(lran::NodeId id);

 private:
  lran::CtxId random_ctx();
  void        clear(Identity& e);

  uint8_t     master_[lran::kMasterKeyLen] = {0};
  lran::IKdf* kdf_                         = nullptr;
  RandomFn    random_                      = nullptr;
  Identity    slots_[kMaxIdentities];
};

}  // namespace simnode
