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

  // ---------------------------------------------------------------------------
  // The receive path's interrupt accounting (rx_wake.h). Bridge-local, NOT spec 14.1.
  //
  // WHY NOT SPEC 14.1, decided here rather than left open: 14.1 is a normative registry
  // of RECEIVE-LADDER discards, carried on the wire by schema 0xF0 and reported by every
  // node under the same names. Neither of these is a discard and neither has a `Status`
  // or a stage - they describe how a frame reached the ladder, one layer below it, on a
  // driver every node need not share. Putting them in 14.1 would make a bridge-local
  // driver detail protocol-visible. queues.h reasons the same way about queue overflow,
  // and root rule 4's real demand - that no loss is silent - is met by naming them here
  // and publishing them on lran/bridge/diag/radio/state.
  // ---------------------------------------------------------------------------

  // RX_DONE found by the timed read with no DIO1 edge behind it. DIO1 is a level output
  // and RadioLib attaches on the rising edge, so a frame arriving while the previous
  // one's RX_DONE is still set announces itself to nothing; the timed read is what
  // recovers it. A NON-ZERO VALUE HERE IS THE MEASUREMENT: it says the interrupt path
  // missed a frame, and how often, without inferring it from a PER curve.
  uint32_t rx_no_interrupt = 0;

  // An edge arrived and the register held neither RX_DONE nor HEADER_ERR - the frame
  // that raised it was already gone. readData() clears the WHOLE register, so a frame
  // arriving between getIrqFlags() and that clear takes its own RX_DONE with it while
  // leaving the edge behind. This counts what that costs.
  uint32_t rx_wake_empty = 0;
};

}  // namespace bridge
