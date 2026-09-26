// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-28, BF-29, BF-30 - the HEX proxy, its write gates and the charge-parameter readback.
// Impl Plan 6.4; PRD 3.5, BS-2, V-B6; spec 7.6, 8.13, 9.2, 10.2, 10.3.
//
// V-B6 asks that each gate be tested on its own. Gate 1 is the MAC: build_hex_req_frame()
// cannot produce a write without one. Gate 2 is WriteArm and HexProxy::next()'s `armed`.
// Gate 3 is `audit` on every write's Resolve and the document it publishes. The node's
// half of gate 1 is the simnode's test_gatelink.

#include <unity.h>

#include <cstring>

#include "charge_readback.h"
#include "hex_proxy.h"
#include "lran/lran.h"
#include "net_policy.h"
#include "refimpl_mac.h"
#include "test_key.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefKdf g_kdf;
refimpl::RefMac g_mac;

HexRequest request(const char* hex, NodeId dst = kNodeSim1) {
  HexRequest r;
  r.dst = dst;
  r.n   = static_cast<uint8_t>(std::strlen(hex));
  std::memcpy(r.hex, hex, r.n);
  return r;
}

msg::HexRsp answer(const char* hex, HexStatus s = HexStatus::Ok) {
  return msg::HexRsp{static_cast<uint8_t>(s), static_cast<uint8_t>(std::strlen(hex)),
                     reinterpret_cast<const uint8_t*>(hex)};
}

// Submits and sends; returns the Send step.
HexStep start(HexProxy& p, const char* hex, bool armed, uint32_t now = 1000, Seq wseq = 5) {
  const bool write = classify_hex(hex, std::strlen(hex)) == HexClass::Write;
  TEST_ASSERT_TRUE(p.submit(request(hex), write, 0x11223344u, wseq, now));
  HexStep st = p.next(now, armed);
  if (st.action == HexAction::Send) p.on_sent(now);
  return st;
}

}  // namespace

// ---- classification ------------------------------------------------------------------

void test_classify_follows_the_command_nibble() {
  TEST_ASSERT_EQUAL(HexClass::Read, classify_hex(":7F0ED0071", 10));
  TEST_ASSERT_EQUAL(HexClass::Write, classify_hex(":8F0ED0064000C", 14));
  TEST_ASSERT_EQUAL(HexClass::Write, classify_hex(":64F", 4));
  TEST_ASSERT_EQUAL(HexClass::Read, classify_hex(":154", 4));
  vedirect::Parse why;
  TEST_ASSERT_EQUAL(HexClass::Malformed, classify_hex(":7F0ED0072", 10, &why));
  TEST_ASSERT_EQUAL(vedirect::Parse::BadChecksum, why);
  TEST_ASSERT_EQUAL(HexClass::Malformed, classify_hex("8F0ED0064000C", 13));
}

// The bridge's class must be the class the library MACs, or gate 1 has a hole.
void test_classify_agrees_with_the_library_mac_decision() {
  const char* cases[] = {":7F0ED0071", ":8F0ED0064000C", ":64F", ":154", ":352", ":451"};
  for (const char* c : cases) {
    const size_t n = std::strlen(c);
    uint8_t      payload[2 + 32];
    payload[0] = 0;
    payload[1] = static_cast<uint8_t>(n);
    std::memcpy(payload + 2, c, n);
    TEST_ASSERT_EQUAL_MESSAGE(hex_req_is_write_class(payload, 2 + n),
                              classify_hex(c, n) == HexClass::Write, c);
  }
}

// ---- gate 1: the MAC ------------------------------------------------------------------

void test_a_write_frame_carries_a_mac_that_verifies() {
  uint8_t key[kNodeKeyLen];
  g_kdf.derive_node_key(lran_test::kTestMasterKey, kNodeSim1, key);
  EncodeCtx e;
  e.mac      = &g_mac;
  e.node_key = key;
  uint8_t buf[kMaxFrame];
  const size_t len = build_hex_req_frame(kNodeSim1, 0xAABBCCDDu, 9, kProtoVer, ":8F0ED0064000C",
                                         14, e, buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN(0, len);

  DecodeCtx d;
  d.self          = kNodeSim1;
  d.mac           = &g_mac;
  d.node_key      = key;
  d.expect_ctx_id = 0xAABBCCDDu;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, len, d, &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, len, d, &f));
  TEST_ASSERT_TRUE(f.mac_verified);
  TEST_ASSERT_EQUAL_HEX8(kHexReqFlagWriteClass, f.payload[0]);
}

