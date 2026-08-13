// BmsDisplay.h — SSD1306 rendering for the Heltec V3's onboard OLED.
//
// Takes a BmsData and a link state. Knows nothing about BLE or the TDT
// protocol (§9): decode produces a struct, presentation layers consume it.
// Same rule as the serial printer, so both can change independently.
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

#include <Arduino.h>
#include <SSD1306Wire.h>

#include "BmsData.h"

// Heltec WiFi LoRa 32 V3 pin map (§9) — the part that trips everyone up.
static const int kPinOledSda = 17;
static const int kPinOledScl = 18;
static const int kPinOledRst = 21;
static const int kPinVext = 36;          // ACTIVE LOW: LOW = Vext ON
static const uint8_t kOledAddr = 0x3c;

// Values older than this are shown as dashes rather than stale numbers. A
// dropped link must never look like a live reading (§9).
static const uint32_t kStaleAfterMs = 15000;

enum class LinkState {
    Idle,
    Scanning,      // hollow indicator
    Connecting,
    Connected      // filled indicator
};

class BmsDisplay {
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
    bool isOn() const { return on_; }

    void setDeviceName(const char* name);
    void setLink(LinkState state, int rssi_dBm, bool rssi_valid);

    // Records the frame and stamps it, which drives the staleness rule.
    void setData(const bms::BmsData& data, uint32_t now_ms);

    void render(uint32_t now_ms);

    // M0b: proves the panel before it has to show real data.
    void showMessage(const char* line1, const char* line2);

  private:
    bool dataFresh(uint32_t now_ms) const;
    void drawLinkIndicator(bool live);

    SSD1306Wire display_;
    bms::BmsData data_;
    char name_[24];
    LinkState link_;
    int rssi_;
    bool rssi_valid_;
    uint32_t last_data_ms_;
    bool have_data_;
    bool on_;
};

#endif  // BMS_DISPLAY_H
