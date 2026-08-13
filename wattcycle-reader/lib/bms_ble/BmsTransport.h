// BmsTransport.h — the seam between the protocol and the radio (§7 rule 2).
//
// TdtBmsClient (M2+) drives the state machine through this interface and never
// names NimBLE. NimBleTransport is the only file allowed to include NimBLE
// headers; GateLink can substitute its own transport and inherit the state
// machine and decode for free.
//
// Header-only, abstract, no Arduino types — so it compiles on the host too.
//
// STATUS: interface only. No implementation exists yet; M1 is scan-and-print
// (see src/main.cpp), and the first implementer lands at M2/M3.
#ifndef BMS_BLE_BMSTRANSPORT_H
#define BMS_BLE_BMSTRANSPORT_H

#include <stddef.h>
#include <stdint.h>

namespace bms {

// The three characteristics that matter, all under service 0xFFF0 (§4).
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

// MTU to request on connect (§5.5). The default of 23 on NimBLE leaves 20
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
        virtual void onNotify(const uint8_t* data, size_t len) = 0;
    };

    virtual ~BmsTransport() {}

    virtual bool isConnected() const = 0;

    // Write to a characteristic. `with_response` matters: every request to this
    // BMS is written WITH response (§5.2), and a successful ATT write proves
    // nothing about application-layer acceptance (§10.3).
    virtual bool write(GattChar ch, const uint8_t* data, size_t len,
                       bool with_response) = 0;

    // Read a characteristic back. Used for the FFFA 0x01 handshake ack, which
    // is the gate: if it isn't 0x01, do not proceed (§5.1 step 3).
    // Returns bytes read, or -1 on error.
    virtual int read(GattChar ch, uint8_t* out, size_t out_size) = 0;

    // Subscribe to notifications on Rx (0xFFF1) by writing its CCCD.
    virtual bool subscribe(NotifyHandler* handler) = 0;

    virtual void disconnect() = 0;

    // Link quality — exposed as a diagnostic because it is the early-warning
    // signal for a mount degrading from moisture or corrosion (§5.8).
    virtual int rssi() const = 0;
};

}  // namespace bms

#endif  // BMS_BLE_BMSTRANSPORT_H
