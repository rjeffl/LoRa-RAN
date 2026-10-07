// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The carrier's SX1262. GL1, GL3; GateLink Impl Plan 5.2, 5.3; spec 12.1-12.3, 14.
//
// THE ONLY FILE IN THIS FIRMWARE, BESIDE THE BRING-UP IMAGE, THAT INCLUDES RADIOLIB. It
// moves bytes between the radio and lora_task's engine, and applies lib/lran-link's spec
// 12.3 media access to every outgoing frame, the implementation the bridge and the simnode
// run. The state machine is the simnode's radio.cpp, which follows the bridge's
// lora_link.cpp; the RadioLib traps it names are theirs.
//
// NOTHING HERE WAITS ON THE RADIO. A CAD and a transmission are started, and their
// completion is read from the IRQ register on later passes against a deadline. DIO1 only
// notifies lora_task; the interrupt does no SPI work (Impl Plan 5.2).
//
// lora_task is the only caller. Every call takes the SpiLock for the whole operation.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/counters.h"
#include "lran/node/outbox.h"

namespace gatelink {

// What the driver hands a received frame to, on lora_task.
class RadioClient {
 public:
  virtual ~RadioClient() = default;
  // A frame with a good PHY CRC (spec 14 stage 2 on).
  virtual void on_frame(const uint8_t* buf, size_t len, int16_t rssi_dbm, int16_t snr_db10,
                        uint32_t now_ms) = 0;
  // spec 14 stage 1.
  virtual void on_phy_crc_error(uint32_t now_ms) = 0;
  // The spec 14.1 counters media access writes its CAD figures to.
  virtual lran::Counters* counters() = 0;
};

struct RadioStats {
  uint32_t begin_failures    = 0;
  int16_t  last_begin_status = 0;
  uint32_t rx_frames         = 0;  // RX_DONE with a good CRC, handed to the client
  uint32_t rx_driver_errors  = 0;  // readData() failed other than on the PHY CRC
  uint32_t tx_frames         = 0;  // TX_DONE seen
  uint32_t tx_errors         = 0;  // startTransmit() refused the frame
  uint32_t tx_timeouts       = 0;  // TX_DONE never came
  uint32_t tx_forced         = 0;  // spec 12.3's "transmit regardless"
  uint32_t cad_errors        = 0;
  uint32_t cad_deferred      = 0;  // a CAD not started because a frame was arriving
  // The last RadioLib call that failed, and its status: what a count alone cannot say.
  const char* last_error_at  = "";
  int16_t     last_error     = 0;
};

// Configures the radio from kCarrierRadio and lran::link::kPhy and starts receiving.
// `task` is lora_task's handle, which DIO1 notifies. Returns RadioLib's status: 0 is
// success, -2 CHIP_NOT_FOUND, -705 SPI_CMD_TIMEOUT (BUSY never fell), -707 SPI_CMD_FAILED.
// After a failure, radio_service() tries again every 10 s.
int16_t radio_begin(void* task);

// One pass: hand a received frame to the client, then move the next outgoing frame through
// media access.
void radio_service(RadioClient* client, lran::node::Outbox* outbox, uint32_t now_ms);

// True while a CAD or transmission is in progress, or a frame is backing off: lora_task
// then polls at a short tick rather than waiting on DIO1.
bool radio_tx_active();

// Nothing of ours in media access or on the air. With an empty outbox, every frame queued
// has reached TX_DONE or been given up on - what spec 8.1 waits for before a reset.
bool radio_tx_idle();

bool              radio_ready();
const RadioStats& radio_stats();

// Reads the LoRa sync word back. True if the read succeeded and returned 0x1424, which is
// what kPhy writes; anything else is a transfer another device's traffic corrupted. GL1's
// bus test.
bool radio_probe();

}  // namespace gatelink
