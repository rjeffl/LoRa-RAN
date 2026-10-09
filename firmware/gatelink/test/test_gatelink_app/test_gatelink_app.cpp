// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL3 - GateLink's application through lran-node's engine, on the host (GateLink Impl Plan
// 5.2, 6.1; spec 6.6, 7.2, 8.1, 9.4).
//
// The "bridge" is the library's codec with self = 0x00, as in lran-node's own suite. The
// port stands in for io_task: it records each sequence it is handed.

#include <unity.h>

#include <cstring>
#include <string>

#include "gatelink_app.h"
#include "lran/lran.h"
#include "lran/node/engine.h"
#include "lran/node/names.h"
#include "refimpl_mac.h"
#include "test_key.h"

using namespace lran;
using namespace lran::node;
using gatelink::GateLinkApp;
using gatelink::kNoRelay;
using gatelink::RelaySequence;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefKdf g_kdf;
refimpl::RefMac g_mac;

uint32_t g_next_random = 0x7000;
uint32_t counting_random() { return ++g_next_random; }

class NullSink final : public Sink {
 public:
  void line(const char*) override {}
};

class FakePort final : public gatelink::GateLinkPort {
 public:
  bool dispatch(const RelaySequence& seq) override {
    if (refuse) return false;
    last = seq;
    ++count;
    return true;
  }
  gatelink::NodeSnapshot snapshot() const override {
    gatelink::NodeSnapshot s;
    s.inputs             = 0x05;
    s.node_mv            = 12040;
    s.enclosure_temp_c10 = 287;
    s.uptime_s           = 42;
    return s;
  }
  gatelink::MpptSnapshot mppt() const override { return mppt_view; }
  bool hex_submit(const gatelink::HexJob& job) override {
    if (hex_refuse) return false;
    jobs[job_count++ % 4] = job;
    return true;
  }
  void param_changed(uint16_t id, int32_t v) override {
    last_param = id;
    last_value = v;
    ++params_changed;
  }
  bool hex_take(gatelink::HexResult* out) override {
    if (!result_ready) return false;
    result_ready = false;
    *out         = result;
    return true;
  }
  void answer(uint32_t token, HexStatus status, const char* hex) {
    result        = gatelink::HexResult{};
    result.token  = token;
    result.status = status;
    result.n      = std::strlen(hex);
    std::memcpy(result.hex, hex, result.n);
    result_ready  = true;
  }

  RelaySequence          last;
  int                    count  = 0;
  bool                   refuse = false;
  gatelink::MpptSnapshot mppt_view;
  gatelink::HexJob       jobs[4];
  int                    job_count    = 0;
  bool                   hex_refuse   = false;
  gatelink::HexResult    result;
  bool                   result_ready = false;
  uint16_t               last_param     = 0;
  int32_t                last_value     = 0;
  int                    params_changed = 0;
};

// The card, for the CONFIG path's two legs: present, and pulled.
class FakeCard final : public gatelink::ConfigFile {
 public:
  bool   mount(bool) override { return present; }
  size_t read(char*, size_t) override { return 0; }
  bool   write(const char* t, size_t n) override {
    if (!present) return false;
    text.assign(t, n);
    return true;
  }
  bool        present = true;
  std::string text;
};

struct Rig {
  Outbox      out;
  NullSink    log;
  Engine      eng{&out, &g_mac, &log};
  FakePort    port;
  GateLinkApp app{&port, counting_random, &log};
  Context     c;
  FakeCard    card;
  gatelink::GateLinkConfig cfg{&card};
  explicit Rig(bool card_present = true) {
    c.id = kNodeGateLink;
    g_kdf.derive_node_key(lran_test::kTestMasterKey, kNodeGateLink, c.key);
    reset_context(c, counting_random);
    card.present = card_present;
    cfg.persist().load(&cfg.store());
    app.set_config(&cfg);
    app.apply_all(c);
  }

