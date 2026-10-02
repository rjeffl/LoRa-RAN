// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Page text for the panel, kept apart from M5GFX so the native build can test it, as the
// simnode's oled_page.h is. Task L6 adds the boot page; GL1 adds the rest.

#pragma once

#include <cstddef>
#include <cstdint>

namespace gatelink {

// The panel is landscape 240x135. At text size 2 the built-in font is 12x16 pixels, so
// the panel holds 8 lines. board_show() insets each line 6 pixels to clear the case's
// bezel, which leaves room for 19 characters.
inline constexpr size_t kPageCols  = 19;
inline constexpr size_t kPageLines = 8;

struct PageText {
  char   line[kPageLines][kPageCols + 1];
  size_t count;
};

// The node key's state, worded for the serial banner and the panel. GateLink Impl Plan
// 6.8: a node that cannot authenticate must never be mistaken for a provisioned one, so
// lran::key_is_placeholder() decides, and both outputs say the same thing.
bool        node_key_unprovisioned(const uint8_t* key, size_t len);
const char* node_key_status(const uint8_t* key, size_t len);

struct BootInfo {
  const char*    version;  // custom_gatelink_version, stamped by scripts/version.py
  const char*    git;      // the commit built; "-dirty" means it matches no commit
  const char*    reset;    // the reset cause's name
  const uint8_t* node_key;
  size_t         node_key_len;
};

// The boot page. Lines longer than kPageCols are cut, never wrapped.
void render_boot_page(const BootInfo& info, PageText* out);

}  // namespace gatelink
