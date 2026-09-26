// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The command path and its retry. Task BF-18; Impl Plan 6.2; spec 6.2, 6.3, 10.2-10.5.

#include "command.h"

namespace bridge {

// SPEC 10.5's WRAP IS NOT HERE. `seq` is allocated by the registry (take_cmd_seq), so
// the wrap lives with the counter it wraps. The only comparison this file makes is
// equality, on `ack_seq` against the command in flight, and equality needs no ordering
// - which is why RFC 1982 arithmetic has no consumer in this file.

// ---------------------------------------------------------------------------
// Capability pre-filter - Impl Plan 6.2 step 1.
// ---------------------------------------------------------------------------

bool command_allowed(NodeType type, uint8_t cmd) {
  switch (static_cast<lran::Cmd>(cmd)) {
    // Node-local, and every node type answers them (spec 8.1, 0x10+).
    case lran::Cmd::Nop:
    case lran::Cmd::RequestStatus:
    case lran::Cmd::RequestConfig:
    case lran::Cmd::SetDebugMode:
    case lran::Cmd::Reboot:
      return true;

    // Actuation - spec 8.1's 0x00-0x0F, the only values that reach a physical output.
    // GateLink has the relays; WellLink does not.
    //
    // A SIMNODE IS ALLOWED EVERY COMMAND, and that is not a hole in the filter. A
    // bench identity takes a ROLE at runtime (Impl Plan 10.2) and the bridge cannot
    // know from the address which one - 0xF1 is ROLE_GATELINK today and was
    // ROLE_RANGE before BF-6. Refusing it here would mean the bench exercises a
    // different code path from the fleet, which this node's CLAUDE.md names as the
    // thing that defeats having bench nodes at all. They are gated at PUBLICATION
    // (spec 16.6, BF-26), never at the radio and not here.
    case lran::Cmd::Open:
    case lran::Cmd::Close:
    case lran::Cmd::HoldOpen:
    case lran::Cmd::ReleaseHold:
    case lran::Cmd::SetRelayDryRun:
      return type == NodeType::GateLink || type == NodeType::Simnode;

    // GateLink's BMS. WellLink's battery is a different part with no BLE link
    // (System PRD); a node that does not poll a BMS has nothing to switch off.
    case lran::Cmd::SetBmsPolling:
      return type == NodeType::GateLink || type == NodeType::Simnode;

    // spec 10.6 - the bridge sends a roll on its own after it boots, and nothing else may.
    // A roll requested from Home Assistant would move a node to a context the bridge has
    // not adopted, and every command after it would draw REJECTED_CTX.
    case lran::Cmd::RollContext:
      return false;
  }
  // An unrecognized value is refused here rather than sent for the node to refuse.
  // Spec 8.1 is a closed table and a value outside it is a bridge-side defect or a
  // hand-typed topic, neither of which is worth a transmission to a solar node.
  return false;
}

// ---------------------------------------------------------------------------
// The state machine.
// ---------------------------------------------------------------------------

bool CommandPath::submit(const CommandRequest& req, lran::CtxId ctx, lran::Seq seq,
                         uint32_t now_ms) {
  if (phase_ != Phase::Idle) {
    ++stats_.refused_busy;
    return false;
  }
  req_              = req;
  ctx_              = ctx;
  seq_              = seq;
  attempt_          = 0;
  resync_used_      = false;
  ctx_adopted_      = false;
  outcome_          = CmdOutcome::Pending;
  result_           = 0;
  detail_           = 0;
  window_opened_ms_ = now_ms;
  phase_            = Phase::SendDue;
  ++stats_.submitted;
  return true;
}

CmdStep CommandPath::next(uint32_t now_ms) {
  CmdStep st;
  switch (phase_) {
    case Phase::Idle:
      return st;

    case Phase::SendDue:
      st.action      = CmdAction::Send;
      st.dst         = req_.dst;
      st.cmd         = req_.cmd;
      st.arg         = req_.arg;
      st.arg2        = req_.arg2;
      st.seq         = seq_;
      st.ctx_id      = ctx_;
      st.attempt     = attempt_;
      st.ctx_adopted = ctx_adopted_;
      return st;

    case Phase::AwaitingAck: {
      // Unsigned-wrap-safe, like wifi_link.cpp and net_policy.cpp: millis() wraps at
      // ~49.7 days and this node is expected to run for years.
      const uint32_t window = ack_timeout_ms_ + (attempt_ > 0 ? kCommandRetryBackoffMs : 0);
      if (static_cast<int32_t>(now_ms - window_opened_ms_) < static_cast<int32_t>(window)) {
        return st;  // still open
      }
      if (attempt_ >= retries_) {
        // Spec 6.2 - exhausted. Publish failure and do NOT keep trying. The command
        // may well have executed: the node's rx_dup_command is what tells a lost ACK
        // apart from a lost link (Impl Plan 6.2), and the bridge cannot see it.
        outcome_ = CmdOutcome::NoAck;
        phase_   = Phase::Resolved;
        ++stats_.no_ack;
        return next(now_ms);
      }
      ++attempt_;
      phase_ = Phase::SendDue;
      return next(now_ms);
    }

    case Phase::Resolved:
      st.action  = CmdAction::Resolve;
      st.dst     = req_.dst;
      st.seq     = seq_;
      st.attempt = attempt_;
      st.outcome = outcome_;
      st.result  = result_;
      st.detail  = detail_;
      phase_     = Phase::Idle;
      return st;
  }
  return st;
}

void CommandPath::on_sent(uint32_t now_ms) {
  if (phase_ != Phase::SendDue) return;
  window_opened_ms_ = now_ms;
  phase_            = Phase::AwaitingAck;
  ctx_adopted_      = false;  // consumed by the Send the caller just made
  ++stats_.sent;
  if (attempt_ > 0) ++stats_.retries;
}

void CommandPath::on_aired(uint32_t aired_ms) {
  if (phase_ == Phase::AwaitingAck) window_opened_ms_ = aired_ms;
}

void CommandPath::on_ack(lran::NodeId src, const lran::msg::CommandAck& ack,
                         lran::CtxId ack_ctx, uint32_t now_ms) {
  // An ACK for a command that is not in flight, from a node that is not the one
  // addressed, or carrying another `seq`. Counted, never silent (root rule 4's
  // intent), and never acted on: the retry reuses one `seq`, so an ack_seq that does
  // not match is either late from a previous command or not ours at all.
  if (phase_ != Phase::AwaitingAck || src != req_.dst || ack.ack_seq != seq_) {
    ++stats_.ack_ignored;
    return;
  }

  if (static_cast<lran::AckResult>(ack.result) == lran::AckResult::RejectedCtx) {
    if (resync_used_) {
      // Spec 10.3 step 3 - a second REJECTED_CTX stops the command. Looping here is
      // a transmit storm on a channel shared with every other node.
      outcome_ = CmdOutcome::ResyncFailed;
      result_  = ack.result;
      detail_  = ack.detail;
      phase_   = Phase::Resolved;
      ++stats_.resync_failed;
      return;
    }
    // Spec 10.3 step 2 - adopt the ctx_id the ACK carried, reset this node's command
    // seq to 1, and retry the original command ONCE. The retry is a fresh attempt
    // against a fresh sequence space, so `attempt_` restarts: the exhaustion budget
    // is per context, and spending the pre-resync attempts against the new context
    // would give the resync fewer tries the later it happened.
    ctx_              = ack_ctx;
    seq_              = 1;
    attempt_          = 0;
    resync_used_      = true;
    ctx_adopted_      = true;
    window_opened_ms_ = now_ms;
    phase_            = Phase::SendDue;
    ++stats_.resyncs;
    return;
  }

  // Everything else ends the command, ACCEPTED and every rejection alike. The bridge
  // does not retry a node's considered answer: a REJECTED_ARG is still wrong on the
  // second attempt, and a DUPLICATE_CACHED is the node saying it already ran this
  // one - `detail` carries the cached result (spec 6.3) and is what the publication
  // reports, because answering a dedup hit with detail 0 would report a retry as a
  // result-less success.
  outcome_ = CmdOutcome::Acked;
  result_  = ack.result;
  detail_  = ack.detail;
  phase_   = Phase::Resolved;
  ++stats_.acked;
}

// ---------------------------------------------------------------------------
// Frame construction.
// ---------------------------------------------------------------------------

size_t build_command_frame(lran::NodeId dst, lran::CtxId ctx, lran::Seq seq, uint8_t ver,
                           const lran::msg::Command& cmd, const lran::EncodeCtx& ectx,
                           uint8_t* buf, size_t cap) {
  lran::Header h;
  h.ver    = ver;  // R-3.1e - the version last heard from this node (BF-22)
  h.type   = lran::MsgType::Command;
  h.src    = lran::kNodeBridge;
  h.dst    = dst;
  h.seq    = seq;
  h.ctx_id = ctx;  // spec 10.1 - the DESTINATION's context, never one of the bridge's
  h.schema = lran::kSchemaNone;

  uint8_t payload[lran::msg::kCommandLen];
  size_t  plen = 0;
  if (lran::msg::serialize(cmd, payload, sizeof(payload), &plen) != lran::Status::Ok) {
    return 0;
  }

  size_t len = 0;
  return lran::encode(h, payload, plen, ectx, buf, cap, &len) == lran::Status::Ok ? len : 0;
}

}  // namespace bridge
