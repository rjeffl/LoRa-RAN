// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The dummy publish. Task BF-27; Impl Plan 6.6.2; PRD R-5.2d; spec 7.2, 7.3, 8.7, 16.6.
//
// ARDUINO-FREE. A serial console line becomes an RxMessage that app_task hands to the real
// publication policy, so the documents, events and discovery Home Assistant sees are the
// ones a node's frame would produce - with no node and no radio (Impl Plan 6.6).
//
// THE FRAME IS A PRODUCTION NODE'S, AND IT IS MARKED. Chosen with the operator,
// 2026-09-23: a dummy STATUS carries the address the operator names, and so exercises that
// node's own topics and entities. Its status_reason is DEBUG_SYNTHETIC whatever the console
// asks, so every document says `synthetic: true` (spec 8.7, R-5.2d). An EVENT has no
// status_reason (spec 7.3), so app_task marks a dummy one through on_event()'s own flag.
//
// WHAT IT REFUSES, and why here rather than at the policy. A bench address: spec 16.6
// withholds its STATUS and EVENT anyway, so a dummy one would demonstrate nothing. Anything
// that would change the template's status_reason. The console refuses a node the bridge has
// heard this boot as well, in task_runtime.cpp, because this file cannot see the registry:
// synthetic history interleaved with a real node's is the confusion R-5.2d exists to stop.
//
// THIS IS NOT simnode's GENERATOR, and must not become one (Impl Plan 6.6). The template
// below is built here from the library's struct and encoded by the library's serializer, so
// the decode in publish.cpp is tested against the codec, not against itself.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/schema/gatelink_status_v1.h"
#include "lran/types.h"
#include "queues.h"

namespace bridge {

// What a console line asked for.
enum class DummyOutcome : uint8_t {
  NotMine,  // not a `dummy` line
  Reply,    // answered on the console; nothing to inject
  Inject,   // *out is a STATUS or EVENT for app_task
  Refused,  // the reply says why
};

class DummyPublisher {
 public:
  DummyPublisher();

  // One console line, without its line ending. `ctx_id` is the dummy's context for this
  // boot; a fresh one per boot keeps spec 7.3's (ctx_id, event_id) key from repeating when
  // event_id restarts. `now_ms` becomes the message's rx_millis.
  //
  //   dummy help
  //   dummy show
  //   dummy set <field>=<value> [<field>=<value> ...]    `na` is the field's sentinel
  //   dummy status <node>
  //   dummy event <node> <type> [follow]                 <type> is spec 8.9's name, lower
  //                                                      case, or its number
  //
  // `reply` always holds a line for the console; `show` needs about 800 bytes of it.
  DummyOutcome handle(const char* line, lran::CtxId ctx_id, uint32_t now_ms, RxMessage* out,
                      char* reply, size_t cap);

  const lran::schema::GateLinkStatusV1& status_template() const { return status_; }

 private:
  // Moves uptime_s and last_traversal_age_s on by the time since the last frame, as a node's
  // own clock would. Held still, the traversal age computes to a new absolute time on every
  // frame (spec 7.2.9) and the detect document never looks unchanged.
  void advance(uint32_t now_ms);
  bool build_status(lran::NodeId node, lran::CtxId ctx_id, uint32_t now_ms, RxMessage* out);
  bool build_event(lran::NodeId node, uint8_t event_type, bool follow_up, lran::CtxId ctx_id,
                   uint32_t now_ms, RxMessage* out);

  lran::schema::GateLinkStatusV1 status_;
  bool                           clock_valid_ = false;
  uint32_t                       clock_ms_    = 0;  // now_ms at the last advance()
  uint32_t                       carry_ms_    = 0;  // the part of a second not yet counted
  lran::Seq                      seq_      = 0;
  uint32_t                       event_id_ = 0;  // the last one sent; 0 until the first
};

}  // namespace bridge
