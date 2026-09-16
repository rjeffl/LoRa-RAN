// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The command path and its retry. Task BF-18; Impl Plan 6.2; PRD BS-3; spec 6.2, 6.3,
// 10.2-10.5.
//
// ARDUINO-FREE, AND IT DOES NO I/O, like scheduler.h and for the same reason. next()
// says what to transmit or what to report; the caller builds the frame, queues it and
// reports back with on_sent(). A queue that refused the frame is simply not reported,
// and the attempt is retried on the next tick.
//
// ROOT RULE 2 LIVES HERE. The retry reuses the same `seq` (BS-3, spec 10.4), so the
// node's (ctx_id, seq) dedup returns the cached ACK instead of pulsing the relay a
// second time. `seq` is taken once in submit() and nothing advances it afterwards -
// there is deliberately no call on this class that changes it mid-command.
//
// ONE COMMAND IN FLIGHT ACROSS THE FLEET. No document requires this; it is decided
// here and raised in the engineering log. Two reasons, one of them airtime and the
// weaker of the two: a command occupies the channel for ~0.6 s at SF9 and the poll
// scheduler already serializes for that reason (R-3.1d). The real reason is the
// resync in spec 10.3, which resets a node's command `seq` to 1 - a second command
// in flight to the same node during a resync would carry a `seq` from the old space
// and be refused as a replay, which reads at the bridge as a node fault. Serializing
// is the cheap way to make that unrepresentable at a cadence of a gate command a day.
//
// Not thread-safe. task_runtime.cpp holds one instance behind a mutex, because
// sched_task sends and app_task hears the ACK.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/codec.h"
#include "lran/messages.h"
#include "lran/types.h"
#include "registry.h"

namespace bridge {

// ---------------------------------------------------------------------------
// Timing and retry, both runtime-settable - root rule 8. A node that cannot be
// reflashed without a walk to the gate gets no timing constant fixed at compile time.
// ---------------------------------------------------------------------------

// Impl Plan 6.2. Long enough for a node that backed off the full media-access
// sequence: spec 12.3's cad_retries x backoff_max_ms is 7500 ms at the Envelope A
// defaults, which is LONGER than this. That is deliberate and is not the poll
// window's bargain (scheduler.h chose 10 s to cover it). A command is retried with
// the same `seq` and the node's dedup absorbs the repeat, so a timeout here costs an
// extra transmission, not a second pulse - and a 3 s first retry recovers a genuinely
// lost frame far sooner than a 10 s one. The exhaustion budget is what matters, and
// it is cmd_retries x this plus backoff, ~10 s (Impl Plan 6.2).
inline constexpr uint32_t kCommandAckTimeoutDefaultMs = 3000;

// Impl Plan 6.2 - three retries after the first attempt, so four transmissions.
inline constexpr uint8_t kCommandRetriesDefault = 3;

// Backoff between retries, added to the timeout. Flat rather than exponential: the
// whole budget is ~10 s and an exponential sequence inside it would spend its last
// step on a node that is already being given up on.
inline constexpr uint32_t kCommandRetryBackoffMs = 500;

// ---------------------------------------------------------------------------
// What Home Assistant asked for. mqtt_task parses `lran/<node>/cmd/<action>/set`
// into one of these and queues it; sched_task runs it.
// ---------------------------------------------------------------------------
struct CommandRequest {
  lran::NodeId dst  = 0;
  uint8_t      cmd  = 0;  // spec 8.1
  uint8_t      arg  = 0;
  uint16_t     arg2 = 0;
};

// Impl Plan 6.2's first step - "validate against the target node's capability set".
// False for a `cmd` this node type does not implement, checked at the bridge so a
// solar node is not woken to answer REJECTED_NOT_SUPPORTED (spec 8.2 0x06) over a
// link that cost it a transmission.
//
// This is a bridge-side pre-filter and NOT a substitute for the node's own check. The
// node is the authority on what it implements; this only spares the airtime.
bool command_allowed(NodeType type, uint8_t cmd);

// ---------------------------------------------------------------------------
// The state machine's output.
// ---------------------------------------------------------------------------

enum class CmdAction : uint8_t {
  None,     // nothing to do this step
  Send,     // build a COMMAND from the step's fields, queue it, then call on_sent()
  Resolve,  // the command is over; publish `outcome`, `result` and `detail`
};

// How a command ended. One value per case a reader of `lran/<node>/cmd/ack` has to
// tell apart - and `NoAck` is deliberately NOT reported as a rejection: spec 6.2's
// ACK never arrived, so nothing is known about whether the node executed.
enum class CmdOutcome : uint8_t {
  Pending,       // not finished
  Acked,         // a COMMAND_ACK arrived; `result` and `detail` are the node's (spec 8.2)
  NoAck,         // every attempt timed out. The command MAY have executed
  ResyncFailed,  // a second REJECTED_CTX - spec 10.3 step 3, stop rather than loop
};

struct CmdStep {
  CmdAction    action = CmdAction::None;
  lran::NodeId dst    = 0;

