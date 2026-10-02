// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The seam between the protocol and the radio.
//
// NimBleTransport (nimble_transport.h/.cpp) is the only code in this library that includes
// NimBLE headers. A firmware can substitute its own transport and keep the reassembler and
// decoder.
//
// Header-only, abstract, no Arduino types, so it compiles on the host too.
//
// SCOPE NOTE: this interface covers what happens *after* a connection exists: write, read,
// subscribe and rssi. Scanning and connecting are the client's, and talk to NimBLE directly.
// wattcycle-reader's src/main.cpp does that for the PoC; GateLink's bms_task does it against
// PRD R-3.4a/R-3.4b. A client that wants to swap transports needs to abstract scanning and
// connecting too, because this interface alone doesn't cover them.
#ifndef BMS_BLE_BMS_TRANSPORT_H
#define BMS_BLE_BMS_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

namespace bms {

// The three characteristics that matter, all under service 0xFFF0 (bms-protocol §2).
// Service 02F00000-...-FE00 / FF00-FF05 is a Telink SDK chipset service that
// carries no battery data. Ignore it. Do not write to it.
enum class GattChar {
    Rx,          // 0xFFF1 — notify, read.  Responses arrive here.
    Tx,          // 0xFFF2 — write, write-no-response, read.  Requests go here.
    Handshake    // 0xFFFA — write, read.   "HiLink" goes here, ack read back.
};

// 16-bit UUIDs for the above, in the same order.
static const uint16_t kUuidService  = 0xFFF0;
static const uint16_t kUuidCharRx   = 0xFFF1;
static const uint16_t kUuidCharTx   = 0xFFF2;
static const uint16_t kUuidCharHs   = 0xFFFA;

// MTU to request on connect (bms-protocol §7). The default of 23 on NimBLE leaves 20
// bytes of payload and fragments every response; macOS negotiated 512 and the
// 71-byte device-info response arrived whole. Ask for it explicitly — and
// still run the reassembler, because negotiation can be refused.
static const uint16_t kDesiredMtu = 517;

class BmsTransport {
  public:
    // Raw notification sink. Called from whatever context the BLE stack uses,
    // so implementations must keep it short — feed the reassembler, no more.
    class NotifyHandler {
      public:
        virtual ~NotifyHandler() {}
        virtual void on_notify(const uint8_t* data, size_t len) = 0;
    };

    virtual ~BmsTransport() {}

    virtual bool is_connected() const = 0;

    // Write to a characteristic. `with_response` matters: every request to this
    // BMS is written WITH response (bms-protocol §4), and a successful ATT write proves
    // nothing about application-layer acceptance (bms-protocol §11).
    virtual bool write(GattChar ch, const uint8_t* data, size_t len,
                       bool with_response) = 0;

    // Read a characteristic back. Used for the FFFA 0x01 handshake ack, which
    // is the gate: if it isn't 0x01, do not proceed (bms-protocol §3 step 3).
    // Returns bytes read, or -1 on error.
    virtual int read(GattChar ch, uint8_t* out, size_t out_size) = 0;

    // Subscribe to notifications on Rx (0xFFF1) by writing its CCCD.
    virtual bool subscribe(NotifyHandler* handler) = 0;

    virtual void disconnect() = 0;

    // Link quality — exposed as a diagnostic because it is the early-warning
    // signal for a mount degrading from moisture or corrosion (bms-protocol §10).
    //
    // KNOWN LIMITATION: 0 doubles as "not connected" / "read failed" in this
    // implementation and in wattcycle-reader's scan-result handling, rather than
    // a distinct sentinel. A real 0 dBm reading would be misread as "no
    // signal" — not a practical concern at BLE ranges (would mean the
    // antennas are essentially touching), but a caller relying on this for
    // something other than the diagnostic/aiming use case here should be
    // aware the interface doesn't distinguish the two.
    virtual int rssi() const = 0;
};

}  // namespace bms

#endif  // BMS_BLE_BMS_TRANSPORT_H
