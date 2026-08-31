// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Host tests for the R6 responder position log.

#include <unity.h>

#include "resp_log.h"

using namespace rangetest;

void setUp() {}
void tearDown() {}

// The whole point of the log: separating downlink loss from uplink loss, which
// round-trip PER conflates by design.
static void test_probes_heard_and_echoes_sent_are_counted_separately() {
  PositionLog log;
  // Three probes heard, but only two echoes got out.
  log.record_probe(0, -450, 110);
  log.record_echo(0);
  log.record_probe(0, -455, 112);
  log.record_echo(0);
  log.record_probe(0, -460, 108);   // heard, echo failed to transmit

  const PositionSummary* s = log.find(0);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_EQUAL_UINT16(3, s->probes_heard);
  TEST_ASSERT_EQUAL_UINT16(2, s->echoes_sent);
}

static void test_positions_are_kept_apart() {
  PositionLog log;
  log.record_probe(0, -450, 110);
  log.record_probe(1, -600, 60);
  log.record_probe(1, -610, 55);

  TEST_ASSERT_EQUAL_size_t(2, log.count());
  TEST_ASSERT_EQUAL_UINT16(1, log.find(0)->probes_heard);
  TEST_ASSERT_EQUAL_UINT16(2, log.find(1)->probes_heard);
  TEST_ASSERT_EQUAL_INT16(-605, log.find(1)->rssi_dbm10.mean());
}

static void test_revisiting_a_position_accumulates_into_the_same_slot() {
  PositionLog log;
  log.record_probe(4, -450, 110);
  log.record_probe(5, -500, 100);
  log.record_probe(4, -460, 112);   // walked back

  TEST_ASSERT_EQUAL_size_t(2, log.count());
  TEST_ASSERT_EQUAL_UINT16(2, log.find(4)->probes_heard);
}

static void test_unknown_position_is_not_found() {
  PositionLog log;
  log.record_probe(0, -450, 110);
  TEST_ASSERT_NULL(log.find(99));
}

// A dumped log that has quietly dropped its first positions is worse than one that
// says it has.
static void test_ring_overflow_is_reported() {
  PositionLog log;
  TEST_ASSERT_FALSE(log.overflowed());
  for (uint16_t i = 0; i < kPositionLogCapacity; ++i) log.record_probe(i, -450, 110);
  TEST_ASSERT_FALSE(log.overflowed());
  TEST_ASSERT_EQUAL_size_t(kPositionLogCapacity, log.count());

  log.record_probe(999, -450, 110);   // one too many
  TEST_ASSERT_TRUE(log.overflowed());
  TEST_ASSERT_EQUAL_size_t(kPositionLogCapacity, log.count());
  TEST_ASSERT_NOT_NULL(log.find(999));
  TEST_ASSERT_NULL(log.find(0));      // oldest displaced
}

// ---------------------------------------------------------------------------
// Persistence - the part that can be silently wrong
// ---------------------------------------------------------------------------

static void test_blob_round_trip_preserves_every_field() {
  PositionLog a;
  a.record_probe(7, -450, 110);
  a.record_echo(7);
  a.record_probe(7, -470, 130);
  a.record_probe(8, -600, -20);

  uint8_t blob[PositionLog::kBlobMaxLen];
  const size_t n = a.serialize(blob, sizeof(blob));
  TEST_ASSERT_GREATER_THAN_size_t(0, n);

  PositionLog b;
  TEST_ASSERT_TRUE(b.deserialize(blob, n));
  TEST_ASSERT_EQUAL_size_t(a.count(), b.count());

  const PositionSummary* x = b.find(7);
  TEST_ASSERT_NOT_NULL(x);
  TEST_ASSERT_EQUAL_UINT16(2,    x->probes_heard);
  TEST_ASSERT_EQUAL_UINT16(1,    x->echoes_sent);
  TEST_ASSERT_EQUAL_INT16(-470,  x->rssi_dbm10.min);
  TEST_ASSERT_EQUAL_INT16(-450,  x->rssi_dbm10.max);
  TEST_ASSERT_EQUAL_INT16(-460,  x->rssi_dbm10.mean());

  // SNR min AND max. An earlier draft stored only min and rebuilt max from it, which
  // silently discarded the reading - this asserts both survive.
  TEST_ASSERT_EQUAL_INT16(110, x->snr_db10.min);
  TEST_ASSERT_EQUAL_INT16(130, x->snr_db10.max);
  TEST_ASSERT_EQUAL_INT16(120, x->snr_db10.mean());

  const PositionSummary* y = b.find(8);
  TEST_ASSERT_NOT_NULL(y);
  TEST_ASSERT_EQUAL_INT16(-20, y->snr_db10.min);
  TEST_ASSERT_EQUAL_INT16(-20, y->snr_db10.max);
}

