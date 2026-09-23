// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// P8 - CommandGate. Spec 9.4 steps 4-6, 10.4, 14.1. D34 as amended 2026-09-11.
//
// The failure these tests exist for is a second relay pulse at a driveway gate (root
// rule 2). test_retry_inside_the_window_never_executes is the one that matters most:
// it is the falsifier the original single-threaded-receiver precondition never had,
// and it fails against the API the library plan first specified.

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "lran/lran.h"

using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

// Moves a gate's high-water mark to `h` through accepted commands, in steps well
// inside the half-space so each is newer than the last. Always takes at least one
// step, so `h` itself is left in the cache as a recorded entry.
void advance_to(CommandGate& g, Seq h) {
  uint32_t remaining = static_cast<uint16_t>(h - g.high_water());
  if (remaining == 0) remaining = 0x10000;
  Seq cur = g.high_water();
  while (remaining > 0) {
    const uint32_t step = remaining > 0x4000 ? 0x4000 : remaining;
    cur = static_cast<Seq>(cur + step);
    remaining -= step;
    TEST_ASSERT_EQUAL(Verdict::Execute, g.check(cur).verdict);
    TEST_ASSERT_TRUE(g.record(cur, AckResult::Accepted, 0));
  }
  TEST_ASSERT_EQUAL_HEX16(h, g.high_water());
}

// Accept and record `seq` in one motion - a command that executed synchronously.
void run(CommandGate& g, Seq seq, AckResult result = AckResult::Accepted,
         uint8_t detail = 0) {
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(seq).verdict);
  TEST_ASSERT_TRUE(g.record(seq, result, detail));
}

}  // namespace

// --- spec 9.4 steps 4-6 -------------------------------------------------------

void test_first_command_executes() {
  Counters c;
  CommandGate g(&c);
  g.reset_context(0x12345678u);
  // spec 10.2 - the bridge's command seq starts at 1 in a new context.
  const GateResult r = g.check(1);
  TEST_ASSERT_EQUAL(Verdict::Execute, r.verdict);
  TEST_ASSERT_EQUAL(Status::Ok, r.status);
  TEST_ASSERT_EQUAL_HEX16(1, g.high_water());
  TEST_ASSERT_EQUAL_HEX32(0x12345678u, g.ctx_id());
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_dup_command);
}

// The one that matters. D34's original form advanced the mark in record(); a retry
// arriving between check() and record() - which GateLink's own task split makes
// routine, lora_task receiving while io_task pulses - then found no cached entry AND
// an unmoved mark, and was answered Execute. That is a second relay pulse.
void test_retry_inside_the_window_never_executes() {
  Counters c;
  CommandGate g(&c);
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(7).verdict);

  // The mark has moved before any execution happened (spec 9.4 step 6's order).
  TEST_ASSERT_EQUAL_HEX16(7, g.high_water());

  // Three bridge retries while the first copy is still executing.
  for (int i = 0; i < 3; ++i) {
    const GateResult r = g.check(7);
    TEST_ASSERT_EQUAL(Verdict::InFlight, r.verdict);
    TEST_ASSERT_EQUAL(Status::DuplicateInFlight, r.status);
  }
  // Counted as the retry mechanism working (spec 14.1), and not as a drop.
  TEST_ASSERT_EQUAL_UINT32(3, c.rx_dup_command);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_rejected_seq);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());

  // Execution finishes. The next retry receives the real result.
  TEST_ASSERT_TRUE(g.record(7, AckResult::Accepted, 0x2A));
  const GateResult r = g.check(7);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, r.verdict);
  TEST_ASSERT_EQUAL(Status::DuplicateCached, r.status);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.cached_result);
  TEST_ASSERT_EQUAL_HEX8(0x2A, r.cached_detail);
  TEST_ASSERT_EQUAL_UINT32(4, c.rx_dup_command);
}

