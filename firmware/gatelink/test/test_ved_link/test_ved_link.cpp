// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// VedLink: the text cache into spec 7.2.2, staleness (spec 7.2.6 bit 1, PRD R-3.3f) and one
// HEX transaction at a time (Impl Plan 4.2.4, PRD R-3.3d). GL4.
//
// kCaptured is lib/vedirect's capture of the gate's 75/15 (test_text.cpp), read on
// 2026-10-08 on a bench supply with no PV.

#include <cstring>
#include <string>

#include <unity.h>

#include "ved_link.h"

using gatelink::HexJob;
using gatelink::HexResult;
using gatelink::MpptSnapshot;
using gatelink::VedLink;
using gatelink::VedParams;
using lran::HexStatus;

void setUp() {}
void tearDown() {}

namespace {

const char kCaptured[] =
    "\r\nPID\t0xA075\r\nFW\t175\r\nSER#\tHQ25492XRJM\r\nV\t13360\r\nI\t-40\r\nVPV\t10"
    "\r\nPPV\t0\r\nCS\t0\r\nMPPT\t0\r\nOR\t0x00000001\r\nERR\t0\r\nLOAD\tON\r\nIL\t0"
    "\r\nH19\t467\r\nH20\t0\r\nH21\t0\r\nH22\t3\r\nH23\t26\r\nHSDS\t59\r\nChecksum\t\xDC";

class FakeUart final : public gatelink::VedWriter {
 public:
  bool write_line(const char* s, size_t n) override {
    ++writes;
    last.assign(s, n);
    return !fail;
  }
  int         writes = 0;
  std::string last;
  bool        fail   = false;
};

struct Rig {
  FakeUart uart;
  VedLink  link{&uart};

  void feed(const std::string& s, uint32_t now) {
    for (char c : s) link.feed(static_cast<uint8_t>(c), now);
  }
  // Feeds the parser past its first, unsynced, boundary so kCaptured is read whole.
  void sync(uint32_t now) { feed("\r\nChecksum\tX", now); }

  void start(const char* hex, uint32_t now, uint32_t token = 1) {
    HexJob j;
    j.token = token;
    j.n     = std::strlen(hex);
    std::memcpy(j.hex, hex, j.n);
    TEST_ASSERT_TRUE(link.start(j, now));
  }

