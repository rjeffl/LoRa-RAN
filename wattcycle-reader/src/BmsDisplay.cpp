#include "BmsDisplay.h"

#include <Wire.h>

namespace {

// Row origins, chosen so the whole layout closes out at exactly 64 px.
// ArialMT_Plain_10 advances 13 px, _24 advances 28.
//
// SOC and RSSI deliberately share vertical space: SOC is large and left, RSSI
// is small and right-aligned, so they never collide horizontally. That is what
// makes room for the RSSI line without shrinking the SOC font.
const int16_t kRowName = 0;    // 0..12
const int16_t kRowRssi = 12;   // 12..24, right half only
const int16_t kRowSoc = 10;    // 10..37, left half only
const int16_t kRowPack = 38;   // 38..50
const int16_t kRowTemp = 51;   // 51..63

const int16_t kWidth = 128;
const int16_t kIndicatorX = 121;
const int16_t kIndicatorY = 5;
const int16_t kIndicatorR = 3;

// Plain "C", no degree sign. 0xB0 is the degree glyph in ISO-8859-1 and the
// ThingPulse fonts nominally cover that range, but it renders as nothing on
// this panel — tried and rejected on hardware. A missing glyph reads worse
// than a plain label. If it's ever wanted, draw it: a 1 px circle via
// drawCircle() next to the string is font-independent.
const char* kDegC = "C";

}  // namespace

BmsDisplay::BmsDisplay()
    : display_(kOledAddr, kPinOledSda, kPinOledScl),
      link_(LinkState::Idle),
      rssi_(0),
      rssi_valid_(false),
      last_data_ms_(0),
      have_data_(false),
      on_(false) {
    data_.clear();
    name_[0] = '\0';
}

bool BmsDisplay::begin(const char* title) {
    // Bring-up order matters (§9). The OLED is powered through Vext, not
    // directly — skip this and the panel stays dark and the I2C scan finds
    // nothing, which looks exactly like a dead panel or a wrong address.
    pinMode(kPinVext, OUTPUT);
    digitalWrite(kPinVext, LOW);      // LOW = Vext ON
    delay(100);

    pinMode(kPinOledRst, OUTPUT);
    digitalWrite(kPinOledRst, LOW);   // pulse reset
    delay(20);
    digitalWrite(kPinOledRst, HIGH);
    delay(50);

    Wire.begin(kPinOledSda, kPinOledScl);

    // Probe before init so a missing panel is reported rather than guessed at.
    Wire.beginTransmission(kOledAddr);
    if (Wire.endTransmission() != 0) return false;

    display_.init();
    display_.flipScreenVertically();   // required on the V3 — verified on hardware
    display_.setContrast(255);
    on_ = true;

    showMessage(title, "starting...");
    return true;
}

void BmsDisplay::displayOn() {
    digitalWrite(kPinVext, LOW);
    display_.displayOn();
    on_ = true;
}

void BmsDisplay::displayOff() {
    display_.displayOff();
    on_ = false;
}

