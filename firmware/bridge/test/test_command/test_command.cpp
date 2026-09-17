// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-18 - the command path and its retry (Impl Plan 6.2; PRD BS-3; spec 6.2, 6.3, 10.3-10.5).
//
// THE PROPERTY UNDER TEST is root rule 2: a retry reuses the `seq` of the attempt it
// repeats. At the gate, incrementing it is a second relay pulse, and it is the change
// that looks most like a fix for a stuck command. Everything else here - the exhaustion
// budget, the single resync, the dedup result - is what makes that property hold
// without the command path becoming a source of transmit storms.
//
// WHAT THIS CANNOT COVER. sched_task's lock, queue send and registry calls
// (task_runtime.cpp needs FreeRTOS), and a node actually executing. B3b's bench run is
// where a simnode answers these commands.

#include <unity.h>

#include "command.h"
#include "lran/lran.h"
#include "refimpl_mac.h"
#include "registry.h"
#include "test_key.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefKdf g_kdf;
refimpl::RefMac g_mac;

constexpr NodeId kTarget = kNodeGateLink;
constexpr CtxId  kCtx    = 0x11223344u;

CommandRequest open_gate() {
  CommandRequest r;
  r.dst = kTarget;
  r.cmd = static_cast<uint8_t>(Cmd::Open);
  return r;
}

msg::CommandAck ack_of(Seq seq, AckResult result, uint8_t detail = 0) {
  msg::CommandAck a;
  a.ack_seq = seq;
  a.result  = static_cast<uint8_t>(result);
  a.detail  = detail;
  return a;
}

// Drives one Send, as sched_task would: take the step, queue the frame, report back.
CmdStep send_now(CommandPath& c, uint32_t now) {
  const CmdStep st = c.next(now);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdAction::Send), static_cast<int>(st.action));
  c.on_sent(now);
  return st;
}

// The moment the window for `attempt` has closed. The first attempt waits the timeout;
// every retry waits the timeout plus the backoff.
uint32_t after_window(const CommandPath& c, uint32_t opened, uint8_t attempt) {
  return opened + c.ack_timeout_ms() + (attempt > 0 ? kCommandRetryBackoffMs : 0) + 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Root rule 2 / BS-3 - the property this file exists for.
// ---------------------------------------------------------------------------

// Every transmission of one command carries one `seq`. If this test ever fails, the
// gate opens twice.
void test_every_retry_reuses_the_same_seq() {
  CommandPath c;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 42, 0));

  uint32_t  now   = 0;
  const Seq first = send_now(c, now).seq;
  TEST_ASSERT_EQUAL_UINT16(42, first);

  // Three retries after the first attempt, so four transmissions in all.
  for (uint8_t attempt = 0; attempt < kCommandRetriesDefault; ++attempt) {
    now              = after_window(c, now, attempt);
    const CmdStep st = send_now(c, now);
    TEST_ASSERT_EQUAL_UINT16(first, st.seq);
    TEST_ASSERT_EQUAL_UINT8(attempt + 1, st.attempt);
  }

  TEST_ASSERT_EQUAL_UINT32(4, c.stats().sent);
  TEST_ASSERT_EQUAL_UINT32(3, c.stats().retries);
}

// The registry is the only thing that advances a seq, and a retry never asks it for one.
void test_the_registry_advances_cmd_seq_and_a_retry_does_not() {
  Registry r;
  r.load(lran_test::kTestMasterKey, &g_kdf);

  Seq a = 0, b = 0;
  TEST_ASSERT_TRUE(r.take_cmd_seq(kTarget, &a));
  TEST_ASSERT_TRUE(r.take_cmd_seq(kTarget, &b));
  TEST_ASSERT_EQUAL_UINT16(1, a);
  TEST_ASSERT_EQUAL_UINT16(2, b);

  CommandPath c;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, b, 0));
  send_now(c, 0);
  send_now(c, after_window(c, 0, 0));
  // Nothing the command path did touched the registry's counter.
  Seq next = 0;
  TEST_ASSERT_TRUE(r.take_cmd_seq(kTarget, &next));
  TEST_ASSERT_EQUAL_UINT16(3, next);
}

