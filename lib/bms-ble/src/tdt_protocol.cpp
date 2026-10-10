// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "bms_ble/tdt_protocol.h"

namespace bms {
namespace tdt {

const char kHandshakeMagic[] = "HiLink";   // 48 69 4C 69 6E 6B -> FFFA (bms-protocol §3)

// kHandshakeMagicLen (tdt_protocol.h) is hand-maintained separately, since
// it's declared alongside the `extern` before this definition's array size
// is visible there. Catch drift at compile time instead of letting an edit
// to one silently desync from the other.
static_assert(sizeof(kHandshakeMagic) - 1 == kHandshakeMagicLen,
              "kHandshakeMagicLen must match strlen(kHandshakeMagic)");

namespace {

inline uint16_t be16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

// Copy a fixed-width ASCII field, stopping at the first NUL and trimming
// trailing whitespace. `dst` must hold width + 1 bytes.
void copy_field(char* dst, const uint8_t* src, size_t width) {
    size_t n = 0;
    while (n < width && src[n] != 0x00) ++n;
    while (n > 0 && (uint8_t)src[n - 1] <= 0x20) --n;
    for (size_t i = 0; i < n; ++i) dst[i] = (char)src[i];
    dst[n] = '\0';
}

}  // namespace

// --- CRC -------------------------------------------------------------------

uint16_t crc16_modbus(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x0001) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

// --- Request building ------------------------------------------------------

size_t build_request(uint8_t cmd, uint8_t* out, size_t out_size) {
    if (out == 0 || out_size < kRequestLen) return 0;

    out[0] = kRequestHead;
    out[1] = 0x00;
    out[2] = 0x01;
    out[3] = 0x03;
    out[4] = 0x00;
    out[5] = cmd;
    out[6] = 0x00;
    out[7] = 0x00;                        // payload length: requests carry none

    const uint16_t crc = crc16_modbus(out, 8);
    out[8] = (uint8_t)(crc >> 8);         // big-endian — hi byte first (bms-protocol §4)
    out[9] = (uint8_t)(crc & 0xFF);
    out[10] = kTerminator;

    return kRequestLen;
}

// --- Reassembly ------------------------------------------------------------

void FrameReassembler::reset() {
    len_ = 0;
    timing_ = false;
    started_ms_ = 0;
    frame_.cmd = 0;
    frame_.payload = 0;
    frame_.payload_len = 0;
}

FrameReassembler::Status FrameReassembler::validate(size_t total) {
    const size_t body = total - kTrailerLen;   // everything the CRC covers

    // Terminator first: a wrong last byte means the length byte lied, and the
    // CRC result would be meaningless anyway.
    if (buf_[total - 1] != kTerminator) {
        discarded_ += total;
        len_ = 0;
        timing_ = false;
        return kBadTerminator;
    }

    const uint16_t calc = crc16_modbus(buf_, body);
    const uint16_t got = (uint16_t)((uint16_t)buf_[body] << 8 | buf_[body + 1]);
    if (calc != got) {
        discarded_ += total;
        len_ = 0;
        timing_ = false;
        return kCrcError;
    }

    frame_.cmd = buf_[kOffsetCmd];
    frame_.payload = buf_ + kHeaderLen;
    frame_.payload_len = buf_[kOffsetPayloadLen];

    // Leave the buffer contents intact: frame_.payload points into it and stays
    // valid until the next feed() or reset().
    len_ = 0;
    timing_ = false;
    return kComplete;
}

size_t FrameReassembler::feed(const uint8_t* data, size_t len, Status& status) {
    status = kIncomplete;
    if (data == 0) return 0;

    size_t i = 0;
    while (i < len) {
        const uint8_t b = data[i++];

        // Rule 1: resync by scanning for 0x7E, discarding anything before it.
        // Only applies between frames — a 0x7E *inside* a payload (cell 4 reads
        // 0x0B7E in the reference capture) is just a byte, because once len_ > 0
        // we are length-driven and never look at head bytes again.
        if (len_ == 0 && b != kResponseHead) {
            ++discarded_;
            continue;
        }

        buf_[len_++] = b;

        // Rule 2: wait for 8 bytes, then read payload_len from offset 7.
        if (len_ < kHeaderLen) continue;

        // Rule 3: wait for 8 + payload_len + 3 bytes total.
        const size_t total = kHeaderLen + buf_[kOffsetPayloadLen] + kTrailerLen;
        if (len_ < total) continue;

        // Rule 4: validate CRC and terminator.
        status = validate(total);
        return i;   // hand control back so the caller can consume the frame
    }
    return i;
}

size_t FrameReassembler::feed(const uint8_t* data, size_t len, Status& status,
                              uint32_t now_ms) {
    const bool was_empty = (len_ == 0);
    const size_t consumed = feed(data, len, status);
    if (len_ > 0 && (was_empty || !timing_)) {
        started_ms_ = now_ms;      // first byte of this frame landed now
        timing_ = true;
    }
    return consumed;
}

// Rule 5: time out and reset a partial frame after ~1 s.
void FrameReassembler::tick(uint32_t now_ms) {
    if (len_ == 0 || !timing_) return;
    // Signed, because the notification task stamps feed() and the loop calls tick(): a loop
    // that read its clock before a blocking write ticks with a time older than the stamp,
    // and an unsigned age wrapped to ~2^32 ms and dropped the frame (GateLink L7, 2026-10-02).
    const int32_t age_ms = (int32_t)(now_ms - started_ms_);
    if (age_ms >= (int32_t)kPartialFrameTimeoutMs) {
        discarded_ += len_;
        len_ = 0;
        timing_ = false;
    }
}

// --- Decode: command 0x8C (bms-protocol §6) -------------------------------------------

bool decode_cells_and_pack(const Frame& frame, BmsData& out) {
    out.clear();

    if (frame.cmd != kCmdCellsPack || frame.payload == 0) return false;

    const uint8_t* p = frame.payload;
    const size_t n = frame.payload_len;

    // offset 0: cell count N
    if (n < 1) return false;
    const uint8_t cells = p[0];
    if (cells == 0 || cells > kMaxCells) return false;

    // offset 1: N x u16 cell voltage (mV)
    size_t off = 1 + (size_t)cells * 2;

    // offset 1+2N: temp sensor count M
    if (n < off + 1) return false;
    const uint8_t temps = p[off];
    if (temps > kMaxTemps) return false;
    off += 1;

    // offset 2+2N: M x u16 temperature
    const size_t tail = off + (size_t)temps * 2;

    // Then a fixed 14-byte tail: current, pack V, remaining, nominal, cycles,
    // SOH, SOC — seven u16s. Anything shorter is a truncated or foreign frame.
    if (n < tail + 14) return false;

    out.cell_count = cells;
    for (uint8_t i = 0; i < cells; ++i) {
        out.cell_mv[i] = be16(p + 1 + (size_t)i * 2);
    }

    out.temp_count = temps;
    for (uint8_t i = 0; i < temps; ++i) {
        // 0.1 K raw; °C = (raw - 2731) / 10. Kept as 0.1 °C, signed.
        out.temp_dc[i] = (int16_t)((int32_t)be16(p + off + (size_t)i * 2) - 2731);
    }

    // Current: bit 15 is the sign, set while discharging; the low 14 bits are the
    // magnitude in 0.1 A. Bit 14 was set at rest, on charge and under load, so it carries
    // no direction. Measured against the WattCycle app and the MPPT (M7, bms-protocol §6).
    const uint16_t raw_i = be16(p + tail);
    out.current_raw = raw_i;
    const int32_t magnitude_ma = (int32_t)(raw_i & 0x3FFF) * 100;
    out.discharging = (raw_i & 0x8000) != 0;
    out.current_ma = out.discharging ? -magnitude_ma : magnitude_ma;

    out.pack_mv       = (uint32_t)be16(p + tail + 2) * 10;   // x10 mV
    out.remaining_dAh = be16(p + tail + 4);                  // x0.1 Ah
    out.nominal_dAh   = be16(p + tail + 6);                  // x0.1 Ah
    out.cycles        = be16(p + tail + 8);
    out.soh_dpct      = be16(p + tail + 10);                 // x0.1 %
    out.soc_pct       = (uint8_t)be16(p + tail + 12);        // %

    out.valid = true;
    return true;
}

// --- Decode: command 0x92 (bms-protocol §9) -------------------------------------------

bool decode_device_info(const Frame& frame, DeviceInfo& out) {
    out.clear();

    if (frame.cmd != kCmdDeviceInfo || frame.payload == 0) return false;

    const size_t w = DeviceInfo::kFieldLen;
    if (frame.payload_len < w * 3) return false;

    copy_field(out.sw_version, frame.payload, w);
    copy_field(out.manufacturer, frame.payload + w, w);
    copy_field(out.serial_number, frame.payload + w * 2, w);
    return true;
}

}  // namespace tdt
}  // namespace bms
