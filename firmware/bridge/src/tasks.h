// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The task table, as data. Task BF-11; Impl Plan 5.2.
//
// ARDUINO-FREE ON PURPOSE. This header and tasks.cpp carry the table, the queue
// depths and the invariants over them; task_runtime.cpp does the FreeRTOS calls.
// The split is what lets `pio test -e native` assert the properties Impl Plan 5.2
// states in prose - that lora_task outranks everything, that log_task is beneath
// everything - instead of leaving them as a comment nobody can fail.

#pragma once

#include <cstddef>
#include <cstdint>

namespace bridge {

// The seven tasks of Impl Plan 5.2, in that table's order.
//
// The enumerators are the identity used everywhere: the table is indexed by them,
// and a task added without a table row fails to compile rather than starting with
// whatever priority the last row happened to have.
enum class TaskId : uint8_t {
  Lora = 0,
  Sched,
  Mqtt,
  App,
  Ota,
  Ui,
  Log,
  kCount,
};

inline constexpr size_t kTaskCount = static_cast<size_t>(TaskId::kCount);

// FreeRTOS priorities on ESP32-S3. IDLE is 0 and the Arduino `loopTask` is 1, so
// nothing here may sit below 1 without being starved by the Arduino loop.
//
// The plan's five bands map onto five numbers, and the GAPS ARE DELIBERATE: a task
// added later at "just above mqtt" gets a number rather than a renumbering of
// everything above it.
enum Priority : uint8_t {
  kPriorityLog   = 1,  // Lowest
  kPriorityLow   = 2,  // ota, ui
  kPriorityNormal = 3,  // mqtt, app
  kPriorityHigh  = 4,  // sched
  kPriorityLora  = 6,  // Highest, and alone at the top
};

// Which core a task is pinned to, or kAnyCore for the scheduler's choice.
//
// The WiFi and lwIP stacks run on core 0 (PRO_CPU) under Arduino-ESP32. lora_task
// is pinned to core 1 so that a busy network stack cannot delay the one task that
// must not be delayed - which is the same asymmetry Bridge PRD 4.4 records at the
// radio level and M22 is the measurement for. If M22 shows LoRa PER degrading with
// WiFi saturated, this pinning is one of the two levers (the other is antenna
// separation, PRD 4.4); it is not the fix for a design that publishes inline.
inline constexpr int kCore0    = 0;
inline constexpr int kCore1    = 1;
inline constexpr int kAnyCore  = -1;  // tskNO_AFFINITY at the call site

struct TaskSpec {
  TaskId      id;
  const char* name;         // FreeRTOS task name; appears in a panic backtrace
  uint8_t     priority;
  // BYTES. ESP-IDF's FreeRTOS takes the depth in bytes and StackType_t is uint8_t on
  // the ESP32-S3 (portmacro.h). This field was `stack_words` until BF-16 found the unit
  // wrong: upstream FreeRTOS counts words, ESP-IDF does not.
  uint32_t    stack_bytes;
  int         core;

  // 0 for a task that waits on a queue rather than a tick. Impl Plan 5.2's
  // "Trigger" column, made explicit: a period and a queue wait are different
  // shapes, and a task with both is usually a task doing two jobs.
  uint32_t period_ms;
};

// The table. Definition in tasks.cpp; kTaskCount entries, indexed by TaskId.
const TaskSpec& task_spec(TaskId id);
const TaskSpec* task_table();

// ---------------------------------------------------------------------------
// The invariants. Impl Plan 5.2's rules, written so a test can fail.
// ---------------------------------------------------------------------------

// "lora_task is highest priority" - and STRICTLY highest. A tie means the frame
// path can be made to wait for whatever it tied with, which is the property this
// rule exists to deny.
bool lora_is_strictly_highest();

// log_task is strictly lowest: leveled logging is never worth delaying anything,
// and a log that can preempt the radio is a log that changes what it measures.
bool log_is_strictly_lowest();

// Every priority sits at or above the Arduino loop task, so nothing here is
// starved by a `loop()` that never yields.
bool all_priorities_above_arduino_loop();

// Names are unique - they are what a panic backtrace prints.
bool task_names_are_unique();

// ---------------------------------------------------------------------------
// Queues. Depths are here rather than at the creation site so the sizing argument
// has one home, and so the native tests can reason about them.
// ---------------------------------------------------------------------------

// lora_task -> app_task. A received frame, copied out of the radio buffer.
//
// Depth 8 against a fleet whose whole design cadence is one poll outstanding
// (Impl Plan 6.1) and per-node status at 1-5 minutes: eight queued frames is
// already an app_task that has not run for many seconds. It is sized to absorb a
// burst, not to buffer an outage.
inline constexpr size_t kRxQueueDepth = 8;

// app_task -> mqtt_task. Publication is queued, never inline (Impl Plan 5.2).
//
// Deeper than the RX queue because one received frame fans out into many topics -
// a GateLink status is a dozen entities - and because this is the queue that has to
// ride out a broker reconnect without stalling anything upstream of it.
inline constexpr size_t kPublishQueueDepth = 32;

// anything -> lora_task. The TX side: polls, commands, ACKs.
//
// Shallow on purpose. The bridge serializes polls fleet-wide (Impl Plan 6.1,
// R-3.1d), so a deep TX queue would mean something upstream has stopped honouring
// that, and a queue is the wrong place to discover it.
inline constexpr size_t kTxQueueDepth = 4;

// anything -> log_task.
inline constexpr size_t kLogQueueDepth = 16;

// mqtt_task -> sched_task. Commands from Home Assistant (BF-18).
//
// Depth 4, and it is not a buffer. The command path runs ONE command at a time
// across the fleet (command.h), so anything queued behind the first is already
// waiting on a 3-10 second exchange. Four is enough that a dashboard button pressed
// twice, or two entities toggled together, are not lost between sched_task ticks -
// and shallow enough that a stuck command path shows up as a counted drop within
// seconds rather than as a gate that opens a minute after the button was pressed.
inline constexpr size_t kCommandQueueDepth = 4;

}  // namespace bridge
