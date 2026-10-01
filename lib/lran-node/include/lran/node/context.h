// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// What one node owns on the protocol side: its key, its context, its sequence spaces, its
// counters, its command gate and its reassembly state. Spec 9.4, 10.1-10.7, 11, 12.4.2.
// GateLink task L1, from the simnode's Identity.
//
// A board holds one Context per identity. GateLink holds one; the simnode holds up to four
// and derives its Identity from this struct, so each identity owns everything a physical
// node would.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/command_gate.h"
#include "lran/config.h"
#include "lran/counters.h"
#include "lran/mac.h"
#include "lran/reassembly.h"
#include "lran/types.h"

namespace lran::node {

// Any uniformly distributed uint32. esp_random() on the board.
using RandomFn = uint32_t (*)();

// What follows a command's COMMAND_ACK.
enum class AfterAck : uint8_t { None, Status, ConfigReadback, Reboot };

// A command that passed CommandGate::check() and has not been recorded. Spec 9.4's
// execution window: between check() and record() a retry is InFlight and gets no answer.
struct PendingCommand {
  bool      active   = false;
  NodeId    peer     = 0;
  Seq       seq      = 0;
  uint8_t   cmd      = 0;
  AckResult result   = AckResult::Accepted;
  uint8_t   detail   = 0;
  AfterAck  after    = AfterAck::None;
  uint32_t  start_ms = 0;
};

// spec 8.13 - the node's one HEX transaction. While it is active the node answers BUSY,
// and at due_ms it answers TIMEOUT.
struct HexPending {
  bool     active = false;
  uint32_t due_ms = 0;
  NodeId   peer   = 0;
  Seq      seq    = 0;
};

struct Context {
  Context() = default;

  // The gate and the reassembler hold a pointer to `counters` below. A copy would point
  // them at another context's counters.
  Context(const Context&)            = delete;
  Context& operator=(const Context&) = delete;

  NodeId  id        = 0;
  uint8_t proto_ver = kProtoVer;  // spec 13.1 - the one version this node accepts

  uint8_t key[kNodeKeyLen] = {0};  // spec 9.1

  CtxId ctx_id = 0;  // spec 10.1 - random, non-zero
  Seq   tx_seq = 1;  // spec 10.2 - this node's status seq space

  Counters    counters;
  CommandGate gate{&counters};         // spec 9.4 steps 4-5; its high-water is cmd_seq
  Reassembler reassembler{&counters};  // one peer: whoever addresses this node

  // The chunk the peer fragmented its current set with, inferred from the longest
  // fragment: spec 11.1 fixes every non-final fragment to one length. A fragmented PING is
  // echoed with the same chunk (spec 6.6.2).
  uint8_t rx_chunk = 0;

  bool    heard         = false;
  int16_t last_rssi_dbm = kI16NotAvailable;
  int16_t last_snr_db10 = kI16NotAvailable;

  PendingCommand pending;
  HexPending     hex_pending;

  // spec 12.4.2 step 8 - the PHY_REVERTED detail still owed to the bridge, 0 when none.
  // Sent ahead of the answer to the next frame from the bridge, then cleared.
  uint16_t phy_revert_detail = 0;

  // spec 8.7, D69 - a revert changed the effective configuration and no CONFIG_ACK said
  // so. Carried by the next poll's STATUS, then cleared.
  bool config_change_owed = false;

  // Local diagnostics, not spec 14.1 counters: the frames they count were valid.
  uint32_t unhandled    = 0;  // decoded, but nothing here answers that type
  uint32_t executions   = 0;  // COMMANDs and CONFIGs past the gate
  uint32_t hex_requests = 0;  // HEX_REQs forwarded or refused
};

// spec 10.1 - a non-zero ctx_id. Bounded, so an RNG that returns zero forever produces a
// fixed context rather than a hung board.
CtxId new_ctx_id(RandomFn random);

// spec 10.1, 10.3 - a reboot's context: a new ctx_id, status seq 1, the gate's cache and
// mark cleared, reassembly dropped, and any command or HEX transaction forgotten.
// Counters and the owed PHY report survive; the caller clears what else a reboot loses.
void reset_context(Context& c, RandomFn random);

// spec 10.6 - a roll, much less than a reboot. A ctx_id different from the current one,
// the gate's cache and mark cleared, status seq 1. Everything else survives.
void roll_context(Context& c, RandomFn random);

}  // namespace lran::node
