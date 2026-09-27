// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The bridge's PING responder: BF-27's RF echo, PRD R-5.4a; spec 6.6, 17.3.
//
// ARDUINO-FREE AND IT DOES NO I/O. app_task offers each PING it receives; sched_task asks
// for the echo's frames one at a time and queues them. Both calls are made under the
// scheduler's lock, so the echo is one more exchange that air_turn.h can see.
//
// ONE ECHO AT A TIME. A PING that arrives while an echo is still going out is refused and
// counted, not queued. The initiator times out and says so, which is the right answer on a
// bench tool, and no node can make the bridge hold the air for more than one echo.
//
// THE ECHO IS THE PING, TURNED ROUND. src and dst swap; seq, ctx_id, ver, ping_flags and
// the echo bytes stay as received (spec 6.6). ctx_id is the node's, as it is on every
// frame the bridge sends that node. A fragmented PING is re-fragmented at the chunk the
// initiator used (spec 6.6.2), so both directions exercise the same split.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config.h"
#include "lran/frame.h"

namespace bridge {

enum class EchoOffer : uint8_t {
  Accepted,
  Busy,       // an echo is still going out
  Malformed,  // not a PING payload spec 6.6 describes
};

struct EchoStats {
  uint32_t answered  = 0;  // echoes accepted
  uint32_t busy      = 0;  // refused, EchoOffer::Busy
  uint32_t malformed = 0;  // refused, EchoOffer::Malformed
  uint32_t frames    = 0;  // echo frames queued for lora_task
};

class PingEcho {
 public:
  // app_task. `frag_chunk` is RxMessage::frag_chunk: 0 for a single frame.
  EchoOffer offer(const lran::Header& ping, const uint8_t* payload, size_t len,
                  uint8_t frag_chunk);

  // True from an accepted offer until finish().
  bool busy() const { return active_; }

  // Frames not yet queued.
  bool frames_left() const { return active_ && next_ < total_; }

  // Whether the first frame is queued. Until then the echo waits for the air as any
  // exchange does; after it, the rest follow without asking.
  bool started() const { return next_ > 0; }

  // The frame encode_next() gives is the echo's last.
  bool at_last() const { return active_ && next_ + 1 == total_; }

  // Encodes the next frame into `buf` without moving past it, so a frame the TX queue
  // refuses is encoded again next time. 0 when none is left or encoding failed.
  size_t encode_next(uint8_t* buf, size_t cap) const;

  // The frame encode_next() gave was queued.
  void advance();

  // The last frame has left lora_task, or the echo is abandoned.
  void finish() { active_ = false; }

  lran::NodeId peer() const { return reply_.dst; }
  lran::Seq    seq() const { return reply_.seq; }
  uint8_t      total() const { return total_; }
  size_t       len() const { return len_; }
  const EchoStats& stats() const { return stats_; }

 private:
  lran::Header reply_{};
  uint8_t      payload_[lran::kMaxPayloadPlain] = {0};
  size_t       len_    = 0;
  uint8_t      chunk_  = 0;  // 0: one frame
  uint8_t      total_  = 0;
  uint8_t      next_   = 0;
  bool         active_ = false;
  EchoStats    stats_;
};

}  // namespace bridge
