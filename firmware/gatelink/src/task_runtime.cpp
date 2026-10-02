// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task creation and the task bodies. Task L6; GateLink Impl Plan 5.2.
//
// EVERY BODY IS A STUB. Each counts its passes and waits out its period, so a bench session
// can see seven tasks scheduled at their priorities before any of them does real work. The
// milestone that fills a body is named at it.
//
// THE WATCHDOG IS NOT ARMED HERE. Impl Plan 5.2 feeds it from app_task, and the timeout is a
// timing constant on a node with no OTA (root rule 8). The bridge fixed its own at 10 s on
// the strength of having OTA (bridge tasks.h), an argument GateLink cannot borrow. Arming it
// waits on that decision, at GL3. Arduino-ESP32's default watchdog still watches the idle
// task on core 0.

#include "task_runtime.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdio>

namespace gatelink {
namespace {

std::atomic<uint32_t> g_passes[kTaskCount];

StaticTask_t g_tcb[kTaskCount];
StackType_t  g_stack_io[4096];
StackType_t  g_stack_vedirect[4096];
StackType_t  g_stack_lora[8192];
StackType_t  g_stack_app[6144];
StackType_t  g_stack_bms[6144];
StackType_t  g_stack_ui[6144];
StackType_t  g_stack_log[4096];

// How often log_task prints the pass counts.
constexpr uint32_t kAliveReportMs = 30000;

void count(TaskId id) { g_passes[static_cast<size_t>(id)].fetch_add(1, std::memory_order_relaxed); }

TickType_t period_ticks(TaskId id) {
  return pdMS_TO_TICKS(default_period_ms(task_spec(id)));
}

// R-5.2a - io_task never blocks on anything but its own period, and vTaskDelayUntil keeps
// that period from drifting by the length of a pass. tools/checks/io_task_never_blocks.py
// reads this function. GL1 adds the input poll and the relay pulse, and the Store's
// input_poll_ms replaces the table default.
void io_task(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    count(TaskId::Io);
    vTaskDelayUntil(&last, period_ticks(TaskId::Io));
  }
}

// GL4 - waits on UART RX events instead of a second.
void vedirect_task(void*) {
  for (;;) {
    count(TaskId::Vedirect);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// GL3 - waits on the DIO1 notification and the TX queue instead of a second.
void lora_task(void*) {
  for (;;) {
    count(TaskId::Lora);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// GL3 - state derivation, triggers and status; feeds the watchdog once it is armed.
void app_task(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    count(TaskId::App);
    vTaskDelayUntil(&last, period_ticks(TaskId::App));
  }
}

// GL5 - one BMS read per bms_poll_s, under the LoRa/BLE interlock (R-4.3h).
void bms_task(void*) {
  for (;;) {
    count(TaskId::Bms);
    vTaskDelay(period_ticks(TaskId::Bms));
  }
}

// GL1 - the panel pages, under the SPI lock. Until then main.cpp draws the boot page once,
// before any task starts, so nothing shares the bus.
void ui_task(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    count(TaskId::Ui);
    vTaskDelayUntil(&last, period_ticks(TaskId::Ui));
  }
}

// GL1 - a queue of leveled lines and the microSD log. Today it prints the pass counts, the
// only task that writes to Serial: a full USB CDC buffer blocks the writer, and no other
// task may be that writer.
//
// The line is built whole and written once. Arduino-ESP32 2.0.17's USB-serial driver lost
// bytes from every line written as eight printf calls (bench, 2026-10-02). The first report
// waits a period, so it cannot interleave with setup()'s last banner line.
void log_task(void*) {
  char line[160];
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(kAliveReportMs));
    count(TaskId::Log);
    size_t n = static_cast<size_t>(std::snprintf(line, sizeof(line), "alive:"));
    for (size_t i = 0; i < kTaskCount && n < sizeof(line); ++i) {
      n += static_cast<size_t>(std::snprintf(
          line + n, sizeof(line) - n, " %s=%lu", task_table()[i].name,
          static_cast<unsigned long>(g_passes[i].load(std::memory_order_relaxed))));
    }
    if (n > sizeof(line) - 2) n = sizeof(line) - 2;  // a cut line still ends in a newline
    line[n++] = '\n';
    Serial.write(reinterpret_cast<const uint8_t*>(line), n);
  }
}

struct Slot {
  TaskFunction_t body;
  StackType_t*   stack;
  size_t         stack_bytes;
};

// Indexed by TaskId. start_tasks() refuses a row whose stack here differs from the table's
// size, and the banner's task count then shows the shortfall.
const Slot kSlots[kTaskCount] = {
    {io_task, g_stack_io, sizeof(g_stack_io)},
    {vedirect_task, g_stack_vedirect, sizeof(g_stack_vedirect)},
    {lora_task, g_stack_lora, sizeof(g_stack_lora)},
    {app_task, g_stack_app, sizeof(g_stack_app)},
    {bms_task, g_stack_bms, sizeof(g_stack_bms)},
    {ui_task, g_stack_ui, sizeof(g_stack_ui)},
    {log_task, g_stack_log, sizeof(g_stack_log)},
};

static_assert(sizeof(StackType_t) == 1, "ESP-IDF counts stack depth in bytes (tasks.h)");

}  // namespace

size_t start_tasks() {
  size_t started = 0;
  for (size_t i = 0; i < kTaskCount; ++i) {
    const TaskSpec& spec = task_table()[i];
    if (kSlots[i].stack_bytes != spec.stack_bytes) continue;  // a table row and a stack disagree
    TaskHandle_t h = xTaskCreateStaticPinnedToCore(
        kSlots[i].body, spec.name, spec.stack_bytes, nullptr, spec.priority, kSlots[i].stack,
        &g_tcb[i], spec.core == kAnyCore ? tskNO_AFFINITY : spec.core);
    if (h != nullptr) ++started;
  }
  return started;
}

uint32_t task_passes(TaskId id) {
  return g_passes[static_cast<size_t>(id)].load(std::memory_order_relaxed);
}

}  // namespace gatelink
