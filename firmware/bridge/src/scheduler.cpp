// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-17; see scheduler.h.

#include "scheduler.h"

#include "lran/codec.h"
#include "lran/config.h"

namespace bridge {
namespace {

// millis() wraps at ~49.7 days and the bridge runs for years. Unsigned subtraction gives the
// elapsed time across the wrap; a due time "in the future" reads as more than half the range.
constexpr uint32_t kHalfRange = 0x80000000u;

bool reached(uint32_t now_ms, uint32_t at_ms) { return now_ms - at_ms < kHalfRange; }

// An interval of 0 would make the node due again at once and, with the window closing
// every reply_timeout_ms, poll it continuously. The table's minimum of 10 s refuses 0
// where the value is set (BF-23); this still holds it to 1 s, for a caller that bypassed
// the table.
uint32_t interval_ms(uint16_t interval_s) {
  return static_cast<uint32_t>(interval_s == 0 ? 1 : interval_s) * 1000u;
}

}  // namespace

PollScheduler::PollScheduler() {
  for (size_t i = 0; i < kNodeCount; ++i) {
    rows_[i].id       = kNodeTable[i].id;
    rows_[i].bench    = lran::is_bench_node(kNodeTable[i].id);
    rows_[i].enrolled = !rows_[i].bench;
  }
}

int PollScheduler::index_of(lran::NodeId node) const {
  for (size_t i = 0; i < kNodeCount; ++i) {
    if (rows_[i].id == node) return static_cast<int>(i);
  }
  return -1;
}

bool PollScheduler::enrolled(lran::NodeId node) const {
  const int i = index_of(node);
  return i >= 0 && rows_[i].enrolled;
}

PollStep PollScheduler::next(uint32_t now_ms, bool may_start) {
  PollStep step;
  if (outstanding_) {
    if (now_ms - sent_ms_ < reply_timeout_ms_) return step;  // still waiting
    outstanding_ = false;
    ++stats_.missed;
    step.action = PollAction::Missed;
    step.node   = outstanding_node_;
    return step;
  }
  if (!may_start) return step;

  // The most overdue enrolled row; a row never polled counts as the most overdue of all. Ties
  // go to table order, so production rows lead at boot.
  int      best      = -1;
  uint32_t best_late = 0;
  for (size_t i = 0; i < kNodeCount; ++i) {
    const Row& r = rows_[i];
    if (!r.enrolled) continue;
    if (r.due_set && !reached(now_ms, r.due_ms)) continue;
    const uint32_t late = r.due_set ? now_ms - r.due_ms : UINT32_MAX;
    if (best < 0 || late > best_late) {
      best      = static_cast<int>(i);
      best_late = late;
    }
  }
  if (best < 0) return step;
  step.action = PollAction::Poll;
  step.node   = rows_[best].id;
  return step;
}

void PollScheduler::on_sent(lran::NodeId node, uint16_t interval_s, uint32_t now_ms) {
  const int i = index_of(node);
  if (i < 0) return;
  rows_[i].due_ms   = now_ms + interval_ms(interval_s);
  rows_[i].due_set  = true;
  rows_[i].sent_ms  = now_ms;
  outstanding_      = true;
  outstanding_node_ = node;
  sent_ms_          = now_ms;
  ++stats_.sent;
}

void PollScheduler::retime(lran::NodeId node, uint16_t interval_s) {
  const int i = index_of(node);
  if (i < 0 || !rows_[i].due_set) return;
  rows_[i].due_ms = rows_[i].sent_ms + interval_ms(interval_s);
}

uint32_t PollScheduler::on_heard(lran::NodeId node, uint32_t now_ms) {
  const int i = index_of(node);
  if (i < 0) return kNotAnAnswer;
  uint32_t answer_ms = kNotAnAnswer;
  if (outstanding_ && outstanding_node_ == node) {
    outstanding_ = false;
    ++stats_.answered;
    answer_ms = now_ms - sent_ms_;  // unsigned, so correct across the millis() wrap
  }
  if (!rows_[i].enrolled) {
    rows_[i].enrolled = true;  // a bench node that just spoke is polled at the next free slot
    rows_[i].due_set  = false;
  }
  return answer_ms;
}

size_t build_poll_frame(lran::NodeId dst, lran::CtxId ctx, lran::Seq seq, uint8_t ver,
                        uint8_t* buf, size_t cap, uint8_t poll_flags) {
  lran::Header h;
  h.ver    = ver;  // R-3.1e - the version last heard from this node (BF-22)
  h.type   = lran::MsgType::Poll;
  h.src    = lran::kNodeBridge;
  h.dst    = dst;
  h.seq    = seq;
  h.ctx_id = ctx;
  h.schema = lran::kSchemaNone;

  const uint8_t   payload[1] = {poll_flags};
  lran::EncodeCtx ectx;  // spec 9.2 - POLL carries no MAC
  size_t          len = 0;
  return lran::encode(h, payload, sizeof(payload), ectx, buf, cap, &len) == lran::Status::Ok ? len : 0;
}

}  // namespace bridge
