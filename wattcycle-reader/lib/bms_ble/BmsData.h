// BmsData.h — decoded battery record.
//
// Pure data. No Arduino, no NimBLE, no floating point in the storage format:
// every field is an integer in a fixed unit so the decode is exact and the
// same struct can be printed, rendered to an OLED, or serialised to MQTT
// without re-deriving anything. (§7 rule 1, §8: "decode produces a struct".)
#ifndef BMS_BLE_BMSDATA_H
#define BMS_BLE_BMSDATA_H

#include <stddef.h>
#include <stdint.h>

namespace bms {

// The 4S pack in §3 needs 4/4. Headroom kept for other TDT units; these bound
// the fixed buffers below, and the decoder rejects anything larger.
static const uint8_t kMaxCells = 16;
static const uint8_t kMaxTemps = 8;

// One poll's worth of battery state, from command 0x8C (§5.4).
struct BmsData {
    bool valid;             // false until a frame has decoded cleanly

    uint8_t  cell_count;
    uint16_t cell_mV[kMaxCells];

    uint8_t  temp_count;
    int16_t  temp_dC[kMaxTemps];   // 0.1 °C, signed

    uint32_t pack_mV;              // pack voltage, mV
    int32_t  current_mA;           // + = charge, - = discharge  (SEE WARNING)
    bool     discharging;          // raw 0x4000 flag, before sign interpretation

    uint8_t  soc_pct;              // state of charge, %
    uint16_t remaining_dAh;        // 0.1 Ah
    uint16_t nominal_dAh;          // 0.1 Ah
    uint16_t cycles;
    uint16_t soh_dpct;             // state of health, 0.1 %

    // WARNING (§5.8, open question): the sign convention is NOT verified.
    // The battery has only ever been observed at rest, where current reads raw
    // 0x4000 — which is exactly the discharge flag with zero magnitude, so a
    // resting pack cannot disambiguate charge from discharge. `discharging`
    // records the raw flag; `current_mA` applies the assumed convention.
    // Capture 0x8C under charge and under load and confirm before trusting it.

    void clear() {
        valid = false;
        cell_count = 0;
        temp_count = 0;
        pack_mV = 0;
        current_mA = 0;
        discharging = false;
        soc_pct = 0;
        remaining_dAh = 0;
        nominal_dAh = 0;
        cycles = 0;
        soh_dpct = 0;
        for (uint8_t i = 0; i < kMaxCells; ++i) cell_mV[i] = 0;
        for (uint8_t i = 0; i < kMaxTemps; ++i) temp_dC[i] = 0;
    }

    uint16_t minCell_mV() const {
        if (cell_count == 0) return 0;
        uint16_t lo = cell_mV[0];
        for (uint8_t i = 1; i < cell_count; ++i)
            if (cell_mV[i] < lo) lo = cell_mV[i];
        return lo;
    }

    uint16_t maxCell_mV() const {
        if (cell_count == 0) return 0;
        uint16_t hi = cell_mV[0];
        for (uint8_t i = 1; i < cell_count; ++i)
            if (cell_mV[i] > hi) hi = cell_mV[i];
        return hi;
    }

    // Cell imbalance — the number worth watching over time on a LiFePO4 pack.
    uint16_t deltaCell_mV() const {
        return cell_count == 0 ? 0 : (uint16_t)(maxCell_mV() - minCell_mV());
    }

    // Hottest sensor. Used where only one temperature can be shown: which
    // sensor is which (ambient / MOSFET / cell) is not established for this
    // unit, and the maximum is the one figure that never understates a
    // thermal problem.
    int16_t maxTemp_dC() const {
        if (temp_count == 0) return 0;
        int16_t hi = temp_dC[0];
        for (uint8_t i = 1; i < temp_count; ++i)
            if (temp_dC[i] > hi) hi = temp_dC[i];
        return hi;
    }
};

// Device identity, from command 0x92 (§5.7).
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

#endif  // BMS_BLE_BMSDATA_H