  void config(Seq seq, ConfigOp op, const schema::ConfigEntry* e = nullptr, uint8_t n = 0) {
    schema::NodeConfigV1 m;
    m.op    = op;
    m.count = n;
    for (uint8_t i = 0; i < n; ++i) m.entries[i] = e[i];
    uint8_t p[kMaxPayloadPlain];
    size_t  len = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(schema::serialize(m, p, sizeof(p), &len)));
    Header h = to_node(MsgType::Config, seq);
    h.schema = kSchemaNodeConfigV1;
    rx(h, p, len);
  }

  schema::NodeConfigAckV1 next_config_ack(Seq* seq = nullptr) {
    uint8_t     buf[kOutFrameMax];
    const Frame f = next(buf);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::ConfigAck),
                            static_cast<uint8_t>(f.hdr.type));
    if (seq != nullptr) *seq = f.hdr.seq;
    schema::NodeConfigAckV1 a;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(schema::deserialize(f.payload, f.payload_len, &a)));
    return a;
  }

  schema::GateLinkStatusV1 poll_status(uint8_t flags = 0) {
    const uint8_t p[1] = {flags};
    rx(to_node(MsgType::Poll, 9), p, 1);
    uint8_t     buf[kOutFrameMax];
    const Frame f = next(buf);
    schema::GateLinkStatusV1 s;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(schema::deserialize(f.payload, f.payload_len, &s)));
    return s;
  }

  void rx(const Header& h, const uint8_t* payload, size_t n, const uint8_t* key = nullptr) {
    EncodeCtx ectx;
    ectx.mac      = &g_mac;
    ectx.node_key = key != nullptr ? key : c.key;
    uint8_t buf[kMaxFrame];
    size_t  len = 0;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(encode(h, payload, n, ectx, buf, sizeof(buf), &len)));
    eng.receive(c, app, buf, len, -60, 75, 1000);
  }

  Header to_node(MsgType type, Seq seq) const {
    Header h;
    h.type   = type;
    h.src    = kNodeBridge;
    h.dst    = kNodeGateLink;
    h.seq    = seq;
    h.ctx_id = c.ctx_id;
    return h;
  }

  void command(Seq seq, Cmd cmd, uint8_t arg = 0) {
    const msg::Command m{static_cast<uint8_t>(cmd), arg, 0};
    uint8_t            p[msg::kCommandLen];
    size_t             n = 0;
    msg::serialize(m, p, sizeof(p), &n);
    rx(to_node(MsgType::Command, seq), p, n);
  }

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

  void hex_req(Seq seq, const char* text, const uint8_t* key = nullptr) {
    const msg::HexReq req{0, static_cast<uint8_t>(std::strlen(text)),
                          reinterpret_cast<const uint8_t*>(text)};
    uint8_t p[kMaxPayloadPlain];
    size_t  n = 0;
    msg::serialize(req, p, sizeof(p), &n);
    rx(to_node(MsgType::HexReq, seq), p, n, key);
  }

  // The HEX_RSP's status and string; `seq` receives its seq.
  HexStatus next_hex(Seq* seq, std::string* hex = nullptr) {
    uint8_t     buf[kOutFrameMax];
    const Frame f = next(buf);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::HexRsp), static_cast<uint8_t>(f.hdr.type));
    *seq = f.hdr.seq;
    if (hex != nullptr) hex->assign(reinterpret_cast<const char*>(f.payload + 2), f.payload[1]);
    return static_cast<HexStatus>(f.payload[0]);
  }

  msg::CommandAck next_ack() {
    uint8_t     buf[kOutFrameMax];
    const Frame f = next(buf);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::CommandAck),
                            static_cast<uint8_t>(f.hdr.type));
    msg::CommandAck a;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                          static_cast<int>(msg::deserialize(f.payload, f.payload_len, &a)));
    return a;
  }
};