void test_a_write_without_key_material_is_not_built() {
  EncodeCtx none;
  uint8_t   buf[kMaxFrame];
  TEST_ASSERT_EQUAL_size_t(0, build_hex_req_frame(kNodeSim1, 1, 1, kProtoVer, ":8F0ED0064000C",
                                                  14, none, buf, sizeof(buf)));
  // A read needs none (spec 9.2).
  TEST_ASSERT_GREATER_THAN(0, build_hex_req_frame(kNodeSim1, 1, 1, kProtoVer, ":7F0ED0071", 10,
                                                  none, buf, sizeof(buf)));
}

// ---- gate 2: the arm ------------------------------------------------------------------

void test_the_arm_defaults_off_and_expires() {
  WriteArm a;
  TEST_ASSERT_FALSE(a.armed(0, 300));
  a.arm(1000);
  TEST_ASSERT_TRUE(a.armed(1000 + 299999, 300));
  TEST_ASSERT_FALSE(a.armed(1000 + 300000, 300));  // refused before expire() runs
  TEST_ASSERT_TRUE(a.shown_armed());
  TEST_ASSERT_TRUE(a.expire(1000 + 300000, 300));   // the switch goes back off, once
  TEST_ASSERT_FALSE(a.expire(1000 + 300001, 300));
  TEST_ASSERT_FALSE(a.shown_armed());
}

void test_a_write_while_disarmed_resolves_refused_without_a_send() {
  HexProxy p;
  const HexStep st = start(p, ":8F0ED0064000C", /*armed=*/false);
  TEST_ASSERT_EQUAL(HexAction::Resolve, st.action);
  TEST_ASSERT_EQUAL(HexOutcome::RefusedDisarmed, st.outcome);
  TEST_ASSERT_TRUE(st.audit);
  TEST_ASSERT_EQUAL_UINT32(0, p.stats().sent);
}

void test_a_read_ignores_the_arm() {
  HexProxy p;
  const HexStep st = start(p, ":7F0ED0071", /*armed=*/false);
  TEST_ASSERT_EQUAL(HexAction::Send, st.action);
  TEST_ASSERT_FALSE(st.write);
}

// An arm that lapses between a write and its resync refuses the resync's frame too.
void test_the_resync_asks_the_arm_again() {
  HexProxy p;
  HexStep  st = start(p, ":8F0ED0064000C", true);
  TEST_ASSERT_EQUAL(HexAction::Send, st.action);
  const msg::CommandAck ctx_ack{st.seq, static_cast<uint8_t>(AckResult::RejectedCtx), 0};
  TEST_ASSERT_TRUE(p.on_ack(kNodeSim1, ctx_ack, 0x99u, 1200));
  st = p.next(1200, /*armed=*/false);
  TEST_ASSERT_EQUAL(HexAction::Resolve, st.action);
  TEST_ASSERT_EQUAL(HexOutcome::RefusedDisarmed, st.outcome);
}

// ---- the state machine -----------------------------------------------------------------

void test_a_write_uses_the_registry_seq_and_resolves_on_its_answer() {
  HexProxy p;
  HexStep  st = start(p, ":8F0ED0064000C", true, 1000, 42);
  TEST_ASSERT_TRUE(st.write);
  TEST_ASSERT_EQUAL_UINT16(42, st.seq);
  TEST_ASSERT_EQUAL_HEX32(0x11223344u, st.ctx_id);
  TEST_ASSERT_FALSE(p.on_rsp(kNodeSim1, 41, answer(":8F0ED0064000C")));  // another seq
  TEST_ASSERT_FALSE(p.on_rsp(kNodeSim2, 42, answer(":8F0ED0064000C")));  // another node
  TEST_ASSERT_TRUE(p.on_rsp(kNodeSim1, 42, answer(":8F0ED0064000C")));
  st = p.next(1100, true);
  TEST_ASSERT_EQUAL(HexOutcome::Answered, st.outcome);
  TEST_ASSERT_TRUE(st.audit);
  TEST_ASSERT_EQUAL_size_t(14, p.response_len());
  TEST_ASSERT_EQUAL_UINT32(2, p.stats().answer_ignored);
}

