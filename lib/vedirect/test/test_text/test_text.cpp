// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The text parser and its HEX multiplexer (GateLink Impl Plan 4.2.4, task L3).
//
// NOT A CAPTURE. L3's criterion asks for a captured MPPT 75/15 block, and none exists yet:
// the MPPT is at the gate, and osh-labs' own tests synthesize theirs. kMppt7515 is built
// from osh-labs' sample block, with H19 and H21 added from spec 7.2.2 and the FW and SER#
// labels osh-labs names. OR and H23 are left out because neither source defines them.
// Replace it with a real capture at GL4, and keep the expected values as the capture reads.

#include <cstdio>
#include <cstring>
#include <string>

#include <unity.h>

#include "vedirect/hex.h"
#include "vedirect/text.h"

using namespace vedirect;

void setUp() {}
void tearDown() {}

namespace {

struct Rec {
  const char* label;
  const char* value;
};

const Rec kMppt7515[] = {
    {"PID", "0xA053"}, {"FW", "159"},   {"SER#", "HQ0000TEST"}, {"V", "13280"},
    {"I", "1200"},     {"VPV", "36540"}, {"PPV", "21"},          {"CS", "3"},
    {"MPPT", "2"},     {"ERR", "0"},     {"LOAD", "ON"},         {"IL", "300"},
    {"H19", "4521"},   {"H20", "12"},    {"H21", "64"},          {"H22", "34"},
    {"HSDS", "77"},
};
constexpr size_t kMpptCount = sizeof(kMppt7515) / sizeof(kMppt7515[0]);

// The block as the MPPT sends it: each record opened by "\r\n", then the checksum record
// and the byte that brings the sum to zero.
std::string make_block(const Rec* recs, size_t n) {
  std::string s;
  for (size_t i = 0; i < n; ++i) {
    s += "\r\n";
    s += recs[i].label;
    s += '\t';
    s += recs[i].value;
  }
  s += "\r\nChecksum\t";
  uint8_t sum = 0;
  for (char c : s) sum = static_cast<uint8_t>(sum + static_cast<uint8_t>(c));
  s += static_cast<char>(static_cast<uint8_t>(0x100 - sum));
  return s;
}

std::string mppt_block() { return make_block(kMppt7515, kMpptCount); }

struct Tally {
  int blocks  = 0;
  int dropped = 0;
  int hex     = 0;
};

Tally feed_all(TextParser& p, const std::string& s) {
  Tally t;
  for (char c : s) {
    switch (p.feed(static_cast<uint8_t>(c))) {
      case TextEvent::Block:   ++t.blocks; break;
      case TextEvent::Dropped: ++t.dropped; break;
      case TextEvent::HexLine: ++t.hex; break;
      case TextEvent::None:    break;
    }
  }
  return t;
}

// A block whose checksum byte is `want`, found by varying H19. ':', '\t', '\r' and
// '\n' are all reached by 1029.
std::string block_with_checksum(char want) {
  static char h19[8];
  Rec recs[kMpptCount];
  std::memcpy(recs, kMppt7515, sizeof(recs));
  for (int h = 0; h < 10000; ++h) {
    std::snprintf(h19, sizeof(h19), "%d", h);
    recs[12].value = h19;
    std::string s = make_block(recs, kMpptCount);
    if (s.back() == want) return s;
  }
  TEST_FAIL_MESSAGE("no H19 gives that checksum byte");
  return {};
}

}  // namespace

void test_every_field_of_the_block() {
  TextParser p;
  Tally t = feed_all(p, mppt_block());
  TEST_ASSERT_EQUAL_INT(1, t.blocks);
  TEST_ASSERT_EQUAL_INT(0, t.dropped);

  const TextBlock& b = p.block();
  TEST_ASSERT_EQUAL_size_t(kMpptCount, b.count);
  for (size_t i = 0; i < kMpptCount; ++i) {
    TEST_ASSERT_EQUAL_STRING(kMppt7515[i].label, b.fields[i].label);
    TEST_ASSERT_EQUAL_STRING(kMppt7515[i].value, b.fields[i].value);
  }
  TEST_ASSERT_NULL(b.find("Checksum"));
  TEST_ASSERT_EQUAL_UINT32(1, p.counters().blocks);
}