void assert_result(AckResult want, const msg::CommandAck& a) {
  TEST_ASSERT_EQUAL_STRING(ack_result_name(want),
                           ack_result_name(static_cast<AckResult>(a.result)));
}

void assert_sequence(uint8_t first, uint8_t second, const RelaySequence& s) {
  TEST_ASSERT_EQUAL_UINT8(first, s.first);
  TEST_ASSERT_EQUAL_UINT8(second, s.second);
}

schema::ConfigEntry set_entry(uint16_t id, PType t, uint32_t raw) {
  schema::ConfigEntry e;
  schema::entry_pack(&e, id, t, raw);
  return e;
}

constexpr uint16_t kRelayPulseMs = 0x1000;
constexpr uint8_t  kFlagPersisted    = 0x01;  // spec 7.2.8
constexpr uint8_t  kFlagCardWritable = 0x02;

}  // namespace

// PRD 3.1.2 and Impl Plan 6.1 - K1 open and lock, K2 unlock, K3 momentary open, K4
// immediate close, which always follows K2 (R-3.1.2c).
void test_commands_map_to_relays() {
  RelaySequence s;
  TEST_ASSERT_TRUE(gatelink::relay_sequence_for({0x01, 0, 0}, &s));
  assert_sequence(2, kNoRelay, s);
  TEST_ASSERT_TRUE(gatelink::relay_sequence_for({0x03, 0, 0}, &s));
  assert_sequence(0, kNoRelay, s);
  TEST_ASSERT_TRUE(gatelink::relay_sequence_for({0x04, 0, 0}, &s));
  assert_sequence(1, kNoRelay, s);
  TEST_ASSERT_TRUE(gatelink::relay_sequence_for({0x02, 0, 0}, &s));
  assert_sequence(1, kNoRelay, s);
  TEST_ASSERT_TRUE(gatelink::relay_sequence_for({0x02, 1, 0}, &s));
  assert_sequence(1, 3, s);
  TEST_ASSERT_FALSE(gatelink::relay_sequence_for({0x02, 2, 0}, &s));
  TEST_ASSERT_FALSE(gatelink::relay_sequence_for({0x00, 0, 0}, &s));
  TEST_ASSERT_FALSE(gatelink::relay_sequence_for({0x05, 0, 0}, &s));
}

// Impl Plan 5.2, decided 2026-10-07 - the ACK waits for io_task. A retry in the window gets
// nothing (spec 9.4); after finish_command() the ACK goes out, and a retry is answered from
// the cache without a second dispatch (root rule 2).
void test_actuation_acks_after_the_pulse() {
  Rig r;
  r.command(7, Cmd::Open);
  TEST_ASSERT_EQUAL_INT(1, r.port.count);
  assert_sequence(2, kNoRelay, r.port.last);
  TEST_ASSERT_EQUAL_size_t(0, r.out.size());
  TEST_ASSERT_TRUE(r.c.pending.active);

  r.command(7, Cmd::Open);  // the bridge's retry, inside the window
  TEST_ASSERT_EQUAL_size_t(0, r.out.size());
  TEST_ASSERT_EQUAL_INT(1, r.port.count);

  r.eng.finish_command(r.c, r.app, 1600);
  assert_result(AckResult::Accepted, r.next_ack());

  r.command(7, Cmd::Open);
  const msg::CommandAck a = r.next_ack();
  assert_result(AckResult::DuplicateCached, a);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckResult::Accepted), a.detail);
  TEST_ASSERT_EQUAL_INT(1, r.port.count);
}

