// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink task L1 - the engine against a minimal application, one context, on the host.
//
// The simnode's suites remain the regression evidence for the extraction: they exercise
// this engine through four identities and the whole fault catalogue. This suite holds the
// rules a single-identity node relies on, and the one the simnode never needed: a command
// that stays in flight until the application finishes it (Impl Plan 5.2).
//
// The "bridge" here is the library's codec with self = 0x00, which is what the bridge's
// receive ladder calls.

#include <unity.h>

#include <cstring>

#include "lran/lran.h"
#include "lran/node/engine.h"
#include "lran/node/names.h"
#include "refimpl_mac.h"
#include "test_key.h"

using namespace lran;
using namespace lran::node;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefKdf g_kdf;
refimpl::RefMac g_mac;

uint32_t g_next_random = 0x7000;
uint32_t counting_random() { return ++g_next_random; }

constexpr NodeId kSelf = kNodeSim1;

class NullSink final : public Sink {
 public:
  void line(const char*) override { ++lines; }
  int  lines = 0;
};

// Real schema 0xFE and 0x11 bodies, so the codec accepts them; the tests read back the
// reason, the event type and the detail.
class TestApp final : public Application {
 public:
  uint8_t  capabilities(const Context&) const override {
    return kAnswersCommands | kAnswersConfig | kAnswersHex | kRefusesAuthenticated;
  }
  RandomFn random() const override { return counting_random; }

  bool on_poll(Engine& eng, Context& c, const Header& hdr, const uint8_t*, size_t,
               uint32_t now_ms) override {
    eng.send_status(c, *this, hdr.src, StatusReason::PollResponse, now_ms);
    return true;
  }

  CommandOutcome execute(Context&, const msg::Command& cmd, uint32_t) override {
    ++executed;
    if (cmd.cmd == static_cast<uint8_t>(Cmd::Reboot)) {
      return {AckResult::Accepted, 0, AfterAck::Reboot, false};
    }
    if (cmd.cmd == static_cast<uint8_t>(Cmd::RequestConfig)) {
      return {AckResult::Accepted, 0, AfterAck::ConfigReadback, false};
    }
    return {AckResult::Accepted, 0, AfterAck::None, defer};
  }

  size_t build_status(const Context&, StatusReason r, uint32_t, uint8_t* out, size_t cap,
                      uint8_t* schema) override {
    schema::GateLinkStatusV1 st;
    st.status_reason = static_cast<uint8_t>(r);
    size_t n         = 0;
    *schema          = kSchemaSimnodeStatusV1;
    return schema::serialize(st, out, cap, &n) == Status::Ok ? n : 0;
  }
  size_t build_event(Context&, EventType t, uint16_t detail, uint32_t, uint8_t* out, size_t cap,
                     uint8_t* schema) override {
    schema::GateLinkEventV1 ev;
    ev.event_type = static_cast<uint8_t>(t);
    ev.detail     = detail;
    ev.event_id   = ++events;
    size_t n      = 0;
    *schema       = kSchemaGateLinkEventV1;
    return schema::serialize(ev, out, cap, &n) == Status::Ok ? n : 0;
  }

  HexReply hex_forward(Context&, const char*, size_t, char* rsp, size_t, size_t* n,
                       uint32_t) override {
    if (hex_silent) return HexReply::Pending;
    std::memcpy(rsp, ":5A0", 4);
    *n = 4;
    return HexReply::Answered;
  }
  uint32_t hex_timeout_ms(const Context&) const override { return 500; }

  // `listed` uint32 rows from 0x1000 up, listed in DESCENDING param_id, so a test sees the
  // engine put a readback in order (spec 7.4.1).
  void config_list(const Context&, ConfigSink* sink) override {
    for (size_t i = listed; i > 0; --i) {
      schema::ConfigAckEntry a;
      a.param_id = static_cast<uint16_t>(0x1000 + i - 1);
      a.ptype    = PType::U32;
      a.len      = 4;
      a.value[0] = static_cast<uint8_t>(i);
      sink->add(a);
    }
  }
  size_t listed = 0;

