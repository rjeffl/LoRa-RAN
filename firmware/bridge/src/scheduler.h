// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The poll scheduler. Task BF-17; Impl Plan 6.1; PRD R-3.1d; spec 6.4, 12.3.
//
// ARDUINO-FREE, AND IT DOES NO I/O. It decides; sched_task acts. next() says which node to
// poll or which poll went unanswered, the caller builds and queues the frame and reports
// back with on_sent(). A queue that refused the frame is simply not reported, and the node
// stays due.
//
// ONE OUTSTANDING POLL ACROSS THE WHOLE FLEET (R-3.1d). A poll is outstanding from on_sent()
// until a frame from that node is heard or the reply window closes. Nothing else starts
// meanwhile, however overdue - the largest predictable collision source, removed for the
// cost of a little latency at a 1-5 minute cadence.
//
// WHO IS POLLED (decided with the operator 2026-09-14). Production rows from the first tick,
// so a GateLink that never answers still counts missed polls for BF-20. A bench row, f0-f3,
// only once the bridge has heard any frame from it this boot: a simnode that is not on the
// bench costs no airtime. Nothing removes a row once enrolled; going offline is BF-20's.
//
// A PUSH DOES NOT MOVE THE SCHEDULE (Impl Plan 6.1). on_heard() answers an outstanding poll
// and enrols a bench row, and leaves every due time alone.
//
// Not thread-safe. task_runtime.cpp holds one instance behind a mutex.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/types.h"
#include "registry.h"

namespace bridge {

// The reply window - how long a poll stays outstanding before it counts as missed.
//
// NO DOCUMENT GIVES THIS NUMBER; derived here and raised in the engineering log. A node that
// finds the channel busy may back off cad_retries x backoff_max_ms = 5 x 1500 = 7500 ms
// before it transmits regardless (spec 12.3), and its 0xFE answer is ~0.6 s at SF9 (spec
// 15.1). A window shorter than that counts a node that obeyed media access as missing.
// 10 s covers it with margin. Runtime-settable (root rule 8); TODO(BF-23): from Home Assistant.
inline constexpr uint32_t kPollReplyTimeoutDefaultMs = 10000;

enum class PollAction : uint8_t {
  None,    // nothing to do this step
  Poll,    // build a POLL to `node`, queue it, then call on_sent()
  Missed,  // the outstanding poll to `node` went unanswered: count it
};

struct PollStep {
  PollAction   action = PollAction::None;
  lran::NodeId node   = 0;
};

struct PollStats {
  uint32_t sent     = 0;
  uint32_t answered = 0;
  uint32_t missed   = 0;
};

class PollScheduler {
 public:
  PollScheduler();

  void     set_reply_timeout_ms(uint32_t ms) { reply_timeout_ms_ = ms; }
  uint32_t reply_timeout_ms() const { return reply_timeout_ms_; }

  // Call until it returns None. A Missed is reported before any new Poll, so a closed window
  // is counted on the same tick it frees the fleet. `may_start` false - an OTA upload about
  // to restart the bridge (ota.h) - holds new polls but still closes windows.
  PollStep next(uint32_t now_ms, bool may_start);

  // The POLL to `node` is queued. It is outstanding from now, and the node is next due
  // `interval_s` after this moment - after the send, not after the due time, so a poll that
  // waited behind another does not make the next one early.
  void on_sent(lran::NodeId node, uint16_t interval_s, uint32_t now_ms);

  // A frame from `node` passed the receive ladder. Returns the poll-to-answer time: ms from
  // on_sent() to `now_ms`, when this frame answers the outstanding poll. Returns
  // kNotAnAnswer otherwise. The time includes the POLL's wait in the TX queue and its own
  // media access, because the reply window starts there too; B3a records it against
  // reply_timeout_ms() (Impl Plan 6.1.1).
  static constexpr uint32_t kNotAnAnswer = UINT32_MAX;
  uint32_t on_heard(lran::NodeId node, uint32_t now_ms);

  // The seq for the next POLL. POLL is unauthenticated (spec 9.2), so it takes no command seq:
  // spending one would move nothing a node checks, but it would muddle the space BF-18 owns.
  lran::Seq take_poll_seq() { return poll_seq_++; }

  bool             outstanding() const { return outstanding_; }
  lran::NodeId     outstanding_node() const { return outstanding_node_; }
  bool             enrolled(lran::NodeId node) const;
  const PollStats& stats() const { return stats_; }

 private:
  struct Row {
    lran::NodeId id       = 0;
    bool         bench    = false;
    bool         enrolled = false;
    // False until the first poll is sent: the row is due at once, and is served before any
    // row merely late. A time of 0 would do only while millis() is small - past ~24.8 days
    // it reads as the future, and a row enrolled then would never come due.
    bool         due_set  = false;
    uint32_t     due_ms   = 0;
  };

  int index_of(lran::NodeId node) const;

  Row          rows_[kNodeCount];
  bool         outstanding_      = false;
  lran::NodeId outstanding_node_ = 0;
  uint32_t     sent_ms_          = 0;
  uint32_t     reply_timeout_ms_ = kPollReplyTimeoutDefaultMs;
  lran::Seq    poll_seq_         = 1;
  PollStats    stats_;
};

// spec 6.4 - a POLL to `dst`, carrying the node's ctx_id as learned (0 until heard, which
// a node does not check on an unauthenticated type). Returns the frame length, or 0.
// `ver` is what this node last announced, or kProtoVer before it has been heard
// (R-3.1e, BF-22). Use node_tx_ver().
//
// `poll_flags` defaults to bit 0, full status, which is every scheduled poll. BF-32 sends
// one with bit 1 as well: spec 7.4 resolves a CONFIG whose ACK never arrived with a
// readback request rather than a retransmission, and bit 1 is how that is asked for.
size_t build_poll_frame(lran::NodeId dst, lran::CtxId ctx, lran::Seq seq, uint8_t ver,
                        uint8_t* buf, size_t cap,
                        uint8_t poll_flags = lran::kPollFlagFullStatus);

}  // namespace bridge
