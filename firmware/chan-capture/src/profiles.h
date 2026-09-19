// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Hardware profiles - one RadioPins instance per board. Spec 12.2; Bridge Impl Plan 10.8.1.
//
// PROVENANCE. The values are firmware/simnode/src/profiles.h's, which took them from Impl
// Plan 10.8.1, their one home - correct them there first. Both are confirmed over the air:
// the Heltec by the range test and the bridge, the Kit by range-test pass 2. The values
// carry over; simnode code does not.
//
// No panel. This image has nothing to show that the capture file does not already record,
// and an OLED left dark cannot be mistaken for a board still running simnode firmware.

#pragma once

#include "lran/link/radio_config.h"

namespace chancap {

using lran::link::kPinNone;
using lran::link::RadioPins;

// Heltec WiFi LoRa 32 V3. tcxo_mv and dio2_as_rf_switch both fail silently when wrong.
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

// XIAO ESP32S3 + Wio-SX1262 KIT (Seeed p-5982), B2B connector. rf_sw is the receive-enable
// line, and this image needs it as much as a transmitter does: a wrong rf_sw initialises
// cleanly and listens to a dead end, which a capture would record as a very quiet channel.
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

}  // namespace chancap
