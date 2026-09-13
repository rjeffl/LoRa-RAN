// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The OLED, drawn. Task BF-14; R-4.1c, Impl Plan 5.1.1.
//
// ui_task is the only caller. The display and Wire are touched from nowhere else, so
// neither needs a lock.

#pragma once

#include "board_ui.h"
#include "status_page.h"

namespace bridge {

// Vext on, panel reset pulse, I2C, probe - in that order, which is the order verified
// on this board by /wattcycle-reader and the range test. Returns false if the panel
// does not ACK. A missing or dead panel is REPORTED and then IGNORED: R-4.1c is
// MAY-level, and a bridge that stops relaying the property's telemetry because its
// status display failed has its priorities backwards.
bool ui_begin(const BoardUiConfig& cfg);

void ui_render(const StatusLines& lines, int16_t x_shift);

}  // namespace bridge
