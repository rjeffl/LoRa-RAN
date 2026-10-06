// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The one SPI lock. GL1; GateLink Impl Plan 5.2; expansion board 7.2.
//
// The radio, the LCD (CS G12) and the microSD (CS G10) share G7/G8/G9. Each driver locks
// only its own transactions, and a radio command with its BUSY wait, or an SD sector with
// its CS, spans more than one. Every access to any of the three holds this lock for the
// whole operation. The GL0 bus test ran all three under one mutex for 60 s with no bad
// transfer (engineering log, 2026-10-06).
//
// io_task never takes it: io_task waits on nothing but its period (R-5.2a).

#pragma once

namespace gatelink {

// Creates the mutex. setup() calls it before any task starts.
void spi_bus_init();

// Holds the lock for its scope. Waits as long as it takes: an SD write holds the bus for
// up to 59 ms, and a caller that gave up would drop a frame or a log line instead.
class SpiLock {
 public:
  SpiLock();
  ~SpiLock();
  SpiLock(const SpiLock&)            = delete;
  SpiLock& operator=(const SpiLock&) = delete;
};

}  // namespace gatelink