// The console's `lran ack drop` withholds one fresh ACK and disarms. The retry is answered
// from the cache, which the hook never touches (spec 9.4 step 4).
void test_ack_drop_withholds_one_fresh_ack() {
  Rig r;
  r.app.withhold_next_ack();
  r.command(3, Cmd::Nop);
  TEST_ASSERT_EQUAL_size_t(0, r.out.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.app.acks_withheld());

  r.command(3, Cmd::Nop);
  const msg::CommandAck a = r.next_ack();
  assert_result(AckResult::DuplicateCached, a);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckResult::Accepted), a.detail);

  r.command(4, Cmd::Nop);
  assert_result(AckResult::Accepted, r.next_ack());
  TEST_ASSERT_EQUAL_UINT32(1, r.app.acks_withheld());
}

// An immediate close is one command and one ACK: HA sees one "close" (Impl Plan 6.1).
void test_immediate_close_is_one_sequence() {
  Rig r;
  r.command(3, Cmd::Close, 1);
  TEST_ASSERT_EQUAL_INT(1, r.port.count);
  assert_sequence(1, 3, r.port.last);
}

void test_close_with_a_bad_arg_is_refused_at_once() {
  Rig r;
  r.command(3, Cmd::Close, 2);
  assert_result(AckResult::RejectedArg, r.next_ack());
  TEST_ASSERT_EQUAL_INT(0, r.port.count);
}

// spec 8.2 DRY_RUN - accepted and logged, no output energized, answered at once.
void test_dry_run_moves_nothing() {
  Rig r;
  r.command(1, Cmd::SetRelayDryRun, 1);
  assert_result(AckResult::Accepted, r.next_ack());
  r.command(2, Cmd::Open);
  assert_result(AckResult::DryRun, r.next_ack());
  TEST_ASSERT_EQUAL_INT(0, r.port.count);
  TEST_ASSERT_TRUE(r.app.dry_run());
}

// spec 8.2 ACTUATOR_BUSY - io_task could not take the sequence.
void test_full_port_answers_busy() {
  Rig r;
  r.port.refuse = true;
  r.command(4, Cmd::HoldOpen);
  assert_result(AckResult::ActuatorBusy, r.next_ack());
  TEST_ASSERT_EQUAL_UINT32(1, r.app.dispatch_refused());
  TEST_ASSERT_FALSE(r.c.pending.active);
}

// spec 8.1 - REBOOT needs its guard, and is owed only once the ACK is out.
void test_reboot_needs_its_guard() {
  Rig r;
  r.command(1, Cmd::Reboot, 0x00);
  assert_result(AckResult::RejectedArg, r.next_ack());
  TEST_ASSERT_FALSE(r.eng.restart_owed());
  r.command(2, Cmd::Reboot, kRebootGuard);
  assert_result(AckResult::Accepted, r.next_ack());
  TEST_ASSERT_TRUE(r.eng.restart_owed());
}

// spec 7.2 - schema 0x10, what is known filled in, and every unread block at its sentinel
// with its "no data" flag (root rule 6).
void test_poll_answers_status_with_sentinels() {
  Rig r;
  const uint8_t p[1] = {0};
  r.rx(r.to_node(MsgType::Poll, 9), p, 1);
  uint8_t     buf[kOutFrameMax];
  const Frame f = r.next(buf);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::Status), static_cast<uint8_t>(f.hdr.type));
  TEST_ASSERT_EQUAL_UINT8(kSchemaGateLinkStatusV1, f.hdr.schema);
  schema::GateLinkStatusV1 s;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::deserialize(f.payload, f.payload_len, &s)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StatusReason::PollResponse), s.status_reason);
  TEST_ASSERT_EQUAL_UINT8(0x05, s.input_bits);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GateState::Unknown), s.gate_state);
  TEST_ASSERT_EQUAL_UINT16(12040, s.node_mv);
  TEST_ASSERT_EQUAL_INT16(kI16NotAvailable, s.node_ma);
  TEST_ASSERT_EQUAL_INT16(287, s.enclosure_temp_c10);
  TEST_ASSERT_EQUAL_UINT32(42, s.uptime_s);
  TEST_ASSERT_EQUAL_UINT16(kU16NotAvailable, s.batt_mv);
  TEST_ASSERT_EQUAL_HEX8(0x02, s.mppt_flags & 0x02);
  TEST_ASSERT_EQUAL_HEX8(0x00, s.bms_flags & 0x01);
  TEST_ASSERT_EQUAL_UINT8(kSocNotAvailable, s.bms_soc);
  TEST_ASSERT_EQUAL_UINT32(kU32NotAvailable, s.last_traversal_age_s);
}

