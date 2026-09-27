// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The bridge-side GateLink simulator. Task BF-27; Impl Plan 6.6.3; PRD R-5.4c, R-5.2d.
//
// ARDUINO-FREE. An unattended, time-varying GateLink: once started from the serial console
// it sends a STATUS every `period` seconds, and a gate cycle's events every `gate` seconds if
// asked, through the same injection as the dummy publish (dummy.h). The dummy answers one
// line with one frame; this answers a bench session that wants Home Assistant's graphs to
// move without anyone typing.
//
// ITS OWN MODEL, NOT simnode's GENERATOR (Impl Plan 6.6). simnode tests the RF path and this
// tests the MQTT path. A shared generator would make each the other's oracle, and a decoder
// fed its own encoder's idea of a plausible node tests nothing. The model below is
// deliberately simple - a sine sun, a coulomb-counted pack - and written here.
//
// MARKED, AND REFUSED WHERE THE DUMMY IS REFUSED. Every STATUS goes through
// synthetic_status(), which forces DEBUG_SYNTHETIC; every EVENT goes out marked `dummy`, so
// app_task publishes it with `synthetic: true`. A bench node is refused at `start`. A node
// heard this boot is refused per frame in task_runtime.cpp, which stops the simulator.
//
// EVENTS ARE OPT-IN. An event topic drives email and SMS, and an unattended source of gate
// events is exactly what an automation that forgot to filter on `synthetic` would mail. So
// `gate` defaults to 0, which sends none.
//
// NO TIMING IS COMPILED IN (root rule 8). Period, day length and gate interval are console
// arguments; the constants below are their defaults and bounds.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/schema/gatelink_status_v1.h"
#include "lran/types.h"
#include "queues.h"

namespace bridge {

enum class SimOutcome : uint8_t {
  NotMine,  // not a `sim` line
  Reply,    // answered on the console
  Refused,  // the reply says why
};

inline constexpr uint32_t kSimPeriodDefaultS = 30;
inline constexpr uint32_t kSimDayDefaultS    = 1800;  // a simulated day in half an hour
inline constexpr uint32_t kSimGateDefaultS   = 0;     // no events unless asked

// A gate cycle's shape, in seconds from its start. Longer than a cycle, `gate` would start
// the next one before the last closed.
inline constexpr uint32_t kSimOpenAtS    = 15;
inline constexpr uint32_t kSimClosingAtS = 45;
inline constexpr uint32_t kSimClosedAtS  = 60;
inline constexpr uint32_t kSimGateMinS   = 90;

class GateLinkSim {
 public:
  GateLinkSim();

  // One console line, without its line ending. `ctx_id` becomes the simulated node's context
  // if the line starts it: a fresh one per start keeps spec 7.3's (ctx_id, event_id) from
  // repeating when event_id restarts, and from colliding with the dummy's.
  //
  //   sim help
  //   sim start <node> [period=<s>] [day=<s>] [gate=<s>]
  //   sim stop
  //   sim show
  SimOutcome handle(const char* line, lran::CtxId ctx_id, uint32_t now_ms, char* reply,
                    size_t cap);

  // Call often. True when *out holds one frame to inject; at most one per call, events
  // before a STATUS that fell due at the same moment.
  bool poll(uint32_t now_ms, RxMessage* out);

  void         stop() { running_ = false; }
  bool         running() const { return running_; }
  lran::NodeId node() const { return node_; }

  // The model's state, for the tests and `sim show`.
  const lran::schema::GateLinkStatusV1& status() const { return status_; }
  uint32_t                              time_of_day_s() const;

 private:
  void reset_model();
  // Moves the model on by `real_ms` of bench time: the sun by the simulated day, the node's
  // own clocks (uptime, traversal age) by real seconds, as a node's would.
  void step(uint32_t real_ms);
  // The gate cycle's next edge, if one is due. Changes the model and fills *out.
  bool gate_edge(uint32_t now_ms, RxMessage* out);
  bool make_event(lran::EventType type, uint32_t now_ms, RxMessage* out);

  lran::schema::GateLinkStatusV1 status_;

  bool         running_ = false;
  lran::NodeId node_    = 0;
  lran::CtxId  ctx_id_  = 0;
  lran::Seq    seq_     = 0;
  uint32_t     event_id_ = 0;

  uint32_t period_s_ = kSimPeriodDefaultS;
  uint32_t day_s_    = kSimDayDefaultS;
  uint32_t gate_s_   = kSimGateDefaultS;

  uint32_t last_ms_      = 0;  // now_ms at the last step()
  uint32_t carry_ms_     = 0;  // real time not yet counted into the node's whole seconds
  uint32_t next_status_ms_ = 0;
  uint32_t cycle_ms_     = 0;  // now_ms at which the current or next gate cycle starts
  uint8_t  cycle_edge_   = 0;  // edges of the current cycle already sent

  // The model's continuous state. Float: the ESP32-S3 has a single-precision FPU, and
  // nothing here needs more than the schema's integer resolution.
  float tod_s_      = 0;  // simulated time of day
  float soc_mah_    = 0;
  float wh_today_   = 0;
  float wh_total_   = 0;
};

}  // namespace bridge
