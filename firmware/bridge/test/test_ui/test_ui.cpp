// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-14 - the status page's text, and the burn-in shift.
//
// WHAT THIS COVERS. Every string the panel can show fits its font's character budget,
// sentinels read as `--` rather than as numbers, and the pixel shift cycles. The range
// test's panel truncated two strings on hardware before anyone noticed; these budgets
// are the desk-side guard against a third.
//
// WHAT IT CANNOT. That the panel lights (Vext), that it is the right way up, that the
// fonts measure as the budgets assume. That is the bench, and B2's "OLED shows a
// status page" is not met until someone has looked at it.

#include <unity.h>

#include <climits>
#include <cstring>

#include "status_page.h"

using namespace bridge;

void setUp() {}
void tearDown() {}

namespace {

StatusSnapshot healthy() {
  StatusSnapshot s;
  s.uptime_s       = 3 * 86400 + 4 * 3600;
  s.wifi_connected = true;
  s.wifi_rssi_dbm  = -61;
  s.mqtt_connected = true;
  s.nodes_online   = 2;
  s.nodes_total    = 3;
  s.slot           = "app1";
  s.version        = "0.1.0";
  return s;
}

void assert_fits(const StatusLines& l) {
  TEST_ASSERT_TRUE_MESSAGE(std::strlen(l.top) + 1 + std::strlen(l.top_right) <= kSmallFontBudget,
                           l.top);
  TEST_ASSERT_TRUE_MESSAGE(std::strlen(l.big) <= kBigFontBudget, l.big);
  TEST_ASSERT_TRUE_MESSAGE(std::strlen(l.mid) <= kSmallFontBudget, l.mid);
  TEST_ASSERT_TRUE_MESSAGE(std::strlen(l.foot) <= kSmallFontBudget, l.foot);
}

}  // namespace

void test_healthy_page_reads_as_expected() {
  const StatusLines l = build_status_lines(healthy());
  TEST_ASSERT_EQUAL_STRING("LRAN bridge", l.top);
  TEST_ASSERT_EQUAL_STRING("app1", l.top_right);
  TEST_ASSERT_EQUAL_STRING("nodes 2/3", l.big);
  TEST_ASSERT_EQUAL_STRING("WiFi -61  MQTT up", l.mid);
  TEST_ASSERT_EQUAL_STRING("up 3d04h 0.1.0", l.foot);
}

// Until the registry exists the count is unknown, and unknown is `--`. "0" would read
// as every node down (root rule 6).
void test_unknown_node_count_is_dashes_not_zero() {
  StatusSnapshot s = healthy();
  s.nodes_online = kNodesUnknown;
  s.nodes_total  = kNodesUnknown;
  TEST_ASSERT_EQUAL_STRING("nodes --", build_status_lines(s).big);

  s.nodes_online = 0;
  s.nodes_total  = 3;
  TEST_ASSERT_EQUAL_STRING("nodes 0/3", build_status_lines(s).big);

  // Two digits fit the large font; three do not, and say so instead of overrunning.
  s.nodes_online = 99;
  s.nodes_total  = 99;
  TEST_ASSERT_EQUAL_STRING("nodes 99/99", build_status_lines(s).big);
  s.nodes_total = 100;
  TEST_ASSERT_EQUAL_STRING("nodes >99", build_status_lines(s).big);
}

// No association and the INT16_MIN sentinel both read as `--`, never as a number.
void test_no_wifi_reading_is_dashes() {
  StatusSnapshot s = healthy();
  s.wifi_connected = false;
  TEST_ASSERT_EQUAL_STRING("WiFi --  MQTT up", build_status_lines(s).mid);

  s.wifi_connected = true;
  s.wifi_rssi_dbm  = INT16_MIN;
  s.mqtt_connected = false;
  TEST_ASSERT_EQUAL_STRING("WiFi --  MQTT --", build_status_lines(s).mid);
}

// The warnings outrank the uptime, and a pending image outranks a queue drop: it is
// the state V-B9 is watched in, and the one that ends in a reboot.
void test_footer_priority() {
  StatusSnapshot s = healthy();
  s.any_dropped = true;
  TEST_ASSERT_EQUAL_STRING("DROPS up 3d04h", build_status_lines(s).foot);
  s.ota_pending = true;
  TEST_ASSERT_EQUAL_STRING("VERIFY up 3d04h", build_status_lines(s).foot);
}

void test_ota_in_progress_takes_the_page() {
  StatusSnapshot s = healthy();
  s.ota_in_progress = true;
  const StatusLines l = build_status_lines(s);
  TEST_ASSERT_EQUAL_STRING("OTA...", l.big);
  assert_fits(l);
}

// Every combination of the page's variable parts stays inside its font's budget,
// including the widest RSSI, the longest uptime and a full node count.
void test_every_state_fits_the_panel() {
  StatusSnapshot s = healthy();
  s.uptime_s      = 0xFFFFFFFFu;  // ~49710 days - the widest uptime string there is
  s.wifi_rssi_dbm = -127;
  s.nodes_online  = 254;
  s.nodes_total   = 254;
  s.slot          = "app0";
  s.version       = "10.20.30";

  for (int pending = 0; pending < 2; ++pending) {
    for (int dropped = 0; dropped < 2; ++dropped) {
      s.ota_pending = pending != 0;
      s.any_dropped = dropped != 0;
      assert_fits(build_status_lines(s));
    }
  }
}

void test_missing_strings_do_not_crash_the_page() {
  StatusSnapshot s = healthy();
  s.slot    = nullptr;
  s.version = nullptr;
  const StatusLines l = build_status_lines(s);
  TEST_ASSERT_EQUAL_STRING("?", l.top_right);
  TEST_ASSERT_EQUAL_STRING("up 3d04h ?", l.foot);
}

void test_uptime_formats() {
  char b[12];
  format_uptime(0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("0s", b);
  format_uptime(59, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("59s", b);
  format_uptime(12 * 60 + 5, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("12m", b);
  format_uptime(3 * 3600 + 4 * 60, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("3h04m", b);
  format_uptime(3 * 86400 + 4 * 3600, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("3d04h", b);
}

// Burn-in: the shift cycles through every step and returns to zero, so no column of
// the layout is lit continuously for years.
void test_pixel_shift_cycles() {
  TEST_ASSERT_EQUAL_INT16(0, pixel_shift_x(0));
  TEST_ASSERT_EQUAL_INT16(0, pixel_shift_x(kPixelShiftPeriodS - 1));
  TEST_ASSERT_EQUAL_INT16(1, pixel_shift_x(kPixelShiftPeriodS));
  TEST_ASSERT_EQUAL_INT16(kPixelShiftSteps - 1,
                          pixel_shift_x(kPixelShiftPeriodS * (kPixelShiftSteps - 1)));
  TEST_ASSERT_EQUAL_INT16(0, pixel_shift_x(kPixelShiftPeriodS * kPixelShiftSteps));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_healthy_page_reads_as_expected);
  RUN_TEST(test_unknown_node_count_is_dashes_not_zero);
  RUN_TEST(test_no_wifi_reading_is_dashes);
  RUN_TEST(test_footer_priority);
  RUN_TEST(test_ota_in_progress_takes_the_page);
  RUN_TEST(test_every_state_fits_the_panel);
  RUN_TEST(test_missing_strings_do_not_crash_the_page);
  RUN_TEST(test_uptime_formats);
  RUN_TEST(test_pixel_shift_cycles);
  return UNITY_END();
}
