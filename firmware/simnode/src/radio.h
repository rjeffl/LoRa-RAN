// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The SX1262 shared by every identity. Task BF-2; Impl Plan 10.6; spec 12.
//
// THE ONLY FILE IN THE SIMNODE THAT INCLUDES RADIOLIB. It moves bytes between the radio and
// node.h, and applies lib/lran-link's spec 12.3 media access to every outgoing frame - the
// same implementation the bridge runs.
//
// NOTHING HERE WAITS ON THE RADIO. The bridge found that RadioLib's scanChannel() has no
// timeout and transmit() busy-waits (engineering log, BF-16). A CAD and a transmission are
// started, and their completion is read from the IRQ register on later passes against a
// deadline, so the console stays responsive while a frame backs off.

#pragma once

#include <cstdint>

#include "lran/link/radio_config.h"
#include "node.h"

namespace simnode {

struct RadioStats {
  uint32_t begin_failures   = 0;
  int16_t  last_begin_status = 0;
  uint32_t rx_driver_errors = 0;  // readData() failed other than on the PHY CRC
  uint32_t tx_frames        = 0;  // TX_DONE seen
  uint32_t tx_errors        = 0;  // startTransmit() refused the frame
  uint32_t tx_timeouts      = 0;  // TX_DONE never came
  uint32_t tx_forced        = 0;  // spec 12.3's "transmit regardless"
  uint32_t cad_errors       = 0;
  uint32_t cad_deferred     = 0;  // a CAD not started because a frame was arriving
};

// Brings the radio up against the injected pins (spec 12.2) and the fixed PHY (spec 12.1).
// A failure is printed and retried from radio_service() every 10 s.
void radio_start(const lran::link::RadioPins& pins, const lran::link::PhyConfig& phy, Sink* log);

// One pass: read a received frame into the node, then move the next outgoing frame through
// media access.
void radio_service(Node* node, Outbox* outbox, uint32_t now_ms);

// spec 12.4.2 step 3 - retune to `phy` once the outbox is empty and no frame is moving,
// so an answer already queued goes out on the settings it was asked on. A second request
// before the first is applied replaces it.
void radio_request_phy(const lran::link::PhyConfig& phy);

// True once, on the pass after a requested retune was applied.
bool radio_take_retuned();

const lran::link::PhyConfig& radio_phy();

bool              radio_ready();
const RadioStats& radio_stats();

}  // namespace simnode