// spec 6.6 - the echo swaps src and dst and keeps seq, the flags and the bytes.
void test_ping_is_echoed() {
  Rig r;
  uint8_t         data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const msg::Ping ping{0, sizeof(data), data};
  uint8_t         p[msg::kPingHdrLen + sizeof(data)];
  size_t          n = 0;
  msg::serialize(ping, p, sizeof(p), &n);
  r.rx(r.to_node(MsgType::Ping, 0x1234), p, n);
  uint8_t     buf[kOutFrameMax];
  const Frame f = r.next(buf);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(MsgType::Ping), static_cast<uint8_t>(f.hdr.type));
  TEST_ASSERT_EQUAL_UINT8(kNodeGateLink, f.hdr.src);
  TEST_ASSERT_EQUAL_UINT8(kNodeBridge, f.hdr.dst);
  TEST_ASSERT_EQUAL_UINT16(0x1234, f.hdr.seq);
  TEST_ASSERT_EQUAL_size_t(n, f.payload_len);
  TEST_ASSERT_EQUAL_MEMORY(p, f.payload, n);
}

// spec 7.2.2 - the MPPT block is vedirect_task's snapshot, passed through.
void test_status_carries_the_mppt_snapshot() {
  Rig r;
  r.port.mppt_view.batt_mv      = 13360;
  r.port.mppt_view.batt_ma      = -40;
  r.port.mppt_view.yield_total  = 467;
  r.port.mppt_view.charge_state = 3;
  r.port.mppt_view.mppt_flags   = gatelink::kMpptFlagLoadOn;
  const uint8_t p[1] = {0};
  r.rx(r.to_node(MsgType::Poll, 9), p, 1);
  uint8_t     buf[kOutFrameMax];
  const Frame f = r.next(buf);
  schema::GateLinkStatusV1 s;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(schema::deserialize(f.payload, f.payload_len, &s)));
  TEST_ASSERT_EQUAL_UINT16(13360, s.batt_mv);
  TEST_ASSERT_EQUAL_INT16(-40, s.batt_ma);
  TEST_ASSERT_EQUAL_UINT32(467, s.yield_total);
  TEST_ASSERT_EQUAL_UINT8(3, s.charge_state);
  TEST_ASSERT_EQUAL_HEX8(gatelink::kMpptFlagLoadOn, s.mppt_flags);
}

// spec 7.6 - a Get needs no MAC. It goes to vedirect_task, and its answer goes back under
// the request's seq (D74).
void test_hex_get_round_trip() {
  Rig r;
  r.hex_req(40, ":70001004D");
  TEST_ASSERT_EQUAL_INT(1, r.port.job_count);
  TEST_ASSERT_EQUAL_STRING(":70001004D", r.port.jobs[0].hex);
  TEST_ASSERT_TRUE(r.c.hex_pending.active);
  r.app.poll_hex(r.eng, r.c);
  TEST_ASSERT_EQUAL_size_t(0, r.out.size());
  r.port.answer(r.port.jobs[0].token, HexStatus::Ok, ":7000100075A048");
  r.app.poll_hex(r.eng, r.c);
  Seq         seq = 0;
  std::string hex;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Ok),
                          static_cast<uint8_t>(r.next_hex(&seq, &hex)));
  TEST_ASSERT_EQUAL_UINT16(40, seq);
  TEST_ASSERT_EQUAL_STRING(":7000100075A048", hex.c_str());
  TEST_ASSERT_FALSE(r.c.hex_pending.active);
}

