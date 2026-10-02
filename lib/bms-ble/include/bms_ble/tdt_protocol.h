// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// TDT smart BMS wire protocol: framing, CRC, reassembly and decode.
//
// THIS FILE AND ITS .cpp MUST COMPILE ON THE BUILD HOST. No <Arduino.h>, no NimBLE, no
// String, no heap and no delay(). The library's `native` environment enforces it, and it is
// what lets test/test_tdt_protocol run the whole decoder against the captured frames in
// bms-protocol §9 with no board attached.
//
// Protocol reference: docs/gatelink/bms-protocol.md. Section numbers below point into it.
#ifndef BMS_BLE_TDT_PROTOCOL_H
#define BMS_BLE_TDT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#include "bms_ble/bms_data.h"

namespace bms {
namespace tdt {

// --- Frame constants (bms-protocol §4) ------------------------------------------------
// request:   1E 00 01 03 00 <cmd> 00 00 <crc_hi> <crc_lo> 0D          (11 bytes)
// response:  7E 00 01 03 00 <cmd> 00 <len> <payload...> <crc_hi> <crc_lo> 0D

static const uint8_t kRequestHead  = 0x1E;   // NOT 0x7E — see bms-protocol §4
static const uint8_t kResponseHead = 0x7E;
static const uint8_t kTerminator   = 0x0D;

static const size_t kHeaderLen  = 8;    // head .. payload_len inclusive
static const size_t kTrailerLen = 3;    // crc_hi, crc_lo, 0x0D
static const size_t kRequestLen = 11;
static const size_t kMaxPayloadLen = 255;
static const size_t kMaxFrameLen   = kHeaderLen + kMaxPayloadLen + kTrailerLen;

static const size_t kOffsetCmd        = 5;
static const size_t kOffsetPayloadLen = 7;

// Drop a partial frame that stops mid-flight (bms-protocol §7 rule 5).
static const uint32_t kPartialFrameTimeoutMs = 1000;

// --- Commands (bms-protocol §5) -------------------------------------------------------
enum Command {
    kCmdCellsPack  = 0x8C,   // cells, temps, pack V/I, SOC, capacity, cycles
    kCmdAlarms      = 0x8D,   // alarm/protection bitmaps, MOSFET status
    kCmdDeviceInfo = 0x92    // SW version, manufacturer, serial number
};

// The handshake magic written to FFFA before anything else works (bms-protocol §3).
// Not a challenge/response — a fixed ASCII string.
extern const char kHandshakeMagic[];      // "HiLink"
static const size_t kHandshakeMagicLen = 6;
static const uint8_t kHandshakeAck = 0x01;  // what reading FFFA back must return

// --- CRC (bms-protocol §4) ------------------------------------------------------------
// CRC-16/MODBUS: poly 0x8005, init 0xFFFF, reflected in/out, no final XOR.
// Computed over every byte from the head up to but excluding the CRC itself.
//
// It is transmitted BIG-ENDIAN (hi byte first), which is the opposite of the
// Modbus convention. build_request() and the reassembler both handle this;
// don't hand-roll it a third time.
uint16_t crc16_modbus(const uint8_t* data, size_t len);

// --- Request building ------------------------------------------------------
// Writes an 11-byte request for `cmd` into `out`. Returns bytes written, or 0
// if the buffer is too small. All requests go to FFF2, written WITH response.
size_t build_request(uint8_t cmd, uint8_t* out, size_t out_size);

// --- A validated, complete response ---------------------------------------
struct Frame {
    uint8_t        cmd;
    const uint8_t* payload;      // points into the reassembler's buffer
    uint8_t        payload_len;
};

// --- Reassembly (bms-protocol §7) -----------------------------------------------------
//
// Frames on the LENGTH BYTE, never on the terminator. A 0x8C response contains
// four literal 0x0D bytes (cell voltages near 3.4 V encode as 0x0D89, 0x0DA1,
// ...) and a literal 0x7E (cell 4 = 0x0B7E), so both "read until 0x0D" and
// naive head-scanning inside a frame corrupt data silently. Both cases are
// covered by tests.
//
// MTU 512 was negotiated on macOS and the 71-byte response arrived as a single
// notification — but MTU negotiation can fail or be refused, so reassembly is
// implemented regardless. Treat one-notification frames as the happy path,
// not the contract.
class FrameReassembler {
  public:
    enum Status {
        kIncomplete,      // need more bytes
        kComplete,        // frame() is valid until the next feed()/reset()
        kCrcError,
        kBadTerminator
    };

    FrameReassembler() : discarded_(0) { reset(); }

    // Discard any partial frame and resync from the next 0x7E. Does not clear
    // discarded_bytes(), which is a lifetime counter.
    void reset();

    // Feed one notification (or any chunk of one). Returns how many bytes were
    // consumed and sets `status`. A chunk may contain more than one frame, so
    // the caller must loop until the whole chunk is consumed:
    //
    //     size_t off = 0;
    //     while (off < len) {
    //         FrameReassembler::Status st;
    //         off += rx.feed(data + off, len - off, st);
    //         if (st == FrameReassembler::kComplete) handle(rx.frame());
    //     }
    size_t feed(const uint8_t* data, size_t len, Status& status);

    // Same, but stamps the arrival time so tick() can time out a partial frame.
    size_t feed(const uint8_t* data, size_t len, Status& status, uint32_t now_ms);

    // Call from the main loop with a monotonic millisecond clock. Drops a
    // partial frame that has been sitting incomplete for too long. A clock
    // older than the partial's arrival stamp is not a timeout, so a caller
    // may tick with a time it read before a blocking write.
    void tick(uint32_t now_ms);

    // Valid only immediately after feed() reported kComplete.
    const Frame& frame() const { return frame_; }

    bool   has_partial() const { return len_ > 0; }
    size_t discarded_bytes() const { return discarded_; }   // resync diagnostic

  private:
    Status validate(size_t total);

    uint8_t  buf_[kMaxFrameLen];
    size_t   len_;
    size_t   discarded_;
    uint32_t started_ms_;
    bool     timing_;
    Frame    frame_;
};

// --- Decode ----------------------------------------------------------------

// Command 0x8C (bms-protocol §6). Returns false and leaves `out` invalid if the frame is
// the wrong command, is truncated, or declares more cells/sensors than
// kMaxCells/kMaxTemps. Layout is driven by two inline counts, so every offset
// past the cell block depends on the payload being self-consistent — hence the
// bounds checks rather than trusting payload_len alone.
bool decode_cells_and_pack(const Frame& frame, BmsData& out);

// Command 0x92 (bms-protocol §9). Three fixed 20-byte ASCII fields, NUL/space trimmed.
bool decode_device_info(const Frame& frame, DeviceInfo& out);

// Command 0x8D is deliberately NOT decoded here. It is only partially
// understood (bms-protocol §9): `0629` sits where MOSFET and status bits appear to live,
// but mapping the bitmaps properly needs a capture taken during a real
// protection event. Writing a speculative bit map now would produce
// confident-looking wrong alarms (bms-protocol §10); framing and CRC for 0x8D
// frames are already covered by the reassembler and its tests.

}  // namespace tdt
}  // namespace bms

#endif  // BMS_BLE_TDT_PROTOCOL_H