  // One writable row, 0x1000, held in RAM; `unsaved` is the store's verdict on it.
  bool config_set(Context&, const schema::ConfigEntry& in, schema::ConfigAckEntry* out) override {
    if (in.param_id != 0x1000) return false;
    schema::entry_pack(out, in.param_id, ParamStatus::Ok, in.ptype,
                       schema::entry_raw(in.value, in.len));
    return true;
  }
  bool config_unpersisted(const Context&) const override { return unsaved; }
  bool unsaved = false;

  bool defer      = false;
  bool hex_silent = false;
  int  executed   = 0;
  uint32_t events = 0;
};

struct Rig {
  Outbox   out;
  NullSink log;
  Engine   eng{&out, &g_mac, &log};
  TestApp  app;
  Context  c;
  Rig() {
    c.id = kSelf;
    g_kdf.derive_node_key(lran_test::kTestMasterKey, kSelf, c.key);
    reset_context(c, counting_random);
  }

  void rx(const Header& h, const uint8_t* payload, size_t n, uint32_t now = 1000) {
    EncodeCtx ectx;
    ectx.mac      = &g_mac;
    ectx.node_key = c.key;
    uint8_t buf[kMaxFrame];
    size_t  len = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(encode(h, payload, n, ectx, buf, sizeof(buf), &len)));
    eng.receive(c, app, buf, len, -60, 75, now);
  }

  void command(Seq seq, Cmd cmd, uint8_t arg = 0, CtxId ctx = 0) {
    Header h;
    h.type   = MsgType::Command;
    h.src    = kNodeBridge;
    h.dst    = kSelf;
    h.seq    = seq;
    h.ctx_id = ctx != 0 ? ctx : c.ctx_id;
    const msg::Command m{static_cast<uint8_t>(cmd), arg, 0};
    uint8_t            p[msg::kCommandLen];
    size_t             n = 0;
    msg::serialize(m, p, sizeof(p), &n);
    rx(h, p, n);
  }

  // The next queued frame, decoded as the bridge would.
  Frame next(uint8_t* buf) {
    OutFrame f;
    TEST_ASSERT_TRUE(out.pop(&f));
    std::memcpy(buf, f.bytes, f.len);
    DecodeCtx d;
    d.self           = kNodeBridge;
    d.accept_ver_min = kProtoVer;
    d.accept_ver_max = kProtoVer;
    d.mac            = &g_mac;
    d.node_key       = c.key;
    Frame fr;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(decode_header(buf, f.len, d, &fr)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(decode_payload(buf, f.len, d, &fr)));
    return fr;
  }

  msg::CommandAck next_ack(CtxId* ctx = nullptr, Seq* seq = nullptr) {
    uint8_t     buf[kOutFrameMax];
    const Frame f = next(buf);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::CommandAck),
                            static_cast<uint8_t>(f.hdr.type));
    if (ctx != nullptr) *ctx = f.hdr.ctx_id;
    if (seq != nullptr) *seq = f.hdr.seq;
    msg::CommandAck a;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(msg::deserialize(f.payload, f.payload_len, &a)));
    return a;
  }
};

uint8_t reason_of(const Frame& f) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::Status), static_cast<uint8_t>(f.hdr.type));
  schema::GateLinkStatusV1 st;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::deserialize(f.payload, f.payload_len, &st)));
  return st.status_reason;
}

schema::GateLinkEventV1 event_of(const Frame& f) {
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::Event), static_cast<uint8_t>(f.hdr.type));
  schema::GateLinkEventV1 ev;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::deserialize(f.payload, f.payload_len, &ev)));
  return ev;
}

void assert_result(AckResult want, const msg::CommandAck& a) {
  TEST_ASSERT_EQUAL_STRING(ack_result_name(want),
                           ack_result_name(static_cast<AckResult>(a.result)));
}

}  // namespace

