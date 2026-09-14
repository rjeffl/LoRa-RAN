// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-8 - the fault catalogue. Impl Plan 10.5, 10.5.1.
//
// THE ASSERTION IS THE POINT. Each fault claims one spec 14 counter (its catalogue row), and
// this suite feeds the fault's frames into the codec's own receive ladder - the same
// decode_header, decode_payload and Reassembler the bridge runs - and checks that exactly
// that counter moves. A fault whose frame is malformed the wrong way lands on a different
// stage, and the wrong counter fails the test. Without this, every discard counter in the
// bridge ships tested only against prose.
//
// WHAT THIS CANNOT COVER: that the frame reaches the air (radio.cpp needs a board), and
// bad_phy_crc, which the SX1262 makes unproducible (Impl Plan 10.5.2). The command-path
// faults wait for ROLE_GATELINK (BF-6) and are asserted here only to be refused by arm().

#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "fault.h"
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

uint32_t g_next_random = 0xA000;
uint32_t counting_random() { return ++g_next_random; }

class NullSink final : public Sink {
 public:
  void line(const char*) override {}
};

// A simnode board holding one fault identity, plus the injector under test.
struct Sim {
  IdentityTable ids;
  Outbox        out;
  NullSink      log;
  Node          node{&ids, &out, &g_mac, &log};
  FaultInjector faults{&ids, &out, &node, &g_mac, &log};
  Sim() {
    ids.init(lran_test::kTestMasterKey, &g_kdf, counting_random);
    ids.add(kNodeSim0, Role::Fault);
  }
  CtxId ctx() const { return ids.find(kNodeSim0)->ctx_id; }
};

// The bridge's receive path for frames from one peer: stages 2-10, counters and all. Stage 11
// (the command gate) is BF-6's; nothing here reaches it.
struct Receiver {
  Counters    counters;
  Reassembler reasm{&counters};
  uint8_t     key[kNodeKeyLen];

  Receiver() {
    // The bridge holds every node's derived key; here, simnode-0's.
    g_kdf.derive_node_key(lran_test::kTestMasterKey, kNodeSim0, key);
  }

  // Returns the last stage's Status: the header stage if it failed, else the payload stage,
  // else the reassembler's.
  Status feed(const uint8_t* buf, size_t len, uint32_t now_ms) {
    DecodeCtx ctx;
    ctx.self           = kNodeBridge;
    ctx.accept_ver_min = kProtoVer - 1;  // spec 13.1 - the bridge accepts N and N-1
    ctx.accept_ver_max = kProtoVer;
    ctx.mac            = &g_mac;
    ctx.node_key       = key;
    ctx.expect_ctx_id  = 0;  // the bridge tracks each node's context, expects none of its own
    ctx.counters       = &counters;

    Frame        f;
    const Status head = decode_header(buf, len, ctx, &f);
    if (head != Status::Ok) return head;
    const Status body = decode_payload(buf, len, ctx, &f);
    if (body != Status::Ok) return body;
    if (f.hdr.frag_total() == 1) return Status::Ok;  // delivered
    return reasm.accept(f, now_ms);
  }
};

// Arms `name` on f0 and feeds every queued frame to `rx` at `now_ms`. Returns frames fed.
size_t inject(Sim& s, Receiver& rx, const char* name, const FaultRequest& req, uint32_t now_ms) {
  const FaultResult r = s.faults.arm(kNodeSim0, name, req, now_ms);
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(FaultResult::Ok), static_cast<int>(r), name);
  size_t   n = 0;
  OutFrame f;
  while (s.out.pop(&f)) {
    rx.feed(f.bytes, f.len, now_ms);
    ++n;
  }
  return n;
}

// The common case: one injection, and the named counter moves by exactly `by`.
void expect_counter(const char* name, uint32_t Counters::* counter, uint32_t by) {
  Sim      s;
  Receiver rx;
  inject(s, rx, name, FaultRequest{}, 1000);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(by, rx.counters.*counter, name);
}

}  // namespace

// ---------------------------------------------------------------------------
// The catalogue holds together
// ---------------------------------------------------------------------------