void test_decode_mppt() {
  TextParser p;
  feed_all(p, mppt_block());
  MpptText m;
  TEST_ASSERT_EQUAL_size_t(0, decode_mppt(p.block(), &m));
  TEST_ASSERT_EQUAL_HEX16(0xA053, m.pid);
  TEST_ASSERT_EQUAL_UINT32(13280, m.batt_mv);
  TEST_ASSERT_EQUAL_INT32(1200, m.batt_ma);
  TEST_ASSERT_EQUAL_UINT32(36540, m.pv_mv);
  TEST_ASSERT_EQUAL_UINT32(21, m.pv_w);
  TEST_ASSERT_EQUAL_INT32(300, m.load_ma);
  TEST_ASSERT_TRUE(m.load == LoadState::On);
  TEST_ASSERT_EQUAL_UINT16(3, m.charge_state);
  TEST_ASSERT_EQUAL_UINT16(2, m.tracker);
  TEST_ASSERT_EQUAL_UINT16(0, m.err);
  TEST_ASSERT_EQUAL_UINT32(4521, m.yield_total);
  TEST_ASSERT_EQUAL_UINT32(12, m.yield_today);
  TEST_ASSERT_EQUAL_UINT32(64, m.pmax_today);
  TEST_ASSERT_EQUAL_UINT32(34, m.yield_yest);
  TEST_ASSERT_EQUAL_UINT16(77, m.day_seq);
  TEST_ASSERT_EQUAL_STRING("HQ0000TEST", p.block().find("SER#"));
}

void test_decode_sentinels_and_malformed() {
  const Rec recs[] = {{"I", "-1500"}, {"V", "13x"}, {"LOAD", "MAYBE"}, {"Hsds", "5"},
                      {"PID", "A053"}};
  TextParser p;
  TEST_ASSERT_EQUAL_INT(1, feed_all(p, make_block(recs, 5)).blocks);
  MpptText m;
  TEST_ASSERT_EQUAL_size_t(3, decode_mppt(p.block(), &m));
  TEST_ASSERT_EQUAL_INT32(-1500, m.batt_ma);
  TEST_ASSERT_EQUAL_UINT32(kU32NotAvailable, m.batt_mv);
  TEST_ASSERT_TRUE(m.load == LoadState::NotAvailable);
  TEST_ASSERT_EQUAL_UINT16(5, m.day_seq);
  TEST_ASSERT_EQUAL_UINT16(kU16NotAvailable, m.pid);
  TEST_ASSERT_EQUAL_UINT32(kU32NotAvailable, m.pv_mv);  // absent
  TEST_ASSERT_EQUAL_INT32(kI32NotAvailable, m.load_ma);
}

void test_bad_checksum_rejected_and_counted() {
  TextParser p;
  feed_all(p, mppt_block());
  std::string bad = mppt_block();
  bad[10] = static_cast<char>(bad[10] + 1);
  Tally t = feed_all(p, bad);
  TEST_ASSERT_EQUAL_INT(0, t.blocks);
  TEST_ASSERT_EQUAL_INT(1, t.dropped);
  TEST_ASSERT_TRUE(p.last_drop() == TextDrop::BadChecksum);
  TEST_ASSERT_EQUAL_UINT32(1, p.counters().bad_checksum);
  // The last good block stays readable.
  TEST_ASSERT_EQUAL_size_t(kMpptCount, p.block().count);
  TEST_ASSERT_EQUAL_UINT32(1, p.counters().blocks);
}

void test_joining_mid_block_is_unsynced_not_bad() {
  TextParser p;
  std::string s = mppt_block();
  Tally t = feed_all(p, s.substr(s.size() / 2) + s);
  TEST_ASSERT_EQUAL_INT(1, t.dropped);
  TEST_ASSERT_EQUAL_INT(1, t.blocks);
  TEST_ASSERT_EQUAL_UINT32(1, p.counters().unsynced);
  TEST_ASSERT_EQUAL_UINT32(0, p.counters().bad_checksum);
}

void test_hex_between_blocks() {
  TextParser p;
  Tally t = feed_all(p, mppt_block() + ":7F0ED009600DB\n" + mppt_block());
  TEST_ASSERT_EQUAL_INT(2, t.blocks);
  TEST_ASSERT_EQUAL_INT(1, t.hex);
  TEST_ASSERT_EQUAL_INT(0, t.dropped);
  TEST_ASSERT_EQUAL_STRING(":7F0ED009600DB", p.hex_line());

  // Passed through to the HEX codec unchanged.
  Frame f;
  TEST_ASSERT_TRUE(decode(p.hex_line(), p.hex_len(), &f) == Parse::Ok);
  RegReply r;
  TEST_ASSERT_TRUE(reg_reply(f, &r));
  TEST_ASSERT_EQUAL_HEX16(0xEDF0, r.reg);
  TEST_ASSERT_EQUAL_UINT32(150, r.value);
}

