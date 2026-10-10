// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The only code in lib/bms-ble/ allowed to include NimBLE headers. Implements
// bms::BmsTransport against service 0xFFF0 (bms-protocol §2), for post-connection I/O only;
// bms_transport.h's scope note says why scanning is not here.
//
// connect() and log_discovery() confirm the GATT layout. write(), read() and subscribe()
// carry the handshake, the poll requests and the notifications. wattcycle-reader ran all of
// it on both the Heltec V3 and the StamPLC.
//
// It logs through Serial, as the PoC did, unless the client passes a log function.
// GateLink passes one, because only its log_task may write to Serial. log_discovery() always
// prints to Serial, and is a bench diagnostic for wattcycle-reader.
#ifndef BMS_BLE_NIMBLE_TRANSPORT_H
#define BMS_BLE_NIMBLE_TRANSPORT_H

// The native environment compiles this library's src/ too, but has no NimBLE headers to
// offer. Compile out there; every real build defines ARDUINO.
#ifdef ARDUINO

#include <NimBLEDevice.h>

#include "bms_ble/bms_transport.h"

class NimBleTransport : public bms::BmsTransport {
  public:
    NimBleTransport();
    ~NimBleTransport() override;

    // Connect to `device` and resolve all three FFF0 characteristics (bms-protocol §2).
    // Returns false if the connect fails, service 0xFFF0 is missing, or any
    // of FFF1/FFF2/FFFA is missing — a device that answers to the target
    // name but lacks the full GATT layout is not this BMS (bms-protocol §2 note: the
    // FFF0/1/2 triple alone is not proof of protocol).
    bool connect(NimBLEAdvertisedDevice* device);

    // As above, by address, giving up after `timeout_s` seconds. NimBLE's own default is
    // 30 s, which would hold GateLink's LoRa/BLE interlock far past its cap (Impl Plan 5.2).
    // The address carries its type, as a scan reports it.
    bool connect(const NimBLEAddress& address, uint8_t timeout_s);

    // Where the failure lines go. Null, the default, prints to Serial.
    using LogFn = void (*)(const char* line);
    void set_log(LogFn fn) { log_ = fn; }

    // Serial-logs handle + properties for FFF1/FFF2/FFFA. A bench
    // diagnostic: it shows the negotiated MTU and that all three characteristics resolved.
    void log_discovery() const;

    bool is_connected() const override;
    bool write(bms::GattChar ch, const uint8_t* data, size_t len,
               bool with_response) override;
    int read(bms::GattChar ch, uint8_t* out, size_t out_size) override;
    bool subscribe(NotifyHandler* handler) override;
    void disconnect() override;
    int rssi() const override;

  private:
    NimBLERemoteCharacteristic* char_for(bms::GattChar ch) const;
    bool resolve();
    void log(const char* line) const;

    NimBLEClient* client_;
    NimBLERemoteCharacteristic* char_rx_;   // FFF1
    NimBLERemoteCharacteristic* char_tx_;   // FFF2
    NimBLERemoteCharacteristic* char_hs_;   // FFFA
    NotifyHandler* notify_handler_;
    LogFn log_ = nullptr;
};

#endif  // ARDUINO
#endif  // BMS_BLE_NIMBLE_TRANSPORT_H
