// BmsDisplay.h — SSD1306 rendering for the Heltec V3's onboard OLED.
//
// Shared state (data/link/staleness) lives in DisplayBase, which TftDisplay
// (StamPLC) also derives from — see that header for the full rationale.
// This class only owns what's genuinely SSD1306/Heltec-specific: the pin
// map, the panel object, and the actual drawing.
//
// Layout (128x64):
//
//   XDZN_001_49A1        *     name + link indicator (filled = live)
//                    -62dBm    RSSI, live while scanning or connected
//     87%                      SOC, largest font
//
//   13.42 V      -4.2 A        pack voltage, current
//   24.1 C       DSG           hottest sensor, FET state
#ifndef BMS_DISPLAY_H
#define BMS_DISPLAY_H

#include <SSD1306Wire.h>

#include "DisplayBase.h"

// Heltec WiFi LoRa 32 V3 pin map (§9) — the part that trips everyone up.
static const int kPinOledSda = 17;
static const int kPinOledScl = 18;
static const int kPinOledRst = 21;
static const int kPinVext = 36;          // ACTIVE LOW: LOW = Vext ON
static const uint8_t kOledAddr = 0x3c;

class BmsDisplay : public DisplayBase {
  public:
    BmsDisplay();

    // Powers Vext, pulses the OLED reset, brings up I2C and shows a splash.
    // Returns false if the panel does not ACK at 0x3C.
    bool begin(const char* title);

    // §9: GateLink holds Vext off during normal operation and powers the panel
    // on demand, so the on/off pair exists here from the start and that
    // behaviour is inherited rather than retrofitted.
    void displayOn();
    void displayOff();

    void render(uint32_t now_ms);

    // M0b: proves the panel before it has to show real data.
    void showMessage(const char* line1, const char* line2);

  private:
    void drawLinkIndicator(bool live);

    SSD1306Wire display_;
};

#endif  // BMS_DISPLAY_H
