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

void config(Board& b, Seq seq, const schema::NodeConfigV1& cfg) {
  uint8_t p[kMaxSchemaPayload];
  size_t  n = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::serialize(cfg, p, sizeof(p), &n)));
  bridge_send(b, MsgType::Config, seq, p, n, kSchemaNodeConfigV1);
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
  // spec 7.4, D53 - a read reports the current overrides, and a fresh node holds none.
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::Persisted),
                          static_cast<uint8_t>(a.persist_status));
  TEST_ASSERT_EQUAL_UINT8(0, a.count);
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
  config(b, 1, set);
  schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(2, a.count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::AppliedNotPersisted),
                          static_cast<uint8_t>(a.persist_status));  // RAM: honest (spec 7.4)
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::Ok), static_cast<uint8_t>(a.entries[0].status));
  TEST_ASSERT_EQUAL_UINT32(500, schema::entry_raw(a.entries[0].value, a.entries[0].len));

  auto get = cfg_of(ConfigOp::Get);
  add_entry(&get, 0x0101, PType::U16, 0);
  add_entry(&get, 0x0999, PType::U16, 0);
  config(b, 2, get);
  a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::Ok), static_cast<uint8_t>(a.entries[0].status));
  TEST_ASSERT_EQUAL_UINT32(500, schema::entry_raw(a.entries[0].value, a.entries[0].len));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::UnknownParam),
                          static_cast<uint8_t>(a.entries[1].status));

  // A held id set with another type keeps its value, and the ACK carries the value held.
  auto retype = cfg_of(ConfigOp::Set);
  add_entry(&retype, 0x0101, PType::U8, 9);
  config(b, 3, retype);
  a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::TypeMismatch),
                          static_cast<uint8_t>(a.entries[0].status));
  TEST_ASSERT_EQUAL_UINT32(500, schema::entry_raw(a.entries[0].value, a.entries[0].len));

  // D53 - a read while an override is held reports it unpersisted (the store is RAM).
  config(b, 4, cfg_of(ConfigOp::GetAll));
  a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(2, a.count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::AppliedNotPersisted),
                          static_cast<uint8_t>(a.persist_status));

  // D52 - RESTORE_DEFAULTS is answered with the full effective configuration: empty here.
  config(b, 5, cfg_of(ConfigOp::RestoreDefaults));
  a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ConfigOp::RestoreDefaults), static_cast<uint8_t>(a.op));
  TEST_ASSERT_EQUAL_UINT8(0, a.count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PersistStatus::Persisted),
                          static_cast<uint8_t>(a.persist_status));
  config(b, 6, cfg_of(ConfigOp::GetAll));
  TEST_ASSERT_EQUAL_UINT8(0, next_config_ack(b).count);
}

// spec 8.11, D53 - NOT_APPLIED means nothing took effect: a SET whose every entry was refused.
void test_a_set_with_every_entry_rejected_is_not_applied() {
  Board b;
  auto  set = cfg_of(ConfigOp::Set);
  add_entry(&set, 0x0101, PType::U16, 500);
  set.entries[0].len = 4;  // disagrees with its ptype: TYPE_MISMATCH (spec 7.4, D51)
  config(b, 1, set);
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
  config(b, 1, set);
  next_config_ack(b);
  config(b, 1, set);
  expect_ack(next_ack(b), 1, AckResult::DuplicateCached);
  TEST_ASSERT_EQUAL_UINT32(1, b.f1().gl.executions);
}

void test_the_config_store_is_bounded() {
  Board b;
  auto  set = cfg_of(ConfigOp::Set);
  for (uint16_t i = 0; i < kConfigStoreDepth + 1; ++i) add_entry(&set, 0x0200 + i, PType::U8, i);
  b.log.expect("RAM store full");
  config(b, 1, set);
  const schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(kConfigStoreDepth + 1, a.count);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::Ok),
                          static_cast<uint8_t>(a.entries[kConfigStoreDepth - 1].status));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ParamStatus::UnknownParam),
                          static_cast<uint8_t>(a.entries[kConfigStoreDepth].status));
  TEST_ASSERT_TRUE(b.log.seen());
}

// Twenty-four u32 entries fit one CONFIG (spec 7.4's figure), but their results do not fit one
// CONFIG_ACK, which is single-frame (spec 11.4). The ACK is cut and the cut is logged; how a
// node splits one is spec W10's open question, not settled here.
void test_a_config_ack_that_cannot_fit_is_cut_and_logged() {
  Board b;
  auto  set = cfg_of(ConfigOp::Set);
  for (uint16_t i = 0; i < 24; ++i) add_entry(&set, 0x0300 + i, PType::U32, 100000u + i);
  b.log.expect("did not fit");
  config(b, 1, set);
  const schema::NodeConfigAckV1 a = next_config_ack(b);
  TEST_ASSERT_EQUAL_UINT8(21, a.count);
  TEST_ASSERT_TRUE(b.log.seen());
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

  RUN_TEST(test_config_set_get_and_restore);
  RUN_TEST(test_a_set_with_every_entry_rejected_is_not_applied);
  RUN_TEST(test_a_repeated_config_is_answered_from_the_cache);
  RUN_TEST(test_the_config_store_is_bounded);
  RUN_TEST(test_a_config_ack_that_cannot_fit_is_cut_and_logged);

  RUN_TEST(test_events_new_again_follow_up_and_a_new_context);
  return UNITY_END();
}
