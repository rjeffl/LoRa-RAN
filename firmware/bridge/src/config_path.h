// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The node half of the configuration path: one CONFIG on the air, its CONFIG_ACK, and
// the readback that resolves an outcome nobody knows. Task BF-32; spec 7.4, 7.4.1, 16.7;
// D57.
//
// ARDUINO-FREE AND IT DOES NO I/O, like command.h and for the same reason. next() says
// what to transmit or what to publish; the caller builds the frame, queues it and reports
// back with on_sent().
//
// A LOST CONFIG_ACK IS RECOVERED BY READBACK, NOT RETRANSMISSION (spec 7.4). This is the
// rule that shapes the whole file and it is the opposite of the command path's. A command
// is idempotent through the node's (ctx_id, seq) dedup, so a retry is safe and is what
// BS-3 asks for. A configuration write is NOT idempotently repeatable and a repeat cannot
// tell you whether the first one took effect - so a CONFIG with no CONFIG_ACK inside its
// timeout is reported to Home Assistant as `unknown`, and the bridge asks the node what
// it is actually running. Retrying one to find out is the wrong instinct and the
// specification says so.
//
// THE READBACK MAY ARRIVE IN PIECES (spec 7.4.1, D57). A node whose answer outgrows one
// frame sends several CONFIG_ACK messages, every one but the last carrying MORE_FOLLOWS.
// Two things follow and both are easy to get wrong: the bridge must ACCEPT MORE THAN ONE
// CONFIG_ACK bearing a given `seq` - a bridge that closes on the first message strands
// the rest and reports a configuration it did not finish reading - and it must PUBLISH
// NOTHING until the answer completes. A retained config/state holding half of this
// answer and half of the last one cannot be read back apart afterwards.
//
// ONE TRANSACTION IN FLIGHT ACROSS THE FLEET, for the reason command.h gives. Two would
// need a correlation this protocol does not carry: an unsolicited readback takes its
// `seq` from the node's status space (D45) and correlates to no request at all.

#pragma once

#include <cstddef>
#include <cstdint>

#include "config_json.h"
#include "lran/frame.h"
#include "lran/schema/node_config_v1.h"
#include "lran/types.h"
#include "phy_change.h"
#include "registry.h"

namespace bridge {

// Spec 7.4.1 bounds one answer at kMaxConfigAckMessages of kMaxConfigAckEntries. The
// staging area is capped at what a table can hold instead, because a bridge that cannot
// stage an answer it asked for has a defect the node cannot fix.
inline constexpr size_t kMaxStagedResults = 64;

// How long a CONFIG waits for its CONFIG_ACK before the outcome is `unknown`. Longer than
// a command's, because a CONFIG_ACK is larger than a COMMAND_ACK and the node may be
// several frames into an answer; shorter than the readback's, which starts afterwards.
inline constexpr uint32_t kConfigAckTimeoutDefaultMs = 8000;

// Spec 7.4.1 - from the FIRST message of an answer, not from the request. A node that
// sent one message and stopped is the case this bounds, and the clock has to start when
// the answer started or a slow node looks like a lost one.
inline constexpr uint32_t kConfigReadbackTimeoutDefaultMs = 15000;

// What mqtt_task hands to sched_task. The names ride along so the answer can be published
// by name without a second table lookup on another task.
struct ConfigJob {
  lran::NodeId               dst = 0;
  lran::ConfigOp             op  = lran::ConfigOp::Set;
  lran::schema::NodeConfigV1 config{};
  char                       names[kMaxConfigSetEntries][kMaxParamNameLen] = {};
  uint8_t                    name_count = 0;

  // THE BRIDGE'S HALF RIDES ALONG BECAUSE THERE IS ONLY ONE ANSWER. Spec 16.7.1 asks for
  // ONE `config/ack` published when EVERY half has an outcome, so the half the bridge
  // already applied waits here for the half that is still on the air. Publishing the
  // bridge's half early would give Home Assistant two answers to one set, the first of
  // them incomplete.
  ConfigResult bridge_results[kMaxConfigSetEntries] = {};
  uint8_t      bridge_result_count                  = 0;
  AckPersist   bridge_persist                       = AckPersist::NotApplied;
  bool         bridge_changed                       = false;

