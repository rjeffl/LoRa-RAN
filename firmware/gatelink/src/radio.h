// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The carrier's SX1262. GL1; GateLink Impl Plan 5.2, 5.3; spec 12.1, 12.2.
//
// THE ONLY FILE IN THIS FIRMWARE, BESIDE THE BRING-UP IMAGE, THAT INCLUDES RADIOLIB. GL1
// needs only enough of a radio to share the SPI bus with the LCD and the microSD under the
// one lock (Impl Plan 8.2, GL1). GL3 grows this into the simnode's radio.cpp: receive,
// media access and transmit.
//
// lora_task is the only caller. Every call takes the SpiLock for the whole operation.

#pragma once

#include <cstdint>

namespace gatelink {

// Configures the radio from kCarrierRadio and lran::link::kPhy. Returns RadioLib's
// status: 0 is success, -2 CHIP_NOT_FOUND, -707 SPI_CMD_TIMEOUT (BUSY never fell).
int16_t radio_begin();

// Reads the LoRa sync word back. True if the read succeeded and returned 0x1424, which is
// what kPhy writes; anything else is a transfer another device's traffic corrupted.
bool radio_probe();

}  // namespace gatelink
