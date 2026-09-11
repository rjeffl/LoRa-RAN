// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The OLED status page's text. Task BF-14; R-4.1c.

#include "status_page.h"

#include <cstdio>
#include <cstring>

namespace bridge {

void format_uptime(uint32_t s, char* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return;
  }
  const uint32_t d = s / 86400U;
  const uint32_t h = (s % 86400U) / 3600U;
  const uint32_t m = (s % 3600U) / 60U;
  if (d > 0) {
    std::snprintf(out, cap, "%lud%02luh", static_cast<unsigned long>(d),
                  static_cast<unsigned long>(h));
  } else if (h > 0) {
    std::snprintf(out, cap, "%luh%02lum", static_cast<unsigned long>(h),
                  static_cast<unsigned long>(m));
  } else if (m > 0) {
    std::snprintf(out, cap, "%lum", static_cast<unsigned long>(m));
  } else {
    std::snprintf(out, cap, "%lus", static_cast<unsigned long>(s));
  }
}

int16_t pixel_shift_x(uint32_t uptime_s) {
  return static_cast<int16_t>((uptime_s / kPixelShiftPeriodS) % kPixelShiftSteps);
}

StatusLines build_status_lines(const StatusSnapshot& s) {
  StatusLines l;

  std::snprintf(l.top, sizeof(l.top), "LRAN bridge");
  std::snprintf(l.top_right, sizeof(l.top_right), "%s", s.slot != nullptr ? s.slot : "?");

  // An upload in progress takes the whole page. The bridge reboots at the end of it,
  // and a person looking at the panel should know that is about to happen rather than
  // read it as a crash.
  if (s.ota_in_progress) {
    std::snprintf(l.big, sizeof(l.big), "OTA...");
    std::snprintf(l.mid, sizeof(l.mid), "updating - do not");
    std::snprintf(l.foot, sizeof(l.foot), "power off");
    return l;
  }

  // R-4.1c's glanceable figure, and the largest element. `--` until BF-15/BF-20 can
  // count: "0" would read as "every node is down".
  //
  // Two digits each at most. The fleet is a handful of nodes, but node IDs are a byte
  // and "nodes 254/254" overran the large font's budget in a host test - so a count
  // past 99 says so rather than running off the panel.
  if (s.nodes_online == kNodesUnknown || s.nodes_total == kNodesUnknown) {
    std::snprintf(l.big, sizeof(l.big), "nodes --");
  } else if (s.nodes_online > 99 || s.nodes_total > 99) {
    std::snprintf(l.big, sizeof(l.big), "nodes >99");
  } else {
    std::snprintf(l.big, sizeof(l.big), "nodes %u/%u", static_cast<unsigned>(s.nodes_online),
                  static_cast<unsigned>(s.nodes_total));
  }

  // WiFi with its RSSI, because "connected at -88" and "connected at -55" are
  // different situations for a bridge in a house. `--` when there is no reading.
  char wifi[12];
  if (!s.wifi_connected || s.wifi_rssi_dbm == INT16_MIN) {
    std::snprintf(wifi, sizeof(wifi), "WiFi --");
  } else {
    std::snprintf(wifi, sizeof(wifi), "WiFi %d", static_cast<int>(s.wifi_rssi_dbm));
  }
  std::snprintf(l.mid, sizeof(l.mid), "%s  MQTT %s", wifi, s.mqtt_connected ? "up" : "--");

  // Footer: the one warning that outranks the uptime, else uptime and version.
  //
  // A pending image is shown because it is the state V-B9 is watched in, and because
  // a bridge in it will roll itself back if its network does not come up - worth
  // knowing from across a room. A queue drop is shown because it is never supposed to
  // happen (queues.h), and a flag on the panel is noticed sooner than a counter on a
  // chart.
  char up[12];
  format_uptime(s.uptime_s, up, sizeof(up));
  //
  // Single spaces and short words, because the widest uptime ("49710d06h") plus a
  // longer version string ran past the small font's budget - caught by
  // test_every_state_fits_the_panel and by GCC's -Wformat-truncation on the same day.
  if (s.ota_pending) {
    std::snprintf(l.foot, sizeof(l.foot), "VERIFY up %s", up);
  } else if (s.any_dropped) {
    std::snprintf(l.foot, sizeof(l.foot), "DROPS up %s", up);
  } else {
    std::snprintf(l.foot, sizeof(l.foot), "up %s %s", up,
                  s.version != nullptr ? s.version : "?");
  }
  return l;
}

}  // namespace bridge
