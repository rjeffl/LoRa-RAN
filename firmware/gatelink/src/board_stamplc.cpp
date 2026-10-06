// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The board layer over M5StamPLC. L6, GL1; GateLink Impl Plan 5.3.
//
// Read the INSTALLED M5StamPLC headers under .pio/libdeps/, not GitHub: the published
// package has drifted from its main branch (wattcycle-reader's README).

#include "board_stamplc.h"

#include <M5StamPLC.h>
#include <SD.h>
#include <SPI.h>

#include <cmath>
#include <cstring>

#include "board_profile.h"
#include "spi_bus.h"

namespace gatelink {
namespace {

// The case's bezel covers the panel's leftmost pixels, cutting the first character of a
// row drawn at x = 0. Half a character clears it (bench, 2026-10-02).
constexpr int32_t kInsetX = 6;

// The relay and input expander, "IO expander B" in M5StamPLC 1.2.0 (aw9523.h's default
// address). Relays are P0_0-P0_3; inputs are P0_4-P0_7 and P1_4-P1_7 (M5StamPLC.cpp).
constexpr uint8_t  kExpanderB  = 0x59;
constexpr uint8_t  kRegInput0  = 0x00;
constexpr uint8_t  kRegOutput0 = 0x02;
constexpr uint32_t kI2cHz      = 400000;
constexpr uint8_t  kRelayBits  = 0x0F;

bool g_sd_up = false;

}  // namespace

bool board_relays_off_early() {
  // The internal bus's pins, from M5StamPLC's pin_config.h. M5StamPLC.begin() releases
  // and restarts the bus later, so starting it here costs nothing.
  m5::In_I2C.begin(I2C_NUM_0, STAMPLC_PIN_I2C_INTER_SDA, STAMPLC_PIN_I2C_INTER_SCL);
  // The latch alone, not the direction. A relay pin still configured as an output goes low
  // here. io_expander_b_init() then sets each relay pin to an output BEFORE it writes the
  // pin low, which with a 1 left in the latch would close the relay for one I2C
  // transaction; with 0 here, it does not.
  return m5::In_I2C.writeRegister8(kExpanderB, kRegOutput0, 0x00, kI2cHz);
}

void board_begin() {
  // The global SPI gets the carrier's bus pins before M5StamPLC.begin(). SD.begin() would
  // otherwise start it on the board definition's defaults, G11-G13: BUSY and the internal
  // I2C's SDA (GL0 bring-up).
  SPI.begin(kCarrierRadio.sck, kCarrierRadio.miso, kCarrierRadio.mosi, -1);
  // Whether every relay output stays off through this, a watchdog reset and a brownout is
  // GL1's scope check (PRD R-3.5j).
  M5StamPLC.begin();
  M5StamPLC.setBacklight(true);
  // GateLink mounts the StamPLC upside down (bench, 2026-10-02). Turning from the library's
  // landscape default keeps that default's offset and panel size, whatever M5GFX sets.
  auto& d = M5StamPLC.Display;
  d.setRotation((d.getRotation() + 2) & 3);
}

bool board_write_relays(uint8_t mask) {
  // Bits 4-7 of this latch belong to input pins, where the latch has no effect.
  return m5::In_I2C.writeRegister8(kExpanderB, kRegOutput0, mask & kRelayBits, kI2cHz);
}

bool board_read_inputs(uint8_t* raw) {
  uint8_t port[2] = {};
  if (!m5::In_I2C.readRegister(kExpanderB, kRegInput0, port, sizeof(port), kI2cHz)) {
    return false;
  }
  *raw = static_cast<uint8_t>((port[0] >> 4) | (port[1] & 0xF0));
  return true;
}

uint8_t board_poll_buttons() {
  M5StamPLC.update();
  uint8_t pressed = 0;
  if (M5StamPLC.BtnA.wasPressed()) pressed |= 0x01;
  if (M5StamPLC.BtnB.wasPressed()) pressed |= 0x02;
  if (M5StamPLC.BtnC.wasPressed()) pressed |= 0x04;
  return pressed;
}

BoardSensors board_read_sensors() {
  // The library returns floats with no failure flag; a NaN is the only failure it can show.
  BoardSensors s;
  const float t = M5StamPLC.getTemp();
  if (std::isfinite(t)) s.temp_c10 = static_cast<int16_t>(std::lround(t * 10.0f));
  const float v = M5StamPLC.getPowerVoltage();
  if (std::isfinite(v) && v >= 0.0f) s.bus_mv = static_cast<uint16_t>(std::lround(v * 1000.0f));
  // The library calls this the "IO socket output current" (M5StamPLC.h). Expansion board
  // 7.7 reads it as the bank's current, and plan 9.8 as the node's. M12 decides.
  const float a = M5StamPLC.getIoSocketOutputCurrent();
  if (std::isfinite(a)) s.shunt_ma = static_cast<int32_t>(std::lround(a * 1000.0f));
  return s;
}

bool board_rtc_get(std::tm* out) {
  std::memset(out, 0, sizeof(*out));
  M5StamPLC.getRtcTime(out);
  return out->tm_year != 0 || out->tm_mday != 0;
}

void board_show(const PageText& page) {
  SpiLock lock;
  auto& d = M5StamPLC.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setTextDatum(top_left);
  d.setTextSize(2);  // 12x16 pixels a character; ui_pages.h sizes the lines to it
  for (size_t i = 0; i < page.count; ++i) {
    d.drawString(page.line[i], kInsetX, static_cast<int32_t>(i * 16));
  }
}

void board_beep(uint16_t freq_hz, uint16_t ms) { M5StamPLC.tone(freq_hz, ms); }

bool board_sd_begin() {
  SpiLock lock;
  if (!g_sd_up) g_sd_up = SD.begin(kSdCs, SPI, 4000000);
  return g_sd_up;
}

bool board_sd_append(const char* path, const char* line) {
  SpiLock lock;
  if (!g_sd_up) return false;
  File f = SD.open(path, FILE_APPEND, true);
  if (!f) return false;
  const size_t n  = std::strlen(line);
  const bool   ok = f.write(reinterpret_cast<const uint8_t*>(line), n) == n &&
                  f.write(static_cast<uint8_t>('\n')) == 1;
  f.close();
  return ok;
}

}  // namespace gatelink