// Two commands can be in flight at once; each is tracked on its own.
void test_two_commands_in_flight_independently() {
  CommandGate g;
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(1).verdict);
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(2).verdict);
  TEST_ASSERT_EQUAL(Verdict::InFlight, g.check(1).verdict);
  TEST_ASSERT_EQUAL(Verdict::InFlight, g.check(2).verdict);
  TEST_ASSERT_TRUE(g.record(2, AckResult::DryRun, 0));
  TEST_ASSERT_EQUAL(Verdict::InFlight, g.check(1).verdict);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, g.check(2).verdict);
  TEST_ASSERT_TRUE(g.record(1, AckResult::ActuatorBusy, 0));
  const GateResult r = g.check(1);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, r.verdict);
  TEST_ASSERT_EQUAL(AckResult::ActuatorBusy, r.cached_result);
}

// Step 4 BEFORE step 5. A retry carries seq == high_water, which step 5 refuses; a
// gate checking seq first answers REJECTED_SEQ where spec 10.4 requires the cached
// ACK. Reversing the two checks in check() fails this test.
void test_dedup_is_checked_before_seq() {
  Counters c;
  CommandGate g(&c);
  run(g, 5, AckResult::Accepted, 0x11);
  const GateResult r = g.check(5);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, r.verdict);
  TEST_ASSERT_EQUAL(Status::DuplicateCached, r.status);
  TEST_ASSERT_EQUAL_HEX8(0x11, r.cached_detail);
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_dup_command);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_rejected_seq);
}

// A cached ACK is returned every time, never re-executed.
void test_cached_ack_returned_without_reexecution() {
  CommandGate g;
  run(g, 10);
  for (int i = 0; i < 50; ++i) {
    TEST_ASSERT_EQUAL(Verdict::ReturnCached, g.check(10).verdict);
  }
  TEST_ASSERT_FALSE(g.record(10, AckResult::Accepted, 0));  // nothing in flight
}

// A seq at or below the mark with no cache entry is a replay, refused at step 5.
void test_seq_below_mark_refused() {
  Counters c;
  CommandGate g(&c);
  run(g, 100);
  const GateResult r = g.check(99);
  TEST_ASSERT_EQUAL(Verdict::Reject, r.verdict);
  TEST_ASSERT_EQUAL(Status::RejectedSeq, r.status);
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(0).verdict);
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(static_cast<Seq>(100 + 0x8000)).verdict);
  TEST_ASSERT_EQUAL_HEX16(100, g.high_water());  // a refusal moves nothing
  // spec 14.1 - rx_rejected_seq is a drop; rx_dup_command is not.
  TEST_ASSERT_EQUAL_UINT32(3, c.rx_rejected_seq);
  TEST_ASSERT_EQUAL_UINT32(3, c.total_dropped());
}

// The mark advances in check(), so a seq is consumed whether or not its execution
// succeeds, and the failure is cached as its answer. seq is attacker-visible and
// must not be reusable, and a retry must not get a second attempt at a command the
// node refused.
void test_failed_execution_is_cached() {
  CommandGate g;
  run(g, 3, AckResult::RejectedArg, 0x07);
  const GateResult r = g.check(3);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, r.verdict);
  TEST_ASSERT_EQUAL(AckResult::RejectedArg, r.cached_result);
  TEST_ASSERT_EQUAL_HEX8(0x07, r.cached_detail);
  // A newer seq still executes: the failure consumed 3 and nothing else.
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(4).verdict);
}

// --- record() refusals --------------------------------------------------------

void test_record_refuses_without_an_in_flight_entry() {
  CommandGate g;
  TEST_ASSERT_FALSE(g.record(1, AckResult::Accepted, 0));  // never checked
  run(g, 1, AckResult::Accepted, 0x01);
  // A second record must not overwrite the result a retry has already been told.
  TEST_ASSERT_FALSE(g.record(1, AckResult::RejectedUnsafe, 0x02));
  const GateResult r = g.check(1);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.cached_result);
  TEST_ASSERT_EQUAL_HEX8(0x01, r.cached_detail);
}

// An in-flight entry evicted before its execution finished. Its retry falls to step
// 5 and is refused - never executed - which is the high-water mark doing the job the
// cache could not. record() reports the loss so the caller can log it.
void test_evicted_in_flight_entry_is_never_reexecuted() {
  Counters c;
  CommandGate g(&c);
  g.set_cache_depth(1);
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(1).verdict);
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(2).verdict);  // evicts 1, in flight
  TEST_ASSERT_FALSE(g.record(1, AckResult::Accepted, 0));
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(1).verdict);
  TEST_ASSERT_EQUAL(Verdict::InFlight, g.check(2).verdict);
  TEST_ASSERT_TRUE(g.record(2, AckResult::Accepted, 0));
}

