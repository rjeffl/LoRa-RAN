// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The SX1262's injected configuration and the fixed PHY, as data. Spec 12.1, 12.2; Bridge
// Impl Plan 10.8.1; R-4.1b.
//
// Written for the bridge by BF-16 and moved here on 2026-09-14 with media_access, when the
// simnode became the second firmware to need it. kPhy in particular is one value for the
// whole fleet: a node on a different channel from the bridge is a node nobody hears, and
// two copies of the constant are how that happens.
//
// ARDUINO-FREE AND DRIVER-FREE. Each firmware's radio driver is the only file that turns
// kPinNone into RADIOLIB_NC. Board pin maps stay in the firmware that owns the board; this
// header holds only the shape they share.
//
// TCXO voltage is millivolts, not Impl Plan 10.8.1's float: a float that prints as "1.8"
// and compares unequal to 1.8f is a debugging session nobody needs (the range test's
// finding, taken by the bridge and now by every firmware).

#pragma once

#include <cstdint>

namespace lran {
namespace link {

// RADIOLIB_NC is -1. Spelled locally so this header pulls in no driver.
inline constexpr int8_t kPinNone = -1;

// spec 12.2 - the pin map, TCXO reference voltage and RF-switch mode are supplied by
// configuration at construction, never compiled into the driver.
struct RadioPins {
  int8_t nss;
  int8_t rst;
  int8_t busy;
  int8_t dio1;

  int8_t sck;
  int8_t miso;
  int8_t mosi;

  // kPinNone when DIO2 alone drives the RF switch. Present on every board so the
  // driver needs no #ifdef; the Wio-SX1262 carries a real one (Impl Plan 10.8.1).
  int8_t rf_sw;

  uint16_t tcxo_mv;
  bool     dio2_as_rf_switch;
};

// spec 12.1 - the radio's working point. kPhy below is the one D1 fixed on 2026-09-10
// (Decision Register 3.4).
//
// kPhy IS THE DEFAULT, NOT THE ONLY VALUE. Since spec v0.13 (D56) the PHY group is
// runtime configuration, changed only through spec 12.4's commit-and-revert, because a
// change travels over the link it changes and a node left on the other side of it is a
// walk to the gate. The bridge builds a PhyConfig from its committed group at boot and at
// each retune (BF-33, firmware/bridge/src/phy_change.h); the fields the group does not
// carry - sync word, preamble, antenna gain - come from kPhy.
struct PhyConfig {
  uint32_t freq_hz;
  uint16_t bw_khz10;  // tenths of a kHz: 1250 is 125.0 kHz
  uint8_t  sf;
  uint8_t  cr_denom;  // 5 is CR 4/5

  // Root rule 11 - conducted power and antenna gain are recorded SEPARATELY. The D33
  // ceiling is EIRP, and a combined figure cannot be audited.
  int8_t  conducted_dbm;
  uint8_t antenna_gain_dbi10;  // 30 is 3.0 dBi, Bridge PRD R-4.3a.1's stick

  // spec 12.1's "Private (0x12 / SX126x 0x1424)" are one value at two layers. RadioLib
  // takes the one-byte form and expands it into the SX126x's register pair.
  uint8_t sync_word;
  uint8_t preamble_symbols;  // spec 15.1 computes airtime on 8
};

// The antenna term is the 3.0 dBi stick every board in the fleet so far carries. A board
// with a different antenna needs its own PhyConfig, and the assert below then applies to it.
inline constexpr PhyConfig kPhy = {
    /* freq_hz            */ 917400000u,
    /* bw_khz10           */ 1250,
    /* sf                 */ 9,
    /* cr_denom           */ 5,
    /* conducted_dbm      */ -4,
    /* antenna_gain_dbi10 */ 30,
    /* sync_word          */ 0x12,
    /* preamble_symbols   */ 8,
};

// D33 / spec 18.2 - at or below -1 dBm EIRP. Integer tenths of a dB, so a rounding
// direction cannot put a half-dB above the ceiling. A change to either term that breaks
// the ceiling is a compile error here, not a finding on a spectrum analyser.
inline constexpr int kEirpCeilingDbm10 = -10;

constexpr bool within_eirp_ceiling(const PhyConfig& phy) {
  return phy.conducted_dbm * 10 + phy.antenna_gain_dbi10 <= kEirpCeilingDbm10;
}

static_assert(within_eirp_ceiling(kPhy),
              "D33 - conducted power plus antenna gain exceeds the EIRP ceiling");

}  // namespace link
}  // namespace lran
