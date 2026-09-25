// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The MPPT's charge parameters, read back and published as diagnostic sensors. Task BF-30;
// PRD R-3.5d, R-3.5e; Impl Plan 6.4; Victron's "BlueSolar HEX protocol", section 1.1,
// "Battery settings registers".
//
// ARDUINO-FREE, AND IT DOES NO I/O. sched_task asks which register to read next, sends
// the Get through the HEX proxy, and hands the answer back.
//
// WHY THE BRIDGE HOLDS REGISTER SEMANTICS. The node is transport only (spec 7.6). A
// register's meaning is the most change-prone part of this interface, and getting one
// wrong under LiFePO4 is a battery-damage path, so it lives where a correction is an OTA
// rather than a walk to the gate (PRD 3.5). This table IS that model, and it is small on
// purpose: R-3.5e forbids modelling a hundred registers as entities. It reads the ones
// that decide how the pack is charged, and publishes them so a wrong profile is visible in
// Home Assistant rather than latent (R-3.5d).
//
// SCALES ARE VICTRON'S, NOT OBSERVED. Every scale and width below is transcribed from the
// document; none has been read from GateLink's MPPT 75/15 yet. BF-30's readback against the
// real MPPT at B6 is what confirms them. A wrong scale here publishes a plausible wrong
// voltage, which is exactly the failure R-3.5d exists to expose, so a disagreement at B6 is
// a finding to record, not a number to adjust until it looks right.
//
// A REFUSED OR UNREADABLE REGISTER IS NULL, never 0 V (root rule 6's intent in JSON, spec
// 16.2.1). A firmware older than the register's introduction answers Unknown Id.

#pragma once

#include <cstddef>
#include <cstdint>

#include "vedirect/hex.h"

namespace bridge {

struct ChargeRegister {
  uint16_t    id;        // Victron's register
  uint8_t     width;     // bytes on the wire
  bool        is_signed; // sn16
  uint8_t     decimals;  // Victron's scale as a power of ten: 0.01 is 2
  // The document key, which is also the HA object_id: FROZEN once published. `charge_`
  // marks a setting read back rather than a reading, beside GateLink's pack_voltage and
  // mppt_batt_voltage; chosen with the operator on 2026-09-25.
  const char* key;
  const char* unit;      // Home Assistant's unit, or nullptr
  const char* name;      // the entity's display name
};

// Victron section 1.1. Absorption, float and equalisation voltage and temperature
// compensation are the four note 5 makes writable only with a user-defined battery type,
// and the four GateLink PRD R-6.1b sets for LiFePO4.
inline constexpr ChargeRegister kChargeRegisters[] = {
    {0xEDF7, 2, false, 2, "charge_absorption_voltage_v", "V", "Absorption voltage"},
    {0xEDF6, 2, false, 2, "charge_float_voltage_v", "V", "Float voltage"},
    {0xEDF4, 2, false, 2, "charge_equalisation_voltage_v", "V", "Equalisation voltage"},
    {0xEDFD, 1, false, 0, "charge_auto_equalisation", nullptr, "Automatic equalisation"},
    {0xEDF2, 2, true, 2, "charge_temp_compensation_mv_k", "mV/K", "Temperature compensation"},
    {0xEDF1, 1, false, 0, "charge_battery_type", nullptr, "Battery type"},
    {0xEDF0, 2, false, 1, "charge_max_current_a", "A", "Maximum charge current"},
    {0xEDFB, 2, false, 2, "charge_absorption_time_limit_h", "h", "Absorption time limit"},
    {0xEDEF, 1, false, 0, "charge_system_voltage_v", "V", "System voltage setting"},
    {0xEDE0, 2, true, 2, "charge_low_temp_level_c", "°C", "Low temperature charge cut-off"},
};
inline constexpr size_t kChargeRegisterCount = sizeof(kChargeRegisters) / sizeof(kChargeRegisters[0]);
static_assert(kChargeRegisterCount <= 16, "the pending set is a 16-bit mask");

// `lran/<node>/vedirect/charge/state`, retained. Spec 16.1's grammar with `charge` as the
// item; spec 16.2 does not list the topic, which is raised for v0.16.
inline constexpr const char* kChargeItem = "charge";

// One node's readback. A pass reads every register once, in table order, and the document
// is published after each answer so a slow pass still shows what it has.
class ChargeReadback {
 public:
  // Schedules a full pass: on first hearing the node, and after any write it answered, so
  // a Set that changed a charge parameter shows in HA without a reboot.
  void request_all() { pending_ = static_cast<uint16_t>((1u << kChargeRegisterCount) - 1u); }

  bool pending() const { return pending_ != 0; }

  // The next register to Get, or nullptr when the pass is done.
  const ChargeRegister* next() const;

  // The answer to the Get of `reg`. `hex` is the HEX_RSP's string when the node answered
  // OK; nullptr for any other outcome. A register that answers with an error flag, the
  // wrong width or another register's id is null in the document.
  //
  // Returns true when the value published changed.
  bool on_answer(uint16_t reg, const char* hex, size_t n);

  // No answer at all, or a node status other than OK - a node with no MPPT behind it, or a
  // simnode identity in another role. The rest of the pass is abandoned rather than spent
  // one timeout at a time, and the next request_all() starts again.
  void abandon() { pending_ = 0; }

  // The document. Every key is present; an unread register is null. Returns the length,
  // or 0 when `cap` is short.
  size_t json(char* out, size_t cap) const;

  bool     have(size_t i) const { return (have_ & (1u << i)) != 0; }
  int32_t  value(size_t i) const { return values_[i]; }

 private:
  uint16_t pending_ = 0;
  uint16_t have_    = 0;
  int32_t  values_[kChargeRegisterCount] = {};
};

}  // namespace bridge
