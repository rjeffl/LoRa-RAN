// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// What the OLED status page says, built as text. Task BF-14; R-4.1c.
//
// ARDUINO-FREE. The renderer in ui.cpp draws four strings in four places; everything
// that decides what those strings ARE - sentinels, units, truncation, the pixel shift
// - is here and has host tests. The range test's panel truncated two strings on
// hardware ("RESPONDE") before anyone noticed, and a character budget checked at a
// desk is cheaper than a third.

#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>

namespace bridge {

// "Not known yet" for the node counts: before sched_task's first tick, the availability
// watchdog (BF-20) has counted nothing, and the page says `--`, never `0`.
// Root rule 6: a reader must be able to tell "no nodes online" from "not counted".
inline constexpr uint8_t kNodesUnknown = 0xFF;

struct StatusSnapshot {
  uint32_t    uptime_s        = 0;
  bool        wifi_connected  = false;
  int16_t     wifi_rssi_dbm   = INT16_MIN;  // root rule 6 - no reading is not 0 dBm
  bool        mqtt_connected  = false;
  uint8_t     nodes_online    = kNodesUnknown;
  uint8_t     nodes_total     = kNodesUnknown;
  bool        any_dropped     = false;      // QueueAccounting::any_dropped()
  bool        ota_in_progress = false;
  bool        ota_pending     = false;      // this image has not yet proven itself
  const char* slot            = nullptr;    // "app0" / "app1"
  const char* version         = nullptr;
};

// Four rows, matching the renderer's four positions. Sized with headroom over the
// budgets below; the budgets, not the arrays, are what keep text on the panel.
struct StatusLines {
  char top[24]  = {0};  // small font, left:  "LRAN bridge"      right is `top_right`
  char top_right[8] = {0};
  char big[16]  = {0};  // large font:        "nodes 2/3"
  char mid[24]  = {0};  // small font:        "WiFi -61  MQTT up"
  char foot[24] = {0};  // small font:        "up 3d04h  0.1.0" / warnings
};

// Character budgets per font, measured against the ThingPulse ArialMT faces on a
// 128 px panel: ArialMT_Plain_10 fits about 21 mixed characters, _16 about 12. Worst
// case (all wide glyphs) is less; these are the ceilings a test enforces, and the
// strings built here stay well inside them.
inline constexpr size_t kSmallFontBudget = 21;
inline constexpr size_t kBigFontBudget   = 12;

StatusLines build_status_lines(const StatusSnapshot& s);

// "59s", "12m", "3h04m", "3d04h". The uptime keeps counting, which is what makes a
// frozen panel distinguishable from a quiet one - the range test's show_stale()
// lesson, applied to a display that is otherwise static for years.
void format_uptime(uint32_t seconds, char* out, size_t cap);

// Burn-in. The panel is on permanently (R-4.1c) on a mains node expected to run for
// years, and an SSD1306 showing the same static layout that long keeps a ghost of it.
// Every element is shifted by 0-3 px on a slow cycle so no pixel is lit continuously.
// Returns the x offset for this uptime.
inline constexpr uint32_t kPixelShiftPeriodS = 300;  // one step every five minutes
inline constexpr int16_t  kPixelShiftSteps   = 4;
int16_t pixel_shift_x(uint32_t uptime_s);

}  // namespace bridge