void test_hex_inside_a_block_resumes_it() {
  TextParser p;
  std::string s = mppt_block();
  const size_t mid = s.find("VPV\t") + 6;  // inside the value
  s.insert(mid, ":11641FD\r\n");
  Tally t = feed_all(p, s);
  TEST_ASSERT_EQUAL_INT(1, t.hex);
  TEST_ASSERT_EQUAL_INT(1, t.blocks);
  TEST_ASSERT_EQUAL_STRING("36540", p.block().find("VPV"));
  TEST_ASSERT_EQUAL_STRING(":11641FD", p.hex_line());
}

void test_interrupted_block_that_fails_is_counted_apart() {
  TextParser p;
  feed_all(p, mppt_block());
  std::string s = mppt_block();
  s.insert(s.find("PPV"), ":154\n");
  s[s.find("13280")] = '2';
  feed_all(p, s);
  TEST_ASSERT_TRUE(p.last_drop() == TextDrop::Interrupted);
  TEST_ASSERT_EQUAL_UINT32(1, p.counters().interrupted);
  TEST_ASSERT_EQUAL_UINT32(0, p.counters().bad_checksum);
}

void test_checksum_byte_colon_and_newline() {
  for (char c : {':', '\n', '\t', '\r'}) {
    TextParser p;
    std::string s = block_with_checksum(c);
    Tally t = feed_all(p, s + mppt_block());
    TEST_ASSERT_EQUAL_INT(2, t.blocks);
    TEST_ASSERT_EQUAL_INT(0, t.hex);
    TEST_ASSERT_EQUAL_INT(0, t.dropped);
  }
}

void test_overflow_counted() {
  const std::string long_value(kValueChars + 1, '7');
  const Rec too_long[] = {{"V", "13280"}, {"PID", long_value.c_str()}};
  TextParser p;
  Tally t = feed_all(p, make_block(too_long, 2));
  TEST_ASSERT_EQUAL_INT(1, t.dropped);
  TEST_ASSERT_TRUE(p.last_drop() == TextDrop::Overflow);

  Rec many[kMaxFields + 1];
  for (auto& r : many) r = {"V", "1"};
  t = feed_all(p, make_block(many, kMaxFields + 1));
  TEST_ASSERT_EQUAL_INT(1, t.dropped);
  TEST_ASSERT_EQUAL_UINT32(2, p.counters().overflow);

  Rec exact[kMaxFields];
  for (auto& r : exact) r = {"V", "1"};
  TEST_ASSERT_EQUAL_INT(1, feed_all(p, make_block(exact, kMaxFields)).blocks);
}

void test_hex_too_long_dropped() {
  TextParser p;
  std::string line = ":" + std::string(kMaxChars, 'A') + "\n";
  Tally t = feed_all(p, line + mppt_block());
  TEST_ASSERT_EQUAL_INT(0, t.hex);
  TEST_ASSERT_EQUAL_INT(1, t.dropped);
  TEST_ASSERT_EQUAL_INT(1, t.blocks);
  TEST_ASSERT_EQUAL_UINT32(1, p.counters().hex_too_long);
  TEST_ASSERT_EQUAL_size_t(0, p.hex_len());
}

void test_reset_keeps_block_and_counters() {
  TextParser p;
  feed_all(p, mppt_block());
  p.reset();
  TEST_ASSERT_EQUAL_size_t(kMpptCount, p.block().count);
  TEST_ASSERT_EQUAL_UINT32(1, p.counters().blocks);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_every_field_of_the_block);
  RUN_TEST(test_decode_mppt);
  RUN_TEST(test_decode_sentinels_and_malformed);
  RUN_TEST(test_bad_checksum_rejected_and_counted);
  RUN_TEST(test_joining_mid_block_is_unsynced_not_bad);
  RUN_TEST(test_hex_between_blocks);
  RUN_TEST(test_hex_inside_a_block_resumes_it);
  RUN_TEST(test_interrupted_block_that_fails_is_counted_apart);
  RUN_TEST(test_checksum_byte_colon_and_newline);
  RUN_TEST(test_overflow_counted);
  RUN_TEST(test_hex_too_long_dropped);
  RUN_TEST(test_reset_keeps_block_and_counters);
  return UNITY_END();
}
