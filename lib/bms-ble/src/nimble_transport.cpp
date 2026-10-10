// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "bms_ble/nimble_transport.h"

#ifdef ARDUINO

using bms::GattChar;

namespace {

// Refuses the peer's connection-parameter update. The pack asks for one just after the FFF1
// subscription. A connection torn down while that procedure is pending, and the controller
// de-initialized straight after, panicked GateLink's ESP32-S3 in the FreeRTOS timer task
// (engineering log, 2026-10-10). The connection lasts a few seconds, so the pack's
// parameters buy nothing.
class RefuseParamUpdate : public NimBLEClientCallbacks {
  public:
    bool onConnParamsUpdateRequest(NimBLEClient*, const ble_gap_upd_params*) override {
        return false;
    }
};
RefuseParamUpdate g_refuse_param_update;

}  // namespace

NimBleTransport::NimBleTransport()
    : client_(nullptr), char_rx_(nullptr), char_tx_(nullptr),
      char_hs_(nullptr), notify_handler_(nullptr) {}

NimBleTransport::~NimBleTransport() { disconnect(); }

bool NimBleTransport::connect(NimBLEAdvertisedDevice* device) {
    char_rx_ = char_tx_ = char_hs_ = nullptr;

    client_ = NimBLEDevice::createClient();
    if (!client_->connect(device)) {
        log("  connect: FAILED");
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
        return false;
    }
    return resolve();
}

bool NimBleTransport::connect(const NimBLEAddress& address, uint8_t timeout_s) {
    char_rx_ = char_tx_ = char_hs_ = nullptr;

    client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(&g_refuse_param_update, false);
    // Open at the parameters the pack asks for after the subscription: a 15 ms interval
    // (12 x 1.25 ms), no latency, a 4 s supervision timeout (400 x 10 ms). Every GATT step
    // then runs at that interval without the update this client refuses.
    client_->setConnectionParams(12, 12, 0, 400);
    client_->setConnectTimeout(timeout_s);
    if (!client_->connect(address)) {
        log("  connect: FAILED");
        NimBLEDevice::deleteClient(client_);
        client_ = nullptr;
        return false;
    }
    return resolve();
}

bool NimBleTransport::resolve() {
    NimBLERemoteService* svc = client_->getService(bms::kUuidService);
    if (svc == nullptr) {
        log("  connect: service 0xFFF0 NOT FOUND");
        disconnect();
        return false;
    }

    // One discovery pass for every characteristic. Asked for one by one, NimBLE discovers
    // each separately, which took 1.4 s of a 1.9 s connect on GateLink's bench
    // (engineering log, 2026-10-10).
    svc->getCharacteristics(true);
    char_rx_ = svc->getCharacteristic(bms::kUuidCharRx);
    char_tx_ = svc->getCharacteristic(bms::kUuidCharTx);
    char_hs_ = svc->getCharacteristic(bms::kUuidCharHs);

    if (char_rx_ == nullptr || char_tx_ == nullptr || char_hs_ == nullptr) {
        // The FFF0/1/2 triple alone doesn't prove this is the right device
        // (bms-protocol §2) — a device missing FFFA is not this BMS even if it wears the
        // same service/characteristic UUIDs.
        log("  connect: FFF1/FFF2/FFFA incomplete — not this BMS");
        disconnect();
        return false;
    }

    return true;
}

void NimBleTransport::log(const char* line) const {
    if (log_ != nullptr) {
        log_(line);
    } else {
        Serial.println(line);
    }
}

void NimBleTransport::log_discovery() const {
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

bool NimBleTransport::is_connected() const {
    return client_ != nullptr && client_->isConnected();
}

NimBLERemoteCharacteristic* NimBleTransport::char_for(GattChar ch) const {
    switch (ch) {
        case GattChar::Rx:        return char_rx_;
        case GattChar::Tx:        return char_tx_;
        case GattChar::Handshake: return char_hs_;
    }
    return nullptr;
}

bool NimBleTransport::write(GattChar ch, const uint8_t* data, size_t len,
                             bool with_response) {
    NimBLERemoteCharacteristic* c = char_for(ch);
    if (c == nullptr) return false;
    return c->writeValue(data, len, with_response);
}

int NimBleTransport::read(GattChar ch, uint8_t* out, size_t out_size) {
    NimBLERemoteCharacteristic* c = char_for(ch);
    if (c == nullptr) return -1;

    NimBLEAttValue value = c->readValue();
    // BmsTransport.h documents "-1 on error"; NimBLE's readValue() has no
    // separate error signal, it just returns empty on failure. None of this
    // transport's characteristics (bms-protocol §3's FFFA ack, bms-protocol §5's command replies)
    // ever legitimately reads back zero bytes, so treat empty as failure
    // too rather than returning 0 and leaving a real failure
    // indistinguishable from "read succeeded with no data."
    if (value.size() == 0 || value.size() > out_size) return -1;
    memcpy(out, value.data(), value.size());
    return (int)value.size();
}

bool NimBleTransport::subscribe(NotifyHandler* handler) {
    if (char_rx_ == nullptr) return false;
    notify_handler_ = handler;
    return char_rx_->subscribe(
        true,
        [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
            if (notify_handler_ != nullptr) notify_handler_->on_notify(data, len);
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
