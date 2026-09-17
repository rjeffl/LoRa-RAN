// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The fault catalogue and its injector. Task BF-8; Impl Plan 10.5, 10.5.1, 10.5.2, 10.6.
//
// ARDUINO-FREE, so every fault's frames are host-tested against the codec's receive path.
//
// EVERY MALFORMED FRAME COMES FROM lran::sim::FramePatch (10.6 rule 1): a correct frame from
// the real encoder, one stated patch, an explicit reseal. Faults that are sequences of
// correct frames (frag_dup, set_displaced, seq_wrap...) use encode() and nothing else.
//
// THE MODEL, decided with the operator 2026-09-14. `fault <hex> <name> [count]` ARMS the
// fault on one identity for `count` injections. The first fires at once; the rest fire as
// the outbox has room and the gap allows, and then the fault disarms itself (10.6 rule 2).
// An injection is the whole frame sequence the catalogue row describes - bad_ver is two
// frames, set_displaced five. Three are behaviour faults and send nothing: `silent` withholds
// the identity's next `count` answers, and `ack_suppress` and `ack_dup` (BF-6) act on its next
// `count` fresh COMMAND_ACKs. All three live on the identity, where the receive path sees them.
//
// THE CARRIER FRAME is schema 0xF0 node health, the status a simnode really sends, so every
// fault differs from a frame the receiver would accept in exactly the way its row says.
// Authenticated faults carry COMMAND(NOP): if a receiver defect ever accepts one, nothing
// moves.
//
// cmd_replay AND cmd_stale_seq POINT THE OTHER WAY (Impl Plan 10.5.1). They test a simnode's
// OWN CommandGate, so the frames are COMMANDs as the bridge would send them: src 00, the
// target's key, the target's ctx. A target on this board - the arming identity by default,
// or `to <hex>` naming another - is fed through Node::on_rx and never transmitted (decided
// with the operator 2026-09-14). A target on another board goes over the air and needs its
// ctx_id from `ctx <hex32>`.

#pragma once

#include <cstddef>
#include <cstdint>

#include "identity.h"
#include "lran/counters.h"
#include "lran/mac.h"
#include "lran/types.h"
#include "node.h"
#include "sink.h"

namespace simnode {

enum class FaultId : uint8_t {
  // 10.5 - one frame, patched.
  Runt, Oversize, BadCrc, BadVer, WrongDst, CritExt, HdrRsv, FragZero, UnknownType,
  UnknownSchema, BadLength, FragCommand, BadMac,
  // 10.5 - sequences of fragments.
  FragTimeout, FragOverflow, FragOversize, FragDup, FragLate, SingleFrameInterleave,
  SetDisplaced,
  // 10.5 - context and sequence.
  CtxJump, SeqJump, SeqWrap,
  // 10.5 - behaviour.
  Flood, Silent, AckSuppress, AckDup, EventReplay, CtxReject,
  // 10.5.1 - the node's own command gate.
  CmdReplay, CmdStaleSeq,
  // 10.5.2 - named so the console can say why it cannot exist.
  BadPhyCrc,
};

struct FaultInfo {
  const char* name;  // the exact console token, as in Impl Plan 10.5
  FaultId     id;
  uint8_t     frames;  // frames one injection queues; 0 for a behaviour fault
  // The spec 14.1 counter the receiver should move, or nullptr. The name is read from
  // kCounterRegistry, never spelled here (counters.h).
  uint32_t lran::Counters::* counter;
  const char* expect;     // what else the receiver should do, in a few words
  const char* waits_for;  // the task that brings it, or nullptr when built
  uint32_t    gap_ms;     // default minimum time between injections
};

extern const FaultInfo kFaultCatalogue[];
extern const size_t    kFaultCatalogueLen;

const FaultInfo* find_fault(const char* name);

// The spec 14.1 name of a Counters field, or "-".
const char* counter_name(uint32_t lran::Counters::* field);

// Fragment chunk for the fragment faults: 0xF0's 20 bytes in three frames (7, 7, 6).
inline constexpr uint8_t kFaultChunk = 7;

// frag_timeout's injections must be further apart than the receiver's reassembly timeout
// (spec 11.2, default 5000 ms), or the next fragment 0 displaces the set instead of letting
// it expire. Overridable per arm with `gap`.
inline constexpr uint32_t kFragTimeoutGapMs = 6000;

enum class FaultResult : uint8_t {
  Ok,
  NoIdentity,
  Disabled,
  UnknownFault,
  WaitsForTask,   // in the catalogue, not built yet
  NotInjectable,  // bad_phy_crc
  BadCount,
  WrongRole,      // needs ROLE_GATELINK on the identity, or on the command target
  NeedsCtx,       // a command target on another board needs `ctx <hex32>`
  BadTarget,      // a command target that is not a simnode, f0-f3
};
const char* fault_result_name(FaultResult r);

struct FaultRequest {
  lran::NodeId dst     = lran::kNodeBridge;
  uint16_t     count   = 1;
  bool         has_gap = false;
  uint32_t     gap_ms  = 0;
  // The ctx_id an authenticated fault carries. A node checks ctx before the MAC (spec 9.4
  // step 2), so bad_mac aimed at another simnode needs that identity's ctx - read it from
  // `id list` on its board. Without it the fault carries the sender's own ctx, which is
  // right for the bridge: the bridge checks no ctx of its own.
  bool        has_ctx = false;
  lran::CtxId ctx     = 0;
  // cmd_replay / cmd_stale_seq to another board: the first command seq. That board's
  // high-water mark is not visible from here, so the operator reads `cmd_hw` from its
  // `id list`. Later injections advance from it. A target on this board ignores it.
  bool      has_seq = false;
  lran::Seq seq     = 0;
};

struct ArmedFault {
  bool             active    = false;
  lran::NodeId     id        = 0;  // the identity it was armed on; a re-added slot disarms
  const FaultInfo* info      = nullptr;
  FaultRequest     req;
  uint16_t         left      = 0;
  uint16_t         fired     = 0;
  uint32_t         last_ms   = 0;
};

class FaultInjector {
 public:
  FaultInjector(IdentityTable* ids, Outbox* out, Node* node, lran::IMac* mac, Sink* log);

