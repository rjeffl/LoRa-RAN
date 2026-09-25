// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-32; spec 7.4, 7.4.1.

#include "config_path.h"

#include "lran/schema/gatelink_status_v1.h"

namespace bridge {

bool ConfigPath::submit(const ConfigJob& job, lran::CtxId ctx, lran::Seq seq,
                        uint32_t now_ms) {
  if (phase_ != Phase::Idle) {
    ++stats_.refused_busy;
    return false;
  }
  job_               = job;
  ctx_               = ctx;
  seq_               = seq;
  outcome_           = ConfigOutcome::Pending;
  persist_           = AckPersist::Unknown;
  staged_count_      = 0;
  messages_staged_   = 0;
  readback_first_ms_ = 0;
  window_opened_ms_  = now_ms;
  phase_             = job.readback_only ? Phase::ReadbackDue : Phase::SendDue;
  if (job.readback_only) job_.op = lran::ConfigOp::GetAll;
  ++stats_.submitted;
  return true;
}

bool status_reports_config_change(const lran::Header& hdr, const uint8_t* payload,
                                  size_t len) {
  if (hdr.type != lran::MsgType::Status) return false;
  if (hdr.schema != lran::kSchemaGateLinkStatusV1 &&
      hdr.schema != lran::kSchemaSimnodeStatusV1) {
    return false;
  }
  lran::schema::GateLinkStatusV1 s;
  if (lran::schema::deserialize(payload, len, &s) != lran::Status::Ok) return false;
  return s.status_reason == static_cast<uint8_t>(lran::StatusReason::ConfigChange);
}

ConfigStep ConfigPath::next(uint32_t now_ms) {
  ConfigStep step;
  step.dst = job_.dst;

  switch (phase_) {
    case Phase::Idle:
      return step;

    case Phase::SendDue:
      step.action  = ConfigAction::SendConfig;
      step.payload = job_.config;
      step.seq     = seq_;
      step.ctx_id  = ctx_;
      return step;

    case Phase::AwaitingAck:
      // spec 7.4 - the outcome is UNKNOWN rather than failed, and the resolution is a
      // readback rather than a retransmission. Home Assistant is told `unknown` now and
      // told the truth when the node answers.
      if (static_cast<int32_t>(now_ms - window_opened_ms_) >=
          static_cast<int32_t>(ack_timeout_ms_)) {
        ++stats_.unknown;
        outcome_ = ConfigOutcome::Unknown;
        persist_ = AckPersist::Unknown;
        // Published FIRST, then the readback is asked for. An operator watching the
        // dashboard learns the bridge does not know inside the timeout, rather than
        // after a second round trip that may also fail.
        phase_ = Phase::ReadbackDue;
        step.action       = ConfigAction::Resolve;
        step.op_outcome   = ConfigOutcome::Unknown;
        step.op           = job_.op;
        step.persist      = AckPersist::Unknown;
        step.results      = nullptr;
        step.result_count = 0;
        return step;
      }
      return step;

    case Phase::ReadbackDue:
      step.action = ConfigAction::RequestReadback;
      return step;

    case Phase::AwaitingReadback:
      // spec 7.4.1 - the clock runs from the FIRST message of the answer. A node that
      // has sent nothing at all is bounded by the same window opened when the poll went
      // out, so a node that never answers does not wait forever either.
      if (static_cast<int32_t>(now_ms - (readback_first_ms_ != 0 ? readback_first_ms_
                                                                 : window_opened_ms_)) >=
          static_cast<int32_t>(readback_timeout_ms_)) {
        ++stats_.config_readback_abandoned;
        staged_count_    = 0;
        messages_staged_ = 0;
        // Straight to Idle rather than through finish(): this branch emits its own
        // Resolve, and leaving the phase at Resolved would emit a SECOND one on the next
        // tick - two config/ack publications for one transaction, the later of them
        // carrying no results and no explanation.
        outcome_          = ConfigOutcome::Abandoned;
        phase_            = Phase::Idle;
        step.action       = ConfigAction::Resolve;
        step.op_outcome   = ConfigOutcome::Abandoned;
        step.op           = lran::ConfigOp::GetAll;
        step.persist      = AckPersist::Unknown;
        step.results      = nullptr;
        step.result_count = 0;
        return step;
      }
      return step;

    case Phase::Resolved:
      phase_            = Phase::Idle;
      step.action       = ConfigAction::Resolve;
      step.op_outcome   = outcome_;
      // A readback answers GET_ALL whatever asked for it, so the op published with it is
      // the readback's and not the set's. Spec 16.7.3 echoes the op the ANSWER carries.
      step.op      = outcome_ == ConfigOutcome::ReadbackOk ? lran::ConfigOp::GetAll : job_.op;
      step.persist = persist_;
      step.results      = staged_;
      step.result_count = staged_count_;
      step.updates_state =
          outcome_ == ConfigOutcome::ReadbackOk || outcome_ == ConfigOutcome::Acked;
      return step;
  }
  return step;
}

void ConfigPath::on_sent(uint32_t now_ms) {
  window_opened_ms_ = now_ms;
  if (phase_ == Phase::SendDue) {
    ++stats_.sent;
    phase_ = Phase::AwaitingAck;
    return;
  }
  if (phase_ == Phase::ReadbackDue) {
    ++stats_.readbacks_requested;
    readback_first_ms_ = 0;
    phase_             = Phase::AwaitingReadback;
  }
}

void ConfigPath::stage(const lran::schema::NodeConfigAckV1& ack) {
  for (size_t i = 0; i < ack.count; ++i) {
    if (staged_count_ >= kMaxStagedResults) {
      // An answer larger than this bridge can hold. Counted rather than truncated
      // silently: a staged answer missing rows publishes a configuration that is partly
      // this node's and partly nothing at all.
      ++stats_.staging_overflow;
      return;
    }
    staged_[staged_count_++] = ack.entries[i];
  }
}

void ConfigPath::finish(ConfigOutcome outcome) {
  outcome_ = outcome;
  phase_   = Phase::Resolved;
}

void ConfigPath::on_config_ack(lran::NodeId src, const lran::schema::NodeConfigAckV1& ack,
                               lran::Seq ack_seq, uint32_t now_ms) {
  if (phase_ != Phase::AwaitingAck && phase_ != Phase::AwaitingReadback) {
    ++stats_.ack_ignored;
    return;
  }
  if (src != job_.dst) {
    ++stats_.ack_ignored;
    return;
  }

  if (phase_ == Phase::AwaitingAck) {
    // spec 9.2 - a SOLICITED answer correlates by `seq`, and spec 7.4.1 repeats that
    // `seq` on every message of it. An ACK bearing another `seq` belongs to nothing this
    // bridge is waiting for.
    if (ack_seq != seq_) {
      ++stats_.ack_ignored;
      return;
    }
  } else {
    // An UNSOLICITED readback takes one value from the node's STATUS sequence space per
    // message (D45) and correlates to no request, so `seq` is not matched here. Matching
    // it would reject the answer the bridge just asked for.
    if (readback_first_ms_ == 0) readback_first_ms_ = now_ms;
  }

  if (messages_staged_ >= lran::schema::kMaxConfigAckMessages) {
    // The node promised at most this many (spec 7.4.1). One more is a node that has
    // outgrown the mechanism, and staging it would grow without a termination condition.
    ++stats_.staging_overflow;
    return;
  }
  ++messages_staged_;
  stage(ack);
  persist_ = ack_persist_of(ack.persist_status);

  // SPEC 7.4.1's CLOSING RULE. The transaction ends on the message whose MORE_FOLLOWS is
  // clear, never on the first one. A bridge that closed on the first strands the rest of
  // the answer and reports a configuration it did not finish reading.
  if (ack.more_follows) {
    if (readback_first_ms_ == 0) readback_first_ms_ = now_ms;
    return;
  }

  if (phase_ == Phase::AwaitingAck) {
    ++stats_.acked;
    finish(ConfigOutcome::Acked);
  } else {
    ++stats_.readbacks_completed;
    finish(ConfigOutcome::ReadbackOk);
  }
}

}  // namespace bridge