  // BF-33 - a fleet PHY change from lran/bridge/config/set (spec 12.4.1). `dst` is unused;
  // the fleet is every node sched_task polls when the change starts.
  bool         phy = false;
  PhyGroup     phy_from{};
  PhyGroup     phy_to{};
  bool         phy_named[kPhyGroupSize]  = {};
  ResultStatus phy_status[kPhyGroupSize] = {};

  // spec 8.7, D69 - the node reported CONFIG_CHANGE, and nobody asked for anything. The
  // transaction starts at the readback (POLL bit 1), and its resolution republishes
  // `config/state` and publishes no `config/ack`, because there is no set to answer.
  bool readback_only = false;
};

// spec 8.7, D69 - true when a STATUS carries `status_reason` CONFIG_CHANGE. Schema 0xFE
// mirrors 0x10's layout, so a bench node reports it the same way; any other schema, or
// a payload that does not decode, is false.
bool status_reports_config_change(const lran::Header& hdr, const uint8_t* payload,
                                  size_t len);

// A node reboot, read from its STATUS, so that the bridge can ask for a readback.
//
// WHY. The readback mirror (config_store.h) holds what a node last said it runs, and a
// reboot can change that without a word. A simnode holds its overrides in RAM, so a
// reboot clears them while `config/state` goes on reporting them. A node with a store
// (D49) keeps them, and the bridge cannot tell the two apart from the reboot alone. The
// answer costs one POLL per reboot: ask.
//
// NOT ON A NEW ctx_id. Spec 10.1 says a new context no longer means a reboot, because a
// roll (spec 10.6) makes one and keeps the configuration. The same section names what
// does report a reboot: `boot_count` and `uptime_s`. So a roll cannot trigger this.
//
// THREE SIGNS, ANY ONE ENOUGH:
//   - `status_reason` BOOT (spec 8.7), on schemas 0x10 and 0xFE;
//   - `boot_count` changed, when both readings are non-zero (0 is unavailable, 7.2.4);
//   - `uptime_s` is below what the last reading predicts. The prediction adds the time
//     since that frame arrived, so a node heard again only after running longer than
//     it had before its reboot is still caught. The slack covers truncation to whole
//     seconds and a frame's wait for media access (spec 12.3), and scales with the gap
//     for clock drift.
// Schema 0xF0 carries no `status_reason`; the other two signs apply to it.
//
// The first STATUS heard from a node sets the reference and reports nothing, unless it
// says BOOT. app_task alone calls this, so it takes no lock.
class RebootWatch {
 public:
  // True when this STATUS shows `src` rebooted since its last one. `rx_ms` is the
  // bridge's receive time.
  bool on_status(const lran::Header& hdr, const uint8_t* payload, size_t len,
                 uint32_t rx_ms);

  static constexpr uint32_t kSlackS       = 5;
  static constexpr uint32_t kDriftDivisor = 256;  // ~0.4 %, far above a crystal's

 private:
  struct Seen {
    bool     heard      = false;
    uint32_t uptime_s   = 0;
    uint16_t boot_count = 0;
    uint32_t rx_ms      = 0;
  };
  Seen seen_[kNodeCount] = {};
};

enum class ConfigAction : uint8_t {
  None,
  SendConfig,       // build a CONFIG from `payload` for `dst`, queue it, then on_sent()
  RequestReadback,  // a POLL with poll_flags bit 1 to `dst`, then on_sent()
  Resolve,          // the transaction is over; publish what `results` says
};

// How a transaction ended, as spec 16.7.3 reports it.
enum class ConfigOutcome : uint8_t {
  Pending,
  Acked,        // a CONFIG_ACK completed; `results` are the node's
  Unknown,      // no CONFIG_ACK inside the timeout (spec 7.4) - NEVER failure
  ReadbackOk,   // an unsolicited readback completed; `results` are the node's values
  Abandoned,    // a readback started and did not finish inside its timeout
};

struct ConfigStep {
  ConfigAction action = ConfigAction::None;
  lran::NodeId dst    = 0;

  // SendConfig
  lran::schema::NodeConfigV1 payload{};
  lran::Seq                  seq    = 0;
  lran::CtxId                ctx_id = 0;

