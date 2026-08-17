// TftDisplay.h — M7a: StamPLC's onboard ST7789v2 TFT, via the official
// M5StamPLC library (M5GFX/M5Unified underneath).
//
// Shared state (data/link/staleness) lives in DisplayBase, which BmsDisplay
// (Heltec OLED) also derives from — see that header for the full rationale.
// This class only owns what's genuinely StamPLC/ST7789-specific: the
// back-buffer sprite and the actual drawing. main.cpp swaps a type alias
// between the two (§9's rule holds across boards too: decode produces a
// struct, presentation layers consume it, and now there are two
// presentation layers reading the same BmsData/LinkState).
//
// M5StamPLC.begin() also brings up the board's onboard LM75B/INA226/RX8130
// sensors and I2C IO expanders (that's what the library does — there's no
// display-only init path). It leaves Modbus, CAN and SD card disabled by
// default (Config_t), so nothing here drives the RS485/CAN transceivers or
// actuates a PLC relay — only ever the display and its IO-expander-gated
// backlight.
#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#include <M5StamPLC.h>

#include "DisplayBase.h"

class TftDisplay : public DisplayBase {
  public:
    TftDisplay();
    ~TftDisplay();

    // Brings up the whole StamPLC board (display + onboard sensors/IO
    // expanders, per the M5StamPLC library) and allocates the full-screen
    // back-buffer sprite (see canvas_ below). Unlike the OLED, the panel
    // itself is integrated on the same PCB as everything else on this board
    // and can't fail an I2C probe — but the back-buffer allocation can still
    // fail (heap, not hardware), so this returns false in that case.
    bool begin(const char* title);

    void displayOn();
    void displayOff();

    void render(uint32_t now_ms);

    // M7a: proves the panel before it has to show real data, same role as
    // BmsDisplay::showMessage at M0b.
    void showMessage(const char* line1, const char* line2);

  private:
    void drawLinkIndicator(bool live);

    // Full-screen off-screen sprite. Every draw call in render()/
    // showMessage() targets this, invisibly, and a single pushSprite(0, 0)
    // at the end blits the finished frame to the panel in one SPI burst.
    // Confirmed on hardware: drawing straight to M5StamPLC.Display (the
    // panel itself) with a fillScreen() at the top of every render()
    // produced a visible flicker on every refresh — the panel briefly went
    // black before each element redrew. Same pattern the library's own
    // DashboardUI example uses. ~63 KB for 240x135 RGB565; comes out of
    // heap since this board has no PSRAM (ESP32-S3FN8).
    LGFX_Sprite* canvas_;
};

#endif  // TFT_DISPLAY_H
