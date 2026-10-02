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
// It still logs through Serial, as the PoC did. Routing that through GateLink's leveled log
// (GL1) is open in docs/gatelink/HANDOFF.md.
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

    NimBLEClient* client_;
    NimBLERemoteCharacteristic* char_rx_;   // FFF1
    NimBLERemoteCharacteristic* char_tx_;   // FFF2
    NimBLERemoteCharacteristic* char_hs_;   // FFFA
    NotifyHandler* notify_handler_;
};

#endif  // ARDUINO
#endif  // BMS_BLE_NIMBLE_TRANSPORT_H
