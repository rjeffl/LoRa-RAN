// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R6 - the responder's own record of what IT received.
//
// WHY THIS EXISTS, given the echo already carries resp_rssi/resp_snr back to the
// initiator: those only arrive when the echo does. A probe the responder HEARD whose
// ECHO was lost is invisible to the initiator - it sees a missing echo and cannot
// tell which leg failed. That is the direction R4 deliberately gives up when it makes
// round-trip PER the primary metric.
//
// Comparing three numbers separates them completely:
//
//   probes_sent   (initiator)  vs  probes_heard (responder)  -> DOWNLINK loss
//   probes_heard  (responder)  vs  echoes_recv  (initiator)  -> UPLINK loss
//
// Task R6: "The responder has no SD card. If retaining its local log matters, a small
// NVS ring of per-position summaries dumped over serial on reconnect is enough - do
// not build anything larger." This is that ring, and it is deliberately per-position
// rather than per-test-point.
//
// Arduino-free. The NVS blob is produced and consumed here so the persistence layer
// is a dumb byte store and the serialization is host tested - which is the part that
// can be silently wrong.

#pragma once

#include <cstddef>
#include <cstdint>

#include "sentinels.h"
#include "sweep.h"

namespace rangetest {

struct PositionSummary {
  uint16_t position_id  = 0;
  uint16_t probes_heard = 0;   // probes that reached us, echoed or not
  uint16_t echoes_sent  = 0;   // echoes we transmitted - not that they arrived

  Series rssi_dbm10;
  Series snr_db10;

  bool used = false;
};

// Sixteen positions. A walk out and back on one bearing is a handful of stops; this
// is "small" per R6 and still more than a bearing needs.
inline constexpr size_t kPositionLogCapacity = 16;

class PositionLog {
 public:
  // Folds one heard probe into `position`'s summary, opening a slot if needed.
  void record_probe(uint16_t position, int16_t rssi_dbm10, int16_t snr_db10);

  // A separate call because a probe can be heard and its echo still fail to transmit.
  void record_echo(uint16_t position);

  size_t count() const { return count_; }
  const PositionSummary* at(size_t i) const;
  const PositionSummary* find(uint16_t position) const;

  // True once a position has been displaced by the ring wrapping. Reported rather
  // than hidden: a dumped log missing its first positions should say so.
  bool overflowed() const { return overflowed_; }

  void clear();

  // ---- persistence ------------------------------------------------------
  //
  // A flat little-endian blob, field by field (repo rule 1). Never memcpy the struct:
  // this is read back by host tooling and the layout must not depend on a compiler.

  static constexpr size_t kBlobEntryLen = 24;
  static constexpr size_t kBlobHeaderLen = 4;
  static constexpr size_t kBlobMaxLen =
      kBlobHeaderLen + kPositionLogCapacity * kBlobEntryLen;

  // Returns bytes written, or 0 if `cap` is too small.
  size_t serialize(uint8_t* out, size_t cap) const;

  // Replaces the log's contents. Returns false on a bad magic, version or length, in
  // which case the log is left cleared rather than half-loaded.
  bool deserialize(const uint8_t* in, size_t len);

 private:
  PositionSummary* slot_for(uint16_t position);

  PositionSummary entries_[kPositionLogCapacity]{};
  size_t          count_      = 0;
  size_t          next_       = 0;      // ring write cursor
  bool            overflowed_ = false;
};

}  // namespace rangetest
