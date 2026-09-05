// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R2 - the injected radio pin map (spec 12.2).
//
// Deliberately free of every Arduino and RadioLib header so the map is host
// testable. `kPinNone` stands in for RADIOLIB_NC; radio_link.cpp is the only
// file that translates one to the other. A pin map that can only be compiled
// for the target is a pin map no host test can check.

#pragma once

#include <cstddef>
#include <cstdint>

namespace rangetest {

// RADIOLIB_NC is -1. Spelled locally so this header pulls in no driver.
inline constexpr int8_t kPinNone = -1;

// spec 12.2 - the pin map, TCXO reference voltage and RF-switch mode are supplied
// by configuration at construction, never compiled in. Pass 1 has one board type,
// which makes the seam look like ceremony; it is the reason pass 2 is a config
// addition rather than a rewrite (task R2), and four firmwares depend on it.
//
// TCXO voltage is TENTHS OF A VOLT, not a float. The value is compared and printed
// in the settings dump (R3) and a float that prints as "1.8" but compares unequal to
// 1.8f is a debugging session nobody needs. spec 12.2's own figure is 1.8 V.
struct BoardRadioConfig {
  // Full name. Goes in the R3 serial settings dump, which is what a CSV is
  // correlated against, so it is spelled out in full and width does not matter.
  const char* name;

  // OLED name. The panel is 128 px and the full board name does not fit - it
  // rendered as "heltec_wifi_lora_32_3" on hardware, one character clipped, which is
  // the kind of silent truncation that makes a display untrustworthy. Kept as a
  // separate field rather than trimmed at the draw site so the limit lives with the
  // board it belongs to, and pass 2's entry has to answer the same question.
  const char* short_name;

  int8_t nss;
  int8_t rst;
  int8_t busy;
  int8_t dio1;

  int8_t sck;
  int8_t miso;
  int8_t mosi;

  // kPinNone when DIO2 alone drives the RF switch. Present rather than absent so the
  // struct shape is identical across profiles and the driver needs no #ifdef
  // (Bridge Impl Plan 10.8.1).
  int8_t rf_sw;

