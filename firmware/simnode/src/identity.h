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
// is shared. Since GateLink task L1 those are lran-node's Context, which Identity extends. If the bridge behaves differently towards four identities on one radio than
// towards four radios, the bridge is keyed on the wrong thing - report it, do not
// compensate here.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config.h"
#include "lran/config/table.h"
#include "lran/counters.h"
#include "lran/mac.h"
#include "lran/schema/node_config_v1.h"
#include "lran/schema/gatelink_event_v1.h"
#include "lran/schema/gatelink_status_v1.h"
#include "lran/node/context.h"
#include "lran/types.h"
#include "sim_mppt.h"

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

// ---------------------------------------------------------------------------
// ROLE_GATELINK state (BF-6). Every identity carries it; only ROLE_GATELINK reads it.
// ---------------------------------------------------------------------------

// Every row a GateLink holds outside the PHY group: node-common's others and GateLink's
// block. Filled, the store answers GET_ALL with GateLink's full readback, 199 bytes, which
// spec 7.4.1 splits across two CONFIG_ACKs - the shape the bridge must stage. Until
// lran-node split readbacks it was 21, the most one CONFIG_ACK holds at u32.
inline constexpr size_t kConfigStoreDepth =
    sizeof(lran::config::kNodeCommonParams) / sizeof(lran::config::kNodeCommonParams[0]) -
    lran::config::kPhyGroupSize +
    sizeof(lran::config::kGateLinkParams) / sizeof(lran::config::kGateLinkParams[0]);

// One parameter held by the generic RAM store (decided with the operator 2026-09-14). No
// param_id is invented here: they belong to /lib/lran-config/, which does not exist yet.
struct StoredParam {
  bool        used     = false;
  uint16_t    param_id = 0;
  lran::PType ptype    = lran::PType::U8;
  uint8_t     len      = 0;
  uint8_t     value[lran::schema::kMaxParamValueLen] = {0, 0, 0, 0};
};

struct GateLinkState {
  // Synthetic telemetry for schema 0xFE, edited by `field`. uptime_s is generated unless set;
  // node_flags bits 2-4 are generated from the three settings below.
  lran::schema::GateLinkStatusV1 status;
  bool                           uptime_set = false;

  // spec 7.3 - monotonic per boot, never reused within a ctx_id; restarts at 1 on a new one.
  uint32_t                      next_event_id  = 1;
  bool                          has_last_event = false;
  lran::schema::GateLinkEventV1 last_event;

  uint32_t   ack_delay_ms      = 0;  // persistent until `ack <hex> normal`
  uint16_t   ack_suppress_left = 0;  // the ack_suppress fault: ACKs still to withhold
  // The ctx_reject fault (BF-21): COMMANDs still to answer REJECTED_CTX whatever ctx they
  // carry. It acts BEFORE the dedup gate, so a rejected command consumes no seq and is
  // never cached - spec 9.4 step 2 comes before steps 4-6, and a node that rejected on
  // context has not looked at the sequence space.
  uint16_t   ctx_reject_left   = 0;
  uint16_t   ack_dup_left      = 0;  // the ack_dup fault: ACKs still to send twice
  // `ack <hex> delay <ms>` stretches a command's spec 9.4 execution window, where a retry is
  // in flight and receives nothing. The delay in force when the pending command started.
  uint32_t   pending_delay_ms  = 0;

  bool     dry_run     = false;  // SET_RELAY_DRY_RUN
  bool     bms_polling = true;   // SET_BMS_POLLING
  uint16_t debug_modes = 0;      // SET_DEBUG_MODE

  // Local diagnostics, not spec 14.1 counters. `actuations` is what a relay would have
  // pulsed: cmd_replay's assertion is that a replay leaves it unchanged.
  uint32_t actuations      = 0;
  uint32_t acks_suppressed = 0;
  uint32_t ctx_rejects_forced = 0;  // ctx_reject fault, spec 10.3

  StoredParam params[kConfigStoreDepth];

  // BF-36 - the MPPT on the far side of the UART. The node's one HEX transaction is the
  // Context's hex_pending.
  SimMppt  mppt;
  // GateLink Impl Plan's hex_timeout_ms, default 1000: how long the node waits for the MPPT
  // before answering TIMEOUT (spec 8.13). Root rule 8, so the console sets it.
  uint32_t hex_timeout_ms   = 1000;
  // `mppt <hex> timeout [count]` - requests still to leave unanswered by the MPPT. Bounded
  // and self-disarming, as every fault here is (simnode rule 3).
  uint16_t hex_timeout_left = 0;
};

// Plausible, not physical, values: a charged 4-cell LiFePO4 pack, a closed gate, sentinels
// where a sensor is absent. Defined in gatelink.cpp.
void reset_gatelink_telemetry(lran::schema::GateLinkStatusV1* s);

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

// The protocol state - id, key, ctx_id, seq spaces, counters, gate, reassembler, pending
// command and HEX transaction - is the Context. Copying is deleted there, because the gate
// and the reassembler point at the counters. proto_ver is per identity, so V-B10 can put
// N and N-1 on air at once.
struct Identity : lran::node::Context {
  bool used    = false;
  Role role    = Role::Range;
  bool enabled = true;  // a disabled identity neither hears nor answers

  PendingPing ping;

  // The `silent` fault (Impl Plan 10.5): answers still to withhold, and answers withheld.
  // Local, like `unhandled`: the frames that went unanswered were valid.
  uint16_t silent_left        = 0;
  uint32_t answers_suppressed = 0;

  GateLinkState gl;
};

enum class AddResult : uint8_t { Ok, BadId, Exists, Full, NotReady };

using lran::node::RandomFn;

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

  // spec 10.6 - a context roll, which is much less than a reboot. A new random ctx_id that
  // differs from the current one, the gate's cache and mark cleared, and the status seq
  // back to 1. Configuration, the actuator state, the counters and the reassembler survive.
  bool roll_context(lran::NodeId id);

  // The key of simnode `id` (0xF0-0xF3), whether or not this board holds that identity, so
  // an authenticated fault can be signed for a simnode on another board. Refuses every other
  // id: this is a bench tool, and it signs for no production node.
  bool derive_simnode_key(lran::NodeId id, uint8_t out[lran::kNodeKeyLen]) const;

  RandomFn random_fn() const { return random_; }

 private:
  void        clear(Identity& e);

  uint8_t     master_[lran::kMasterKeyLen] = {0};
  lran::IKdf* kdf_                         = nullptr;
  RandomFn    random_                      = nullptr;
  Identity    slots_[kMaxIdentities];
};

}  // namespace simnode
