// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// L6 - the boot page, and the node key's status on it (GateLink Impl Plan 6.8).

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "ui_pages.h"

using namespace gatelink;

void setUp() {}
void tearDown() {}

namespace {

// secrets.h.example's LRAN_GATELINK_NODE_KEY: 32 zero bytes.
const uint8_t kTemplateKey[32] = {};

// Any key with a non-zero byte. Not a real key; this suite never sees one.
uint8_t provisioned_key[32];

BootInfo boot(const uint8_t* key) { return {"0.1.0", "abc1234-dirty", "power_on", key, 32}; }

bool page_has(const PageText& p, const char* text) {
  for (size_t i = 0; i < p.count; ++i) {
    if (std::strstr(p.line[i], text) != nullptr) return true;
  }
  return false;
}

}  // namespace

// A board flashed from the template must say it cannot authenticate.
void test_template_key_is_reported_unprovisioned() {
  TEST_ASSERT_TRUE(node_key_unprovisioned(kTemplateKey, sizeof(kTemplateKey)));
  PageText p;
  render_boot_page(boot(kTemplateKey), &p);
  TEST_ASSERT_TRUE(page_has(p, "KEY UNPROVISIONED"));
}

void test_provisioned_key_is_reported_provisioned() {
  std::memset(provisioned_key, 0, sizeof(provisioned_key));
  provisioned_key[31] = 0x01;  // one non-zero byte is enough to leave the placeholder
  TEST_ASSERT_FALSE(node_key_unprovisioned(provisioned_key, sizeof(provisioned_key)));
  PageText p;
  render_boot_page(boot(provisioned_key), &p);
  TEST_ASSERT_TRUE(page_has(p, "key provisioned"));
  TEST_ASSERT_FALSE(page_has(p, "UNPROVISIONED"));
}

// The banner identifies the image: release and commit, dirty marker included.
void test_page_names_node_version_and_reset() {
  PageText p;
  render_boot_page(boot(kTemplateKey), &p);
  TEST_ASSERT_TRUE(page_has(p, "0x01"));
  TEST_ASSERT_TRUE(page_has(p, "0.1.0 abc1234-dirty"));
  TEST_ASSERT_TRUE(page_has(p, "reset power_on"));
}

// A panel line holds kPageCols characters. A long field is cut, not wrapped onto
// the line below, and every line stays terminated.
void test_long_fields_are_cut_to_the_panel() {
  BootInfo b = boot(kTemplateKey);
  b.git = "0123456789abcdef0123456789abcdef-dirty";
  PageText p;
  render_boot_page(b, &p);
  TEST_ASSERT_TRUE(p.count <= kPageLines);
  for (size_t i = 0; i < p.count; ++i) {
    TEST_ASSERT_TRUE(std::strlen(p.line[i]) <= kPageCols);
  }
}

// D92 - the status lines fit the panel's 19 columns at their longest.
void test_status_lines_read_as_designed() {
  char line[48];
  format_ved_line(false, 0, 5, 0, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("VE none", line);
  format_ved_line(true, 2400, 5, 13210, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("VE ok 2s 13.21V", line);
  format_ved_line(true, 45000, 5, 13210, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("VE STALE 45s", line);
  format_ved_line(true, 1000, 5, UINT16_MAX, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("VE ok 1s", line);

  BmsHistory h;
  format_bms_line(h, true, false, 0, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("BMS 0/0 none", line);
  for (bool ok : {true, false, true, true, true, true, true}) h.record(ok);  // 7 > kDepth
  format_bms_line(h, true, true, 250, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("BMS 5/6 ok 4m", line);
  format_bms_line(h, false, true, 250, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("BMS off", line);

  format_lora_line(false, 0, 0, 0, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("RX none", line);
  format_lora_line(true, 12, -87, 75, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("RX 12s -87 7.5dB", line);
  format_lora_line(true, 99, -112, -125, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("RX 99s -112 -12.5dB", line);
  TEST_ASSERT_LESS_OR_EQUAL(kPageCols, std::strlen(line));
  format_lora_line(true, 7200, -90, -5, line, sizeof(line));
  TEST_ASSERT_EQUAL_STRING("RX 2h -90 -0.5dB", line);
}

// PRD R-4.7 with D92's display_mode.
void test_backlight_follows_mode_button_and_timeout() {
  Backlight b(0);
  TEST_ASSERT_TRUE(b.update(Backlight::kButton, false, false, 60, 1000));    // lit from boot
  TEST_ASSERT_FALSE(b.update(Backlight::kButton, false, false, 60, 60000));  // timed out
  TEST_ASSERT_TRUE(b.update(Backlight::kButton, true, false, 60, 70000));    // press lights
  TEST_ASSERT_FALSE(b.update(Backlight::kButton, true, false, 60, 71000));   // press blanks
  TEST_ASSERT_TRUE(b.update(Backlight::kButton, false, true, 60, 200000));   // debug holds it
  TEST_ASSERT_TRUE(b.update(Backlight::kButton, false, true, 60, 900000));
  TEST_ASSERT_TRUE(b.update(Backlight::kButton, false, false, 60, 959000));  // timer restarted
  TEST_ASSERT_FALSE(b.update(Backlight::kButton, false, false, 60, 960000));
  TEST_ASSERT_FALSE(b.update(Backlight::kOff, true, true, 60, 961000));      // off means off
  TEST_ASSERT_TRUE(b.update(Backlight::kOn, false, false, 60, 962000));
  TEST_ASSERT_TRUE(b.update(Backlight::kOn, false, false, 60, 9999000));     // on ignores timeout
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_template_key_is_reported_unprovisioned);
  RUN_TEST(test_provisioned_key_is_reported_provisioned);
  RUN_TEST(test_page_names_node_version_and_reset);
  RUN_TEST(test_long_fields_are_cut_to_the_panel);
  RUN_TEST(test_status_lines_read_as_designed);
  RUN_TEST(test_backlight_follows_mode_button_and_timeout);
  return UNITY_END();
}