// --- spec 10.4: depth ---------------------------------------------------------

void test_default_depth_is_eight() {
  CommandGate g;
  TEST_ASSERT_EQUAL_UINT8(8, g.cache_depth());
  TEST_ASSERT_EQUAL_UINT8(kDefaultDedupCacheDepth, g.cache_depth());
  for (Seq s = 1; s <= 9; ++s) run(g, s);
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(1).verdict);  // ninth evicted the first
  for (Seq s = 2; s <= 9; ++s) {
    TEST_ASSERT_EQUAL(Verdict::ReturnCached, g.check(s).verdict);
  }
}

void test_depth_clamped_to_range() {
  CommandGate g;
  g.set_cache_depth(0);
  TEST_ASSERT_EQUAL_UINT8(1, g.cache_depth());
  g.set_cache_depth(200);
  TEST_ASSERT_EQUAL_UINT8(kDedupCacheCapacity, g.cache_depth());
  g.set_cache_depth(32);
  TEST_ASSERT_EQUAL_UINT8(32, g.cache_depth());
}

// Runtime changes evict oldest-first and never read beyond capacity (root rule 8:
// dedup_cache_depth is a node parameter, set over the air).
void test_depth_changes_at_runtime() {
  CommandGate g;
  g.set_cache_depth(4);
  for (Seq s = 1; s <= 6; ++s) run(g, s, AckResult::Accepted, static_cast<uint8_t>(s));
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(1).verdict);
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(2).verdict);
  for (Seq s = 3; s <= 6; ++s) {
    const GateResult r = g.check(s);
    TEST_ASSERT_EQUAL(Verdict::ReturnCached, r.verdict);
    TEST_ASSERT_EQUAL_HEX8(s, r.cached_detail);
  }

  // Shrink: the oldest two go.
  g.set_cache_depth(2);
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(3).verdict);
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(4).verdict);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, g.check(5).verdict);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, g.check(6).verdict);

  // Grow to capacity and push well past it, so the ring wraps its storage more than
  // once. Exactly the last 32 survive, each with its own result.
  g.set_cache_depth(32);
  for (Seq s = 7; s <= 100; ++s) run(g, s, AckResult::Accepted, static_cast<uint8_t>(s));
  for (Seq s = 1; s <= 68; ++s) {
    TEST_ASSERT_EQUAL(Verdict::Reject, g.check(s).verdict);
  }
  for (Seq s = 69; s <= 100; ++s) {
    const GateResult r = g.check(s);
    TEST_ASSERT_EQUAL(Verdict::ReturnCached, r.verdict);
    TEST_ASSERT_EQUAL_HEX8(s, r.cached_detail);
  }
}

// Shrinking past an in-flight entry drops it like any other; the mark still refuses
// its retry.
void test_shrink_evicts_in_flight_entry_safely() {
  CommandGate g;
  g.set_cache_depth(4);
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(1).verdict);  // in flight, oldest
  run(g, 2);
  run(g, 3);
  g.set_cache_depth(2);
  TEST_ASSERT_FALSE(g.record(1, AckResult::Accepted, 0));
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(1).verdict);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, g.check(3).verdict);
}

// --- spec 10.1, 10.3: context -------------------------------------------------

void test_reset_context_clears_cache_and_mark() {
  CommandGate g;
  g.reset_context(0xAAAAAAAAu);
  run(g, 1);
  run(g, 2);
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(3).verdict);  // left in flight

  // The node rebooted: new context, and the bridge restarts its seq at 1.
  g.reset_context(0xBBBBBBBBu);
  TEST_ASSERT_EQUAL_HEX32(0xBBBBBBBBu, g.ctx_id());
  TEST_ASSERT_EQUAL_HEX16(0, g.high_water());
  TEST_ASSERT_FALSE(g.record(3, AckResult::Accepted, 0));  // in-flight entry gone
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(1).verdict);  // not ReturnCached
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(2).verdict);
}

