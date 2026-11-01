// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The task table, as data. Task L6; GateLink Impl Plan 5.2.
//
// ARDUINO-FREE ON PURPOSE, as the bridge's tasks.h is. This header and tasks.cpp carry the
// table and the invariants over it; task_runtime.cpp makes the FreeRTOS calls. The split
// lets `pio test -e native` assert what Impl Plan 5.2 states in prose: io_task outranks
// everything (R-5.2a), and log_task sits beneath everything.

#pragma once

#include <cstddef>
#include <cstdint>

namespace gatelink {

// The seven tasks of Impl Plan 5.2, in that table's order. The table is indexed by these,
// so a task added without a row fails to compile.
enum class TaskId : uint8_t {
  Io = 0,
  Vedirect,
  Lora,
  App,
  Bms,
  Ui,
  Log,
  kCount,
};

inline constexpr size_t kTaskCount = static_cast<size_t>(TaskId::kCount);

// FreeRTOS priorities on the ESP32-S3. IDLE is 0 and the Arduino loopTask is 1. The gaps
// leave room for a task added later without renumbering the ones above it.
enum Priority : uint8_t {
  kPriorityLog    = 1,  // Lowest
  kPriorityLow    = 2,  // bms, ui
  kPriorityNormal = 3,  // app
  kPriorityHigh   = 4,  // vedirect, lora
  kPriorityIo     = 6,  // Highest, and alone at the top (R-5.2a)
};

// No task is pinned yet. NimBLE's host and controller choose their own core, and where
// io_task and lora_task should sit relative to them is a question for GL5, when BLE first
// runs beside the radio.
inline constexpr int kAnyCore = -1;  // tskNO_AFFINITY at the call site

struct TaskSpec {
  TaskId      id;
  const char* name;  // FreeRTOS task name; a panic backtrace prints it
  uint8_t     priority;
  // BYTES. ESP-IDF's FreeRTOS takes the depth in bytes on the ESP32-S3, where upstream
  // FreeRTOS counts words. The bridge sized every stack a quarter too small before it
  // found that (bridge engineering log, 2026-09-13).
  uint32_t    stack_bytes;
  int         core;
  // A fixed tick, or 0 when the period is a parameter or the task waits on an event.
  uint32_t    period_ms;
  // The lran-config parameter that sets the period, or 0. Root rule 8: a period an
  // operator may need to change is a parameter, because changing a constant here means a
  // walk to the gate with a laptop.
  uint16_t    period_param;
};

const TaskSpec& task_spec(TaskId id);
const TaskSpec* task_table();

// A task's fixed tick, or the default of its period parameter, scaled to milliseconds. 0
// for an event-driven task. io_task and bms_task read their period parameters live instead.
uint32_t default_period_ms(const TaskSpec& spec);

// The default of a GateLink parameter in lran-config's table, or 0 if the table has no such
// row. The Store's value is task_runtime's live_param(); this is the table's alone.
uint32_t param_default(uint16_t id);

// The node-common rows GateLink applies (spec 7.4, D46): two in the engine's context, two
// in the radio's media access (spec 12.3).
inline constexpr uint16_t kParamDedupCacheDepth         = 0x0100;
inline constexpr uint16_t kParamFragReassemblyTimeoutMs = 0x0101;
inline constexpr uint16_t kParamCadRetries              = 0x0102;
inline constexpr uint16_t kParamBackoffMaxMs            = 0x0103;

// GateLink's rows in lran-config's table (Impl Plan 4.4) that io_task reads.
inline constexpr uint16_t kParamRelayPulseMs        = 0x1000;
inline constexpr uint16_t kParamRelayMinSpacingMs   = 0x1001;
inline constexpr uint16_t kParamUnlockSettleMs      = 0x1002;
inline constexpr uint16_t kParamInputPollMs         = 0x1010;
inline constexpr uint16_t kParamInputDebounceSamples = 0x1011;
// And those vedirect_task and lora_task read (GL4).
inline constexpr uint16_t kParamHexTimeoutMs        = 0x1030;
inline constexpr uint16_t kParamVedirectStaleS      = 0x1031;
// And bms_task's period (GL5, PRD R-3.4a).
inline constexpr uint16_t kParamBmsPollS            = 0x1040;
inline constexpr uint16_t kParamBmsWindowMaxMs      = 0x1042;  // the interlock's cap, Impl Plan 5.2
inline constexpr uint16_t kParamBmsRetryS           = 0x1046;  // after an aborted window
// And app_task's charge inhibition (PRD R-6.4a, R-6.4b).
inline constexpr uint16_t kParamChargeConfirmS      = 0x1041;
inline constexpr uint16_t kParamChargeTempC         = 0x1043;
inline constexpr uint16_t kParamChargeBandMa        = 0x1044;
inline constexpr uint16_t kParamChargeRiseMv        = 0x1045;
// And app_task's: state derivation, detection and injection (GL3; PRD R-3.1.4b, R-3.2.3e,
// R-5.4b; Impl Plan 6.3).
inline constexpr uint16_t kParamHoldConfirmMs       = 0x1012;
inline constexpr uint16_t kParamDetectWindowMs      = 0x1020;
inline constexpr uint16_t kParamDetectIdleMs        = 0x1021;
inline constexpr uint16_t kParamAlertRepeatS        = 0x1022;
inline constexpr uint16_t kParamCauseWindowMs       = 0x1023;
inline constexpr uint16_t kParamInjectSpacingMs     = 0x1052;
// And the task watchdog's timeout, which start_tasks() arms (GL3, Impl Plan 5.2).
inline constexpr uint16_t kParamWatchdogTimeoutS    = 0x1060;

// watchdog_timeout_s held to its row's range. The Store clamps a SET already; this guards
// the call itself, because a timeout of 0 or 1 s that slips through is a reset loop at a
// node only a USB reflash can recover.
uint32_t watchdog_timeout_in_range(uint32_t seconds);

// F5 - whether io_task's pass count has stood still for `limit_ms`. The task watchdog watches
// app_task alone, so app_task asks this and stops feeding it. A hung io_task with a relay
// closed then ends in a reset, and setup() releases the relay (engineering log, 2026-11-01).
// The first call is a baseline. A count that moves again clears it.
class ProgressWatch {
 public:
  bool stalled(uint32_t passes, uint32_t now_ms, uint32_t limit_ms);

