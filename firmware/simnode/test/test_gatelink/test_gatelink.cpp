// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-6 - ROLE_GATELINK. Impl Plan 10.2; spec 6.2-6.4, 7.2-7.4, 8.1-8.2, 9.4, 10.1-10.4.
//
// THE BRIDGE IN THESE TESTS is the library's encoder with f1's derived key, and the library's
// decoder with self = 0x00 - what the bridge's lora_link and BF-18 will run. What is under test
// is what the node does with a command: whether it executes, what it answers, and above all
// that a retry never executes twice (root rule 2, BS-3).
//
// WHAT THIS CANNOT COVER: the air, and a bridge that retries on its own. BF-18 builds the
// second; B3's bench run joins the two.

#include <unity.h>

#include <climits>
#include <cstring>

#include "gatelink.h"
#include "identity.h"
#include "lran/lran.h"
#include "node.h"
#include "refimpl_mac.h"
#include "test_key.h"

using namespace simnode;
using namespace lran;
using lran::config::PhyBlob;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefKdf g_kdf;
refimpl::RefMac g_mac;

uint32_t g_next_random = 0x7000;
uint32_t counting_random() { return ++g_next_random; }

class CaptureSink final : public Sink {
 public:
  void line(const char* text) override {
    if (std::strstr(text, want_) != nullptr) seen_ = true;
  }
  void expect(const char* s) {
    want_ = s;
    seen_ = false;
  }
  bool seen() const { return seen_; }

 private:
  const char* want_ = "\x01";
  bool        seen_ = false;
};

// One simnode board holding f1 ROLE_GATELINK.
struct Board {
  IdentityTable ids;
  Outbox        out;
  CaptureSink   log;
  Node          node{&ids, &out, &g_mac, &log};
  Board() {
    ids.init(lran_test::kTestMasterKey, &g_kdf, counting_random);
    ids.add(kNodeSim1, Role::GateLink);
  }
  Identity& f1() { return *ids.find(kNodeSim1); }
};

struct Send {
  NodeId   dst     = kNodeSim1;
  CtxId    ctx     = 0;  // 0 means the target's own
  bool     bad_key = false;
  uint32_t now_ms  = 1000;
};

// The bridge sends `type` to a simnode on board `b`.
void bridge_send(Board& b, MsgType type, Seq seq, const uint8_t* payload, size_t n, SchemaId schema,
                 const Send& o = Send{}) {
  const Identity* t = b.ids.find(o.dst);
  TEST_ASSERT_NOT_NULL(t);
  Header h;
  h.type   = type;
  h.src    = kNodeBridge;
  h.dst    = o.dst;
  h.seq    = seq;
  h.ctx_id = o.ctx != 0 ? o.ctx : t->ctx_id;
  h.schema = schema;

  uint8_t key[kNodeKeyLen];
  std::memcpy(key, t->key, sizeof(key));
  if (o.bad_key) key[0] ^= 0x01;
  EncodeCtx ectx;
  ectx.mac      = &g_mac;
  ectx.node_key = key;

  uint8_t buf[kMaxFrame];
  size_t  len = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(encode(h, payload, n, ectx, buf, sizeof(buf), &len)));
  b.node.on_rx(buf, len, -40, 70, o.now_ms);
}

void command(Board& b, Seq seq, Cmd cmd, uint8_t arg = 0, const Send& o = Send{}) {
  const msg::Command c{static_cast<uint8_t>(cmd), arg, 0};
  uint8_t            p[msg::kCommandLen];
  size_t             n = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(msg::serialize(c, p, sizeof(p), &n)));
  bridge_send(b, MsgType::Command, seq, p, n, kSchemaNone, o);
}

void poll(Board& b, uint8_t flags) {
  const uint8_t p[1] = {flags};
  bridge_send(b, MsgType::Poll, 9, p, 1, kSchemaNone);
}

void send_config(Board& b, Seq seq, const schema::NodeConfigV1& cfg, const Send& o = Send{}) {
  uint8_t p[kMaxSchemaPayload];
  size_t  n = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::serialize(cfg, p, sizeof(p), &n)));
  bridge_send(b, MsgType::Config, seq, p, n, kSchemaNodeConfigV1, o);
}

// One frame the bridge hears from the board, decoded as the bridge would.
struct Heard {
  Header  hdr;
  uint8_t payload[kMaxPayloadPlain] = {0};
  size_t  len                       = 0;
};

bool hear(Board& b, Heard* out) {
  OutFrame f;
  if (!b.out.pop(&f)) return false;
  DecodeCtx ctx;
  ctx.self = kNodeBridge;
  Frame fr;
  if (decode_header(f.bytes, f.len, ctx, &fr) != Status::Ok) return false;
  if (decode_payload(f.bytes, f.len, ctx, &fr) != Status::Ok) return false;
  out->hdr = fr.hdr;
  out->len = fr.payload_len;
  std::memcpy(out->payload, fr.payload, fr.payload_len);
  return true;
}

msg::CommandAck next_ack(Board& b, Header* hdr = nullptr) {
  Heard h;
  TEST_ASSERT_TRUE_MESSAGE(hear(b, &h), "no frame queued");
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::CommandAck), static_cast<uint8_t>(h.hdr.type));
  msg::CommandAck a;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(msg::deserialize(h.payload, h.len, &a)));
  if (hdr != nullptr) *hdr = h.hdr;
  return a;
}

schema::GateLinkStatusV1 next_status(Board& b, Header* hdr = nullptr) {
  Heard h;
  TEST_ASSERT_TRUE_MESSAGE(hear(b, &h), "no frame queued");
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::Status), static_cast<uint8_t>(h.hdr.type));
  TEST_ASSERT_EQUAL_UINT8(kSchemaSimnodeStatusV1, h.hdr.schema);  // never 0x10
  schema::GateLinkStatusV1 s;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::deserialize(h.payload, h.len, &s)));
  if (hdr != nullptr) *hdr = h.hdr;
  return s;
}

schema::NodeConfigAckV1 next_config_ack(Board& b) {
  Heard h;
  TEST_ASSERT_TRUE_MESSAGE(hear(b, &h), "no frame queued");
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::ConfigAck), static_cast<uint8_t>(h.hdr.type));
  schema::NodeConfigAckV1 a;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::deserialize(h.payload, h.len, &a)));
  return a;
}

void expect_ack(const msg::CommandAck& a, Seq seq, AckResult result) {
  TEST_ASSERT_EQUAL_UINT16(seq, a.ack_seq);
  TEST_ASSERT_EQUAL_STRING(ack_result_name(result), ack_result_name(static_cast<AckResult>(a.result)));
}

}  // namespace