// spec 10.5 - the wrap. 0 is skipped, because 0 is what an entry carries before any
// command has been sent.
void test_cmd_seq_wraps_past_zero() {
  Registry r;
  r.load(lran_test::kTestMasterKey, &g_kdf);

  Seq s = 0;
  for (uint32_t i = 0; i < 0xFFFFu; ++i) TEST_ASSERT_TRUE(r.take_cmd_seq(kTarget, &s));
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, s);
  TEST_ASSERT_TRUE(r.take_cmd_seq(kTarget, &s));
  TEST_ASSERT_EQUAL_UINT16(1, s);  // not 0
}

// ---------------------------------------------------------------------------
// spec 6.2 - the ACK, and the end of a command.
// ---------------------------------------------------------------------------

void test_an_accepted_ack_resolves_the_command() {
  CommandPath c;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 7, 0));
  send_now(c, 0);
  c.on_ack(kTarget, ack_of(7, AckResult::Accepted), kCtx, 100);

  const CmdStep st = c.next(100);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdAction::Resolve), static_cast<int>(st.action));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdOutcome::Acked), static_cast<int>(st.outcome));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckResult::Accepted), st.result);
  TEST_ASSERT_FALSE(c.busy());
  // Resolve is emitted exactly once.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdAction::None),
                        static_cast<int>(c.next(100).action));
}

// spec 6.3 - "a receiver MUST NOT answer a dedup hit with detail = 0". The bridge is the
// reader of that field, and the cached result is what reaches Home Assistant.
void test_duplicate_cached_carries_the_cached_result_in_detail() {
  CommandPath c;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 9, 0));
  send_now(c, 0);
  c.on_ack(kTarget, ack_of(9, AckResult::DuplicateCached,
                           static_cast<uint8_t>(AckResult::Accepted)),
           kCtx, 50);

  const CmdStep st = c.next(50);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdOutcome::Acked), static_cast<int>(st.outcome));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckResult::DuplicateCached), st.result);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(AckResult::Accepted), st.detail);
}

// A node's considered rejection is final. Retrying a REJECTED_ARG spends airtime on an
// answer that will not change.
void test_a_rejection_is_not_retried() {
  CommandPath c;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 3, 0));
  send_now(c, 0);
  c.on_ack(kTarget, ack_of(3, AckResult::RejectedArg), kCtx, 10);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdAction::Resolve),
                        static_cast<int>(c.next(10).action));
  TEST_ASSERT_EQUAL_UINT32(1, c.stats().sent);
}

// spec 6.2 - exhausted. The ACK never came, so nothing is known about execution:
// NoAck is its own outcome and is not reported as a rejection.
void test_exhaustion_resolves_as_no_ack_and_stops() {
  CommandPath c;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 5, 0));

  uint32_t now = 0;
  send_now(c, now);
  for (uint8_t attempt = 0; attempt < kCommandRetriesDefault; ++attempt) {
    now = after_window(c, now, attempt);
    send_now(c, now);
  }

  now              = after_window(c, now, kCommandRetriesDefault);
  const CmdStep st = c.next(now);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdAction::Resolve), static_cast<int>(st.action));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdOutcome::NoAck), static_cast<int>(st.outcome));
  TEST_ASSERT_EQUAL_UINT32(4, c.stats().sent);
  TEST_ASSERT_EQUAL_UINT32(1, c.stats().no_ack);
  TEST_ASSERT_FALSE(c.busy());
}

// ---------------------------------------------------------------------------
// spec 10.3 - the resync, and why it retries exactly once.
// ---------------------------------------------------------------------------

void test_rejected_ctx_adopts_the_acks_context_and_retries_once() {
  CommandPath c;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 77, 0));
  send_now(c, 0);

  constexpr CtxId kNodeCtx = 0xAABBCCDDu;
  c.on_ack(kTarget, ack_of(77, AckResult::RejectedCtx), kNodeCtx, 100);

  const CmdStep st = c.next(100);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdAction::Send), static_cast<int>(st.action));
  TEST_ASSERT_EQUAL_HEX32(kNodeCtx, st.ctx_id);   // spec 10.3 step 2 - adopted
  TEST_ASSERT_EQUAL_UINT16(1, st.seq);            // and the seq space reset
  TEST_ASSERT_TRUE(st.ctx_adopted);               // the caller must write both back
  TEST_ASSERT_EQUAL_UINT32(1, c.stats().resyncs);
}

