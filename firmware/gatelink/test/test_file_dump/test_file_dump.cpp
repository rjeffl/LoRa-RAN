// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The console's `file` command: the CRC and the tail's start.

#include <unity.h>

#include <cstring>

#include "file_dump.h"

using gatelink::Crc32;
using gatelink::TailScan;

void setUp() {}
void tearDown() {}

namespace {

// Runs the scan over `text` in chunks of `chunk`, as log_task does over the card.
uint32_t tail_start(const char* text, uint32_t lines, size_t chunk) {
  const uint32_t size = static_cast<uint32_t>(std::strlen(text));
  TailScan       scan(size, lines);
  uint32_t       offset = 0;
  size_t         n;
  while ((n = scan.next_chunk(chunk, &offset)) != 0) {
    if (scan.feed(reinterpret_cast<const uint8_t*>(text) + offset, n)) break;
  }
  return scan.start();
}

}  // namespace

// zlib's check value, so a saved dump verifies with Python's zlib.crc32.
void test_crc_matches_zlib() {
  Crc32 c;
  c.update(reinterpret_cast<const uint8_t*>("123456789"), 9);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, c.value());
}

void test_crc_is_the_same_across_chunks() {
  Crc32 c;
  c.update(reinterpret_cast<const uint8_t*>("1234"), 4);
  c.update(reinterpret_cast<const uint8_t*>("56789"), 5);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, c.value());
}

void test_tail_finds_the_last_lines() {
  const char* t = "one\ntwo\nthree\n";
  TEST_ASSERT_EQUAL_UINT32(8, tail_start(t, 1, 512));
  TEST_ASSERT_EQUAL_UINT32(4, tail_start(t, 2, 512));
  TEST_ASSERT_EQUAL_UINT32(0, tail_start(t, 3, 512));
}

// A chunk boundary falls anywhere, a newline included.
void test_tail_is_the_same_for_any_chunk_size() {
  const char* t = "one\ntwo\nthree\n";
  for (size_t chunk = 1; chunk < 16; ++chunk) {
    TEST_ASSERT_EQUAL_UINT32(4, tail_start(t, 2, chunk));
  }
}

void test_tail_asking_for_too_many_is_the_whole_file() {
  TEST_ASSERT_EQUAL_UINT32(0, tail_start("one\ntwo\n", 50, 3));
}

// A file cut off mid-line still has that partial line as its last.
void test_tail_counts_an_unterminated_last_line() {
  TEST_ASSERT_EQUAL_UINT32(4, tail_start("one\ntwo", 1, 512));
}

void test_tail_of_an_empty_file() {
  TEST_ASSERT_EQUAL_UINT32(0, tail_start("", 5, 512));
}

void test_fault_line() {
  char out[32];
  TEST_ASSERT_EQUAL_size_t(24, gatelink::format_fault_line(out, sizeof(out), 42, "boot cause=4"));
  TEST_ASSERT_EQUAL_STRING("uptime_s=42 boot cause=4", out);
}

void test_fault_line_that_does_not_fit() {
  char out[8];
  TEST_ASSERT_EQUAL_size_t(0, gatelink::format_fault_line(out, sizeof(out), 42, "boot"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc_matches_zlib);
  RUN_TEST(test_crc_is_the_same_across_chunks);
  RUN_TEST(test_tail_finds_the_last_lines);
  RUN_TEST(test_tail_is_the_same_for_any_chunk_size);
  RUN_TEST(test_tail_asking_for_too_many_is_the_whole_file);
  RUN_TEST(test_tail_counts_an_unterminated_last_line);
  RUN_TEST(test_tail_of_an_empty_file);
  RUN_TEST(test_fault_line);
  RUN_TEST(test_fault_line_that_does_not_fit);
  return UNITY_END();
}
