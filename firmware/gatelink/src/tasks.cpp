// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The task table and its invariants. Task L6; GateLink Impl Plan 5.2.

#include "tasks.h"

#include <cstring>

#include "lran/config/table.h"

namespace gatelink {
namespace {

// Stack sizes are first guesses, in bytes. uxTaskGetStackHighWaterMark corrects them once
// each task does real work; the bridge's sched_task shows why a measured figure replaces a
// guess (bridge tasks.cpp, BF-32).
constexpr TaskSpec kTable[kTaskCount] = {
    // Input polling, debounce and relay pulse timing. The only task that touches the
    // AW9523B (Impl Plan 5.2). Its period is input_poll_ms.
    {TaskId::Io, "io", kPriorityIo, 4096, kAnyCore, 0, 0x1010},

    // The VE.Direct line parser and the HEX transaction. Driven by UART RX events from GL4.
    {TaskId::Vedirect, "vedirect", kPriorityHigh, 4096, kAnyCore, 0, 0},

    // The radio, media access and lran-node's receive path. Driven by DIO1 and the TX
    // queue from GL3. 8192 is the bridge's lora_task stack, which runs the same RadioLib.
    {TaskId::Lora, "lora", kPriorityHigh, 8192, kAnyCore, 0, 0},

    // State, hold tracking, detection, triggers and status assembly on a 100 ms tick. GL3
    // makes it the task that feeds the watchdog (Impl Plan 5.2).
    {TaskId::App, "app", kPriorityNormal, 6144, kAnyCore, 100, 0},

    // NimBLE connect, read, disconnect and controller de-init. Its period is bms_poll_s.
    {TaskId::Bms, "bms", kPriorityLow, 6144, kAnyCore, 0, 0x1040},

    // LCD pages, buttons, backlight and buzzer on a 100 ms tick. M5GFX draws on this stack.
    {TaskId::Ui, "ui", kPriorityLow, 6144, kAnyCore, 100, 0},

    // Leveled serial log, and the microSD log from GL1.
    {TaskId::Log, "log", kPriorityLog, 4096, kAnyCore, 0, 0},
};

static_assert(sizeof(kTable) / sizeof(kTable[0]) == kTaskCount,
              "every TaskId needs a row - see Impl Plan 5.2");

const lran::config::ParamDef* find_param(uint16_t id) {
  for (const auto& p : lran::config::kGateLinkParams) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

}  // namespace

const TaskSpec& task_spec(TaskId id) { return kTable[static_cast<size_t>(id)]; }

const TaskSpec* task_table() { return kTable; }

uint32_t default_period_ms(const TaskSpec& spec) {
  if (spec.period_param == 0) return spec.period_ms;
  const lran::config::ParamDef* p = find_param(spec.period_param);
  if (p == nullptr) return 0;
  // The unit string decides the scale. An "s" row is seconds; every other period row is ms.
  const bool seconds = p->unit != nullptr && std::strcmp(p->unit, "s") == 0;
  return static_cast<uint32_t>(p->def) * (seconds ? 1000u : 1u);
}

bool io_is_strictly_highest() {
  const uint8_t io = task_spec(TaskId::Io).priority;
  for (const auto& t : kTable) {
    if (t.id != TaskId::Io && t.priority >= io) return false;
  }
  return true;
}

bool log_is_strictly_lowest() {
  const uint8_t log = task_spec(TaskId::Log).priority;
  for (const auto& t : kTable) {
    if (t.id != TaskId::Log && t.priority <= log) return false;
  }
  return true;
}

bool all_priorities_above_arduino_loop() {
  for (const auto& t : kTable) {
    if (t.priority < 1) return false;
  }
  return true;
}

bool task_names_are_unique() {
  for (size_t i = 0; i < kTaskCount; ++i) {
    for (size_t j = i + 1; j < kTaskCount; ++j) {
      if (std::strcmp(kTable[i].name, kTable[j].name) == 0) return false;
    }
  }
  return true;
}

bool period_params_exist() {
  for (const auto& t : kTable) {
    if (t.period_param != 0 && find_param(t.period_param) == nullptr) return false;
  }
  return true;
}

}  // namespace gatelink