// ---------------------------------------------------------------------------
// POLL and status
// ---------------------------------------------------------------------------

void test_a_poll_is_answered_with_schema_0xfe_and_poll_response() {
  Board b;
  poll(b, kPollFlagFullStatus);
  Header                         h;
  const schema::GateLinkStatusV1 s = next_status(b, &h);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::PollResponse), s.status_reason);
  TEST_ASSERT_EQUAL_UINT8(kNodeBridge, h.dst);
  TEST_ASSERT_EQUAL_UINT32(b.f1().ctx_id, h.ctx_id);
  TEST_ASSERT_EQUAL_UINT16(13300, s.batt_mv);
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, s.mppt_temp_c10);  // a sentinel, not 0 (root rule 6)
  TEST_ASSERT_EQUAL_UINT8(0x48, s.node_flags);          // BMS polling, age not persisted
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
}

void test_poll_config_readback_follows_the_status() {
  Board b;
  poll(b, kPollFlagFullStatus | kPollFlagConfigReadback);
  next_status(b);
  const schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConfigOp::GetAll), static_cast<uint8_t>(a.op));
  // spec 7.4, D53 - a read reports the current overrides, and a fresh node holds none. The
  // PHY group is always there (BF-33).
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::Persisted),
                          static_cast<uint8_t>(a.persist_status));
  TEST_ASSERT_EQUAL_UINT8(kPhyGroupSize, a.count);
}

void test_push_carries_the_given_reason_and_needs_the_role() {
  Board b;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EmitResult::Ok),
                        static_cast<int>(b.node.push(kNodeSim1, StatusReason::GateStateChange, 5000)));
  const schema::GateLinkStatusV1 s = next_status(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::GateStateChange), s.status_reason);
  TEST_ASSERT_EQUAL_UINT32(5, s.uptime_s);

  b.ids.add(kNodeSim2, Role::Health);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EmitResult::WrongRole),
                        static_cast<int>(b.node.push(kNodeSim2, StatusReason::Boot, 0)));
}

void test_field_overrides_reach_the_status() {
  Board b;
  GateLinkState& gl = b.f1().gl;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FieldResult::Ok),
                        static_cast<int>(field_set(&gl, "batt_mv", "12000")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FieldResult::Ok),
                        static_cast<int>(field_set(&gl, "load_ma", "na")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FieldResult::Ok),
                        static_cast<int>(field_set(&gl, "uptime_s", "77")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FieldResult::Ok),
                        static_cast<int>(field_set(&gl, "cell_temp_c2", "-5")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FieldResult::Ok),
                        static_cast<int>(field_set(&gl, "bms_soc", "na")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FieldResult::UnknownField),
                        static_cast<int>(field_set(&gl, "status_reason", "1")));  // push owns it
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FieldResult::NoSentinel),
                        static_cast<int>(field_set(&gl, "gate_state", "na")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FieldResult::BadValue),
                        static_cast<int>(field_set(&gl, "cell_temp_c0", "200")));

  b.node.push(kNodeSim1, StatusReason::DebugSynthetic, 999000);
  const schema::GateLinkStatusV1 s = next_status(b);
  TEST_ASSERT_EQUAL_UINT16(12000, s.batt_mv);
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, s.load_ma);
  TEST_ASSERT_EQUAL_UINT32(77, s.uptime_s);  // set, so not generated
  TEST_ASSERT_EQUAL_INT8(-5, s.cell_temp_c[2]);
  TEST_ASSERT_EQUAL_UINT8(kSocNotAvailable, s.bms_soc);
}

// ---------------------------------------------------------------------------
// COMMAND - spec 9.4 steps 2-6
// ---------------------------------------------------------------------------

void test_a_command_is_executed_once_and_acknowledged_from_its_own_context() {
  Board b;
  command(b, 1, Cmd::Open);
  Header h;
  expect_ack(next_ack(b, &h), 1, AckResult::Accepted);
  TEST_ASSERT_EQUAL_UINT32(b.f1().ctx_id, h.ctx_id);
  TEST_ASSERT_EQUAL_UINT8(kNodeBridge, h.dst);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().gl.actuations);
  TEST_ASSERT_EQUAL_UINT16(1, b.f1().gate.high_water());
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
}

// Root rule 2 and BS-3. The bridge's retry reuses the seq; the node answers from the cache.
void test_a_retry_gets_the_cached_ack_and_does_not_actuate_again() {
  Board b;
  command(b, 1, Cmd::Open);
  expect_ack(next_ack(b), 1, AckResult::Accepted);
  command(b, 1, Cmd::Open);
  const msg::CommandAck a = next_ack(b);
  expect_ack(a, 1, AckResult::DuplicateCached);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckResult::Accepted), a.detail);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().gl.actuations);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().counters.rx_dup_command);
}

void test_a_stale_seq_is_rejected() {
  Board b;
  command(b, 5, Cmd::Nop);
  next_ack(b);
  command(b, 3, Cmd::Open);
  expect_ack(next_ack(b), 3, AckResult::RejectedSeq);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().counters.rx_rejected_seq);
  TEST_ASSERT_EQUAL_UINT32(0, b.f1().gl.actuations);
}

// spec 10.3 step 1 - the ACK carries the node's OWN ctx_id, which the bridge adopts.
void test_a_wrong_context_is_refused_with_the_nodes_own_ctx() {
  Board     b;
  const CtxId own = b.f1().ctx_id;
  Send      o;
  o.ctx = own ^ 0x00FF00FFu;
  command(b, 1, Cmd::Open, 0, o);
  Header h;
  expect_ack(next_ack(b, &h), 1, AckResult::RejectedCtx);
  TEST_ASSERT_EQUAL_UINT32(own, h.ctx_id);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().counters.rx_rejected_ctx);
  TEST_ASSERT_EQUAL_UINT32(0, b.f1().gl.executions);
  TEST_ASSERT_EQUAL_UINT16(0, b.f1().gate.high_water());  // an unauthenticated seq moves nothing
}

void test_a_bad_mac_is_refused_and_not_executed() {
  Board b;
  Send  o;
  o.bad_key = true;
  command(b, 1, Cmd::Open, 0, o);
  expect_ack(next_ack(b), 1, AckResult::RejectedMac);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().counters.rx_rejected_mac);
  TEST_ASSERT_EQUAL_UINT32(0, b.f1().gl.executions);
}

