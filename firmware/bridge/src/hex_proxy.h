// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The VE.Direct HEX proxy: one request from Home Assistant to a node and its answer back,
// and the two write gates the bridge owns. Tasks BF-28 and BF-29; Impl Plan 6.4; PRD 3.5,
// BS-2, V-B6; spec 6.7, 7.6, 8.13, 9.2, 10.2, 10.3, 16.2.
//
// ARDUINO-FREE, AND IT DOES NO I/O, like command.h. next() says what to transmit or what
// to report; sched_task builds the frame, queues it and reports back with on_sent().
//
// THREE GATES ON A WRITE, AND EACH STANDS ALONE (PRD R-3.5b). A write is a VE.Direct Set or
// Restart, judged from the command nibble alone (spec 7.6). The failure mode is battery
// damage, and it is invisible until it is not (BS-2).
//
//   1. A MAC. The library adds one to every write-class HEX_REQ it encodes and the node
//      refuses one without it. Nothing here can turn it off.
//   2. An armed write-enable switch that expires (WriteArm). next() asks whether the node is
//      armed before EVERY write transmission, a resync's included, and a disarmed node's
//      write resolves as RefusedDisarmed without a frame being built. The arm is asked
//      with the clock, so a lapsed arm refuses even before sched_task has published it off.
//   3. An audit trail. Every write resolves with `audit` set, whatever became of it, and
//      sched_task publishes that retained. A refusal is an attempt too.
//
// ONE HEX TRANSACTION IN FLIGHT ACROSS THE FLEET, for the command path's reason: a write
// shares the command seq space (spec 9.4 steps 4-6 apply to every authenticated type), and
// a resync resets that space to 1 (spec 10.3).
//
// A WRITE IS NEVER RETRIED. A Set is not idempotently repeatable in the sense that matters:
// the node's dedup cache holds an AckResult, not the MPPT's answer, so a retried write
// would draw COMMAND_ACK(DUPLICATE_CACHED) and never the register value. A write with no
// answer resolves Unknown, and a Get settles it, as a CONFIG with no ACK is settled by a
// readback (spec 7.4). A READ is retried under the same seq, which is harmless: a read
// changes nothing and its seq is in neither space (spec 10.2). Decided with the operator
// on 2026-09-25. The one exception is spec 10.3's resync, which retries once because the
// node refused at step 2 and never forwarded the first.
//
// Not thread-safe. task_runtime.cpp holds the instance behind the scheduler's mutex:
// sched_task sends, app_task hears the answer, and mqtt_task arms.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/codec.h"
#include "lran/messages.h"
#include "lran/types.h"
#include "registry.h"
#include "vedirect/hex.h"

namespace bridge {


// Root rule 8 - both are rows in /lib/lran-config/, and these are their defaults.
inline constexpr uint32_t kHexRspTimeoutDefaultMs = 3000;   // hex_rsp_timeout_ms
inline constexpr uint32_t kWriteArmTimeoutDefaultS = 300;   // mppt_write_arm_timeout_s

// A read's retries after the first attempt, under the same seq. A count, not a time.
inline constexpr uint8_t kHexReadRetriesDefault = 2;

// The longest request the proxy carries: vedirect's frame, which spec 3.1's plain payload
// cap holds with room to spare.
inline constexpr size_t kHexMaxChars = vedirect::kMaxChars;
static_assert(kHexMaxChars + lran::msg::kHexReqHdrLen <= lran::kMaxPayloadAuth,
              "a write-class HEX_REQ must fit one authenticated frame (spec 11.4)");

// Who asked. An operator's request answers on `hex/response`; a readback's (BF-30) feeds the
// charge-parameter document instead.
enum class HexOrigin : uint8_t { Operator, Readback };

struct HexRequest {
  lran::NodeId dst    = 0;
  HexOrigin    origin = HexOrigin::Operator;
  uint16_t     reg    = 0;  // Readback only: the register asked for
  uint8_t      n      = 0;
  char         hex[kHexMaxChars] = {0};  // ASCII, leading ':', no newline, not terminated
};

// What the bridge makes of a request string before any airtime is spent on it.
enum class HexClass : uint8_t {
  Read,       // a well-formed frame whose command is not Set or Restart
  Write,      // Set (0x8) or Restart (0x6) - spec 7.6's write class
  Malformed,  // not a VE.Direct HEX frame; refused here, never transmitted
};

// Classifies with vedirect's parser, so a frame the MPPT would answer with a frame error
// is refused before it costs a solar node a transmission. `why` names the parse failure.
// The write/read split agrees with lran::hex_req_is_write_class(), which decides the MAC.
HexClass classify_hex(const char* hex, size_t n, vedirect::Parse* why = nullptr);

// ---------------------------------------------------------------------------
// Gate 2 - PRD R-3.5b and R-3.5c. One per node that carries an MPPT.
// ---------------------------------------------------------------------------
class WriteArm {
 public:
  // R-3.5c - deliberately a separate step from the write. Arming again restarts the time.
  void arm(uint32_t now_ms) {
    armed_    = true;
    armed_ms_ = now_ms;
  }
  void disarm() { armed_ = false; }

