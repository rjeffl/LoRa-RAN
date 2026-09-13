// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// WiFi station and its reconnect. Task BF-12; R-3.2a, R-3.2b, R-3.2c.

#include "wifi_link.h"

#include <WiFi.h>

#include <climits>

#include "net_policy.h"

namespace bridge {
namespace {

const char* g_ssid     = nullptr;
const char* g_password = nullptr;

uint32_t g_attempt        = 0;  // 0 while connected; counts retries while down
uint32_t g_next_try_ms    = 0;
uint32_t g_reconnects     = 0;
bool     g_was_connected  = false;

}  // namespace

void wifi_begin(const char* ssid, const char* password) {
  g_ssid     = ssid;
  g_password = password;

  WiFi.mode(WIFI_STA);

  // Arduino-ESP32 persists credentials in NVS by default and re-associates from
  // that copy on boot. Off, deliberately: the credentials that matter are the ones
  // in secrets.h, and a stale NVS copy produces a bridge that connects to a network
  // the build no longer names - which looks like a build that worked.
  WiFi.persistent(false);

  // The SDK's own auto-reconnect is also off. Reconnect cadence belongs to
  // net_policy.h where it is one deterministic sequence with host tests, not split
  // between our backoff and the SDK's.
  WiFi.setAutoReconnect(false);

  WiFi.begin(g_ssid, g_password);
  g_attempt     = 0;
  g_next_try_ms = 0;
}

void wifi_service(uint32_t now_ms) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!g_was_connected) {
      g_was_connected = true;
      if (g_attempt > 0) {
        ++g_reconnects;  // a recovery, not the first connect
      }
      g_attempt = 0;
    }
    return;
  }

  if (g_was_connected) {
    g_was_connected = false;
    g_attempt       = 1;
    g_next_try_ms   = now_ms + reconnect_delay_ms(g_attempt);
    return;
  }

  // Still down. Wait out the backoff, then try again.
  //
  // The subtraction rather than `now_ms >= g_next_try_ms` is deliberate: millis()
  // wraps at ~49.7 days and this is a mains-powered node expected to run for years.
  // Unsigned wrap-around arithmetic stays correct across the boundary; a direct
  // comparison stalls the reconnect for the rest of the epoch.
  if (g_next_try_ms != 0 && static_cast<int32_t>(now_ms - g_next_try_ms) < 0) {
    return;
  }

  WiFi.disconnect();
  WiFi.begin(g_ssid, g_password);
  if (g_attempt < UINT32_MAX) {
    ++g_attempt;
  }
  g_next_try_ms = now_ms + reconnect_delay_ms(g_attempt);
}

bool wifi_connected() { return WiFi.status() == WL_CONNECTED; }

int16_t wifi_rssi_dbm() {
  // Root rule 6 - a sentinel, not a zero. INT16_MIN is "no reading"; 0 dBm would be
  // a signal strong enough to be impossible.
  if (WiFi.status() != WL_CONNECTED) {
    return INT16_MIN;
  }
  return static_cast<int16_t>(WiFi.RSSI());
}

uint32_t wifi_reconnects() { return g_reconnects; }

}  // namespace bridge
