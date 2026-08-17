// TftDisplay.h — M7a: StamPLC's onboard ST7789v2 TFT, via the official
// M5StamPLC library (M5GFX/M5Unified underneath).
//
// Same public API as BmsDisplay (M0b, Heltec OLED) — begin/setDeviceName/
// setLink/setData/render/showMessage — so main.cpp only swaps a type alias
// between the two, not any call sites (§9's rule holds across boards too:
// decode produces a struct, presentation layers consume it, and now there
// are two presentation layers reading the same BmsData/LinkState).
//
// M5StamPLC.begin() also brings up the board's onboard LM75B/INA226/RX8130
// sensors and I2C IO expanders (that's what the library does — there's no
// display-only init path). It leaves Modbus, CAN and SD card disabled by
// default (Config_t), so nothing here drives the RS485/CAN transceivers or
// actuates a PLC relay — only ever the display and its IO-expander-gated
// backlight.
#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#include <Arduino.h>
#include <M5StamPLC.h>

#include "BmsData.h"
#include "LinkState.h"

class TftDisplay {
  public:
    TftDisplay();
    ~TftDisplay();

    // Brings up the whole StamPLC board (display + onboard sensors/IO
    // expanders, per the M5StamPLC library), allocates the full-screen
    // back-buffer sprite (see canvas_ below), and shows a splash. Unlike the
    // OLED, this panel is integrated on the same PCB as everything else on
    // this board — there's no "not found" case to report, so this always
    // returns true.
    bool begin(const char* title);

    void displayOn();
    void displayOff();
    bool isOn() const { return on_; }

    void setDeviceName(const char* name);
    void setLink(LinkState state, int rssi_dBm, bool rssi_valid);

    // Records the frame and stamps it, which drives the staleness rule.
    void setData(const bms::BmsData& data, uint32_t now_ms);

    void render(uint32_t now_ms);

    // M7a: proves the panel before it has to show real data, same role as
    // BmsDisplay::showMessage at M0b.
    void showMessage(const char* line1, const char* line2);

  private:
    bool dataFresh(uint32_t now_ms) const;
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

    bms::BmsData data_;
    char name_[24];
    LinkState link_;
    int rssi_;
    bool rssi_valid_;
    uint32_t last_data_ms_;
    bool have_data_;
    bool on_;
};

#endif  // TFT_DISPLAY_H
