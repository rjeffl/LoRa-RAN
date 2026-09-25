// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "hex_proxy.h"

#include <cstring>

#include "json_writer.h"
#include "lran/wire.h"
#include "publish.h"

namespace bridge {

HexClass classify_hex(const char* hex, size_t n, vedirect::Parse* why) {
  vedirect::Frame f;
  const vedirect::Parse p = vedirect::decode(hex, n, &f);
  if (why != nullptr) *why = p;
  if (p != vedirect::Parse::Ok) return HexClass::Malformed;
  // spec 7.6 - Set and Restart. The same two nibbles lran::hex_req_is_write_class() looks
  // for, so the class decided here is the class the library MACs.
  return (f.cmd == static_cast<uint8_t>(vedirect::HexCmd::Set) ||
          f.cmd == static_cast<uint8_t>(vedirect::HexCmd::Restart))
             ? HexClass::Write
             : HexClass::Read;
}

// ---------------------------------------------------------------------------
// Gate 2.
// ---------------------------------------------------------------------------

bool WriteArm::armed(uint32_t now_ms, uint32_t timeout_s) const {
  if (!armed_) return false;
  // Unsigned-wrap-safe, as command.cpp is. 3600 s is the row's maximum, far inside int32.
  return static_cast<int32_t>(now_ms - armed_ms_) < static_cast<int32_t>(timeout_s * 1000u);
}

bool WriteArm::expire(uint32_t now_ms, uint32_t timeout_s) {
  if (!armed_ || armed(now_ms, timeout_s)) return false;
  armed_ = false;
  return true;
}

// ---------------------------------------------------------------------------
// The state machine.
// ---------------------------------------------------------------------------

bool HexProxy::submit(const HexRequest& req, bool write, lran::CtxId ctx, lran::Seq write_seq,
                      uint32_t now_ms) {
  if (phase_ != Phase::Idle) {
    ++stats_.refused_busy;
    return false;
  }
  req_   = req;
  write_ = write;
  if (write) {
    seq_ = write_seq;
    ctx_ = ctx;
  } else {
    // spec 10.2 - a bridge-originated unauthenticated frame's seq is local and advisory.
    if (++read_seq_ == 0) read_seq_ = 1;
    seq_ = read_seq_;
    ctx_ = ctx;
  }
  attempt_          = 0;
  resync_used_      = false;
  ctx_adopted_      = false;
  outcome_          = HexOutcome::Pending;
  status_           = 0;
  ack_result_       = 0;
  ack_detail_       = 0;
  rsp_len_          = 0;
  window_opened_ms_ = now_ms;
  phase_            = Phase::SendDue;
  ++stats_.submitted;
  return true;
}

void HexProxy::resolve(HexOutcome o) {
  outcome_ = o;
  phase_   = Phase::Resolved;
  switch (o) {
    case HexOutcome::Answered:        ++stats_.answered; break;
    case HexOutcome::NoResponse:      ++stats_.no_response; break;
    case HexOutcome::Unknown:         ++stats_.unknown; break;
    case HexOutcome::Rejected:
    case HexOutcome::ResyncFailed:    ++stats_.rejected; break;
    case HexOutcome::RefusedDisarmed: ++stats_.refused_disarmed; break;
    case HexOutcome::Pending:         break;
  }
}

HexStep HexProxy::next(uint32_t now_ms, bool armed) {
  HexStep st;
  switch (phase_) {
    case Phase::Idle:
      return st;

    case Phase::SendDue:
      // GATE 2. Asked at every write transmission, the resync's included, so an arm that
      // lapsed between the request and its resync refuses the second frame too.
      if (write_ && !armed) {
        resolve(HexOutcome::RefusedDisarmed);
        return next(now_ms, armed);
      }
      st.action      = HexAction::Send;
      st.dst         = req_.dst;
      st.write       = write_;
      st.seq         = seq_;
      st.ctx_id      = ctx_;
      st.attempt     = attempt_;
      st.ctx_adopted = ctx_adopted_;
      return st;

    case Phase::Awaiting: {
      if (static_cast<int32_t>(now_ms - window_opened_ms_) <
          static_cast<int32_t>(rsp_timeout_ms_)) {
        return st;
      }
      if (write_) {
        // Never retried - the header says why.
        resolve(HexOutcome::Unknown);
        return next(now_ms, armed);
      }
      // A readback is not retried: its registers are read again on the next pass, and
      // a bench identity with no MPPT behind it would otherwise hold the air for every
      // retry of every register (charge_readback.h).
      if (attempt_ >= (req_.origin == HexOrigin::Readback ? 0 : read_retries_)) {
        resolve(HexOutcome::NoResponse);
        return next(now_ms, armed);
      }
      ++attempt_;
      phase_ = Phase::SendDue;
      return next(now_ms, armed);
    }

    case Phase::Resolved:
      st.action     = HexAction::Resolve;
      st.dst        = req_.dst;
      st.write      = write_;
      st.seq        = seq_;
      st.attempt    = attempt_;
      st.outcome    = outcome_;
      st.status     = status_;
      st.ack_result = ack_result_;
      st.ack_detail = ack_detail_;
      st.audit      = write_;  // GATE 3: every write, whatever became of it
      phase_        = Phase::Idle;
      return st;
  }
  return st;
}

void HexProxy::on_sent(uint32_t now_ms) {
  if (phase_ != Phase::SendDue) return;
  window_opened_ms_ = now_ms;
  phase_            = Phase::Awaiting;
  ctx_adopted_      = false;
  ++stats_.sent;
  if (attempt_ > 0) ++stats_.read_retries;
}

bool HexProxy::on_rsp(lran::NodeId src, lran::Seq seq, const lran::msg::HexRsp& rsp) {
  if (phase_ != Phase::Awaiting || src != req_.dst || seq != seq_) {
    ++stats_.answer_ignored;
    return false;
  }
  status_  = rsp.status;
  rsp_len_ = rsp.n <= sizeof(rsp_) ? rsp.n : 0;
  if (rsp_len_ > 0 && rsp.hex != nullptr) std::memcpy(rsp_, rsp.hex, rsp_len_);
  resolve(HexOutcome::Answered);
  return true;
}

bool HexProxy::on_ack(lran::NodeId src, const lran::msg::CommandAck& ack, lran::CtxId ack_ctx,
                      uint32_t now_ms) {
  if (phase_ != Phase::Awaiting || !write_ || src != req_.dst || ack.ack_seq != seq_) {
    return false;
  }
  if (static_cast<lran::AckResult>(ack.result) == lran::AckResult::RejectedCtx &&
      !resync_used_) {
    // spec 10.3 step 2, as CommandPath does. The node refused at spec 9.4 step 2, before
    // the gate and before the MPPT, so this one retry cannot write twice.
    ctx_              = ack_ctx;
    seq_              = 1;
    resync_used_      = true;
    ctx_adopted_      = true;
    window_opened_ms_ = now_ms;
    phase_            = Phase::SendDue;
    ++stats_.resyncs;
    return true;
  }
  ack_result_ = ack.result;
  ack_detail_ = ack.detail;
  resolve(static_cast<lran::AckResult>(ack.result) == lran::AckResult::RejectedCtx
              ? HexOutcome::ResyncFailed
              : HexOutcome::Rejected);
  return true;
}

size_t build_hex_req_frame(lran::NodeId dst, lran::CtxId ctx, lran::Seq seq, uint8_t ver,
                           const char* hex, size_t n, const lran::EncodeCtx& ectx,
                           uint8_t* buf, size_t cap) {
  if (hex == nullptr || n == 0 || n > kHexMaxChars) return 0;
  const bool write = classify_hex(hex, n) == HexClass::Write;
  const lran::msg::HexReq req{write ? lran::kHexReqFlagWriteClass : static_cast<uint8_t>(0),
                              static_cast<uint8_t>(n), reinterpret_cast<const uint8_t*>(hex)};
  uint8_t payload[lran::msg::kHexReqHdrLen + kHexMaxChars];
  size_t  plen = 0;
  if (lran::msg::serialize(req, payload, sizeof(payload), &plen) != lran::Status::Ok) return 0;

  lran::Header h;
  h.ver    = ver;
  h.type   = lran::MsgType::HexReq;
  h.src    = lran::kNodeBridge;
  h.dst    = dst;
  h.seq    = seq;
  h.ctx_id = ctx;  // spec 10.1 - the node's context; checked only on a write (spec 9.4)
  h.schema = lran::kSchemaNone;
  size_t len = 0;
  // spec 9.2 - encode() MACs a write-class HEX_REQ by its nibble, and refuses one it has no
  // key for. Nothing here decides whether to authenticate.
  return lran::encode(h, payload, plen, ectx, buf, cap, &len) == lran::Status::Ok ? len : 0;
}

// ---------------------------------------------------------------------------
// The documents.
// ---------------------------------------------------------------------------

const char* hex_outcome_token(HexOutcome o) {
  switch (o) {
    case HexOutcome::Pending:         return "pending";
    case HexOutcome::Answered:        return "answered";
    case HexOutcome::NoResponse:      return "no_response";
    case HexOutcome::Unknown:         return "unknown";
    case HexOutcome::Rejected:        return "rejected";
    case HexOutcome::ResyncFailed:    return "resync_failed";
    case HexOutcome::RefusedDisarmed: return "refused_disarmed";
  }
  return "?";
}

const char* hex_status_token(uint8_t status) {
  switch (static_cast<lran::HexStatus>(status)) {
    case lran::HexStatus::Ok:                      return "ok";
    case lran::HexStatus::Timeout:                 return "timeout";
    case lran::HexStatus::RejectedUnauthenticated: return "rejected_unauthenticated";
    case lran::HexStatus::Busy:                    return "busy";
    case lran::HexStatus::UartError:               return "uart_error";
    case lran::HexStatus::MalformedRequest:        return "malformed_request";
  }
  return "unknown";
}

namespace {

// A NUL-terminated copy for the JSON writer. Anything longer than the buffer is cut, which
// only a Malformed request can be, and that one is quoted for diagnosis rather than parsed.
void terminated(const char* s, size_t n, char* out, size_t cap) {
  const size_t k = n < cap - 1 ? n : cap - 1;
  if (k > 0 && s != nullptr) std::memcpy(out, s, k);
  out[k] = '\0';
}

void body(JsonObject& j, const HexRequest& req, const HexStep& st, const char* rsp,
          size_t rsp_len) {
  char r[kHexMaxChars + 1];
  terminated(req.hex, req.n, r, sizeof(r));
  j.str("request", r);
  j.u32("seq", st.seq);
  j.str("outcome", hex_outcome_token(st.outcome));
  if (st.outcome == HexOutcome::Answered) {
    char a[kHexMaxChars + 1];
    terminated(rsp, rsp_len, a, sizeof(a));
    j.str("status", hex_status_token(st.status));
    if (rsp_len > 0) {
      j.str("response", a);
    } else {
      j.null("response");
    }
  } else {
    j.null("status");
    j.null("response");
  }
  if (st.outcome == HexOutcome::Rejected || st.outcome == HexOutcome::ResyncFailed) {
    j.u32("result", st.ack_result);  // spec 8.2
  }
}

void at(JsonObject& j, int64_t utc_s) {
  char when[32];
  if (utc_s >= kUtcPlausible && format_utc(utc_s, when, sizeof(when)) > 0) {
    j.str("at", when);
  } else {
    j.null("at");
  }
}

}  // namespace

size_t hex_response_json(const HexRequest& req, const HexStep& st, const char* rsp,
                         size_t rsp_len, char* out, size_t cap) {
  JsonObject j(out, cap);
  body(j, req, st, rsp, rsp_len);
  return j.finish();
}

size_t hex_audit_json(const HexRequest& req, const HexStep& st, const char* rsp,
                      size_t rsp_len, int64_t utc_s, char* out, size_t cap) {
  JsonObject j(out, cap);
  body(j, req, st, rsp, rsp_len);
  // Refused means disarmed; every other outcome was transmitted, which gate 2 allowed.
  j.str("authorization", st.outcome == HexOutcome::RefusedDisarmed ? "disarmed" : "armed");
  at(j, utc_s);
  return j.finish();
}

size_t hex_refusal_json(const HexRequest& req, const char* outcome, char* out, size_t cap) {
  JsonObject j(out, cap);
  char       r[kHexMaxChars + 1];
  terminated(req.hex, req.n, r, sizeof(r));
  j.str("request", r);
  j.null("seq");
  j.str("outcome", outcome);
  j.null("status");
  j.null("response");
  return j.finish();
}

size_t hex_refusal_audit_json(const HexRequest& req, const char* outcome, bool armed,
                              int64_t utc_s, char* out, size_t cap) {
  JsonObject j(out, cap);
  char       r[kHexMaxChars + 1];
  terminated(req.hex, req.n, r, sizeof(r));
  j.str("request", r);
  j.null("seq");
  j.str("outcome", outcome);
  j.null("status");
  j.null("response");
  j.str("authorization", armed ? "armed" : "disarmed");
  at(j, utc_s);
  return j.finish();
}

}  // namespace bridge
