// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL3; see gatelink_app.h.

#include "gatelink_app.h"

#include <cstring>

#include "lran/schema/gatelink_event_v1.h"
#include "lran/schema/gatelink_status_v1.h"
#include "tasks.h"

namespace gatelink {
namespace {

using lran::AckResult;
using lran::Cmd;

// K1-K4 as RelayPulser numbers them (PRD 3.1.2).
constexpr uint8_t kK1OpenLock  = 0;
constexpr uint8_t kK2Unlock    = 1;
constexpr uint8_t kK3Open      = 2;
constexpr uint8_t kK4CloseNow  = 3;

// spec 7.2.7 bits 7:6: soc_source unknown, and bit 0 clear: no BMS read yet.
constexpr uint8_t kBmsSocSourceUnknown = 0xC0;
// spec 7.2.8.
constexpr uint8_t kNodeFlagPersisted      = 0x01;
constexpr uint8_t kNodeFlagCardWritable   = 0x02;
constexpr uint8_t kNodeFlagDryRun         = 0x04;
constexpr uint8_t kNodeFlagBmsPolling     = 0x08;
constexpr uint8_t kNodeFlagDebug          = 0x10;
constexpr uint8_t kNodeFlagAgeNotPersisted = 0x40;

// spec 6.6 - PING's echo. A responder swaps src and dst and keeps seq, the flags and the
// bytes; its header carries its own ctx_id and ver.
lran::Header echo_header(const lran::node::Context& c, const lran::Header& hdr) {
  lran::Header h = lran::node::Engine::header(c, lran::MsgType::Ping, hdr.src, lran::kSchemaNone);
  h.seq          = hdr.seq;
  return h;
}

}  // namespace

bool relay_sequence_for(const lran::msg::Command& cmd, RelaySequence* out) {
  switch (static_cast<Cmd>(cmd.cmd)) {
    case Cmd::Open:
      *out = {kK3Open, kNoRelay};
      return true;
    case Cmd::HoldOpen:
      *out = {kK1OpenLock, kNoRelay};
      return true;
    case Cmd::ReleaseHold:
      *out = {kK2Unlock, kNoRelay};
      return true;
    case Cmd::Close:
      // spec 8.1 - arg 0 releases the hold, 1 releases it and closes at once. K2 precedes
      // K4 whatever the hold state (PRD R-3.1.2c): a locked controller ignores K4 alone.
      if (cmd.arg == 0) {
        *out = {kK2Unlock, kNoRelay};
        return true;
      }
      if (cmd.arg == 1) {
        *out = {kK2Unlock, kK4CloseNow};
        return true;
      }
      return false;
    default:
      return false;
  }
}

lran::node::AckDelivery GateLinkApp::fresh_ack(lran::node::Context&, lran::Seq seq) {
  if (!withhold_ack_) return lran::node::AckDelivery::Send;
  withhold_ack_ = false;
  ++acks_withheld_;
  lran::node::sink_printf(log_, "ack drop: ACK for seq %u withheld", static_cast<unsigned>(seq));
  return lran::node::AckDelivery::Suppress;
}

uint8_t GateLinkApp::capabilities(const lran::node::Context&) const {
  return lran::node::kAnswersCommands | lran::node::kAnswersConfig | lran::node::kAnswersHex |
         lran::node::kRefusesAuthenticated;
}

bool GateLinkApp::on_poll(lran::node::Engine& engine, lran::node::Context& c,
                          const lran::Header& hdr, const uint8_t* payload, size_t len,
                          uint32_t now_ms) {
  lran::msg::Poll p;
  const uint8_t   flags =
      lran::msg::deserialize(payload, len, &p) == lran::Status::Ok ? p.poll_flags : 0;
  if (!engine.send_status(c, *this, hdr.src, lran::StatusReason::PollResponse, now_ms)) {
    engine.count_dropped();
    lran::node::sink_printf(log_, "poll <- %02x: answer not queued", hdr.src);
  }
  // spec 6.4 bit 1 - the readback that recovers a lost CONFIG_ACK (spec 7.4), after the
  // STATUS.
  if ((flags & lran::kPollFlagConfigReadback) != 0 &&
      !engine.send_config_readback(c, *this, hdr.src)) {
    engine.count_dropped();
    lran::node::sink_printf(log_, "poll <- %02x: config readback not queued", hdr.src);
  }
  return true;
}

bool GateLinkApp::on_ping(lran::node::Engine& engine, lran::node::Context& c,
                          const lran::Header& hdr, const uint8_t* payload, size_t len,
                          uint8_t fragments, int16_t, int16_t, uint32_t) {
  lran::msg::Ping p;
  if (lran::msg::deserialize(payload, len, &p) != lran::Status::Ok) return false;
  // spec 6.6.2 - a fragmented PING is echoed in fragments of the chunk it arrived in.
  if (!engine.send(c, echo_header(c, hdr), payload, len, fragments > 1 ? c.rx_chunk : 0)) {
    engine.count_dropped();
    lran::node::sink_printf(log_, "ping <- %02x seq %u: echo not queued", hdr.src,
                            static_cast<unsigned>(hdr.seq));
  }
  return true;
}

lran::node::CommandOutcome GateLinkApp::execute(lran::node::Context&,
                                                const lran::msg::Command& cmd, uint32_t) {
  using lran::node::AfterAck;
  lran::node::CommandOutcome o;
  const Cmd                  which = static_cast<Cmd>(cmd.cmd);

  // spec 8.1 - 0x00-0x0F are the actuation commands.
  if (cmd.cmd != static_cast<uint8_t>(Cmd::Nop) && cmd.cmd <= 0x0F) {
    RelaySequence seq;
    if (!relay_sequence_for(cmd, &seq)) {
      o.result = which == Cmd::Close ? AckResult::RejectedArg : AckResult::RejectedUnknownCmd;
      return o;
    }
    if (dry_run_) {
      o.result = AckResult::DryRun;
      return o;
    }
    if (!port_->dispatch(seq)) {
      ++dispatch_refused_;
      o.result = AckResult::ActuatorBusy;
      return o;
    }
    ++dispatched_;
    o.deferred = true;  // the ACK waits for io_task's last trailing edge
    return o;
  }

  switch (which) {
    case Cmd::Nop:
      break;
    case Cmd::RequestStatus:
      o.after = AfterAck::Status;
      break;
    case Cmd::RequestConfig:
      o.after = AfterAck::ConfigReadback;  // spec 7.4 - the readback follows the ACK
      break;
    case Cmd::SetDebugMode:
      debug_modes_ = static_cast<uint8_t>(cmd.arg2);
      break;
    case Cmd::SetRelayDryRun:
      if (cmd.arg > 1) {
        o.result = AckResult::RejectedArg;
        break;
      }
      dry_run_ = cmd.arg == 1;
      break;
    case Cmd::SetBmsPolling:
      if (cmd.arg > 1) {
        o.result = AckResult::RejectedArg;
        break;
      }
      bms_polling_ = cmd.arg == 1;
      break;
    case Cmd::Reboot:
      if (cmd.arg != lran::kRebootGuard) {
        o.result = AckResult::RejectedArg;
        break;
      }
      o.after = AfterAck::Reboot;
      break;
    default:
      // ROLL_CONTEXT included: the engine answers it before the gate (spec 9.4), so it
      // never arrives here, and is refused rather than executed if that ever changes.
      o.result = AckResult::RejectedUnknownCmd;
      break;
  }
  return o;
}

size_t GateLinkApp::build_status(const lran::node::Context&, lran::StatusReason reason,
                                 uint32_t, uint8_t* out, size_t cap, uint8_t* schema) {
  const NodeSnapshot          n = port_->snapshot();
  lran::schema::GateLinkStatusV1 s;
  s.gate_state = static_cast<uint8_t>(lran::GateState::Unknown);
  s.input_bits = n.inputs;

  const MpptSnapshot m = port_->mppt();
  s.batt_mv       = m.batt_mv;
  s.batt_ma       = m.batt_ma;
  s.pv_cv         = m.pv_cv;
  s.pv_w          = m.pv_w;
  s.load_ma       = m.load_ma;
  s.yield_today   = m.yield_today;
  s.yield_yest    = m.yield_yest;
  s.pmax_today    = m.pmax_today;
  s.yield_total   = m.yield_total;
  s.charge_state  = m.charge_state;
  s.mppt_err      = m.mppt_err;
  s.mppt_tracker  = m.mppt_tracker;
  s.mppt_flags    = m.mppt_flags;
  s.mppt_temp_c10 = m.mppt_temp_c10;

  s.bms_flags        = kBmsSocSourceUnknown;
  s.pack_mv          = lran::kU16NotAvailable;
  s.pack_ma          = lran::kI16NotAvailable;
  s.bms_cycles       = lran::kU16NotAvailable;
  s.bms_capacity_dah = lran::kU16NotAvailable;

  s.uptime_s           = n.uptime_s;
  s.boot_count         = n.boot_count;
  s.node_mv            = n.node_mv;
  s.node_ma            = lran::kI16NotAvailable;  // R-4.4b: the INA226 gives VIN only
  s.enclosure_temp_c10 = n.enclosure_temp_c10;
  s.node_flags         = static_cast<uint8_t>(
      (cfg_ == nullptr || !cfg_->unpersisted() ? kNodeFlagPersisted : 0) |
      (cfg_ != nullptr && cfg_->persist().usable() ? kNodeFlagCardWritable : 0) |
      kNodeFlagAgeNotPersisted | (dry_run_ ? kNodeFlagDryRun : 0) |
      (bms_polling_ ? kNodeFlagBmsPolling : 0) | (debug_modes_ != 0 ? kNodeFlagDebug : 0));
  s.status_reason = static_cast<uint8_t>(reason);

  *schema   = lran::kSchemaGateLinkStatusV1;
  size_t nw = 0;
  return lran::schema::serialize(s, out, cap, &nw) == lran::Status::Ok ? nw : 0;
}

size_t GateLinkApp::build_event(lran::node::Context&, lran::EventType type, uint16_t detail,
                                uint32_t, uint8_t* out, size_t cap, uint8_t* schema) {
  const NodeSnapshot           n = port_->snapshot();
  lran::schema::GateLinkEventV1 ev;
  ev.event_type = static_cast<uint8_t>(type);
  ev.gate_state = static_cast<uint8_t>(lran::GateState::Unknown);
  ev.input_bits = n.inputs;
  ev.detail     = detail;
  ev.event_id   = ++event_id_;
  ev.uptime_s   = n.uptime_s;

  *schema   = lran::kSchemaGateLinkEventV1;
  size_t nw = 0;
  return lran::schema::serialize(ev, out, cap, &nw) == lran::Status::Ok ? nw : 0;
}

lran::node::HexReply GateLinkApp::hex_forward(lran::node::Context&, const char* req, size_t n,
                                              char*, size_t, size_t*, uint32_t) {
  HexJob job;
  job.token  = ++hex_token_;
  if (n > vedirect::kMaxChars) {
    // Longer than any VE.Direct frame (hex.h), so the MPPT could only refuse it.
    hex_refusal_owed_ = true;
    hex_refusal_      = lran::HexStatus::MalformedRequest;
    return lran::node::HexReply::Pending;
  }
  std::memcpy(job.hex, req, n);
  job.n = n;
  if (!port_->hex_submit(job)) {
    // vedirect_task still holds a job the engine has given up on.
    hex_refusal_owed_ = true;
    hex_refusal_      = lran::HexStatus::Busy;
  }
  return lran::node::HexReply::Pending;
}

void GateLinkApp::poll_hex(lran::node::Engine& engine, lran::node::Context& c) {
  if (hex_refusal_owed_) {
    hex_refusal_owed_ = false;
    engine.complete_hex(c, hex_refusal_, nullptr, 0);
  }
  HexResult r;
  while (port_->hex_take(&r)) {
    // A token other than the last job's, or no transaction pending, is an answer the engine
    // has already timed out. Completing with it would answer a later request.
    if (r.token != hex_token_ || !c.hex_pending.active) {
      ++hex_late_;
      lran::node::sink_printf(log_, "hex: answer to job %lu after its deadline, discarded",
                              static_cast<unsigned long>(r.token));
      continue;
    }
    lran::node::sink_printf(log_, "hex %02x -> %02x seq %u: status %u %.*s", c.id,
                            c.hex_pending.peer, static_cast<unsigned>(c.hex_pending.seq),
                            static_cast<unsigned>(r.status), static_cast<int>(r.n), r.hex);
    engine.complete_hex(c, r.status, r.hex, r.n);
  }
}

// ---------------------------------------------------------------------------
// CONFIG, spec 7.4 - every row but the PHY group, which the engine answers
// ---------------------------------------------------------------------------

bool GateLinkApp::config_set(lran::node::Context& c, const lran::schema::ConfigEntry& in,
                             lran::schema::ConfigAckEntry* out) {
  if (cfg_ == nullptr) return false;
  *out = cfg_->store().apply(in, nullptr, nullptr);
  note_card();
  if (out->status == lran::ParamStatus::Ok || out->status == lran::ParamStatus::Clamped) {
    apply_param(c, in.param_id);
  }
  return out->status != lran::ParamStatus::UnknownParam;
}

bool GateLinkApp::config_get(const lran::node::Context&, uint16_t id,
                             lran::schema::ConfigAckEntry* out) {
  if (cfg_ == nullptr) return false;
  note_card();
  *out = cfg_->store().get(id);
  return out->status != lran::ParamStatus::UnknownParam;
}

void GateLinkApp::config_list(const lran::node::Context&, lran::node::ConfigSink* sink) {
  if (cfg_ == nullptr) return;
  note_card();
  const lran::config::Table& t = cfg_->table();
  for (size_t i = 0; i < t.size(); ++i) {
    const lran::config::ParamDef* d = t.at(i);
    // The engine lists the PHY group from its own copy; listing it here would answer it
    // twice.
    if (d == nullptr || d->access == lran::config::Access::Phy) continue;
    sink->add(cfg_->store().get(d->id));
  }
}

void GateLinkApp::config_restore_defaults(lran::node::Context& c) {
  if (cfg_ == nullptr) return;
  (void)cfg_->store().restore_defaults();
  note_card();
  apply_all(c);
}

bool GateLinkApp::config_unpersisted(const lran::node::Context&) const {
  return cfg_ != nullptr && cfg_->unpersisted();
}

void GateLinkApp::apply_all(lran::node::Context& c) {
  if (cfg_ == nullptr) return;
  const lran::config::Table& t = cfg_->table();
  for (size_t i = 0; i < t.size(); ++i) {
    const lran::config::ParamDef* d = t.at(i);
    if (d != nullptr && d->access != lran::config::Access::Phy) apply_param(c, d->id);
  }
}

void GateLinkApp::apply_param(lran::node::Context& c, uint16_t id) {
  const int32_t v = cfg_->store().effective(id);
  switch (id) {
    case kParamDedupCacheDepth:
      c.gate.set_cache_depth(static_cast<uint8_t>(v));
      break;
    case kParamFragReassemblyTimeoutMs:
      c.reassembler.set_timeout_ms(static_cast<uint32_t>(v));
      break;
    case kParamHexTimeoutMs:
      hex_timeout_ms_ = static_cast<uint32_t>(v);
      break;
    default:
      break;
  }
  port_->param_changed(id, v);
}

}  // namespace gatelink
