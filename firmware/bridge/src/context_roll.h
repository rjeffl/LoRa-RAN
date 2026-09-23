// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The context roll after a bridge restart. Task BF-34; spec 10.6; D58; PRD R-3.1h.
//
// WHY IT EXISTS. A restart resets the bridge's command `seq` to 1, and a node that did not
// restart still holds its dedup cache and high-water mark under the same ctx_id. The
// bridge's first command would draw DUPLICATE_CACHED, whose `detail` is an EARLIER
// command's result: the new command is reported as acknowledged and never runs. So every
// node starts pending, is sent ROLL_CONTEXT when the bridge first hears it, and is sent
// no COMMAND and no CONFIG until the roll completes.
//
// ARDUINO-FREE, AND IT DOES NO I/O, like command.h and for the same reason. next() says
// what to transmit or what to report; the caller builds the frame, queues it and reports
// back with on_sent().
//
// ROOT RULE 2 HOLDS HERE TOO. A retry of the roll reuses its `seq` (spec 10.6 bridge step
// 2). The node does not check that seq, but a roll retried under a new one would be a
// second rule for one frame type, and nothing is gained by it.
//
// A FAILED ROLL STAYS PENDING. The bridge never falls back to commanding a node whose
// context it could not move, because that is the state in which a command is
// acknowledged and not run (spec 10.6 bridge step 6).
//
// Not thread-safe. task_runtime.cpp holds one instance under the scheduler's lock, as it
// holds CommandPath, because sched_task sends and app_task hears the ACK.

#pragma once

#include <cstddef>
#include <cstdint>

#include "command.h"
#include "lran/messages.h"
#include "lran/types.h"
#include "registry.h"

namespace bridge {

// One bit per kNodeTable row, in table order.
static_assert(kNodeCount <= 32, "one pending bit per registry row");
inline constexpr uint32_t kAllNodesMask =
    kNodeCount == 32 ? 0xFFFFFFFFu : ((1u << kNodeCount) - 1u);

// The bit for `id`, or 0 for a node the table does not carry.
uint32_t node_bit(lran::NodeId id);

enum class RollAction : uint8_t {
  None,     // nothing to do this step
  Send,     // build a ROLL_CONTEXT from the step's fields, queue it, then call on_sent()
  Resolve,  // the roll is over; see `outcome`
};

enum class RollOutcome : uint8_t {
  Pending,
  Rolled,  // spec 10.6 bridge steps 3-4. Adopt `ctx_id` and reset the node's command seq
  Failed,  // spec 10.6 bridge step 6. Still pending; rolled again when next heard
};

struct RollStep {
  RollAction   action = RollAction::None;
  lran::NodeId dst    = 0;

  // Send: the frame's fields. `seq` is the SAME on every attempt.
  lran::Seq   seq     = 0;
  lran::CtxId ctx_id  = 0;  // Send: the node's current context. Resolve(Rolled): its new one
  uint8_t     attempt = 0;  // 0 for the first transmission

  // Resolve.
  RollOutcome outcome = RollOutcome::Pending;
  bool        no_ack  = false;  // Failed because every attempt timed out
  uint8_t     result  = 0;      // Failed on an answer: the node's spec 8.2 result
};

struct RollStats {
  // Spec 14.1's two bridge counters. The names are normative (spec 14.1); diag_json.cpp
  // publishes them on lran/bridge/diag/state.
  uint32_t ctx_rolls       = 0;  // a node's roll completed
  uint32_t ctx_roll_failed = 0;  // retries ran out, or an answer other than the three

  // The bridge's own detail, published beside the command path's.
  uint32_t sent            = 0;  // transmissions, retries included
  uint32_t retries         = 0;
  uint32_t busy            = 0;  // ACTUATOR_BUSY answers, each retried under the same seq
  uint32_t by_rejected_ctx = 0;  // rolls completed by spec 10.6 bridge step 4
  uint32_t cmd_refused     = 0;  // command requests refused because the roll was pending
};

class ContextRoll {
 public:
  // Every registry row starts pending: a bridge that has just booted knows nothing about
  // any node's context.
  ContextRoll() = default;

  // The command path's two levers, applied by sched_levers() (root rule 8). A roll is a
  // COMMAND on the air and is waited for like one.
  void     set_ack_timeout_ms(uint32_t ms) { ack_timeout_ms_ = ms; }
  uint32_t ack_timeout_ms() const { return ack_timeout_ms_; }
  void     set_retries(uint8_t n) { retries_ = n; }
  uint8_t  retries() const { return retries_; }

  // spec 10.6 bridge step 7 - nothing but the roll may reach a pending node.
  bool     pending(lran::NodeId id) const { return (pending_ & node_bit(id)) != 0; }
  uint32_t pending_mask() const { return pending_; }

  // spec 10.6 bridge step 2 - a frame from `id` passed the ladder. A pending node becomes
  // due for a roll; one already in flight is left alone, so the ACK that answers a roll
  // does not queue a second one before on_ack() has read it.
  void on_heard(lran::NodeId id);

  bool         busy() const { return phase_ != Phase::Idle; }
  lran::NodeId in_flight_node() const { return dst_; }

  // The due node to roll next, in table order. False when none is due.
  bool next_due(lran::NodeId* out) const;

  // Start the roll for `id`. `ctx` is the node's learned context, which the request must
  // carry (spec 9.4 step 2), and `seq` the bridge's next command seq for it. False when a
  // roll is already in flight, the node is not pending, or `ctx` is 0 - a context not yet
  // learned, under which the node would refuse the request at step 2.
  bool submit(lran::NodeId id, lran::CtxId ctx, lran::Seq seq, uint32_t now_ms);

  // Call until it returns None.
  RollStep next(uint32_t now_ms);

  // The frame from the last Send is on the TX queue. The reply window starts now.
  void on_sent(uint32_t now_ms);

  // A COMMAND_ACK passed the ladder. True when it answered the roll in flight and was
  // consumed here; false leaves it for the command path. An ACK for anything else is NOT
  // counted here, because CommandPath counts what neither claims.
  bool on_ack(lran::NodeId src, const lran::msg::CommandAck& ack, lran::CtxId ack_ctx,
              uint32_t now_ms);

  // The command path refused a request because its node's roll is pending.
  void note_cmd_refused() { ++stats_.cmd_refused; }

  const RollStats& stats() const { return stats_; }

 private:
  enum class Phase : uint8_t { Idle, SendDue, AwaitingAck, Resolved };

  void resolve_rolled(lran::CtxId ctx);
  void resolve_failed(bool no_ack, uint8_t result);

  uint32_t pending_ = kAllNodesMask;
  uint32_t due_     = 0;

  Phase        phase_   = Phase::Idle;
  lran::NodeId dst_     = 0;
  lran::CtxId  ctx_     = 0;
  lran::Seq    seq_     = 0;
  uint8_t      attempt_ = 0;

  uint32_t    window_opened_ms_ = 0;
  RollOutcome outcome_          = RollOutcome::Pending;
  bool        no_ack_           = false;
  uint8_t     result_           = 0;

  uint32_t ack_timeout_ms_ = kCommandAckTimeoutDefaultMs;
  uint8_t  retries_        = kCommandRetriesDefault;

  RollStats stats_;
};

}  // namespace bridge
