// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The task table and its invariants. Task BF-11; Impl Plan 5.2.

#include "tasks.h"

#include <cstring>

namespace bridge {
namespace {

// Stack sizes are BYTES, the unit ESP-IDF's xTaskCreateStaticPinnedToCore takes;
// StackType_t is uint8_t on the ESP32-S3. BF-11 wrote these believing they were
// 4-byte words, so every task had a quarter of the stack it was sized for - found
// in BF-16, engineering log 2026-09-13. They are starting points, and the way to
// correct one is uxTaskGetStackHighWaterMark, reported through the diagnostic topics -
// not a guess doubled after a crash.
//
// mqtt_task and app_task are the deep ones because ArduinoJson serializes a
// discovery config on their stacks; a Discovery payload is the largest single thing
// this firmware builds, which is also why MQTT_MAX_PACKET_SIZE is 1024 (D5).
constexpr TaskSpec kTable[kTaskCount] = {
    // Highest, and never blocks on the network. Owns RadioLib, the frame codec,
    // MAC verification and reassembly.
    //
    // 8192, raised from 4096 in BF-16: this task runs RadioLib's begin() and a
    // Serial.printf of the configured PHY, and 4096 was a quarter of what BF-11
    // intended. lora_link.cpp logs the high-water mark after bring-up; that number,
    // not this comment, is what says whether 8192 is enough.
    {TaskId::Lora, "lora", kPriorityLora, 8192, kCore1, 0},

    // 1 s tick: per-node poll scheduling, retry and backoff, the availability
    // watchdog. Feeds the hardware watchdog (Impl Plan 5.2).
    {TaskId::Sched, "sched", kPriorityHigh, 3072, kCore1, 1000},

    // 100 ms tick plus its queue: broker connection, publish queue, subscription
    // dispatch, discovery. Core 0, with the WiFi and lwIP stacks it talks to.
    {TaskId::Mqtt, "mqtt", kPriorityNormal, 6144, kCore0, 100},

    // Queue-driven: decode per schema, publication policy, event dedup, HEX proxy
    // authorization.
    {TaskId::App, "app", kPriorityNormal, 6144, kAnyCore, 0},

    // On request, and deferred until lora_task reports idle (R-5.3d).
    {TaskId::Ota, "ota", kPriorityLow, 4096, kAnyCore, 0},

    // 500 ms tick: the OLED status page.
    {TaskId::Ui, "ui", kPriorityLow, 3072, kAnyCore, 500},

    // Lowest: leveled serial log and the raw frame log.
    {TaskId::Log, "log", kPriorityLog, 3072, kAnyCore, 0},
};

static_assert(sizeof(kTable) / sizeof(kTable[0]) == kTaskCount,
              "every TaskId needs a row - see Impl Plan 5.2");

}  // namespace

const TaskSpec& task_spec(TaskId id) { return kTable[static_cast<size_t>(id)]; }

const TaskSpec* task_table() { return kTable; }

bool lora_is_strictly_highest() {
  const uint8_t lora = task_spec(TaskId::Lora).priority;
  for (size_t i = 0; i < kTaskCount; ++i) {
    if (kTable[i].id == TaskId::Lora) {
      continue;
    }
    if (kTable[i].priority >= lora) {
      return false;
    }
  }
  return true;
}

bool log_is_strictly_lowest() {
  const uint8_t log = task_spec(TaskId::Log).priority;
  for (size_t i = 0; i < kTaskCount; ++i) {
    if (kTable[i].id == TaskId::Log) {
      continue;
    }
    if (kTable[i].priority <= log) {
      return false;
    }
  }
  return true;
}

bool all_priorities_above_arduino_loop() {
  // The Arduino `loopTask` runs at priority 1. Equal is acceptable - the scheduler
  // round-robins peers - but below it is starvation by a `loop()` that never yields.
  for (size_t i = 0; i < kTaskCount; ++i) {
    if (kTable[i].priority < 1) {
      return false;
    }
  }
  return true;
}

bool task_names_are_unique() {
  for (size_t i = 0; i < kTaskCount; ++i) {
    for (size_t j = i + 1; j < kTaskCount; ++j) {
      if (std::strcmp(kTable[i].name, kTable[j].name) == 0) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace bridge