  uint16_t tcxo_mv;
  bool     dio2_as_rf_switch;
};

// Heltec WiFi LoRa 32 V3 - SX1262 on a dedicated internal SPI bus.
//
// PIN PROVENANCE, because task R2 requires it and "from memory or a forum post" is
// what it rules out. Every GPIO below is transcribed from the vendor board
// definition shipped with the Arduino core:
//
//   ~/.platformio/packages/framework-arduinoespressif32/
//       variants/heltec_wifi_lora_32_V3/pins_arduino.h
//
// read at framework-arduinoespressif32 3.20017.241212+sha.dcc1105b, which is the
// version espressif32@6.13.0 resolves - the platform this project pins. Recorded
// because "the vendor definition" is only a checkable claim with a version on it.
//
//   SS = 8   SCK = 9   MISO = 11   MOSI = 10   RST_LoRa = 12   BUSY_LoRa = 13
//
// They agree, value for value, with Bridge Impl Plan 10.8.1's LRAN_PROFILE_HELTEC
// entry, which described itself as "the community-standard V3 assignment" and
// derived rather than transcribed. It is now confirmed against the vendor variant.
//
// ONE NAMING TRAP. The variant header calls GPIO 14 `DIO0`, which is the SX127x
// name; on the SX1262 that line is DIO1 and it is what RadioLib wants as its IRQ
// pin. The number is right and the label is legacy - do not "correct" it to a
// different GPIO on the strength of the name.
//
// The two silent-failure settings (task R2, spec 12.2) are here on the first commit
// rather than added after an afternoon of debugging a radio that reports a
// successful transmit and puts nothing on the air:
//   - tcxo_mv 1800. The V3 uses a TCXO, not a crystal. Wrong value presents as a
//     radio that will not calibrate, not as an error.
//   - dio2_as_rf_switch true. The V3 switches its RF path from DIO2; without it the
//     PA is never connected to the antenna.
inline constexpr BoardRadioConfig kHeltecV3 = {
    /* name              */ "heltec_wifi_lora_32_V3",
    /* short_name        */ "Heltec V3",
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

// XIAO ESP32S3 + Wio-SX1262 KIT (Seeed p-5982) - the B2B variant.
//
// THE NAME CARRIES "Kit" ON PURPOSE. Seeed sells two Wio-SX1262 products and they are
// NOT pin-compatible outside the three SPI nets (Pass 2 Tasks 2.1):
//
//   Kit    "Wio-SX1262 with XIAO ESP32S3" (p-5982)  B2B connector   <- THIS ENTRY
//   Header "Wio-SX1262 for XIAO"          (p-6379)  2.54 mm headers
//
// A constant named `kXiaoWio` would be the wrong map half the time and would look
// right both times. The board in hand was identified by INTERCONNECT, not silkscreen -
// the stack is zip-tied and the underside is inaccessible - and the B2B connection is
// what separates the two products, so it is the stronger discriminator anyway.
//
// PIN PROVENANCE, to the standard the Heltec entry set. The control lines are NOT on
// the XIAO's D-pad header at all; they cross the B2B connector, which is why they are
// GPIO 38-42 and not 1-9. Transcribed from:
//
//   meshtastic/firmware  variants/esp32s3/seeed_xiao_s3/variant.h   (the Kit variant)
//
// corroborated for the SPI nets by the vendor board definition at the framework
// version this project pins (espressif32@6.13.0):
//
//   ~/.platformio/packages/framework-arduinoespressif32/
//       variants/XIAO_ESP32S3/pins_arduino.h
//   D0..D10 = 1, 2, 3, 4, 5, 6, 43, 44, 7, 8, 9   SCK 7  MISO 8  MOSI 9
//
// THIS ENTRY DOES NOT VALIDATE GATELINK'S CARRIER. Bridge Impl Plan 2.3.1 finding 2
// asked whether the kit's module would match the carrier's; it does not. The Kit
// validates the SX1262 silicon, the module's RF performance, RadioLib against a second
// board, and the injected-config seam - not the carrier's net list. See Pass 2 Tasks
// 2.2 before quoting a result from this board as a GateLink result.
//
// THREE SILENT-FAILURE SETTINGS HERE, not two. The Heltec has tcxo_mv and
// dio2_as_rf_switch; this board adds a real `rf_sw`, because Seeed does NOT tie DIO2
// to the RF switch internally (gatelink-expansion-board 7.3). All three fail the same
// way - the radio initialises, reports a successful transmit, and puts nothing on the
// air.
inline constexpr BoardRadioConfig kXiaoWioKit = {
    /* name              */ "xiao_esp32s3_wio_kit",
    /* short_name        */ "XIAO+Wio",
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

// GATELINK CARRIER SLOT - StamPLC + Wio-SX1262 header board. NOT POPULATED.
//
// Left as a declaration for the same reason pass 1 left one for the XIAO, and the
// reasoning has now been vindicated twice. gatelink-expansion-board.md 6 carries the
// carrier's D-pad column and 11's "ring out each D-pad" checkbox is STILL OPEN - the
// Kit cannot tick it, because the Kit does not use those pads (see kXiaoWioKit above).
//
// What changed in pass 2 is only the confidence, not the status: the carrier's pad
// table gained an INDEPENDENT corroboration (meshtastic/firmware issue #8409, the
// header-board map) that matches it value for value. Two derivations agreeing is not a
// continuity check. Populate this from the carrier itself, after ring-out, during
// GateLink's own bring-up.
//
// TODO(GateLink-M1): populate from gatelink-expansion-board rev 0.3, after ring-out.

// ---------------------------------------------------------------------------
// The operator interface - display and role button.
//
// SEPARATE FROM BoardRadioConfig ON PURPOSE. That struct is cited by spec 12.2 and is
// injected into a driver that four firmwares share; widening it to carry display pins
// would blur a seam those firmwares depend on. GateLink is headless and needs the
// radio config alone, which is the clearest argument for the split.
//
// Pass 1 had one board, so these lived as constants in ui_oled.h and role.h. A second
// board is what turned them into configuration - the same lesson R2 learned early for
// the radio, arriving late for the panel (Pass 2 Tasks 1).
// ---------------------------------------------------------------------------
struct BoardUiConfig {
  int8_t sda;
  int8_t scl;

  // kPinNone when the panel has no reset line of its own.
  int8_t rst;

  // kPinNone when the panel is powered directly. ACTIVE LOW where present.
  int8_t vext;

  // The R1 role selector. Active LOW with a pull-up on both boards, so role.cpp needs
  // no polarity concept - if a third board ever inverts it, that is a field here and a
  // finding for the log, not an #ifdef at the read site.
  int8_t role_button;

  uint8_t addr;

  // The V3's panel is mounted rotated; the XIAO expansion board's is not. Silent and
  // cosmetic rather than dangerous, but an upside-down display at the far end of a
  // walk is not something to discover there.
  bool flip_vertically;
};

// Heltec V3 - vendor variant pins_arduino.h: SDA_OLED 17, SCL_OLED 18, RST_OLED 21,
// Vext 36. Identical to the values wattcycle-reader confirmed on hardware, and the
// bring-up ORDER (Vext, reset pulse, I2C, probe) is lifted from there too - see
// ui_oled.cpp. A dark panel on this board is usually Vext, not the driver.
//
// role_button: the PRG button on GPIO 0. NOT in the vendor variant, which stops at the
// LoRa and OLED pins, so unlike every other value here it is not transcribed.
// CONFIRMED BEHAVIOURALLY ON HARDWARE 2026-08-31 on both boards, which is the stronger
// check - what matters is that the button reaches this GPIO, and it does.
inline constexpr BoardUiConfig kHeltecV3Ui = {
    /* sda             */ 17,
    /* scl             */ 18,
    /* rst             */ 21,
    /* vext            */ 36,
    /* role_button     */ 0,
    /* addr            */ 0x3c,
    /* flip_vertically */ true,
};

// Seeeduino XIAO Expansion Board - SSD1306 on the XIAO's standard I2C pads, no Vext
// and no reset line.
//
// SDA 5 / SCL 6 are D4 / D5, from the vendor variant above AND from the Kit's
// Meshtastic variant (`I2C_SDA 5`, `I2C_SCL 6`) - and the assembled stack drives this
// panel on stock firmware, so these two are confirmed on hardware before pass 2 wrote
// a line.
//
// THE COLLISION THAT DID NOT HAPPEN, recorded because it was luck rather than design:
// the HEADER-BOARD variant puts NSS on GPIO 5 and RF_SW on GPIO 6 - directly on top of
// this I2C bus. The Kit's B2B pads are not brought out to the D-pad header at all, so
// radio and display coexist here and would not have on the other product.
// `has_pin_conflict()` below makes that a compile-time check rather than a memory.
//
// role_button 21: the user button on TOP OF THE WIO BOARD, reached across the B2B
// connector - GPIO 21 is not one of the XIAO's D-pads. Meshtastic's Kit variant calls
// it the program button and declares BUTTON_NEED_PULLUP, so it is active low with a
// pull-up, matching the Heltec convention.
//
// DELIBERATELY NOT GPIO 0 ON THIS BOARD. The R1 gesture is a press held across a reset
// (role.h), and GPIO 0 is the ESP32-S3 BOOT strapping pin: the Heltec gets away with it
// because the selection happens in a window AFTER boot, but there is no reason to point
// a second board at the download-mode strap when a plain GPIO is available. The
// expansion board's own D1 (GPIO 2) is left unclaimed for a future second control.
inline constexpr BoardUiConfig kXiaoWioKitUi = {
    /* sda             */ 5,
    /* scl             */ 6,
    /* rst             */ kPinNone,
    /* vext            */ kPinNone,
    /* role_button     */ 21,
    /* addr            */ 0x3c,
    /* flip_vertically */ false,
};

// ---------------------------------------------------------------------------
// Pin collision check - host testable, and a static_assert below.
//
// Every pin a board drives, in one list: if two of them are the same GPIO, one of the
// two peripherals does not work and the symptom is attributed to the wrong subsystem.
// kPinNone is "not connected" and never collides with anything, including itself.
//
// This exists because of a near miss, not a theory - see kXiaoWioKitUi. It is cheap,
// it is arithmetic, and it protects the NEXT board added to this file rather than the
// two already here.
// ---------------------------------------------------------------------------
constexpr bool has_pin_conflict(const BoardRadioConfig& r, const BoardUiConfig& u) {
  const int8_t pins[] = {r.nss, r.rst,  r.busy, r.dio1, r.sck,        r.miso,
                         r.mosi, r.rf_sw, u.sda, u.scl, u.rst, u.vext, u.role_button};
  const size_t n = sizeof(pins) / sizeof(pins[0]);
  for (size_t i = 0; i < n; ++i) {
    if (pins[i] == kPinNone) continue;
    for (size_t j = i + 1; j < n; ++j) {
      if (pins[j] == kPinNone) continue;
      if (pins[i] == pins[j]) return true;
    }
  }
  return false;
}

static_assert(!has_pin_conflict(kHeltecV3, kHeltecV3Ui),
              "Heltec V3 pin map has two peripherals on one GPIO");
static_assert(!has_pin_conflict(kXiaoWioKit, kXiaoWioKitUi),
              "XIAO+Wio Kit pin map has two peripherals on one GPIO");

// ---------------------------------------------------------------------------
// THE ONE SELECTION POINT (task X3).
//
// The preprocessor picks WHICH INSTANCE and nothing else. It does not reach into the
// driver, the role logic or the UI - that is the whole of R-4.1b, and it is why
// GateLink can add a third instance without touching radio_link.cpp.
//
// The Heltec is the default so that an unflagged build - including the `native` test
// environment - is pass 1 unchanged.
//
// A WRONG SELECTION HERE IS QUIET. The board still boots, still displays, and writes
// the wrong pin map and the wrong antenna gain into a CSV that looks entirely normal.
// The R3 settings dump prints `kBoard.name` for exactly this reason: read the board
// name off the dump before trusting the first trace (Pass 2 Tasks 4.0.1).
// ---------------------------------------------------------------------------
#if defined(LRAN_BOARD_XIAO_WIO_KIT)
inline constexpr const BoardRadioConfig& kBoard   = kXiaoWioKit;
inline constexpr const BoardUiConfig&    kBoardUi = kXiaoWioKitUi;
#else
inline constexpr const BoardRadioConfig& kBoard   = kHeltecV3;
inline constexpr const BoardUiConfig&    kBoardUi = kHeltecV3Ui;
#endif

}  // namespace rangetest