// GL4's write-rejection criterion, spec 7.6 step 3 (D73) - a Set under the wrong key is
// answered REJECTED_UNAUTHENTICATED and never reaches vedirect_task.
void test_unauthenticated_set_never_reaches_the_mppt() {
  Rig           r;
  const uint8_t wrong[kNodeKeyLen] = {0x5A};
  r.hex_req(41, ":8F7ED008C0598", wrong);
  Seq seq = 0;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::RejectedUnauthenticated),
                          static_cast<uint8_t>(r.next_hex(&seq)));
  TEST_ASSERT_EQUAL_UINT16(41, seq);
  TEST_ASSERT_EQUAL_INT(0, r.port.job_count);
}

// The same Set with the node's MAC goes through.
void test_authenticated_set_is_forwarded() {
  Rig r;
  r.hex_req(42, ":8F7ED008C0598");
  TEST_ASSERT_EQUAL_INT(1, r.port.job_count);
  TEST_ASSERT_EQUAL_STRING(":8F7ED008C0598", r.port.jobs[0].hex);
}

// vedirect_task still busy with a job the engine gave up on: BUSY, not silence.
void test_hex_refused_by_the_port_answers_busy() {
  Rig r;
  r.port.hex_refuse = true;
  r.hex_req(43, ":154");
  r.app.poll_hex(r.eng, r.c);
  Seq seq = 0;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Busy),
                          static_cast<uint8_t>(r.next_hex(&seq)));
  TEST_ASSERT_EQUAL_UINT16(43, seq);
}

// An answer to a job the engine has already timed out must not answer the next request.
void test_late_answer_is_discarded() {
  Rig r;
  r.hex_req(44, ":154");
  const uint32_t old = r.port.jobs[0].token;
  r.eng.tick(r.c, 1000 + r.app.hex_timeout_ms(r.c));
  Seq seq = 0;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Timeout),
                          static_cast<uint8_t>(r.next_hex(&seq)));
  r.hex_req(45, ":154");
  r.port.answer(old, HexStatus::Ok, ":51641F9");
  r.app.poll_hex(r.eng, r.c);
  TEST_ASSERT_EQUAL_size_t(0, r.out.size());
  TEST_ASSERT_EQUAL_UINT32(1, r.app.hex_late());
  TEST_ASSERT_TRUE(r.c.hex_pending.active);
}

// ---------------------------------------------------------------------------
// CONFIG - Impl Plan 6.4 and V-10, with the card and without it (spec 7.4, 8.11)
// ---------------------------------------------------------------------------

// spec 7.4 - the ACK carries the effective value, the override bit, and PERSISTED once the
// card holds it. The new value reaches the port, which carries it to its task.
void test_config_set_with_a_card_is_persisted() {
  Rig r;
  const schema::ConfigEntry e = set_entry(kRelayPulseMs, PType::U16, 700);
  r.config(50, ConfigOp::Set, &e, 1);
  Seq                           seq = 0;
  const schema::NodeConfigAckV1 a   = r.next_config_ack(&seq);
  TEST_ASSERT_EQUAL_UINT16(50, seq);
  TEST_ASSERT_EQUAL(PersistStatus::Persisted, a.persist_status);
  TEST_ASSERT_EQUAL_UINT8(1, a.count);
  TEST_ASSERT_EQUAL(ParamStatus::Ok, a.entries[0].status);
  TEST_ASSERT_TRUE(a.entries[0].is_override);
  TEST_ASSERT_EQUAL_UINT32(700, schema::entry_raw(a.entries[0].value, a.entries[0].len));
  TEST_ASSERT_EQUAL_UINT16(kRelayPulseMs, r.port.last_param);
  TEST_ASSERT_EQUAL_INT32(700, r.port.last_value);
  TEST_ASSERT_FALSE(r.app.take_card_retry());  // a working card owes no retry
  TEST_ASSERT_TRUE(r.card.text.find("\"relay_pulse_ms\": 700") != std::string::npos);

  const schema::GateLinkStatusV1 s = r.poll_status();
  TEST_ASSERT_EQUAL_HEX8(kFlagPersisted | kFlagCardWritable,
                         s.node_flags & (kFlagPersisted | kFlagCardWritable));
}

