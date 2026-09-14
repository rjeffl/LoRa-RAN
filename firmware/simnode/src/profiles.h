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
// PROVENANCE. The bridge's board_ui.h values (SDA_OLED 17, SCL_OLED 18, RST_OLED 21, Vext 36,
// from the vendor variant), confirmed on this board model by the bridge, /wattcycle-reader
// and the range test. Values carry over; the bridge's code does not.
struct PanelPins {
  uint8_t addr;             // SSD1306 I2C address
  int8_t  sda;
  int8_t  scl;
  int8_t  rst;              // panel reset line
  int8_t  vext;             // Vext enable, ACTIVE LOW - the panel is powered through it
  bool    flip_vertically;  // the V3 mounts its panel rotated
};

inline constexpr PanelPins kHeltecV3Panel = {
    /* addr            */ 0x3C,
    /* sda             */ 17,
    /* scl             */ 18,
    /* rst             */ 21,
    /* vext            */ 36,
    /* flip_vertically */ true,
};

// A silent collision between the two pin maps would be a panel that blanks the radio, or the
// reverse. Checked here because both structs are here.
constexpr bool pin_in_radio(int8_t pin, const RadioPins& r) {
  return pin == r.nss || pin == r.rst || pin == r.busy || pin == r.dio1 || pin == r.sck ||
         pin == r.miso || pin == r.mosi || (r.rf_sw != kPinNone && pin == r.rf_sw);
}
static_assert(!pin_in_radio(kHeltecV3Panel.sda, kHeltecV3Radio) &&
                  !pin_in_radio(kHeltecV3Panel.scl, kHeltecV3Radio) &&
                  !pin_in_radio(kHeltecV3Panel.rst, kHeltecV3Radio) &&
                  !pin_in_radio(kHeltecV3Panel.vext, kHeltecV3Radio),
              "Heltec V3: an OLED pin collides with a radio pin");

#if defined(LRAN_PROFILE_HELTEC)
inline constexpr const char*      kBoardName = "heltec_wifi_lora_32_V3";
inline constexpr RadioPins        kRadio     = kHeltecV3Radio;
inline constexpr const PanelPins* kPanel     = &kHeltecV3Panel;
#elif defined(LRAN_PROFILE_XIAO_WIO_KIT)
inline constexpr const char*      kBoardName = "xiao_esp32s3+wio_sx1262_kit";
inline constexpr RadioPins        kRadio     = kXiaoWioKitRadio;
// The Kit is the XIAO and the Wio module on a B2B connector. There is no display on it, so
// armed faults on this board show only on the console (`fault list`, `id list`).
inline constexpr const PanelPins* kPanel     = nullptr;
#endif

}  // namespace simnode
