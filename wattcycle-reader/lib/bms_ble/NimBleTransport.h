// NimBleTransport.h — the only file allowed to include NimBLE headers
// (§7 rule 2). Implements bms::BmsTransport against service 0xFFF0 (§4).
//
// M2 (this milestone) exercises connect() and logDiscovery() only: connect,
// resolve FFF1/FFF2/FFFA, print their handles, disconnect. write()/read()/
// subscribe() are implemented here because BmsTransport is a pure interface
// and this class must be concrete to compile, but they go untested until M3
// (handshake) actually calls them.
#ifndef WATTCYCLE_NIMBLE_TRANSPORT_H
#define WATTCYCLE_NIMBLE_TRANSPORT_H

// The native test env (§7 rule 1) discovers this whole lib/bms_ble/ folder
// too, but has no NimBLE headers to offer. Compile out on that env; ARDUINO
// is defined by every real build.
#ifdef ARDUINO

#include <NimBLEDevice.h>

#include "BmsTransport.h"

class NimBleTransport : public bms::BmsTransport {
  public:
    NimBleTransport();
    ~NimBleTransport() override;

    // Connect to `device` and resolve all three FFF0 characteristics (§4).
    // Returns false if the connect fails, service 0xFFF0 is missing, or any
    // of FFF1/FFF2/FFFA is missing — a device that answers to the target
    // name but lacks the full GATT layout is not this BMS (§4 note: the
    // FFF0/1/2 triple alone is not proof of protocol).
    bool connect(NimBLEAdvertisedDevice* device);

    // Serial-logs handle + properties for FFF1/FFF2/FFFA. This is M2's
    // "done when": connects, enumerates FFF0, confirms FFF1/FFF2 handles.
    void logDiscovery() const;

    bool isConnected() const override;
    bool write(bms::GattChar ch, const uint8_t* data, size_t len,
               bool with_response) override;
    int read(bms::GattChar ch, uint8_t* out, size_t out_size) override;
    bool subscribe(NotifyHandler* handler) override;
    void disconnect() override;
    int rssi() const override;

  private:
    NimBLERemoteCharacteristic* charFor(bms::GattChar ch) const;

    NimBLEClient* client_;
    NimBLERemoteCharacteristic* char_rx_;   // FFF1
    NimBLERemoteCharacteristic* char_tx_;   // FFF2
    NimBLERemoteCharacteristic* char_hs_;   // FFFA
    NotifyHandler* notify_handler_;
};

#endif  // ARDUINO
#endif  // WATTCYCLE_NIMBLE_TRANSPORT_H