// spec 6.4 - a POLL reaches the application, and its answer comes from this context.
void test_poll_is_answered_from_the_context() {
  Rig r;
  Header h;
  h.type   = MsgType::Poll;
  h.src    = kNodeBridge;
  h.dst    = kSelf;
  h.seq    = 9;
  h.ctx_id = r.c.ctx_id;
  const uint8_t p[1] = {0};
  r.rx(h, p, 1);
  uint8_t     buf[kOutFrameMax];
  const Frame f = r.next(buf);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::PollResponse), reason_of(f));
  TEST_ASSERT_EQUAL_UINT32(r.c.ctx_id, f.hdr.ctx_id);
  TEST_ASSERT_EQUAL_UINT16(1, f.hdr.seq);
}

// A frame for another node is counted and never answered.
void test_frame_for_another_node_is_counted_not_answered() {
  Rig r;
  Header h;
  h.type   = MsgType::Poll;
  h.src    = kNodeBridge;
  h.dst    = kNodeSim2;
  h.seq    = 9;
  const uint8_t p[1] = {0};
  r.rx(h, p, 1);
  TEST_ASSERT_EQUAL_size_t(0, r.out.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.c.counters.rx_not_addressed);
}

// spec 9.4 - check() before execute, record() before the ACK, and a retry answered from the
// cache without a second execution: root rule 2's second pulse, refused at the node.
void test_command_executes_once_and_retry_is_cached() {
  Rig r;
  r.command(5, Cmd::Open);
  assert_result(AckResult::Accepted, r.next_ack());
  r.command(5, Cmd::Open);
  const msg::CommandAck a = r.next_ack();
  assert_result(AckResult::DuplicateCached, a);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckResult::Accepted), a.detail);
  TEST_ASSERT_EQUAL_INT(1, r.app.executed);
  TEST_ASSERT_EQUAL_UINT32(1, r.c.executions);
}

// Impl Plan 5.2 - a deferred command holds the gate between check() and record(). A retry
// in that window gets no answer, a roll answers ACTUATOR_BUSY and changes nothing, and a
// second command answers ACTUATOR_BUSY. finish_command() then records and ACKs.
void test_deferred_command_holds_the_execution_window() {
  Rig r;
  r.app.defer = true;
  r.command(5, Cmd::Open);
  TEST_ASSERT_EQUAL_size_t(0, r.out.size());
  TEST_ASSERT_TRUE(r.c.pending.active);

  r.command(5, Cmd::Open);  // the bridge's retry
  TEST_ASSERT_EQUAL_size_t(0, r.out.size());

  const CtxId before = r.c.ctx_id;
  r.command(6, Cmd::RollContext, kRollContextGuard);
  assert_result(AckResult::ActuatorBusy, r.next_ack());
  TEST_ASSERT_EQUAL_UINT32(before, r.c.ctx_id);

  r.command(7, Cmd::Close);
  assert_result(AckResult::ActuatorBusy, r.next_ack());

  r.eng.finish_command(r.c, r.app, 2000);
  Seq             ack_seq = 0;
  msg::CommandAck a       = r.next_ack();
  ack_seq                 = a.ack_seq;
  assert_result(AckResult::Accepted, a);
  TEST_ASSERT_EQUAL_UINT16(5, ack_seq);
  TEST_ASSERT_FALSE(r.c.pending.active);
  TEST_ASSERT_EQUAL_INT(1, r.app.executed);

  r.command(5, Cmd::Open);
  assert_result(AckResult::DuplicateCached, r.next_ack());
}

// spec 9.4 steps 2 and 10.3 - a wrong ctx_id answers REJECTED_CTX under this node's own.
void test_wrong_context_is_refused_with_own_ctx() {
  Rig r;
  r.command(5, Cmd::Open, 0, r.c.ctx_id ^ 0x1u);
  CtxId                 ctx = 0;
  const msg::CommandAck a   = r.next_ack(&ctx);
  assert_result(AckResult::RejectedCtx, a);
  TEST_ASSERT_EQUAL_UINT32(r.c.ctx_id, ctx);
  TEST_ASSERT_EQUAL_INT(0, r.app.executed);
}

