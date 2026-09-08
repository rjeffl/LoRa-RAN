// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// R9 - the W9 bench runs. Spec 6.6, 6.6.1, 6.6.2, 6.6.3, 11.
//
// THE FIRST FILE IN THIS FIRMWARE THAT LINKS /lib/lran-protocol/. Branches 2 and 3
// deliberately did not: the sweep measures the RADIO LINK with its own raw frame
// (bench_frame.h), and W9 measures the PROTOCOL with the real codec and real `PING`
// frames. Two runs, both over RF, both against the shipped library:
//
//   1. spec 6.6.1 - a 202-byte echo, which is a frame of exactly 222 bytes,
//      LRAN_MAX_FRAME. The largest frame the system can ever build, unfragmented.
//   2. spec 6.6.2 - the same 202-byte echo with frag_chunk = 14, which is the full
//      15-fragment set and the only mechanism in the protocol that exercises
//      reassembly over the air.
//
// Arduino-free on purpose, like phy_params and airtime: every decision in here is
// arithmetic over the codec, and the native suite is where a fragmentation fault
// should be found rather than at the far end of a walk. Repo rule 7's reasoning
// applied one level up.
//
// GUARDRAIL 6 HOLDS: nothing here reaches into /lib/lran-protocol/. It is consumed
// through its public headers exactly as node firmware will consume it, which is a
// second thing this run is worth - R9 is the first time that API is driven by
// something that is not its own test suite.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/codec.h"
#include "lran/config.h"
#include "lran/frame.h"
#include "lran/messages.h"
#include "lran/types.h"

namespace rangetest {

// spec 5.3 - the two boards take BENCH node ids, not production ones. A W9 frame
// that escaped onto a live network must be recognizable as bench traffic and never
// mistakable for the bridge or for GateLink (spec 16.6).
inline constexpr lran::NodeId kW9Initiator = lran::kNodeSim0;  // 0xF0
inline constexpr lran::NodeId kW9Responder = lran::kNodeSim1;  // 0xF1

// spec 6.6.1 - `n` at its cap. 2 bytes of PING header + 202 echo = 204 payload,
// + 16 header + 2 CRC = 222 = LRAN_MAX_FRAME exactly.
inline constexpr uint8_t kW9EchoBytes = 202;

// spec 6.6.2 - the chunk that yields the full set. ceil(204 / 14) = 15 fragments,
// which is kMaxFragments. A local sender parameter; it appears nowhere on the wire,
// and a receiver cannot tell this split from a necessary one. That is what makes it
// a valid test rather than a special case the far end colludes in.
inline constexpr size_t  kW9FragChunk       = 14;
inline constexpr uint8_t kW9ExpectFragments = 15;

enum class W9Run : uint8_t {
  MaxFrame   = 0,  // spec 6.6.1
  Fragmented = 1,  // spec 6.6.2
};

const char* to_string(W9Run r);

struct W9Plan {
  W9Run    run          = W9Run::MaxFrame;
  uint8_t  echo_n       = kW9EchoBytes;
  size_t   frag_chunk   = 0;  // 0 = do not fragment
  uint8_t  expect_frags = 1;
  uint16_t pings        = 0;  // PINGs in the run
  uint16_t payload_len  = 0;  // the PING payload, 2 + echo_n
};

// How many PINGs each run sends. Enough to see an intermittent reassembly fault and
// few enough to finish while the operator is standing there: at SF7 a 222-byte frame
// is ~200 ms of airtime and the fragmented run puts 15 frames on the air per PING in
// each direction, so run 2 is the long one by a factor of thirty.
inline constexpr uint16_t kW9PingsPerRun = 32;

W9Plan w9_plan(W9Run run, uint16_t pings = kW9PingsPerRun);

// The header every frame of a W9 exchange shares. `frag` is left at 0x01 and is
// overwritten by encode_fragment for a fragmented set (spec 11.1).
lran::Header w9_header(lran::Seq seq, lran::NodeId src, lran::NodeId dst);

// Builds the PING payload `[ping_flags][n][data:n]` with PATTERN_FILL set, the
// pattern generated from `seq` per spec 6.6.3.
//
// PATTERN_FILL is not optional in this bench. Spec 6.6.3's whole argument is that
// the CRC tells you a frame is corrupt and the pattern tells you WHERE, and "where"
// is the only thing that separates a marginal RF path from a reassembly or
// buffer-indexing bug. Both are live the first time 6.6.2 runs over the air.
lran::Status w9_build_ping(lran::Seq seq, uint8_t n, uint8_t* out, size_t cap,
                           size_t* out_len);

// What an echo turned out to be.
//
// `status` carries the CODEC's verdict and nothing else. A pattern divergence is not
// a decode failure - the frame parsed perfectly and the bytes inside it are wrong -
// and lran::Status is the spec 14 stage enum, so folding a content fault into it
// would put a stage number on something that happened at no stage. The three
// conditions stay separate and ok() is the conjunction.
struct W9EchoCheck {
  lran::Status status     = lran::Status::Ok;
  bool         pattern_ok = false;
  bool         flags_ok   = false;  // PATTERN_FILL preserved, reserved bits clear
  uint8_t      n          = 0;
  size_t       first_bad  = 0;  // byte offset of the first divergence; valid when !pattern_ok

