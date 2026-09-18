// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Every frame in and out, one record each: source, type, schema, RSSI, SNR and discard
// reason (Impl Plan 6.6, BF-27). The last instrument on the receive path's 1 s knee
// (engineering log, 2026-09-17).
//
// ARDUINO-FREE AND HOST-TESTED, for the reason rx_wake.h and rx_deaf.h give: the ring,
// the overwrite accounting and the rendering are where a lost record becomes visible or
// stays invisible, and none of them need a radio. lora_link.cpp supplies the bytes.
//
// WHAT THIS ANSWERS THAT A COUNTER CANNOT. Every earlier instrument on the knee was a
// total: rx_no_interrupt over a burst, rx_deaf_ms over a window, cad_backoffs against
// frames lost. Both candidates they were built for came back clean, and a total cannot
// say anything further, because the question left is about POSITION - which frames go
// missing, and what the bridge was doing when they did. Flood frames carry an
// incrementing status `seq` (simnode fault.cpp), so a record per arrival turns the gaps
// into a list: scattered points put the loss at the chip or in the air, a cluster
// following a bridge action puts it in this firmware.
//
// READ `deaf_ms` AS A DIFFERENCE, NOT A VALUE. It is the running rx_deaf_ms (rx_deaf.h)
// as it stood when the record was made, so the deafness BETWEEN two records is the
// difference of their fields. That is the quantitative form of "clustered after a bridge
// action", and it is the form to use: the previous session's transmit-path result came
// from a constancy argument and not from a correlation, which is the shape of evidence
// this log should be read for too.
//
// W11 - GROUP BY TYPE BEFORE READING A GAP AS A LOSS. A `PING` responder echoes the
// initiator's `seq` (spec 6.6), so a node's echo carries a value from the BRIDGE's
// sequence space and lands in the middle of that node's status numbering. `type` is
// recorded for exactly this reason; a gap analysis that ignores it invents losses.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "lran/types.h"

namespace bridge {

// Which way the frame went. `Tx` is recorded where the bytes are handed to the radio,
// not where they are queued: the log is a timeline of what the RADIO did, and the gap
// between queueing and transmitting is the scheduler's, already visible elsewhere.
enum class FrameDir : uint8_t { Rx = 0, Tx = 1 };

// How the radio delivered this reception, or failed to.
//
// A DIFFERENT QUESTION FROM RxPass, WHICH IS WHY IT IS A DIFFERENT ENUMERATION.
// rx_wake.h's RxPass answers what the IRQ register held; this answers what became of
// the frame between the register and the decoder, which includes two outcomes the
// register cannot express - a completed reception whose PHY CRC failed, and a readData()
// that returned an error. The first three values carry RxPass's meaning across.
//
// spec 14 STAGE 1 IS HERE AND NOT IN `status`, DELIBERATELY. lran::Status has no value
// for a PHY CRC error and must not gain one: rx_ladder.h keeps stage 1 apart from
// Status::BadCrc because the two lead to opposite conclusions about RF versus software,
// and folding them together during bring-up produces a confident and wrong verdict.
enum class RxOutcome : uint8_t {
  Packet      = 0,  // RX_DONE, announced by its own interrupt
  Orphan      = 1,  // RX_DONE found by the timed read - no interrupt ever arrived
  HeaderError = 2,  // spec 14 stage 1, the header half
  PhyCrc      = 3,  // spec 14 stage 1, the payload half - the reception completed, badly
  DriverError = 4,  // readData() refused; rx_driver_errors counts it
  Transmitted = 5,  // not a reception at all: the record is a Tx
};

// `status` when no ladder verdict exists, because the frame never reached the ladder.
// OUTSIDE lran::Status's RANGE ON PURPOSE (root rule 6's reasoning, one layer up): the
// natural alternative is 0, which is Status::Ok, which in this record means DELIVERED.
// A marker that cannot be read as a real verdict is the whole point of a sentinel.
inline constexpr uint8_t kStatusNotRun = 0xFF;

// ---------------------------------------------------------------------------
// One frame.
//
// 24 bytes, and the size matters: kFrameLogSlots of these is static RAM on a device
// that has already spent ~28 KB on the publish queue.
//
// NO RADIO METADATA IS SPELT `rssi_dbm == INT16_MIN` (root rule 6), and `snr_db` is
// meaningless whenever it reads so. Two cases produce it, and both are real: a `Tx`
// record, which has no reception to measure, and a header error, where getRSSI() would
// answer for the LAST packet rather than for this failure - the same trap
// getPacketLength() sets and that lora_link.cpp already refuses to walk into.
// ---------------------------------------------------------------------------
struct FrameLogEntry {
  // Monotonic across the life of the log, assigned when the record is made. A GAP IN
  // THIS FIELD IS THE ONLY HONEST REPORT OF AN OVERWRITE: `lost` counts them, but only
  // the index says WHERE they fell, and a knee investigation is about position.
  //
  // It is also what makes the reader safe against a torn slot - see read_next().
  uint32_t index = 0;

  uint32_t ms      = 0;  // millis() at the radio read, or at startTransmit
  uint32_t deaf_ms = 0;  // running rx_deaf_ms at `ms` (rx_deaf.h); read as a difference

  lran::Seq seq      = 0;
  int16_t   rssi_dbm = INT16_MIN;