// spec 10.6, D58 - the roll skips the gate, changes the ctx_id and ACKs under the new one
// with status seq 1. A wrong guard is REJECTED_ARG.
void test_roll_context() {
  Rig r;
  r.command(5, Cmd::Open);
  (void)r.next_ack();

  r.command(6, Cmd::RollContext, 0x00);
  assert_result(AckResult::RejectedArg, r.next_ack());

  const CtxId before = r.c.ctx_id;
  r.command(7, Cmd::RollContext, kRollContextGuard);
  CtxId                 ctx = 0;
  Seq                   seq = 0;
  const msg::CommandAck a   = r.next_ack(&ctx, &seq);
  assert_result(AckResult::Accepted, a);
  TEST_ASSERT_NOT_EQUAL(before, r.c.ctx_id);
  TEST_ASSERT_EQUAL_UINT32(r.c.ctx_id, ctx);
  TEST_ASSERT_EQUAL_UINT16(1, seq);
  // The gate starts again: seq 5 is new in the new context.
  r.command(5, Cmd::Open);
  assert_result(AckResult::Accepted, r.next_ack());
}

// spec 8.1 - an accepted REBOOT owes a restart after its ACK.
void test_reboot_is_owed_after_its_ack() {
  Rig r;
  r.command(5, Cmd::Reboot, kRebootGuard);
  assert_result(AckResult::Accepted, r.next_ack());
  TEST_ASSERT_TRUE(r.eng.restart_owed());
}

// spec 10.7 - STATUS with BOOT, then the BOOT event carrying the cause.
void test_announce_boot_sends_status_then_event() {
  Rig r;
  TEST_ASSERT_TRUE(r.eng.announce_boot(r.c, r.app, ResetCause::Watchdog, 0));
  uint8_t buf[kOutFrameMax];
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::Boot), reason_of(r.next(buf)));
  const schema::GateLinkEventV1 ev = event_of(r.next(buf));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventType::Boot), ev.event_type);
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(ResetCause::Watchdog), ev.detail);
}

// spec 12.4.2 step 8, D69 - an owed revert goes out ahead of the next answer to the bridge,
// and the next poll's STATUS carries CONFIG_CHANGE.
void test_phy_revert_is_reported_to_the_bridge() {
  Rig r;
  Engine::note_phy_revert(r.c, RevertCause::Window);
  Header h;
  h.type   = MsgType::Poll;
  h.src    = kNodeBridge;
  h.dst    = kSelf;
  h.seq    = 9;
  h.ctx_id = r.c.ctx_id;
  const uint8_t p[1] = {0};
  r.rx(h, p, 1);
  uint8_t buf[kOutFrameMax];
  const schema::GateLinkEventV1 ev = event_of(r.next(buf));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventType::PhyReverted), ev.event_type);
  TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(RevertCause::Window), ev.detail);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::ConfigChange), reason_of(r.next(buf)));
  TEST_ASSERT_EQUAL_UINT16(0, r.c.phy_revert_detail);
  TEST_ASSERT_FALSE(r.c.config_change_owed);
}

namespace {

void hex_req(Rig& r, Seq seq, const char* text) {
  Header h;
  h.type   = MsgType::HexReq;
  h.src    = kNodeBridge;
  h.dst    = kSelf;
  h.seq    = seq;
  h.ctx_id = r.c.ctx_id;
  const msg::HexReq req{0, static_cast<uint8_t>(std::strlen(text)),
                        reinterpret_cast<const uint8_t*>(text)};
  uint8_t p[kMaxPayloadPlain];
  size_t  n = 0;
  msg::serialize(req, p, sizeof(p), &n);
  r.rx(h, p, n);
}

HexStatus next_hex_status(Rig& r, Seq* seq) {
  uint8_t     buf[kOutFrameMax];
  const Frame f = r.next(buf);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::HexRsp), static_cast<uint8_t>(f.hdr.type));
  *seq = f.hdr.seq;
  return static_cast<HexStatus>(f.payload[0]);
}

}  // namespace

