// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink's half of lran-node's engine: what a command does to the relays, and what its
// STATUS and EVENT frames carry. GL3; GateLink Impl Plan 5.2, 5.4, 6.1; spec 6.6, 7.2, 8.1,
// 8.9.
//
// ARDUINO-FREE, so the native suite can drive it. lora_task owns the engine, the context
// and this application, and is their only caller. The relays are reached through
// GateLinkPort, which on the board queues to io_task.
//
// AN ACTUATION COMMAND IS DEFERRED. execute() hands the relay sequence to io_task and
// leaves the command in flight; lora_task calls Engine::finish_command() when io_task
// reports the last trailing edge (Impl Plan 5.2, decided 2026-10-07). A retry in between
// gets no answer (spec 9.4).
//
// WHAT GL3'S FIRST SLICE LEAVES OUT. Gate state derivation, hold tracking and direction are
// app_task's, and STATUS reports them as UNKNOWN until it runs. CONFIG is not answered yet,
// so the capabilities leave kAnswersConfig out. The MPPT and BMS blocks carry their
// sentinels and their "no data" flags until GL4 and GL5 fill them.

#pragma once

#include <cstddef>
#include <cstdint>

#include "gate_io.h"
#include "lran/node/application.h"
#include "lran/node/engine.h"
#include "lran/node/sink.h"

namespace gatelink {

// What the status needs from io_task, copied at the moment it is built.
struct NodeSnapshot {
  uint8_t  inputs             = 0;  // debounced, bit 0 = IN1
  uint16_t node_mv            = 0;  // INA226 VIN, 0 if unavailable (spec 7.2.4)
  int16_t  enclosure_temp_c10 = INT16_MIN;
  uint32_t uptime_s           = 0;
  uint16_t boot_count         = 0;  // 0 until the Store counts boots
};

class GateLinkPort {
 public:
  virtual ~GateLinkPort() = default;

  // Hands a sequence to io_task. False when it cannot take one, which the engine answers
  // ACTUATOR_BUSY.
  virtual bool dispatch(const RelaySequence& seq) = 0;

  virtual NodeSnapshot snapshot() const = 0;
};

// spec 8.1 to relays (PRD 3.1.2, Impl Plan 6.1). False for a command that moves nothing or
// an `arg` the command does not take.
bool relay_sequence_for(const lran::msg::Command& cmd, RelaySequence* out);

class GateLinkApp final : public lran::node::Application {
 public:
  GateLinkApp(GateLinkPort* port, lran::node::RandomFn random, lran::node::Sink* log)
      : port_(port), random_(random), log_(log) {}

  uint8_t              capabilities(const lran::node::Context&) const override;
  lran::node::RandomFn random() const override { return random_; }

  bool on_poll(lran::node::Engine& engine, lran::node::Context& c, const lran::Header& hdr,
               const uint8_t* payload, size_t len, uint32_t now_ms) override;
  bool on_ping(lran::node::Engine& engine, lran::node::Context& c, const lran::Header& hdr,
               const uint8_t* payload, size_t len, uint8_t fragments, int16_t rssi_dbm,
               int16_t snr_db10, uint32_t now_ms) override;

  lran::node::CommandOutcome execute(lran::node::Context& c, const lran::msg::Command& cmd,
                                     uint32_t now_ms) override;

  size_t build_status(const lran::node::Context& c, lran::StatusReason reason, uint32_t now_ms,
                      uint8_t* out, size_t cap, uint8_t* schema) override;
  size_t build_event(lran::node::Context& c, lran::EventType type, uint16_t detail,
                     uint32_t now_ms, uint8_t* out, size_t cap, uint8_t* schema) override;

  bool    dry_run() const { return dry_run_; }
  uint8_t debug_modes() const { return debug_modes_; }
  bool    bms_polling() const { return bms_polling_; }

  // Local diagnostics: sequences handed to io_task, and those it could not take.
  uint32_t dispatched() const { return dispatched_; }
  uint32_t dispatch_refused() const { return dispatch_refused_; }

 private:
  GateLinkPort*        port_;
  lran::node::RandomFn random_;
  lran::node::Sink*    log_;

  bool    dry_run_     = false;
  uint8_t debug_modes_ = 0;
  bool    bms_polling_ = true;  // PRD R-3.4a polls from boot

  // GateLinkEventV1.event_id - monotonic per boot, so never reused within a ctx_id. HA
  // deduplicates events by it (spec 16.3).
  uint32_t event_id_ = 0;

  uint32_t dispatched_       = 0;
  uint32_t dispatch_refused_ = 0;
};

}  // namespace gatelink