static void test_empty_log_round_trips() {
  PositionLog a;
  uint8_t blob[PositionLog::kBlobMaxLen];
  const size_t n = a.serialize(blob, sizeof(blob));
  TEST_ASSERT_GREATER_THAN_size_t(0, n);

  PositionLog b;
  b.record_probe(1, -400, 100);
  TEST_ASSERT_TRUE(b.deserialize(blob, n));
  TEST_ASSERT_EQUAL_size_t(0, b.count());
}

static void test_full_log_fits_the_advertised_blob() {
  PositionLog a;
  for (uint16_t i = 0; i < kPositionLogCapacity; ++i) {
    a.record_probe(i, -450, 110);
    a.record_echo(i);
  }
  uint8_t blob[PositionLog::kBlobMaxLen];
  const size_t n = a.serialize(blob, sizeof(blob));
  TEST_ASSERT_GREATER_THAN_size_t(0, n);
  TEST_ASSERT_LESS_OR_EQUAL_size_t(PositionLog::kBlobMaxLen, n);
}

static void test_serialize_refuses_a_short_buffer() {
  PositionLog a;
  a.record_probe(1, -400, 100);
  uint8_t small[4];
  TEST_ASSERT_EQUAL_size_t(0, a.serialize(small, sizeof(small)));
}

// Garbage in NVS must not become a half-loaded log that looks like data.
static void test_corrupt_blob_leaves_the_log_cleared() {
  PositionLog a;
  a.record_probe(1, -400, 100);
  uint8_t blob[PositionLog::kBlobMaxLen];
  const size_t n = a.serialize(blob, sizeof(blob));

  PositionLog b;
  b.record_probe(9, -500, 90);

  uint8_t bad_magic[PositionLog::kBlobMaxLen];
  for (size_t i = 0; i < n; ++i) bad_magic[i] = blob[i];
  bad_magic[0] ^= 0xFF;
  TEST_ASSERT_FALSE(b.deserialize(bad_magic, n));
  TEST_ASSERT_EQUAL_size_t(0, b.count());

  uint8_t bad_ver[PositionLog::kBlobMaxLen];
  for (size_t i = 0; i < n; ++i) bad_ver[i] = blob[i];
  bad_ver[2] = 99;
  TEST_ASSERT_FALSE(b.deserialize(bad_ver, n));
  TEST_ASSERT_EQUAL_size_t(0, b.count());

  TEST_ASSERT_FALSE(b.deserialize(blob, 2));            // truncated
  TEST_ASSERT_FALSE(b.deserialize(blob, n - 1));        // short by a byte
  TEST_ASSERT_EQUAL_size_t(0, b.count());
}

// Repo rule 1 - the blob is read back by host tooling, so its byte order is asserted
// against literal offsets rather than against a round trip through itself.
static void test_blob_is_explicit_little_endian() {
  PositionLog a;
  a.record_probe(0x0201, -2, 1);

  uint8_t blob[PositionLog::kBlobMaxLen];
  TEST_ASSERT_GREATER_THAN_size_t(0, a.serialize(blob, sizeof(blob)));

  TEST_ASSERT_EQUAL_UINT8(0x4C, blob[0]);   // magic 0x524C, low byte first
  TEST_ASSERT_EQUAL_UINT8(0x52, blob[1]);
  TEST_ASSERT_EQUAL_UINT8(1,    blob[2]);   // version
  TEST_ASSERT_EQUAL_UINT8(1,    blob[3]);   // one entry
  TEST_ASSERT_EQUAL_UINT8(0x01, blob[4]);   // position_id low
  TEST_ASSERT_EQUAL_UINT8(0x02, blob[5]);
  TEST_ASSERT_EQUAL_UINT8(0x01, blob[6]);   // probes_heard = 1
  TEST_ASSERT_EQUAL_UINT8(0x00, blob[7]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_probes_heard_and_echoes_sent_are_counted_separately);
  RUN_TEST(test_positions_are_kept_apart);
  RUN_TEST(test_revisiting_a_position_accumulates_into_the_same_slot);
  RUN_TEST(test_unknown_position_is_not_found);
  RUN_TEST(test_ring_overflow_is_reported);
  RUN_TEST(test_blob_round_trip_preserves_every_field);
  RUN_TEST(test_empty_log_round_trips);
  RUN_TEST(test_full_log_fits_the_advertised_blob);
  RUN_TEST(test_serialize_refuses_a_short_buffer);
  RUN_TEST(test_corrupt_blob_leaves_the_log_cleared);
  RUN_TEST(test_blob_is_explicit_little_endian);
  return UNITY_END();
}
