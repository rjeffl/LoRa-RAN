// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-27 - the raw frame log's ring and its rendering (Impl Plan 6.6). frame_log.h has
// the reasoning.
//
// WHAT THIS CANNOT COVER. That lora_task records at every outcome the radio can produce,
// and that log_task drains faster than a burst fills the ring. The first is read from
// lora_link.cpp, which does not build on the host; the second is a bench measurement.
// What is under test is everything between: that a record survives the ring intact, that
// one the ring overwrote is counted and locatable, and that a batch is refused rather
// than truncated.

#include <unity.h>

#include <cstring>
#include <string>

#include "frame_log.h"
#include "lran/lran.h"

using namespace bridge;

void setUp() {}
void tearDown() {}

namespace {

FrameLogEntry rx_entry(uint32_t ms, lran::NodeId peer, lran::Seq seq) {
  FrameLogEntry e;
  e.ms       = ms;
  e.deaf_ms  = 100;
  e.dir      = static_cast<uint8_t>(FrameDir::Rx);
  e.rx       = static_cast<uint8_t>(RxOutcome::Packet);
  e.status   = static_cast<uint8_t>(lran::Status::Ok);
  e.peer     = peer;
  e.seq      = seq;
  e.type     = static_cast<uint8_t>(lran::MsgType::Status);
  e.schema   = 0x01;
  e.frag     = 0x01;
  e.rssi_dbm = -42;
  e.snr_db   = 9;
  return e;
}

}  // namespace

// ---------------------------------------------------------------------------
// The ring
// ---------------------------------------------------------------------------

void test_an_empty_log_yields_nothing() {
  FrameLog      log;
  FrameLogEntry out;
  TEST_ASSERT_FALSE(log.read_next(&out));
  TEST_ASSERT_EQUAL_UINT32(0, log.lost());
  TEST_ASSERT_EQUAL_UINT32(0, log.recorded());
}

void test_a_record_comes_back_whole() {
  FrameLog log;
  log.record(rx_entry(1234, 0xF0, 77));

  FrameLogEntry out;
  TEST_ASSERT_TRUE(log.read_next(&out));
  TEST_ASSERT_EQUAL_UINT32(1234, out.ms);
  TEST_ASSERT_EQUAL_UINT8(0xF0, out.peer);
  TEST_ASSERT_EQUAL_UINT16(77, out.seq);
  TEST_ASSERT_EQUAL_INT16(-42, out.rssi_dbm);
  TEST_ASSERT_EQUAL_INT8(9, out.snr_db);
  TEST_ASSERT_TRUE(out.has_radio_meta());

  TEST_ASSERT_FALSE(log.read_next(&out));
}

void test_the_index_is_assigned_by_the_log_not_the_caller() {
  FrameLog      log;
  FrameLogEntry e = rx_entry(1, 0xF0, 1);
  e.index         = 9999;  // whatever the caller left here is ignored
  log.record(e);

  FrameLogEntry out;
  TEST_ASSERT_TRUE(log.read_next(&out));
  TEST_ASSERT_EQUAL_UINT32(0, out.index);
}

void test_records_come_back_in_order() {
  FrameLog log;
  for (uint16_t i = 0; i < 8; ++i) log.record(rx_entry(i, 0xF0, i));

  FrameLogEntry out;
  for (uint16_t i = 0; i < 8; ++i) {
    TEST_ASSERT_TRUE(log.read_next(&out));
    TEST_ASSERT_EQUAL_UINT32(i, out.index);
    TEST_ASSERT_EQUAL_UINT16(i, out.seq);
  }
  TEST_ASSERT_FALSE(log.read_next(&out));
}

void test_a_full_ring_is_not_an_overwrite() {
  // Exactly kFrameLogSlots records with no reader is the boundary, and it must not
  // report loss: the ring holds them all.
  FrameLog log;
  for (size_t i = 0; i < kFrameLogSlots; ++i) {
    log.record(rx_entry(static_cast<uint32_t>(i), 0xF0, static_cast<lran::Seq>(i)));
  }

  FrameLogEntry out;
  TEST_ASSERT_TRUE(log.read_next(&out));
  TEST_ASSERT_EQUAL_UINT32(0, out.index);
  TEST_ASSERT_EQUAL_UINT32(0, log.lost());
}

