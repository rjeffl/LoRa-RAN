// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The node's side of the MPPT's UART: the text cache spec 7.2.2 is built from, the staleness
// of spec 7.2.6 bit 1, and one HEX transaction at a time. GL4; GateLink Impl Plan 4.2.4 and
// 5.2's vedirect_task row; PRD R-3.3d-R-3.3f.
//
// ARDUINO-FREE, so the native suite can drive it. vedirect_task owns one, feeds it every byte
// Serial1 receives, and is its only caller. The UART is reached through VedWriter.
//
// THE ENGINE HAS ALREADY AUTHENTICATED THE REQUEST. A write-class HEX_REQ reaches start()
// only after spec 9.4's steps 2-6 have passed (lran-node's on_hex_req). This class sends the
// string verbatim and inspects only its command nibble, to know what answers it.
//
// hex_timeout_ms IS THE WHOLE WAIT, retry included. lran-config's row stops it at 2000 so
// the node's TIMEOUT reaches the bridge before hex_rsp_timeout_ms (3000) runs out. A Get
// unanswered by half that wait is sent once more, because the 75/15 answers its first HEX
// contact with a burst of Async frames and drops the Get sent into it (engineering log,
// 2026-10-08). Only a Get is retried: it reads, so a second copy changes nothing.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/types.h"
#include "vedirect/hex.h"
#include "vedirect/text.h"

namespace gatelink {

// lran-config rows 0x1030 and 0x1031, in milliseconds. The Store supplies them once CONFIG
// is answered; until then they are the table's defaults.
struct VedParams {
  uint32_t hex_timeout_ms = 1000;
  uint32_t stale_ms       = 5000;
};

// spec 7.2.6.
inline constexpr uint8_t kMpptFlagLoadOn      = 0x01;
inline constexpr uint8_t kMpptFlagStale       = 0x02;
inline constexpr uint8_t kMpptFlagHexPending  = 0x04;

// spec 7.2.2 has no sentinel for its uint8 code fields. A label missing from a block that
// passed its checksum reads as this, which no Victron code uses; the handoff carries the
// question for the specification.
inline constexpr uint8_t kCodeNotAvailable = 0xFF;

// The MPPT block of spec 7.2.2, in its units. Sentinels until a block arrives (root rule 6).
// A stale snapshot keeps the last block's values: bit 1 says they are old, and the bridge
// marks the entities unavailable from it.
struct MpptSnapshot {
  uint16_t batt_mv       = lran::kU16NotAvailable;
  int16_t  batt_ma       = lran::kI16NotAvailable;
  uint16_t pv_cv         = lran::kU16NotAvailable;
  uint16_t pv_w          = lran::kU16NotAvailable;
  int16_t  load_ma       = lran::kI16NotAvailable;
  uint16_t yield_today   = lran::kU16NotAvailable;
  uint16_t yield_yest    = lran::kU16NotAvailable;
  uint16_t pmax_today    = lran::kU16NotAvailable;
  uint32_t yield_total   = lran::kU32NotAvailable;
  uint8_t  charge_state  = kCodeNotAvailable;
  uint8_t  mppt_err      = kCodeNotAvailable;
  uint8_t  mppt_tracker  = kCodeNotAvailable;
  uint8_t  mppt_flags    = kMpptFlagStale;
  int16_t  mppt_temp_c10 = lran::kI16NotAvailable;  // the 75/15's text block has no temperature
};

// Scales one decoded block into spec 7.2.2's fields. A value its field cannot hold reads as
// the sentinel, not a clipped number. The flags are left to the caller.
MpptSnapshot mppt_from_text(const vedirect::MpptText& m);

// A HEX_REQ's string, handed from lora_task. `token` matches the answer to it.
struct HexJob {
  uint32_t token = 0;
  char     hex[vedirect::kMaxChars + 1] = {0};
  size_t   n     = 0;
};

struct HexResult {
  uint32_t         token  = 0;
  lran::HexStatus  status = lran::HexStatus::Ok;
  char             hex[vedirect::kMaxChars + 1] = {0};
  size_t           n      = 0;
};

class VedWriter {
 public:
  virtual ~VedWriter() = default;
  // The request and its newline. False when the UART took fewer bytes.
  virtual bool write_line(const char* s, size_t n) = 0;
};

// Everything the UART delivered that was not a decoded block or a matched reply (root
// rule 4). The text parser keeps its own counters.
struct VedCounters {
  uint32_t blocks_unparsed = 0;  // known labels whose value did not parse
  uint32_t hex_bad         = 0;  // HEX lines that failed decode()
  uint32_t hex_async       = 0;  // Async frames, which the MPPT sends unasked
  uint32_t hex_unmatched   = 0;  // replies that answered no outstanding request
  uint32_t hex_sent        = 0;
  uint32_t hex_retries     = 0;
  uint32_t hex_answered    = 0;
  uint32_t hex_timeouts    = 0;
  uint32_t hex_uart_errors = 0;  // a request the UART did not take whole
};

class VedLink {
 public:
  explicit VedLink(VedWriter* out) : out_(out) {}

  void set_params(const VedParams& p) { params_ = p; }
  const VedParams& params() const { return params_; }

  void feed(uint8_t b, uint32_t now_ms);

  // Sends `job`. False when a transaction is outstanding, which the caller answers BUSY. A
  // job too long for the UART completes at once as MALFORMED_REQUEST.
  bool start(const HexJob& job, uint32_t now_ms);

  // The retry and the TIMEOUT. Call often.
  void tick(uint32_t now_ms);

  bool busy() const { return active_; }

  // The answer to the last job, once. False while none is waiting.
  bool take_result(HexResult* out);

  // spec 7.2.2 and 7.2.6 at `now_ms`.
  MpptSnapshot mppt(uint32_t now_ms) const;

  const vedirect::TextParser& parser() const { return text_; }
  const VedCounters&          counters() const { return counters_; }
  // When the last block arrived; meaningful only when has_block().
  bool     has_block() const { return have_block_; }
  uint32_t last_block_ms() const { return last_block_ms_; }

 private:
  void on_hex_line();
  bool answers(const vedirect::Frame& f) const;
  void finish(lran::HexStatus status, const char* hex, size_t n);
  bool send(uint32_t now_ms);

  VedWriter*           out_;
  VedParams            params_;
  vedirect::TextParser text_;
  VedCounters          counters_;

  MpptSnapshot last_;  // flags clear; mppt() adds them
  bool         load_on_       = false;
  bool         have_block_    = false;
  uint32_t     last_block_ms_ = 0;

  // The outstanding transaction.
  bool     active_   = false;
  HexJob   job_;
  uint8_t  req_cmd_  = 0;     // the command nibble
  bool     req_reg_known_ = false;  // a Get or Set that decoded, so its register is known
  uint16_t req_reg_  = 0;
  bool     retryable_ = false;
  bool     retried_  = false;
  uint32_t start_ms_ = 0;

  bool      result_ready_ = false;
  HexResult result_;
};

}  // namespace gatelink