// spec 10.3 step 3. A resync loop on a shared channel is a transmit storm for every
// other node, so the second REJECTED_CTX stops the command rather than resyncing again.
void test_a_second_rejected_ctx_stops_rather_than_looping() {
  CommandPath c;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 77, 0));
  send_now(c, 0);
  c.on_ack(kTarget, ack_of(77, AckResult::RejectedCtx), 0xAABBCCDDu, 100);

  const CmdStep retry = send_now(c, 100);
  c.on_ack(kTarget, ack_of(retry.seq, AckResult::RejectedCtx), 0xEEFF0011u, 200);

  const CmdStep st = c.next(200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdAction::Resolve), static_cast<int>(st.action));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdOutcome::ResyncFailed),
                        static_cast<int>(st.outcome));
  TEST_ASSERT_EQUAL_UINT32(1, c.stats().resyncs);
  TEST_ASSERT_EQUAL_UINT32(1, c.stats().resync_failed);
  TEST_ASSERT_EQUAL_UINT32(2, c.stats().sent);  // never a third
}

// The resync's retry gets the full budget, because it is the first attempt in a
// sequence space the node will accept.
void test_the_resync_retry_starts_a_fresh_attempt_budget() {
  CommandPath c;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 77, 0));
  uint32_t now = 0;
  send_now(c, now);
  now = after_window(c, now, 0);
  send_now(c, now);  // one retry already spent

  c.on_ack(kTarget, ack_of(77, AckResult::RejectedCtx), 0xAABBCCDDu, now);
  TEST_ASSERT_EQUAL_UINT8(0, c.next(now).attempt);
}

// spec 10.3 step 2 adopts a context; the registry is where it lands, and the next
// command must start there rather than repeating the resync.
void test_adopt_ctx_resets_the_registrys_seq_space() {
  Registry r;
  r.load(lran_test::kTestMasterKey, &g_kdf);

  Seq s = 0;
  r.take_cmd_seq(kTarget, &s);
  r.take_cmd_seq(kTarget, &s);
  TEST_ASSERT_TRUE(r.adopt_ctx(kTarget, 0xDEADBEEFu));

  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, r.state(kTarget)->ctx_id);
  TEST_ASSERT_TRUE(r.take_cmd_seq(kTarget, &s));
  TEST_ASSERT_EQUAL_UINT16(1, s);
  TEST_ASSERT_FALSE(r.adopt_ctx(0x55, 1));  // unregistered
}

// ---------------------------------------------------------------------------
// What the path refuses.
// ---------------------------------------------------------------------------

// One command in flight across the fleet. Refused, not queued: a gate command that
// waits an unbounded time is worse than one refused while the operator is watching.
void test_a_second_command_is_refused_while_one_is_in_flight() {
  CommandPath c;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 1, 0));
  send_now(c, 0);
  TEST_ASSERT_FALSE(c.submit(open_gate(), kCtx, 2, 0));
  TEST_ASSERT_EQUAL_UINT32(1, c.stats().refused_busy);

  c.on_ack(kTarget, ack_of(1, AckResult::Accepted), kCtx, 10);
  c.next(10);  // drain the Resolve
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 2, 20));
}

// A retry reuses one `seq`, so an ack_seq that does not match is late from a previous
// command or not ours. Counted, never acted on - acting on it would resolve the wrong
// command.
void test_an_ack_that_matches_nothing_is_counted_and_ignored() {
  CommandPath c;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 8, 0));
  send_now(c, 0);

  c.on_ack(kTarget, ack_of(7, AckResult::Accepted), kCtx, 10);        // stale seq
  c.on_ack(kNodeWellLink, ack_of(8, AckResult::Accepted), kCtx, 10);  // wrong node
  TEST_ASSERT_EQUAL_UINT32(2, c.stats().ack_ignored);
  TEST_ASSERT_TRUE(c.busy());

  c.on_ack(kTarget, ack_of(8, AckResult::Accepted), kCtx, 10);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdAction::Resolve),
                        static_cast<int>(c.next(10).action));
}

void test_an_ack_arriving_with_nothing_in_flight_is_ignored() {
  CommandPath c;
  c.on_ack(kTarget, ack_of(1, AckResult::Accepted), kCtx, 0);
  TEST_ASSERT_EQUAL_UINT32(1, c.stats().ack_ignored);
  TEST_ASSERT_FALSE(c.busy());
}