// spec 9.4 (v0.11) - the retry that lands while the first copy executes gets NOTHING. Then the
// real result arrives, and the next retry gets it from the cache.
void test_a_retry_inside_the_execution_window_receives_nothing() {
  Board b;
  b.f1().gl.ack_delay_ms = 500;
  command(b, 1, Cmd::Open);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());

  Send retry;
  retry.now_ms = 1200;
  command(b, 1, Cmd::Open, 0, retry);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().counters.rx_dup_command);

  b.node.tick(1499);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
  b.node.tick(1500);
  expect_ack(next_ack(b), 1, AckResult::Accepted);

  retry.now_ms = 1600;
  command(b, 1, Cmd::Open, 0, retry);
  expect_ack(next_ack(b), 1, AckResult::DuplicateCached);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().gl.actuations);
}

void test_a_new_command_while_one_executes_is_actuator_busy() {
  Board b;
  b.f1().gl.ack_delay_ms = 500;
  command(b, 1, Cmd::Open);
  command(b, 2, Cmd::Close);
  expect_ack(next_ack(b), 2, AckResult::ActuatorBusy);
  b.node.tick(2000);
  expect_ack(next_ack(b), 1, AckResult::Accepted);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().gl.actuations);
  command(b, 2, Cmd::Close);  // its retry is answered with the busy result it was given
  const msg::CommandAck a = next_ack(b);
  expect_ack(a, 2, AckResult::DuplicateCached);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckResult::ActuatorBusy), a.detail);
}

// Impl Plan 10.5 ctx_reject (BF-21): the identity answers REJECTED_CTX whatever context the
// COMMAND carried, so spec 10.3 step 3 can be reached deliberately.
//
// BEFORE THE GATE, and that is the whole point. Spec 9.4 puts the context check at step 2
// and the dedup gate at steps 4-6: a node that refuses on context has not looked at the
// sequence space, so the seq is not consumed and no result is cached. If it were cached,
// the bridge's resync retry would meet a DUPLICATE_CACHED instead of a second rejection -
// and the second rejection is the path this fault exists to produce.
void test_ctx_reject_answers_rejected_ctx_without_consuming_the_seq() {
  Board b;
  b.f1().gl.ctx_reject_left = 2;

  command(b, 1, Cmd::Open);
  expect_ack(next_ack(b), 1, AckResult::RejectedCtx);
  TEST_ASSERT_EQUAL_UINT32(0, b.f1().gl.actuations);   // never executed
  TEST_ASSERT_EQUAL_UINT16(0, b.f1().gate.high_water());  // no seq consumed

  // The bridge's resync retry, which spec 10.3 restarts at seq 1. The second rejection is
  // what makes it stop rather than resync again.
  command(b, 1, Cmd::Open);
  expect_ack(next_ack(b), 1, AckResult::RejectedCtx);
  TEST_ASSERT_EQUAL_UINT32(2, b.f1().gl.ctx_rejects_forced);
  TEST_ASSERT_EQUAL_UINT16(0, b.f1().gl.ctx_reject_left);

  // Disarmed, so the next command runs normally - and the seq space was never touched.
  command(b, 1, Cmd::Open);
  expect_ack(next_ack(b), 1, AckResult::Accepted);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().gl.actuations);
}

// Impl Plan 10.5 ack_suppress: the bridge sees no ACK and retries with the same seq.
void test_ack_suppress_withholds_only_the_fresh_ack() {
  Board b;
  b.f1().gl.ack_suppress_left = 1;
  command(b, 1, Cmd::Open);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().gl.acks_suppressed);
  command(b, 1, Cmd::Open);
  expect_ack(next_ack(b), 1, AckResult::DuplicateCached);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().gl.actuations);
  TEST_ASSERT_EQUAL_UINT16(0, b.f1().gl.ack_suppress_left);
}

void test_ack_dup_sends_two_identical_acks_then_stops() {
  Board b;
  b.f1().gl.ack_dup_left = 1;
  command(b, 1, Cmd::Nop);
  expect_ack(next_ack(b), 1, AckResult::Accepted);
  expect_ack(next_ack(b), 1, AckResult::Accepted);
  command(b, 2, Cmd::Nop);
  expect_ack(next_ack(b), 2, AckResult::Accepted);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
}

void test_command_results_follow_spec_8() {
  Board b;
  command(b, 1, Cmd::Close, 2);
  expect_ack(next_ack(b), 1, AckResult::RejectedArg);
  command(b, 2, static_cast<Cmd>(0x55));
  expect_ack(next_ack(b), 2, AckResult::RejectedUnknownCmd);
  command(b, 3, Cmd::Reboot, 0);  // no 0xA5 guard
  expect_ack(next_ack(b), 3, AckResult::RejectedArg);
  command(b, 4, Cmd::SetRelayDryRun, 1);
  expect_ack(next_ack(b), 4, AckResult::Accepted);
  command(b, 5, Cmd::Open);
  expect_ack(next_ack(b), 5, AckResult::DryRun);

  b.node.push(kNodeSim1, StatusReason::DebugSynthetic, 1000);
  TEST_ASSERT_EQUAL_UINT8(0x4C, next_status(b).node_flags);  // bit 2 dry run joins bits 3 and 6
}

void test_request_status_sends_the_ack_then_the_status() {
  Board b;
  command(b, 1, Cmd::RequestStatus);
  expect_ack(next_ack(b), 1, AckResult::Accepted);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::PollResponse), next_status(b).status_reason);
}

// spec 10.1 - REBOOT is a new context. The ACK goes out under the old one, then BOOT under the new.
void test_reboot_acks_under_the_old_context_then_boots_a_new_one() {
  Board       b;
  const CtxId old = b.f1().ctx_id;
  b.node.event(kNodeSim1, EventType::Boot, EventMode::New, 0);
  OutFrame drop;
  while (b.out.pop(&drop)) {}

  command(b, 7, Cmd::Reboot, kRebootGuard);
  Header h;
  expect_ack(next_ack(b, &h), 7, AckResult::Accepted);
  TEST_ASSERT_EQUAL_UINT32(old, h.ctx_id);

  const schema::GateLinkStatusV1 s = next_status(b, &h);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::Boot), s.status_reason);
  TEST_ASSERT_NOT_EQUAL(old, h.ctx_id);
  TEST_ASSERT_EQUAL_UINT32(b.f1().ctx_id, h.ctx_id);
  TEST_ASSERT_EQUAL_UINT16(1, s.boot_count);
  TEST_ASSERT_EQUAL_UINT16(0, b.f1().gate.high_water());
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().gl.next_event_id);
}

void test_silent_withholds_a_command_before_the_gate() {
  Board b;
  b.f1().silent_left = 1;
  command(b, 1, Cmd::Open);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
  TEST_ASSERT_EQUAL_UINT16(0, b.f1().gate.high_water());
  command(b, 1, Cmd::Open);  // heard this time
  expect_ack(next_ack(b), 1, AckResult::Accepted);
}