void test_a_write_is_never_retried_and_resolves_unknown() {
  HexProxy p;
  start(p, ":8F0ED0064000C", true);
  TEST_ASSERT_EQUAL(HexAction::None, p.next(1000 + 2999, true).action);
  const HexStep st = p.next(1000 + 3000, true);
  TEST_ASSERT_EQUAL(HexAction::Resolve, st.action);
  TEST_ASSERT_EQUAL(HexOutcome::Unknown, st.outcome);
  TEST_ASSERT_TRUE(st.audit);
  TEST_ASSERT_EQUAL_UINT32(1, p.stats().sent);
}

void test_a_read_is_retried_under_the_same_seq_then_gives_up() {
  HexProxy p;
  HexStep  first = start(p, ":7F0ED0071", false);
  for (uint8_t i = 1; i <= kHexReadRetriesDefault; ++i) {
    const HexStep st = p.next(1000 + i * 3000u, false);
    TEST_ASSERT_EQUAL(HexAction::Send, st.action);
    TEST_ASSERT_EQUAL_UINT16(first.seq, st.seq);  // root rule 2's habit, harmless here
    TEST_ASSERT_EQUAL_UINT8(i, st.attempt);
    p.on_sent(1000 + i * 3000u);
  }
  const HexStep st = p.next(1000 + (kHexReadRetriesDefault + 1) * 3000u, false);
  TEST_ASSERT_EQUAL(HexOutcome::NoResponse, st.outcome);
  TEST_ASSERT_FALSE(st.audit);  // a read is not a write attempt
}

void test_a_readback_is_not_retried() {
  HexProxy   p;
  HexRequest r = request(":7F0ED0071");
  r.origin     = HexOrigin::Readback;
  TEST_ASSERT_TRUE(p.submit(r, false, 0, 0, 1000));
  TEST_ASSERT_EQUAL(HexAction::Send, p.next(1000, false).action);
  p.on_sent(1000);
  const HexStep st = p.next(1000 + 3000, false);
  TEST_ASSERT_EQUAL(HexOutcome::NoResponse, st.outcome);
  TEST_ASSERT_EQUAL_UINT32(1, p.stats().sent);
}

void test_only_nodes_with_an_mppt_get_the_proxy() {
  TEST_ASSERT_TRUE(hex_allowed(NodeType::GateLink));
  TEST_ASSERT_TRUE(hex_allowed(NodeType::Simnode));
  TEST_ASSERT_FALSE(hex_allowed(NodeType::WellLink));
}

void test_reads_take_their_own_seq_and_writes_do_not_touch_it() {
  HexProxy p;
  HexStep  a = start(p, ":7F0ED0071", false);
  p.on_rsp(kNodeSim1, a.seq, answer(":7F0ED009600DB"));
  (void)p.next(1000, false);
  HexStep b = start(p, ":7F0ED0071", false);
  TEST_ASSERT_EQUAL_UINT16(a.seq + 1, b.seq);
}

void test_a_duplicate_or_stale_answer_to_a_write_is_rejected() {
  HexProxy p;
  HexStep  st = start(p, ":8F0ED0064000C", true);
  const msg::CommandAck dup{st.seq, static_cast<uint8_t>(AckResult::DuplicateCached), 0};
  TEST_ASSERT_FALSE(p.on_ack(kNodeSim1, msg::CommandAck{static_cast<Seq>(st.seq + 1), 0, 0}, 0, 1100));
  TEST_ASSERT_TRUE(p.on_ack(kNodeSim1, dup, 0, 1100));
  st = p.next(1100, true);
  TEST_ASSERT_EQUAL(HexOutcome::Rejected, st.outcome);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckResult::DuplicateCached), st.ack_result);
}

void test_a_read_never_claims_a_command_ack() {
  HexProxy p;
  HexStep  st = start(p, ":7F0ED0071", false);
  TEST_ASSERT_FALSE(p.on_ack(kNodeSim1, msg::CommandAck{st.seq, 0, 0}, 0, 1100));
}

