// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// L6 - the boot page, and the node key's status on it (GateLink Impl Plan 6.8).

#include <unity.h>

#include <cstring>

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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_template_key_is_reported_unprovisioned);
  RUN_TEST(test_provisioned_key_is_reported_provisioned);
  RUN_TEST(test_page_names_node_version_and_reset);
  RUN_TEST(test_long_fields_are_cut_to_the_panel);
  return UNITY_END();
}