// Depth is configuration, not context state: a reboot of the PEER does not reset it.
void test_reset_context_keeps_depth() {
  CommandGate g;
  g.set_cache_depth(3);
  g.reset_context(1);
  TEST_ASSERT_EQUAL_UINT8(3, g.cache_depth());
}

// --- spec 10.5: wrap ------------------------------------------------------------

void test_commands_continue_across_the_wrap() {
  CommandGate g;
  advance_to(g, 0xFFFE);
  run(g, 0xFFFF);
  run(g, 0x0000);
  run(g, 0x0001);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, g.check(0xFFFF).verdict);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, g.check(0x0000).verdict);
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(0xFFFD).verdict);
}

// Exhaustive, as P5: for each mark near the wrap and at the half-space boundary,
// every one of the 65,536 candidate seqs gets the verdict its distance from the mark
// dictates. The expectation is written from the distance directly rather than by
// calling seq_newer, so the test does not share the code it checks.
void test_seq_exhaustive_near_the_wrap() {
  static const Seq kExtra[] = {0x7FFF, 0x8000, 0x8001};
  Seq marks[33 + 3];
  size_t n = 0;
  for (uint32_t i = 0; i < 33; ++i) marks[n++] = static_cast<Seq>(0xFFF0 + i);
  for (Seq e : kExtra) marks[n++] = e;

  for (size_t m = 0; m < n; ++m) {
    const Seq h = marks[m];
    CommandGate base;
    base.set_cache_depth(1);  // only h itself is cached
    advance_to(base, h);
    for (uint32_t s = 0; s <= 0xFFFF; ++s) {
      CommandGate g = base;
      const Verdict got = g.check(static_cast<Seq>(s)).verdict;
      const uint16_t d = static_cast<uint16_t>(s - h);
      const Verdict want = (d == 0)     ? Verdict::ReturnCached
                           : (d < 0x8000) ? Verdict::Execute
                                          : Verdict::Reject;
      if (got != want) {
        char msg[64];
        snprintf(msg, sizeof(msg), "mark 0x%04X seq 0x%04X", h, static_cast<unsigned>(s));
        TEST_FAIL_MESSAGE(msg);
      }
    }
  }
}

// --- spec 14.1: counters --------------------------------------------------------

void test_counters_optional() {
  CommandGate g;  // no Counters: every path still works
  run(g, 1);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, g.check(1).verdict);
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(0).verdict);
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(2).verdict);
  TEST_ASSERT_EQUAL(Verdict::InFlight, g.check(2).verdict);
}

// Exactly one counter moves per non-Execute verdict, and Execute moves none.
void test_each_verdict_moves_exactly_its_counter() {
  Counters c;
  CommandGate g(&c);

  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(1).verdict);
  for (const CounterField& f : kCounterRegistry) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, c.*(f.field), f.name);
  }
  g.check(1);  // in flight
  g.record(1, AckResult::Accepted, 0);
  g.check(1);  // cached
  g.check(0);  // stale
  for (const CounterField& f : kCounterRegistry) {
    uint32_t want = 0;
    if (strcmp(f.name, "rx_dup_command") == 0) want = 2;
    if (strcmp(f.name, "rx_rejected_seq") == 0) want = 1;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(want, c.*(f.field), f.name);
  }
  TEST_ASSERT_EQUAL_UINT32(1, c.total_dropped());
}

// The brief and the plan state 128 B of cache per peer. The entry is the part that
// scales with capacity; the whole object is reported so the footprint can be recorded
// from the target's own compiler.
// spec 10.6 - the question a node asks before a ROLL_CONTEXT. True from check() to
// record(), and false again once every executing command has a result.
void test_any_in_flight_tracks_check_and_record() {
  CommandGate g;
  g.reset_context(0x11111111u);
  TEST_ASSERT_FALSE(g.any_in_flight());
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(1).verdict);
  TEST_ASSERT_TRUE(g.any_in_flight());
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(2).verdict);
  TEST_ASSERT_TRUE(g.record(1, AckResult::Accepted, 0));
  TEST_ASSERT_TRUE(g.any_in_flight());  // seq 2 still executing
  TEST_ASSERT_TRUE(g.record(2, AckResult::Accepted, 0));
  TEST_ASSERT_FALSE(g.any_in_flight());
}

