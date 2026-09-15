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

// The OLED, where a board has one (BF-9). A second peripheral, so a second struct - the
// radio's rule applied again, and the shape the bridge's board_ui.h settled on.
//
// PROVENANCE. The range test's board_config.h (kHeltecV3Ui, kXiaoWioKitUi), where both panels
// are confirmed on hardware; the Heltec values also match the bridge's board_ui.h. Values
// carry over; range-test code does not.
struct PanelPins {
  uint8_t addr;             // SSD1306 I2C address
  int8_t  sda;
  int8_t  scl;
  int8_t  rst;              // panel reset line, or kPinNone when the panel has none
  int8_t  vext;             // Vext enable, ACTIVE LOW, or kPinNone when powered directly
  // Set for two different reasons: the V3 mounts its panel rotated; the XIAO expansion
  // board does not, but its enclosure holds the stack inverted. The draw site need not know.
  bool    flip_vertically;
};

inline constexpr PanelPins kHeltecV3Panel = {
    /* addr            */ 0x3C,
    /* sda             */ 17,
    /* scl             */ 18,
    /* rst             */ 21,
    /* vext            */ 36,
    /* flip_vertically */ true,
};

// Seeeduino XIAO Expansion Board, under the XIAO + Wio-SX1262 Kit. The SSD1306 sits on the
// XIAO's D4/D5 I2C pads, with no reset line and no Vext. The Kit's radio lines cross the B2B
// connector (GPIO 38-42) and never reach those pads; the header-board Wio (p-6379) would
// put NSS and RF_SW on GPIO 5 and 6, straight onto this bus.
inline constexpr PanelPins kXiaoExpansionPanel = {
    /* addr            */ 0x3C,
    /* sda             */ 5,
    /* scl             */ 6,
    /* rst             */ kPinNone,
    /* vext            */ kPinNone,
    /* flip_vertically */ true,
};

// A silent collision between the two pin maps would be a panel that blanks the radio, or the
// reverse. Checked here because both structs are here. kPinNone collides with nothing.
constexpr bool pin_in_radio(int8_t pin, const RadioPins& r) {
  if (pin == kPinNone) return false;
  return pin == r.nss || pin == r.rst || pin == r.busy || pin == r.dio1 || pin == r.sck ||
         pin == r.miso || pin == r.mosi || pin == r.rf_sw;
}
constexpr bool panel_collides(const PanelPins& p, const RadioPins& r) {
  return pin_in_radio(p.sda, r) || pin_in_radio(p.scl, r) || pin_in_radio(p.rst, r) ||
         pin_in_radio(p.vext, r);
}
static_assert(!panel_collides(kHeltecV3Panel, kHeltecV3Radio),
              "Heltec V3: an OLED pin collides with a radio pin");
static_assert(!panel_collides(kXiaoExpansionPanel, kXiaoWioKitRadio),
              "XIAO Kit: an expansion-board OLED pin collides with a radio pin");

#if defined(LRAN_PROFILE_HELTEC)
inline constexpr const char*      kBoardName = "heltec_wifi_lora_32_V3";
inline constexpr RadioPins        kRadio     = kHeltecV3Radio;
inline constexpr const PanelPins* kPanel     = &kHeltecV3Panel;
#elif defined(LRAN_PROFILE_XIAO_WIO_KIT)
inline constexpr const char*      kBoardName = "xiao_esp32s3+wio_sx1262_kit";
inline constexpr RadioPins        kRadio     = kXiaoWioKitRadio;
inline constexpr const PanelPins* kPanel     = &kXiaoExpansionPanel;
#endif

}  // namespace simnode
