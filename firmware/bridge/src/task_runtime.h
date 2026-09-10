// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task and queue creation. Task BF-11; Impl Plan 5.2.
//
// THE ARDUINO SIDE. tasks.h and queues.h hold the table, the depths and the
// accounting, all host-testable; everything FreeRTOS touches is here and in
// task_runtime.cpp, which the `native` environment does not build.

#pragma once

#include "queues.h"
#include "tasks.h"

namespace bridge {

// Creates the four queues and starts the seven tasks, in that order. Returns false
// if any creation failed, which on static allocation means a table defect rather
// than a runtime condition - a bad depth, or storage that does not match the item
// size. Boot stops rather than running a fleet with a missing task.
bool start_tasks();

// Queue sends. EVERY ONE OF THESE IS NON-BLOCKING and returns false when the queue
// was full, having counted the drop.
//
// There is no blocking variant, and that absence is the design: a blocking send is
// how a stalled consumer reaches back and stops lora_task, which is the one thing
// Impl Plan 5.2 and PRD 1.3 property 2 forbid. tools/checks/lora_task_never_blocks.py
// fails the build's checks if a blocking primitive appears on the LoRa path.
bool send_rx(const RxMessage& msg);
bool send_tx(const TxMessage& msg);

// Publication and logging carry their own payload types, which BF-12 and BF-24
// define. The queues exist now because their DEPTHS and their drop accounting are
// part of the task structure, and retrofitting a queue boundary is the edit this
// task exists to avoid.
//
// TODO(BF-12): PublishMessage, and mqtt_task's consumption of it.
// TODO(BF-24): the publication policy that decides what reaches that queue.

// Whether lora_task is idle. ota_task defers until it is (R-5.3d).
//
// TODO(BF-16): report the radio's real state. Returning true unconditionally is
// honest for a build with no radio, and it is why BF-13's OTA must not be written
// against this until lora_link exists.
bool lora_task_idle();

// The per-queue counters, for the diagnostic topics and the OLED page.
const QueueAccounting& queue_accounting();

}  // namespace bridge
