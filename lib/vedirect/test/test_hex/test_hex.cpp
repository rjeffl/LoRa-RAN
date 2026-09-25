// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The HEX frame codec against the worked examples in Victron's "BlueSolar HEX protocol"
// document, section 1.3. Those strings are the only test vectors that did not come from
// this code, so every one of them is here.

#include <cstring>

#include <unity.h>

#include "vedirect/hex.h"

using namespace vedirect;

void setUp() {}
void tearDown() {}

namespace {

Parse decode_str(const char* s, Frame* f) { return decode(s, std::strlen(s), f); }

void assert_encodes(const char* want, size_t n) {
  TEST_ASSERT_EQUAL_size_t(std::strlen(want), n);
}

}  // namespace

void test_victron_examples_decode() {
  const char* examples[] = {
      ":154",             // ping
      ":51641F9",         // ping response, application 1.16
      ":352",             // application version
      ":11641FD",         // its answer
      ":451",             // product id
      ":1000351",         // 0x0300
      ":64F",             // restart
      ":7F0ED0071",       // get battery maximum current
      ":7F0ED009600DB",   // 15.0 A
      ":8F0ED0064000C",   // set to 10.0 A, and the acknowledgement
      ":4AAAAFD",         // error response
      ":A0102000543",     // async, device state float
  };
  for (const char* s : examples) {
    Frame f;
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ok", parse_name(decode_str(s, &f)), s);
  }
}

void test_get_encodes_as_victron_writes_it() {
  char   out[kMaxChars];
  size_t n = encode_get(0xEDF0, out, sizeof(out));
  assert_encodes(":7F0ED0071", n);
  TEST_ASSERT_EQUAL_MEMORY(":7F0ED0071", out, n);
}

void test_set_encodes_as_victron_writes_it() {
  char   out[kMaxChars];
  size_t n = encode_set(0xEDF0, 100, 2, out, sizeof(out));
  assert_encodes(":8F0ED0064000C", n);
  TEST_ASSERT_EQUAL_MEMORY(":8F0ED0064000C", out, n);
}

void test_ping_and_restart_encode() {
  char   out[kMaxChars];
  size_t n = encode(static_cast<uint8_t>(HexCmd::Ping), nullptr, 0, out, sizeof(out));
  TEST_ASSERT_EQUAL_MEMORY(":154", out, n);
  n = encode(static_cast<uint8_t>(HexCmd::Restart), nullptr, 0, out, sizeof(out));
  TEST_ASSERT_EQUAL_MEMORY(":64F", out, n);
}

void test_get_reply_reads_register_flags_and_value() {
  Frame f;
  TEST_ASSERT_EQUAL(Parse::Ok, decode_str(":7F0ED009600DB", &f));
  RegReply r;
  TEST_ASSERT_TRUE(reg_reply(f, &r));
  TEST_ASSERT_EQUAL_HEX16(0xEDF0, r.reg);
  TEST_ASSERT_EQUAL_HEX8(0, r.flags);
  TEST_ASSERT_EQUAL_UINT8(2, r.width);
  TEST_ASSERT_EQUAL_UINT32(150, r.value);
}

void test_async_and_ping_replies_are_not_register_replies() {
  Frame    f;
  RegReply r;
  TEST_ASSERT_EQUAL(Parse::Ok, decode_str(":A0102000543", &f));
  TEST_ASSERT_FALSE(reg_reply(f, &r));
  TEST_ASSERT_EQUAL(Parse::Ok, decode_str(":51641F9", &f));
  TEST_ASSERT_FALSE(reg_reply(f, &r));
}

void test_a_wrong_check_byte_is_refused() {
  Frame f;
  TEST_ASSERT_EQUAL(Parse::BadChecksum, decode_str(":7F0ED0072", &f));
}

// Victron section 1: digits "must be uppercase". A lowercase frame is refused here, not
// passed to an MPPT that would answer it with a frame error.
void test_lowercase_is_refused() {
  Frame f;
  TEST_ASSERT_EQUAL(Parse::BadChar, decode_str(":7f0ed0071", &f));
}

void test_malformed_shapes_are_named() {
  Frame f;
  TEST_ASSERT_EQUAL(Parse::NoColon, decode_str("7F0ED0071", &f));
  TEST_ASSERT_EQUAL(Parse::TooShort, decode_str(":15", &f));
  TEST_ASSERT_EQUAL(Parse::OddLength, decode_str(":7F0ED007", &f));
  TEST_ASSERT_EQUAL(Parse::BadChar, decode_str(":7F0ED0071\n", &f));
}

void test_too_long_is_refused_by_both_directions() {
  uint8_t data[kMaxData + 1] = {0};
  char    out[kMaxChars + 2];
  TEST_ASSERT_EQUAL_size_t(0, encode(7, data, sizeof(data), out, sizeof(out)));

  // kMaxData + 1 data bytes and a check, all zero but a check that balances them.
  char   s[2 + 2 * (kMaxData + 2) + 1];
  size_t n = 0;
  s[n++] = ':';
  s[n++] = '7';
  for (size_t i = 0; i < kMaxData + 1; ++i) { s[n++] = '0'; s[n++] = '0'; }
  s[n++] = '4';
  s[n++] = 'E';
  Frame f;
  TEST_ASSERT_EQUAL(Parse::TooLong, decode(s, n, &f));
}

void test_encode_refuses_a_small_buffer_and_a_wide_command() {
  char out[4];
  TEST_ASSERT_EQUAL_size_t(0, encode_get(0xEDF0, out, sizeof(out)));
  char big[kMaxChars];
  TEST_ASSERT_EQUAL_size_t(0, encode(0x10, nullptr, 0, big, sizeof(big)));
  TEST_ASSERT_EQUAL_size_t(0, encode_set(0xEDF0, 1, 3, big, sizeof(big)));
}

void test_round_trip_of_a_signed_value() {
  // 0xEDF2 temperature compensation is sn16 in 0.01 mV/K; -16.20 is 0xF9AC.
  char   out[kMaxChars];
  size_t n = encode_set(0xEDF2, static_cast<uint16_t>(-1620), 2, out, sizeof(out));
  Frame  f;
  TEST_ASSERT_EQUAL(Parse::Ok, decode(out, n, &f));
  RegReply r;
  TEST_ASSERT_TRUE(reg_reply(f, &r));
  TEST_ASSERT_EQUAL_INT16(-1620, static_cast<int16_t>(r.value));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_victron_examples_decode);
  RUN_TEST(test_get_encodes_as_victron_writes_it);
  RUN_TEST(test_set_encodes_as_victron_writes_it);
  RUN_TEST(test_ping_and_restart_encode);
  RUN_TEST(test_get_reply_reads_register_flags_and_value);
  RUN_TEST(test_async_and_ping_replies_are_not_register_replies);
  RUN_TEST(test_a_wrong_check_byte_is_refused);
  RUN_TEST(test_lowercase_is_refused);
  RUN_TEST(test_malformed_shapes_are_named);
  RUN_TEST(test_too_long_is_refused_by_both_directions);
  RUN_TEST(test_encode_refuses_a_small_buffer_and_a_wide_command);
  RUN_TEST(test_round_trip_of_a_signed_value);
  return UNITY_END();
}
