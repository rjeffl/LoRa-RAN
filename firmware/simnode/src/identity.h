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
#include "lran/schema/node_config_v1.h"
#include "lran/schema/gatelink_event_v1.h"
#include "lran/schema/gatelink_status_v1.h"
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

// ---------------------------------------------------------------------------
// ROLE_GATELINK state (BF-6). Every identity carries it; only ROLE_GATELINK reads it.
// ---------------------------------------------------------------------------

// spec 7.4 / 3.1 - a CONFIG_ACK result entry for a u32 is 9 bytes and the whole ACK must fit
// kMaxSchemaPayload: (196 - 3) / 9 = 21, the figure spec 7.4 gives. A store any deeper could
// answer a GET_ALL that no CONFIG_ACK can carry.
inline constexpr size_t kConfigStoreDepth = 21;

// One parameter held by the generic RAM store (decided with the operator 2026-09-14). No
// param_id is invented here: they belong to /lib/lran-config/, which does not exist yet.
struct StoredParam {
  bool        used     = false;
  uint16_t    param_id = 0;
  lran::PType ptype    = lran::PType::U8;
  uint8_t     len      = 0;
  uint8_t     value[lran::schema::kMaxParamValueLen] = {0, 0, 0, 0};
};

// What follows a command's COMMAND_ACK.
enum class AfterAck : uint8_t { None, Status, ConfigReadback, Reboot };

// A command dispatched and not yet acknowledged - `ack <hex> delay <ms>` stretches this into
// the spec 9.4 execution window, where a retry is in flight and receives nothing.
struct PendingAck {
  bool            active   = false;
  lran::NodeId    peer     = 0;
  lran::Seq       seq      = 0;
  uint8_t         cmd      = 0;
  lran::AckResult result   = lran::AckResult::Accepted;
  uint8_t         detail   = 0;
  AfterAck        after    = AfterAck::None;
  uint32_t        start_ms = 0;
  uint32_t        delay_ms = 0;
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
  PendingAck pending;

  bool     dry_run     = false;  // SET_RELAY_DRY_RUN
  bool     bms_polling = true;   // SET_BMS_POLLING
  uint16_t debug_modes = 0;      // SET_DEBUG_MODE

  // Local diagnostics, not spec 14.1 counters. `actuations` is what a relay would have
  // pulsed: cmd_replay's assertion is that a replay leaves it unchanged.
  uint32_t executions      = 0;
  uint32_t actuations      = 0;
  uint32_t acks_suppressed = 0;
  uint32_t ctx_rejects_forced = 0;  // ctx_reject fault, spec 10.3

  StoredParam params[kConfigStoreDepth];
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

  // The `silent` fault (Impl Plan 10.5): answers still to withhold, and answers withheld.
  // Local, like `unhandled`: the frames that went unanswered were valid.
  uint16_t silent_left        = 0;
  uint32_t answers_suppressed = 0;

  GateLinkState gl;
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

  // spec 10.6 - a context roll, which is much less than a reboot. A new random ctx_id that
  // differs from the current one, the gate's cache and mark cleared, and the status seq
  // back to 1. Configuration, the actuator state, the counters and the reassembler survive.
  bool roll_context(lran::NodeId id);

  // The key of simnode `id` (0xF0-0xF3), whether or not this board holds that identity, so
  // an authenticated fault can be signed for a simnode on another board. Refuses every other
  // id: this is a bench tool, and it signs for no production node.
  bool derive_simnode_key(lran::NodeId id, uint8_t out[lran::kNodeKeyLen]) const;

 private:
  lran::CtxId random_ctx();
  void        clear(Identity& e);

  uint8_t     master_[lran::kMasterKeyLen] = {0};
  lran::IKdf* kdf_                         = nullptr;
  RandomFn    random_                      = nullptr;
  Identity    slots_[kMaxIdentities];
};

}  // namespace simnode
