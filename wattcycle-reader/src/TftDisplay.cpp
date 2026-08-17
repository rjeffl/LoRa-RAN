#include "TftDisplay.h"

namespace {

// Text sizes are integer scale factors on M5GFX's default 6x8 font (12x16 at
// size 2, 18x24 at size 3, ...) — same reasoning as picking fixed OLED fonts
// in BmsDisplay: predictable width in pixels, no font-file dependency.
const float kSizeSmall = 1;   // 6x8   px/char
const float kSizeMed   = 2;   // 12x16 px/char
const float kSizeBig   = 3;   // 18x24 px/char

const int32_t kIndicatorR = 4;

}  // namespace

TftDisplay::TftDisplay() : canvas_(nullptr) {}

TftDisplay::~TftDisplay() {
    delete canvas_;   // LGFX_Sprite::~LGFX_Sprite() frees the pixel buffer
}

bool TftDisplay::begin(const char* title) {
    // Brings up the whole board per the M5StamPLC library's own design —
    // display, LM75B/INA226/RX8130 sensors, IO expanders — not just the
    // panel. Config_t defaults (Modbus/CAN/SD all disabled) mean nothing
    // here touches the RS485/CAN transceivers or a relay.
    M5StamPLC.begin();
    M5StamPLC.setBacklight(true);

    Serial.printf("TFT: %dx%d rotation=%d\n", M5StamPLC.Display.width(),
                  M5StamPLC.Display.height(), M5StamPLC.Display.getRotation());

    // Full-screen back-buffer (see canvas_ in TftDisplay.h) — every
    // subsequent draw call targets this, not the panel directly. ~63 KB on
    // this board's heap (no PSRAM); if it's fragmented or short right after
    // M5StamPLC.begin()'s sensor/IO-expander bring-up, createSprite() can
    // fail and return null. This *is* this board's "not found" case — the
    // panel itself is always present, but the back-buffer it needs isn't
    // guaranteed.
    canvas_ = new LGFX_Sprite(&M5StamPLC.Display);
    if (canvas_->createSprite(M5StamPLC.Display.width(), M5StamPLC.Display.height()) == nullptr) {
        Serial.println(F("TFT: createSprite FAILED (out of heap?)"));
        return false;
    }

    on_ = true;
    showMessage(title, "starting...");
    return true;
}

void TftDisplay::displayOn() {
    M5StamPLC.setBacklight(true);
    on_ = true;
}

void TftDisplay::displayOff() {
    M5StamPLC.setBacklight(false);
    on_ = false;
}

void TftDisplay::drawLinkIndicator(bool live) {
    auto& d = *canvas_;
    const int32_t x = d.width() - kIndicatorR - 2;
    const int32_t y = kIndicatorR + 2;
    if (live) {
        d.fillCircle(x, y, kIndicatorR, TFT_GREEN);
    } else {
        d.drawCircle(x, y, kIndicatorR, TFT_WHITE);
    }
}

void TftDisplay::showMessage(const char* line1, const char* line2) {
    if (!on_) return;
    auto& d = *canvas_;
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setTextDatum(top_left);
    d.setTextSize(kSizeSmall);
    d.drawString(line1, 2, 2);
    d.setTextSize(kSizeMed);
    d.drawString(line2, 2, 20);
    d.pushSprite(0, 0);
}

void TftDisplay::render(uint32_t now_ms) {
    if (!on_) return;

    auto& d = *canvas_;
    const bool fresh = dataFresh(now_ms);
    char buf[24];

    // Row Y positions as fractions of the panel's actual height, not fixed
    // pixels. First cut hardcoded pixel rows sized for a portrait 135x240
    // panel and clipped badly off-screen — confirmed on hardware this panel
    // is landscape, 240x135 (M5GFX autodetected rotation=1). Computing off
    // height() rather than assuming a number is what should have happened
    // from the start.
    const int32_t h = d.height();
    const int32_t row_name = 2;
    const int32_t row_rssi = h * 12 / 100;
    const int32_t row_soc  = h * 22 / 100;
    const int32_t row_mid  = h * 55 / 100;   // pack voltage (left) / current (right)
    const int32_t row_bot  = h * 85 / 100;   // temp (left) / FET state (right)

    d.fillScreen(TFT_BLACK);

    // --- name + link indicator ----------------------------------------------
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setTextDatum(top_left);
    d.setTextSize(kSizeSmall);
    d.drawString(name_[0] ? name_ : "(no target)", 2, row_name);
    drawLinkIndicator(link_ == LinkState::Connected);

    // --- RSSI -----------------------------------------------------------------
    // Shown whenever the target has been heard, including while only
    // scanning — same aiming-instrument rationale as BmsDisplay (M1).
    d.setTextDatum(top_right);
    if (rssi_valid_) {
        snprintf(buf, sizeof(buf), "%ddBm", rssi_);
    } else {
        snprintf(buf, sizeof(buf), "--dBm");
    }
    d.drawString(buf, d.width() - 2, row_rssi);

    // --- SOC --------------------------------------------------------------
    d.setTextDatum(top_center);
    d.setTextSize(kSizeBig);
    if (fresh) {
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)data_.soc_pct);
    } else {
        snprintf(buf, sizeof(buf), "--%%");
    }
    d.drawString(buf, d.width() / 2, row_soc);

    // --- pack voltage / current, current colour-coded charge/discharge/idle -
    // FET state is DERIVED FROM CURRENT SIGN, not read from the BMS — same
    // caveat as BmsDisplay (M0b): real MOSFET status lives in 0x8D, not
    // decoded until M6.
    d.setTextSize(kSizeMed);
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setTextDatum(top_left);
    if (fresh) {
        snprintf(buf, sizeof(buf), "%.2fV", data_.pack_mV / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "--.--V");
    }
    d.drawString(buf, 2, row_mid);

    uint16_t current_color = TFT_WHITE;
    const char* fet = "---";
    if (fresh) {
        if (data_.current_mA < 0) {
            current_color = TFT_RED;
            fet = "DSG";
        } else if (data_.current_mA > 0) {
            current_color = TFT_GREEN;
            fet = "CHG";
        } else {
            fet = "IDLE";
        }
    }
    d.setTextColor(current_color, TFT_BLACK);
    d.setTextDatum(top_right);
    if (fresh) {
        snprintf(buf, sizeof(buf), "%.1fA", data_.current_mA / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "--.-A");
    }
    d.drawString(buf, d.width() - 2, row_mid);

    // --- temperature / FET state ---------------------------------------------
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.setTextSize(kSizeSmall);
    d.setTextDatum(top_left);
    if (fresh && data_.temp_count > 0) {
        snprintf(buf, sizeof(buf), "%.1fC", data_.maxTemp_dC() / 10.0);
    } else {
        snprintf(buf, sizeof(buf), "--.-C");
    }
    d.drawString(buf, 2, row_bot);

    d.setTextColor(current_color, TFT_BLACK);
    d.setTextDatum(top_right);
    d.drawString(fet, d.width() - 2, row_bot);

    // Blit the finished frame to the panel in one SPI burst — see canvas_'s
    // comment in TftDisplay.h. Everything above this line drew into RAM, not
    // the panel, so the fillScreen() at the top of this function never
    // produced a visible flash.
    d.pushSprite(0, 0);
}
