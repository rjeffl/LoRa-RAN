// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Tasks BF-3, BF-5; GateLink task L1 moved the protocol to lran-node. See node.h.

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
// Names
// ---------------------------------------------------------------------------

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
    : ids_(ids), out_(outbox), log_(log), engine_(outbox, mac, log) {}

void Node::on_phy_crc_error(uint32_t now_ms) {
  last_rx_       = LastRx{};
  last_rx_.kind  = RxKind::PhyCrc;
  last_rx_.at_ms = now_ms;
  for (size_t i = 0; i < kMaxIdentities; ++i) {
    Identity& e = ids_->slot(i);
    if (!e.used || !e.enabled) continue;
    lran::node::Engine::on_phy_crc_error(e);
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
    const lran::node::Engine::RxResult r =
        engine_.receive(e, app_, buf, len, rssi_dbm, snr_db10, now_ms);
    if (!r.decoded) continue;
    last_rx_.kind = RxKind::Frame;
    last_rx_.src  = r.hdr.src;
    last_rx_.dst  = r.hdr.dst;
    last_rx_.type = r.hdr.type;
  }
}

size_t build_health_payload(const Identity& e, const lran::Counters& radio, uint16_t boot_count,
                            uint32_t now_ms, uint8_t* out, size_t cap) {
  lran::schema::NodeHealthV1 h;
  h.uptime_s      = now_ms / 1000;  // millis() wraps at 49.7 days; a bench board reboots first
  // The board's count from NVS. Spec 7.5 names no sentinel; root rule 6's stands for none.
  h.boot_count    = boot_count != 0 ? boot_count : lran::kU16NotAvailable;
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

// spec 6.4 / 7.5 - a POLL is answered with STATUS schema 0xF0, from this identity's own
// context and status seq space.
void Node::answer_poll(Identity& e, const lran::Header& hdr, uint32_t now_ms) {
  if (silenced(e, "poll", hdr)) return;

  uint8_t      payload[lran::schema::kNodeHealthV1Len];
  const size_t n = build_health_payload(e, radio_counters_, boot_count_, now_ms, payload, sizeof(payload));
  lran::Header out;
  out.ver    = e.proto_ver;
  out.type   = lran::MsgType::Status;
  out.src    = e.id;
  out.dst    = hdr.src;
  out.seq    = e.tx_seq++;
  out.ctx_id = e.ctx_id;
  out.schema = lran::kSchemaNodeHealthV1;

  if (n == 0 || !engine_.send(e, out, payload, n, 0)) {
    engine_.count_dropped();
    sink_printf(log_, "poll %02x <- %02x: answer not queued", e.id, hdr.src);
  }
}

// False leaves the engine to count the frame unhandled.
bool Node::on_ping(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
                   uint8_t fragments, int16_t rssi_dbm, int16_t snr_db10, uint32_t now_ms) {
  lran::msg::Ping p;
  if (lran::msg::deserialize(payload, len, &p) != lran::Status::Ok) return false;

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
    return true;
  }

  if (e.role != Role::Range) return false;
  if (silenced(e, "ping", hdr)) return true;

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

  if (!engine_.send(e, echo, payload, len, fragments > 1 ? e.rx_chunk : 0)) {
    engine_.count_dropped();
    sink_printf(log_, "ping %02x <- %02x seq %u: echo not queued", e.id, hdr.src,
                static_cast<unsigned>(hdr.seq));
  }
  return true;
}

void Node::tick(uint32_t now_ms) {
  const RevertCause reverted = engine_.phy()->tick(now_ms);  // spec 12.4.2 step 6
  if (reverted != RevertCause::None) on_phy_revert(reverted);
  for (size_t i = 0; i < kMaxIdentities; ++i) {
    Identity& e = ids_->slot(i);
    if (!e.used) continue;
    e.reassembler.tick(now_ms);
    if (e.enabled) {
      engine_.tick_hex(e, now_ms);
      // `ack <hex> delay <ms>` - the deferred command's ACK falls due.
      if (e.pending.active && now_ms - e.pending.start_ms >= e.gl.pending_delay_ms) {
        engine_.finish_command(e, app_, now_ms);
      }
    }
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

  if (!engine_.send(*e, h, payload, len, frames > 1 ? frag_chunk : 0)) {
    return PingResult::EncodeFailed;
  }

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