// spec 8.13 - one transaction at a time, and TIMEOUT rather than silence.
void test_hex_busy_then_timeout() {
  Rig r;
  r.app.hex_silent = true;
  hex_req(r, 20, ":154");
  TEST_ASSERT_TRUE(r.c.hex_pending.active);
  hex_req(r, 21, ":154");
  Seq seq = 0;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Busy),
                          static_cast<uint8_t>(next_hex_status(r, &seq)));
  TEST_ASSERT_EQUAL_UINT16(21, seq);
  r.eng.tick(r.c, 1499);
  TEST_ASSERT_EQUAL_size_t(0, r.out.size());
  r.eng.tick(r.c, 1500);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Timeout),
                          static_cast<uint8_t>(next_hex_status(r, &seq)));
  TEST_ASSERT_EQUAL_UINT16(20, seq);
}

// A device that answers later: complete_hex() closes the transaction under the request's seq.
void test_hex_completed_later() {
  Rig r;
  r.app.hex_silent = true;
  hex_req(r, 20, ":154");
  TEST_ASSERT_TRUE(r.eng.complete_hex(r.c, HexStatus::Ok, ":5A0", 4));
  Seq seq = 0;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Ok),
                          static_cast<uint8_t>(next_hex_status(r, &seq)));
  TEST_ASSERT_EQUAL_UINT16(20, seq);
  TEST_ASSERT_FALSE(r.eng.complete_hex(r.c, HexStatus::Ok, ":5A0", 4));
}

// spec 7.4 - with no application parameters, GET_ALL is the PHY group, answered under the
// request's seq (spec 7.4.1).
void test_config_get_all_lists_the_phy_group() {
  Rig r;
  schema::NodeConfigV1 cfg;
  cfg.op = ConfigOp::GetAll;
  uint8_t p[kMaxSchemaPayload];
  size_t  n = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::serialize(cfg, p, sizeof(p), &n)));
  Header h;
  h.type   = MsgType::Config;
  h.src    = kNodeBridge;
  h.dst    = kSelf;
  h.seq    = 30;
  h.ctx_id = r.c.ctx_id;
  h.schema = kSchemaNodeConfigV1;
  r.rx(h, p, n);
  uint8_t     buf[kOutFrameMax];
  const Frame f = r.next(buf);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::ConfigAck), static_cast<uint8_t>(f.hdr.type));
  TEST_ASSERT_EQUAL_UINT16(30, f.hdr.seq);
  schema::NodeConfigAckV1 ack;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::deserialize(f.payload, f.payload_len, &ack)));
  TEST_ASSERT_EQUAL_UINT8(kPhyGroupSize, ack.count);
}

namespace {

void send_config(Rig& r, Seq seq, ConfigOp op, const schema::ConfigEntry* e = nullptr,
                 uint8_t ne = 0) {
  schema::NodeConfigV1 cfg;
  cfg.op    = op;
  cfg.count = ne;
  for (uint8_t i = 0; i < ne; ++i) cfg.entries[i] = e[i];
  uint8_t p[kMaxSchemaPayload];
  size_t  n = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::serialize(cfg, p, sizeof(p), &n)));
  Header h;
  h.type   = MsgType::Config;
  h.src    = kNodeBridge;
  h.dst    = kSelf;
  h.seq    = seq;
  h.ctx_id = r.c.ctx_id;
  h.schema = kSchemaNodeConfigV1;
  r.rx(h, p, n);
}