void test_resync_once_then_stop() {
  HexProxy p;
  HexStep  st = start(p, ":8F0ED0064000C", true, 1000, 17);
  const msg::CommandAck ctx1{17, static_cast<uint8_t>(AckResult::RejectedCtx), 0};
  TEST_ASSERT_TRUE(p.on_ack(kNodeSim1, ctx1, 0xCAFEu, 1100));
  st = p.next(1100, true);
  TEST_ASSERT_EQUAL(HexAction::Send, st.action);
  TEST_ASSERT_TRUE(st.ctx_adopted);
  TEST_ASSERT_EQUAL_UINT16(1, st.seq);  // spec 10.3 step 2
  TEST_ASSERT_EQUAL_HEX32(0xCAFEu, st.ctx_id);
  p.on_sent(1100);
  const msg::CommandAck ctx2{1, static_cast<uint8_t>(AckResult::RejectedCtx), 0};
  TEST_ASSERT_TRUE(p.on_ack(kNodeSim1, ctx2, 0xBEEFu, 1200));
  st = p.next(1200, true);
  TEST_ASSERT_EQUAL(HexOutcome::ResyncFailed, st.outcome);
}

// spec 10.7, D70 - a Restart refused for its context may already have restarted the MPPT
// before the node reset. It is not resent; the context is adopted for the next request.
void test_a_restart_refused_for_its_context_is_not_resent() {
  HexProxy p;
  HexStep  st = start(p, ":64F", true, 1000, 17);
  TEST_ASSERT_TRUE(st.write);
  const msg::CommandAck ctx1{17, static_cast<uint8_t>(AckResult::RejectedCtx), 0};
  TEST_ASSERT_TRUE(p.on_ack(kNodeSim1, ctx1, 0xCAFEu, 1100));
  st = p.next(1100, true);
  TEST_ASSERT_EQUAL(HexAction::Resolve, st.action);
  TEST_ASSERT_EQUAL(HexOutcome::Unknown, st.outcome);
  TEST_ASSERT_TRUE(st.ctx_adopted);
  TEST_ASSERT_EQUAL_HEX32(0xCAFEu, st.ctx_id);
  TEST_ASSERT_EQUAL_UINT32(1, p.stats().sent);
  TEST_ASSERT_EQUAL_UINT32(0, p.stats().resyncs);
}

void test_one_transaction_at_a_time() {
  HexProxy p;
  start(p, ":7F0ED0071", false);
  TEST_ASSERT_FALSE(p.submit(request(":7F0ED0071"), false, 0, 0, 1000));
  TEST_ASSERT_EQUAL_UINT32(1, p.stats().refused_busy);
}

// ---- gate 3 and the documents -----------------------------------------------------------

