// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Low-temperature charge inhibition, made observable. GL5; PRD 6.4, R-6.4a and R-6.4b;
// spec 7.2.6 bit 3, 8.7 and 8.9.
//
// ARDUINO-FREE, so the native suite drives it. app_task owns one, feeds it the MPPT and BMS
// snapshots each pass, and queues the pushes it asks for.
//
// TWO PATHS, AND THE BMS ONE WINS WHEN IT HAS A FRESH READ.
//   BMS (R-6.4a)        the coldest pack sensor at or below kChargeInhibitTempC, and the
//                       pack not charging. The pack's own inhibit flag is in 0x8D, which is
//                       not decoded (bms-protocol 10), so temperature stands in for it.
//   VE.Direct (R-6.4b)  PV power present, the charger in bulk or absorption, battery current
//                       within kChargeInhibitBandMa of zero and battery voltage not rising,
//                       all held for charge_inhibit_confirm_s. Used only while the BMS has no
//                       read younger than three polls, the case R-6.4b was written for.
//
// Either path sets spec 7.2.6 bit 3, the derived indication. spec 7.2.7 bit 3 is the BMS's
// own flag and stays clear until 0x8D is decoded: a temperature is not the BMS's word.
//
// The VE.Direct inference clears the way it sets, after the condition has failed for
// charge_inhibit_confirm_s. A cloud that drops PV for a minute does not flap the flag.

#pragma once

#include <cstdint>

#include "bms_link.h"
#include "detect.h"
#include "ved_link.h"

namespace gatelink {

// spec 7.2.6.
inline constexpr uint8_t kMpptFlagChargeInhibited = 0x08;

// TODO(R-5.3a): these three are constants until the bridge can carry more GateLink rows.
// Any new row pushes GateLink's config/state past the bridge's kMaxPayloadLen, and raising
// that overflows the bridge's DRAM (engineering log, 2026-10-14). R-5.3a wants each one a
// parameter; until then a change here is a USB reflash at the gate.
//
// PRD 6.4 says "approximately 0 C"; the pack's real cut-off is not measured.
inline constexpr int16_t  kChargeInhibitTempC  = 0;
// One count of the pack's 0.1 A current.
inline constexpr uint32_t kChargeInhibitBandMa = 100;
inline constexpr uint32_t kChargeInhibitRiseMv = 20;

// lran-config 0x1041 and 0x1040, and the thresholds above. The tests vary them all.
struct ChargeParams {
  uint32_t confirm_ms = 300000;                // 0x1041 charge_inhibit_confirm_s
  int16_t  temp_c     = kChargeInhibitTempC;
  uint32_t band_ma    = kChargeInhibitBandMa;
  uint32_t rise_mv    = kChargeInhibitRiseMv;
  uint32_t bms_poll_s = 300;                   // 0x1040 bms_poll_s
};

enum class ChargeSource : uint8_t { None, Bms, VeDirect };

const char* charge_source_name(ChargeSource s);

class ChargeInhibit {
 public:
  // One pass. Returns true when the indication changed, and then adds a STATUS with reason
  // CHARGE_INHIBITED to `out`, and an EVENT as well when it set (spec 8.9: its severity is
  // Low, and it is the onset that HA logs). `ids` draws the event's id.
  bool update(const MpptSnapshot& m, const BmsSnapshot& b, uint32_t now_ms,
              const ChargeParams& p, const GateContext& g, EventIds* ids, PushList* out);

  bool         inhibited() const { return inhibited_; }
  ChargeSource source() const { return source_; }
  // Which path decided the last pass, inhibited or not.
  ChargeSource path() const { return path_; }

 private:
  bool ved_update(const MpptSnapshot& m, uint32_t now_ms, const ChargeParams& p);

  bool         inhibited_ = false;
  ChargeSource source_    = ChargeSource::None;
  ChargeSource path_      = ChargeSource::None;

  // The VE.Direct inference: whether it holds, and since when its condition last changed.
  bool     ved_inferred_ = false;
  bool     ved_met_      = false;
  uint32_t ved_since_ms_ = 0;
  uint16_t ved_ref_mv_   = 0;  // the lowest battery voltage while the condition held
};

}  // namespace gatelink