void test_an_overwritten_record_is_counted_and_the_index_says_where() {
  FrameLog log;
  for (size_t i = 0; i < kFrameLogSlots + 5; ++i) {
    log.record(rx_entry(static_cast<uint32_t>(i), 0xF0, static_cast<lran::Seq>(i)));
  }

  FrameLogEntry out;
  TEST_ASSERT_TRUE(log.read_next(&out));

  // Five records fell off the back. THE POINT OF THE INDEX: the reader is told not only
  // that it lost five but that the five it lost were 0 to 4.
  TEST_ASSERT_EQUAL_UINT32(5, log.lost());
  TEST_ASSERT_EQUAL_UINT32(5, out.index);
}

void test_loss_accumulates_across_reads() {
  FrameLog      log;
  FrameLogEntry out;

  for (size_t i = 0; i < kFrameLogSlots + 3; ++i) log.record(rx_entry(0, 0xF0, 0));
  TEST_ASSERT_TRUE(log.read_next(&out));
  TEST_ASSERT_EQUAL_UINT32(3, log.lost());

  // Drain, then lap it again. The counter is cumulative, not per drain.
  while (log.read_next(&out)) {
  }
  for (size_t i = 0; i < kFrameLogSlots + 2; ++i) log.record(rx_entry(0, 0xF0, 0));
  TEST_ASSERT_TRUE(log.read_next(&out));
  TEST_ASSERT_EQUAL_UINT32(5, log.lost());
}

void test_recorded_counts_everything_ever_recorded() {
  FrameLog log;
  for (size_t i = 0; i < kFrameLogSlots + 10; ++i) log.record(rx_entry(0, 0xF0, 0));
  // Not the ring's occupancy: what was recorded, overwritten or not.
  TEST_ASSERT_EQUAL_UINT32(kFrameLogSlots + 10, log.recorded());
}

// ---------------------------------------------------------------------------
// The serial line
// ---------------------------------------------------------------------------

void test_a_line_carries_the_fields_a_gap_analysis_needs() {
  char line[160];
  const size_t n = render_line(rx_entry(1234, 0xF0, 77), line, sizeof(line));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_size_t(n, std::strlen(line));

  const std::string s(line);
  TEST_ASSERT_TRUE(s.find("rx") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("t=1234") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("peer=0xf0") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("seq=77") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("rssi=-42") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("deaf=100") != std::string::npos);
}

void test_a_line_with_no_radio_metadata_says_so_rather_than_printing_zero() {
  FrameLogEntry e = rx_entry(1, 0xF0, 1);
  e.rssi_dbm      = INT16_MIN;  // root rule 6 - a Tx record or a header error
  e.snr_db        = 0;

  char line[160];
  TEST_ASSERT_TRUE(render_line(e, line, sizeof(line)) > 0);
  const std::string s(line);
  TEST_ASSERT_TRUE(s.find("rssi=-") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("rssi=0") == std::string::npos);
}

void test_a_line_is_refused_rather_than_truncated() {
  char line[20];
  TEST_ASSERT_EQUAL_size_t(0, render_line(rx_entry(1234, 0xF0, 77), line, sizeof(line)));
  TEST_ASSERT_EQUAL_size_t(0, std::strlen(line));
}

// ---------------------------------------------------------------------------
// The MQTT batch
// ---------------------------------------------------------------------------

void test_a_batch_is_one_json_object_with_the_records_in_it() {
  FrameLogEntry entries[3] = {rx_entry(10, 0xF0, 1), rx_entry(20, 0xF0, 2),
                              rx_entry(30, 0xF0, 3)};
  char          out[768];
  size_t        consumed = 0;
  const size_t  n        = render_batch(entries, 3, 0, out, sizeof(out), &consumed);

  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_size_t(3, consumed);
  TEST_ASSERT_EQUAL_size_t(n, std::strlen(out));

  const std::string s(out);
  TEST_ASSERT_EQUAL_CHAR('{', s.front());
  TEST_ASSERT_EQUAL_CHAR('}', s.back());
  TEST_ASSERT_TRUE(s.find("\"lost\":0") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("\"seq\":1") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("\"seq\":3") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("\"rssi\":-42") != std::string::npos);
}

void test_an_unavailable_reading_is_null_and_never_a_number() {
  // spec 16.2.1 - a consumer must be able to tell "no reading" from a reading of zero.
  FrameLogEntry e = rx_entry(10, 0x00, 1);
  e.dir           = static_cast<uint8_t>(FrameDir::Tx);
  e.rx            = static_cast<uint8_t>(RxOutcome::Transmitted);
  e.rssi_dbm      = INT16_MIN;
  e.snr_db        = 0;

  char         out[768];
  size_t       consumed = 0;
  TEST_ASSERT_TRUE(render_batch(&e, 1, 0, out, sizeof(out), &consumed) > 0);

  const std::string s(out);
  TEST_ASSERT_TRUE(s.find("\"rssi\":null") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("\"snr\":null") != std::string::npos);
  TEST_ASSERT_TRUE(s.find("\"d\":\"tx\"") != std::string::npos);
}

