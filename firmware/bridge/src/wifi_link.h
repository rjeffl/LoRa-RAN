// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// WiFi station and its reconnect. Task BF-12; R-3.2a, R-3.2b, R-3.2c.

#pragma once

#include <cstdint>

namespace bridge {

// Credentials are PASSED IN, not read from secrets.h here. main.cpp is the only
// translation unit that includes secrets.h, which keeps the number of files that
// could log a password at one - and that one has no logging in it.
//
// Non-blocking: it starts the association and returns. R-3.2b forbids blocking LoRa
// receive while reconnecting, and the way to honour that is for nothing in this file
// to wait for the network.
void wifi_begin(const char* ssid, const char* password);

// Called from mqtt_task's tick. Reassociates on the backoff in net_policy.h when the
// link is down, and does nothing when it is up.
void wifi_service(uint32_t now_ms);

bool wifi_connected();

// R-3.2c - published as a diagnostic. INT16_MIN when there is no association, never
// 0: a consumer must be able to tell "no reading" from "0 dBm", which would be an
// absurdly strong signal (root rule 6).
int16_t wifi_rssi_dbm();

uint32_t wifi_reconnects();

}  // namespace bridge
