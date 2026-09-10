// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-10 - the bridge project's `native` environment, proving one thing.
//
// WHAT THIS IS FOR. Impl Plan 5.4 requires every firmware project to define a native
// environment that builds and runs the shared-library tests, because "a library that
// only compiles for ESP32-S3 has quietly acquired a platform dependency, and the
// place that surfaces is the host build". /lib/lran-protocol/ has its own Unity
// suite and its own native environment; this file does NOT duplicate it. It checks
// that the library builds and links FROM THIS PROJECT, through this project's
// `lib_extra_dirs`, with this project's flags.
//
// That is a real failure mode and not a hypothetical one: a project that adds an
// Arduino include to a shared header, or a flag that changes a type width, breaks
// here while the library's own suite stays green.
//
// A POLL round trip is the smallest thing that exercises serialize, encode, the
// spec 14 receive ladder and the CRC together. POLL is unauthenticated (spec 9.2), so
// this needs no key material and no MAC implementation - which is what keeps the host
// environment free of secrets.h.

#include <unity.h>

#include <cstring>

#include "lran/lran.h"

using namespace lran;
using namespace lran::msg;  // Poll, kPollLen, serialize - spec 7.2.1

namespace {

Header bridge_poll() {
  Header h;
  h.ver    = kProtoVer;
  h.type   = MsgType::Poll;
  h.src    = kNodeBridge;
  h.dst    = kNodeGateLink;
  h.seq    = 0x0001;
  h.ctx_id = 0x0000ABCDu;
  h.set_frag(0, 1);
  h.schema = kSchemaNone;
  return h;
}

// spec 7.2.1 - POLL carries one byte, `poll_flags`. Serialized field by field through
// the library rather than written as a literal: root rule 1, never a struct on the
// wire, and a test that hand-rolls the payload is not testing the codec.
size_t poll_payload(uint8_t* out, size_t cap) {
  Poll   p;
  size_t written = 0;
  if (serialize(p, out, cap, &written) != Status::Ok) {
    return 0;
  }
  return written;
}

}  // namespace

void setUp() {}
void tearDown() {}

// The library links here, encodes, and the frame it produces is the length spec 19
// says it is.
void test_poll_encodes_from_this_project() {
  uint8_t buf[kMaxFrame] = {0};
  uint8_t payload[kPollLen] = {0};
  const size_t payload_len = poll_payload(payload, sizeof(payload));
  TEST_ASSERT_EQUAL_size_t(kPollLen, payload_len);

  size_t    len = 0;
  EncodeCtx ctx;  // no MAC - POLL is unauthenticated, spec 9.2
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(encode(bridge_poll(), payload, payload_len, ctx,
                                                buf, sizeof(buf), &len)));
  TEST_ASSERT_EQUAL_size_t(frame_len(payload_len, false), len);
}

// And the receive ladder accepts it back with the header intact. A decode that
// disagrees with the encoder about a field width is the shape of the platform
// dependency this environment exists to catch.
void test_poll_round_trips_through_the_receive_ladder() {
  uint8_t buf[kMaxFrame] = {0};
  uint8_t payload[kPollLen] = {0};
  const size_t payload_len = poll_payload(payload, sizeof(payload));

  size_t    len = 0;
  EncodeCtx enc;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(encode(bridge_poll(), payload, payload_len, enc,
                                                buf, sizeof(buf), &len)));

  // The node's view of a bridge poll: dst is itself, and it verifies no MAC because
  // POLL carries none.
  DecodeCtx dec;
  dec.self = kNodeGateLink;

  Frame f;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_header(buf, len, dec, &f)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_payload(buf, len, dec, &f)));

  TEST_ASSERT_EQUAL_UINT8(kProtoVer, f.hdr.ver);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MsgType::Poll), static_cast<int>(f.hdr.type));
  TEST_ASSERT_EQUAL_UINT8(kNodeBridge, f.hdr.src);
  TEST_ASSERT_EQUAL_UINT8(kNodeGateLink, f.hdr.dst);
  TEST_ASSERT_EQUAL_UINT16(0x0001, f.hdr.seq);
  TEST_ASSERT_EQUAL_UINT32(0x0000ABCDu, f.hdr.ctx_id);
  TEST_ASSERT_EQUAL_size_t(kPollLen, f.payload_len);
}

// A frame addressed elsewhere is discarded and COUNTED. Root rule 4 - never discard
// silently - is a property of the library, and this asserts the bridge project sees
// it too rather than assuming it.
void test_frame_for_another_node_is_discarded_and_counted() {
  uint8_t buf[kMaxFrame] = {0};
  uint8_t payload[kPollLen] = {0};
  const size_t payload_len = poll_payload(payload, sizeof(payload));

  size_t    len = 0;
  EncodeCtx enc;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(encode(bridge_poll(), payload, payload_len, enc,
                                                buf, sizeof(buf), &len)));

  Counters  counters;
  DecodeCtx dec;
  dec.self     = kNodeWellLink;  // not the dst the frame carries
  dec.counters = &counters;

  Frame f;
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(Status::Ok),
                        static_cast<int>(decode_header(buf, len, dec, &f)));
  TEST_ASSERT_EQUAL_UINT32(1, counters.rx_not_addressed);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_poll_encodes_from_this_project);
  RUN_TEST(test_poll_round_trips_through_the_receive_ladder);
  RUN_TEST(test_frame_for_another_node_is_discarded_and_counted);
  return UNITY_END();
}