  bool ok() const {
    return status == lran::Status::Ok && pattern_ok && flags_ok;
  }
};

// Checks a reassembled (or single-frame) PING payload against what was sent for
// `seq`. Spec 6.6: the responder preserves `seq`, `ping_flags` and the echo bytes,
// so anything else is a finding.
W9EchoCheck w9_check_echo(lran::Seq seq, uint8_t sent_n, const uint8_t* payload,
                          size_t len);

// The chunk the RESPONDER re-fragments an echo with (spec 6.6.2 - "reassembles,
// verifies, then re-fragments the echo on the way back").
//
// `frag_chunk` is a local sender parameter and appears nowhere on the wire, so the
// responder cannot be told which one the initiator used - it has to infer it. The
// largest fragment payload in the received set IS that chunk: spec 11.1 requires
// every fragment but the last to carry the same length, and the last to be no
// longer. Mirroring it makes the return leg exercise the same split as the outbound
// one, which is what the run is for.
//
// Returns 0 when the set was a single frame, meaning "echo unfragmented".
size_t w9_echo_chunk(uint16_t largest_frag_payload, uint8_t frag_total);

// One run's tally. Every discard is named (repo rule 4): a W9 run that merely
// reports "12 of 32 failed" is not a protocol finding, it is a rumour.
struct W9Stats {
  uint16_t pings_sent      = 0;
  uint16_t frames_sent     = 0;  // fragments included
  uint16_t echoes_ok       = 0;
  uint16_t echo_timeouts   = 0;  // nothing came back inside the window
  uint16_t pattern_faults  = 0;  // bytes came back wrong - spec 6.6.3
  uint16_t decode_faults   = 0;  // header or payload rejected by the codec
  uint16_t reassembly_fails = 0; // set never completed
  uint16_t late_fragments  = 0;  // spec 11.2 - a fragment of an already-completed set
  uint16_t crc_errors      = 0;  // spec 14 stage 1, the PHY CRC
  uint16_t foreign_frames  = 0;  // passed the PHY CRC, was not ours

  // The worst pattern divergence seen, and the seq it was seen on. A single offset
  // is worth more than a count: byte 0 diverging says one thing about the path and
  // byte 168 of a 15-fragment set says a very different one.
  size_t   first_bad_worst = 0;
  lran::Seq first_bad_seq  = 0;
  bool     any_pattern_fault = false;
};

void w9_stats_reset(W9Stats* s);

// True when the run met spec: every PING echoed, every echo byte correct.
bool w9_run_passed(const W9Stats& s);

// spec 15.1 + 12.3 - the airtime check R9 asks for by name.
//
// "check that against the CAD/backoff window while the boards are out, since 12.3's
// defaults were chosen against an empty channel." A 222-byte frame is over a second
// of channel occupancy at SF9, and a backoff window shorter than one frame's airtime
// cannot clear a channel that frame is sitting on.
struct W9AirtimeCheck {
  uint32_t frame_airtime_ms  = 0;
  uint32_t backoff_max_ms    = 0;
  bool     backoff_covers    = false;  // window >= one frame's airtime
};

W9AirtimeCheck w9_airtime_check(uint32_t frame_airtime_ms, uint32_t backoff_max_ms);

// spec 12.3 - `backoff_max_ms`, default 500, alongside `cad_retries` 5. The spec says
// in the same breath that both "were chosen against an empty channel", and spec 15.1's
// own note calls a full-size PING at SF9 "the number to check a CAD/backoff window
// against". Carried here rather than in the library because it is a POLICY default
// this bench is testing, not a wire constant the codec enforces.
inline constexpr uint32_t kW9SpecBackoffMaxMs = 500;

}  // namespace rangetest