  HexResult result() {
    HexResult r;
    TEST_ASSERT_TRUE(link.take_result(&r));
    return r;
  }
};

// A Get reply for `reg`: register, flags 0, then a two-byte value.
std::string get_reply(uint16_t reg, uint16_t value) {
  const uint8_t d[] = {static_cast<uint8_t>(reg), static_cast<uint8_t>(reg >> 8), 0,
                       static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
  char          out[vedirect::kMaxChars];
  const size_t  n = vedirect::encode(0x7, d, sizeof(d), out, sizeof(out));
  return std::string(out, n) + "\n";
}

std::string async_frame() {
  const uint8_t d[] = {0x01, 0x02, 0x00, 0x00};
  char          out[vedirect::kMaxChars];
  const size_t  n = vedirect::encode(0xA, d, sizeof(d), out, sizeof(out));
  return std::string(out, n) + "\n";
}

std::string get_req(uint16_t reg) {
  char         out[vedirect::kMaxChars];
  const size_t n = vedirect::encode_get(reg, out, sizeof(out));
  return std::string(out, n);
}

}  // namespace

// Before any block, every field is its sentinel and bit 1 is set (root rule 6).
void test_no_block_is_stale_with_sentinels() {
  Rig                r;
  const MpptSnapshot s = r.link.mppt(0);
  TEST_ASSERT_EQUAL_UINT16(lran::kU16NotAvailable, s.batt_mv);
  TEST_ASSERT_EQUAL_UINT32(lran::kU32NotAvailable, s.yield_total);
  TEST_ASSERT_EQUAL_HEX8(gatelink::kMpptFlagStale, s.mppt_flags);
}

// spec 7.2.2 - the 75/15's own block, scaled.
void test_captured_block_fills_the_mppt_fields() {
  Rig r;
  r.sync(0);
  r.feed(kCaptured, 1000);
  const MpptSnapshot s = r.link.mppt(1000);
  TEST_ASSERT_EQUAL_UINT16(13360, s.batt_mv);
  TEST_ASSERT_EQUAL_INT16(-40, s.batt_ma);
  TEST_ASSERT_EQUAL_UINT16(1, s.pv_cv);  // VPV 10 mV
  TEST_ASSERT_EQUAL_UINT16(0, s.pv_w);
  TEST_ASSERT_EQUAL_INT16(0, s.load_ma);
  TEST_ASSERT_EQUAL_UINT32(467, s.yield_total);
  TEST_ASSERT_EQUAL_UINT16(0, s.yield_today);
  TEST_ASSERT_EQUAL_UINT16(3, s.yield_yest);
  TEST_ASSERT_EQUAL_UINT16(0, s.pmax_today);
  TEST_ASSERT_EQUAL_UINT8(0, s.charge_state);
  TEST_ASSERT_EQUAL_UINT8(0, s.mppt_err);
  TEST_ASSERT_EQUAL_UINT8(0, s.mppt_tracker);
  TEST_ASSERT_EQUAL_INT16(lran::kI16NotAvailable, s.mppt_temp_c10);
  TEST_ASSERT_EQUAL_HEX8(gatelink::kMpptFlagLoadOn, s.mppt_flags);
  TEST_ASSERT_EQUAL_UINT32(0, r.link.counters().blocks_unparsed);
}

// A value its field cannot hold reads as the sentinel, not a clipped number.
void test_out_of_range_reads_as_sentinel() {
  vedirect::MpptText m;
  m.batt_ma = 40000;
  m.pv_mv   = 800000;
  m.err     = 300;
  const MpptSnapshot s = gatelink::mppt_from_text(m);
  TEST_ASSERT_EQUAL_INT16(lran::kI16NotAvailable, s.batt_ma);
  TEST_ASSERT_EQUAL_UINT16(lran::kU16NotAvailable, s.pv_cv);
  TEST_ASSERT_EQUAL_UINT8(gatelink::kCodeNotAvailable, s.mppt_err);
}

// PRD R-3.3f - bit 1 asserts vedirect_stale_s after the last block, and the values stay.
void test_stale_after_vedirect_stale_s() {
  Rig r;
  r.link.set_params(VedParams{1000, 5000});
  r.sync(0);
  r.feed(kCaptured, 1000);
  TEST_ASSERT_EQUAL_HEX8(0, r.link.mppt(5999).mppt_flags & gatelink::kMpptFlagStale);
  const MpptSnapshot s = r.link.mppt(6000);
  TEST_ASSERT_EQUAL_HEX8(gatelink::kMpptFlagStale, s.mppt_flags & gatelink::kMpptFlagStale);
  TEST_ASSERT_EQUAL_UINT16(13360, s.batt_mv);
}

// Impl Plan 4.2.4 - the reply is matched by its register, Async frames and replies for
// another register are counted and passed over, and bit 2 is set while the Get waits.
void test_get_matched_by_register_among_async() {
  Rig r;
  r.start(get_req(0x0100).c_str(), 0);
  TEST_ASSERT_EQUAL_INT(1, r.uart.writes);
  TEST_ASSERT_EQUAL_STRING(get_req(0x0100).c_str(), r.uart.last.c_str());
  TEST_ASSERT_EQUAL_HEX8(gatelink::kMpptFlagHexPending,
                         r.link.mppt(0).mppt_flags & gatelink::kMpptFlagHexPending);
  r.feed(async_frame() + get_reply(0xEDF0, 150), 10);
  HexResult res;
  TEST_ASSERT_FALSE(r.link.take_result(&res));
  const std::string want = get_reply(0x0100, 0xA075);
  r.feed(want, 12);
  res = r.result();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Ok), static_cast<uint8_t>(res.status));
  TEST_ASSERT_EQUAL_UINT32(1, res.token);
  TEST_ASSERT_EQUAL_STRING(want.substr(0, want.size() - 1).c_str(), std::string(res.hex, res.n).c_str());
  TEST_ASSERT_EQUAL_UINT32(1, r.link.counters().hex_async);
  TEST_ASSERT_EQUAL_UINT32(1, r.link.counters().hex_unmatched);
  TEST_ASSERT_FALSE(r.link.busy());
}

// A history record's reply carries 34 bytes after the flags, past reg_reply()'s 4, and
// is still the answer to its Get (engineering log, 2026-10-09).
void test_get_of_a_wide_register_is_matched() {
  Rig r;
  r.start(get_req(0x1050).c_str(), 0);
  uint8_t d[3 + 34] = {0x50, 0x10, 0x00};
  for (size_t i = 3; i < sizeof(d); ++i) d[i] = static_cast<uint8_t>(i);
  char         out[vedirect::kMaxChars];
  const size_t n = vedirect::encode(0x7, d, sizeof(d), out, sizeof(out));
  r.feed(std::string(out, n) + "\n", 10);
  const HexResult res = r.result();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Ok), static_cast<uint8_t>(res.status));
  TEST_ASSERT_EQUAL_STRING(std::string(out, n).c_str(), std::string(res.hex, res.n).c_str());
  TEST_ASSERT_EQUAL_UINT32(0, r.link.counters().hex_unmatched);
}