  // THE GATE. False once `timeout_s` has run, whether or not expire() has been called.
  bool armed(uint32_t now_ms, uint32_t timeout_s) const;

  // True exactly once, when an arm lapses: the caller publishes the switch back to off, so
  // Home Assistant shows what the gate will do (Impl Plan 6.4).
  bool expire(uint32_t now_ms, uint32_t timeout_s);

  // The switch's state as last published; an arm that has lapsed but not yet expired still
  // reads true here, and armed() is what refuses it.
  bool shown_armed() const { return armed_; }

 private:
  bool     armed_    = false;  // default off, PRD R-3.5b
  uint32_t armed_ms_ = 0;
};

// ---------------------------------------------------------------------------
// The state machine.
// ---------------------------------------------------------------------------

enum class HexAction : uint8_t { None, Send, Resolve };

// How a request ended. Every value is a case a reader of `hex/response` or `hex/audit` has
// to tell apart; the token each publishes is hex_outcome_token()'s.
enum class HexOutcome : uint8_t {
  Pending,
  Answered,         // a HEX_RSP arrived; `status` and the response string are the node's
  NoResponse,       // a read: every attempt went unanswered
  Unknown,          // a write went unanswered. It MAY have reached the MPPT; read it back
  Rejected,         // a COMMAND_ACK answered a write: spec 9.4 steps 4-5 (`ack_result`)
  ResyncFailed,     // a second REJECTED_CTX, spec 10.3 step 3
  RefusedDisarmed,  // gate 2: no frame was built
};

struct HexStep {
  HexAction    action = HexAction::None;
  lran::NodeId dst    = 0;
  bool         write  = false;

  // Send: the frame's fields. `seq` is the same on every attempt of one request.
  lran::Seq   seq         = 0;
  lran::CtxId ctx_id      = 0;
  uint8_t     attempt     = 0;
  bool        ctx_adopted = false;  // as CmdStep's: write ctx_id and seq back to the registry

  // Resolve.
  HexOutcome outcome    = HexOutcome::Pending;
  uint8_t    status     = 0;  // spec 8.13, when Answered
  uint8_t    ack_result = 0;  // spec 8.2, when Rejected or ResyncFailed
  uint8_t    ack_detail = 0;
  bool       audit      = false;  // gate 3: every write resolves with this set
};

struct HexStats {
  uint32_t submitted         = 0;
  uint32_t refused_busy      = 0;
  uint32_t sent              = 0;
  uint32_t read_retries      = 0;
  uint32_t answered          = 0;
  uint32_t no_response       = 0;
  uint32_t unknown           = 0;
  uint32_t rejected          = 0;
  uint32_t refused_disarmed  = 0;
  uint32_t resyncs           = 0;
  uint32_t answer_ignored    = 0;  // a HEX_RSP matching nothing in flight
};

class HexProxy {
 public:
  void     set_rsp_timeout_ms(uint32_t ms) { rsp_timeout_ms_ = ms; }
  uint32_t rsp_timeout_ms() const { return rsp_timeout_ms_; }
  void     set_read_retries(uint8_t n) { read_retries_ = n; }

  bool         busy() const { return phase_ != Phase::Idle; }
  lran::NodeId in_flight_node() const { return req_.dst; }
  bool         in_flight_write() const { return write_; }

  // Accept a classified request. `write` must be classify_hex()'s answer; a Malformed one
  // is the caller's to refuse. A write's `ctx` and `seq` are the registry's, from the
  // command space; a read's `seq` is this class's own (spec 10.2: advisory, in neither
  // space) and `ctx` is ignored. False when a transaction is already in flight.
  bool submit(const HexRequest& req, bool write, lran::CtxId ctx, lran::Seq write_seq,
              uint32_t now_ms);

  // Call until it returns None. `armed` is gate 2 for the node in flight, asked by the
  // caller from its WriteArm with the clock; a write whose Send is due while it is false
  // resolves RefusedDisarmed instead. A read ignores it.
  HexStep next(uint32_t now_ms, bool armed);

  void on_sent(uint32_t now_ms);