// An in-flight entry pushed out of the cache is no longer in flight as far as a roll is
// concerned: its retry already fails at step 5, so it has no result for a roll to lose.
void test_any_in_flight_forgets_an_evicted_entry() {
  CommandGate g;
  g.reset_context(0x11111111u);
  g.set_cache_depth(1);
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(1).verdict);
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(2).verdict);  // evicts seq 1, in flight
  TEST_ASSERT_TRUE(g.record(2, AckResult::Accepted, 0));
  TEST_ASSERT_FALSE(g.any_in_flight());
}

// spec 10.6 node step 2 - the roll empties the cache and the mark. A seq the old context
// had cached executes again in the new one, which is what lets the restarted bridge's
// seq 1 through.
void test_roll_resets_cache_and_mark() {
  CommandGate g;
  g.reset_context(0x11111111u);
  advance_to(g, 40);
  TEST_ASSERT_EQUAL(Verdict::ReturnCached, g.check(40).verdict);
  TEST_ASSERT_EQUAL(Verdict::Reject, g.check(1).verdict);

  g.reset_context(0x22222222u);
  TEST_ASSERT_EQUAL_HEX32(0x22222222u, g.ctx_id());
  TEST_ASSERT_EQUAL_UINT16(0, g.high_water());
  TEST_ASSERT_FALSE(g.any_in_flight());
  TEST_ASSERT_EQUAL(Verdict::Execute, g.check(1).verdict);
}

void test_report_footprint() {
  char msg[96];
  snprintf(msg, sizeof(msg), "sizeof(CommandGate) = %u B, capacity %u",
           static_cast<unsigned>(sizeof(CommandGate)),
           static_cast<unsigned>(kDedupCacheCapacity));
  TEST_MESSAGE(msg);
  TEST_ASSERT_TRUE(sizeof(CommandGate) >= 128);
  TEST_ASSERT_TRUE(sizeof(CommandGate) <= 160);
}

int run_all() {
  UNITY_BEGIN();
  RUN_TEST(test_first_command_executes);
  RUN_TEST(test_retry_inside_the_window_never_executes);
  RUN_TEST(test_two_commands_in_flight_independently);
  RUN_TEST(test_dedup_is_checked_before_seq);
  RUN_TEST(test_cached_ack_returned_without_reexecution);
  RUN_TEST(test_seq_below_mark_refused);
  RUN_TEST(test_failed_execution_is_cached);
  RUN_TEST(test_record_refuses_without_an_in_flight_entry);
  RUN_TEST(test_evicted_in_flight_entry_is_never_reexecuted);
  RUN_TEST(test_default_depth_is_eight);
  RUN_TEST(test_depth_clamped_to_range);
  RUN_TEST(test_depth_changes_at_runtime);
  RUN_TEST(test_shrink_evicts_in_flight_entry_safely);
  RUN_TEST(test_reset_context_clears_cache_and_mark);
  RUN_TEST(test_reset_context_keeps_depth);
  RUN_TEST(test_commands_continue_across_the_wrap);
  RUN_TEST(test_seq_exhaustive_near_the_wrap);
  RUN_TEST(test_counters_optional);
  RUN_TEST(test_each_verdict_moves_exactly_its_counter);
  RUN_TEST(test_any_in_flight_tracks_check_and_record);
  RUN_TEST(test_any_in_flight_forgets_an_evicted_entry);
  RUN_TEST(test_roll_resets_cache_and_mark);
  RUN_TEST(test_report_footprint);
  return UNITY_END();
}

// PlatformIO runs the same suites on the host and on the ESP32-S3. The host entry
// point is main(); Arduino's is setup()/loop().
#ifdef ARDUINO
void setup() {
  // The USB-serial link needs a moment before the first report, or the opening
  // lines are lost and a passing run looks like a hang.
  delay(2000);
  run_all();
}
void loop() {}
#else
int main() { return run_all(); }
#endif