// V-10's second leg: no card, and every answer says the change was not saved.
void test_config_set_with_no_card_says_so() {
  Rig r(false);
  TEST_ASSERT_EQUAL_HEX8(kFlagPersisted, r.poll_status().node_flags & 0x03);  // nothing unsaved yet

  const schema::ConfigEntry e = set_entry(kRelayPulseMs, PType::U16, 700);
  r.config(50, ConfigOp::Set, &e, 1);
  const schema::NodeConfigAckV1 a = r.next_config_ack();
  TEST_ASSERT_EQUAL(PersistStatus::AppliedNotPersisted, a.persist_status);
  TEST_ASSERT_EQUAL(ParamStatus::Ok, a.entries[0].status);
  TEST_ASSERT_EQUAL_INT32(700, r.port.last_value);  // applied all the same
  TEST_ASSERT_TRUE(r.app.take_card_retry());         // lora_task tries the card once
  TEST_ASSERT_FALSE(r.app.take_card_retry());

  TEST_ASSERT_EQUAL_HEX8(0x00, r.poll_status().node_flags & 0x03);

  // A read reports the same, so a readback after a lost ACK is honest too (spec 7.4, D53).
  r.config(51, ConfigOp::GetAll);
  TEST_ASSERT_EQUAL(PersistStatus::AppliedNotPersisted, r.next_config_ack().persist_status);
}

// The card pulled after boot: found by the write that fails.
void test_config_set_after_the_card_is_pulled() {
  Rig r;
  r.card.present = false;
  const schema::ConfigEntry e = set_entry(kRelayPulseMs, PType::U16, 700);
  r.config(50, ConfigOp::Set, &e, 1);
  TEST_ASSERT_EQUAL(PersistStatus::AppliedNotPersisted, r.next_config_ack().persist_status);
  TEST_ASSERT_EQUAL_HEX8(0x00, r.poll_status().node_flags & 0x03);

  r.card.present = true;
  TEST_ASSERT_TRUE(r.app.take_card_retry());
  TEST_ASSERT_TRUE(r.cfg.persist().refresh());
  TEST_ASSERT_EQUAL_HEX8(0x03, r.poll_status().node_flags & 0x03);
}

// spec 7.4.1 - the full table, every row once, ascending, in two messages; the PHY group
// comes from the engine and nowhere else.
void test_config_get_all_lists_every_row_once() {
  Rig r;
  r.config(52, ConfigOp::GetAll);
  const schema::NodeConfigAckV1 first  = r.next_config_ack();
  const schema::NodeConfigAckV1 second = r.next_config_ack();
  TEST_ASSERT_TRUE(first.more_follows);
  TEST_ASSERT_FALSE(second.more_follows);
  TEST_ASSERT_EQUAL_size_t(r.cfg.table().size(), first.count + second.count);
  uint16_t prev = 0;
  for (const schema::NodeConfigAckV1* m : {&first, &second}) {
    for (uint8_t i = 0; i < m->count; ++i) {
      TEST_ASSERT_TRUE(m->entries[i].param_id > prev);
      prev = m->entries[i].param_id;
    }
  }
}

// spec 12.4 - the PHY group is READ_ONLY until 12.4.2 is built here.
void test_config_phy_rows_are_read_only() {
  Rig r;
  const schema::ConfigEntry e = set_entry(0x0110, PType::U32, 915000000);
  r.config(53, ConfigOp::Set, &e, 1);
  const schema::NodeConfigAckV1 a = r.next_config_ack();
  TEST_ASSERT_EQUAL(ParamStatus::ReadOnly, a.entries[0].status);
  TEST_ASSERT_EQUAL(PersistStatus::NotApplied, a.persist_status);
}