// Every catalogue counter is a real spec 14.1 field. counter_name returns "?" for one that is
// not, so this fails the moment a row names a counter the registry does not carry.
void test_every_catalogue_counter_is_in_the_registry() {
  for (size_t i = 0; i < kFaultCatalogueLen; ++i) {
    const FaultInfo& f = kFaultCatalogue[i];
    if (f.counter == nullptr) continue;
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, std::strcmp("?", counter_name(f.counter)), f.name);
  }
}

void test_names_are_unique_and_findable() {
  for (size_t i = 0; i < kFaultCatalogueLen; ++i) {
    TEST_ASSERT_EQUAL_PTR(&kFaultCatalogue[i], find_fault(kFaultCatalogue[i].name));
    for (size_t j = i + 1; j < kFaultCatalogueLen; ++j) {
      TEST_ASSERT_NOT_EQUAL(0, std::strcmp(kFaultCatalogue[i].name, kFaultCatalogue[j].name));
    }
  }
  TEST_ASSERT_NULL(find_fault("no_such_fault"));
}

// ---------------------------------------------------------------------------
// Single-frame faults - one stage each
// ---------------------------------------------------------------------------

void test_runt() { expect_counter("runt", &Counters::rx_runt, 1); }
void test_oversize() { expect_counter("oversize", &Counters::rx_oversize, 1); }
void test_bad_crc() { expect_counter("bad_crc", &Counters::rx_bad_crc, 1); }
void test_wrong_dst() { expect_counter("wrong_dst", &Counters::rx_not_addressed, 1); }
void test_crit_ext() { expect_counter("crit_ext", &Counters::rx_unknown_hdr_ext, 1); }
void test_frag_zero() { expect_counter("frag_zero", &Counters::rx_bad_frag, 1); }
void test_unknown_type() { expect_counter("unknown_type", &Counters::rx_unknown_type, 1); }
void test_unknown_schema() { expect_counter("unknown_schema", &Counters::rx_unknown_schema, 1); }
void test_frag_command() { expect_counter("frag_command", &Counters::rx_not_fragmentable, 1); }
void test_bad_mac() { expect_counter("bad_mac", &Counters::rx_rejected_mac, 1); }
void test_frag_overflow() { expect_counter("frag_overflow", &Counters::rx_fragment_overflow, 1); }

// oversize reaches the SX1262's 255-byte ceiling, not merely one past LRAN_MAX_FRAME.
void test_oversize_is_the_phy_maximum() {
  Sim      s;
  Receiver rx;
  s.faults.arm(kNodeSim0, "oversize", FaultRequest{}, 1000);
  OutFrame f;
  TEST_ASSERT_TRUE(s.out.pop(&f));
  TEST_ASSERT_EQUAL_size_t(255, f.len);
}

// bad_ver sends two frames: N-1 the bridge accepts, N-2 it rejects with rx_bad_ver.
void test_bad_ver_accepts_n_minus_1_and_rejects_n_minus_2() {
  Sim      s;
  Receiver rx;
  const size_t n = inject(s, rx, "bad_ver", FaultRequest{}, 1000);
  TEST_ASSERT_EQUAL_size_t(2, n);
  TEST_ASSERT_EQUAL_UINT32(1, rx.counters.rx_bad_ver);
}

// bad_length sends one short and one long frame; both fail stage 8.
void test_bad_length_moves_the_counter_twice() {
  Sim      s;
  Receiver rx;
  const size_t n = inject(s, rx, "bad_length", FaultRequest{}, 1000);
  TEST_ASSERT_EQUAL_size_t(2, n);
  TEST_ASSERT_EQUAL_UINT32(2, rx.counters.rx_bad_length);
}

// hdr_rsv is the forward-compatibility case: a non-zero reserved byte is ACCEPTED. A discard
// here would be the bug (spec 4.3).
void test_hdr_rsv_is_accepted() {
  Sim      s;
  Receiver rx;
  s.faults.arm(kNodeSim0, "hdr_rsv", FaultRequest{}, 1000);
  OutFrame f;
  TEST_ASSERT_TRUE(s.out.pop(&f));

  DecodeCtx ctx;
  ctx.self           = kNodeBridge;
  ctx.accept_ver_min = kProtoVer - 1;
  ctx.accept_ver_max = kProtoVer;
  ctx.counters       = &rx.counters;
  Frame got;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_header(f.bytes, f.len, ctx, &got)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_payload(f.bytes, f.len, ctx, &got)));
  TEST_ASSERT_EQUAL_UINT8(0x5A, got.hdr.reserved[1]);
  TEST_ASSERT_EQUAL_UINT32(0, rx.counters.total_dropped());
}