void test_role_health_does_not_answer_a_command() {
  Board b;
  b.ids.add(kNodeSim2, Role::Health);
  Send o;
  o.dst = kNodeSim2;
  command(b, 1, Cmd::Open, 0, o);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
  TEST_ASSERT_EQUAL_UINT32(1, b.ids.find(kNodeSim2)->unhandled);
}

// ---------------------------------------------------------------------------
// ROLL_CONTEXT - spec 10.6, D58
// ---------------------------------------------------------------------------

// The failure D58 exists for: a restarted bridge's seq 1 meeting a cache that already
// holds seq 1. After the roll the same seq executes, in a context the bridge has adopted.
void test_a_roll_takes_a_new_context_and_acks_under_it() {
  Board b;
  for (Seq s = 1; s <= 5; ++s) {
    command(b, s, Cmd::Open);
    next_ack(b);
  }
  b.f1().gl.dry_run = true;
  const CtxId    old        = b.f1().ctx_id;
  const uint32_t dup_before = b.f1().counters.rx_dup_command;

  // seq 1 sits at or below the mark and is in the cache. The roll does not judge it.
  command(b, 1, Cmd::RollContext, kRollContextGuard);
  Header h;
  expect_ack(next_ack(b, &h), 1, AckResult::Accepted);
  TEST_ASSERT_NOT_EQUAL(old, b.f1().ctx_id);
  TEST_ASSERT_EQUAL_UINT32(b.f1().ctx_id, h.ctx_id);  // the NEW context
  TEST_ASSERT_EQUAL_UINT16(1, h.seq);                 // its first frame
  TEST_ASSERT_EQUAL_UINT16(0, b.f1().gate.high_water());
  TEST_ASSERT_EQUAL_UINT32(dup_before, b.f1().counters.rx_dup_command);

  // Nothing but the context moved (spec 10.6 node step 2).
  TEST_ASSERT_TRUE(b.f1().gl.dry_run);
  TEST_ASSERT_EQUAL_UINT32(5, b.f1().gl.actuations);

  command(b, 1, Cmd::Open);
  expect_ack(next_ack(b), 1, AckResult::DryRun);
  TEST_ASSERT_EQUAL_UINT32(6, b.f1().gl.actuations);
}

// spec 10.6 node step 1 - the running command keeps its context, and the bridge's retry of
// the roll, under the same seq, succeeds once that command has a result.
void test_a_roll_while_a_command_executes_is_actuator_busy() {
  Board b;
  b.f1().gl.ack_delay_ms = 500;
  const CtxId old        = b.f1().ctx_id;
  command(b, 1, Cmd::Open);
  command(b, 2, Cmd::RollContext, kRollContextGuard);
  Header h;
  expect_ack(next_ack(b, &h), 2, AckResult::ActuatorBusy);
  TEST_ASSERT_EQUAL_UINT32(old, b.f1().ctx_id);
  TEST_ASSERT_EQUAL_UINT32(old, h.ctx_id);

  b.node.tick(2000);
  expect_ack(next_ack(b), 1, AckResult::Accepted);
  command(b, 2, Cmd::RollContext, kRollContextGuard);
  expect_ack(next_ack(b, &h), 2, AckResult::Accepted);
  TEST_ASSERT_NOT_EQUAL(old, h.ctx_id);
}

void test_a_roll_with_the_wrong_guard_is_rejected_arg() {
  Board       b;
  const CtxId old = b.f1().ctx_id;
  command(b, 1, Cmd::RollContext, 0x5A);
  expect_ack(next_ack(b), 1, AckResult::RejectedArg);
  TEST_ASSERT_EQUAL_UINT32(old, b.f1().ctx_id);
}

// spec 10.6 bridge step 4 - the node rolled and its ACK was lost, so the bridge's retry
// carries the old ctx_id and draws REJECTED_CTX under the new one, which completes the roll.
// The same path bounds a replayed roll: once acted on, the request fails at step 2.
void test_a_retried_roll_after_a_lost_ack_is_rejected_ctx() {
  Board       b;
  const CtxId old             = b.f1().ctx_id;
  b.f1().gl.ack_suppress_left = 1;
  command(b, 1, Cmd::RollContext, kRollContextGuard);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
  const CtxId rolled = b.f1().ctx_id;
  TEST_ASSERT_NOT_EQUAL(old, rolled);

  Send o;
  o.ctx = old;
  command(b, 1, Cmd::RollContext, kRollContextGuard, o);
  Header h;
  expect_ack(next_ack(b, &h), 1, AckResult::RejectedCtx);
  TEST_ASSERT_EQUAL_UINT32(rolled, h.ctx_id);
  TEST_ASSERT_EQUAL_UINT32(rolled, b.f1().ctx_id);  // not rolled a second time
}

// Decided 2026-09-23 - every role answers a roll, so a bench identity the bridge hears
// completes its roll rather than drawing one on every frame. Other commands stay
// ROLE_GATELINK's alone.
void test_role_health_answers_a_roll_and_nothing_else() {
  Board b;
  b.ids.add(kNodeSim2, Role::Health);
  Identity&   f2  = *b.ids.find(kNodeSim2);
  const CtxId old = f2.ctx_id;
  Send        o;
  o.dst = kNodeSim2;
  command(b, 1, Cmd::RollContext, kRollContextGuard, o);
  Header h;
  expect_ack(next_ack(b, &h), 1, AckResult::Accepted);
  TEST_ASSERT_NOT_EQUAL(old, f2.ctx_id);
  TEST_ASSERT_EQUAL_UINT32(f2.ctx_id, h.ctx_id);
  TEST_ASSERT_EQUAL_UINT32(0, f2.unhandled);

  command(b, 2, Cmd::Open, 0, o);
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
  TEST_ASSERT_EQUAL_UINT32(1, f2.unhandled);
}

// ---------------------------------------------------------------------------
// CONFIG - spec 7.4, the generic RAM store
// ---------------------------------------------------------------------------

namespace {

schema::NodeConfigV1 cfg_of(ConfigOp op) {
  schema::NodeConfigV1 c;
  c.op = op;
  return c;
}

void add_entry(schema::NodeConfigV1* c, uint16_t id, PType t, uint32_t raw) {
  TEST_ASSERT_TRUE(schema::entry_pack(&c->entries[c->count++], id, t, raw));
}

}  // namespace