// A Get unanswered by half of hex_timeout_ms is sent once more, inside the same wait.
void test_get_retried_once_at_half_the_wait() {
  Rig r;
  r.link.set_params(VedParams{1000, 5000});
  r.start(get_req(0x0100).c_str(), 0);
  r.link.tick(499);
  TEST_ASSERT_EQUAL_INT(1, r.uart.writes);
  r.link.tick(500);
  TEST_ASSERT_EQUAL_INT(2, r.uart.writes);
  r.link.tick(900);
  TEST_ASSERT_EQUAL_INT(2, r.uart.writes);
  r.feed(get_reply(0x0100, 0xA075), 910);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Ok),
                          static_cast<uint8_t>(r.result().status));
  TEST_ASSERT_EQUAL_UINT32(1, r.link.counters().hex_retries);
}

// PRD R-3.3d - TIMEOUT, not silence, at hex_timeout_ms.
void test_timeout_at_hex_timeout_ms() {
  Rig r;
  r.link.set_params(VedParams{1000, 5000});
  r.start(get_req(0x0100).c_str(), 0);
  r.link.tick(500);
  r.link.tick(999);
  HexResult res;
  TEST_ASSERT_FALSE(r.link.take_result(&res));
  r.link.tick(1000);
  res = r.result();
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Timeout), static_cast<uint8_t>(res.status));
  TEST_ASSERT_EQUAL_size_t(0, res.n);
  TEST_ASSERT_FALSE(r.link.busy());
}

// Only a Get is retried. A Set changes the MPPT, so it goes once.
void test_set_is_not_retried() {
  Rig          r;
  char         set[vedirect::kMaxChars + 1] = {0};
  vedirect::encode_set(0xEDF7, 1420, 2, set, sizeof(set));
  r.start(set, 0);
  r.link.tick(500);
  TEST_ASSERT_EQUAL_INT(1, r.uart.writes);
  r.link.tick(1000);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Timeout),
                          static_cast<uint8_t>(r.result().status));
}

// One transaction at a time: a second job is refused while the first waits.
void test_second_job_refused_while_one_waits() {
  Rig r;
  r.start(get_req(0x0100).c_str(), 0);
  HexJob j;
  j.n = 4;
  std::memcpy(j.hex, ":154", 4);
  TEST_ASSERT_FALSE(r.link.start(j, 1));
  TEST_ASSERT_EQUAL_INT(1, r.uart.writes);
}

// Victron - a Restart is never answered, so it completes once written.
void test_restart_completes_when_written() {
  Rig r;
  r.start(":64F", 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::Ok),
                          static_cast<uint8_t>(r.result().status));
  TEST_ASSERT_FALSE(r.link.busy());
}

// A Ping is answered by command 5; an Unknown reply answers any request.
void test_ping_and_unknown_replies() {
  Rig r;
  r.start(":154", 0, 7);
  r.feed(":51641F9\n", 5);
  HexResult res = r.result();
  TEST_ASSERT_EQUAL_UINT32(7, res.token);
  TEST_ASSERT_EQUAL_STRING(":51641F9", std::string(res.hex, res.n).c_str());
  r.start(":B", 10, 8);  // command B is not Victron's; the MPPT answers Unknown
  r.feed(":30B47\n", 15);
  res = r.result();
  TEST_ASSERT_EQUAL_UINT32(8, res.token);
  TEST_ASSERT_EQUAL_STRING(":30B47", std::string(res.hex, res.n).c_str());
}

// A request the UART did not take whole answers UART_ERROR.
void test_uart_failure_is_uart_error() {
  Rig r;
  r.uart.fail = true;
  r.start(":154", 0);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(HexStatus::UartError),
                          static_cast<uint8_t>(r.result().status));
  TEST_ASSERT_EQUAL_UINT32(1, r.link.counters().hex_uart_errors);
}

// A reply with no transaction outstanding is counted, never delivered.
void test_reply_with_nothing_outstanding_is_counted() {
  Rig r;
  r.feed(get_reply(0x0100, 0xA075), 0);
  HexResult res;
  TEST_ASSERT_FALSE(r.link.take_result(&res));
  TEST_ASSERT_EQUAL_UINT32(1, r.link.counters().hex_unmatched);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_no_block_is_stale_with_sentinels);
  RUN_TEST(test_captured_block_fills_the_mppt_fields);
  RUN_TEST(test_out_of_range_reads_as_sentinel);
  RUN_TEST(test_stale_after_vedirect_stale_s);
  RUN_TEST(test_get_matched_by_register_among_async);
  RUN_TEST(test_get_of_a_wide_register_is_matched);
  RUN_TEST(test_get_retried_once_at_half_the_wait);
  RUN_TEST(test_timeout_at_hex_timeout_ms);
  RUN_TEST(test_set_is_not_retried);
  RUN_TEST(test_second_job_refused_while_one_waits);
  RUN_TEST(test_restart_completes_when_written);
  RUN_TEST(test_ping_and_unknown_replies);
  RUN_TEST(test_uart_failure_is_uart_error);
  RUN_TEST(test_reply_with_nothing_outstanding_is_counted);
  return UNITY_END();
}
