// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The carrier's radio, as data. GL0; GateLink Impl Plan 3.3, 4.1; expansion board 6, 9.
//
// THE CARRIER CARRIES THE HEADER BOARD, "Wio-SX1262 for XIAO" (p-6379), confirmed by the
// operator on 2026-10-05. Its control lines are the D-pads that expansion board 6.1 maps.
// The Kit (p-5982) puts them on GPIO 38-42, and a pin map copied from it gives a carrier
// that looks configured and never answers.
//
// ARDUINO-FREE. Root rule 10 and spec 12.2 inject the pin map, TCXO voltage and RF-switch
// mode into the driver as a RadioPins value; the expansion board's #defines are where the
// numbers come from, not how this firmware spells them.

#pragma once

#include <cstdint>

#include "lran/link/radio_config.h"

namespace gatelink {

inline constexpr lran::link::RadioPins kCarrierRadio = {
    /* nss               */ 41,  // Bus 16. R3 pulls it up, so the radio is deselected from power-on
    /* rst               */ 2,   // PORT.A yellow. R4 holds the radio in reset while G2 floats
    /* busy              */ 11,  // Bus 14, which the vendor's pin table labels CS
    /* dio1              */ 1,   // PORT.A white. INPUT_PULLDOWN before begin() (expansion board 7.1.1)
    /* sck               */ 7,   // Bus 12, shared with the LCD and the microSD
    /* miso              */ 9,   // Bus 11
    /* mosi              */ 8,   // Bus 13
    /* rf_sw             */ 40,  // Bus 15, the RX enable in setRfSwitchPins(rf_sw, NC)
    /* tcxo_mv           */ 1800,  // DIO3 powers the Wio's TCXO. The Heltec's value differs
    /* dio2_as_rf_switch */ true,  // as well as rf_sw, not instead of it (expansion board 7.3)
};

// The microSD's chip select, on the radio's bus. M5StamPLC's pin_config.h names it too.
inline constexpr int8_t kSdCs = 10;

// VE.Direct, named from the ESP32's side (expansion board 6 and 9). Victron names its
// connector from the MPPT's side, so its TX, J4 pin 3, is kVedUartRx here.
inline constexpr int8_t   kVedUartTx = 5;  // PORT.C yellow -> converter ch 3 -> J4 pin 2
inline constexpr int8_t   kVedUartRx = 4;  // PORT.C white  -> converter ch 4 -> R2 -> J4 pin 3
inline constexpr uint32_t kVedBaud   = 19200;  // GateLink Impl Plan 4.2

}  // namespace gatelink