void test_config_set_get_and_restore() {
  Board b;
  auto  set = cfg_of(ConfigOp::Set);
  add_entry(&set, 0x0101, PType::U16, 500);
  add_entry(&set, 0x0102, PType::U8, 7);
  send_config(b, 1, set);
  schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(2, a.count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::AppliedNotPersisted),
                          static_cast<uint8_t>(a.persist_status));  // RAM: honest (spec 7.4)
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::Ok), static_cast<uint8_t>(a.entries[0].status));
  TEST_ASSERT_EQUAL_UINT32(500, schema::entry_raw(a.entries[0].value, a.entries[0].len));
  TEST_ASSERT_TRUE(a.entries[0].is_override);  // spec 7.4, D68 - across the wire

  auto get = cfg_of(ConfigOp::Get);
  add_entry(&get, 0x0101, PType::U16, 0);
  add_entry(&get, 0x0999, PType::U16, 0);
  send_config(b, 2, get);
  a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::Ok), static_cast<uint8_t>(a.entries[0].status));
  TEST_ASSERT_EQUAL_UINT32(500, schema::entry_raw(a.entries[0].value, a.entries[0].len));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::UnknownParam),
                          static_cast<uint8_t>(a.entries[1].status));
  TEST_ASSERT_TRUE(a.entries[0].is_override);
  TEST_ASSERT_FALSE(a.entries[1].is_override);

  // A held id set with another type keeps its value, and the ACK carries the value held.
  auto retype = cfg_of(ConfigOp::Set);
  add_entry(&retype, 0x0101, PType::U8, 9);
  send_config(b, 3, retype);
  a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::TypeMismatch),
                          static_cast<uint8_t>(a.entries[0].status));
  TEST_ASSERT_EQUAL_UINT32(500, schema::entry_raw(a.entries[0].value, a.entries[0].len));

  // D53 - a read while an override is held reports it unpersisted (the store is RAM). The
  // answer also carries the six PHY rows (BF-33).
  send_config(b, 4, cfg_of(ConfigOp::GetAll));
  a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(2 + kPhyGroupSize, a.count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::AppliedNotPersisted),
                          static_cast<uint8_t>(a.persist_status));

  // D52 - RESTORE_DEFAULTS is answered with the full effective configuration: the PHY
  // group alone here, which it keeps (D60).
  send_config(b, 5, cfg_of(ConfigOp::RestoreDefaults));
  a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConfigOp::RestoreDefaults), static_cast<uint8_t>(a.op));
  TEST_ASSERT_EQUAL_UINT8(kPhyGroupSize, a.count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::Persisted),
                          static_cast<uint8_t>(a.persist_status));
  send_config(b, 6, cfg_of(ConfigOp::GetAll));
  TEST_ASSERT_EQUAL_UINT8(kPhyGroupSize, next_config_ack(b).count);
}

// spec 8.11, D53 - NOT_APPLIED means nothing took effect: a SET whose every entry was refused.
void test_a_set_with_every_entry_rejected_is_not_applied() {
  Board b;
  auto  set = cfg_of(ConfigOp::Set);
  add_entry(&set, 0x0101, PType::U16, 500);
  set.entries[0].len = 4;  // disagrees with its ptype: TYPE_MISMATCH (spec 7.4, D51)
  send_config(b, 1, set);
  const schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(1, a.count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::TypeMismatch),
                          static_cast<uint8_t>(a.entries[0].status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::NotApplied),
                          static_cast<uint8_t>(a.persist_status));
}

// spec 9.4 applies steps 4-6 to every authenticated type, and words the step 4 answer as a
// COMMAND_ACK. A repeated CONFIG is therefore answered that way and not applied again.
void test_a_repeated_config_is_answered_from_the_cache() {
  Board b;
  auto  set = cfg_of(ConfigOp::Set);
  add_entry(&set, 0x0101, PType::U16, 500);
  send_config(b, 1, set);
  next_config_ack(b);
  send_config(b, 1, set);
  expect_ack(next_ack(b), 1, AckResult::DuplicateCached);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().gl.executions);
}

void test_the_config_store_is_bounded() {
  Board b;
  auto  set = cfg_of(ConfigOp::Set);
  for (uint16_t i = 0; i < kConfigStoreDepth + 1; ++i) add_entry(&set, 0x0200 + i, PType::U8, i);
  b.log.expect("RAM store full");
  send_config(b, 1, set);
  const schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(kConfigStoreDepth + 1, a.count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::Ok),
                          static_cast<uint8_t>(a.entries[kConfigStoreDepth - 1].status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::UnknownParam),
                          static_cast<uint8_t>(a.entries[kConfigStoreDepth].status));
  TEST_ASSERT_TRUE(b.log.seen());
}

// Twenty-four u32 entries fit one CONFIG (spec 7.4's figure), but their results do not fit one
// CONFIG_ACK, which is single-frame (spec 11.4). The ACK is cut and the cut is logged. D57
// closed W10 with spec 7.4.1's split, which the generic RAM store does not build.
void test_a_config_ack_that_cannot_fit_is_cut_and_logged() {
  Board b;
  auto  set = cfg_of(ConfigOp::Set);
  for (uint16_t i = 0; i < 24; ++i) add_entry(&set, 0x0300 + i, PType::U32, 100000u + i);
  b.log.expect("did not fit");
  send_config(b, 1, set);
  const schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(21, a.count);
  TEST_ASSERT_TRUE(b.log.seen());
}

// ---------------------------------------------------------------------------
// PHY - spec 12.4.2, BF-33 slice 3
// ---------------------------------------------------------------------------

namespace {

class RamBlob final : public BlobStore {
 public:
  bool   usable() const override { return usable_; }
  size_t read(uint8_t* out, size_t cap) const override {
    if (len_ == 0 || len_ > cap) return 0;
    std::memcpy(out, bytes_, len_);
    return len_;
  }
  bool write(const uint8_t* bytes, size_t len) override {
    if (!usable_ || len > sizeof(bytes_)) return false;
    std::memcpy(bytes_, bytes, len);
    len_ = len;
    ++writes;
    return true;
  }
  bool erase() override {
    len_ = 0;
    return true;
  }
  bool     usable_ = true;
  uint32_t writes  = 0;

