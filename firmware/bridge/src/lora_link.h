// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The SX1262, and the work lora_task does with it. Task BF-16; spec 12, 14; Impl Plan
// 5.2, 5.3, 10.8.1; R-4.1b, R-5.3d.
//
// THE ONLY FILE IN THE BRIDGE THAT INCLUDES RADIOLIB. The two decisions lora_task makes
// live elsewhere and are host-tested - rx_ladder.h (spec 14 stages 2 to 10) and
// media_access.h (spec 12.3). This file moves bytes and interrupts between them and the
// radio.
//
// NOTHING HERE WAITS ON THE RADIO EITHER. RadioLib's transmit() and scanChannel() loop
// until DIO1 rises, and scanChannel()'s loop has no timeout (SX126x.cpp, 7.7.1): a radio
// that never raised CAD_DONE would hang the highest-priority task in the bridge for good.
// So a CAD and a transmission are STARTED here, and their completion is read from the IRQ
// register on later passes, each against a deadline, while lora_task keeps its loop.
// lora_task's one wait is lora_wait(): bounded, and on the radio's own interrupt.

#pragma once

#include <cstdint>

#include "lran/counters.h"
#include "lran/mac.h"
#include "media_access.h"
#include "radio_config.h"
#include "rx_ladder.h"

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

// Records the calling task as the one DIO1 wakes, then brings the radio up against the
// injected pin map (spec 12.2) and the fixed PHY (spec 12.1). Call once, at the top of
// lora_task. A failure is logged and retried from lora_service() every 10 s.
void lora_start(const RadioPins& pins, const PhyConfig& phy);

// One pass of lora_task's work: read a received frame, expire reassembly sets, move the
// waiting frame through media access, and take the next one from the TX queue.
void lora_service(uint32_t now_ms);

// Waits for DIO1 or `max_wait_ms`, whichever comes first.
void lora_wait(uint32_t max_wait_ms);

// Root rule 8 - spec 12.3's cad_retries and backoff_max_ms, and spec 11.2's
// frag_reassembly_timeout_ms, are runtime-configurable. This is where they enter.
// TODO(BF-23): called from the HA-visible configuration, through lora_task rather than
// across it; until then only the defaults are ever in effect.
void lora_configure(const MediaAccessConfig& access, uint32_t frag_timeout_ms);

// For BF-15's registry. Until it is called, every authenticated frame is refused.
// TODO(BF-15): call before start_tasks(), so lora_task never races the assignment.
void lora_set_auth(lran::IMac* mac, const PeerKeys* keys);

// Safe from any task.
bool lora_radio_ready();

// R-5.3d. True when no frame is waiting or on the air, no reassembly set is incomplete,
// and the radio is receiving - or is down, when there is nothing an OTA could interrupt.
bool lora_idle();

// lora_task's own counters. TODO(BF-19): a consistent snapshot for the diagnostic
// publication - one field read torn is harmless on this core, a multi-field view is not.
const lran::Counters& lora_counters();
const LoraStats&      lora_stats();

}  // namespace bridge