// Impl Plan 6.2 step 1. Checked at the bridge so a solar node is not woken to answer
// REJECTED_NOT_SUPPORTED over a link that cost it a transmission.
void test_the_capability_filter_matches_spec_8_1s_split() {
  // Actuation - spec 8.1's 0x00-0x0F. Only GateLink has relays.
  TEST_ASSERT_TRUE(command_allowed(NodeType::GateLink, static_cast<uint8_t>(Cmd::Open)));
  TEST_ASSERT_TRUE(command_allowed(NodeType::GateLink, static_cast<uint8_t>(Cmd::HoldOpen)));
  TEST_ASSERT_FALSE(command_allowed(NodeType::WellLink, static_cast<uint8_t>(Cmd::Open)));

  // Node-local - 0x10+. Every node type answers them.
  TEST_ASSERT_TRUE(
      command_allowed(NodeType::WellLink, static_cast<uint8_t>(Cmd::RequestStatus)));
  TEST_ASSERT_TRUE(command_allowed(NodeType::WellLink, static_cast<uint8_t>(Cmd::Reboot)));

  // Outside spec 8.1's closed table.
  TEST_ASSERT_FALSE(command_allowed(NodeType::GateLink, 0x55));
}

// A bench identity takes a ROLE at runtime and the bridge cannot know from the address
// which one, so refusing an actuation command here would make the bench exercise a
// different code path from the fleet - the thing the bridge's CLAUDE.md says defeats
// having bench nodes. Gating is at publication (spec 16.6), never here.
//
// This test is the one that would have been missing when B3b's bench run could not
// command a simnode at all.
void test_a_simnode_is_allowed_every_command() {
  const uint8_t kEvery[] = {
      static_cast<uint8_t>(Cmd::Nop),           static_cast<uint8_t>(Cmd::Open),
      static_cast<uint8_t>(Cmd::Close),         static_cast<uint8_t>(Cmd::HoldOpen),
      static_cast<uint8_t>(Cmd::ReleaseHold),   static_cast<uint8_t>(Cmd::RequestStatus),
      static_cast<uint8_t>(Cmd::RequestConfig), static_cast<uint8_t>(Cmd::SetDebugMode),
      static_cast<uint8_t>(Cmd::SetRelayDryRun),
      static_cast<uint8_t>(Cmd::SetBmsPolling), static_cast<uint8_t>(Cmd::Reboot),
  };
  for (uint8_t cmd : kEvery) {
    TEST_ASSERT_TRUE_MESSAGE(command_allowed(NodeType::Simnode, cmd), "simnode");
  }
  TEST_ASSERT_FALSE(command_allowed(NodeType::Simnode, 0x55));
}

// ---------------------------------------------------------------------------
// Timing.
// ---------------------------------------------------------------------------

// Root rule 8 - nothing timing-related is fixed at compile time in a node that cannot
// be reflashed without a walk to the gate.
void test_the_timeout_and_retry_count_are_runtime_settable() {
  CommandPath c;
  c.set_ack_timeout_ms(500);
  c.set_retries(1);
  TEST_ASSERT_EQUAL_UINT32(500, c.ack_timeout_ms());

  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 1, 0));
  send_now(c, 0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdAction::None), static_cast<int>(c.next(400).action));

  // The shortened window closed. One retry is all set_retries(1) allows, and its own
  // window carries the backoff on top of the timeout.
  const uint32_t retry_at = after_window(c, 0, 0);
  send_now(c, retry_at);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdAction::None),
                        static_cast<int>(c.next(retry_at + 900).action));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdOutcome::NoAck),
                        static_cast<int>(c.next(after_window(c, retry_at, 1)).outcome));
  TEST_ASSERT_EQUAL_UINT32(2, c.stats().sent);
}

// millis() wraps at ~49.7 days and this node is expected to run for years.
void test_the_window_survives_the_millis_wrap() {
  CommandPath c;
  const uint32_t before = UINT32_MAX - 1000;
  TEST_ASSERT_TRUE(c.submit(open_gate(), kCtx, 1, before));
  send_now(c, before);

  // 500 ms later, across the wrap: the window is still open.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CmdAction::None),
                        static_cast<int>(c.next(before + 500).action));
  // Past the timeout, also across the wrap: a retry, with the same seq.
  TEST_ASSERT_EQUAL_UINT16(1, send_now(c, before + c.ack_timeout_ms() + 1).seq);
}

// ---------------------------------------------------------------------------
// The frame.
// ---------------------------------------------------------------------------

