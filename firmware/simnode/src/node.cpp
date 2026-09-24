// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Tasks BF-3, BF-5; see node.h.

#include "node.h"

#include <cstring>

#include "lran/codec.h"
#include "lran/messages.h"
#include "lran/schema/node_health_v1.h"

namespace simnode {
namespace {

uint16_t sat16(uint32_t v) { return v > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(v); }

// The non-pattern echo bytes. Arbitrary by spec 6.6.3, but deterministic, so an initiator
// can still compare them.
uint8_t plain_fill(size_t i) { return static_cast<uint8_t>(0xA5 ^ i); }

}  // namespace

// ---------------------------------------------------------------------------
// Outbox
// ---------------------------------------------------------------------------

bool Outbox::push(const uint8_t* bytes, size_t len) {
  if (count_ == kOutboxDepth || len > kOutFrameMax) return false;
  OutFrame& f = frames_[(head_ + count_) % kOutboxDepth];
  std::memcpy(f.bytes, bytes, len);  // bytes already encoded; not a struct
  f.len = len;
  ++count_;
  return true;
}

bool Outbox::pop(OutFrame* out) {
  if (count_ == 0) return false;
  const OutFrame& f = frames_[head_];
  std::memcpy(out->bytes, f.bytes, f.len);
  out->len = f.len;
  head_    = (head_ + 1) % kOutboxDepth;
  --count_;
  return true;
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

const char* log_level_name(LogLevel l) {
  switch (l) {
    case LogLevel::Quiet: return "quiet";
    case LogLevel::Info:  return "info";
    case LogLevel::Debug: return "debug";
  }
  return "?";
}

bool parse_log_level(const char* token, LogLevel* out) {
  const LogLevel all[] = {LogLevel::Quiet, LogLevel::Info, LogLevel::Debug};
  for (LogLevel l : all) {
    if (std::strcmp(token, log_level_name(l)) == 0) {
      *out = l;
      return true;
    }
  }
  return false;
}

const char* ping_result_name(PingResult r) {
  switch (r) {
    case PingResult::Ok:           return "ok";
    case PingResult::NoIdentity:   return "no such identity";
    case PingResult::Disabled:     return "identity disabled";
    case PingResult::BadLength:    return "n above 202";
    case PingResult::BadChunk:     return "chunk needs more than 15 fragments";
    case PingResult::Pending:      return "previous echo still pending";
    case PingResult::OutboxFull:   return "outbox full";
    case PingResult::EncodeFailed: return "encode failed";
  }
  return "?";
}

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------

Node::Node(IdentityTable* ids, Outbox* outbox, lran::IMac* mac, Sink* log)
    : ids_(ids), out_(outbox), mac_(mac), log_(log) {}

void Node::on_phy_crc_error(uint32_t now_ms) {
  last_rx_       = LastRx{};
  last_rx_.kind  = RxKind::PhyCrc;
  last_rx_.at_ms = now_ms;
  for (size_t i = 0; i < kMaxIdentities; ++i) {
    Identity& e = ids_->slot(i);
    if (!e.used || !e.enabled) continue;
    ++e.counters.rx_frames;
    ++e.counters.rx_crc_err;
  }
}

void Node::on_rx(const uint8_t* buf, size_t len, int16_t rssi_dbm, int16_t snr_db10,
                 uint32_t now_ms) {
  // Upgraded to Frame below when any identity decodes the header. The header fields are read
  // only from a successful decode_header - never from raw offsets, which would be a second
  // parser (Impl Plan 10.6 rule 1's reasoning, applied to reading).
  last_rx_          = LastRx{};
  last_rx_.kind     = RxKind::HeaderDiscard;
  last_rx_.at_ms    = now_ms;
  last_rx_.len      = len;
  last_rx_.rssi_dbm = rssi_dbm;

  for (size_t i = 0; i < kMaxIdentities; ++i) {
    Identity& e = ids_->slot(i);
    if (!e.used || !e.enabled) continue;
    ++e.counters.rx_frames;

    // A node accepts only the version it speaks (spec 13.1); V-B10 sets it per identity.
    lran::DecodeCtx ctx;
    ctx.self           = e.id;
    ctx.accept_ver_min = e.proto_ver;
    ctx.accept_ver_max = e.proto_ver;
    ctx.counters       = &e.counters;

    lran::Frame        f;
    const lran::Status head = lran::decode_header(buf, len, ctx, &f);  // stages 2-6
    if (head != lran::Status::Ok) {
      if (level_ == LogLevel::Debug) {
        sink_printf(log_, "rx %02x: discarded at header, status %u", e.id,
                    static_cast<unsigned>(head));
      }
      continue;
    }

    last_rx_.kind = RxKind::Frame;
    last_rx_.src  = f.hdr.src;
    last_rx_.dst  = f.hdr.dst;
    last_rx_.type = f.hdr.type;

    e.heard         = true;
    e.last_rssi_dbm = rssi_dbm;
    e.last_snr_db10 = snr_db10;

    // spec 9.4 steps 2 and 3. The codec applies the ctx check to authenticated types only,
    // so expecting this identity's own context on every frame is correct.
    ctx.mac           = mac_;
    ctx.node_key      = e.key;
    ctx.expect_ctx_id = e.ctx_id;
    const lran::Status body = lran::decode_payload(buf, len, ctx, &f);  // stages 7-9
    if (body != lran::Status::Ok) {
      if (e.role == Role::GateLink &&
          (body == lran::Status::RejectedCtx || body == lran::Status::RejectedMac)) {
        refuse_authenticated(e, f.hdr, body);  // spec 9.4 steps 2-3
      } else if (level_ == LogLevel::Debug) {
        sink_printf(log_, "rx %02x: discarded at payload, status %u", e.id,
                    static_cast<unsigned>(body));
      }
      continue;
    }

    // The codec answers Ok for an authenticated frame whose MAC it had no key to check. mac_
    // and the key are always set here, so this cannot fire; the check is what keeps that true.
    if (lran::frame_has_mac(f.hdr.type, f.payload, f.payload_len) && !f.mac_verified) {
      ++e.unhandled;
      sink_printf(log_, "rx %02x <- %02x seq %u: authenticated type with no verified MAC, ignored",
                  e.id, f.hdr.src, static_cast<unsigned>(f.hdr.seq));
      continue;
    }

    if (f.hdr.frag_total() == 1) {
      deliver(e, f.hdr, f.payload, f.payload_len, 1, rssi_dbm, snr_db10, now_ms);
      continue;
    }

    // spec 11 - stage 10. A new set starts the chunk inference afresh.
    if (!e.reassembler.active()) e.rx_chunk = 0;
    const lran::Status st = e.reassembler.accept(f, now_ms);
    if (st == lran::Status::Ok && f.payload_len > e.rx_chunk) {
      e.rx_chunk = static_cast<uint8_t>(f.payload_len);
    }
    if (st != lran::Status::Ok || !e.reassembler.complete()) continue;

    deliver(e, f.hdr, e.reassembler.data(), e.reassembler.len(), f.hdr.frag_total(), rssi_dbm,
            snr_db10, now_ms);
  }
}

void Node::on_error(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len) {
  lran::msg::Error err;
  if (lran::msg::deserialize(payload, len, &err) != lran::Status::Ok) {
    sink_printf(log_, "rx %02x <- %02x ERROR: %u B, would not decode", e.id, hdr.src,
                static_cast<unsigned>(len));
    return;
  }
  // spec 8.8's err_code, printed raw: the library has no to_string for it, and a bench
  // instrument is the wrong place to grow one.
  sink_printf(log_, "rx %02x <- %02x ERROR err_code 0x%02x detail 0x%02x ref_seq %u", e.id,
              hdr.src, static_cast<unsigned>(err.err_code), static_cast<unsigned>(err.detail),
              static_cast<unsigned>(err.ref_seq));
}

void Node::deliver(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
                   uint8_t fragments, int16_t rssi_dbm, int16_t snr_db10, uint32_t now_ms) {
  if (level_ == LogLevel::Debug) {
    sink_printf(log_, "rx %02x <- %02x type 0x%02x seq %u, %u B in %u frame(s)", e.id, hdr.src,
                static_cast<unsigned>(hdr.type), static_cast<unsigned>(hdr.seq),
                static_cast<unsigned>(len), static_cast<unsigned>(fragments));
  }

  // spec 12.4.2 step 8 - a revert is reported when the next frame from the bridge arrives,
  // which is the evidence that the restored settings reach it.
  if (hdr.src == lran::kNodeBridge && e.role == Role::GateLink && e.gl.phy_revert_detail != 0) {
    send_phy_reverted(e, hdr.src, now_ms);
  }

  switch (hdr.type) {
    case lran::MsgType::Error:
      // BF-19a - the bridge's spec 14.2 reply. The simnode acts on none of it; it is
      // logged because the catalogue's only on-air evidence of WHICH spec 14 stage fired
      // is the err_code, and every other line here reports type and length alone.
      on_error(e, hdr, payload, len);
      return;
    case lran::MsgType::Ping:
      on_ping(e, hdr, payload, len, fragments, rssi_dbm, snr_db10, now_ms);
      return;
    case lran::MsgType::Poll:
      if (e.role == Role::Range || e.role == Role::Health) {
        answer_poll(e, hdr, now_ms);
        return;
      }
      if (e.role == Role::GateLink) {
        answer_poll_gatelink(e, hdr, payload, len, now_ms);
        return;
      }
      break;
    case lran::MsgType::Command:
      if (e.role == Role::GateLink) {
        on_command(e, hdr, payload, len, now_ms);
        return;
      }
      if (answer_roll_only(e, hdr, payload, len)) return;
      break;
    case lran::MsgType::Config:
      // BF-33 - every role but ROLE_FAULT answers a CONFIG, for the PHY group at least
      // (apply_config()), because the bridge moves every node it polls (spec 12.4.1).
      if (e.role != Role::Fault) {
        on_config(e, hdr, payload, len, now_ms);
        return;
      }
      break;
    default:
      break;
  }
  ++e.unhandled;
}

size_t build_health_payload(const Identity& e, const lran::Counters& radio, uint32_t now_ms,
                            uint8_t* out, size_t cap) {
  lran::schema::NodeHealthV1 h;
  h.uptime_s      = now_ms / 1000;  // millis() wraps at 49.7 days; a bench board reboots first
  h.boot_count    = lran::kU16NotAvailable;  // nothing persists across a simnode reboot
  h.rx_frames     = sat16(e.counters.rx_frames);
  h.tx_frames     = sat16(e.counters.tx_frames);
  h.rx_dropped    = sat16(e.counters.total_dropped());
  h.cad_backoffs  = sat16(radio.cad_backoffs);
  h.last_rssi_dbm = e.heard ? e.last_rssi_dbm : lran::kI16NotAvailable;
  h.last_snr_db10 = e.heard ? e.last_snr_db10 : lran::kI16NotAvailable;
  h.proto_ver     = e.proto_ver;
  h.health_flags  = lran::schema::kHealthFlagDebugActive;  // the synthetic marker - node.h

  size_t n = 0;
  return lran::schema::serialize(h, out, cap, &n) == lran::Status::Ok ? n : 0;
}

bool Node::silenced(Identity& e, const char* what, const lran::Header& hdr) {
  if (e.silent_left == 0) return false;
  --e.silent_left;
  ++e.answers_suppressed;
  sink_printf(log_, "fault %02x silent: %s from %02x seq %u not answered, %u left", e.id, what,
              hdr.src, static_cast<unsigned>(hdr.seq), static_cast<unsigned>(e.silent_left));
  return true;
}

void Node::on_roll(Identity& e, const lran::Header& hdr, const lran::msg::Command& c) {
  // spec 10.6 - the same guard as REBOOT, and the same answer to a wrong one.
  if (c.arg != lran::kRollContextGuard) {
    sink_printf(log_, "roll %02x <- %02x seq %u: arg 0x%02x, REJECTED_ARG", e.id, hdr.src,
                static_cast<unsigned>(hdr.seq), static_cast<unsigned>(c.arg));
    send_ack(e, hdr.src, hdr.seq, lran::AckResult::RejectedArg, 0);
    return;
  }
  // spec 10.6 node step 1 - a command still executing keeps its context. Emptying the cache
  // now would drop its result, and the bridge's retry of that command would meet a fresh
  // cache and run it a second time. The bridge retries the roll under the same seq.
  if (e.gate.any_in_flight()) {
    sink_printf(log_, "roll %02x <- %02x seq %u: a command is in flight, ACTUATOR_BUSY", e.id,
                hdr.src, static_cast<unsigned>(hdr.seq));
    send_ack(e, hdr.src, hdr.seq, lran::AckResult::ActuatorBusy, 0);
    return;
  }
  // spec 12.4.1 - the roll after a bridge restart is an authenticated frame, and it
  // confirms a node still in its trial. It skips the gate, so it passes stage 11 here.
  phy_->on_authenticated();
  const lran::CtxId old_ctx = e.ctx_id;
  ids_->roll_context(e.id);
  sink_printf(log_, "roll %02x <- %02x seq %u: ctx 0x%08lx -> 0x%08lx, ACCEPTED", e.id, hdr.src,
              static_cast<unsigned>(hdr.seq), static_cast<unsigned long>(old_ctx),
              static_cast<unsigned long>(e.ctx_id));
  // spec 10.6 node step 3 - under the NEW ctx_id with status seq 1, the first frame of the
  // new context. Through send_fresh_ack(), so the ack_suppress fault can lose it and the
  // bridge's retry reaches spec 10.6 bridge step 4, REJECTED_CTX completing the roll.
  send_fresh_ack(e, hdr.src, hdr.seq, lran::AckResult::Accepted, 0);
}

bool Node::answer_roll_only(Identity& e, const lran::Header& hdr, const uint8_t* payload,
                            size_t len) {
  lran::msg::Command c;
  if (lran::msg::deserialize(payload, len, &c) != lran::Status::Ok) return false;
  if (c.cmd != static_cast<uint8_t>(lran::Cmd::RollContext)) return false;
  if (!silenced(e, "roll", hdr)) on_roll(e, hdr, c);
  return true;
}

// spec 6.4 / 7.5 - a POLL is answered with STATUS schema 0xF0, from this identity's own
// context and status seq space.
void Node::answer_poll(Identity& e, const lran::Header& hdr, uint32_t now_ms) {
  if (silenced(e, "poll", hdr)) return;

  uint8_t      payload[lran::schema::kNodeHealthV1Len];
  const size_t n = build_health_payload(e, radio_counters_, now_ms, payload, sizeof(payload));
  lran::Header out;
  out.ver    = e.proto_ver;
  out.type   = lran::MsgType::Status;
  out.src    = e.id;
  out.dst    = hdr.src;
  out.seq    = e.tx_seq++;
  out.ctx_id = e.ctx_id;
  out.schema = lran::kSchemaNodeHealthV1;

  if (n == 0 || !send(e, out, payload, n, 0)) {
    ++answers_dropped_;
    sink_printf(log_, "poll %02x <- %02x: answer not queued", e.id, hdr.src);
  }
}

void Node::on_ping(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
                   uint8_t fragments, int16_t rssi_dbm, int16_t snr_db10, uint32_t now_ms) {
  lran::msg::Ping p;
  if (lran::msg::deserialize(payload, len, &p) != lran::Status::Ok) {
    ++e.unhandled;
    return;
  }

  // THE ECHO OF OUR OWN PING. Recognised by peer and seq and never echoed back: two
  // simnodes echoing each other's echoes would fill the channel.
  if (e.ping.active && hdr.src == e.ping.peer && hdr.seq == e.ping.seq) {
    bool   ok        = p.n == e.ping.n && p.ping_flags == e.ping.flags;
    size_t first_bad = 0;
    if (ok && (p.ping_flags & lran::kPingFlagPatternFill) != 0) {
      ok = lran::msg::ping_check_pattern(hdr.seq, p.data, p.n, &first_bad);
    } else if (ok) {
      for (size_t i = 0; i < p.n; ++i) {
        if (p.data[i] != plain_fill(i)) {
          ok        = false;
          first_bad = i;
          break;
        }
      }
    }
    const unsigned rtt = static_cast<unsigned>(now_ms - e.ping.sent_ms);
    if (ok) {
      sink_printf(log_,
                  "ping %02x -> %02x seq %u: echo ok, n %u, %u frame(s) out, %u back, "
                  "rssi %d dBm, snr %.1f dB, %u ms",
                  e.id, hdr.src, static_cast<unsigned>(hdr.seq), static_cast<unsigned>(p.n),
                  static_cast<unsigned>(e.ping.fragments), static_cast<unsigned>(fragments),
                  static_cast<int>(rssi_dbm), static_cast<double>(snr_db10) / 10.0, rtt);
    } else {
      sink_printf(log_, "ping %02x -> %02x seq %u: ECHO MISMATCH, n %u of %u, first bad byte %u",
                  e.id, hdr.src, static_cast<unsigned>(hdr.seq), static_cast<unsigned>(p.n),
                  static_cast<unsigned>(e.ping.n), static_cast<unsigned>(first_bad));
    }
    e.ping = PendingPing{};
    return;
  }

  if (e.role != Role::Range) {
    ++e.unhandled;
    return;
  }
  if (silenced(e, "ping", hdr)) return;

  // spec 6.6 / 17.3 - swap src and dst, preserve seq, ping_flags and the echo bytes. The
  // header's ctx_id and ver are this node's own. A fragmented PING is re-fragmented at the
  // chunk the initiator used (spec 6.6.2).
  lran::Header echo;
  echo.ver    = e.proto_ver;
  echo.type   = lran::MsgType::Ping;
  echo.src    = e.id;
  echo.dst    = hdr.src;
  echo.seq    = hdr.seq;
  echo.ctx_id = e.ctx_id;
  echo.schema = lran::kSchemaNone;

  if (!send(e, echo, payload, len, fragments > 1 ? e.rx_chunk : 0)) {
    ++answers_dropped_;
    sink_printf(log_, "ping %02x <- %02x seq %u: echo not queued", e.id, hdr.src,
                static_cast<unsigned>(hdr.seq));
  }
}

bool Node::send(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
                uint8_t chunk) {
  lran::EncodeCtx ectx;
  ectx.mac      = mac_;
  ectx.node_key = e.key;

  uint8_t buf[lran::kMaxFrame];
  size_t  n = 0;

  if (chunk == 0 || chunk >= len) {
    if (out_->free_slots() < 1) return false;
    if (lran::encode(hdr, payload, len, ectx, buf, sizeof(buf), &n) != lran::Status::Ok) {
      return false;
    }
    out_->push(buf, n);
    ++e.counters.tx_frames;
    return true;
  }

  const uint8_t total = lran::fragment_count(len, chunk);
  if (total == 0 || out_->free_slots() < total) return false;
  for (uint8_t i = 0; i < total; ++i) {
    if (lran::encode_fragment(hdr, payload, len, i, chunk, ectx, buf, sizeof(buf), &n) !=
        lran::Status::Ok) {
      // Only possible on the first fragment in practice; the set's shape is fixed by then.
      return false;
    }
    out_->push(buf, n);
    ++e.counters.tx_frames;
  }
  return true;
}

void Node::tick(uint32_t now_ms) {
  const RevertCause reverted = phy_->tick(now_ms);  // spec 12.4.2 step 6
  if (reverted != RevertCause::None) on_phy_revert(reverted);
  for (size_t i = 0; i < kMaxIdentities; ++i) {
    Identity& e = ids_->slot(i);
    if (!e.used) continue;
    e.reassembler.tick(now_ms);
    tick_gatelink(e, now_ms);
    if (e.ping.active && now_ms - e.ping.sent_ms >= ping_timeout_ms_) {
      sink_printf(log_, "ping %02x -> %02x seq %u: no echo in %u ms", e.id, e.ping.peer,
                  static_cast<unsigned>(e.ping.seq), static_cast<unsigned>(ping_timeout_ms_));
      e.ping = PendingPing{};
    }
  }
}

PingResult Node::ping(lran::NodeId id, uint8_t n, bool pattern, uint8_t frag_chunk,
                      lran::NodeId dst, uint32_t now_ms) {
  Identity* e = ids_->find(id);
  if (e == nullptr) return PingResult::NoIdentity;
  if (!e->enabled) return PingResult::Disabled;
  if (n > lran::kPingMaxEcho) return PingResult::BadLength;
  if (e->ping.active) return PingResult::Pending;

  const size_t len = lran::msg::kPingHdrLen + n;
  uint8_t      frames = 1;
  if (frag_chunk != 0 && frag_chunk < len) {
    frames = lran::fragment_count(len, frag_chunk);
    if (frames == 0) return PingResult::BadChunk;
  }
  if (out_->free_slots() < frames) return PingResult::OutboxFull;

  const lran::Seq seq = e->tx_seq++;

  uint8_t payload[lran::kMaxPayloadPlain];
  payload[0] = pattern ? lran::kPingFlagPatternFill : 0;
  payload[1] = n;
  if (pattern) {
    lran::msg::ping_fill_pattern(seq, payload + lran::msg::kPingHdrLen, n);
  } else {
    for (size_t i = 0; i < n; ++i) payload[lran::msg::kPingHdrLen + i] = plain_fill(i);
  }

  lran::Header h;
  h.ver    = e->proto_ver;
  h.type   = lran::MsgType::Ping;
  h.src    = e->id;
  h.dst    = dst;
  h.seq    = seq;
  h.ctx_id = e->ctx_id;
  h.schema = lran::kSchemaNone;

  if (!send(*e, h, payload, len, frames > 1 ? frag_chunk : 0)) return PingResult::EncodeFailed;

  e->ping.active    = true;
  e->ping.peer      = dst;
  e->ping.seq       = seq;
  e->ping.flags     = payload[0];
  e->ping.n         = n;
  e->ping.fragments = frames;
  e->ping.sent_ms   = now_ms;
  return PingResult::Ok;
}

}  // namespace simnode
