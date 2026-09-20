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
// THE PANEL IS SWITCHED OFF, NOT MERELY LEFT ALONE. This image draws nothing, but an SSD1306
// keeps its last frame for as long as it has power. The XIAO expansion board's panel has no
// reset line and no Vext, so after a reflash from the simnode it went on showing "f1
// ROLE_GATELINK" over a board that was sampling (operator, 2026-09-19). Switching it off at
// boot makes a dark panel mean what it looks like, and stops its charge pump running beside
// a receiver whose floor is the measurement.

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

// Only what switching the panel off needs. Values from firmware/simnode/src/profiles.h,
// where both panels are confirmed on hardware.
struct PanelPins {
  uint8_t addr;  // SSD1306 I2C address
  int8_t  sda;
  int8_t  scl;
  int8_t  vext;  // Vext enable, ACTIVE LOW, or kPinNone when the panel is powered directly
};

// Heltec V3: the panel hangs off Vext, so holding Vext high unpowers it.
inline constexpr PanelPins kHeltecV3Panel = {0x3C, 17, 18, 36};

// XIAO expansion board: powered directly, so it is told to switch off over I2C.
inline constexpr PanelPins kXiaoExpansionPanel = {0x3C, 5, 6, kPinNone};

constexpr bool pin_in_radio(int8_t pin, const RadioPins& r) {
  if (pin == kPinNone) return false;
  return pin == r.nss || pin == r.rst || pin == r.busy || pin == r.dio1 || pin == r.sck ||
         pin == r.miso || pin == r.mosi || pin == r.rf_sw;
}
constexpr bool panel_collides(const PanelPins& p, const RadioPins& r) {
  return pin_in_radio(p.sda, r) || pin_in_radio(p.scl, r) || pin_in_radio(p.vext, r);
}
static_assert(!panel_collides(kHeltecV3Panel, kHeltecV3Radio),
              "Heltec V3: a panel pin collides with a radio pin");
static_assert(!panel_collides(kXiaoExpansionPanel, kXiaoWioKitRadio),
              "XIAO Kit: a panel pin collides with a radio pin");

#if defined(LRAN_PROFILE_HELTEC)
inline constexpr const char* kBoardName = "heltec_wifi_lora_32_V3";
inline constexpr RadioPins   kRadio     = kHeltecV3Radio;
inline constexpr PanelPins   kPanel     = kHeltecV3Panel;
#elif defined(LRAN_PROFILE_XIAO_WIO_KIT)
inline constexpr const char* kBoardName = "xiao_esp32s3+wio_sx1262_kit";
inline constexpr RadioPins   kRadio     = kXiaoWioKitRadio;
inline constexpr PanelPins   kPanel     = kXiaoExpansionPanel;
#endif

}  // namespace chancap