// One answer as the bridge reads it: CONFIG_ACKs until MORE_FOLLOWS is clear. Checks what
// every message of an answer shares (spec 7.4.1) and that param_id ascends across all of
// them. Returns the message count; the results land in `ids`.
size_t read_answer(Rig& r, ConfigOp op, uint16_t* ids, size_t cap, size_t* nids, Seq* seqs) {
  *nids        = 0;
  size_t   m   = 0;
  uint32_t last = 0;
  for (;;) {
    TEST_ASSERT_TRUE_MESSAGE(r.out.size() > 0, "answer ended on a message marked MORE_FOLLOWS");
    uint8_t     buf[kOutFrameMax];
    const Frame f = r.next(buf);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::ConfigAck),
                            static_cast<uint8_t>(f.hdr.type));
    schema::NodeConfigAckV1 ack;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(schema::deserialize(f.payload, f.payload_len, &ack)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(op), static_cast<uint8_t>(ack.op));
    seqs[m] = f.hdr.seq;
    for (uint8_t i = 0; i < ack.count; ++i) {
      TEST_ASSERT_TRUE(ack.entries[i].param_id > last);
      last = ack.entries[i].param_id;
      TEST_ASSERT_TRUE(*nids < cap);
      ids[(*nids)++] = ack.entries[i].param_id;
    }
    ++m;
    if (!ack.more_follows) return m;
  }
}

}  // namespace

// spec 7.4.1 - GateLink's readback outgrows one CONFIG_ACK. 25 uint32 rows and the PHY
// group are 267 bytes against 193, so two messages: the first marked, both under the
// request's seq, and the PHY rows (0x01xx) ahead of the application's (0x1xxx).
void test_get_all_splits_in_param_id_order() {
  Rig r;
  r.app.listed = 25;
  send_config(r, 30, ConfigOp::GetAll);
  uint16_t ids[64];
  size_t   n = 0;
  Seq      seqs[4];
  TEST_ASSERT_EQUAL_size_t(2, read_answer(r, ConfigOp::GetAll, ids, 64, &n, seqs));
  TEST_ASSERT_EQUAL_size_t(25 + kPhyGroupSize, n);
  TEST_ASSERT_EQUAL_UINT16(30, seqs[0]);
  TEST_ASSERT_EQUAL_UINT16(30, seqs[1]);
  TEST_ASSERT_EQUAL_UINT16(0x1018, ids[n - 1]);
  TEST_ASSERT_EQUAL_size_t(0, r.out.size());
}

// spec 7.4.1, D45 - the unsolicited readback after REQUEST_CONFIG takes a status seq per
// message, in the order sent.
void test_unsolicited_readback_takes_a_status_seq_per_message() {
  Rig r;
  r.app.listed = 25;
  r.command(40, Cmd::RequestConfig);
  assert_result(AckResult::Accepted, r.next_ack());
  uint16_t ids[64];
  size_t   n = 0;
  Seq      seqs[4];
  TEST_ASSERT_EQUAL_size_t(2, read_answer(r, ConfigOp::GetAll, ids, 64, &n, seqs));
  TEST_ASSERT_EQUAL_UINT16(static_cast<Seq>(seqs[0] + 1), seqs[1]);
}

// spec 7.4.1 - a repeated GET_ALL is walked again, not answered DUPLICATE_CACHED.
void test_repeated_get_all_is_answered_again() {
  Rig r;
  r.app.listed = 25;
  send_config(r, 30, ConfigOp::GetAll);
  uint16_t ids[64];
  size_t   n = 0;
  Seq      seqs[4];
  read_answer(r, ConfigOp::GetAll, ids, 64, &n, seqs);
  r.app.listed = 26;  // a value that changed between answers is reported as it now stands
  send_config(r, 30, ConfigOp::GetAll);
  TEST_ASSERT_EQUAL_size_t(2, read_answer(r, ConfigOp::GetAll, ids, 64, &n, seqs));
  TEST_ASSERT_EQUAL_size_t(26 + kPhyGroupSize, n);
  TEST_ASSERT_EQUAL_UINT16(30, seqs[1]);
}