  // Arms `name` on identity `id` and fires the first injection if the outbox has room. A
  // fault already armed on that identity is replaced, and the log says so.
  FaultResult arm(lran::NodeId id, const char* name, const FaultRequest& req, uint32_t now_ms);

  // Disarms any fault on `id`, including `silent`. False if nothing was armed.
  bool disarm(lran::NodeId id);

  // Fires armed faults whose gap has passed and whose frames fit. Call every loop.
  void tick(uint32_t now_ms);

  // What is armed on `id`, or nullptr. `silent` is reported through Identity::silent_left.
  const ArmedFault* armed(lran::NodeId id) const;

  uint32_t injections() const { return injections_; }

 private:
  bool fire(ArmedFault& a, Identity& e, uint32_t now_ms);
  bool build(const ArmedFault& a, Identity& e, uint32_t now_ms);

  // Frame builders. Each appends to stage_ and returns false if an encode or patch failed,
  // which would be a defect in this file, and is logged.
  bool add_command(Identity& e, const FaultRequest& r, bool frag, bool bad_mac);
  bool add_fragments(Identity& e, const FaultRequest& r, lran::Seq seq, const uint8_t* payload,
                     size_t len, const uint8_t* order, size_t order_len);
  bool push_stage(const uint8_t* frame, size_t len);

  lran::Header status_header(const Identity& e, const FaultRequest& r) const;

  // cmd_replay / cmd_stale_seq. A COMMAND from src 00 to `target`, signed with its key. `local`
  // is the target when it is on this board, which supplies ctx and ver; else r.ctx.
  bool        add_bridge_command(const Identity* local, lran::NodeId target, const FaultRequest& r,
                                 lran::Cmd cmd, lran::Seq seq);
  FaultResult check_command_target(const Identity& e, const FaultRequest& r) const;

  bool      loopback_   = false;  // this injection is fed to Node::on_rx, not the outbox
  lran::Seq remote_seq_ = 1;      // the next remote command seq when `seq` was not given

  IdentityTable* ids_;
  Outbox*        out_;
  Node*          node_;
  lran::IMac*    mac_;
  Sink*          log_;

  ArmedFault armed_[kMaxIdentities];

  // A whole injection is built here before any of it is queued, so a sequence is queued
  // entire or not at all. Static storage: 16 frames of 255 bytes is too much for a stack.
  uint8_t stage_[kOutboxDepth][kOutFrameMax] = {};
  size_t  stage_len_[kOutboxDepth]           = {};
  size_t  staged_                            = 0;

  uint32_t injections_ = 0;
};

}  // namespace simnode
