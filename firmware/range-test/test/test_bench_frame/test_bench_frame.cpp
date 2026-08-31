// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Host tests for the R4 bench frame.

#include <unity.h>

#include <cstring>

#include "bench_frame.h"

using namespace rangetest;

void setUp() {}
void tearDown() {}

namespace {

BenchFrame probe(uint16_t seq) {
  BenchFrame f{};
  f.kind        = BenchKind::Probe;
  f.position_id = 3;
  f.tp_index    = 7;
  f.probe_seq   = seq;
  return f;
}

}  // namespace

// Repo rule 1: the byte order is asserted against literal offsets, not against a
// round trip. A round trip through one implementation agrees with itself even when
// both halves are wrong - which is the whole reason the rule exists.
static void test_wire_layout_is_explicit_little_endian() {
  BenchFrame f = probe(0x1234);
  f.kind            = BenchKind::Echo;
  f.position_id     = 0xAABB;
  f.tp_index        = 0x0102;
  f.resp_rssi_dbm10 = -495;   // -49.5 dBm
  f.resp_snr_db10   = 122;    //  12.2 dB

  uint8_t buf[32];
  TEST_ASSERT_EQUAL_size_t(kBenchHeaderLen,
                           bench_serialize(f, buf, sizeof(buf), kBenchHeaderLen));

  TEST_ASSERT_EQUAL_UINT8(0x52, buf[0]);   // magic 0x4C52, low byte first
  TEST_ASSERT_EQUAL_UINT8(0x4C, buf[1]);
  TEST_ASSERT_EQUAL_UINT8(1,    buf[2]);   // version
  TEST_ASSERT_EQUAL_UINT8(2,    buf[3]);   // kind = Echo
  TEST_ASSERT_EQUAL_UINT8(0xBB, buf[4]);   // position_id low
  TEST_ASSERT_EQUAL_UINT8(0xAA, buf[5]);
  TEST_ASSERT_EQUAL_UINT8(0x02, buf[6]);   // tp_index low
  TEST_ASSERT_EQUAL_UINT8(0x01, buf[7]);
  TEST_ASSERT_EQUAL_UINT8(0x34, buf[8]);   // probe_seq low
  TEST_ASSERT_EQUAL_UINT8(0x12, buf[9]);
  TEST_ASSERT_EQUAL_UINT8(0x11, buf[10]);  // -495 = 0xFE11
  TEST_ASSERT_EQUAL_UINT8(0xFE, buf[11]);
  TEST_ASSERT_EQUAL_UINT8(0x7A, buf[12]);  //  122 = 0x007A
  TEST_ASSERT_EQUAL_UINT8(0x00, buf[13]);
}

static void test_round_trip_preserves_every_field() {
  BenchFrame f = probe(1000);
  f.kind            = BenchKind::Echo;
  f.resp_rssi_dbm10 = -1001;
  f.resp_snr_db10   = -35;

  uint8_t buf[64];
  TEST_ASSERT_EQUAL_size_t(64, bench_serialize(f, buf, sizeof(buf), 64));

  BenchFrame g{};
  TEST_ASSERT_TRUE(bench_parse(buf, 64, &g));
  TEST_ASSERT_EQUAL(static_cast<int>(BenchKind::Echo), static_cast<int>(g.kind));
  TEST_ASSERT_EQUAL_UINT16(3,     g.position_id);
  TEST_ASSERT_EQUAL_UINT16(7,     g.tp_index);
  TEST_ASSERT_EQUAL_UINT16(1000,  g.probe_seq);
  TEST_ASSERT_EQUAL_INT16(-1001,  g.resp_rssi_dbm10);
  TEST_ASSERT_EQUAL_INT16(-35,    g.resp_snr_db10);
}

// spec 4.6 / repo rule 6. A probe has no responder measurement, and that must be
// distinguishable from a genuine 0.0 reading.
static void test_probe_carries_the_not_available_sentinel_not_zero() {
  BenchFrame f = probe(5);
  uint8_t buf[32];
  bench_serialize(f, buf, sizeof(buf), kBenchHeaderLen);

  BenchFrame g{};
  TEST_ASSERT_TRUE(bench_parse(buf, kBenchHeaderLen, &g));
  TEST_ASSERT_EQUAL_INT16(kI16NotAvailable, g.resp_rssi_dbm10);
  TEST_ASSERT_EQUAL_INT16(kI16NotAvailable, g.resp_snr_db10);
  TEST_ASSERT_NOT_EQUAL(0, g.resp_rssi_dbm10);
}