// ---------------------------------------------------------------------------
// Fragment-sequence faults
// ---------------------------------------------------------------------------

// A single fragment, then silence: the receiver's periodic tick expires the set.
void test_frag_timeout_expires_on_the_tick() {
  Sim      s;
  Receiver rx;
  const size_t n = inject(s, rx, "frag_timeout", FaultRequest{}, 1000);
  TEST_ASSERT_EQUAL_size_t(1, n);
  TEST_ASSERT_EQUAL_UINT32(0, rx.counters.rx_reassembly_timeout);  // not yet
  rx.reasm.tick(1000 + rx.reasm.timeout_ms() + 1);
  TEST_ASSERT_EQUAL_UINT32(1, rx.counters.rx_reassembly_timeout);
}

// 15 fragments of 14 bytes reassemble to 210 > the 196-byte cap (spec 3.1).
void test_frag_oversize_exceeds_the_cap() {
  Sim      s;
  Receiver rx;
  const size_t n = inject(s, rx, "frag_oversize", FaultRequest{}, 1000);
  TEST_ASSERT_GREATER_THAN_size_t(1, n);
  TEST_ASSERT_GREATER_THAN_UINT32(0, rx.counters.rx_fragment_overflow);
}

// A duplicate index completes the set and never touches rx_dropped (spec 11.2).
void test_frag_dup_completes_and_does_not_drop() {
  Sim      s;
  Receiver rx;
  const size_t n = inject(s, rx, "frag_dup", FaultRequest{}, 1000);
  TEST_ASSERT_EQUAL_size_t(4, n);
  TEST_ASSERT_EQUAL_UINT32(1, rx.counters.rx_frag_duplicate);
  TEST_ASSERT_TRUE(rx.reasm.complete());
  TEST_ASSERT_EQUAL_UINT32(0, rx.counters.total_dropped());
}

// A fragment repeated after its set completed is a late echo, excluded from rx_dropped.
void test_frag_late_completes_and_does_not_drop() {
  Sim      s;
  Receiver rx;
  const size_t n = inject(s, rx, "frag_late", FaultRequest{}, 1000);
  TEST_ASSERT_EQUAL_size_t(4, n);
  TEST_ASSERT_EQUAL_UINT32(1, rx.counters.rx_frag_late);
  TEST_ASSERT_EQUAL_UINT32(0, rx.counters.total_dropped());
}

// The highest-value entry: a single-frame frame interposed in a set neither displaces nor
// abandons it. The set completes and rx_reassembly_abandoned does not move (spec 11.2).
void test_single_frame_interleave_leaves_the_set_intact() {
  Sim      s;
  Receiver rx;
  const size_t n = inject(s, rx, "single_frame_interleave", FaultRequest{}, 1000);
  TEST_ASSERT_EQUAL_size_t(4, n);
  TEST_ASSERT_EQUAL_UINT32(0, rx.counters.rx_reassembly_abandoned);
  TEST_ASSERT_TRUE(rx.reasm.complete());
}

// A second set's first fragment displaces a live set (spec 11.3).
void test_set_displaced() {
  Sim      s;
  Receiver rx;
  inject(s, rx, "set_displaced", FaultRequest{}, 1000);
  TEST_ASSERT_EQUAL_UINT32(1, rx.counters.rx_reassembly_abandoned);
}

// ---------------------------------------------------------------------------
// Context and sequence faults - accepted, no discard
// ---------------------------------------------------------------------------

void test_ctx_jump_is_accepted_with_a_new_context() {
  Sim      s;
  Receiver rx;
  const CtxId before = s.ctx();
  s.faults.arm(kNodeSim0, "ctx_jump", FaultRequest{}, 1000);
  OutFrame f;
  TEST_ASSERT_TRUE(s.out.pop(&f));
  DecodeCtx ctx;
  ctx.self           = kNodeBridge;
  ctx.accept_ver_min = kProtoVer - 1;
  ctx.accept_ver_max = kProtoVer;
  ctx.counters       = &rx.counters;
  Frame got;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_header(f.bytes, f.len, ctx, &got)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_payload(f.bytes, f.len, ctx, &got)));
  TEST_ASSERT_NOT_EQUAL(before, got.hdr.ctx_id);  // a jumped context the bridge adopts
  TEST_ASSERT_EQUAL_UINT32(0, rx.counters.total_dropped());
}

