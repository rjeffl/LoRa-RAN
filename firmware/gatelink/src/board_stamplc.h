// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The board layer: everything GateLink reaches through the M5StamPLC library. Task L6
// starts it with the panel alone; GL1 adds the relays, inputs, buttons, buzzer, INA226,
// LM75, RTC and microSD (GateLink Impl Plan 5.3).
//
// FIRMWARE-LOCAL, by operator decision 2026-10-01, and written so it can move to /lib/
// unchanged when a second StamPLC firmware is built here (Impl Plan 5.3, doc-findings 9).

#pragma once

#include "ui_pages.h"

namespace gatelink {

// M5StamPLC.begin(): the internal I2C bus, both IO expanders, the LM75, INA226 and RTC,
// and the panel. Then the backlight on.
void board_begin();

// Clears the panel and draws the page. Not locked: until GL1 adds the SPI lock, only
// setup() calls it, before any task starts.
void board_show(const PageText& page);

}  // namespace gatelink
