// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-11a - the leveled log's formatting. The queue and log_task's drain are FreeRTOS
// and are not reached from here; what a reader sees on the serial line is.

#include <unity.h>

#include <cstring>
#include <string>

#include "log.h"

using namespace bridge;

namespace {

bool fmt(LogMessage* m, LogLevel level, const char* f, ...) {
  va_list ap;
  va_start(ap, f);
  const bool ok = log_format(m, level, f, ap);
  va_end(ap);
  return ok;
}

}  // namespace

void setUp() {}
void tearDown() {}

// The call sites this replaced ended their formats in "\n", and log_task ends every
// line itself. Keeping both would print a blank line after each one.
void test_one_trailing_newline_is_dropped() {
  LogMessage m;
  TEST_ASSERT_TRUE(fmt(&m, LogLevel::Info, "roll: %02x rolled\n", 0xf1));
  TEST_ASSERT_EQUAL_STRING("roll: f1 rolled", m.text);
}

// An Info line prints exactly as its caller printed it before BF-11a, so nothing that
// reads the serial log has to change.
void test_info_renders_as_its_text_alone() {
  LogMessage m;
  fmt(&m, LogLevel::Info, "phy: every node heard - committed");
  char out[kLogTextLen + 8];
  TEST_ASSERT_EQUAL_size_t(std::strlen(m.text), log_render(m, out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("phy: every node heard - committed", out);
}

void test_warn_and_error_are_marked() {
  LogMessage m;
  char       out[kLogTextLen + 8];
  fmt(&m, LogLevel::Warn, "x");
  log_render(m, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("WARN x", out);
  fmt(&m, LogLevel::Error, "x");
  log_render(m, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("ERROR x", out);
}

// A cut line says so. Without the marker, a truncated number reads as a real one.
void test_a_long_line_is_cut_and_marked() {
  LogMessage        m;
  const std::string long_text(kLogTextLen * 2, 'a');
  TEST_ASSERT_FALSE(fmt(&m, LogLevel::Info, "%s", long_text.c_str()));
  TEST_ASSERT_EQUAL_size_t(kLogTextLen - 1, std::strlen(m.text));
  TEST_ASSERT_EQUAL_STRING("...", &m.text[kLogTextLen - 4]);
}

// The longest line migrated to the queue, with every field at its widest. It must fit
// whole: a cut `levers:` line hides the value a bench run set out to confirm.
void test_the_levers_line_fits() {
  LogMessage m;
  TEST_ASSERT_TRUE(fmt(&m, LogLevel::Info,
                       "levers: gen %u - diag %u s, poll reply %u ms, missed %u, cmd ack %u "
                       "ms x%u, config ack %u ms, readback %u ms, hex rsp %u ms, arm %u s, "
                       "simnode diag %s\n",
                       4294967295u, 65535u, 65535u, 255u, 65535u, 255u, 65535u, 65535u,
                       65535u, 65535u, "off"));
}

// A buffer too small for the whole line gets nothing rather than half a line.
void test_render_refuses_a_short_buffer() {
  LogMessage m;
  fmt(&m, LogLevel::Warn, "abcdef");
  char out[8];
  TEST_ASSERT_EQUAL_size_t(0, log_render(m, out, sizeof(out)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_one_trailing_newline_is_dropped);
  RUN_TEST(test_info_renders_as_its_text_alone);
  RUN_TEST(test_warn_and_error_are_marked);
  RUN_TEST(test_a_long_line_is_cut_and_marked);
  RUN_TEST(test_the_levers_line_fits);
  RUN_TEST(test_render_refuses_a_short_buffer);
  return UNITY_END();
}