void test_seq_jump_is_accepted() {
  Sim      s;
  Receiver rx;
  inject(s, rx, "seq_jump", FaultRequest{}, 1000);
  TEST_ASSERT_EQUAL_UINT32(0, rx.counters.total_dropped());
}

// seq 0xFFFF then 0x0000: both accepted. The failure guarded against is a plain `>` locking
// out every frame after the wrap (spec 10.2).
void test_seq_wrap_accepts_both_sides_of_the_wrap() {
  Sim      s;
  Receiver rx;
  const size_t n = inject(s, rx, "seq_wrap", FaultRequest{}, 1000);
  TEST_ASSERT_EQUAL_size_t(2, n);
  TEST_ASSERT_EQUAL_UINT32(0, rx.counters.total_dropped());
}

// ---------------------------------------------------------------------------
// Behaviour faults
// ---------------------------------------------------------------------------

// silent withholds the identity's next `count` answers. A polled ROLE_RANGE stays quiet, and
// availability at the bridge would go offline (V-B3).
void test_silent_withholds_answers_then_resumes() {
  Sim s;
  s.ids.remove(kNodeSim0);
  s.ids.add(kNodeSim0, Role::Range);
  Identity* e = s.ids.find(kNodeSim0);

  FaultRequest sreq;
  sreq.count          = 2;
  const FaultResult r = s.faults.arm(kNodeSim0, "silent", sreq, 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultResult::Ok), static_cast<int>(r));
  TEST_ASSERT_EQUAL_UINT16(2, e->silent_left);

  // Two polls answered by silence, the third answered normally.
  Receiver rx;
  uint8_t  poll[kMaxFrame];
  auto     send_poll = [&](uint32_t now) {
    Header h;
    h.type   = MsgType::Poll;
    h.src    = kNodeBridge;
    h.dst    = kNodeSim0;
    h.seq    = 7;
    h.ctx_id = e->ctx_id;
    EncodeCtx ectx;
    ectx.mac      = &g_mac;
    ectx.node_key = e->key;
    const uint8_t p[1] = {0};
    size_t        len  = 0;
    encode(h, p, 1, ectx, poll, sizeof(poll), &len);
    s.node.on_rx(poll, len, -50, 90, now);
  };

  send_poll(10);
  send_poll(20);
  TEST_ASSERT_EQUAL_size_t(0, s.out.size());  // both withheld
  TEST_ASSERT_EQUAL_UINT16(0, e->silent_left);
  send_poll(30);
  TEST_ASSERT_EQUAL_size_t(1, s.out.size());  // resumed
  TEST_ASSERT_EQUAL_UINT32(2, e->answers_suppressed);
}

// flood arms a high count at gap 0; each tick that has outbox room queues one more, and the
// fault disarms after the count. lora_task blocking is the bridge's concern, checked there.
void test_flood_fires_the_whole_count_across_ticks() {
  Sim s;
  FaultRequest req;
  req.count = 5;
  const FaultResult r = s.faults.arm(kNodeSim0, "flood", req, 0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultResult::Ok), static_cast<int>(r));
  TEST_ASSERT_EQUAL_size_t(1, s.out.size());  // first injection at arm

  // Drain and tick until the fault disarms.
  OutFrame f;
  uint32_t now = 1;
  for (int guard = 0; guard < 100 && s.faults.armed(kNodeSim0) != nullptr; ++guard) {
    while (s.out.pop(&f)) {
    }
    s.faults.tick(now++);
  }
  TEST_ASSERT_NULL(s.faults.armed(kNodeSim0));
  TEST_ASSERT_EQUAL_UINT32(5, s.faults.injections());
}

// ---------------------------------------------------------------------------
// Arming rules
// ---------------------------------------------------------------------------

