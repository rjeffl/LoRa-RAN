// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Schema 0x10 - GateLink status v1, 78 bytes. Spec 7.2.
// Schema 0xFE - simnode synthetic status, bench only, mirrors this layout (spec 7.1).

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/types.h"

namespace lran {
namespace schema {

inline constexpr size_t kGateLinkStatusV1Len = 78;  // spec 7.1 registry
inline constexpr uint8_t kMaxCells = 4;  // spec 7.2.3 - >4 requires a new schema ID

struct GateLinkStatusV1 {
  // --- Gate block, offsets 0-9 (spec 7.2.1) ---
  uint8_t gate_state     = 0;  // 0   spec 8.3, derived from IN1/IN2
  uint8_t input_bits     = 0;  // 1   debounced raw inputs; the evidence for every
                               //     derivation the node performs, so a state-machine
                               //     bug is diagnosable from a logged frame
  uint8_t hold           = 0;  // 2   bit 0 held_open, bits 3:1 hold_source (spec 8.4)
  uint8_t movement_cause = 0;  // 3   spec 8.5
  uint8_t last_direction = 0;  // 4   spec 8.6
  uint8_t detect_flags   = 0;  // 5   spec 7.2.5
  // 6..9  spec 7.2.9 - uint32 because uint16 saturates at 18h12m, which a driveway
  //       reaches routinely and reaches precisely in the away-from-home condition
  //       the field exists to report. An AGE, not a timestamp: correct on the bridge
  //       the moment it arrives regardless of clock skew.
  uint32_t last_traversal_age_s = kU32NotAvailable;

  // --- MPPT block, offsets 10-35 (spec 7.2.2) ---
  uint16_t batt_mv       = 0;                 // 10  mV,      VE.Direct V
  int16_t  batt_ma       = 0;                 // 12  mA,      I, negative = discharge
  uint16_t pv_cv         = 0;                 // 14  10 mV,   VPV. 10 mV units because
                                              //     the 75/15 accepts 75 V and mV
                                              //     overflows uint16 at 65.5 V
  uint16_t pv_w          = 0;                 // 16  W,       PPV
  int16_t  load_ma       = kI16NotAvailable;  // 18  mA,      IL
  uint16_t yield_today   = 0;                 // 20  10 Wh,   H20
  uint16_t yield_yest    = 0;                 // 22  10 Wh,   H22
  uint16_t pmax_today    = 0;                 // 24  W,       H21
  uint32_t yield_total   = 0;                 // 26  10 Wh,   H19. uint32 - uint16
                                              //     overflows at 655 kWh
  uint8_t  charge_state  = 0;                 // 30  CS,   passed through unmodified
  uint8_t  mppt_err      = 0;                 // 31  ERR,  non-zero triggers a push
  uint8_t  mppt_tracker  = 0;                 // 32  MPPT, passed through
  uint8_t  mppt_flags    = 0;                 // 33  spec 7.2.6
  int16_t  mppt_temp_c10 = kI16NotAvailable;  // 34  0.1 C

  // --- BMS block, offsets 36-63 (spec 7.2.3) ---
  uint8_t  bms_soc          = kSocNotAvailable;  // 36  %
  uint8_t  bms_flags        = 0;                 // 37  spec 7.2.7
  uint16_t pack_mv          = 0;                 // 38  mV
  int16_t  pack_ma          = 0;                 // 40  mA
  // TODO(W6): pack_ma sign convention unconfirmed. Bit 0x4000 is believed to be the
  // discharge flag but has only ever been observed at 0.0 A - capture once under
  // charge and once under load.
  uint8_t  cell_count       = 0;  // 42  cells actually reported
  uint8_t  bms_rssi_neg     = 0;  // 43  magnitude of BLE RSSI; 80 means -80 dBm,
                                  //     0 means no link. Evidence for GateLink D28
  uint16_t cell_mv[kMaxCells]     = {0, 0, 0, 0};  // 44,46,48,50  mV
  int8_t   cell_temp_c[kMaxCells] = {0, 0, 0, 0};  // 52,53,54,55  whole degrees C
  uint16_t bms_cycles       = 0;  // 56
  uint16_t bms_capacity_dah = 0;  // 58  0.1 Ah
  uint16_t bms_alarms       = 0;  // 60  passed through from the BMS unmodified
  // 62  spec 7.2.9 - stays uint16 deliberately. A staleness gate, not a duration
  //     record: past roughly an hour the BLE link is dead and every consumer
  //     thresholds rather than reads it.
  uint16_t bms_age_s = kU16NotAvailable;

  // --- Node block, offsets 64-77 (spec 7.2.4) ---
  uint32_t uptime_s            = 0;                 // 64  s
  uint16_t boot_count          = 0;                 // 68  0 if unavailable
  uint16_t node_mv             = 0;                 // 70  mV,   INA226
  int16_t  node_ma             = 0;                 // 72  mA,   INA226
  int16_t  enclosure_temp_c10  = kI16NotAvailable;  // 74  0.1 C, LM75
  uint8_t  node_flags          = 0;                 // 76  spec 7.2.8
  uint8_t  status_reason       = 0;                 // 77  spec 8.7
};

Status serialize(const GateLinkStatusV1& v, uint8_t* out, size_t cap, size_t* written);
Status deserialize(const uint8_t* in, size_t len, GateLinkStatusV1* out);

}  // namespace schema
}  // namespace lran
