// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// lora_task's driver diagnostics. Task BF-16; published by BF-19.
//
// ARDUINO-FREE, and apart from lora_link.h for that reason: diag_json.cpp formats these on
// the host, and lora_link.h reaches RadioLib's pin map.

#pragma once

#include <cstdint>

namespace bridge {

// Bridge diagnostics kept beside the spec 14.1 counters. Not schema 0xF0: none of these is
// a receive-ladder discard (queues.h gives the same reasoning for the queue counters).
struct LoraStats {
  uint32_t begin_failures      = 0;  // radio init attempts that returned an error
  int16_t  last_begin_status   = 0;  // RadioLib's code from the latest attempt
  uint32_t rx_driver_errors    = 0;  // readData() failed other than on the PHY CRC
  uint32_t tx_forced           = 0;  // spec 12.3's "transmit regardless"
  uint32_t tx_errors           = 0;  // startTransmit() refused the frame
  uint32_t tx_timeouts         = 0;  // TX_DONE never came
  uint32_t tx_dropped_no_radio = 0;  // queued frames discarded while the radio was down
  uint32_t cad_errors          = 0;  // CADs that failed outright (MediaAccess)

  // A CAD not started because a frame was arriving. Counted separately because it also
  // counts as a busy CAD in cad_backoffs, and the two causes read differently.
  uint32_t cad_deferred = 0;
};

}  // namespace bridge
