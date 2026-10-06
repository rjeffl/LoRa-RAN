// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The board layer: everything GateLink reaches through the M5StamPLC library. L6 started
// it with the panel; GL1 adds the relays, inputs, buttons, buzzer, INA226, LM75, RTC and
// microSD (GateLink Impl Plan 5.3).
//
// FIRMWARE-LOCAL, by operator decision 2026-10-01, and written so it can move to /lib/
// unchanged when a second StamPLC firmware is built here (Impl Plan 5.3, doc-findings 9).
//
// TWO BUSES, TWO RULES.
//   Internal I2C - the relay and input expander, the buttons' expander, INA226, LM75 and
//     RTC. io_task is the only caller once tasks run (Impl Plan 5.2). Whether M5Unified's
//     I2C class locks is not established, so nothing else touches the bus.
//   SPI - the LCD and the microSD, with the radio. Every call here that reaches them takes
//     the SpiLock (spi_bus.h) itself.

#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

#include "ui_pages.h"

namespace gatelink {

// PRD R-3.5j. Writes 0 to the relay expander's output latch before anything else runs.
// The AW9523B is outside the ESP32's reset domain: a watchdog reset or panic in the middle
// of a pulse leaves the relay energized until M5StamPLC.begin() reaches it. setup() calls
// this first. Returns false if the expander did not acknowledge.
bool board_relays_off_early();

// SPI on the carrier's pins, then M5StamPLC.begin(): the internal I2C bus, both IO
// expanders, the LM75, INA226 and RTC, and the panel. Then the backlight on.
void board_begin();

// ---- io_task only ---------------------------------------------------------------------

// Bit n drives K(n+1). One I2C write of the whole latch, so two relays never change on
// separate transactions. Returns false if the expander did not acknowledge.
bool board_write_relays(uint8_t mask);

// IN1-IN8 as bits 0-7, at the opto's output level. Returns false on an I2C failure, and
// leaves *raw unchanged.
bool board_read_inputs(uint8_t* raw);

// Reads the three front buttons. Bit 0 is A, 1 is B, 2 is C; a bit is set once per press.
uint8_t board_poll_buttons();

// Sentinels, not zero, for a reading the board could not take (root rule 6).
struct BoardSensors {
  int16_t  temp_c10 = INT16_MIN;   // LM75, 0.1 degC
  uint16_t bus_mv   = UINT16_MAX;  // INA226 bus voltage
  int32_t  shunt_ma = INT32_MIN;   // INA226 shunt current; which current is measurement M12
};
BoardSensors board_read_sensors();

bool board_rtc_get(std::tm* out);

// ---- any task -------------------------------------------------------------------------

// Clears the panel and draws the page, under the SpiLock.
void board_show(const PageText& page);

// Non-blocking: the buzzer runs on a LEDC timer.
void board_beep(uint16_t freq_hz, uint16_t ms);

// Mounts the card under the SpiLock. FAT32 only; the core's FatFs has no exFAT
// (engineering log, 2026-10-06).
bool board_sd_begin();

// Appends one line to a file on the card, under the SpiLock. False if the card is not
// mounted or the write fell short.
bool board_sd_append(const char* path, const char* line);

}  // namespace gatelink
