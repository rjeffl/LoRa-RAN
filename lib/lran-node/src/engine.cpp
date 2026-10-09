// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink task L1; see engine.h. The code is the simnode's Node, moved: node.cpp's receive
// ladder, and gatelink.cpp's command, config, HEX and boot paths.
//
// A DUPLICATE_CACHED ACK carries the cached result in `detail`. Spec 9.4 step 4 says
// "COMMAND_ACK(DUPLICATE_CACHED) with the cached result" and spec 6.3 has one result byte, so
// the cached result can only ride in `detail`, and the cached detail is lost. Raised for
// spec v0.12 rather than settled here.

#include "lran/node/engine.h"

#include <cstring>

#include "lran/codec.h"
#include "lran/config/table.h"
#include "lran/node/names.h"
#include "lran/wire.h"

namespace lran::node {
namespace {

// Resets copy from these rather than assigning a braced temporary. GCC 13.3 (CI's
// ubuntu-24.04 native build) hits an internal compiler error gimplifying
// `x = lran::schema::NodeConfigAckV1{};` - an aggregate whose array member carries
// a default member initializer. Clang and the Xtensa GCC compile it; the copy is the
// same bytes either way.
const schema::NodeConfigV1    kEmptyConfig{};
const schema::NodeConfigAckV1 kEmptyConfigAck{};

}  // namespace

Engine::Engine(Outbox* outbox, IMac* mac, Sink* log) : out_(outbox), mac_(mac), log_(log) {}

Header Engine::header(const Context& c, MsgType type, NodeId dst, uint8_t schema) {
  Header h;
  h.ver    = c.proto_ver;
  h.type   = type;
  h.src    = c.id;
  h.dst    = dst;
  h.ctx_id = c.ctx_id;
  h.schema = schema;
  return h;
}

// ---------------------------------------------------------------------------
// The receive ladder
// ---------------------------------------------------------------------------

void Engine::on_phy_crc_error(Context& c) {
  ++c.counters.rx_frames;
  ++c.counters.rx_crc_err;
}

Engine::RxResult Engine::receive(Context& c, Application& app, const uint8_t* buf, size_t len,
                                 int16_t rssi_dbm, int16_t snr_db10, uint32_t now_ms) {
  RxResult r;
  ++c.counters.rx_frames;

  // A node accepts only the version it speaks (spec 13.1); V-B10 sets it per identity.
  DecodeCtx ctx;
  ctx.self           = c.id;
  ctx.accept_ver_min = c.proto_ver;
  ctx.accept_ver_max = c.proto_ver;
  ctx.counters       = &c.counters;

  Frame        f;
  const Status head = decode_header(buf, len, ctx, &f);  // stages 2-6
  if (head != Status::Ok) {
    if (level_ == LogLevel::Debug) {
      sink_printf(log_, "rx %02x: discarded at header, status %u", c.id,
                  static_cast<unsigned>(head));
    }
    return r;
  }
  r.decoded = true;
  r.hdr     = f.hdr;

  c.heard         = true;
  c.last_rssi_dbm = rssi_dbm;
  c.last_snr_db10 = snr_db10;

  // spec 9.4 steps 2 and 3. The codec applies the ctx check to authenticated types only,
  // so expecting this context's own ctx_id on every frame is correct.
  ctx.mac           = mac_;
  ctx.node_key      = c.key;
  ctx.expect_ctx_id = c.ctx_id;
  const Status body = decode_payload(buf, len, ctx, &f);  // stages 7-9
  if (body != Status::Ok) {
    if ((app.capabilities(c) & kRefusesAuthenticated) != 0 &&
        (body == Status::RejectedCtx || body == Status::RejectedMac)) {
      refuse_authenticated(c, f.hdr, body);  // spec 9.4 steps 2-3
    } else if (level_ == LogLevel::Debug) {
      sink_printf(log_, "rx %02x: discarded at payload, status %u", c.id,
                  static_cast<unsigned>(body));
    }
    return r;
  }

  // The codec answers Ok for an authenticated frame whose MAC it had no key to check. mac_
  // and the key are always set here, so this cannot fire; the check is what keeps that true.
  if (frame_has_mac(f.hdr.type, f.payload, f.payload_len) && !f.mac_verified) {
    ++c.unhandled;
    sink_printf(log_, "rx %02x <- %02x seq %u: authenticated type with no verified MAC, ignored",
                c.id, f.hdr.src, static_cast<unsigned>(f.hdr.seq));
    return r;
  }

  if (f.hdr.frag_total() == 1) {
    deliver(c, app, f.hdr, f.payload, f.payload_len, 1, rssi_dbm, snr_db10, now_ms);
    return r;
  }

  // spec 11 - stage 10. A new set starts the chunk inference afresh.
  if (!c.reassembler.active()) c.rx_chunk = 0;
  const Status st = c.reassembler.accept(f, now_ms);
  if (st == Status::Ok && f.payload_len > c.rx_chunk) {
    c.rx_chunk = static_cast<uint8_t>(f.payload_len);
  }
  if (st != Status::Ok || !c.reassembler.complete()) return r;

  deliver(c, app, f.hdr, c.reassembler.data(), c.reassembler.len(), f.hdr.frag_total(),
          rssi_dbm, snr_db10, now_ms);
  return r;
}

void Engine::deliver(Context& c, Application& app, const Header& hdr, const uint8_t* payload,
                     size_t len, uint8_t fragments, int16_t rssi_dbm, int16_t snr_db10,
                     uint32_t now_ms) {
  if (level_ == LogLevel::Debug) {
    sink_printf(log_, "rx %02x <- %02x type 0x%02x seq %u, %u B in %u frame(s)", c.id, hdr.src,
                static_cast<unsigned>(hdr.type), static_cast<unsigned>(hdr.seq),
                static_cast<unsigned>(len), static_cast<unsigned>(fragments));
  }

  // spec 12.4.2 step 8 - a revert is reported when the next frame from the bridge arrives,
  // which is the evidence that the restored settings reach it.
  if (hdr.src == kNodeBridge && c.phy_revert_detail != 0) {
    send_phy_reverted(c, app, hdr.src, now_ms);
  }

  const uint8_t caps = app.capabilities(c);
  switch (hdr.type) {
    case MsgType::Error:
      // BF-19a - the bridge's spec 14.2 reply. The node acts on none of it; it is logged
      // because the err_code is the only on-air evidence of WHICH spec 14 stage fired.
      on_error(c, hdr, payload, len);
      return;
    case MsgType::Ping:
      if (app.on_ping(*this, c, hdr, payload, len, fragments, rssi_dbm, snr_db10, now_ms)) {
        return;
      }
      break;
    case MsgType::Poll:
      if (app.on_poll(*this, c, hdr, payload, len, now_ms)) return;
      break;
    case MsgType::Command:
      if ((caps & kAnswersCommands) != 0) {
        on_command(c, app, hdr, payload, len, now_ms);
        return;
      }
      if (answer_roll_only(c, app, hdr, payload, len)) return;
      break;
    case MsgType::HexReq:
      if ((caps & kAnswersHex) != 0) {
        on_hex_req(c, app, hdr, payload, len, now_ms);
        return;
      }
      break;
    case MsgType::Config:
      if ((caps & kAnswersConfig) != 0) {
        on_config(c, app, hdr, payload, len, now_ms);
        return;
      }
      break;
    default:
      break;
  }
  ++c.unhandled;
}

void Engine::on_error(Context& c, const Header& hdr, const uint8_t* payload, size_t len) {
  msg::Error err;
  if (msg::deserialize(payload, len, &err) != Status::Ok) {
    sink_printf(log_, "rx %02x <- %02x ERROR: %u B, would not decode", c.id, hdr.src,
                static_cast<unsigned>(len));
    return;
  }
  // spec 8.8's err_code, printed raw: the library has no to_string for it.
  sink_printf(log_, "rx %02x <- %02x ERROR err_code 0x%02x detail 0x%02x ref_seq %u", c.id,
              hdr.src, static_cast<unsigned>(err.err_code), static_cast<unsigned>(err.detail),
              static_cast<unsigned>(err.ref_seq));
}

void Engine::refuse_authenticated(Context& c, const Header& hdr, Status why) {
  // spec 9.4 steps 2-3 - answered with COMMAND_ACK carrying this node's own ctx_id (spec
  // 10.3), for every authenticated type. Only a frame addressed to this context.
  if (hdr.dst != c.id) return;
  if (hdr.type == MsgType::HexReq) {
    // A write-class HEX_REQ whose MAC failed or is absent answers HEX_RSP
    // (REJECTED_UNAUTHENTICATED), as spec 8.13 names it. A context mismatch still answers
    // COMMAND_ACK(REJECTED_CTX), because that ACK is what carries this node's ctx_id back
    // for the bridge's resync (spec 10.3). Spec 7.6 states the split from v0.17 (D73).
    ++c.hex_requests;
    if (why == Status::RejectedMac) {
      sink_printf(log_, "hex %02x <- %02x seq %u: write-class, MAC failed, REJECTED_UNAUTHENTICATED",
                  c.id, hdr.src, static_cast<unsigned>(hdr.seq));
      send_hex_rsp(c, hdr.src, hdr.seq, HexStatus::RejectedUnauthenticated, nullptr, 0);
      return;
    }
  } else if (hdr.type != MsgType::Command && hdr.type != MsgType::Config) {
    return;
  }
  const AckResult r = why == Status::RejectedCtx ? AckResult::RejectedCtx : AckResult::RejectedMac;
  sink_printf(log_, "cmd %02x <- %02x seq %u: %s (frame ctx 0x%08lx, own 0x%08lx)", c.id, hdr.src,
              static_cast<unsigned>(hdr.seq), ack_result_name(r),
              static_cast<unsigned long>(hdr.ctx_id), static_cast<unsigned long>(c.ctx_id));
  send_ack(c, hdr.src, hdr.seq, r, 0);
}

void Engine::tick(Context& c, uint32_t now_ms) {
  c.reassembler.tick(now_ms);
  tick_hex(c, now_ms);
}

void Engine::tick_hex(Context& c, uint32_t now_ms) {
  HexPending& h = c.hex_pending;
  if (!h.active || static_cast<int32_t>(now_ms - h.due_ms) < 0) return;
  h.active = false;
  sink_printf(log_, "hex %02x -> %02x seq %u: no answer by its deadline, TIMEOUT", c.id, h.peer,
              static_cast<unsigned>(h.seq));
  send_hex_rsp(c, h.peer, h.seq, HexStatus::Timeout, nullptr, 0);
}

// ---------------------------------------------------------------------------
// Sends
// ---------------------------------------------------------------------------

bool Engine::send(Context& c, const Header& hdr, const uint8_t* payload, size_t len,
                  uint8_t chunk) {
  EncodeCtx ectx;
  ectx.mac      = mac_;
  ectx.node_key = c.key;

  uint8_t buf[kMaxFrame];
  size_t  n = 0;

  if (chunk == 0 || chunk >= len) {
    if (out_->free_slots() < 1) return false;
    if (encode(hdr, payload, len, ectx, buf, sizeof(buf), &n) != Status::Ok) return false;
    out_->push(buf, n);
    ++c.counters.tx_frames;
    return true;
  }

  const uint8_t total = fragment_count(len, chunk);
  if (total == 0 || out_->free_slots() < total) return false;
  for (uint8_t i = 0; i < total; ++i) {
    if (encode_fragment(hdr, payload, len, i, chunk, ectx, buf, sizeof(buf), &n) != Status::Ok) {
      // Only possible on the first fragment in practice; the set's shape is fixed by then.
      return false;
    }
    out_->push(buf, n);
    ++c.counters.tx_frames;
  }
  return true;
}

bool Engine::send_ack(Context& c, NodeId peer, Seq ack_seq, AckResult result, uint8_t detail) {
  const msg::CommandAck ack{ack_seq, static_cast<uint8_t>(result), detail};
  uint8_t               payload[msg::kCommandAckLen];
  size_t                n = 0;
  if (msg::serialize(ack, payload, sizeof(payload), &n) != Status::Ok) return false;

  Header h = header(c, MsgType::CommandAck, peer, kSchemaNone);
  h.seq    = c.tx_seq++;
  if (!send(c, h, payload, n, 0)) {
    ++answers_dropped_;
    sink_printf(log_, "cmd %02x seq %u: COMMAND_ACK not queued", c.id, static_cast<unsigned>(ack_seq));
    return false;
  }
  return true;
}

// The ACK for a result this context just produced. The bench hook acts here and nowhere
// else: a retry answered from the cache is the path the simnode's ack faults exercise.
void Engine::send_fresh_ack(Context& c, Application& app, NodeId peer, Seq seq,
                            AckResult result, uint8_t detail) {
  const AckDelivery d = app.fresh_ack(c, seq);
  if (d == AckDelivery::Suppress) return;
  send_ack(c, peer, seq, result, detail);
  if (d == AckDelivery::Twice) send_ack(c, peer, seq, result, detail);
}

bool Engine::send_status(Context& c, Application& app, NodeId dst, StatusReason reason,
                         uint32_t now_ms) {
  // spec 8.7, D69 - a poll's answer has no reason of its own, so it carries an owed
  // CONFIG_CHANGE. Any other reason is kept, and the report waits for the next poll.
  const bool report_change = c.config_change_owed && reason == StatusReason::PollResponse;
  if (report_change) reason = StatusReason::ConfigChange;
  uint8_t      payload[kMaxSchemaPayload];
  uint8_t      schema = kSchemaNone;
  const size_t n      = app.build_status(c, reason, now_ms, payload, sizeof(payload), &schema);
  Header       h      = header(c, MsgType::Status, dst, schema);
  h.seq               = c.tx_seq++;
  if (n == 0 || !send(c, h, payload, n, 0)) {
    ++answers_dropped_;
    sink_printf(log_, "status %02x: schema 0x%02x %s not queued", c.id,
                static_cast<unsigned>(schema), status_reason_name(reason));
    return false;
  }
  if (report_change) c.config_change_owed = false;
  return true;
}

bool Engine::send_event(Context& c, NodeId dst, const uint8_t* payload, size_t len,
                        uint8_t schema) {
  Header h = header(c, MsgType::Event, dst, schema);
  h.seq    = c.tx_seq++;
  return send(c, h, payload, len, 0);
}

bool Engine::send_config_ack(Context& c, NodeId dst, const schema::NodeConfigAckV1& ack,
                             uint32_t reply_seq) {
  uint8_t payload[kMaxSchemaPayload];
  size_t  n = 0;
  if (schema::serialize(ack, payload, sizeof(payload), &n) != Status::Ok) {
    sink_printf(log_, "config %02x: CONFIG_ACK did not serialize", c.id);
    return false;
  }
  Header h = header(c, MsgType::ConfigAck, dst, kSchemaNodeConfigV1);
  // spec 7.4.1 - the request's `seq` when this answers one, the status space when it
  // answers nothing. A solicited answer carrying a status seq cannot be correlated at
  // all: the bridge is waiting on the seq it sent, and an answer under another number
  // reads as an ACK for something else. Found on the bench on 2026-09-21, when the
  // bridge's BF-32 path reported `unknown` for a CONFIG the simnode had already applied
  // and answered.
  h.seq = reply_seq == kUseStatusSeq ? c.tx_seq++ : static_cast<Seq>(reply_seq);
  if (!send(c, h, payload, n, 0)) {
    ++answers_dropped_;
    sink_printf(log_, "config %02x: CONFIG_ACK not queued", c.id);
    return false;
  }
  return true;
}

bool Engine::send_config_readback(Context& c, Application& app, NodeId dst) {
  cfg_rx_    = kEmptyConfig;
  cfg_rx_.op = ConfigOp::GetAll;
  apply_config(c, app, cfg_rx_, 0);
  // Unsolicited: it answers a POLL bit 1 or a REQUEST_CONFIG and correlates to no
  // request at all (D45).
  return send_config_answer(c, dst, kUseStatusSeq);
}

bool Engine::send_config_answer(Context& c, NodeId dst, uint32_t reply_seq) {
  // spec 7.4.1 - a read may span messages; a SET is answered in one, and the bridge sends a
  // set it cannot fit as several CONFIGs.
  const bool   split = cfg_ack_.op != ConfigOp::Set;
  const size_t limit = split ? schema::kMaxConfigAckMessages : 1;

  // Message boundaries first, so the answer is counted before anything is queued and goes
  // out whole or not at all, as a fragment set does. A bridge holding a message marked
  // MORE_FOLLOWS waits out config_readback_timeout_ms for one the outbox refused.
  size_t ends[schema::kMaxConfigAckMessages] = {};
  size_t messages                            = 0;
  size_t next                                = 0;
  do {
    size_t used = schema::kConfigAckHdrLen;
    size_t n    = 0;
    while (next < nresults_ && n < schema::kMaxConfigAckEntries &&
           used + schema::kConfigAckEntryHdrLen + results_[next].len <= kMaxSchemaPayload) {
      used += schema::kConfigAckEntryHdrLen + results_[next].len;
      ++n;
      ++next;
    }
    ends[messages++] = next;
  } while (next < nresults_ && messages < limit);

  if (out_->free_slots() < messages) {
    ++answers_dropped_;
    sink_printf(log_, "config %02x: %u CONFIG_ACK message(s) do not fit the outbox, none queued",
                c.id, static_cast<unsigned>(messages));
    return false;
  }
  const size_t dropped = results_dropped_ + (nresults_ - next);
  if (dropped > 0) {
    sink_printf(log_, "config %02x: %u result(s) did not fit %u CONFIG_ACK message(s) "
                      "(spec 7.4.1) - resolve by readback",
                c.id, static_cast<unsigned>(dropped), static_cast<unsigned>(messages));
  }

  // Every message repeats op and persist_status (spec 7.4.1); only the entries move.
  const ConfigOp      op      = cfg_ack_.op;
  const PersistStatus persist = cfg_ack_.persist_status;
  size_t              first   = 0;
  for (size_t m = 0; m < messages; ++m) {
    cfg_ack_                = kEmptyConfigAck;
    cfg_ack_.op             = op;
    cfg_ack_.persist_status = persist;
    for (size_t i = first; i < ends[m]; ++i) cfg_ack_.entries[cfg_ack_.count++] = results_[i];
    first = ends[m];
    // spec 7.4.1 - every message but the last. The last is unmarked even when results were
    // dropped, as lran-config's Store ends an answer it cannot finish.
    cfg_ack_.more_follows = m + 1 < messages;
    // A solicited answer repeats the request's seq on every message; an unsolicited one
    // takes a status seq per message, which send_config_ack() draws.
    if (!send_config_ack(c, dst, cfg_ack_, reply_seq)) return false;
  }
  return true;
}

bool Engine::send_hex_rsp(Context& c, NodeId dst, Seq seq, HexStatus status, const char* hex,
                          size_t n) {
  const msg::HexRsp rsp{static_cast<uint8_t>(status), static_cast<uint8_t>(n),
                        reinterpret_cast<const uint8_t*>(hex)};
  uint8_t           payload[kMaxPayloadPlain];
  size_t            len = 0;
  if (n > 0xFF || msg::serialize(rsp, payload, sizeof(payload), &len) != Status::Ok) {
    sink_printf(log_, "hex %02x seq %u: HEX_RSP did not serialize", c.id, static_cast<unsigned>(seq));
    return false;
  }
  // The request's seq, as a solicited CONFIG_ACK carries its request's (spec 7.4.1): the
  // bridge correlates by seq (spec 9.2), and HEX_RSP has no field of its own for it.
  Header h = header(c, MsgType::HexRsp, dst, kSchemaNone);
  h.seq    = seq;
  if (!send(c, h, payload, len, 0)) {
    ++answers_dropped_;
    sink_printf(log_, "hex %02x seq %u: HEX_RSP not queued", c.id, static_cast<unsigned>(seq));
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Context roll and commands
// ---------------------------------------------------------------------------

void Engine::on_roll(Context& c, Application& app, const Header& hdr, const msg::Command& cmd) {
  // spec 10.6 - the same guard as REBOOT, and the same answer to a wrong one.
  if (cmd.arg != kRollContextGuard) {
    sink_printf(log_, "roll %02x <- %02x seq %u: arg 0x%02x, REJECTED_ARG", c.id, hdr.src,
                static_cast<unsigned>(hdr.seq), static_cast<unsigned>(cmd.arg));
    send_ack(c, hdr.src, hdr.seq, AckResult::RejectedArg, 0);
    return;
  }
  // spec 10.6 node step 1 - a command still executing keeps its context. Emptying the cache
  // now would drop its result, and the bridge's retry of that command would meet a fresh
  // cache and run it a second time. The bridge retries the roll under the same seq.
  if (c.gate.any_in_flight()) {
    sink_printf(log_, "roll %02x <- %02x seq %u: a command is in flight, ACTUATOR_BUSY", c.id,
                hdr.src, static_cast<unsigned>(hdr.seq));
    send_ack(c, hdr.src, hdr.seq, AckResult::ActuatorBusy, 0);
    return;
  }
  // spec 12.4.1 - the roll after a bridge restart is an authenticated frame, and it
  // confirms a node still in its trial. It skips the gate, so it passes stage 11 here.
  phy_->on_authenticated();
  const CtxId old_ctx = c.ctx_id;
  roll_context(c, app.random());
  sink_printf(log_, "roll %02x <- %02x seq %u: ctx 0x%08lx -> 0x%08lx, ACCEPTED", c.id, hdr.src,
              static_cast<unsigned>(hdr.seq), static_cast<unsigned long>(old_ctx),
              static_cast<unsigned long>(c.ctx_id));
  // spec 10.6 node step 3 - under the NEW ctx_id with status seq 1, the first frame of the
  // new context. Through send_fresh_ack(), so a lost ACK can be simulated and the bridge's
  // retry reaches spec 10.6 bridge step 4, REJECTED_CTX completing the roll.
  send_fresh_ack(c, app, hdr.src, hdr.seq, AckResult::Accepted, 0);
}

bool Engine::answer_roll_only(Context& c, Application& app, const Header& hdr,
                              const uint8_t* payload, size_t len) {
  msg::Command cmd;
  if (msg::deserialize(payload, len, &cmd) != Status::Ok) return false;
  if (cmd.cmd != static_cast<uint8_t>(Cmd::RollContext)) return false;
  if (!app.withhold(c, "roll", hdr)) on_roll(c, app, hdr, cmd);
  return true;
}

void Engine::on_command(Context& c, Application& app, const Header& hdr, const uint8_t* payload,
                        size_t len, uint32_t now_ms) {
  if (app.withhold(c, "command", hdr)) return;
  msg::Command cmd;
  if (msg::deserialize(payload, len, &cmd) != Status::Ok) {
    // The codec checked the 4-byte length at stage 8, so this cannot happen; logged anyway.
    ++c.unhandled;
    sink_printf(log_, "cmd %02x <- %02x seq %u: payload did not deserialize", c.id, hdr.src,
                static_cast<unsigned>(hdr.seq));
    return;
  }

  // BEFORE the gate. Spec 9.4 puts the context check at step 2 and the dedup gate at steps
  // 4-6, so a node that refuses on context has not looked at the sequence space: the seq is
  // not consumed, nothing is cached, and the command is never executed.
  if (app.force_reject_ctx(c, hdr)) {
    send_ack(c, hdr.src, hdr.seq, AckResult::RejectedCtx, 0);
    return;
  }

  // spec 9.4, 10.6 - a roll skips steps 4-6. The bridge sends it because its own seq
  // cannot be trusted after a restart, so the gate must not judge that seq.
  if (cmd.cmd == static_cast<uint8_t>(Cmd::RollContext)) {
    on_roll(c, app, hdr, cmd);
    return;
  }

  const GateResult g = c.gate.check(hdr.seq);  // spec 9.4 steps 4-6, D34
  switch (g.verdict) {
    case Verdict::Execute: {
      phy_->on_authenticated();  // spec 12.4.2 step 5, as in on_config()
      if (c.pending.active) {
        // One execution at a time, as one relay board. The seq is consumed either way.
        c.gate.record(hdr.seq, AckResult::ActuatorBusy, 0);
        sink_printf(log_, "cmd %02x <- %02x seq %u: %s ACTUATOR_BUSY (seq %u executing)", c.id,
                    hdr.src, static_cast<unsigned>(hdr.seq), cmd_name(cmd.cmd),
                    static_cast<unsigned>(c.pending.seq));
        send_fresh_ack(c, app, hdr.src, hdr.seq, AckResult::ActuatorBusy, 0);
        return;
      }
      ++c.executions;
      const CommandOutcome o = app.execute(c, cmd, now_ms);
      c.pending              = PendingCommand{true, hdr.src, hdr.seq, cmd.cmd, o.result,
                                              o.detail, o.after, now_ms};
      if (o.deferred) {
        sink_printf(log_, "cmd %02x <- %02x seq %u: %s executing, ACK deferred", c.id, hdr.src,
                    static_cast<unsigned>(hdr.seq), cmd_name(cmd.cmd));
        return;
      }
      finish_command(c, app, now_ms);
      return;
    }
    case Verdict::ReturnCached:
      sink_printf(log_, "cmd %02x <- %02x seq %u: dedup hit, DUPLICATE_CACHED (%s), not executed",
                  c.id, hdr.src, static_cast<unsigned>(hdr.seq), ack_result_name(g.cached_result));
      send_ack(c, hdr.src, hdr.seq, AckResult::DuplicateCached,
               static_cast<uint8_t>(g.cached_result));
      return;
    case Verdict::InFlight:
      sink_printf(log_, "cmd %02x <- %02x seq %u: retry in flight, not answered (spec 9.4)", c.id,
                  hdr.src, static_cast<unsigned>(hdr.seq));
      return;
    case Verdict::Reject:
      sink_printf(log_, "cmd %02x <- %02x seq %u: REJECTED_SEQ, high water %u", c.id, hdr.src,
                  static_cast<unsigned>(hdr.seq), static_cast<unsigned>(c.gate.high_water()));
      send_ack(c, hdr.src, hdr.seq, AckResult::RejectedSeq, 0);
      return;
  }
}

void Engine::finish_command(Context& c, Application& app, uint32_t now_ms) {
  if (!c.pending.active) return;
  const PendingCommand p = c.pending;
  c.pending.active       = false;

  if (!c.gate.record(p.seq, p.result, p.detail)) {
    // Evicted while in flight (command_gate.h). A retry will read REJECTED_SEQ for a command
    // that ran, so say so here.
    sink_printf(log_, "cmd %02x seq %u: record() refused - evicted in flight", c.id,
                static_cast<unsigned>(p.seq));
  }
  sink_printf(log_, "cmd %02x <- %02x seq %u: %s %s", c.id, p.peer, static_cast<unsigned>(p.seq),
              cmd_name(p.cmd), ack_result_name(p.result));
  send_fresh_ack(c, app, p.peer, p.seq, p.result, p.detail);

  switch (p.after) {
    case AfterAck::None:
      break;
    case AfterAck::Status:
      send_status(c, app, p.peer, StatusReason::PollResponse, now_ms);
      break;
    case AfterAck::ConfigReadback:
      send_config_readback(c, app, p.peer);
      break;
    case AfterAck::Reboot:
      // spec 8.1 - the ACK above goes out under the current ctx_id, and the board resets
      // once it is on the air. The new context, the BOOT status and the BOOT event come from
      // the next boot's announce_boot(), as after any other reset (spec 10.7).
      restart_owed_ = true;
      sink_printf(log_, "id %02x REBOOT accepted: board restarts once the ACK is on the air", c.id);
      break;
  }
}

// ---------------------------------------------------------------------------
// CONFIG
// ---------------------------------------------------------------------------

void Engine::on_config(Context& c, Application& app, const Header& hdr, const uint8_t* payload,
                       size_t len, uint32_t now_ms) {
  if (app.withhold(c, "config", hdr)) return;

  // spec 9.4 applies steps 4-6 to every authenticated type, so a CONFIG shares the command
  // seq space and the gate. The step 4 and 5 answers are COMMAND_ACK, as 9.4 words them.
  const GateResult g = c.gate.check(hdr.seq);
  switch (g.verdict) {
    case Verdict::Execute:
      // spec 12.4.2 step 5 - through stage 11, so this CONFIG confirms a PHY trial. First,
      // so a confirming GET answers with the group committed.
      phy_->on_authenticated();
      break;
    case Verdict::ReturnCached:
      // spec 7.4.1 - a repeated GET or GET_ALL is answered by walking the table again, not
      // from the cache: a read applies nothing, and the cache holds one result, not an
      // answer of several messages. A repeated write keeps the cached answer.
      if (schema::deserialize(payload, len, &cfg_rx_) == Status::Ok &&
          (cfg_rx_.op == ConfigOp::Get || cfg_rx_.op == ConfigOp::GetAll)) {
        sink_printf(log_, "config %02x <- %02x seq %u: dedup hit on a read, answered again",
                    c.id, hdr.src, static_cast<unsigned>(hdr.seq));
        apply_config(c, app, cfg_rx_, now_ms);
        send_config_answer(c, hdr.src, hdr.seq);
        return;
      }
      sink_printf(log_, "config %02x <- %02x seq %u: dedup hit, DUPLICATE_CACHED, not applied",
                  c.id, hdr.src, static_cast<unsigned>(hdr.seq));
      send_ack(c, hdr.src, hdr.seq, AckResult::DuplicateCached,
               static_cast<uint8_t>(g.cached_result));
      return;
    case Verdict::InFlight:
      sink_printf(log_, "config %02x <- %02x seq %u: in flight, not answered", c.id, hdr.src,
                  static_cast<unsigned>(hdr.seq));
      return;
    case Verdict::Reject:
      send_ack(c, hdr.src, hdr.seq, AckResult::RejectedSeq, 0);
      return;
  }

  if (schema::deserialize(payload, len, &cfg_rx_) != Status::Ok) {
    c.gate.record(hdr.seq, AckResult::RejectedArg, 0);
    cfg_ack_                = kEmptyConfigAck;
    cfg_ack_.op             = static_cast<ConfigOp>(len > 0 ? payload[0] : 0);
    cfg_ack_.persist_status = PersistStatus::NotApplied;
    sink_printf(log_, "config %02x <- %02x seq %u: body did not parse, NOT_APPLIED", c.id, hdr.src,
                static_cast<unsigned>(hdr.seq));
    send_config_ack(c, hdr.src, cfg_ack_, hdr.seq);
    return;
  }

  ++c.executions;
  apply_config(c, app, cfg_rx_, now_ms);
  c.gate.record(hdr.seq, AckResult::Accepted, 0);
  sink_printf(log_, "config %02x <- %02x seq %u: op %u, %u entr%s, %u result(s)", c.id, hdr.src,
              static_cast<unsigned>(hdr.seq), static_cast<unsigned>(cfg_rx_.op),
              static_cast<unsigned>(cfg_rx_.count), cfg_rx_.count == 1 ? "y" : "ies",
              static_cast<unsigned>(nresults_));
  send_config_answer(c, hdr.src, hdr.seq);
}

namespace {

// Collects one answer's results; Engine::send_config_answer() splits them into messages
// (spec 7.4.1). Past kMaxResults a result is counted and logged there, never silent.
class ResultCollector final : public ConfigSink {
 public:
  ResultCollector(schema::ConfigAckEntry* out, size_t cap, size_t* n, size_t* dropped)
      : out_(out), cap_(cap), n_(n), dropped_(dropped) {}
  void add(const schema::ConfigAckEntry& a) override {
    if (*n_ >= cap_) {
      ++*dropped_;
      return;
    }
    out_[(*n_)++] = a;
  }

 private:
  schema::ConfigAckEntry* out_;
  size_t                  cap_;
  size_t*                 n_;
  size_t*                 dropped_;
};

// spec 7.4.1 - a full readback walks the table in ascending param_id. The application
// lists its rows in its own order and the PHY group follows them, so the engine sorts.
// Insertion sort: stable, no allocation, and at most kMaxResults entries.
void sort_by_param_id(schema::ConfigAckEntry* r, size_t n) {
  for (size_t i = 1; i < n; ++i) {
    const schema::ConfigAckEntry e = r[i];
    size_t                       j = i;
    while (j > 0 && r[j - 1].param_id > e.param_id) {
      r[j] = r[j - 1];
      --j;
    }
    r[j] = e;
  }
}

schema::ConfigAckEntry unknown_param(uint16_t id, PType t) {
  schema::ConfigAckEntry a;
  a.param_id = id;
  a.status   = ParamStatus::UnknownParam;
  a.ptype    = t;
  a.len      = 0;
  return a;
}

}  // namespace

void Engine::apply_config(Context& c, Application& app, const schema::NodeConfigV1& in,
                          uint32_t now_ms) {
  schema::NodeConfigAckV1* out = &cfg_ack_;
  *out                         = kEmptyConfigAck;
  out->op                      = in.op;
  nresults_                    = 0;
  results_dropped_             = 0;
  ResultCollector ack(results_, kMaxResults, &nresults_, &results_dropped_);

  // spec 7.4, D53 - persist_status after a write says what was applied; after a read,
  // whether the current overrides are persisted. The PHY group reports its own, which a
  // trial makes APPLIED_NOT_PERSISTED (D60).
  auto current = [&]() {
    return app.config_unpersisted(c) ? PersistStatus::AppliedNotPersisted
                                     : phy_->read_persist_status();
  };
  auto list_all = [&]() {
    app.config_list(c, &ack);
    for (const config::ParamDef& d : config::kNodeCommonParams) {
      if (PhyTrial::is_phy(d.id)) ack.add(phy_->get(d.id));
    }
  };

  switch (in.op) {
    case ConfigOp::Set: {
      bool applied   = false;
      bool phy_named = false;
      bool phy_ok    = true;
      for (uint8_t i = 0; i < in.count; ++i) {
        const schema::ConfigEntry& entry = in.entries[i];
        schema::ConfigAckEntry     a;
        if (PhyTrial::is_phy(entry.param_id)) {
          bool took = false;
          a         = phy_->set(entry, &took);
          phy_named = true;
          // spec 12.4.1 step 4 - the bridge abandons on a refusal or a clamp, so only an
          // OK entry counts toward the board's retune.
          phy_ok = phy_ok && a.status == ParamStatus::Ok;
        } else if (!app.config_set(c, entry, &a)) {
          a = unknown_param(entry.param_id, entry.ptype);
        }
        applied = applied || a.status == ParamStatus::Ok || a.status == ParamStatus::Clamped;
        ack.add(a);
      }
      if (phy_named) {
        const uint8_t members = app.phy_members();
        phy_->on_set(app.phy_slot(c), phy_ok, members, now_ms);
        sink_printf(log_, "phy %02x: SET %s, board %s, accepted 0x%02x of 0x%02x", c.id,
                    phy_ok ? "accepted" : "NOT accepted", phy_state_name(phy_->state()),
                    static_cast<unsigned>(phy_->accepted_mask()), static_cast<unsigned>(members));
      }
      // D53 - NOT_APPLIED only when nothing in the set took effect. Otherwise the answer is
      // the store's, read after the set: PERSISTED only when every override the node now
      // holds is on its store. A node with a working store reports PERSISTED, and one
      // whose store failed this write or an earlier one does not.
      out->persist_status = applied ? current() : PersistStatus::NotApplied;
      break;
    }
    case ConfigOp::Get:
      for (uint8_t i = 0; i < in.count; ++i) {
        const uint16_t         id = in.entries[i].param_id;
        schema::ConfigAckEntry a;
        if (PhyTrial::is_phy(id)) {
          a = phy_->get(id);
        } else if (!app.config_get(c, id, &a)) {
          a = unknown_param(id, in.entries[i].ptype);
        }
        ack.add(a);
      }
      out->persist_status = current();
      break;
    case ConfigOp::GetAll:
      list_all();
      sort_by_param_id(results_, nresults_);
      out->persist_status = current();
      break;
    case ConfigOp::RestoreDefaults:
      // The PHY group is kept, as lran-config's Store::restore_defaults() keeps it (D60).
      // D52 - answered with the full effective configuration, as GET_ALL is.
      app.config_restore_defaults(c);
      list_all();
      sort_by_param_id(results_, nresults_);
      out->persist_status = current();
      break;
    default:
      out->persist_status = PersistStatus::NotApplied;
      sink_printf(log_, "config %02x: unknown op 0x%02x, not applied", c.id,
                  static_cast<unsigned>(in.op));
      break;
  }
}

// ---------------------------------------------------------------------------
// HEX transport, spec 7.6
// ---------------------------------------------------------------------------

void Engine::on_hex_req(Context& c, Application& app, const Header& hdr, const uint8_t* payload,
                        size_t len, uint32_t now_ms) {
  if (app.withhold(c, "HEX_REQ", hdr)) return;
  ++c.hex_requests;

  // The codec has already checked the MAC of a write-class request (spec 9.4 step 3). The
  // class comes from the command nibble, never from `flags` bit 0, which is declared by the
  // sender and not trusted (spec 7.6).
  const bool write = hex_req_is_write_class(payload, len);
  if (write) {
    // spec 9.4 applies steps 4-6 to every authenticated type, so a write-class HEX_REQ
    // shares the command seq space and the gate, as a CONFIG does. Without it a captured
    // Set could be replayed for as long as the ctx_id lasts.
    const GateResult g = c.gate.check(hdr.seq);
    switch (g.verdict) {
      case Verdict::Execute:
        phy_->on_authenticated();  // spec 12.4.2 step 5, as on_config()
        break;
      case Verdict::ReturnCached:
        sink_printf(log_, "hex %02x <- %02x seq %u: dedup hit, DUPLICATE_CACHED, not forwarded",
                    c.id, hdr.src, static_cast<unsigned>(hdr.seq));
        send_ack(c, hdr.src, hdr.seq, AckResult::DuplicateCached,
                 static_cast<uint8_t>(g.cached_result));
        return;
      case Verdict::InFlight:
        return;
      case Verdict::Reject:
        sink_printf(log_, "hex %02x <- %02x seq %u: REJECTED_SEQ, high water %u", c.id, hdr.src,
                    static_cast<unsigned>(hdr.seq), static_cast<unsigned>(c.gate.high_water()));
        send_ack(c, hdr.src, hdr.seq, AckResult::RejectedSeq, 0);
        return;
    }
    // Recorded before the device is asked: spec 9.4 step 6 moves the state before dispatch,
    // and a write the node forwarded has consumed its seq whatever the device answers.
    c.gate.record(hdr.seq, AckResult::Accepted, 0);
  }

  msg::HexReq req;
  // The node's own shape check, the same one hex_req_is_write_class() makes: a colon and a
  // hex command nibble. Nothing else in the string is the node's to judge (spec 6.7); a bad
  // checksum goes to the device, which answers it with a frame error.
  const bool shaped = msg::deserialize(payload, len, &req) == Status::Ok && req.n >= 2 &&
                      req.hex[0] == ':' &&
                      std::strchr("0123456789ABCDEFabcdef", req.hex[1]) != nullptr &&
                      req.hex[1] != '\0';
  if (!shaped) {
    sink_printf(log_, "hex %02x <- %02x seq %u: MALFORMED_REQUEST", c.id, hdr.src,
                static_cast<unsigned>(hdr.seq));
    send_hex_rsp(c, hdr.src, hdr.seq, HexStatus::MalformedRequest, nullptr, 0);
    return;
  }
  if (c.hex_pending.active) {
    sink_printf(log_, "hex %02x <- %02x seq %u: transaction outstanding, BUSY", c.id, hdr.src,
                static_cast<unsigned>(hdr.seq));
    send_hex_rsp(c, hdr.src, hdr.seq, HexStatus::Busy, nullptr, 0);
    return;
  }

  // As long as a HEX_RSP can carry. The engine knows no device, so no device's limit.
  char   rsp[kMaxPayloadPlain - msg::kHexRspHdrLen];
  size_t rsp_n = 0;
  if (app.hex_forward(c, reinterpret_cast<const char*>(req.hex), req.n, rsp, sizeof(rsp), &rsp_n,
                      now_ms) == HexReply::Pending) {
    // spec 8.13 - TIMEOUT rather than silence, after the node's own wait.
    c.hex_pending = {true, now_ms + app.hex_timeout_ms(c), hdr.src, hdr.seq};
    return;
  }
  sink_printf(log_, "hex %02x <- %02x seq %u: %s %.*s -> %.*s", c.id, hdr.src,
              static_cast<unsigned>(hdr.seq), write ? "write" : "read", static_cast<int>(req.n),
              reinterpret_cast<const char*>(req.hex), static_cast<int>(rsp_n), rsp);
  send_hex_rsp(c, hdr.src, hdr.seq, HexStatus::Ok, rsp, rsp_n);
}

bool Engine::complete_hex(Context& c, HexStatus status, const char* hex, size_t n) {
  if (!c.hex_pending.active) return false;
  c.hex_pending.active = false;
  return send_hex_rsp(c, c.hex_pending.peer, c.hex_pending.seq, status, hex, n);
}

// ---------------------------------------------------------------------------
// Boot and PHY revert, spec 10.7 and 12.4.2 step 8
// ---------------------------------------------------------------------------

bool Engine::announce_boot(Context& c, Application& app, ResetCause cause, uint32_t now_ms) {
  // spec 10.7 - the first STATUS carries BOOT, and the event follows it. The event is what
  // says why: without the cause a watchdog reset and a directed reboot look alike (8.14).
  const bool status_queued = send_status(c, app, kNodeBridge, StatusReason::Boot, now_ms);
  uint8_t    payload[kMaxSchemaPayload];
  uint8_t    schema = kSchemaNone;
  // The high byte of a BOOT event's detail is reserved, 0.
  const size_t n = app.build_event(c, EventType::Boot, static_cast<uint16_t>(cause), now_ms,
                                   payload, sizeof(payload), &schema);
  const bool event_queued = n > 0 && send_event(c, kNodeBridge, payload, n, schema);
  if (!event_queued) ++answers_dropped_;
  return status_queued && event_queued;
}

void Engine::note_phy_revert(Context& c, RevertCause cause) {
  c.phy_revert_detail  = static_cast<uint16_t>(cause);
  c.config_change_owed = true;
}

void Engine::send_phy_reverted(Context& c, Application& app, NodeId dst, uint32_t now_ms) {
  const uint16_t detail = c.phy_revert_detail;
  c.phy_revert_detail   = 0;
  uint8_t      payload[kMaxSchemaPayload];
  uint8_t      schema = kSchemaNone;
  const size_t n      = app.build_event(c, EventType::PhyReverted, detail, now_ms, payload,
                                        sizeof(payload), &schema);
  if (n == 0 || !send_event(c, dst, payload, n, schema)) {
    ++answers_dropped_;
    sink_printf(log_, "phy %02x: PHY_REVERTED not queued", c.id);
    return;
  }
  sink_printf(log_, "phy %02x: EVENT PHY_REVERTED detail 0x%04x", c.id,
              static_cast<unsigned>(detail));
}

}  // namespace lran::node