  lran::NodeId peer = 0;  // `src` for Rx, `dst` for Tx

  // Raw wire bytes, not decoded enums: a frame refused at stage 6 has a `type` this
  // build has no name for, and that byte is the whole diagnosis.
  uint8_t type   = 0;
  uint8_t schema = 0;
  uint8_t frag   = 0;  // spec 5.6 - high nibble index, low nibble total

  // lran::Status: Ok means the payload was delivered, or - on a Tx record - that the
  // bytes went to the radio. kStatusNotRun means the ladder never saw the frame, which
  // `rx` explains.
  uint8_t status = 0;
  int8_t  snr_db = 0;
  uint8_t dir    = static_cast<uint8_t>(FrameDir::Rx);

  // RxOutcome. Carries "no interrupt ever arrived for this one" per frame rather than
  // only as a total, and says whether `status` holds a verdict at all.
  uint8_t rx = static_cast<uint8_t>(RxOutcome::Packet);

  bool has_radio_meta() const { return rssi_dbm != INT16_MIN; }
};

// 64 x 24 B = 1.5 KB. A `--gap 250` burst produces four records a second and log_task
// drains far faster than that, so the ring is sized for a STALL rather than for the
// rate: it holds sixteen seconds of that burst if the drain stops entirely.
inline constexpr size_t kFrameLogSlots = 64;

// ---------------------------------------------------------------------------
// A single-producer, single-consumer ring that OVERWRITES ITS OLDEST RECORD.
//
// WHY OVERWRITE, WHEN queues.h ARGUES FOR DropNewest. That reasoning is about events
// which drive email and SMS, where the newest arrival is not interchangeable with one
// already queued. This is a diagnostic timeline with one reader, a human looking at a
// burst that has already finished, and the passage that matters is the one around the
// moment the bridge got busy - which is exactly the passage a DropNewest ring throws
// away, because it stops recording at the instant the load arrives. Keeping the most
// recent window is the opposite failure and the useful one.
//
// IT IS COUNTED EITHER WAY, which is what root rule 4 is really asking for: `lost`
// rises by one per overwritten record and every surviving record carries its index.
//
// LOCK-FREE BECAUSE lora_task MUST NOT BLOCK (Impl Plan 5.2, and
// tools/checks/lora_task_never_blocks.py enforces it). record() is a handful of stores
// and one release; it takes no mutex, allocates nothing and never waits.
//
// A TORN READ IS DETECTED, NOT PREVENTED. The producer may overwrite the slot the
// consumer is copying, and no amount of index arithmetic stops that on two cores. The
// consumer copies the slot and then checks the index it copied: a slot that was
// overwritten mid-copy no longer carries the index the consumer asked for, so the copy
// is discarded and the read resynchronises to what is actually still in the ring. This
// is why `index` lives in the record rather than being implied by the slot.
// ---------------------------------------------------------------------------
class FrameLog {
 public:
  // Producer side. Called from lora_task only. `e.index` is assigned here and whatever
  // the caller put there is ignored.
  void record(FrameLogEntry e);

  // Consumer side. Fills *out with the oldest record the ring still holds and advances.
  // False when the log is empty.
  //
  // A record the producer overwrote before this ran is NOT returned and never will be;
  // `lost()` counts it and the index gap locates it.
  bool read_next(FrameLogEntry* out);

  // Records overwritten before a reader took them. Cumulative, saturating.
  uint32_t lost() const { return lost_.load(std::memory_order_relaxed); }

  // Records ever made. The index the next record() will carry.
  uint32_t recorded() const { return write_.load(std::memory_order_relaxed); }

 private:
  FrameLogEntry         slots_[kFrameLogSlots];
  std::atomic<uint32_t> write_{0};  // next index to write; producer only
  std::atomic<uint32_t> read_{0};   // next index to read; consumer only
  std::atomic<uint32_t> lost_{0};
};

// ---------------------------------------------------------------------------
// Rendering.
// ---------------------------------------------------------------------------

// One record as a serial line, NUL-terminated. Returns the length written, or 0 when it
// would not fit - refused, never truncated, like every other size limit in this
// firmware. A truncated log line is read as a frame that arrived differently.
size_t render_line(const FrameLogEntry& e, char* out, size_t cap);

// A batch as one JSON object for `lran/bridge/diag/rxlog/state`:
//
//   {"lost":0,"f":[{"i":12,"ms":123456,"deaf":659,"d":"rx","peer":240,"type":3,
//                   "schema":1,"frag":1,"seq":77,"rssi":-42,"snr":9,"st":0,"rx":0}]}
//
// Keys are short because kMaxPayloadLen is 768 and a record is worth more than its
// key names; the reader is tools/simctl/rxlog.py, not a person scrolling a broker.
// `rssi` and `snr` are `null` together when the record carries no radio metadata -
// spec 16.2.1's rule, which is about JSON and so applies to any payload the bridge
// writes, not only to the two §16.2.1 defines.
//
// Returns the length written and sets *consumed to how many of `n` fit. Zero means not
// even one record fits, which cannot happen for cap >= 160 and is a caller error.
size_t render_batch(const FrameLogEntry* entries, size_t n, uint32_t lost, char* out,
                    size_t cap, size_t* consumed);

}  // namespace bridge