void test_command_path_faults_wait_for_bf6() {
  Sim s;
  for (const char* name : {"ack_suppress", "ack_dup", "event_replay", "cmd_replay",
                           "cmd_stale_seq"}) {
    const FaultResult r = s.faults.arm(kNodeSim0, name, FaultRequest{}, 1000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(FaultResult::WaitsForTask),
                                  static_cast<int>(r), name);
  }
}

void test_bad_phy_crc_cannot_be_injected() {
  Sim s;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultResult::NotInjectable),
                        static_cast<int>(s.faults.arm(kNodeSim0, "bad_phy_crc", FaultRequest{}, 1)));
}

void test_unknown_fault_and_missing_identity_and_zero_count() {
  Sim s;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultResult::UnknownFault),
                        static_cast<int>(s.faults.arm(kNodeSim0, "nope", FaultRequest{}, 1)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultResult::NoIdentity),
                        static_cast<int>(s.faults.arm(kNodeSim1, "runt", FaultRequest{}, 1)));
  FaultRequest zero;
  zero.count = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultResult::BadCount),
                        static_cast<int>(s.faults.arm(kNodeSim0, "runt", zero, 1)));
}

// A fault disarms when the identity it was armed on is removed under it (10.6 rule 2's spirit:
// no fault outlives its target).
void test_a_fault_disarms_when_its_identity_is_removed() {
  Sim          s;
  FaultRequest req;
  req.count = 10;
  req.has_gap = true;
  req.gap_ms  = 1000;
  s.faults.arm(kNodeSim0, "runt", req, 0);
  TEST_ASSERT_NOT_NULL(s.faults.armed(kNodeSim0));
  s.ids.remove(kNodeSim0);
  s.faults.tick(5000);
  TEST_ASSERT_NULL(s.faults.armed(kNodeSim0));
}

// Re-arming replaces the previous fault on that identity rather than stacking a second.
void test_rearming_replaces() {
  Sim          s;
  FaultRequest req;
  req.count = 10;
  req.has_gap = true;
  req.gap_ms  = 1000;
  s.faults.arm(kNodeSim0, "runt", req, 0);
  s.faults.arm(kNodeSim0, "bad_crc", req, 0);
  const ArmedFault* a = s.faults.armed(kNodeSim0);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultId::BadCrc), static_cast<int>(a->info->id));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_catalogue_counter_is_in_the_registry);
  RUN_TEST(test_names_are_unique_and_findable);

  RUN_TEST(test_runt);
  RUN_TEST(test_oversize);
  RUN_TEST(test_oversize_is_the_phy_maximum);
  RUN_TEST(test_bad_crc);
  RUN_TEST(test_bad_ver_accepts_n_minus_1_and_rejects_n_minus_2);
  RUN_TEST(test_wrong_dst);
  RUN_TEST(test_crit_ext);
  RUN_TEST(test_hdr_rsv_is_accepted);
  RUN_TEST(test_frag_zero);
  RUN_TEST(test_unknown_type);
  RUN_TEST(test_unknown_schema);
  RUN_TEST(test_bad_length_moves_the_counter_twice);
  RUN_TEST(test_frag_command);
  RUN_TEST(test_bad_mac);

  RUN_TEST(test_frag_timeout_expires_on_the_tick);
  RUN_TEST(test_frag_overflow);
  RUN_TEST(test_frag_oversize_exceeds_the_cap);
  RUN_TEST(test_frag_dup_completes_and_does_not_drop);
  RUN_TEST(test_frag_late_completes_and_does_not_drop);
  RUN_TEST(test_single_frame_interleave_leaves_the_set_intact);
  RUN_TEST(test_set_displaced);

  RUN_TEST(test_ctx_jump_is_accepted_with_a_new_context);
  RUN_TEST(test_seq_jump_is_accepted);
  RUN_TEST(test_seq_wrap_accepts_both_sides_of_the_wrap);

  RUN_TEST(test_silent_withholds_answers_then_resumes);
  RUN_TEST(test_flood_fires_the_whole_count_across_ticks);

  RUN_TEST(test_command_path_faults_wait_for_bf6);
  RUN_TEST(test_bad_phy_crc_cannot_be_injected);
  RUN_TEST(test_unknown_fault_and_missing_identity_and_zero_count);
  RUN_TEST(test_a_fault_disarms_when_its_identity_is_removed);
  RUN_TEST(test_rearming_replaces);
  return UNITY_END();
}