// A real 0.0 dB SNR must survive as 0 and not be confused with "no reading".
static void test_zero_is_a_real_reading() {
  BenchFrame f = probe(5);
  f.kind          = BenchKind::Echo;
  f.resp_snr_db10 = 0;

  uint8_t buf[32];
  bench_serialize(f, buf, sizeof(buf), kBenchHeaderLen);
  BenchFrame g{};
  TEST_ASSERT_TRUE(bench_parse(buf, kBenchHeaderLen, &g));
  TEST_ASSERT_EQUAL_INT16(0, g.resp_snr_db10);
  TEST_ASSERT_NOT_EQUAL(kI16NotAvailable, g.resp_snr_db10);
}

// The site has known 915 MHz occupants (D1 notes: four YoLink sensors on an SX1276).
// A foreign frame counted as an echo would inflate apparent link quality, which is
// the one direction of error that matters.
static void test_foreign_traffic_is_rejected() {
  uint8_t buf[32];
  bench_serialize(probe(1), buf, sizeof(buf), 32);
  BenchFrame g{};

  uint8_t bad_magic[32];
  std::memcpy(bad_magic, buf, 32);
  bad_magic[0] ^= 0xFF;
  TEST_ASSERT_FALSE(bench_parse(bad_magic, 32, &g));

  uint8_t bad_ver[32];
  std::memcpy(bad_ver, buf, 32);
  bad_ver[2] = 99;
  TEST_ASSERT_FALSE(bench_parse(bad_ver, 32, &g));

  uint8_t bad_kind[32];
  std::memcpy(bad_kind, buf, 32);
  bad_kind[3] = 0;
  TEST_ASSERT_FALSE(bench_parse(bad_kind, 32, &g));
}

static void test_runt_frame_is_rejected() {
  uint8_t buf[32];
  bench_serialize(probe(1), buf, sizeof(buf), 32);
  BenchFrame g{};
  TEST_ASSERT_FALSE(bench_parse(buf, kBenchHeaderLen - 1, &g));
  TEST_ASSERT_TRUE(bench_parse(buf, kBenchHeaderLen, &g));
}

static void test_serialize_refuses_impossible_lengths() {
  uint8_t buf[32];
  TEST_ASSERT_EQUAL_size_t(0, bench_serialize(probe(1), buf, sizeof(buf),
                                              kBenchHeaderLen - 1));   // too short
  TEST_ASSERT_EQUAL_size_t(0, bench_serialize(probe(1), buf, sizeof(buf), 33));
  TEST_ASSERT_EQUAL_size_t(0, bench_serialize(probe(1), nullptr, 32, 32));
}

// Payload size is a swept parameter, so the padding has to be real bytes and has to
// be checkable - otherwise a large payload only tests that the radio moved zeros.
static void test_filler_follows_the_pattern_and_localizes_corruption() {
  uint8_t buf[64];
  bench_serialize(probe(0x0102), buf, sizeof(buf), 64);

  TEST_ASSERT_EQUAL_UINT8(0x02, buf[kBenchHeaderLen + 0]);  // (seq & 0xFF) + 0
  TEST_ASSERT_EQUAL_UINT8(0x03, buf[kBenchHeaderLen + 1]);
  TEST_ASSERT_TRUE(bench_check_filler(buf, 64, 0x0102, nullptr));

  buf[40] ^= 0x01;
  size_t bad = 0;
  TEST_ASSERT_FALSE(bench_check_filler(buf, 64, 0x0102, &bad));
  TEST_ASSERT_EQUAL_size_t(40, bad);
}

static void test_header_only_frame_has_no_filler_to_check() {
  uint8_t buf[32];
  bench_serialize(probe(9), buf, sizeof(buf), kBenchHeaderLen);
  TEST_ASSERT_TRUE(bench_check_filler(buf, kBenchHeaderLen, 9, nullptr));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wire_layout_is_explicit_little_endian);
  RUN_TEST(test_round_trip_preserves_every_field);
  RUN_TEST(test_probe_carries_the_not_available_sentinel_not_zero);
  RUN_TEST(test_zero_is_a_real_reading);
  RUN_TEST(test_foreign_traffic_is_rejected);
  RUN_TEST(test_runt_frame_is_rejected);
  RUN_TEST(test_serialize_refuses_impossible_lengths);
  RUN_TEST(test_filler_follows_the_pattern_and_localizes_corruption);
  RUN_TEST(test_header_only_frame_has_no_filler_to_check);
  return UNITY_END();
}
