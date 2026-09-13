// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The OLED, drawn. Task BF-14; R-4.1c, Impl Plan 5.1.1.

#include "ui.h"

#include <Arduino.h>
#include <SSD1306Wire.h>
#include <Wire.h>

namespace bridge {
namespace {

// Row positions for the ThingPulse ArialMT faces on a 64 px panel: a 10 px header, a
// 16 px headline, and two 10 px rows beneath. The same grid the range test settled on,
// less its 24 px row - nothing on this page is read at arm's length in sunlight.
constexpr int16_t kWidth   = 128;
constexpr int16_t kRowTop  = 0;
constexpr int16_t kRowBig  = 14;
constexpr int16_t kRowMid  = 36;
constexpr int16_t kRowFoot = 51;

// Indoor contrast. The range test ran at 255 because its panel was read outdoors;
// this one sits in a house and is on for years, and a lower drive current is the
// other half of the burn-in answer alongside the pixel shift (status_page.h).
constexpr uint8_t kContrast = 96;

// Static storage (root rule 3). Constructed with the Heltec's pins; ui_begin() is
// handed the same config and uses it for the power and reset sequence.
SSD1306Wire g_display(kHeltecV3Ui.addr, kHeltecV3Ui.sda, kHeltecV3Ui.scl);
bool        g_ok = false;

}  // namespace

bool ui_begin(const BoardUiConfig& cfg) {
  // Vext first. On the Heltec the panel is powered THROUGH it; skip this and the I2C
  // probe finds nothing, which looks exactly like a dead panel or a wrong address.
  pinMode(cfg.vext, OUTPUT);
  digitalWrite(cfg.vext, LOW);  // LOW = Vext ON
  vTaskDelay(pdMS_TO_TICKS(100));

  pinMode(cfg.rst, OUTPUT);
  digitalWrite(cfg.rst, LOW);
  vTaskDelay(pdMS_TO_TICKS(20));
  digitalWrite(cfg.rst, HIGH);
  vTaskDelay(pdMS_TO_TICKS(50));

  Wire.begin(cfg.sda, cfg.scl);

  // Probe before init, so a missing panel is reported rather than guessed at.
  Wire.beginTransmission(cfg.addr);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  g_display.init();
  if (cfg.flip_vertically) {
    g_display.flipScreenVertically();
  }
  g_display.setContrast(kContrast);
  g_ok = true;
  return true;
}

void ui_render(const StatusLines& l, int16_t x) {
  if (!g_ok) {
    return;
  }
  g_display.clear();
  g_display.setColor(WHITE);

  g_display.setFont(ArialMT_Plain_10);
  g_display.setTextAlignment(TEXT_ALIGN_LEFT);
  g_display.drawString(x, kRowTop, l.top);
  g_display.setTextAlignment(TEXT_ALIGN_RIGHT);
  // The right-aligned element shifts LEFT as the rest shifts right, so the whole page
  // stays inside 128 px at every step of the cycle.
  g_display.drawString(kWidth - x, kRowTop, l.top_right);

  g_display.setTextAlignment(TEXT_ALIGN_LEFT);
  g_display.setFont(ArialMT_Plain_16);
  g_display.drawString(x, kRowBig, l.big);

  g_display.setFont(ArialMT_Plain_10);
  g_display.drawString(x, kRowMid, l.mid);
  g_display.drawString(x, kRowFoot, l.foot);
  g_display.display();
}

}  // namespace bridge