  // Send: the frame's fields. `seq` is the SAME on every attempt (root rule 2).
  uint8_t     cmd     = 0;
  uint8_t     arg     = 0;
  uint16_t    arg2    = 0;
  lran::Seq   seq     = 0;
  lran::CtxId ctx_id  = 0;
  uint8_t     attempt = 0;  // 0 for the first transmission; diagnostics only

  // Send, after a resync: `ctx_id` above was adopted from a REJECTED_CTX and the
  // caller must write it and `seq` back to the registry (spec 10.3 step 2) so the
  // NEXT command starts in the node's current context rather than repeating this one.
  bool ctx_adopted = false;

  // Resolve: how it ended.
  CmdOutcome outcome = CmdOutcome::Pending;
  uint8_t    result  = 0;  // spec 8.2, valid when outcome is Acked
  uint8_t    detail  = 0;  // spec 6.3; the CACHED result when result is DUPLICATE_CACHED
};

struct CommandStats {
  uint32_t submitted     = 0;
  uint32_t refused_busy  = 0;  // one in flight already
  uint32_t sent          = 0;  // transmissions, retries included
  uint32_t retries       = 0;  // sent, less the first attempt of each command
  uint32_t acked         = 0;
  uint32_t no_ack        = 0;
  uint32_t resyncs       = 0;  // REJECTED_CTX adopted and retried once (spec 10.3)
  uint32_t resync_failed = 0;  // a second REJECTED_CTX
  uint32_t ack_ignored   = 0;  // wrong src, wrong ack_seq, or nothing in flight
};

class CommandPath {
 public:
  void     set_ack_timeout_ms(uint32_t ms) { ack_timeout_ms_ = ms; }
  uint32_t ack_timeout_ms() const { return ack_timeout_ms_; }
  void     set_retries(uint8_t n) { retries_ = n; }
  uint8_t  retries() const { return retries_; }

  bool         busy() const { return phase_ != Phase::Idle; }
  lran::NodeId in_flight_node() const { return req_.dst; }

  // Accept a request. False when one is already in flight, which the caller reports
  // rather than queueing behind: a gate command that waits an unbounded time is worse
  // than one refused while the operator is still looking at the dashboard.
  //
  // `ctx` and `seq` come from the registry - the node's learned context and the
  // bridge's next command seq for it (spec 10.2). The caller advances the registry's
  // cmd_seq; this class never does, because a retry must not take a second one.
  bool submit(const CommandRequest& req, lran::CtxId ctx, lran::Seq seq, uint32_t now_ms);

  // Call until it returns None. Emits Send when a transmission is due - the first one,
  // or a retry whose window has closed - and Resolve exactly once per command.
  CmdStep next(uint32_t now_ms);

  // The frame from the last Send is on the TX queue. The reply window starts now.
  void on_sent(uint32_t now_ms);

  // A COMMAND_ACK passed the receive ladder. `ack_ctx` is the ACK header's ctx_id,
  // which for a REJECTED_CTX is the node's own and is what the resync adopts
  // (spec 10.3 step 1). An ACK that matches nothing in flight is counted and ignored.
  void on_ack(lran::NodeId src, const lran::msg::CommandAck& ack, lran::CtxId ack_ctx,
              uint32_t now_ms);

  const CommandStats& stats() const { return stats_; }

 private:
  enum class Phase : uint8_t {
    Idle,
    SendDue,     // a transmission is due; next() will emit Send
    AwaitingAck, // sent, window open
    Resolved,    // finished; next() will emit Resolve and return to Idle
  };

  CommandRequest req_{};
  Phase          phase_   = Phase::Idle;
  lran::Seq      seq_     = 0;
  lran::CtxId    ctx_     = 0;
  uint8_t        attempt_ = 0;

  // Spec 10.3 step 3. One resync per command, then stop - a resync loop on a shared
  // channel is a transmit storm for every other node, not just this one.
  bool resync_used_  = false;
  bool ctx_adopted_  = false;  // set for the next Send only

  uint32_t   window_opened_ms_ = 0;
  CmdOutcome outcome_          = CmdOutcome::Pending;
  uint8_t    result_           = 0;
  uint8_t    detail_           = 0;

  uint32_t ack_timeout_ms_ = kCommandAckTimeoutDefaultMs;
  uint8_t  retries_        = kCommandRetriesDefault;

  CommandStats stats_;
};

// spec 6.2 - an authenticated COMMAND to `dst`, carrying THAT NODE's ctx_id (spec
// 10.1). `ectx` must carry the IMac and the node key; an EncodeCtx without them
// produces no frame rather than an unauthenticated one. Returns the frame length,
// or 0.
size_t build_command_frame(lran::NodeId dst, lran::CtxId ctx, lran::Seq seq,
                           const lran::msg::Command& cmd, const lran::EncodeCtx& ectx,
                           uint8_t* buf, size_t cap);

}  // namespace bridge