void test_the_response_document() {
  HexProxy p;
  HexStep  st = start(p, ":7F0ED0071", false);
  p.on_rsp(kNodeSim1, st.seq, answer(":7F0ED009600DB"));
  st = p.next(1000, false);
  char buf[512];
  TEST_ASSERT_GREATER_THAN(0, hex_response_json(p.request(), st, p.response(),
                                                p.response_len(), buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("{\"request\":\":7F0ED0071\",\"seq\":1,\"outcome\":\"answered\","
                           "\"status\":\"ok\",\"response\":\":7F0ED009600DB\"}",
                           buf);
}

void test_the_audit_names_authorization_and_time() {
  HexProxy p;
  HexStep  st = start(p, ":8F0ED0064000C", false);
  char     buf[512];
  TEST_ASSERT_GREATER_THAN(0, hex_audit_json(p.request(), st, nullptr, 0, 0, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("{\"request\":\":8F0ED0064000C\",\"seq\":5,\"outcome\":"
                           "\"refused_disarmed\",\"status\":null,\"response\":null,"
                           "\"authorization\":\"disarmed\",\"at\":null}",
                           buf);
  TEST_ASSERT_GREATER_THAN(0, hex_audit_json(p.request(), st, nullptr, 0, 1790000000, buf,
                                             sizeof(buf)));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "\"at\":\"2026-"));
}

void test_a_refusal_quotes_a_hostile_request_safely() {
  const HexRequest r = request("\"}bad");
  char             buf[256];
  TEST_ASSERT_GREATER_THAN(0, hex_refusal_json(r, "malformed", buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("{\"request\":\"\\\"}bad\",\"seq\":null,\"outcome\":\"malformed\","
                           "\"status\":null,\"response\":null}",
                           buf);
}

void test_status_tokens_are_spec_8_13_names() {
  TEST_ASSERT_EQUAL_STRING("rejected_unauthenticated", hex_status_token(0x02));
  TEST_ASSERT_EQUAL_STRING("timeout", hex_status_token(0x01));
  TEST_ASSERT_EQUAL_STRING("unknown", hex_status_token(0x77));
}

// ---- the topics ---------------------------------------------------------------------------

void test_vedirect_topics_parse_exactly() {
  VedirectTopic t;
  TEST_ASSERT_TRUE(parse_vedirect_topic("lran/simnode1/vedirect/hex/request", &t));
  TEST_ASSERT_EQUAL_HEX8(kNodeSim1, t.node_id);
  TEST_ASSERT_EQUAL(VedirectInbound::HexRequest, t.kind);
  TEST_ASSERT_TRUE(parse_vedirect_topic("lran/gatelink/vedirect/write_enable/set", &t));
  TEST_ASSERT_EQUAL(VedirectInbound::WriteEnableSet, t.kind);
  TEST_ASSERT_FALSE(parse_vedirect_topic("lran/gatelink/vedirect/write_enable/state", &t));
  TEST_ASSERT_FALSE(parse_vedirect_topic("lran/gatelink/vedirect/hex/request/x", &t));
  TEST_ASSERT_FALSE(parse_vedirect_topic("lran/bridge/vedirect/hex/request", &t));
  TEST_ASSERT_FALSE(parse_vedirect_topic("lran/gatelink/cmd/hex/request", &t));
  char topic[64];
  TEST_ASSERT_GREATER_THAN(0, topic_vedirect("gatelink", "hex/audit", topic, sizeof(topic)));
  TEST_ASSERT_EQUAL_STRING("lran/gatelink/vedirect/hex/audit", topic);
}

// ---- BF-30: the charge-parameter readback ------------------------------------------------

namespace {
size_t get_reply(uint16_t reg, uint32_t value, uint8_t width, uint8_t flags, char* out) {
  uint8_t d[7] = {static_cast<uint8_t>(reg & 0xFF), static_cast<uint8_t>(reg >> 8), flags};
  for (uint8_t i = 0; i < width; ++i) d[3 + i] = static_cast<uint8_t>(value >> (8 * i));
  return vedirect::encode(static_cast<uint8_t>(vedirect::HexRsp::Get), d, 3u + width, out,
                          vedirect::kMaxChars);
}
}  // namespace

void test_readback_walks_the_table_once() {
  ChargeReadback r;
  TEST_ASSERT_NULL(r.next());
  r.request_all();
  char buf[vedirect::kMaxChars];
  size_t count = 0;
  while (const ChargeRegister* reg = r.next()) {
    const size_t n = get_reply(reg->id, 1, reg->width, 0, buf);
    r.on_answer(reg->id, buf, n);
    ++count;
  }
  TEST_ASSERT_EQUAL_size_t(kChargeRegisterCount, count);
  TEST_ASSERT_FALSE(r.pending());
}

void test_readback_scales_and_signs_as_victron_writes_them() {
  ChargeReadback r;
  r.request_all();
  char buf[vedirect::kMaxChars];
  r.on_answer(0xEDF7, buf, get_reply(0xEDF7, 1420, 2, 0, buf));                        // 14.20 V
  r.on_answer(0xEDF2, buf, get_reply(0xEDF2, static_cast<uint16_t>(-1620), 2, 0, buf)); // -16.20
  r.on_answer(0xEDF0, buf, get_reply(0xEDF0, 150, 2, 0, buf));                          // 15.0 A
  r.on_answer(0xEDF1, buf, get_reply(0xEDF1, 0xFF, 1, 0, buf));                         // user
  char doc[512];
  TEST_ASSERT_GREATER_THAN(0, r.json(doc, sizeof(doc)));
  TEST_ASSERT_NOT_NULL(std::strstr(doc, "\"charge_absorption_voltage_v\":14.20"));
  TEST_ASSERT_NOT_NULL(std::strstr(doc, "\"charge_temp_compensation_mv_k\":-16.20"));
  TEST_ASSERT_NOT_NULL(std::strstr(doc, "\"charge_max_current_a\":15.0"));
  TEST_ASSERT_NOT_NULL(std::strstr(doc, "\"charge_battery_type\":255"));
  TEST_ASSERT_NOT_NULL(std::strstr(doc, "\"charge_float_voltage_v\":null"));  // not read: null, not 0
}

// A flag, a wrong width, another register's id or no answer at all is null.
void test_readback_refuses_what_it_cannot_trust() {
  ChargeReadback r;
  r.request_all();
  char buf[vedirect::kMaxChars];
  r.on_answer(0xEDF7, buf, get_reply(0xEDF7, 1420, 2, 0, buf));
  TEST_ASSERT_TRUE(r.have(0));
  TEST_ASSERT_TRUE(r.on_answer(0xEDF7, buf, get_reply(0xEDF7, 0, 0, vedirect::kFlagUnknownId, buf)));
  TEST_ASSERT_FALSE(r.have(0));
  r.on_answer(0xEDF7, buf, get_reply(0xEDF7, 14, 1, 0, buf));  // wrong width
  TEST_ASSERT_FALSE(r.have(0));
  r.on_answer(0xEDF7, buf, get_reply(0xEDF6, 1350, 2, 0, buf));  // another register
  TEST_ASSERT_FALSE(r.have(0));
  r.on_answer(0xEDF7, nullptr, 0);
  TEST_ASSERT_FALSE(r.have(0));
}

void test_readback_reports_a_change_only_when_the_value_moves() {
  ChargeReadback r;
  char buf[vedirect::kMaxChars];
  TEST_ASSERT_TRUE(r.on_answer(0xEDF6, buf, get_reply(0xEDF6, 1350, 2, 0, buf)));
  TEST_ASSERT_FALSE(r.on_answer(0xEDF6, buf, get_reply(0xEDF6, 1350, 2, 0, buf)));
  TEST_ASSERT_TRUE(r.on_answer(0xEDF6, buf, get_reply(0xEDF6, 1360, 2, 0, buf)));
}

void test_readback_abandon_stops_the_pass() {
  ChargeReadback r;
  r.request_all();
  r.abandon();
  TEST_ASSERT_NULL(r.next());
}

// The response window counts from when the request left lora_task. For a write, which is
// never retried, a window closed early would report `unknown` for a write that happened.
void test_the_window_counts_from_when_the_request_aired() {
  HexProxy p;
  (void)start(p, ":7F0ED0071", false, 0);
  p.on_aired(2500);
  TEST_ASSERT_EQUAL(HexAction::None, p.next(p.rsp_timeout_ms(), false).action);
  TEST_ASSERT_EQUAL(HexAction::Send, p.next(2500 + p.rsp_timeout_ms(), false).action);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_classify_follows_the_command_nibble);
  RUN_TEST(test_classify_agrees_with_the_library_mac_decision);
  RUN_TEST(test_a_write_frame_carries_a_mac_that_verifies);
  RUN_TEST(test_a_write_without_key_material_is_not_built);
  RUN_TEST(test_the_arm_defaults_off_and_expires);
  RUN_TEST(test_a_write_while_disarmed_resolves_refused_without_a_send);
  RUN_TEST(test_a_read_ignores_the_arm);
  RUN_TEST(test_the_resync_asks_the_arm_again);
  RUN_TEST(test_a_write_uses_the_registry_seq_and_resolves_on_its_answer);
  RUN_TEST(test_a_write_is_never_retried_and_resolves_unknown);
  RUN_TEST(test_a_read_is_retried_under_the_same_seq_then_gives_up);
  RUN_TEST(test_a_readback_is_not_retried);
  RUN_TEST(test_only_nodes_with_an_mppt_get_the_proxy);
  RUN_TEST(test_reads_take_their_own_seq_and_writes_do_not_touch_it);
  RUN_TEST(test_a_duplicate_or_stale_answer_to_a_write_is_rejected);
  RUN_TEST(test_a_read_never_claims_a_command_ack);
  RUN_TEST(test_resync_once_then_stop);
  RUN_TEST(test_a_restart_refused_for_its_context_is_not_resent);
  RUN_TEST(test_one_transaction_at_a_time);
  RUN_TEST(test_the_response_document);
  RUN_TEST(test_the_audit_names_authorization_and_time);
  RUN_TEST(test_a_refusal_quotes_a_hostile_request_safely);
  RUN_TEST(test_status_tokens_are_spec_8_13_names);
  RUN_TEST(test_vedirect_topics_parse_exactly);
  RUN_TEST(test_readback_walks_the_table_once);
  RUN_TEST(test_readback_scales_and_signs_as_victron_writes_them);
  RUN_TEST(test_readback_refuses_what_it_cannot_trust);
  RUN_TEST(test_readback_reports_a_change_only_when_the_value_moves);
  RUN_TEST(test_readback_abandon_stops_the_pass);
  RUN_TEST(test_the_window_counts_from_when_the_request_aired);
  return UNITY_END();
}