// spec 10.1 - the bridge's frames carry the DESTINATION's ctx_id, and spec 9.2 makes
// COMMAND authenticated. A frame a node cannot verify is a command that never runs.
void test_the_command_frame_is_what_a_node_decodes() {
  uint8_t key[kNodeKeyLen];
  for (size_t i = 0; i < sizeof(key); ++i) key[i] = static_cast<uint8_t>(i);

  EncodeCtx ectx;
  ectx.mac      = &g_mac;
  ectx.node_key = key;

  const msg::Command cmd{static_cast<uint8_t>(Cmd::Close), 1, 0x0203};
  uint8_t            buf[kMaxFrame];
  const size_t       len = build_command_frame(kTarget, kCtx, 99, kProtoVer, cmd, ectx, buf, sizeof(buf));
  TEST_ASSERT_GREATER_THAN_UINT32(0, len);

  // Decoded as the NODE would: its own address, its own context, the bridge's key.
  DecodeCtx dctx;
  dctx.self          = kTarget;
  dctx.mac           = &g_mac;
  dctx.node_key      = key;
  dctx.expect_ctx_id = kCtx;  // spec 9.4 step 2 - a node checks its own ctx_id

  Frame f;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_header(buf, len, dctx, &f)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_payload(buf, len, dctx, &f)));
  TEST_ASSERT_TRUE(f.mac_verified);  // root rule: never test the status alone
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MsgType::Command), static_cast<int>(f.hdr.type));
  TEST_ASSERT_EQUAL_HEX8(kNodeBridge, f.hdr.src);
  TEST_ASSERT_EQUAL_HEX8(kTarget, f.hdr.dst);
  TEST_ASSERT_EQUAL_UINT16(99, f.hdr.seq);
  TEST_ASSERT_EQUAL_HEX32(kCtx, f.hdr.ctx_id);

  msg::Command out;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(msg::deserialize(f.payload, f.payload_len, &out)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cmd::Close), out.cmd);
  TEST_ASSERT_EQUAL_UINT8(1, out.arg);
  TEST_ASSERT_EQUAL_UINT16(0x0203, out.arg2);
}

// An EncodeCtx without a MAC or a key produces NO frame. The failure this prevents is
// an unauthenticated COMMAND on the air, which a node refuses and which reads at the
// bridge as a node fault rather than as the bridge's own misconfiguration.
void test_a_command_without_a_key_is_not_built() {
  const msg::Command cmd{static_cast<uint8_t>(Cmd::Open), 0, 0};
  uint8_t            buf[kMaxFrame];
  TEST_ASSERT_EQUAL_UINT32(
      0, build_command_frame(kTarget, kCtx, 1, kProtoVer, cmd, EncodeCtx{}, buf, sizeof(buf)));

  EncodeCtx no_key;
  no_key.mac = &g_mac;
  TEST_ASSERT_EQUAL_UINT32(
      0, build_command_frame(kTarget, kCtx, 1, kProtoVer, cmd, no_key, buf, sizeof(buf)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_retry_reuses_the_same_seq);
  RUN_TEST(test_the_registry_advances_cmd_seq_and_a_retry_does_not);
  RUN_TEST(test_cmd_seq_wraps_past_zero);
  RUN_TEST(test_an_accepted_ack_resolves_the_command);
  RUN_TEST(test_duplicate_cached_carries_the_cached_result_in_detail);
  RUN_TEST(test_a_rejection_is_not_retried);
  RUN_TEST(test_exhaustion_resolves_as_no_ack_and_stops);
  RUN_TEST(test_rejected_ctx_adopts_the_acks_context_and_retries_once);
  RUN_TEST(test_a_second_rejected_ctx_stops_rather_than_looping);
  RUN_TEST(test_the_resync_retry_starts_a_fresh_attempt_budget);
  RUN_TEST(test_adopt_ctx_resets_the_registrys_seq_space);
  RUN_TEST(test_a_second_command_is_refused_while_one_is_in_flight);
  RUN_TEST(test_an_ack_that_matches_nothing_is_counted_and_ignored);
  RUN_TEST(test_an_ack_arriving_with_nothing_in_flight_is_ignored);
  RUN_TEST(test_the_capability_filter_matches_spec_8_1s_split);
  RUN_TEST(test_a_simnode_is_allowed_every_command);
  RUN_TEST(test_the_timeout_and_retry_count_are_runtime_settable);
  RUN_TEST(test_the_window_survives_the_millis_wrap);
  RUN_TEST(test_the_command_frame_is_what_a_node_decodes);
  RUN_TEST(test_a_command_without_a_key_is_not_built);
  return UNITY_END();
}