  // The frame from the last Send left lora_task at `aired_ms`: on air, or given up. The
  // window reopens from then, because a frame can wait seconds for media access (spec
  // 12.3) and a window counted from the queue closes early by that much.
  void on_aired(uint32_t aired_ms);

  // A HEX_RSP passed the receive ladder. Claimed when it answers what is in flight: the
  // node and the seq (spec 9.2). The response string is copied in.
  bool on_rsp(lran::NodeId src, lran::Seq seq, const lran::msg::HexRsp& rsp);

  // A COMMAND_ACK. Claimed only when it answers the write in flight, by ack_seq; a read
  // draws none. Checked before the command path sees the ACK, as the roll's is.
  bool on_ack(lran::NodeId src, const lran::msg::CommandAck& ack, lran::CtxId ack_ctx,
              uint32_t now_ms);

  // The request in flight, or the one just resolved, and its answer.
  const HexRequest& request() const { return req_; }
  const char*       response() const { return rsp_; }
  size_t            response_len() const { return rsp_len_; }

  const HexStats& stats() const { return stats_; }

 private:
  enum class Phase : uint8_t { Idle, SendDue, Awaiting, Resolved };

  void resolve(HexOutcome o);

  HexRequest req_{};
  bool       write_   = false;
  Phase      phase_   = Phase::Idle;
  lran::Seq  seq_     = 0;
  lran::CtxId ctx_    = 0;
  uint8_t    attempt_ = 0;
  bool       resync_used_ = false;
  bool       ctx_adopted_ = false;
  uint32_t   window_opened_ms_ = 0;

  HexOutcome outcome_    = HexOutcome::Pending;
  uint8_t    status_     = 0;
  uint8_t    ack_result_ = 0;
  uint8_t    ack_detail_ = 0;
  char       rsp_[kHexMaxChars] = {0};
  size_t     rsp_len_ = 0;

  lran::Seq read_seq_ = 0;  // a read's own counter; 0 is skipped

  uint32_t rsp_timeout_ms_ = kHexRspTimeoutDefaultMs;
  uint8_t  read_retries_   = kHexReadRetriesDefault;

  HexStats stats_;
};

// spec 7.6 - the HEX_REQ frame. `flags` bit 0 is set for a write, as the sender's
// declaration; the node judges by the nibble and does not trust it. `ectx` must carry the
// IMac and the node key, or a write produces no frame rather than an unauthenticated one
// (spec 9.2). Returns the frame length, or 0.
size_t build_hex_req_frame(lran::NodeId dst, lran::CtxId ctx, lran::Seq seq, uint8_t ver,
                           const char* hex, size_t n, const lran::EncodeCtx& ectx,
                           uint8_t* buf, size_t cap);

// ---------------------------------------------------------------------------
// The documents. Spec 16.2 fixes the topics and leaves the payloads to the bridge (16.2.1),
// so these tokens are this firmware's, and frozen once Home Assistant reads them.
// ---------------------------------------------------------------------------

const char* hex_outcome_token(HexOutcome o);
const char* hex_status_token(uint8_t status);  // spec 8.13's names, lower case

// `lran/<node>/vedirect/hex/response`, not retained:
//   {"request":":7F0ED0071","seq":41,"outcome":"answered","status":"ok",
//    "response":":7F0ED009600DB"}
// `status` and `response` are null unless the outcome is `answered`; `result` (spec 8.2) is
// present when a COMMAND_ACK answered. Returns the length, or 0 when `cap` is short.
size_t hex_response_json(const HexRequest& req, const HexStep& st, const char* rsp,
                         size_t rsp_len, char* out, size_t cap);

// `lran/<node>/vedirect/hex/audit`, RETAINED (spec 16.2), for every write attempt: the
// response document plus `authorization`, `armed` or `disarmed`, and `at`, the time in ISO
// 8601 UTC, or null before SNTP has set the clock (publish.h's kUtcPlausible). The
// last write attempt then survives an HA restart (Impl Plan 6.4).
size_t hex_audit_json(const HexRequest& req, const HexStep& st, const char* rsp,
                      size_t rsp_len, int64_t utc_s, char* out, size_t cap);

// A request refused before it reached the state machine: `malformed`, `busy` or
// `context_roll_pending`. The same document, with no seq.
size_t hex_refusal_json(const HexRequest& req, const char* outcome, char* out, size_t cap);

// The same refusal as an audit entry, for a write refused before the state machine saw it.
size_t hex_refusal_audit_json(const HexRequest& req, const char* outcome, bool armed,
                              int64_t utc_s, char* out, size_t cap);

}  // namespace bridge
