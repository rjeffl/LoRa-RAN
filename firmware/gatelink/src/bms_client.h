// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// One BLE window: start the controller, find the pack, connect, handshake, read 0x8C,
// disconnect and de-initialize the controller. GL5; PRD R-3.4a, R-3.4b; Impl Plan 4.3, 5.2;
// bms-protocol 3, 7.
//
// Arduino-only. bms_task calls it while holding the LoRa/BLE interlock. The caller passes
// the window's start and its cap, and a function that says when to stop: the window checks
// it between steps and every 10 ms while it waits. A NimBLE call that blocks cannot be
// interrupted, so the abort latency is the longest single call; GL5 measures it.

#pragma once

#include <cstdint>

#include "bms_link.h"

namespace gatelink {

struct BmsWindowArgs {
  const char* name     = nullptr;  // the advertised name, bms-protocol 2
  uint32_t    start_ms = 0;        // when the interlock was taken
  uint32_t    cap_ms   = 0;        // bms_window_max_ms
  bool (*stop)()       = nullptr;  // true once lora_task asks for the interlock back
  void (*log)(const char*) = nullptr;
};

// Runs one window and always leaves the controller de-initialized. On BmsEnd::Read, `out`
// holds the decoded read, its RSSI and its time.
BmsEnd bms_window(const BmsWindowArgs& args, BmsSnapshot* out, BmsTiming* t);

BmsRxCounters bms_rx_counters();

// Free heap before the controller starts and after it stops, from the last window. A
// controller that leaks on de-init shows as a falling `after`.
struct BmsHeap {
  uint32_t before = 0;
  uint32_t after  = 0;
  uint32_t low    = 0;  // lowest free heap since boot
};
BmsHeap bms_heap();

// The wait before each de-init for NimBLE's host queue to empty. A timeout means the
// de-init went ahead with events still queued, which can panic the host task.
struct BmsDrain {
  uint32_t max_ms   = 0;  // longest wait since boot
  uint32_t timeouts = 0;
};
BmsDrain bms_drain();

}  // namespace gatelink