void test_a_batch_stops_at_the_last_record_that_fits_whole() {
  // A batch too large for the buffer ends early and SAYS how many it took, so the
  // caller re-offers the rest. Nothing is truncated: the object still closes.
  FrameLogEntry entries[32];
  for (size_t i = 0; i < 32; ++i) {
    entries[i] = rx_entry(static_cast<uint32_t>(i), 0xF0, static_cast<lran::Seq>(i));
  }

  char         out[300];
  size_t       consumed = 0;
  const size_t n        = render_batch(entries, 32, 0, out, sizeof(out), &consumed);

  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(consumed > 0);
  TEST_ASSERT_TRUE(consumed < 32);
  TEST_ASSERT_TRUE(n < sizeof(out));
  TEST_ASSERT_EQUAL_size_t(n, std::strlen(out));
  TEST_ASSERT_EQUAL_CHAR('}', out[n - 1]);
  TEST_ASSERT_EQUAL_CHAR(']', out[n - 2]);
}

void test_a_batch_with_no_room_for_one_record_is_refused() {
  FrameLogEntry e = rx_entry(10, 0xF0, 1);
  char          out[40];
  size_t        consumed = 0;
  TEST_ASSERT_EQUAL_size_t(0, render_batch(&e, 1, 0, out, sizeof(out), &consumed));
  TEST_ASSERT_EQUAL_size_t(0, consumed);
  TEST_ASSERT_EQUAL_size_t(0, std::strlen(out));
}

void test_the_batch_reports_what_the_ring_lost() {
  // The count has to reach the reader with the records, not sit in a counter the reader
  // has to go and find on another topic.
  FrameLogEntry e = rx_entry(10, 0xF0, 1);
  char          out[768];
  size_t        consumed = 0;
  TEST_ASSERT_TRUE(render_batch(&e, 1, 17, out, sizeof(out), &consumed) > 0);
  TEST_ASSERT_TRUE(std::string(out).find("\"lost\":17") != std::string::npos);
}

void test_a_drained_batch_round_trips_from_the_ring() {
  // The two halves together: what record() took is what the batch carries.
  FrameLog log;
  for (uint16_t i = 0; i < 4; ++i) log.record(rx_entry(i * 10u, 0xF0, i));

  FrameLogEntry drained[8];
  size_t        n = 0;
  while (n < 8 && log.read_next(&drained[n])) ++n;
  TEST_ASSERT_EQUAL_size_t(4, n);

  char         out[768];
  size_t       consumed = 0;
  TEST_ASSERT_TRUE(render_batch(drained, n, log.lost(), out, sizeof(out), &consumed) > 0);
  TEST_ASSERT_EQUAL_size_t(4, consumed);
  TEST_ASSERT_TRUE(std::string(out).find("\"i\":3") != std::string::npos);
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_an_empty_log_yields_nothing);
  RUN_TEST(test_a_record_comes_back_whole);
  RUN_TEST(test_the_index_is_assigned_by_the_log_not_the_caller);
  RUN_TEST(test_records_come_back_in_order);
  RUN_TEST(test_a_full_ring_is_not_an_overwrite);
  RUN_TEST(test_an_overwritten_record_is_counted_and_the_index_says_where);
  RUN_TEST(test_loss_accumulates_across_reads);
  RUN_TEST(test_recorded_counts_everything_ever_recorded);

  RUN_TEST(test_a_line_carries_the_fields_a_gap_analysis_needs);
  RUN_TEST(test_a_line_with_no_radio_metadata_says_so_rather_than_printing_zero);
  RUN_TEST(test_a_line_is_refused_rather_than_truncated);

  RUN_TEST(test_a_batch_is_one_json_object_with_the_records_in_it);
  RUN_TEST(test_an_unavailable_reading_is_null_and_never_a_number);
  RUN_TEST(test_a_batch_stops_at_the_last_record_that_fits_whole);
  RUN_TEST(test_a_batch_with_no_room_for_one_record_is_refused);
  RUN_TEST(test_the_batch_reports_what_the_ring_lost);
  RUN_TEST(test_a_drained_batch_round_trips_from_the_ring);

  return UNITY_END();
}
