// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The SX1262 as a receiver and nothing else. D1 frequency change brief 5; M25.
//
// THE ONLY FILE IN THIS IMAGE THAT INCLUDES RADIOLIB, and it calls nothing that transmits:
// no transmit, no startTransmit, no channel scan. A CAD is left out too, although it emits
// nothing, because the bridge's sampler counts CAD time as a skip and this one has no
// reason to be deaf. tools/checks/chan_capture_never_transmits.py fails the build's CI job
// if any of those calls appears anywhere under src/.
//
// WHY NEVER TRANSMIT. Brief 5 runs several of these a metre apart, each on a candidate
// frequency 0.2 MHz from the next. A receiver that polled, as the capture-only bridge
// images do, would put its own frames into its neighbours' captures as excursions, at an
// offset where the receiver's rejection has not been measured.

#pragma once

#include <cstdint>

#include "lran/link/radio_config.h"

namespace chancap {

struct ReceiverStats {
  uint32_t begin_failures    = 0;
  int16_t  last_begin_status = 0;
  uint32_t frames_heard      = 0;  // RX_DONE with a good CRC: an LRAN-PHY frame on the channel
  uint32_t frames_bad_crc    = 0;

  // Receive restarts on the 100 ms timer (receiver.cpp says why), and those that failed.
  uint32_t restarts         = 0;
  uint32_t restart_failures = 0;
};

// Tunes to `freq_hz` with every other PHY field from `phy` (spec 12.1), and enters continuous
// receive. A failure is retried from receiver_service() every 10 s.
void receiver_start(const lran::link::RadioPins& pins, const lran::link::PhyConfig& phy,
                    uint32_t freq_hz, uint32_t now_ms);

// One pass before the sample: clears a completed reception so DIO1 can fire again. Returns
// true when a frame finished, CRC good or bad - the caller notes it against the open bucket.
bool receiver_service(uint32_t now_ms);

// One pass after the sample: restarts receive when 100 ms have passed since the last restart,
// so the next sample, 10 ms later, reads a receiver that cannot have been stuck for longer.
void receiver_after_sample(uint32_t now_ms);

// For the `radio` and `restart` console commands. Called from the sampler task only, like
// everything else here: it is the one task that touches the SPI bus.
struct RadioProbe {
  uint32_t irq          = 0;  // GetIrqStatus, raw SX126x bits
  int16_t  rssi_dbm10   = 0;
  bool     ready        = false;
};
RadioProbe receiver_probe();
bool       receiver_restart(uint32_t now_ms);  // true when receive came back up

bool receiver_ready();

// GET_RSSI_INST, in tenths of a dBm: whatever the receiver hears right now. Only valid while
// receiver_ready().
int16_t receiver_rssi_dbm10();

const ReceiverStats& receiver_stats();

}  // namespace chancap
