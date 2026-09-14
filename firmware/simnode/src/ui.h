// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The OLED, drawn. Task BF-9; Impl Plan 10.6. The page's text is oled_page.h's.
//
// Called only from loop(), so neither the display nor Wire needs a lock.

#pragma once

#include "oled_page.h"
#include "profiles.h"

namespace simnode {

// Vext on, panel reset pulse, I2C, probe - the bridge's order, verified on the Heltec V3.
// Returns false for a board with no panel (`pins` is nullptr) or a panel that does not ACK.
// Either way the simnode runs on: the console is the instrument, the panel a convenience.
bool ui_begin(const PanelPins* pins);

// Draws the page. A no-op when ui_begin() failed.
void ui_render(const PageLines& page);

}  // namespace simnode