 private:
  uint8_t bytes_[64] = {0};
  size_t  len_       = 0;
};

// A board with an NVS store. `blob` outlives a PhyTrial, so a reboot is a second PhyTrial
// over the same bytes.
struct PhyBoard : Board {
  RamBlob    blob;
  PhyPersist persist{&blob};
  PhyTrial   phy{&persist};
  PhyBoard() {
    phy.begin();
    node.set_phy(&phy);
  }
};

constexpr uint16_t kFreq = 0x0110;
constexpr int32_t  kNewFreq   = 917000000;
constexpr int32_t  kNewTrialS = 60;

// The whole group, as the bridge's build_phy_set() sends it.
schema::NodeConfigV1 phy_set(int32_t freq = kNewFreq, int32_t tx_dbm = -4) {
  auto c = cfg_of(ConfigOp::Set);
  add_entry(&c, 0x0110, PType::U32, static_cast<uint32_t>(freq));
  add_entry(&c, 0x0111, PType::U8, 9);
  add_entry(&c, 0x0112, PType::U16, 125);
  add_entry(&c, 0x0113, PType::U8, 5);
  add_entry(&c, 0x0114, PType::I16, static_cast<uint32_t>(tx_dbm) & 0xFFFFu);
  add_entry(&c, 0x0115, PType::U16, kNewTrialS);
  return c;
}

schema::NodeConfigV1 phy_get() {
  auto c = cfg_of(ConfigOp::Get);
  for (uint16_t id = 0x0110; id <= 0x0115; ++id) add_entry(&c, id, PType::U8, 0);
  for (uint8_t i = 0; i < c.count; ++i) c.entries[i].len = 0;  // spec 8.10 - a GET names ids
  return c;
}

uint8_t status_of(const schema::NodeConfigAckV1& a, size_t i) {
  return static_cast<uint8_t>(a.entries[i].status);
}

}  // namespace

// spec 12.4.2 step 2 - no store, no change: READ_ONLY with the value held, nothing retunes.
void test_phy_rows_are_read_only_without_a_store() {
  Board b;
  send_config(b, 1, phy_set());
  const schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(kPhyGroupSize, a.count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::ReadOnly), status_of(a, 0));
  TEST_ASSERT_EQUAL_UINT32(917400000, schema::entry_raw(a.entries[0].value, a.entries[0].len));
  TEST_ASSERT_FALSE(a.entries[0].is_override);  // D1's default, never set
  TEST_ASSERT_FALSE(b.node.phy()->retune_due());
}

// spec 7.4, D68 - a trial value is what the board runs, so its result is marked OVERRIDE,
// and a GET after the commit still reads it so.
void test_phy_results_carry_override() {
  PhyBoard b;
  send_config(b, 1, phy_set());
  schema::NodeConfigAckV1 a = next_config_ack(b);
  for (size_t i = 0; i < kPhyGroupSize; ++i) TEST_ASSERT_TRUE(a.entries[i].is_override);
  b.phy.on_retuned(2000);
  b.phy.on_authenticated();
  send_config(b, 2, phy_get());
  a = next_config_ack(b);
  TEST_ASSERT_TRUE(a.entries[0].is_override);
}

// spec 8.12, D64 - 300 kHz is inside the range and no SX1262 bandwidth. It answers
// INVALID_VALUE, carries 125, and the group does not count toward a retune.
void test_phy_an_invalid_bandwidth_does_not_retune() {
  PhyBoard b;
  auto set = phy_set();
  set.entries[2].value[0] = 300 & 0xFF;
  set.entries[2].value[1] = 300 >> 8;
  send_config(b, 1, set);
  const schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::InvalidValue), status_of(a, 2));
  TEST_ASSERT_EQUAL_UINT32(125, schema::entry_raw(a.entries[2].value, a.entries[2].len));
  TEST_ASSERT_FALSE(b.phy.retune_due());
}

// Steps 3 to 5: accept on the old settings, retune, ignore POLL, commit on an authenticated
// frame - and the blob then holds the group with the marker clear.
void test_phy_change_is_accepted_retuned_and_confirmed() {
  PhyBoard b;
  send_config(b, 1, phy_set());
  schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(kPhyGroupSize, a.count);
  for (size_t i = 0; i < kPhyGroupSize; ++i) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::Ok), status_of(a, i));
  }
  TEST_ASSERT_EQUAL_UINT32(kNewFreq, schema::entry_raw(a.entries[0].value, a.entries[0].len));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::AppliedNotPersisted),
                          static_cast<uint8_t>(a.persist_status));  // D60
  TEST_ASSERT_TRUE(b.phy.retune_due());
  TEST_ASSERT_EQUAL_INT32(kNewFreq, b.phy.group().v[0]);
  TEST_ASSERT_EQUAL_INT32(917400000, b.phy.committed().v[0]);

  b.phy.on_retuned(2000);
  TEST_ASSERT_EQUAL_STRING("trial", phy_state_name(b.phy.state()));
  TEST_ASSERT_EQUAL_UINT32(kNewTrialS * 1000u, b.phy.window_left_ms(2000));
  PhyBlob blob;
  TEST_ASSERT_TRUE(b.persist.read(&blob));
  TEST_ASSERT_TRUE(blob.trial_open);

  poll(b, 0);  // step 4 - POLL is answered and confirms nothing
  next_status(b);
  TEST_ASSERT_EQUAL_STRING("trial", phy_state_name(b.phy.state()));

  send_config(b, 2, phy_get());  // step 5 - the bridge's confirming GET
  a = next_config_ack(b);
  TEST_ASSERT_EQUAL_STRING("idle", phy_state_name(b.phy.state()));
  TEST_ASSERT_EQUAL_UINT32(kNewFreq, schema::entry_raw(a.entries[0].value, a.entries[0].len));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::Persisted),
                          static_cast<uint8_t>(a.persist_status));
  TEST_ASSERT_TRUE(b.persist.read(&blob));
  TEST_ASSERT_FALSE(blob.trial_open);
  TEST_ASSERT_EQUAL_size_t(kPhyGroupSize, blob.n);
  TEST_ASSERT_EQUAL_INT32(kNewFreq, blob.values[0]);
  TEST_ASSERT_EQUAL_INT32(kNewFreq, b.phy.committed().v[0]);
  TEST_ASSERT_FALSE(b.phy.retune_due());
}

// Steps 6 and 8 - silence reverts, and the next frame from the bridge draws PHY_REVERTED
// with detail 0x0001.
void test_phy_window_expiry_reverts_and_reports() {
  PhyBoard b;
  send_config(b, 1, phy_set());
  next_config_ack(b);
  b.phy.on_retuned(2000);
  b.node.tick(2000 + kNewTrialS * 1000u - 1);
  TEST_ASSERT_EQUAL_STRING("trial", phy_state_name(b.phy.state()));
  b.log.expect("REVERTED");
  b.node.tick(2000 + kNewTrialS * 1000u);
  TEST_ASSERT_TRUE(b.log.seen());
  TEST_ASSERT_EQUAL_STRING("idle", phy_state_name(b.phy.state()));
  TEST_ASSERT_TRUE(b.phy.retune_due());
  TEST_ASSERT_EQUAL_INT32(917400000, b.phy.group().v[0]);
  b.phy.on_retuned(2000 + kNewTrialS * 1000u);
  TEST_ASSERT_EQUAL_STRING("idle", phy_state_name(b.phy.state()));  // a revert opens no window
  PhyBlob blob;
  TEST_ASSERT_TRUE(b.persist.read(&blob));
  TEST_ASSERT_FALSE(blob.trial_open);

  poll(b, 0);
  Heard h;
  TEST_ASSERT_TRUE(hear(b, &h));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::Event), static_cast<uint8_t>(h.hdr.type));
  schema::GateLinkEventV1 ev;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::deserialize(h.payload, h.len, &ev)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventType::PhyReverted), ev.event_type);
  TEST_ASSERT_EQUAL_UINT16(0x0001, ev.detail);
  // spec 8.7, D69 - the poll's own answer follows, carrying CONFIG_CHANGE: the revert
  // changed the effective configuration and no CONFIG_ACK reported it.
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::ConfigChange),
                          next_status(b).status_reason);
  poll(b, 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::PollResponse),
                          next_status(b).status_reason);  // both reported once
  TEST_ASSERT_EQUAL_size_t(0, b.out.size());
}