void BmsDisplay::setDeviceName(const char* name) {
    if (name == nullptr) {
        name_[0] = '\0';
        return;
    }
    strncpy(name_, name, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
}

void BmsDisplay::setLink(LinkState state, int rssi_dBm, bool rssi_valid) {
    link_ = state;
    rssi_ = rssi_dBm;
    rssi_valid_ = rssi_valid;
}

void BmsDisplay::setData(const bms::BmsData& data, uint32_t now_ms) {
    data_ = data;
    last_data_ms_ = now_ms;
    have_data_ = data.valid;
}

bool BmsDisplay::dataFresh(uint32_t now_ms) const {
    if (!have_data_) return false;
    return (uint32_t)(now_ms - last_data_ms_) < kStaleAfterMs;
}

void BmsDisplay::drawLinkIndicator(bool live) {
    // Filled when connected and subscribed, hollow otherwise. Without this,
    // stale numbers on a dropped link look identical to live ones — the
    // misreading we specifically don't want inherited into GateLink (§9).
    if (live) {
        display_.fillCircle(kIndicatorX, kIndicatorY, kIndicatorR);
    } else {
        display_.drawCircle(kIndicatorX, kIndicatorY, kIndicatorR);
    }
}

void BmsDisplay::showMessage(const char* line1, const char* line2) {
    if (!on_) return;
    display_.clear();
    display_.setTextAlignment(TEXT_ALIGN_LEFT);
    display_.setFont(ArialMT_Plain_10);
    display_.drawString(0, 0, line1);
    display_.setFont(ArialMT_Plain_16);
    display_.drawString(0, 20, line2);
    display_.display();
}

void BmsDisplay::render(uint32_t now_ms) {
    if (!on_) return;

    const bool fresh = dataFresh(now_ms);
    char buf[24];

    display_.clear();

    // --- name + link indicator ---------------------------------------------
    display_.setTextAlignment(TEXT_ALIGN_LEFT);
    display_.setFont(ArialMT_Plain_10);
    display_.drawString(0, kRowName, name_[0] ? name_ : "(no target)");
    drawLinkIndicator(link_ == LinkState::Connected);

    // --- RSSI ---------------------------------------------------------------
    // Shown whenever the target has been heard, including while only scanning:
    // that is what makes the panel useful for aiming the board at a mounting
    // position without a laptop attached.
    display_.setTextAlignment(TEXT_ALIGN_RIGHT);
    if (rssi_valid_) {
        snprintf(buf, sizeof(buf), "%ddBm", rssi_);
    } else {
        snprintf(buf, sizeof(buf), "--dBm");
    }
    display_.drawString(kWidth, kRowRssi, buf);

    // --- SOC ----------------------------------------------------------------
    display_.setTextAlignment(TEXT_ALIGN_LEFT);
    display_.setFont(ArialMT_Plain_24);
    if (fresh) {
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)data_.soc_pct);
    } else {
        snprintf(buf, sizeof(buf), "--%%");
    }
    display_.drawString(2, kRowSoc, buf);

    // --- pack voltage / current --------------------------------------------
    display_.setFont(ArialMT_Plain_10);
    display_.setTextAlignment(TEXT_ALIGN_LEFT);
    if (fresh) {
        snprintf(buf, sizeof(buf), "%.2f V", data_.pack_mV / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "--.-- V");
    }
    display_.drawString(0, kRowPack, buf);

    display_.setTextAlignment(TEXT_ALIGN_RIGHT);
    if (fresh) {
        snprintf(buf, sizeof(buf), "%.1f A", data_.current_mA / 1000.0);
    } else {
        snprintf(buf, sizeof(buf), "--.- A");
    }
    display_.drawString(kWidth, kRowPack, buf);

    // --- temperature / FET state -------------------------------------------
    display_.setTextAlignment(TEXT_ALIGN_LEFT);
    if (fresh && data_.temp_count > 0) {
        snprintf(buf, sizeof(buf), "%.1f%s", data_.maxTemp_dC() / 10.0, kDegC);
    } else {
        snprintf(buf, sizeof(buf), "--.-%s", kDegC);
    }
    display_.drawString(0, kRowTemp, buf);

    // FET state is DERIVED FROM CURRENT SIGN, not read from the BMS. Real
    // MOSFET status lives in 0x8D, which is not decoded yet (M6). Once it is,
    // this should show the actual FET bits — a pack can be idle with its
    // discharge FET open, and these two are not the same statement.
    display_.setTextAlignment(TEXT_ALIGN_RIGHT);
    const char* fet = "---";
    if (fresh) {
        if (data_.current_mA < 0) {
            fet = "DSG";
        } else if (data_.current_mA > 0) {
            fet = "CHG";
        } else {
            fet = "IDLE";
        }
    }
    display_.drawString(kWidth, kRowTemp, fet);

    display_.display();
}
