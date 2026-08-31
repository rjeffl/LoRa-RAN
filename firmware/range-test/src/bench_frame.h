// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// R4 - the bench frame.
//
// NOT an LRAN frame, deliberately. Task R4: the sweep uses raw RadioLib frames
// because it needs a header carrying a position ID, a test-point index and the
// RESPONDER'S OWN measured RSSI, none of which fit `PING` - whose responder echoes
// the payload unchanged (spec 6.6). The sweep measures the RADIO LINK; W9 (R9)
// measures the PROTOCOL. Keeping them apart avoids inventing a schema for a bench
// tool and keeps spec 6.6 clean.
//
// This file therefore includes nothing from /lib/lran-protocol/ and must not start.
// Branches 2 and 3 touch no shared codec; only R9 links it.
//
// Repo rule 1 still applies and is not negotiable here: serialize field by field,
// explicitly, little-endian. Never memcpy a struct to or from the wire. The CSV this
// produces will be parsed by host tooling built with a different compiler, which is
// precisely the case the rule exists for - and a bench tool is where the temptation
// to skip it is strongest.

#pragma once

#include <cstddef>
#include <cstdint>

#include "sentinels.h"

namespace rangetest {

// Distinguishes our traffic from anything else the site is running on 915 MHz. The
// sweep counts what it receives, and the D1 notes are explicit that this band has
// occupants - four YoLink sensors on a Semtech SX1276. A foreign frame that happened
// to pass the PHY CRC and got counted as an echo would inflate the link's apparent
// quality, which is the one direction of error that matters here.
inline constexpr uint16_t kBenchMagic   = 0x4C52;  // 'LR'
inline constexpr uint8_t  kBenchVersion = 2;   // v2 adds resp_heard

enum class BenchKind : uint8_t {
  Probe = 1,  // initiator -> responder
  Echo  = 2,  // responder -> initiator, carrying the responder's own measurement

  // A probe sent while the responder is still finding this radio configuration
  // (sweep_warmup_probes). Echoed exactly like a Probe, and counted by NEITHER end.
  //
  // It needs to be distinguishable on the wire, not just locally. The initiator
  // already excludes its own warmup probes; without this the responder would still
  // count them, and `resp_heard` would exceed `probes_sent` - which is precisely the
  // comparison that column exists to support. Observed on the bench as
  // resp_heard=12 against sent=8.
  //
  // Carried in `kind` rather than a new flags byte so the frame layout is unchanged.
  WarmupProbe = 3,
};

// True for anything the responder should echo.
constexpr bool bench_is_probe(BenchKind k) {
  return k == BenchKind::Probe || k == BenchKind::WarmupProbe;
}

// True for anything either end should count. Warmup is measurement scaffolding.
constexpr bool bench_is_counted(BenchKind k) { return k == BenchKind::Probe; }

// 2 magic + 1 version + 1 kind + 2 position + 2 tp_index + 2 seq + 2 rssi + 2 snr
// + 2 resp_heard.
inline constexpr size_t kBenchHeaderLen = 16;

// Largest bench payload the sweep will ask for, and therefore the size of every
// buffer that holds one. Fixed and caller-owned (repo rule 3 - no dynamic
// allocation).
//
// 222 is LRAN_MAX_FRAME and the ceiling the SX1262 path is sized for everywhere else
// in this repo, so the bench buffers match it even though the default plan tops out
// at 64. R9's full-size PING is a separate path against the real codec, but a bench
// buffer that could not hold one would be an odd place to economise.
inline constexpr size_t kMaxBenchPayload = 222;

struct BenchFrame {
  BenchKind kind        = BenchKind::Probe;
  uint16_t  position_id = 0;
  uint16_t  tp_index    = 0;
  uint16_t  probe_seq   = 0;

  // The RESPONDER's measurement of the probe it is echoing, in tenths. Present only
  // in an Echo; kI16NotAvailable in a Probe.
  //
  // This is what recovers the direction R4 gives up. Round-trip PER conflates the two
  // legs by design, and these two fields are how the initiator's CSV still knows what
  // the downlink looked like, without waiting for the responder's local log.
  int16_t resp_rssi_dbm10 = kI16NotAvailable;
  int16_t resp_snr_db10   = kI16NotAvailable;

  // How many probes the RESPONDER has heard at this test point, echoed or not.
  //
  // Added in v2, and it is what makes the initiator's CSV self-sufficient. Without it
  // a missing echo is ambiguous - the probe may never have arrived (downlink) or the
  // echo may have been lost on the way back (uplink) - and resolving that would mean
  // recovering the responder's NVS log and merging it afterwards. Carrying the count
  // in the echo answers it in the row itself, for every echo that does arrive.
  //
  // kU16NotAvailable in a Probe.
  uint16_t resp_heard = kU16NotAvailable;
};

// Writes header + filler to exactly `total_len` bytes. Returns bytes written, or 0 if
// `total_len` is below kBenchHeaderLen or above `cap`.
//
// The filler is the spec 6.6.3 pattern, `(seq + i) & 0xFF`, reused rather than
// reinvented: it costs nothing, it makes payload size a swept parameter with real
// bytes behind it instead of zeros, and it localizes a corruption to an offset.
size_t bench_serialize(const BenchFrame& f, uint8_t* out, size_t cap, size_t total_len);

// Parses a received frame. Returns false unless the magic and version match and the
// length is at least a header - which is what keeps foreign 915 MHz traffic out of
// the statistics.
bool bench_parse(const uint8_t* in, size_t len, BenchFrame* out);

// Verifies the filler against the pattern for `probe_seq`. On mismatch, `*first_bad`
// (when non-null) receives the offset of the first diverging byte.
bool bench_check_filler(const uint8_t* in, size_t len, uint16_t probe_seq,
                        size_t* first_bad);

}  // namespace rangetest