// Step 7 - a reboot in the trial comes back on the committed group and reports 0x0002.
void test_phy_reboot_during_trial_comes_back_committed() {
  PhyBoard b;
  send_config(b, 1, phy_set());
  next_config_ack(b);
  b.phy.on_retuned(2000);

  PhyTrial rebooted(&b.persist);
  TEST_ASSERT_EQUAL_UINT16(0x0002, static_cast<uint16_t>(rebooted.begin()));
  TEST_ASSERT_EQUAL_INT32(917400000, rebooted.group().v[0]);
  PhyTrial again(&b.persist);
  TEST_ASSERT_EQUAL_UINT16(0x0000, static_cast<uint16_t>(again.begin()));  // marker cleared
}

// A committed group survives a reboot.
void test_phy_committed_group_survives_a_reboot() {
  PhyBoard b;
  send_config(b, 1, phy_set());
  next_config_ack(b);
  b.phy.on_retuned(2000);
  b.phy.on_authenticated();
  PhyTrial rebooted(&b.persist);
  TEST_ASSERT_EQUAL_UINT16(0x0000, static_cast<uint16_t>(rebooted.begin()));
  TEST_ASSERT_EQUAL_INT32(kNewFreq, rebooted.group().v[0]);
  TEST_ASSERT_EQUAL_INT32(kNewTrialS, rebooted.group().v[5]);
}

// Decided 2026-09-24 - one radio, so the board retunes once every member accepted. ROLE_HEALTH
// is a member and answers the PHY group; a ROLE_FAULT identity is not a member.
void test_phy_board_retunes_only_when_every_member_accepted() {
  PhyBoard b;
  b.ids.add(kNodeSim2, Role::Health);
  b.ids.add(kNodeSim3, Role::Fault);
  send_config(b, 1, phy_set());
  next_config_ack(b);
  TEST_ASSERT_FALSE(b.phy.retune_due());
  TEST_ASSERT_EQUAL_STRING("pending", phy_state_name(b.phy.state()));

  Send o;
  o.dst = kNodeSim2;
  send_config(b, 1, phy_set(), o);
  const schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::Ok), status_of(a, 0));
  TEST_ASSERT_TRUE(b.phy.retune_due());
}

// ROLE_HEALTH answers the PHY group and nothing else.
void test_role_health_answers_phy_rows_only() {
  PhyBoard b;
  b.ids.add(kNodeSim2, Role::Health);
  Send o;
  o.dst   = kNodeSim2;
  auto set = cfg_of(ConfigOp::Set);
  add_entry(&set, 0x0101, PType::U16, 500);
  send_config(b, 1, set, o);
  schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::UnknownParam), status_of(a, 0));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::NotApplied),
                          static_cast<uint8_t>(a.persist_status));
  send_config(b, 2, cfg_of(ConfigOp::GetAll), o);
  a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(kPhyGroupSize, a.count);
  TEST_ASSERT_EQUAL_UINT16(kFreq, a.entries[0].param_id);
}

// spec 12.4.1 step 4 - a clamp abandons the change at the bridge, so it does not count here.
void test_phy_a_clamped_set_does_not_retune() {
  PhyBoard b;
  send_config(b, 1, phy_set(kNewFreq, -2));  // above D33's -4 dBm ceiling
  const schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::Clamped), status_of(a, 4));
  TEST_ASSERT_FALSE(b.phy.retune_due());
}

// A second group during a trial is refused, and the trial stands.
void test_phy_a_set_during_the_trial_is_read_only() {
  PhyBoard b;
  send_config(b, 1, phy_set());
  next_config_ack(b);
  b.phy.on_retuned(2000);
  b.phy.on_authenticated();  // commit, then open a second trial
  send_config(b, 2, phy_set(916000000));
  next_config_ack(b);
  b.phy.on_retuned(3000);
  auto again = phy_set(915000000);
  PhyTrial& t = b.phy;
  const schema::ConfigAckEntry r = t.set(again.entries[0], nullptr);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::ReadOnly), static_cast<uint8_t>(r.status));
  TEST_ASSERT_EQUAL_INT32(916000000, t.group().v[0]);
}

// spec 12.4.1 - the roll after a bridge restart confirms a node in its trial.
void test_phy_a_roll_confirms_the_trial() {
  PhyBoard b;
  send_config(b, 1, phy_set());
  next_config_ack(b);
  b.phy.on_retuned(2000);
  command(b, 2, Cmd::RollContext, kRollContextGuard);
  TEST_ASSERT_EQUAL_STRING("idle", phy_state_name(b.phy.state()));
  TEST_ASSERT_EQUAL_INT32(kNewFreq, b.phy.committed().v[0]);
}

// A group some members never accepted is dropped after phy_trial_s, with nothing to report:
// the radio never moved.
void test_phy_a_pending_group_is_abandoned() {
  PhyBoard b;
  b.ids.add(kNodeSim2, Role::Health);
  send_config(b, 1, phy_set());
  next_config_ack(b);
  b.node.tick(1000 + kNewTrialS * 1000u);
  TEST_ASSERT_EQUAL_STRING("idle", phy_state_name(b.phy.state()));
  TEST_ASSERT_FALSE(b.phy.retune_due());
  TEST_ASSERT_EQUAL_UINT32(1, b.phy.stats().abandoned);
  TEST_ASSERT_EQUAL_INT32(917400000, b.phy.group().v[0]);
  TEST_ASSERT_EQUAL_UINT16(0, b.f1().gl.phy_revert_detail);
}

// `phy reset` - back to D1's group, blob erased, a retune owed.
void test_phy_reset_restores_the_defaults() {
  PhyBoard b;
  send_config(b, 1, phy_set());
  next_config_ack(b);
  b.phy.on_retuned(2000);
  b.phy.on_authenticated();
  TEST_ASSERT_TRUE(b.phy.reset_to_defaults());
  TEST_ASSERT_EQUAL_INT32(917400000, b.phy.group().v[0]);
  TEST_ASSERT_TRUE(b.phy.retune_due());
  PhyBlob blob;
  TEST_ASSERT_FALSE(b.persist.read(&blob));
}

