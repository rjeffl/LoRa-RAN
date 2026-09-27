// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-27 - internal packet loopback (Impl Plan 6.6.3, PRD R-5.4a).
//
// THE PROPERTIES UNDER TEST. A dummy STATUS and EVENT go to wire bytes and back through a
// receive ladder unchanged, still marked. A corrupted frame is refused at spec 14 stage 3 and
// named, and the corruption is one-shot. With no keys the ladder refuses the source, as the
// live one does before registry_begin().
//
// WHAT THIS CANNOT COVER. That the loopback's ladder is not lora_task's, which is where
// task_runtime.cpp declares it; and the RF half of R-5.4a, which is not built.

#include <unity.h>

#include <cstring>

#include "dummy.h"
#include "loopback.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

constexpr CtxId kCtx = 0x100B100B;

struct GateLinkOnly final : PeerKeys {
  uint8_t        key[kNodeKeyLen] = {0};
  const uint8_t* key_for(NodeId src) const override {
    return src == kNodeGateLink ? key : nullptr;
  }
  bool is_registered(NodeId src) const override { return src == kNodeGateLink; }
};

RxMessage dummy_frame(const char* line) {
  DummyPublisher d;
  RxMessage      m;
  char           reply[1024];
  TEST_ASSERT_EQUAL(DummyOutcome::Inject, d.handle(line, kCtx, 1000, &m, reply, sizeof(reply)));
  return m;
}

LoopbackOutcome say(Loopback& l, const char* line) {
  char reply[128];
  return l.handle(line, reply, sizeof(reply));
}

}  // namespace

// ---------------------------------------------------------------------------

void test_off_until_turned_on() {
  Loopback l;
  TEST_ASSERT_FALSE(l.enabled());
  TEST_ASSERT_EQUAL(LoopbackOutcome::NotMine, say(l, "sim show"));
  TEST_ASSERT_EQUAL(LoopbackOutcome::Refused, say(l, "loopback corrupt"));
  TEST_ASSERT_EQUAL(LoopbackOutcome::Reply, say(l, "loopback on"));
  TEST_ASSERT_TRUE(l.enabled());
  TEST_ASSERT_EQUAL(LoopbackOutcome::Reply, say(l, "loopback off"));
  TEST_ASSERT_FALSE(l.enabled());
}

void test_status_comes_back_whole_and_marked() {
  GateLinkOnly keys;
  Loopback     l;
  l.set_keys(&keys);
  RxMessage       m    = dummy_frame("dummy status gatelink");
  const RxMessage sent = m;
  char            reply[96];
  TEST_ASSERT_TRUE_MESSAGE(l.pass(&m, 2000, reply, sizeof(reply)), reply);
  TEST_ASSERT_TRUE(m.dummy);
  TEST_ASSERT_EQUAL_UINT32(sent.rx_millis, m.rx_millis);
  TEST_ASSERT_EQUAL(MsgType::Status, m.hdr.type);
  TEST_ASSERT_EQUAL_HEX8(kNodeGateLink, m.hdr.src);
  TEST_ASSERT_EQUAL_UINT32(kCtx, m.hdr.ctx_id);
  TEST_ASSERT_EQUAL_UINT16(sent.hdr.seq, m.hdr.seq);
  TEST_ASSERT_EQUAL_HEX8(kSchemaGateLinkStatusV1, m.hdr.schema);
  TEST_ASSERT_EQUAL(sent.payload_len, m.payload_len);
  TEST_ASSERT_EQUAL_MEMORY(sent.payload, m.payload, sent.payload_len);
  TEST_ASSERT_EQUAL_UINT32(1, l.passed());
}

void test_event_comes_back_whole() {
  GateLinkOnly keys;
  Loopback     l;
  l.set_keys(&keys);
  RxMessage       m    = dummy_frame("dummy event gatelink vehicle_detected");
  const RxMessage sent = m;
  char            reply[96];
  TEST_ASSERT_TRUE_MESSAGE(l.pass(&m, 2000, reply, sizeof(reply)), reply);
  TEST_ASSERT_EQUAL(MsgType::Event, m.hdr.type);
  TEST_ASSERT_EQUAL_MEMORY(sent.payload, m.payload, sent.payload_len);
}

// Stage 3's CRC16 refuses it, the status is named, and only the next frame is corrupted.
void test_corrupt_is_refused_by_name_and_once() {
  GateLinkOnly keys;
  Loopback     l;
  l.set_keys(&keys);
  TEST_ASSERT_EQUAL(LoopbackOutcome::Reply, say(l, "loopback on"));
  TEST_ASSERT_EQUAL(LoopbackOutcome::Reply, say(l, "loopback corrupt"));
  RxMessage m = dummy_frame("dummy status gatelink");
  char      reply[96];
  TEST_ASSERT_FALSE(l.pass(&m, 2000, reply, sizeof(reply)));
  TEST_ASSERT_EQUAL(Status::BadCrc, l.last_status());
  TEST_ASSERT_NOT_NULL(std::strstr(reply, to_string(Status::BadCrc)));
  TEST_ASSERT_EQUAL_UINT32(1, l.refused());
  RxMessage again = dummy_frame("dummy status gatelink");
  TEST_ASSERT_TRUE(l.pass(&again, 2100, reply, sizeof(reply)));
}

void test_no_keys_refuses_the_source() {
  Loopback  l;
  RxMessage m = dummy_frame("dummy status gatelink");
  char      reply[96];
  TEST_ASSERT_FALSE(l.pass(&m, 2000, reply, sizeof(reply)));
  TEST_ASSERT_EQUAL(Status::UnknownSrc, l.last_status());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_off_until_turned_on);
  RUN_TEST(test_status_comes_back_whole_and_marked);
  RUN_TEST(test_event_comes_back_whole);
  RUN_TEST(test_corrupt_is_refused_by_name_and_once);
  RUN_TEST(test_no_keys_refuses_the_source);
  return UNITY_END();
}