  // Resolve
  ConfigOutcome  op_outcome = ConfigOutcome::Pending;
  lran::ConfigOp op         = lran::ConfigOp::Set;
  AckPersist     persist    = AckPersist::Unknown;
  // One result per entry, by param id. The caller maps ids back to names.
  const lran::schema::ConfigAckEntry* results = nullptr;
  size_t                              result_count = 0;
  // True when `results` carry effective values the retained `config/state` may be
  // updated from: a completed readback, or a CONFIG_ACK that answered a SET. Spec 16.7.4
  // forbids publishing from an INCOMPLETE answer, which is why this is set on the
  // closing message and nowhere else.
  bool updates_state = false;
};

struct ConfigStats {
  uint32_t submitted           = 0;
  uint32_t refused_busy        = 0;
  uint32_t sent                = 0;
  uint32_t acked               = 0;
  uint32_t unknown             = 0;  // spec 7.4 - no CONFIG_ACK inside the timeout
  uint32_t readbacks_requested = 0;
  uint32_t readbacks_completed = 0;
  // Spec 7.4.1's bridge-local counter, and deliberately NOT a spec 14.1 one: nothing was
  // discarded on the wire and schema 0xF0 is a node health schema.
  uint32_t config_readback_abandoned = 0;
  uint32_t ack_ignored               = 0;  // wrong src, or nothing in flight
  uint32_t staging_overflow          = 0;  // an answer larger than this bridge can hold
};

class ConfigPath {
 public:
  void     set_ack_timeout_ms(uint32_t ms) { ack_timeout_ms_ = ms; }
  void     set_readback_timeout_ms(uint32_t ms) { readback_timeout_ms_ = ms; }
  uint32_t readback_timeout_ms() const { return readback_timeout_ms_; }

  bool         busy() const { return phase_ != Phase::Idle; }
  lran::NodeId in_flight_node() const { return job_.dst; }

  // False when one is already in flight. `ctx` and `seq` come from the registry, as a
  // command's do: CONFIG is authenticated (spec 9.2) and carries the node's context. A
  // `readback_only` job sends no CONFIG, so its `ctx` and `seq` are unused.
  bool submit(const ConfigJob& job, lran::CtxId ctx, lran::Seq seq, uint32_t now_ms);

  // Call until it returns None. Emits Resolve exactly once per transaction.
  ConfigStep next(uint32_t now_ms);

  // The frame from the last Send or RequestReadback is on the TX queue.
  void on_sent(uint32_t now_ms);

  // The frame from the last Send or RequestReadback left lora_task at `aired_ms`: on air, or given up. The
  // window reopens from then, because a frame can wait seconds for media access (spec
  // 12.3) and a window counted from the queue closes early by that much.
  void on_aired(uint32_t aired_ms);

  // A CONFIG_ACK passed the receive ladder. `ack_seq` is the header's `seq`: a solicited
  // answer repeats the request's on every message, and an unsolicited readback takes one
  // from the node's status space per message (D45), so it is NOT matched for a readback.
  void on_config_ack(lran::NodeId src, const lran::schema::NodeConfigAckV1& ack,
                     lran::Seq ack_seq, uint32_t now_ms);

  const ConfigStats& stats() const { return stats_; }

 private:
  enum class Phase : uint8_t {
    Idle,
    SendDue,
    AwaitingAck,      // the CONFIG is out, its answer is not in
    ReadbackDue,      // a POLL with bit 1 is owed
    AwaitingReadback, // the poll is out, the unsolicited answer is not complete
    Resolved,
  };

  void stage(const lran::schema::NodeConfigAckV1& ack);
  void finish(ConfigOutcome outcome);

  ConfigJob   job_{};
  Phase       phase_  = Phase::Idle;
  lran::Seq   seq_    = 0;
  lran::CtxId ctx_    = 0;

  uint32_t window_opened_ms_  = 0;
  uint32_t readback_first_ms_ = 0;
  uint8_t  messages_staged_   = 0;

  ConfigOutcome outcome_ = ConfigOutcome::Pending;
  AckPersist    persist_ = AckPersist::Unknown;

  lran::schema::ConfigAckEntry staged_[kMaxStagedResults] = {};
  size_t                       staged_count_             = 0;

  uint32_t ack_timeout_ms_      = kConfigAckTimeoutDefaultMs;
  uint32_t readback_timeout_ms_ = kConfigReadbackTimeoutDefaultMs;

  ConfigStats stats_;
};

}  // namespace bridge
