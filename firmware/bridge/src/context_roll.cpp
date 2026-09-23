// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The context roll after a bridge restart. Task BF-34; see context_roll.h.

#include "context_roll.h"

namespace bridge {

uint32_t node_bit(lran::NodeId id) {
  for (size_t i = 0; i < kNodeCount; ++i) {
    if (kNodeTable[i].id == id) return 1u << i;
  }
  return 0;
}

void ContextRoll::on_heard(lran::NodeId id) {
  const uint32_t bit = node_bit(id);
  if ((pending_ & bit) == 0) return;
  if (busy() && dst_ == id) return;
  due_ |= bit;
}

bool ContextRoll::next_due(lran::NodeId* out) const {
  for (size_t i = 0; i < kNodeCount; ++i) {
    if ((due_ & pending_ & (1u << i)) != 0) {
      *out = kNodeTable[i].id;
      return true;
    }
  }
  return false;
}

bool ContextRoll::submit(lran::NodeId id, lran::CtxId ctx, lran::Seq seq, uint32_t now_ms) {
  if (busy() || !pending(id) || ctx == 0) return false;
  due_ &= ~node_bit(id);
  dst_              = id;
  ctx_              = ctx;
  seq_              = seq;
  attempt_          = 0;
  outcome_          = RollOutcome::Pending;
  no_ack_           = false;
  result_           = 0;
  window_opened_ms_ = now_ms;
  phase_            = Phase::SendDue;
  return true;
}

RollStep ContextRoll::next(uint32_t now_ms) {
  RollStep st;
  switch (phase_) {
    case Phase::Idle:
      return st;

    case Phase::SendDue:
      st.action  = RollAction::Send;
      st.dst     = dst_;
      st.seq     = seq_;
      st.ctx_id  = ctx_;
      st.attempt = attempt_;
      return st;

    case Phase::AwaitingAck: {
      // Unsigned-wrap-safe, as command.cpp is: millis() wraps at ~49.7 days.
      const uint32_t window = ack_timeout_ms_ + (attempt_ > 0 ? kCommandRetryBackoffMs : 0);
      if (static_cast<int32_t>(now_ms - window_opened_ms_) < static_cast<int32_t>(window)) {
        return st;
      }
      if (attempt_ >= retries_) {
        resolve_failed(/*no_ack=*/true, 0);
        return next(now_ms);
      }
      ++attempt_;
      phase_ = Phase::SendDue;
      return next(now_ms);
    }

    case Phase::Resolved:
      st.action  = RollAction::Resolve;
      st.dst     = dst_;
      st.seq     = seq_;
      st.ctx_id  = ctx_;
      st.attempt = attempt_;
      st.outcome = outcome_;
      st.no_ack  = no_ack_;
      st.result  = result_;
      phase_     = Phase::Idle;
      return st;
  }
  return st;
}

void ContextRoll::on_sent(uint32_t now_ms) {
  if (phase_ != Phase::SendDue) return;
  window_opened_ms_ = now_ms;
  phase_            = Phase::AwaitingAck;
  ++stats_.sent;
  if (attempt_ > 0) ++stats_.retries;
}

bool ContextRoll::on_ack(lran::NodeId src, const lran::msg::CommandAck& ack,
                         lran::CtxId ack_ctx, uint32_t now_ms) {
  if (phase_ != Phase::AwaitingAck || src != dst_ || ack.ack_seq != seq_) return false;

  switch (static_cast<lran::AckResult>(ack.result)) {
    case lran::AckResult::Accepted:
      // spec 10.6 bridge step 3 - the ACK's header carries the NEW context.
      resolve_rolled(ack_ctx);
      return true;

    case lran::AckResult::RejectedCtx:
      // spec 10.6 bridge step 4 - the node rolled and its ACK was lost, so this retry
      // carried the old context. The header carries the node's current one, which is the
      // context the roll produced. Not a resync: it does not count toward spec 10.3 step
      // 3's limit, and the command path's resync is never involved.
      ++stats_.by_rejected_ctx;
      resolve_rolled(ack_ctx);
      return true;

    case lran::AckResult::ActuatorBusy:
      // spec 10.6 bridge step 5 - a command sent before the bridge restarted is still
      // executing. Retried under the same seq, after the window the timeout path already
      // waits: the node sends nothing more, and a retry at once would find it still busy.
      // Each BUSY spends an attempt, so a node stuck busy ends in ctx_roll_failed rather
      // than holding the command path off forever.
      ++stats_.busy;
      window_opened_ms_ = now_ms;
      return true;

    default:
      // spec 10.6 bridge step 6. REJECTED_UNKNOWN_CMD included: a node without
      // ROLL_CONTEXT is a fault here, and the bridge does not fall back to commanding it.
      resolve_failed(/*no_ack=*/false, ack.result);
      return true;
  }
}

void ContextRoll::resolve_rolled(lran::CtxId ctx) {
  pending_ &= ~node_bit(dst_);
  due_ &= ~node_bit(dst_);
  ctx_     = ctx;
  outcome_ = RollOutcome::Rolled;
  phase_   = Phase::Resolved;
  ++stats_.ctx_rolls;
}

void ContextRoll::resolve_failed(bool no_ack, uint8_t result) {
  // Still pending, and not due: spec 10.6 bridge step 6 rolls again the next time the
  // node is heard, which paces a node that cannot roll at its own transmit rate.
  outcome_ = RollOutcome::Failed;
  no_ack_  = no_ack;
  result_  = result;
  phase_   = Phase::Resolved;
  ++stats_.ctx_roll_failed;
}

}  // namespace bridge
