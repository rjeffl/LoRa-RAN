// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL4; see ved_link.h.

#include "ved_link.h"

#include <cstring>

namespace gatelink {
namespace {

using vedirect::HexCmd;
using vedirect::HexRsp;

bool reached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

uint16_t to_u16(uint32_t v) {
  return v == vedirect::kU32NotAvailable || v >= lran::kU16NotAvailable ? lran::kU16NotAvailable
                                                                        : static_cast<uint16_t>(v);
}

int16_t to_i16(int32_t v) {
  return v == vedirect::kI32NotAvailable || v <= INT16_MIN || v > INT16_MAX
             ? lran::kI16NotAvailable
             : static_cast<int16_t>(v);
}

uint8_t to_code(uint16_t v) {
  return v >= kCodeNotAvailable ? kCodeNotAvailable : static_cast<uint8_t>(v);
}

// The command nibble at s[1]. The engine has already checked that it is a hex digit, in
// either case (lran-node's on_hex_req).
uint8_t nibble(char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
  return 0xFF;
}

}  // namespace

MpptSnapshot mppt_from_text(const vedirect::MpptText& m) {
  MpptSnapshot s;
  s.batt_mv      = to_u16(m.batt_mv);
  s.batt_ma      = to_i16(m.batt_ma);
  // spec 7.2.2 - VPV in 10 mV, because mV overflows uint16 at 65.5 V.
  s.pv_cv        = m.pv_mv == vedirect::kU32NotAvailable ? lran::kU16NotAvailable
                                                         : to_u16(m.pv_mv / 10);
  s.pv_w         = to_u16(m.pv_w);
  s.load_ma      = to_i16(m.load_ma);
  // H19-H22 arrive in 0.01 kWh, which is spec 7.2.2's 10 Wh.
  s.yield_today  = to_u16(m.yield_today);
  s.yield_yest   = to_u16(m.yield_yest);
  s.pmax_today   = to_u16(m.pmax_today);
  s.yield_total  = m.yield_total;
  s.charge_state = to_code(m.charge_state);
  s.mppt_err     = to_code(m.err);
  s.mppt_tracker = to_code(m.tracker);
  s.mppt_flags   = 0;
  return s;
}

void VedLink::feed(uint8_t b, uint32_t now_ms) {
  switch (text_.feed(b)) {
    case vedirect::TextEvent::Block: {
      vedirect::MpptText m;
      counters_.blocks_unparsed += static_cast<uint32_t>(vedirect::decode_mppt(text_.block(), &m));
      last_          = mppt_from_text(m);
      load_on_       = m.load == vedirect::LoadState::On;
      have_block_    = true;
      last_block_ms_ = now_ms;
      break;
    }
    case vedirect::TextEvent::HexLine:
      on_hex_line();
      break;
    case vedirect::TextEvent::Dropped:  // the parser counted it
    case vedirect::TextEvent::None:
      break;
  }
}

bool VedLink::start(const HexJob& job, uint32_t now_ms) {
  if (active_) return false;
  active_   = true;
  job_      = job;
  start_ms_ = now_ms;
  retried_  = false;

  if (job.n < 2 || job.n > vedirect::kMaxChars) {
    finish(lran::HexStatus::MalformedRequest, nullptr, 0);
    return true;
  }
  req_cmd_ = nibble(job.hex[1]);

  // A request that does not decode still goes to the MPPT, which answers it with a frame
  // error (spec 6.7: the string is not the node's to judge). Only Unknown or Error answers it.
  vedirect::Frame q;
  const bool      decoded = vedirect::decode(job.hex, job.n, &q) == vedirect::Parse::Ok;
  req_reg_known_ = decoded && (q.cmd == static_cast<uint8_t>(HexCmd::Get) ||
                               q.cmd == static_cast<uint8_t>(HexCmd::Set)) &&
                   q.len >= 2;
  req_reg_   = req_reg_known_ ? static_cast<uint16_t>(q.data[0] | (q.data[1] << 8)) : 0;
  retryable_ = req_reg_known_ && q.cmd == static_cast<uint8_t>(HexCmd::Get);

  if (!send(now_ms)) return true;
  // Victron: a Restart is never answered, so it completes once it is on the wire.
  if (req_cmd_ == static_cast<uint8_t>(HexCmd::Restart)) finish(lran::HexStatus::Ok, nullptr, 0);
  return true;
}

bool VedLink::send(uint32_t) {
  ++counters_.hex_sent;
  if (!out_->write_line(job_.hex, job_.n)) {
    ++counters_.hex_uart_errors;
    finish(lran::HexStatus::UartError, nullptr, 0);
    return false;
  }
  return true;
}

void VedLink::tick(uint32_t now_ms) {
  if (!active_) return;
  if (reached(now_ms, start_ms_ + params_.hex_timeout_ms)) {
    ++counters_.hex_timeouts;
    finish(lran::HexStatus::Timeout, nullptr, 0);
    return;
  }
  if (retryable_ && !retried_ && reached(now_ms, start_ms_ + params_.hex_timeout_ms / 2)) {
    retried_ = true;
    ++counters_.hex_retries;
    send(now_ms);
  }
}

bool VedLink::take_result(HexResult* out) {
  if (!result_ready_) return false;
  *out          = result_;
  result_ready_ = false;
  return true;
}

MpptSnapshot VedLink::mppt(uint32_t now_ms) const {
  MpptSnapshot s = last_;
  const bool stale = !have_block_ || reached(now_ms, last_block_ms_ + params_.stale_ms);
  s.mppt_flags = static_cast<uint8_t>((have_block_ && load_on_ ? kMpptFlagLoadOn : 0) |
                                      (stale ? kMpptFlagStale : 0) |
                                      (active_ ? kMpptFlagHexPending : 0));
  return s;
}

void VedLink::on_hex_line() {
  vedirect::Frame f;
  if (vedirect::decode(text_.hex_line(), text_.hex_len(), &f) != vedirect::Parse::Ok) {
    ++counters_.hex_bad;
    return;
  }
  if (f.cmd == static_cast<uint8_t>(HexCmd::Async)) {
    ++counters_.hex_async;
    return;
  }
  if (!active_ || !answers(f)) {
    ++counters_.hex_unmatched;
    return;
  }
  ++counters_.hex_answered;
  finish(lran::HexStatus::Ok, text_.hex_line(), text_.hex_len());
}

// Plan 4.2.4 - a reply is correlated by its echoed register; one that carries none by its
// response command. Unknown and Error can answer any request.
bool VedLink::answers(const vedirect::Frame& f) const {
  if (f.cmd == static_cast<uint8_t>(HexRsp::Unknown) || f.cmd == static_cast<uint8_t>(HexRsp::Error)) {
    return true;
  }
  switch (static_cast<HexCmd>(req_cmd_)) {
    case HexCmd::Ping:
      return f.cmd == static_cast<uint8_t>(HexRsp::Ping);
    case HexCmd::AppVersion:
    case HexCmd::ProductId:
      return f.cmd == static_cast<uint8_t>(HexRsp::Done);
    case HexCmd::Get:
    case HexCmd::Set: {
      // Matched on the echoed register alone, not through reg_reply(), which takes a value
      // of at most 4 bytes. A history record (0x1050 onwards) carries 34, and its reply
      // then went unmatched and the request timed out (engineering log, 2026-10-08).
      if (!req_reg_known_ || f.cmd != req_cmd_ || f.len < 3) return false;
      return static_cast<uint16_t>(f.data[0] | (f.data[1] << 8)) == req_reg_;
    }
    default:
      return false;
  }
}

void VedLink::finish(lran::HexStatus status, const char* hex, size_t n) {
  active_         = false;
  result_         = HexResult{};
  result_.token   = job_.token;
  result_.status  = status;
  if (n > vedirect::kMaxChars) n = vedirect::kMaxChars;
  if (n > 0) std::memcpy(result_.hex, hex, n);
  result_.n       = n;
  result_ready_   = true;
}

}  // namespace gatelink
