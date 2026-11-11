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

void format_age(uint32_t age_s, char* out, size_t cap) {
  if (age_s < 100) {
    std::snprintf(out, cap, "%lus", static_cast<unsigned long>(age_s));
  } else if (age_s < 100 * 60) {
    std::snprintf(out, cap, "%lum", static_cast<unsigned long>(age_s / 60));
  } else if (age_s < 100 * 3600) {
    std::snprintf(out, cap, "%luh", static_cast<unsigned long>(age_s / 3600));
  } else {
    std::snprintf(out, cap, "%lud", static_cast<unsigned long>(age_s / 86400));
  }
}

void format_ved_line(bool have_block, uint32_t age_ms, uint32_t stale_s, uint16_t batt_mv,
                     char* out, size_t cap) {
  if (!have_block) {
    std::snprintf(out, cap, "VE none");
    return;
  }
  char age[8];
  format_age(age_ms / 1000, age, sizeof(age));
  if (age_ms > stale_s * 1000) {
    std::snprintf(out, cap, "VE STALE %s", age);
  } else if (batt_mv == UINT16_MAX) {
    std::snprintf(out, cap, "VE ok %s", age);
  } else {
    std::snprintf(out, cap, "VE ok %s %u.%02uV", age, batt_mv / 1000, (batt_mv % 1000) / 10);
  }
}

void BmsHistory::record(bool read_ok) {
  bits_ = static_cast<uint8_t>((bits_ << 1) | (read_ok ? 1 : 0));
  if (count_ < kDepth) ++count_;
}

uint8_t BmsHistory::reads() const {
  const uint8_t kept = static_cast<uint8_t>(bits_ & ((1u << count_) - 1));
  return static_cast<uint8_t>(__builtin_popcount(kept));
}

void format_bms_line(const BmsHistory& h, bool polling, bool have_read, uint32_t read_age_s,
                     char* out, size_t cap) {
  if (!polling) {
    std::snprintf(out, cap, "BMS off");
    return;
  }
  if (!have_read) {
    std::snprintf(out, cap, "BMS %u/%u none", h.reads(), h.windows());
    return;
  }
  char age[8];
  format_age(read_age_s, age, sizeof(age));
  std::snprintf(out, cap, "BMS %u/%u ok %s", h.reads(), h.windows(), age);
}

void format_lora_line(bool heard, uint32_t age_s, int16_t rssi_dbm, int16_t snr_db10, char* out,
                      size_t cap) {
  if (!heard) {
    std::snprintf(out, cap, "RX none");
    return;
  }
  char age[8];
  format_age(age_s, age, sizeof(age));
  const int  snr_abs = snr_db10 < 0 ? -snr_db10 : snr_db10;
  std::snprintf(out, cap, "RX %s %d %s%d.%ddB", age, rssi_dbm, snr_db10 < 0 ? "-" : "",
                snr_abs / 10, snr_abs % 10);
}

bool Backlight::update(uint8_t mode, bool pressed, bool debug, uint32_t timeout_s,
                       uint32_t now_ms) {
  if (mode != mode_) {
    mode_     = mode;
    lit_      = mode != kOff;
    since_ms_ = now_ms;
  }
  if (mode == kOff) return lit_ = false;
  if (mode == kOn) return lit_ = true;
  if (debug) {
    lit_      = true;
    since_ms_ = now_ms;
  } else if (pressed) {
    lit_      = !lit_;
    since_ms_ = now_ms;
  } else if (lit_ && now_ms - since_ms_ >= timeout_s * 1000) {
    lit_ = false;
  }
  return lit_;
}

}  // namespace gatelink