 private:
  bool     seen_  = false;
  uint32_t last_  = 0;
  uint32_t since_ = 0;
};

// How long io_task may stand still, in input polls. Its period is at most input_poll_ms, so
// this tolerates a slow I2C pass, and it scales with the parameter (root rule 8).
inline constexpr uint32_t kIoStallPolls = 20;

// ---------------------------------------------------------------------------
// The invariants. Impl Plan 5.2's rules, written so a test can fail.
// ---------------------------------------------------------------------------

// R-5.2a - "the I/O service must never be starved". STRICTLY highest: a tie lets the task
// it tied with delay the trailing edge of a relay pulse, which is a command of the wrong
// length.
bool io_is_strictly_highest();

// Leveled logging is never worth delaying anything else.
bool log_is_strictly_lowest();

// Nothing sits below the Arduino loopTask, so a loop() that never yields cannot starve it.
bool all_priorities_above_arduino_loop();

// Names are what a panic backtrace prints, so two tasks may not share one.
bool task_names_are_unique();

// Every period parameter named in the table exists in GateLink's block of lran-config.
bool period_params_exist();

// app_task feeds the watchdog once a pass (Impl Plan 5.2), so its tick must come round in
// under half the SHORTEST timeout the row allows, not just the default. The bridge holds
// its watched tasks to the same half (bridge tasks.h).
bool app_feeds_well_inside_the_watchdog();

}  // namespace gatelink
