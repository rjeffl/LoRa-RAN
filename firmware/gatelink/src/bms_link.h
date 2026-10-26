// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// bms_task's logic that needs no radio: the STATUS frame's BMS block from a decoded read,
// the LoRa/BLE interlock's decisions, and each window's line in the card's log. GL5; GateLink Impl Plan 5.2; spec 7.2.3, 7.2.7;
// PRD R-3.4a-R-3.4g, R-4.3h.
//
// ARDUINO-FREE, so the native suite drives it. bms_client.cpp runs the BLE window and
// task_runtime.cpp holds the lock; both call into this file for the decisions.

#pragma once

#include <cstddef>
#include <cstdint>

#include "bms_ble/bms_data.h"
#include "lran/schema/gatelink_status_v1.h"

namespace gatelink {

// The last successful read, as bms_task publishes it. `have` stays false until the first
// read succeeds, and the block carries its sentinels until then.
struct BmsSnapshot {
  bool          have    = false;
  bms::BmsData  data    = {};
  int16_t       rssi_dbm = 0;  // the connection's RSSI at the read; 0 = not measured
  uint32_t      read_ms = 0;   // when the read completed
};

// Fills spec 7.2.3's block and 7.2.7's flags into `s`.
//
// What the pack does not tell us is sent as unavailable, not as zero (root rule 6):
// bms_alarms, because 0x8D is not decoded (bms-protocol 9), and flag bits 1-5 for the same
// reason. pack_ma is clamped to int16, and a cell or sensor the pack did not report carries
// UINT16_MAX or INT8_MIN.
void fill_bms_block(const BmsSnapshot& b, uint32_t now_ms, lran::schema::GateLinkStatusV1* s);

// True for a frame the bridge is waiting on: COMMAND_ACK, CONFIG_ACK, HEX_RSP, or a STATUS
// answering a POLL (Impl Plan 5.2, decided 2026-10-07). Such a frame queued during a BLE
// window cuts the window short. An unsolicited STATUS or EVENT waits for the window to end.
//
// A STATUS in schema 0x10 answers a POLL when its status_reason (payload offset 77) is
// POLL_RESPONSE. Any other STATUS schema is a health answer, which is sent only to a POLL.
bool reply_awaited(const uint8_t* frame, size_t len);

// What lora_task does with a frame whose media access has not started, decided from the
// interlock's state.
enum class TxGate : uint8_t {
  Go,        // the interlock is free; take it and start media access
  Wait,      // bms_task holds it; try again next pass
  WaitAbort, // as Wait, and ask bms_task to end its window now
  Overrun,   // the window has outlived bms_window_max_ms; transmit without the interlock
};

// `ble_holds` is whether bms_task holds the interlock, and `window_ms` how long it has.
// The cap is the bound for a NimBLE stack that hangs inside a call bms_task cannot leave
// (Impl Plan 5.2): past it the radio stops waiting, and R-4.3h's exclusion is given up for
// that one frame rather than held until the watchdog fires.
TxGate tx_gate(bool ble_holds, uint32_t window_ms, uint32_t cap_ms, bool awaited);

// Why a BLE window ended. Each is counted on the console's `bms` line, and every window
// that did not read the pack names one (root rule 4).
enum class BmsEnd : uint8_t {
  Read,          // the 0x8C answer decoded
  Aborted,       // a reply the bridge waits on was queued (Impl Plan 5.2)
  Cap,           // bms_window_max_ms passed
  NotFound,      // the scan ended without the pack's advertisement
  ConnectFailed, // no connection, or the FFF0 layout was incomplete
  Handshake,     // FFFA did not read back 0x01 (bms-protocol 3)
  Subscribe,     // the FFF1 CCCD write failed
  Request,       // the 0x8C write failed
  NoAnswer,      // no decodable 0x8C before the answer timeout
  InitFailed,    // NimBLE did not start
  Count,
};

const char* bms_end_name(BmsEnd e);

// Whether the next window is a retry, bms_retry_s away rather than bms_poll_s. A window a
// reply aborted gets one: bms_task waits bms_poll_s after a window ends, so the window's
// phase walks past the bridge's 60 s poll about every 3.6 h, and a whole period's wait
// after that abort let age_s reach the bridge's bms_stale_s (log, 2026-10-26). A retry
// that is aborted too waits the full period, so a burst of replies cannot hold BLE busy.
bool bms_retry_next(BmsEnd end, bool was_retry);

// How long each step took, in ms. A step that did not run reads 0.
struct BmsTiming {
  uint32_t init_ms      = 0;
  uint32_t scan_ms      = 0;
  uint32_t connect_ms   = 0;
  uint32_t handshake_ms = 0;  // HiLink, read-back and the FFF1 subscription
  uint32_t answer_ms    = 0;  // the 0x8C request to its decoded answer
  uint32_t teardown_ms  = 0;  // disconnect and controller de-init
};

// What the notification path saw over the life of the image (root rule 4).
struct BmsRxCounters {
  uint32_t frames     = 0;  // complete 0x8C frames that decoded
  uint32_t crc_errors = 0;
  uint32_t bad_term   = 0;
  uint32_t undecoded  = 0;  // complete frames that were not a good 0x8C
  uint32_t discarded  = 0;  // bytes the reassembler dropped while resyncing
};

// One window's line in the card's /bms.log, without the newline. Each window appends one,
// read or not, so a soak with USB unplugged keeps why each poll ended, which the console's
// `bms` line shows only to an attached host (engineering log, 2026-10-25). The rx counters
// are cumulative, so a line whose crc or undecoded count rose says the window saw a bad
// frame. Returns the length, or 0 if `cap` is too small.
size_t format_bms_log_line(char* out, size_t cap, uint32_t uptime_s, BmsEnd end,
                           uint32_t window_ms, const BmsTiming& t, bool abort_asked,
                           const BmsRxCounters& k);

}  // namespace gatelink