// The group maps onto the radio's units, and the bridge's defaults are the boot PHY.
void test_phy_group_maps_to_the_radio_config() {
  PhyTrial              t(nullptr);
  lran::link::PhyConfig p{};
  TEST_ASSERT_TRUE(phy_config_from(t.group(), lran::link::kPhy, &p));
  TEST_ASSERT_EQUAL_UINT32(lran::link::kPhy.freq_hz, p.freq_hz);
  TEST_ASSERT_EQUAL_UINT16(lran::link::kPhy.bw_khz10, p.bw_khz10);
  TEST_ASSERT_EQUAL_UINT8(lran::link::kPhy.sf, p.sf);
  TEST_ASSERT_EQUAL_UINT8(lran::link::kPhy.cr_denom, p.cr_denom);
  TEST_ASSERT_EQUAL_INT8(lran::link::kPhy.conducted_dbm, p.conducted_dbm);
}

// ---------------------------------------------------------------------------
// EVENT - spec 7.3
// ---------------------------------------------------------------------------

void test_events_new_again_follow_up_and_a_new_context() {
  Board    b;
  uint32_t id = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EmitResult::NothingToRepeat),
                        static_cast<int>(b.node.event(kNodeSim1, EventType::Boot, EventMode::Again, 0)));

  auto next_event = [&b]() {
    Heard h;
    TEST_ASSERT_TRUE(hear(b, &h));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::Event), static_cast<uint8_t>(h.hdr.type));
    schema::GateLinkEventV1 ev;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(schema::deserialize(h.payload, h.len, &ev)));
    return ev;
  };

  b.node.event(kNodeSim1, EventType::VehicleDetected, EventMode::New, 0, &id);
  schema::GateLinkEventV1 ev = next_event();
  TEST_ASSERT_EQUAL_UINT32(1, ev.event_id);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Direction::Undetermined), ev.direction);

  b.node.event(kNodeSim1, EventType::VehicleDetected, EventMode::Again, 0);
  TEST_ASSERT_EQUAL_UINT32(1, next_event().event_id);

  b.node.event(kNodeSim1, EventType::VehicleDetected, EventMode::FollowUp, 0);
  ev = next_event();
  TEST_ASSERT_EQUAL_UINT32(1, ev.event_id);
  TEST_ASSERT_EQUAL_UINT8(schema::kEventFlagFollowUp, ev.event_flags);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Direction::Entry), ev.direction);

  b.node.event(kNodeSim1, EventType::GateStateChange, EventMode::New, 0);
  TEST_ASSERT_EQUAL_UINT32(2, next_event().event_id);

  b.ids.new_context(kNodeSim1);  // never reused within a ctx_id; restarts in a new one
  b.node.event(kNodeSim1, EventType::Boot, EventMode::New, 0);
  TEST_ASSERT_EQUAL_UINT32(1, next_event().event_id);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_poll_is_answered_with_schema_0xfe_and_poll_response);
  RUN_TEST(test_poll_config_readback_follows_the_status);
  RUN_TEST(test_push_carries_the_given_reason_and_needs_the_role);
  RUN_TEST(test_field_overrides_reach_the_status);

  RUN_TEST(test_a_command_is_executed_once_and_acknowledged_from_its_own_context);
  RUN_TEST(test_a_retry_gets_the_cached_ack_and_does_not_actuate_again);
  RUN_TEST(test_a_stale_seq_is_rejected);
  RUN_TEST(test_a_wrong_context_is_refused_with_the_nodes_own_ctx);
  RUN_TEST(test_a_bad_mac_is_refused_and_not_executed);
  RUN_TEST(test_a_retry_inside_the_execution_window_receives_nothing);
  RUN_TEST(test_a_new_command_while_one_executes_is_actuator_busy);
  RUN_TEST(test_ctx_reject_answers_rejected_ctx_without_consuming_the_seq);
  RUN_TEST(test_ack_suppress_withholds_only_the_fresh_ack);
  RUN_TEST(test_ack_dup_sends_two_identical_acks_then_stops);
  RUN_TEST(test_command_results_follow_spec_8);
  RUN_TEST(test_request_status_sends_the_ack_then_the_status);
  RUN_TEST(test_reboot_acks_under_the_old_context_then_boots_a_new_one);
  RUN_TEST(test_silent_withholds_a_command_before_the_gate);
  RUN_TEST(test_role_health_does_not_answer_a_command);
  RUN_TEST(test_a_roll_takes_a_new_context_and_acks_under_it);
  RUN_TEST(test_a_roll_while_a_command_executes_is_actuator_busy);
  RUN_TEST(test_a_roll_with_the_wrong_guard_is_rejected_arg);
  RUN_TEST(test_a_retried_roll_after_a_lost_ack_is_rejected_ctx);
  RUN_TEST(test_role_health_answers_a_roll_and_nothing_else);

  RUN_TEST(test_phy_rows_are_read_only_without_a_store);
RUN_TEST(test_phy_change_is_accepted_retuned_and_confirmed);
RUN_TEST(test_phy_window_expiry_reverts_and_reports);
RUN_TEST(test_phy_reboot_during_trial_comes_back_committed);
RUN_TEST(test_phy_committed_group_survives_a_reboot);
RUN_TEST(test_phy_board_retunes_only_when_every_member_accepted);
RUN_TEST(test_role_health_answers_phy_rows_only);
RUN_TEST(test_phy_a_clamped_set_does_not_retune);
RUN_TEST(test_phy_a_set_during_the_trial_is_read_only);
RUN_TEST(test_phy_a_roll_confirms_the_trial);
RUN_TEST(test_phy_a_pending_group_is_abandoned);
RUN_TEST(test_phy_reset_restores_the_defaults);
RUN_TEST(test_phy_group_maps_to_the_radio_config);
RUN_TEST(test_phy_results_carry_override);
RUN_TEST(test_phy_an_invalid_bandwidth_does_not_retune);
RUN_TEST(test_config_set_get_and_restore);
  RUN_TEST(test_a_set_with_every_entry_rejected_is_not_applied);
  RUN_TEST(test_a_repeated_config_is_answered_from_the_cache);
  RUN_TEST(test_the_config_store_is_bounded);
  RUN_TEST(test_a_config_ack_that_cannot_fit_is_cut_and_logged);

  RUN_TEST(test_events_new_again_follow_up_and_a_new_context);
  return UNITY_END();
}
