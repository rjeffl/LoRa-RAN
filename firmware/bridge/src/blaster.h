// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// V-B12's WiFi load - a bench-only UDP transmitter, built only into `v_b12_blaster`.
//
// V-B12's saturated arm needs the ESP32-S3's own 2.4 GHz transmitter busy while the
// SX1262 receives (Bridge PRD R-4.4, M22). `diag_interval_s` cannot supply that: its
// 10 s floor adds three small documents every 10 s (bench network brief, 2026-09-23).
// This sends UDP to the broker host's discard port at a rate set at run time, and counts
// what the stack actually accepted, because the rate asked for is not the load applied.
//
// Controlled from the bridge's USB serial port. No MQTT topic or config row, because
// every one of those belongs to the protocol specification. The sweep tool holds the port
// open for the whole run: opening it reboots the bridge and zeroes its counters.
//
//   blast <kbps> [bytes]   start, or change the rate; bytes is the UDP payload size
//   blast 0                stop, and print the totals since the last start
//   blast                  print the running totals without stopping

#pragma once

#include <cstddef>

namespace bridge {

// Called once from setup(), after the network is configured. `host` is where the load is
// sent: the broker, because it is the one host the bench guarantees is on the network.
void blaster_begin(const char* host);

// Called from loop(). Reads console lines and paces the sends. Never blocks for longer
// than one tick, so the rest of loop() keeps running.
void blaster_poll();

}  // namespace bridge