// spec 7.4.1 - at most 4 messages, and the fourth ends the answer unmarked, so the bridge
// is not left waiting for a fifth.
void test_answer_stops_at_four_messages() {
  Rig r;
  r.app.listed = 100;
  send_config(r, 30, ConfigOp::GetAll);
  uint16_t ids[160];
  size_t   n = 0;
  Seq      seqs[4];
  TEST_ASSERT_EQUAL_size_t(4, read_answer(r, ConfigOp::GetAll, ids, 160, &n, seqs));
  TEST_ASSERT_TRUE(n < 100 + kPhyGroupSize);
  TEST_ASSERT_EQUAL_size_t(0, r.out.size());
}

// An answer the outbox cannot hold whole is not started: a partial one leaves the bridge
// holding a message marked MORE_FOLLOWS.
void test_answer_is_queued_whole_or_not_at_all() {
  Rig r;
  r.app.listed = 25;
  const uint8_t filler[1] = {0};
  while (r.out.free_slots() > 1) r.out.push(filler, 1);
  const uint32_t dropped = r.eng.answers_dropped();
  send_config(r, 30, ConfigOp::GetAll);
  TEST_ASSERT_EQUAL_size_t(1, r.out.free_slots());
  TEST_ASSERT_EQUAL_UINT32(dropped + 1, r.eng.answers_dropped());
}

// spec 7.4, D53 - a SET's persist_status is the store's after the set: PERSISTED when the
// node's store holds every override, APPLIED_NOT_PERSISTED when it does not, NOT_APPLIED
// when nothing applied. Until GL3 the engine answered every applied SET
// APPLIED_NOT_PERSISTED, which a node with a working card would have contradicted.
void test_config_set_reports_the_stores_persistence() {
  struct Case {
    uint16_t      id;
    bool          unsaved;
    PersistStatus want;
  };
  const Case cases[] = {{0x1000, false, PersistStatus::Persisted},
                        {0x1000, true, PersistStatus::AppliedNotPersisted},
                        {0x1FFF, false, PersistStatus::NotApplied}};
  Seq seq = 70;
  for (const Case& k : cases) {
    Rig r;
    r.app.unsaved = k.unsaved;
    schema::ConfigEntry e;
    schema::entry_pack(&e, k.id, PType::U32, 5);
    send_config(r, seq, ConfigOp::Set, &e, 1);
    uint8_t     buf[kOutFrameMax];
    const Frame f = r.next(buf);
    schema::NodeConfigAckV1 ack;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(schema::deserialize(f.payload, f.payload_len, &ack)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(k.want), static_cast<uint8_t>(ack.persist_status));
    ++seq;
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_poll_is_answered_from_the_context);
  RUN_TEST(test_frame_for_another_node_is_counted_not_answered);
  RUN_TEST(test_command_executes_once_and_retry_is_cached);
  RUN_TEST(test_deferred_command_holds_the_execution_window);
  RUN_TEST(test_wrong_context_is_refused_with_own_ctx);
  RUN_TEST(test_roll_context);
  RUN_TEST(test_reboot_is_owed_after_its_ack);
  RUN_TEST(test_announce_boot_sends_status_then_event);
  RUN_TEST(test_phy_revert_is_reported_to_the_bridge);
  RUN_TEST(test_hex_busy_then_timeout);
  RUN_TEST(test_hex_completed_later);
  RUN_TEST(test_config_get_all_lists_the_phy_group);
  RUN_TEST(test_config_set_reports_the_stores_persistence);
  RUN_TEST(test_get_all_splits_in_param_id_order);
  RUN_TEST(test_unsolicited_readback_takes_a_status_seq_per_message);
  RUN_TEST(test_repeated_get_all_is_answered_again);
  RUN_TEST(test_answer_stops_at_four_messages);
  RUN_TEST(test_answer_is_queued_whole_or_not_at_all);
  return UNITY_END();
}
