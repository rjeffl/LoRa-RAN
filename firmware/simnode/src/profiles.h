// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Hardware profiles - one RadioPins instance per board. Task BF-2; Impl Plan 10.8, 10.8.1;
// spec 12.2; R-4.1b.
//
// A PROFILE SUPPLIES EXACTLY ONE THING. Everything above the driver is identical across
// boards, and the pin map reaches the driver as an argument, never through the
// preprocessor: GateLink's firmware supplies a third instance of this struct with no
// driver change, which is the whole of R-4.1b.
//
// Both instances are always compiled, so the host tests check both. The build flag only
// chooses which one kRadio names.
//
// PROVENANCE. Values from Impl Plan 10.8.1, which is their one home - correct them there
// first. Heltec: the vendor variant pins_arduino.h at framework-arduinoespressif32
// 3.20017.241212, confirmed over the air by the range test and the bridge. Kit: meshtastic
// firmware variants/esp32s3/seeed_xiao_s3/variant.h, confirmed over the air by range-test
// pass 2 (192 frames out, 192 echoes back). The values carry over; range-test code does not.

#pragma once

#include "lran/link/radio_config.h"

namespace simnode {

using lran::link::kPinNone;
using lran::link::RadioPins;

// Heltec WiFi LoRa 32 V3. GPIO 14 is DIO1, whatever the vendor header's `DIO0` says.
// tcxo_mv and dio2_as_rf_switch both fail silently when wrong.
inline constexpr RadioPins kHeltecV3Radio = {
    /* nss               */ 8,
    /* rst               */ 12,
    /* busy              */ 13,
    /* dio1              */ 14,
    /* sck               */ 9,
    /* miso              */ 11,
    /* mosi              */ 10,
    /* rf_sw             */ kPinNone,
    /* tcxo_mv           */ 1800,
    /* dio2_as_rf_switch */ true,
};

// XIAO ESP32S3 + Wio-SX1262 KIT (Seeed p-5982), B2B connector - the board in hand. NOT the
// header board (p-6379), which is GateLink's module and shares only the three SPI nets.
// The control lines cross the B2B connector, which is why they are GPIO 38-42.
//
// rf_sw is a real pin: Seeed does not tie DIO2 to the RF switch inside the module, so this
// board needs setRfSwitchPins(rf_sw, NC) AND dio2_as_rf_switch. A wrong rf_sw initialises
// cleanly and transmits into a dead end.
inline constexpr RadioPins kXiaoWioKitRadio = {
    /* nss               */ 41,
    /* rst               */ 42,
    /* busy              */ 40,
    /* dio1              */ 39,
    /* sck               */ 7,
    /* miso              */ 8,
    /* mosi              */ 9,
    /* rf_sw             */ 38,
    /* tcxo_mv           */ 1800,
    /* dio2_as_rf_switch */ true,
};

#if defined(LRAN_PROFILE_HELTEC)
inline constexpr const char* kBoardName = "heltec_wifi_lora_32_V3";
inline constexpr RadioPins   kRadio     = kHeltecV3Radio;
#elif defined(LRAN_PROFILE_XIAO_WIO_KIT)
inline constexpr const char* kBoardName = "xiao_esp32s3+wio_sx1262_kit";
inline constexpr RadioPins   kRadio     = kXiaoWioKitRadio;
#endif

}  // namespace simnode
