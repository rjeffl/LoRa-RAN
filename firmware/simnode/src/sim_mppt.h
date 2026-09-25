// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// A simulated Victron MPPT on the far side of ROLE_GATELINK's UART. Task BF-36; spec 6.7,
// 7.6, 8.13. Registers follow osh-labs/VE.Direct_mppt_arduino, the VE.Direct reference of
// record (GateLink Impl Plan 4.2.4); Victron's "BlueSolar HEX protocol" document, section 1,
// fills the gaps the library leaves.
//
// ARDUINO-FREE. This is the MPPT, not the node: it takes a HEX request string and returns
// what an MPPT would put on the UART. gatelink.cpp is the node around it, and holds the
// transport rules - authentication, the gate, BUSY and TIMEOUT.
//
// WHY IT KNOWS REGISTERS WHEN THE NODE MUST NOT. Spec 7.6 makes the node transport only,
// and the node code keeps to that: it inspects the command nibble and nothing else. The
// MPPT is the thing that holds registers, so a simulator of it holds some. A Get or Set
// answers from this table, so a write accepted while armed can be read back changed, which
// is the only evidence V-B6 has that the write reached anything.
//
// CANNED, NOT PHYSICAL. A LiFePO4 profile for a 12 V pack, set as GateLink PRD R-6.1b asks:
// battery type user-defined, equalisation off, temperature compensation 0 mV/K. The values
// are plausible. They are not GateLink's pack specification, and BF-30's readback against
// the real MPPT at B6 is what confirms the register semantics.

#pragma once

#include <cstddef>
#include <cstdint>

namespace simnode {

struct SimRegister {
  uint16_t id       = 0;
  uint8_t  width    = 0;  // bytes: 1 or 2
  bool     writable = false;
  uint32_t value    = 0;
};

inline constexpr size_t kSimMpptRegisters = 12;

// What the MPPT did with a request. `Silent` is Victron's Restart, which "no response is
// sent", and is what a real node reports as HEX_RSP(TIMEOUT) (spec 8.13).
enum class MpptReply : uint8_t { Answered, Silent };

class SimMppt {
 public:
  SimMppt() { reset(); }

  // Restores the canned profile.
  void reset();

  // One request string, without a newline, as the node would write it to the UART. The
  // answer goes to `out` without a newline; `*out_len` is 0 when the reply is Silent.
  MpptReply answer(const char* req, size_t n, char* out, size_t cap, size_t* out_len);

  // The console's `mppt <hex> set` - the MPPT's own front panel, so a read-only register
  // can be changed too. False for an unknown register or a value too wide for it.
  bool set(uint16_t id, uint32_t value);

  size_t             count() const { return kSimMpptRegisters; }
  const SimRegister& reg(size_t i) const { return regs_[i]; }
  const SimRegister* find(uint16_t id) const;

  // Local diagnostics. Writes accepted is what V-B6's "accepted while armed" counts.
  uint32_t requests() const { return requests_; }
  uint32_t writes() const { return writes_; }

 private:
  SimRegister* find_mut(uint16_t id);

  SimRegister regs_[kSimMpptRegisters];
  uint32_t    requests_ = 0;
  uint32_t    writes_   = 0;
};

// The MPPT 75/15 GateLink carries (Victron section 1's product id table).
inline constexpr uint16_t kSimProductId = 0xA042;
// Application firmware 1.64, as Victron's ping encoding writes it: type b01 in the top two
// bits, then the version in hex digits.
inline constexpr uint16_t kSimFirmware  = 0x4164;

}  // namespace simnode
