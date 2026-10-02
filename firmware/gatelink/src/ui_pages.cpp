// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Page text. Task L6; GateLink Impl Plan 5.3, 6.8.

#include "ui_pages.h"

#include <cstdio>

#include "lran/mac.h"

namespace gatelink {
namespace {

void put(PageText* out, const char* fmt, const char* a, const char* b = "") {
  if (out->count >= kPageLines) return;
  std::snprintf(out->line[out->count], sizeof(out->line[0]), fmt, a, b);
  ++out->count;
}

}  // namespace

bool node_key_unprovisioned(const uint8_t* key, size_t len) {
  return lran::key_is_placeholder(key, len);
}

const char* node_key_status(const uint8_t* key, size_t len) {
  return node_key_unprovisioned(key, len) ? "KEY UNPROVISIONED" : "key provisioned";
}

void render_boot_page(const BootInfo& info, PageText* out) {
  out->count = 0;
  put(out, "%s%s", "GateLink node 0x01");
  put(out, "%s %s", info.version, info.git);  // no "v": a dirty build fills all 19
  put(out, "reset %s", info.reset);
  put(out, "%s%s", node_key_status(info.node_key, info.node_key_len));
}

}  // namespace gatelink
