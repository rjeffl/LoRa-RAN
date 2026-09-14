// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The OLED, drawn. Task BF-9; see ui.h.

#include "ui.h"

#include <Arduino.h>
#include <SSD1306Wire.h>
#include <Wire.h>

namespace simnode {
namespace {

// Five rows of ArialMT_Plain_10 on a 64 px panel. The face is 13 px from top to descender,
// so a 13 px pitch puts the last row at 52 and its descenders on row 63.
constexpr int16_t kWidth = 128;
constexpr int16_t kPitch = 13;

// Indoor bench contrast, the bridge's value.
constexpr uint8_t kContrast = 96;

// Static storage (root rule 3). The driver only records pins at construction; nothing touches
// I2C until init(), which runs only for a board whose profile names a panel.
constexpr PanelPins kCtorPins = kPanel != nullptr ? *kPanel : kHeltecV3Panel;
SSD1306Wire g_display(kCtorPins.addr, kCtorPins.sda, kCtorPins.scl);
bool        g_ok = false;

}  // namespace

bool ui_begin(const PanelPins* pins) {
  if (pins == nullptr) return false;

  // Vext first. The Heltec powers the panel THROUGH it; skip this and the probe finds
  // nothing, which looks exactly like a dead panel or a wrong address.
  pinMode(pins->vext, OUTPUT);
  digitalWrite(pins->vext, LOW);  // LOW = Vext ON
  delay(100);

  pinMode(pins->rst, OUTPUT);
  digitalWrite(pins->rst, LOW);
  delay(20);
  digitalWrite(pins->rst, HIGH);
  delay(50);

  Wire.begin(pins->sda, pins->scl);
  Wire.beginTransmission(pins->addr);
  if (Wire.endTransmission() != 0) return false;

  g_display.init();
  if (pins->flip_vertically) g_display.flipScreenVertically();
  g_display.setContrast(kContrast);
  g_display.setFont(ArialMT_Plain_10);
  g_ok = true;
  return true;
}

void ui_render(const PageLines& page) {
  if (!g_ok) return;
  g_display.clear();

  for (size_t i = 0; i < kPageRows; ++i) {
    const PageRow& r = page.rows[i];
    const int16_t  y = static_cast<int16_t>(i * kPitch);

    // An armed fault is a filled bar with dark text (oled_page.h says why).
    if (r.invert) {
      g_display.setColor(WHITE);
      g_display.fillRect(0, y, kWidth, kPitch);
      g_display.setColor(BLACK);
    } else {
      g_display.setColor(WHITE);
    }

    g_display.setTextAlignment(TEXT_ALIGN_LEFT);
    g_display.drawString(1, y, r.left);
    g_display.setTextAlignment(TEXT_ALIGN_RIGHT);
    g_display.drawString(kWidth - 1, y, r.right);
  }
  g_display.setColor(WHITE);
  g_display.display();
}

}  // namespace simnode
