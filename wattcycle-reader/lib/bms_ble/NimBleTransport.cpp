#include "NimBleTransport.h"

#ifdef ARDUINO

using bms::GattChar;

NimBleTransport::NimBleTransport()
    : client_(nullptr), char_rx_(nullptr), char_tx_(nullptr),
      char_hs_(nullptr), notify_handler_(nullptr) {}

NimBleTransport::~NimBleTransport() { disconnect(); }

bool NimBleTransport::connect(NimBLEAdvertisedDevice* device) {
    char_rx_ = char_tx_ = char_hs_ = nullptr;

    client_ = NimBLEDevice::createClient();
    if (!client_->connect(device)) {
        Serial.println(F("  connect: FAILED"));
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
        return false;
    }

    NimBLERemoteService* svc = client_->getService(bms::kUuidService);
    if (svc == nullptr) {
        Serial.println(F("  connect: service 0xFFF0 NOT FOUND"));
        disconnect();
        return false;
    }

    char_rx_ = svc->getCharacteristic(bms::kUuidCharRx);
    char_tx_ = svc->getCharacteristic(bms::kUuidCharTx);
    char_hs_ = svc->getCharacteristic(bms::kUuidCharHs);

    if (char_rx_ == nullptr || char_tx_ == nullptr || char_hs_ == nullptr) {
        // The FFF0/1/2 triple alone doesn't prove this is the right device
        // (§4) — a device missing FFFA is not this BMS even if it wears the
        // same service/characteristic UUIDs.
        Serial.println(F("  connect: FFF1/FFF2/FFFA incomplete — not this BMS"));
        disconnect();
        return false;
    }

    return true;
}

void NimBleTransport::logDiscovery() const {
    if (client_ == nullptr || !client_->isConnected()) {
        Serial.println(F("  discovery: not connected"));
        return;
    }

    Serial.printf("  MTU negotiated: %u\n", client_->getMTU());

    struct { const char* label; NimBLERemoteCharacteristic* c; } chars[] = {
        {"FFF1 (rx/notify)", char_rx_},
        {"FFF2 (tx)",        char_tx_},
        {"FFFA (handshake)", char_hs_},
    };
    for (auto& c : chars) {
        if (c.c == nullptr) {
            Serial.printf("  %-18s MISSING\n", c.label);
            continue;
        }
        Serial.printf("  %-18s handle 0x%04x  read=%d write=%d writeNR=%d notify=%d\n",
                      c.label, c.c->getHandle(), c.c->canRead(), c.c->canWrite(),
                      c.c->canWriteNoResponse(), c.c->canNotify());
    }
}

bool NimBleTransport::isConnected() const {
    return client_ != nullptr && client_->isConnected();
}

NimBLERemoteCharacteristic* NimBleTransport::charFor(GattChar ch) const {
    switch (ch) {
        case GattChar::Rx:        return char_rx_;
        case GattChar::Tx:        return char_tx_;
        case GattChar::Handshake: return char_hs_;
    }
    return nullptr;
}

bool NimBleTransport::write(GattChar ch, const uint8_t* data, size_t len,
                             bool with_response) {
    NimBLERemoteCharacteristic* c = charFor(ch);
    if (c == nullptr) return false;
    return c->writeValue(data, len, with_response);
}

int NimBleTransport::read(GattChar ch, uint8_t* out, size_t out_size) {
    NimBLERemoteCharacteristic* c = charFor(ch);
    if (c == nullptr) return -1;

    NimBLEAttValue value = c->readValue();
    if (value.size() > out_size) return -1;
    memcpy(out, value.data(), value.size());
    return (int)value.size();
}

bool NimBleTransport::subscribe(NotifyHandler* handler) {
    if (char_rx_ == nullptr) return false;
    notify_handler_ = handler;
    return char_rx_->subscribe(
        true,
        [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
            if (notify_handler_ != nullptr) notify_handler_->onNotify(data, len);
        });
}

void NimBleTransport::disconnect() {
    if (client_ != nullptr) {
        if (client_->isConnected()) client_->disconnect();
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
    }
    char_rx_ = char_tx_ = char_hs_ = nullptr;
}

int NimBleTransport::rssi() const {
    return client_ != nullptr ? client_->getRssi() : 0;
}

#endif  // ARDUINO
