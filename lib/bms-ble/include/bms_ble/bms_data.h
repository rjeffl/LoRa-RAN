// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The decoded battery record.
//
// Pure data. No Arduino, no NimBLE and no floating point in the storage format. Every field
// is an integer in a fixed unit, so the decode is exact, and the same struct can be printed,
// drawn on a display or packed into a status payload without deriving anything again.
#ifndef BMS_BLE_BMS_DATA_H
#define BMS_BLE_BMS_DATA_H

#include <stddef.h>
#include <stdint.h>

namespace bms {

// The 4S pack at the gate needs 4/4. Headroom kept for other TDT units; these bound
// the fixed buffers below, and the decoder rejects anything larger.
static const uint8_t kMaxCells = 16;
static const uint8_t kMaxTemps = 8;

// One poll's worth of battery state, from command 0x8C (bms-protocol §6).
struct BmsData {
    bool valid;             // false until a frame has decoded cleanly

    uint8_t  cell_count;
    uint16_t cell_mv[kMaxCells];

    uint8_t  temp_count;
    int16_t  temp_dc[kMaxTemps];   // 0.1 °C, signed

    uint32_t pack_mv;              // pack voltage, mV
    int32_t  current_ma;           // + = charge, - = discharge (M7)
    bool     discharging;          // bit 15 of the raw field
    uint16_t current_raw;          // the field as received; bit 14 is not yet understood

    uint8_t  soc_pct;              // state of charge, %
    uint16_t remaining_dAh;        // 0.1 Ah
    uint16_t nominal_dAh;          // 0.1 Ah
    uint16_t cycles;
    uint16_t soh_dpct;             // state of health, 0.1 %

    void clear() {
        valid = false;
        cell_count = 0;
        temp_count = 0;
        pack_mv = 0;
        current_ma = 0;
        discharging = false;
        current_raw = 0;
        soc_pct = 0;
        remaining_dAh = 0;
        nominal_dAh = 0;
        cycles = 0;
        soh_dpct = 0;
        for (uint8_t i = 0; i < kMaxCells; ++i) cell_mv[i] = 0;
        for (uint8_t i = 0; i < kMaxTemps; ++i) temp_dc[i] = 0;
    }

    uint16_t min_cell_mv() const {
        if (cell_count == 0) return 0;
        uint16_t lo = cell_mv[0];
        for (uint8_t i = 1; i < cell_count; ++i)
            if (cell_mv[i] < lo) lo = cell_mv[i];
        return lo;
    }

    uint16_t max_cell_mv() const {
        if (cell_count == 0) return 0;
        uint16_t hi = cell_mv[0];
        for (uint8_t i = 1; i < cell_count; ++i)
            if (cell_mv[i] > hi) hi = cell_mv[i];
        return hi;
    }

    // Cell imbalance — the number worth watching over time on a LiFePO4 pack.
    uint16_t delta_cell_mv() const {
        return cell_count == 0 ? 0 : (uint16_t)(max_cell_mv() - min_cell_mv());
    }

    // Hottest sensor. Used where only one temperature can be shown: which
    // sensor is which (ambient / MOSFET / cell) is not established for this
    // unit, and the maximum is the one figure that never understates a
    // thermal problem.
    int16_t max_temp_dc() const {
        if (temp_count == 0) return 0;
        int16_t hi = temp_dc[0];
        for (uint8_t i = 1; i < temp_count; ++i)
            if (temp_dc[i] > hi) hi = temp_dc[i];
        return hi;
    }
};

// Device identity, from command 0x92 (bms-protocol §9).
//
// Note: manufacturer and serial_number are unprogrammed placeholder patterns
// on this unit ("11112222...", "IKKKK0000..."). Do not key anything off them —
// in particular, never use serial number to tell two batteries apart.
struct DeviceInfo {
    static const size_t kFieldLen = 20;   // three fixed 20-byte ASCII fields
    char sw_version[kFieldLen + 1];
    char manufacturer[kFieldLen + 1];
    char serial_number[kFieldLen + 1];

    void clear() {
        sw_version[0] = '\0';
        manufacturer[0] = '\0';
        serial_number[0] = '\0';
    }
};

}  // namespace bms

#endif  // BMS_BLE_BMS_DATA_H
