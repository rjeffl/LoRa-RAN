// DisplayBase.h — state and setters shared by every display implementation
// (BmsDisplay: Heltec V3 SSD1306 OLED; TftDisplay: StamPLC ST7789 TFT).
//
// Both boards' "done when" criteria are the same shape (M0b/M6b, M7a):
// SOC/voltage/current/temp with a link indicator and stale-data handling.
// What to show and how stale it is doesn't depend on the panel underneath,
// so that state lives once here; only the actual pixel-pushing (begin(),
// displayOn()/Off(), render(), showMessage(), the link-indicator shape) is
// hardware-specific and stays in each subclass.
//
// Knows nothing about BLE or the TDT protocol (§9): decode produces a
// struct, presentation layers consume it. Same rule as the serial printer,
// so both can change independently — and now that rule holds across boards
// too, since two presentation layers read the same BmsData/LinkState.
#ifndef DISPLAY_BASE_H
#define DISPLAY_BASE_H

#include <Arduino.h>

#include "BmsData.h"
#include "LinkState.h"

class DisplayBase {
  public:
    void setDeviceName(const char* name);
    void setLink(LinkState state, int rssi_dBm, bool rssi_valid);

    // Records the frame and stamps it, which drives the staleness rule.
    void setData(const bms::BmsData& data, uint32_t now_ms);

    bool isOn() const { return on_; }

  protected:
    DisplayBase();

    bool dataFresh(uint32_t now_ms) const;

    bms::BmsData data_;
    char name_[24];
    LinkState link_;
    int rssi_;
    bool rssi_valid_;
    uint32_t last_data_ms_;
    bool have_data_;
    bool on_;
};

#endif  // DISPLAY_BASE_H