// The rows the application applies itself land in the engine's context.
void test_config_dedup_depth_reaches_the_gate() {
  Rig r;
  TEST_ASSERT_EQUAL_UINT8(8, r.c.gate.cache_depth());
  const schema::ConfigEntry e[2] = {set_entry(0x0100, PType::U8, 16),
                                    set_entry(0x0101, PType::U16, 9000)};
  r.config(54, ConfigOp::Set, e, 2);
  (void)r.next_config_ack();
  TEST_ASSERT_EQUAL_UINT8(16, r.c.gate.cache_depth());
  TEST_ASSERT_EQUAL_UINT32(9000, r.c.reassembler.timeout_ms());
}

// D52 - defaults back, the file emptied, and every row re-applied.
void test_config_restore_defaults() {
  Rig r;
  const schema::ConfigEntry e = set_entry(kRelayPulseMs, PType::U16, 700);
  r.config(55, ConfigOp::Set, &e, 1);
  (void)r.next_config_ack();
  r.config(56, ConfigOp::RestoreDefaults);
  const schema::NodeConfigAckV1 a = r.next_config_ack();
  TEST_ASSERT_EQUAL(PersistStatus::Persisted, a.persist_status);
  (void)r.next_config_ack();
  TEST_ASSERT_EQUAL_STRING("{}\n", r.card.text.c_str());
  TEST_ASSERT_EQUAL_INT32(500, r.cfg.store().effective(kRelayPulseMs));
}

// spec 7.4 - REQUEST_CONFIG and POLL bit 1 are answered by the unsolicited readback.
void test_request_config_and_poll_bit_one_read_back() {
  Rig r;
  r.command(60, Cmd::RequestConfig);
  assert_result(AckResult::Accepted, r.next_ack());
  TEST_ASSERT_TRUE(r.next_config_ack().more_follows);
  TEST_ASSERT_FALSE(r.next_config_ack().more_follows);

  (void)r.poll_status(kPollFlagConfigReadback);
  TEST_ASSERT_TRUE(r.next_config_ack().more_follows);
  TEST_ASSERT_FALSE(r.next_config_ack().more_follows);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_commands_map_to_relays);
  RUN_TEST(test_actuation_acks_after_the_pulse);
  RUN_TEST(test_immediate_close_is_one_sequence);
  RUN_TEST(test_close_with_a_bad_arg_is_refused_at_once);
  RUN_TEST(test_dry_run_moves_nothing);
  RUN_TEST(test_full_port_answers_busy);
  RUN_TEST(test_ack_drop_withholds_one_fresh_ack);
  RUN_TEST(test_reboot_needs_its_guard);
  RUN_TEST(test_poll_answers_status_with_sentinels);
  RUN_TEST(test_ping_is_echoed);
  RUN_TEST(test_status_carries_the_mppt_snapshot);
  RUN_TEST(test_hex_get_round_trip);
  RUN_TEST(test_unauthenticated_set_never_reaches_the_mppt);
  RUN_TEST(test_authenticated_set_is_forwarded);
  RUN_TEST(test_hex_refused_by_the_port_answers_busy);
  RUN_TEST(test_late_answer_is_discarded);
  RUN_TEST(test_config_set_with_a_card_is_persisted);
  RUN_TEST(test_config_set_with_no_card_says_so);
  RUN_TEST(test_config_set_after_the_card_is_pulled);
  RUN_TEST(test_config_get_all_lists_every_row_once);
  RUN_TEST(test_config_phy_rows_are_read_only);
  RUN_TEST(test_config_dedup_depth_reaches_the_gate);
  RUN_TEST(test_config_restore_defaults);
  RUN_TEST(test_request_config_and_poll_bit_one_read_back);
  return UNITY_END();
}
