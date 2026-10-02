// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The board layer over M5StamPLC. Task L6; GateLink Impl Plan 5.3.
//
// Read the INSTALLED M5StamPLC headers under .pio/libdeps/, not GitHub: the published
// package has drifted from its main branch (wattcycle-reader's README).

#include "board_stamplc.h"

#include <M5StamPLC.h>

namespace gatelink {

void board_begin() {
  // Relays are behind IO expander A, which begin() initializes. Whether every output stays
  // off through that initialization, a watchdog reset and a brownout is GL1's scope check
  // (PRD R-3.5j). A bare StamPLC switches nothing.
  M5StamPLC.begin();
  M5StamPLC.setBacklight(true);
}

void board_show(const PageText& page) {
  auto& d = M5StamPLC.Display;
  d.fillScreen(TFT_BLACK);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setTextDatum(top_left);
  d.setTextSize(2);  // 12x16 pixels a character; ui_pages.h sizes the lines to it
  for (size_t i = 0; i < page.count; ++i) {
    d.drawString(page.line[i], 0, static_cast<int32_t>(i * 16));
  }
}

}  // namespace gatelink
