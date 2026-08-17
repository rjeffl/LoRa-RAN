#include "DisplayBase.h"

DisplayBase::DisplayBase()
    : link_(LinkState::Idle),
      rssi_(0),
      rssi_valid_(false),
      last_data_ms_(0),
      have_data_(false),
      on_(false) {
    data_.clear();
    name_[0] = '\0';
}

void DisplayBase::setDeviceName(const char* name) {
    if (name == nullptr) {
        name_[0] = '\0';
        return;
    }
    strncpy(name_, name, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
}

void DisplayBase::setLink(LinkState state, int rssi_dBm, bool rssi_valid) {
    link_ = state;
    rssi_ = rssi_dBm;
    rssi_valid_ = rssi_valid;
}

void DisplayBase::setData(const bms::BmsData& data, uint32_t now_ms) {
    data_ = data;
    last_data_ms_ = now_ms;
    have_data_ = data.valid;
}

bool DisplayBase::dataFresh(uint32_t now_ms) const {
    if (!have_data_) return false;
    return (uint32_t)(now_ms - last_data_ms_) < kStaleAfterMs;
}
