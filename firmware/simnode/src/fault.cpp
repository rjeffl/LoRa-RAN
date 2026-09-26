// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-8; see fault.h.

#include "fault.h"

#include <cstring>
#include <initializer_list>

#include "lran/codec.h"
#include "lran/messages.h"
#include "lran/schema/node_health_v1.h"
#include "lran/sim/frame_patch.h"
#include "lran/wire.h"

namespace simnode {
namespace {

using lran::sim::FramePatch;
using lran::sim::HdrByte;
using lran::sim::Seal;

// The staging buffer holds a 255-byte frame; lran-sim's patch buffer must match it, since a
// frame is built there and copied here. If kOutFrameMax and kPhyMaxFrame ever diverge, a
// grown `oversize` frame would be truncated on the way into the outbox.
static_assert(kOutFrameMax == lran::sim::kPhyMaxFrame,
              "kOutFrameMax must equal lran-sim's kPhyMaxFrame (Impl Plan 10.5)");

constexpr auto kRxRunt          = &lran::Counters::rx_runt;
constexpr auto kRxOversize      = &lran::Counters::rx_oversize;
constexpr auto kRxBadCrc        = &lran::Counters::rx_bad_crc;
constexpr auto kRxBadVer        = &lran::Counters::rx_bad_ver;
constexpr auto kRxNotAddressed  = &lran::Counters::rx_not_addressed;
constexpr auto kRxHdrExt        = &lran::Counters::rx_unknown_hdr_ext;
constexpr auto kRxBadFrag       = &lran::Counters::rx_bad_frag;
constexpr auto kRxUnknownType   = &lran::Counters::rx_unknown_type;
constexpr auto kRxUnknownSchema = &lran::Counters::rx_unknown_schema;
constexpr auto kRxBadLength     = &lran::Counters::rx_bad_length;
constexpr auto kRxNotFrag       = &lran::Counters::rx_not_fragmentable;
constexpr auto kRxRejectedMac   = &lran::Counters::rx_rejected_mac;
constexpr auto kRxTimeout       = &lran::Counters::rx_reassembly_timeout;
constexpr auto kRxFragOverflow  = &lran::Counters::rx_fragment_overflow;
constexpr auto kRxAbandoned     = &lran::Counters::rx_reassembly_abandoned;
constexpr auto kRxFragDup       = &lran::Counters::rx_frag_duplicate;
constexpr auto kRxFragLate      = &lran::Counters::rx_frag_late;

constexpr auto kRxRejectedSeq  = &lran::Counters::rx_rejected_seq;
constexpr auto kRxDupCommand   = &lran::Counters::rx_dup_command;

using C = uint32_t lran::Counters::*;
constexpr C kNone = nullptr;

}  // namespace

// Impl Plan 10.5, 10.5.1. The order is the specification's, and it is the order `fault help`
// prints. `frames` is the largest injection the row queues, for the outbox-room check.
const FaultInfo kFaultCatalogue[] = {
    {"runt", FaultId::Runt, 1, kRxRunt, "discarded, no ERROR", nullptr, 0},
    {"oversize", FaultId::Oversize, 1, kRxOversize, "stage 2a, no ERROR", nullptr, 0},
    {"bad_crc", FaultId::BadCrc, 1, kRxBadCrc, "stage 3", nullptr, 0},
    {"bad_ver", FaultId::BadVer, 2, kRxBadVer, "N-1 accepted, N-2 rejected", nullptr, 0},
    {"wrong_dst", FaultId::WrongDst, 1, kRxNotAddressed, "discarded, no ERROR", nullptr, 0},
    {"crit_ext", FaultId::CritExt, 1, kRxHdrExt, "ERROR(UNKNOWN_HDR_EXT)", nullptr, 0},
    {"hdr_rsv", FaultId::HdrRsv, 1, kNone, "ACCEPTED and ignored (spec 4.3)", nullptr, 0},
    {"frag_zero", FaultId::FragZero, 1, kRxBadFrag, "stage 5b, ERROR(BAD_LENGTH)", nullptr, 0},
    {"unknown_type", FaultId::UnknownType, 1, kRxUnknownType, "ERROR(UNKNOWN_TYPE)", nullptr, 0},
    {"unknown_schema", FaultId::UnknownSchema, 1, kRxUnknownSchema, "ERROR(UNKNOWN_SCHEMA)",
     nullptr, 0},
    {"bad_length", FaultId::BadLength, 2, kRxBadLength, "one short, one long", nullptr, 0},
    {"frag_command", FaultId::FragCommand, 1, kRxNotFrag, "stage 8a, ERROR(BAD_LENGTH)", nullptr,
     0},
    {"bad_mac", FaultId::BadMac, 1, kRxRejectedMac, "stage 9, not buffered", nullptr, 0},

    {"frag_timeout", FaultId::FragTimeout, 1, kRxTimeout, "expires on the tick", nullptr,
     kFragTimeoutGapMs},
    {"frag_overflow", FaultId::FragOverflow, 1, kRxFragOverflow, "index >= total", nullptr, 0},
    {"frag_oversize", FaultId::FragOversize, 15, kRxFragOverflow, "set exceeds the cap", nullptr,
     0},
    {"frag_dup", FaultId::FragDup, 4, kRxFragDup, "set completes; rx_dropped still", nullptr, 0},
    {"frag_late", FaultId::FragLate, 4, kRxFragLate, "set completes; rx_dropped still", nullptr,
     0},
    {"single_frame_interleave", FaultId::SingleFrameInterleave, 4, kNone,
     "set completes; rx_reassembly_abandoned still", nullptr, 0},
    {"set_displaced", FaultId::SetDisplaced, 4, kRxAbandoned, "live set displaced, second set completes", nullptr, 0},

    {"ctx_jump", FaultId::CtxJump, 1, kNone,
     "bridge adopts; its next COMMAND gets REJECTED_CTX, one retry", nullptr, 0},
    {"seq_jump", FaultId::SeqJump, 1, kNone, "accepted, RFC 1982", nullptr, 0},
    {"seq_wrap", FaultId::SeqWrap, 2, kNone, "accepted through 0xFFFF", nullptr, 0},

    {"flood", FaultId::Flood, 1, kNone, "bridge stays responsive", nullptr, 0},
    {"silent", FaultId::Silent, 0, kNone, "answers withheld, availability offline", nullptr, 0},

    // BF-6. The two ack faults act on the identity's own fresh ACKs; event_replay sends.
    {"ack_suppress", FaultId::AckSuppress, 0, kNone,
     "no ACK; bridge retries same seq, gets DUPLICATE_CACHED", nullptr, 0},
    {"ack_dup", FaultId::AckDup, 0, kNone, "two ACKs, second ignored", nullptr, 0},
    // BF-21. The only way to reach spec 10.3 step 3 deliberately: arm 2 and the bridge
    // resyncs once, is rejected again, and stops rather than looping.
    {"ctx_reject", FaultId::CtxReject, 0, kNone,
     "COMMAND_ACK(REJECTED_CTX) regardless of ctx; arm 2 to fault the bridge's resync", nullptr,
     0},
    {"event_replay", FaultId::EventReplay, 2, kNone, "same event_id twice, published once", nullptr,
     0},
    // 10.5.1 - the counter is the TARGET node's, not the bridge's.
    {"cmd_replay", FaultId::CmdReplay, 2, kRxDupCommand, "cached ACK, relay does not pulse",
     nullptr, 0},
    {"cmd_stale_seq", FaultId::CmdStaleSeq, 2, kRxRejectedSeq, "COMMAND_ACK(REJECTED_SEQ)",
     nullptr, 0},

    {"bad_phy_crc", FaultId::BadPhyCrc, 0, kNone, "cannot be injected - hardware PHY CRC",
     "the B1 range walk", 0},
};
const size_t kFaultCatalogueLen = sizeof(kFaultCatalogue) / sizeof(kFaultCatalogue[0]);

const FaultInfo* find_fault(const char* name) {
  for (const FaultInfo& f : kFaultCatalogue) {
    if (std::strcmp(name, f.name) == 0) return &f;
  }
  return nullptr;
}

const char* counter_name(uint32_t lran::Counters::* field) {
  if (field == nullptr) return "-";
  for (const lran::CounterField& f : lran::kCounterRegistry) {
    if (f.field == field) return f.name;
  }
  return "?";  // a catalogue counter absent from spec 14.1 - a defect this makes visible
}

const char* fault_result_name(FaultResult r) {
  switch (r) {
    case FaultResult::Ok:            return "ok";
    case FaultResult::NoIdentity:    return "no such identity";
    case FaultResult::Disabled:      return "identity disabled";
    case FaultResult::UnknownFault:  return "unknown fault";
    case FaultResult::WaitsForTask:  return "not implemented";
    case FaultResult::NotInjectable: return "cannot be injected";
    case FaultResult::BadCount:      return "count must be 1 or more";
    case FaultResult::WrongRole:     return "needs ROLE_GATELINK";
    case FaultResult::NeedsCtx:      return "a target on another board needs ctx <hex32>";
    case FaultResult::BadTarget:     return "target must be a simnode, f0-f3";
  }
  return "?";
}

FaultInjector::FaultInjector(IdentityTable* ids, Outbox* out, Node* node, lran::IMac* mac,
                             Sink* log)
    : ids_(ids), out_(out), node_(node), mac_(mac), log_(log) {}

const ArmedFault* FaultInjector::armed(lran::NodeId id) const {
  for (const ArmedFault& a : armed_) {
    if (a.active && a.id == id) return &a;
  }
  return nullptr;
}

FaultResult FaultInjector::arm(lran::NodeId id, const char* name, const FaultRequest& req,
                               uint32_t now_ms) {
  Identity* e = ids_->find(id);
  if (e == nullptr) return FaultResult::NoIdentity;
  if (!e->enabled) return FaultResult::Disabled;
  const FaultInfo* info = find_fault(name);
  if (info == nullptr) return FaultResult::UnknownFault;
  if (info->id == FaultId::BadPhyCrc) return FaultResult::NotInjectable;
  if (info->waits_for != nullptr) return FaultResult::WaitsForTask;
  if (req.count == 0) return FaultResult::BadCount;

  switch (info->id) {
    case FaultId::CtxReject: {
      if (e->role != Role::GateLink) return FaultResult::WrongRole;
      e->gl.ctx_reject_left = req.count;
      sink_printf(log_, "OK fault %02x ctx_reject: next %u COMMAND(s) answered REJECTED_CTX", id,
                  static_cast<unsigned>(req.count));
      return FaultResult::Ok;
    }
    case FaultId::AckSuppress:
    case FaultId::AckDup: {
      if (e->role != Role::GateLink) return FaultResult::WrongRole;
      const bool suppress = info->id == FaultId::AckSuppress;
      (suppress ? e->gl.ack_suppress_left : e->gl.ack_dup_left) = req.count;
      sink_printf(log_, "OK fault %02x %s: next %u ACK(s) %s", id, info->name,
                  static_cast<unsigned>(req.count), suppress ? "withheld" : "sent twice");
      return FaultResult::Ok;
    }
    case FaultId::EventReplay:
      if (e->role != Role::GateLink) return FaultResult::WrongRole;
      break;
    case FaultId::CmdReplay:
    case FaultId::CmdStaleSeq: {
      const FaultResult t = check_command_target(*e, req);
      if (t != FaultResult::Ok) return t;
      break;
    }
    default:
      break;
  }

  // `silent` withholds answers rather than sending frames; it lives on the identity so the
  // receive path can see it without reaching into the injector.
  if (info->id == FaultId::Silent) {
    disarm(id);
    e->silent_left = req.count;
    sink_printf(log_, "OK fault %02x silent: next %u answer(s) withheld", id,
                static_cast<unsigned>(req.count));
    return FaultResult::Ok;
  }

  // One armed fault per identity. Reuse its own slot, or any free one.
  ArmedFault* slot = nullptr;
  for (ArmedFault& a : armed_) {
    if (a.active && a.id == id) {
      slot = &a;
      break;
    }
  }
  if (slot == nullptr) {
    for (ArmedFault& a : armed_) {
      if (!a.active) {
        slot = &a;
        break;
      }
    }
  }
  // Every identity has a slot, so this cannot fail; the guard keeps a future change honest.
  if (slot == nullptr) return FaultResult::UnknownFault;

  *slot        = ArmedFault{};
  slot->active = true;
  slot->id     = id;
  slot->info   = info;
  slot->req    = req;
  slot->left   = req.count;
  slot->last_ms = 0;

  sink_printf(log_, "OK fault %02x %s armed x%u -> %02x (counter %s: %s)", id, info->name,
              static_cast<unsigned>(req.count), req.dst, counter_name(info->counter),
              info->expect);

  // Fire the first injection now, so a one-shot fault needs no tick.
  fire(*slot, *e, now_ms);
  return FaultResult::Ok;
}

bool FaultInjector::disarm(lran::NodeId id) {
  bool any = false;
  for (ArmedFault& a : armed_) {
    if (a.active && a.id == id) {
      a.active = false;
      any      = true;
    }
  }
  Identity* e = ids_->find(id);
  if (e != nullptr && (e->silent_left != 0 || e->gl.ack_suppress_left != 0 ||
                       e->gl.ack_dup_left != 0 || e->gl.ctx_reject_left != 0)) {
    e->silent_left          = 0;
    e->gl.ack_suppress_left = 0;
    e->gl.ack_dup_left      = 0;
    e->gl.ctx_reject_left   = 0;
    any                     = true;
  }
  return any;
}

FaultResult FaultInjector::check_command_target(const Identity& e, const FaultRequest& r) const {
  const Identity* local = r.dst == lran::kNodeBridge ? &e : ids_->find(r.dst);
  if (local != nullptr) {
    if (local->role != Role::GateLink) return FaultResult::WrongRole;
    if (!local->enabled) return FaultResult::Disabled;
    return FaultResult::Ok;
  }
  if (!is_simnode_id(r.dst)) return FaultResult::BadTarget;  // signs for no production node
  if (!r.has_ctx) return FaultResult::NeedsCtx;
  return FaultResult::Ok;
}

void FaultInjector::tick(uint32_t now_ms) {
  for (ArmedFault& a : armed_) {
    if (!a.active) continue;
    Identity* e = ids_->find(a.id);
    if (e == nullptr || !e->used) {  // the identity was removed or replaced under it
      a.active = false;
      continue;
    }
    const uint32_t gap = a.req.has_gap ? a.req.gap_ms : a.info->gap_ms;
    if (a.fired != 0 && now_ms - a.last_ms < gap) continue;
    fire(a, *e, now_ms);
  }
}

bool FaultInjector::fire(ArmedFault& a, Identity& e, uint32_t now_ms) {
  staged_   = 0;
  loopback_ = false;
  if (!build(a, e, now_ms)) {
    // A build failure is a defect in this file, not a bench event; disarm rather than spin.
    sink_printf(log_, "fault %02x %s: build failed - disarmed", e.id, a.info->name);
    a.active = false;
    return false;
  }
  // Wait for the radio to drain. A loopback injection sends nothing itself, but its target
  // answers each frame with a COMMAND_ACK, which needs the same room.
  if (out_->free_slots() < staged_) return false;

  if (loopback_) {
    // Delivered in order, synchronously: the first copy has executed before the second
    // arrives, as 10.5.1 requires - unless `ack delay` holds it in flight, which then tests
    // spec 9.4's in-flight answer instead.
    for (size_t i = 0; i < staged_; ++i) {
      node_->on_rx(stage_[i], stage_len_[i], lran::kI16NotAvailable, lran::kI16NotAvailable,
                   now_ms);
    }
  } else {
    for (size_t i = 0; i < staged_; ++i) out_->push(stage_[i], stage_len_[i]);
  }
  ++a.fired;
  a.last_ms = now_ms;
  ++injections_;
  if (--a.left == 0) {
    a.active = false;
    sink_printf(log_, "fault %02x %s: %u injection(s) done, disarmed", e.id, a.info->name,
                static_cast<unsigned>(a.fired));
  }
  return true;
}

// ---------------------------------------------------------------------------
// Frame builders. Each appends whole frames to stage_; the caller queues them together.
// ---------------------------------------------------------------------------

bool FaultInjector::push_stage(const uint8_t* frame, size_t len) {
  if (frame == nullptr || len == 0 || staged_ >= kOutboxDepth) return false;
  std::memcpy(stage_[staged_], frame, len);
  stage_len_[staged_] = len;
  ++staged_;
  return true;
}

lran::Header FaultInjector::status_header(const Identity& e, const FaultRequest& r) const {
  lran::Header h;
  h.ver    = e.proto_ver;
  h.type   = lran::MsgType::Status;
  h.src    = e.id;
  h.dst    = r.dst;
  h.seq    = 0;  // set per frame by the builder
  h.ctx_id = e.ctx_id;
  h.schema = lran::kSchemaNodeHealthV1;
  return h;
}

bool FaultInjector::add_command(Identity& e, const FaultRequest& r, bool frag, bool bad_mac) {
  lran::Header h;
  h.ver    = e.proto_ver;
  h.type   = lran::MsgType::Command;
  h.src    = e.id;
  h.dst    = r.dst;
  h.seq    = e.tx_seq++;
  h.ctx_id = r.has_ctx ? r.ctx : e.ctx_id;

  // spec 9.1 - a fault aimed at another simnode is signed with THAT node's key, so the
  // receiver's own key verifies it up to the deliberate corruption. Aimed at the bridge,
  // the identity's own key is right: the bridge holds every node's key.
  const uint8_t* key = e.key;
  uint8_t        other[lran::kNodeKeyLen];
  if (is_simnode_id(r.dst) && ids_->derive_simnode_key(r.dst, other)) key = other;

  lran::EncodeCtx ectx;
  ectx.mac      = mac_;
  ectx.node_key = key;

  const lran::msg::Command cmd{static_cast<uint8_t>(lran::Cmd::Nop), 0, 0};
  uint8_t                  payload[lran::msg::kCommandLen];
  size_t                   plen = 0;
  if (lran::msg::serialize(cmd, payload, sizeof(payload), &plen) != lran::Status::Ok) return false;

  uint8_t    buf[kOutFrameMax];
  FramePatch fp(buf, sizeof(buf));
  if (fp.encode(h, payload, plen, ectx) != lran::Status::Ok) return false;
  if (frag && fp.set_frag(0, 2) != lran::sim::PatchStatus::Ok) return false;
  // Seal the MAC over the patched header first (frag lives in the header), then corrupt it
  // for bad_mac. bad_mac reseals CRC only, so the flip stands; frag_command reseals both so
  // stage 8a - not stage 9 - is what refuses it.
  if (bad_mac) {
    if (fp.seal(Seal::MacAndCrc) != lran::sim::PatchStatus::Ok) return false;
    if (fp.flip_mac(lran::kMacLen - 1, 0xFF) != lran::sim::PatchStatus::Ok) return false;
    if (fp.seal(Seal::Crc) != lran::sim::PatchStatus::Ok) return false;
  } else {
    if (fp.seal(Seal::MacAndCrc) != lran::sim::PatchStatus::Ok) return false;
  }
  return push_stage(fp.frame(), fp.len());
}

bool FaultInjector::add_bridge_command(const Identity* local, lran::NodeId target,
                                       const FaultRequest& r, lran::Cmd cmd, lran::Seq seq) {
  lran::Header h;
  h.ver    = local != nullptr ? local->proto_ver : lran::kProtoVer;
  h.type   = lran::MsgType::Command;
  h.src    = lran::kNodeBridge;
  h.dst    = target;
  h.seq    = seq;
  h.ctx_id = local != nullptr ? local->ctx_id : r.ctx;

  uint8_t key[lran::kNodeKeyLen];
  if (!ids_->derive_simnode_key(target, key)) return false;
  lran::EncodeCtx ectx;
  ectx.mac      = mac_;
  ectx.node_key = key;

  const lran::msg::Command c{static_cast<uint8_t>(cmd), 0, 0};
  uint8_t                  payload[lran::msg::kCommandLen];
  size_t                   plen = 0;
  if (lran::msg::serialize(c, payload, sizeof(payload), &plen) != lran::Status::Ok) return false;

  uint8_t buf[lran::kMaxFrame];
  size_t  len = 0;
  if (lran::encode(h, payload, plen, ectx, buf, sizeof(buf), &len) != lran::Status::Ok) return false;
  return push_stage(buf, len);
}

// Fragments of `payload` at kFaultChunk, queued in `order`. A null order sends 0..total-1.
bool FaultInjector::add_fragments(Identity& e, const FaultRequest& r, lran::Seq seq,
                                  const uint8_t* payload, size_t len, const uint8_t* order,
                                  size_t order_len) {
  lran::Header h = status_header(e, r);
  h.seq          = seq;

  lran::EncodeCtx ectx;  // 0xF0 STATUS carries no MAC, so no key is needed
  const uint8_t   total = lran::fragment_count(len, kFaultChunk);
  if (total == 0) return false;

  const size_t n = order == nullptr ? total : order_len;
  for (size_t i = 0; i < n; ++i) {
    const uint8_t idx = order == nullptr ? static_cast<uint8_t>(i) : order[i];
    uint8_t       buf[kOutFrameMax];
    FramePatch    fp(buf, sizeof(buf));
    if (fp.encode_fragment(h, payload, len, idx, kFaultChunk, ectx) != lran::Status::Ok) {
      return false;
    }
    if (!push_stage(fp.frame(), fp.len())) return false;
  }
  return true;
}

bool FaultInjector::build(const ArmedFault& a, Identity& e, uint32_t now_ms) {
  const FaultRequest& r = a.req;

  // A correct 0xF0 payload, the base every single-frame fault deviates from.
  uint8_t      health[lran::schema::kNodeHealthV1Len];
  const size_t hlen = build_health_payload(e, *node_->radio_counters(), node_->boot_count(),
                                           now_ms, health, sizeof(health));
  if (hlen == 0) return false;

  // Shorthand: encode one health STATUS, run `body`, push it. `body` patches and reseals.
  auto one = [&](auto&& body) -> bool {
    lran::Header h = status_header(e, r);
    h.seq          = e.tx_seq++;
    lran::EncodeCtx ectx;
    uint8_t         buf[kOutFrameMax];
    FramePatch      fp(buf, sizeof(buf));
    if (fp.encode(h, health, hlen, ectx) != lran::Status::Ok) return false;
    if (!body(fp)) return false;
    return push_stage(fp.frame(), fp.len());
  };
  using PS = lran::sim::PatchStatus;

  switch (a.info->id) {
    case FaultId::Runt:
      return one([](FramePatch& fp) {
        return fp.truncate_body(15) == PS::Ok && fp.seal(Seal::Crc) == PS::Ok;  // stage 2
      });

    case FaultId::Oversize:
      // 255 bytes, the SX1262's ceiling; the receiver counts it at stage 2a (spec 14).
      return one([](FramePatch& fp) {
        return fp.resize_payload(lran::sim::kPhyMaxFrame - lran::kHdrLen - lran::kCrcLen, 0xA5) ==
                   PS::Ok &&
               fp.seal(Seal::Crc) == PS::Ok;
      });

    case FaultId::BadCrc:
      return one([](FramePatch& fp) { return fp.flip_crc(1, 0xFF) == PS::Ok; });

    case FaultId::BadVer: {
      // N-1 is accepted by the bridge, N-2 rejected with a distinct reason (V-B10).
      const uint8_t v = e.proto_ver;
      const bool ok1  = one([v](FramePatch& fp) {
        return fp.set_header(HdrByte::Ver, static_cast<uint8_t>(v - 1)) == PS::Ok &&
               fp.seal(Seal::Crc) == PS::Ok;
      });
      const bool ok2 = one([v](FramePatch& fp) {
        return fp.set_header(HdrByte::Ver, static_cast<uint8_t>(v - 2)) == PS::Ok &&
               fp.seal(Seal::Crc) == PS::Ok;
      });
      return ok1 && ok2;
    }

    case FaultId::WrongDst:
      return one([](FramePatch& fp) {
        return fp.set_header(HdrByte::Dst, lran::kNodeWellLink) == PS::Ok &&
               fp.seal(Seal::Crc) == PS::Ok;
      });

    case FaultId::CritExt:
      return one([](FramePatch& fp) {
        return fp.set_header(HdrByte::HdrFlags, lran::kHdrFlagCriticalExt) == PS::Ok &&
               fp.seal(Seal::Crc) == PS::Ok;
      });

    case FaultId::HdrRsv:
      // Accepted and ignored (spec 4.3). A discard here is the bug this proves is absent.
      return one([](FramePatch& fp) {
        return fp.set_header(HdrByte::Reserved1, 0x5A) == PS::Ok && fp.seal(Seal::Crc) == PS::Ok;
      });

    case FaultId::FragZero:
      return one([](FramePatch& fp) {
        return fp.set_frag(0, 0) == PS::Ok && fp.seal(Seal::Crc) == PS::Ok;  // stage 5b
      });

    case FaultId::UnknownType:
      return one([](FramePatch& fp) {
        return fp.set_header(HdrByte::Type, 0x0C) == PS::Ok && fp.seal(Seal::Crc) == PS::Ok;
      });

    case FaultId::UnknownSchema:
      return one([](FramePatch& fp) {
        return fp.set_header(HdrByte::Schema, 0x7F) == PS::Ok && fp.seal(Seal::Crc) == PS::Ok;
      });

    case FaultId::BadLength: {
      // Schema 0xF0 is fixed at its length; one byte short and one byte long both fail stage 8.
      const bool ok1 = one([hlen](FramePatch& fp) {
        return fp.resize_payload(hlen - 1, 0) == PS::Ok && fp.seal(Seal::Crc) == PS::Ok;
      });
      const bool ok2 = one([hlen](FramePatch& fp) {
        return fp.resize_payload(hlen + 1, 0) == PS::Ok && fp.seal(Seal::Crc) == PS::Ok;
      });
      return ok1 && ok2;
    }

    case FaultId::FragCommand:
      return add_command(e, r, /*frag=*/true, /*bad_mac=*/false);

    case FaultId::BadMac:
      return add_command(e, r, /*frag=*/false, /*bad_mac=*/true);

    case FaultId::FragTimeout: {
      // Fragment 0 of a 3-fragment set, then silence; the receiver's tick expires it.
      const uint8_t order[] = {0};
      return add_fragments(e, r, e.tx_seq++, health, hlen, order, 1);
    }

    case FaultId::FragOverflow:
      // frag 0x53: index 5 of a declared total of 3 - the W4 vector's off-by-more. decode_header
      // catches index >= total at stage 10 with ERROR(FRAGMENT_OVERFLOW).
      return one([](FramePatch& fp) {
        return fp.set_header(HdrByte::Frag, 0x53) == PS::Ok && fp.seal(Seal::Crc) == PS::Ok;
      });

    case FaultId::FragOversize: {
      // A reassembled set past the cap: 15 fragments of 14 bytes = 210 > 196 (spec 3.1).
      uint8_t big[210];
      for (size_t i = 0; i < sizeof(big); ++i) big[i] = static_cast<uint8_t>(0x40 + i);
      lran::Header h = status_header(e, r);
      h.seq          = e.tx_seq++;
      lran::EncodeCtx ectx;
      const uint8_t   total = lran::fragment_count(sizeof(big), 14);
      for (uint8_t i = 0; i < total; ++i) {
        uint8_t    buf[kOutFrameMax];
        FramePatch fp(buf, sizeof(buf));
        if (fp.encode_fragment(h, big, sizeof(big), i, 14, ectx) != lran::Status::Ok) return false;
        if (!push_stage(fp.frame(), fp.len())) return false;
      }
      return true;
    }

    case FaultId::FragDup: {
      // frag0, frag0 again, frag1, frag2: the set still completes, rx_dropped does not move.
      const uint8_t order[] = {0, 0, 1, 2};
      return add_fragments(e, r, e.tx_seq++, health, hlen, order, 4);
    }

    case FaultId::FragLate: {
      // A complete set, then a repeat of one of its fragments.
      const uint8_t order[] = {0, 1, 2, 1};
      return add_fragments(e, r, e.tx_seq++, health, hlen, order, 4);
    }

    case FaultId::SingleFrameInterleave: {
      // frag0, a single-frame STATUS sharing (src, ctx, schema), frag1, frag2. The set
      // completes and rx_reassembly_abandoned does not move (spec 11.2).
      const lran::Seq set_seq = e.tx_seq++;
      const uint8_t   before[] = {0};
      if (!add_fragments(e, r, set_seq, health, hlen, before, 1)) return false;
      if (!one([](FramePatch& fp) { return fp.seal(Seal::Crc) == PS::Ok; })) return false;
      const uint8_t after[] = {1, 2};
      return add_fragments(e, r, set_seq, health, hlen, after, 2);
    }

    case FaultId::SetDisplaced: {
      // frag0 of one set, then a COMPLETE second set from the same peer (spec 11.3).
      //
      // THE DISPLACING SET IS COMPLETED, decided by BF-21. Until then it was a lone frag0,
      // which displaced the first set and then expired on the tick itself - so the row moved
      // rx_reassembly_abandoned AND rx_reassembly_timeout, measured twice on 2026-09-16. That
      // made it the one row in 10.5 to break the table's own invariant, which the host suite
      // states as "the counter its row names - and only that counter - moves". The timeout
      // path is frag_timeout's to test, and a row that moves two counters cannot tell a
      // displacement defect from a timeout defect.
      const uint8_t only0[]  = {0};
      const uint8_t whole[]  = {0, 1, 2};
      return add_fragments(e, r, e.tx_seq++, health, hlen, only0, 1) &&
             add_fragments(e, r, e.tx_seq++, health, hlen, whole, 3);
    }

    case FaultId::CtxJump: {
      // The node's half: a status from a fresh, different context, with no reboot. The
      // bridge adopts it and resets cmd_seq; the REJECTED_CTX half is a COMMAND path (BF-6).
      lran::Header h = status_header(e, r);
      h.ctx_id       = e.ctx_id ^ 0x5A5A5A5Au;  // guaranteed different, non-zero
      h.seq          = e.tx_seq++;
      lran::EncodeCtx ectx;
      uint8_t         buf[kOutFrameMax];
      FramePatch      fp(buf, sizeof(buf));
      if (fp.encode(h, health, hlen, ectx) != lran::Status::Ok) return false;
      return push_stage(fp.frame(), fp.len());
    }

    case FaultId::SeqJump: {
      // A large forward status-seq step. Accepted (RFC 1982); status seq is advisory (10.2).
      lran::Header h = status_header(e, r);
      h.seq          = static_cast<lran::Seq>(e.tx_seq + 30000u);
      e.tx_seq       = static_cast<lran::Seq>(h.seq + 1);
      lran::EncodeCtx ectx;
      uint8_t         buf[kOutFrameMax];
      FramePatch      fp(buf, sizeof(buf));
      if (fp.encode(h, health, hlen, ectx) != lran::Status::Ok) return false;
      return push_stage(fp.frame(), fp.len());
    }

    case FaultId::SeqWrap: {
      // seq 0xFFFF then 0x0000: the failure guarded against is a plain `>` locking out every
      // frame until reboot (spec 10.2).
      lran::EncodeCtx ectx;
      for (lran::Seq s : {static_cast<lran::Seq>(0xFFFF), static_cast<lran::Seq>(0x0000)}) {
        lran::Header h = status_header(e, r);
        h.seq          = s;
        uint8_t    buf[kOutFrameMax];
        FramePatch fp(buf, sizeof(buf));
        if (fp.encode(h, health, hlen, ectx) != lran::Status::Ok) return false;
        if (!push_stage(fp.frame(), fp.len())) return false;
      }
      e.tx_seq = 1;
      return true;
    }

    case FaultId::Flood:
      // One correct frame per injection, armed with gap 0 and a high count: the receiver must
      // stay responsive and lora_task must not block (Impl Plan 1.3).
      return one([](FramePatch& fp) { return fp.seal(Seal::Crc) == PS::Ok; });

    case FaultId::EventReplay: {
      // One new event, sent twice under two status seqs. spec 7.3's key is (src, ctx_id,
      // event_id), so the bridge must publish once even though the frames differ.
      const lran::schema::GateLinkEventV1 ev = make_event(e, lran::EventType::VehicleDetected, now_ms);
      e.gl.last_event     = ev;
      e.gl.has_last_event = true;
      uint8_t payload[lran::schema::kGateLinkEventV1Len];
      size_t  plen = 0;
      if (lran::schema::serialize(ev, payload, sizeof(payload), &plen) != lran::Status::Ok) return false;
      lran::EncodeCtx ectx;
      for (int i = 0; i < 2; ++i) {
        lran::Header h;
        h.ver    = e.proto_ver;
        h.type   = lran::MsgType::Event;
        h.src    = e.id;
        h.dst    = r.dst;
        h.seq    = e.tx_seq++;
        h.ctx_id = e.ctx_id;
        h.schema = lran::kSchemaGateLinkEventV1;
        uint8_t buf[lran::kMaxFrame];
        size_t  len = 0;
        if (lran::encode(h, payload, plen, ectx, buf, sizeof(buf), &len) != lran::Status::Ok) {
          return false;
        }
        if (!push_stage(buf, len)) return false;
      }
      return true;
    }

    case FaultId::CmdReplay:
    case FaultId::CmdStaleSeq: {
      const Identity*    local  = r.dst == lran::kNodeBridge ? &e : ids_->find(r.dst);
      const lran::NodeId target = local != nullptr ? local->id : r.dst;
      loopback_                 = local != nullptr;

      lran::Seq base = 0;
      if (local != nullptr) {
        base = static_cast<lran::Seq>(local->gate.high_water() + 1);
      } else {
        base        = (r.has_seq && a.fired == 0) ? r.seq : remote_seq_;
        remote_seq_ = static_cast<lran::Seq>(base + 2);
      }

      if (a.info->id == FaultId::CmdReplay) {
        // The same COMMAND twice. OPEN, not NOP: the target counts actuations, and a replay
        // that moved that count would be a second relay pulse at a real gate.
        return add_bridge_command(local, target, r, lran::Cmd::Open, base) &&
               add_bridge_command(local, target, r, lran::Cmd::Open, base);
      }
      // base+1 executes; base is then below the high-water mark and was never executed, so it
      // is not in the dedup cache - step 5 refuses it rather than step 4 answering it.
      return add_bridge_command(local, target, r, lran::Cmd::Nop, static_cast<lran::Seq>(base + 1)) &&
             add_bridge_command(local, target, r, lran::Cmd::Nop, base);
    }

    // Behaviour faults reach build() only if arm() let them through, which it does not.
    // Present so the switch is exhaustive and a new FaultId is a compile error.
    case FaultId::Silent:
    case FaultId::AckSuppress:
    case FaultId::AckDup:
    case FaultId::CtxReject:
    case FaultId::BadPhyCrc:
      return false;
  }
  return false;
}

}  // namespace simnode
