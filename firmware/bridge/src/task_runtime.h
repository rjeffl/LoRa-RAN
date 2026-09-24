// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task and queue creation. Task BF-11; Impl Plan 5.2.
//
// THE ARDUINO SIDE. tasks.h and queues.h hold the table, the depths and the
// accounting, all host-testable; everything FreeRTOS touches is here and in
// task_runtime.cpp, which the `native` environment does not build.

#pragma once

#include "mqtt_transport.h"
#include "queues.h"
#include "tasks.h"

namespace bridge {

// How long lora_task waits for DIO1 before its next pass. It bounds how long a frame
// queued by another task waits to be picked up, and it is the resolution of a spec 12.3
// backoff; a reception wakes the task at once regardless.
//
// IN THE HEADER SINCE M25, because it also sets the channel sampler's rate - lora_task
// takes one RSSI reading per wake, so ~100 a second - and chan_monitor.h's reasoning
// about what a long capture can and cannot see rests on this number. Changing it changes
// both, and the second one silently.
inline constexpr uint32_t kLoraMaxWaitMs = 10;

// Creates the four queues and starts the seven tasks, in that order. Returns false
// if any creation failed, which on static allocation means a table defect rather
// than a runtime condition - a bad depth, or storage that does not match the item
// size. Boot stops rather than running a fleet with a missing task.
bool start_tasks();

// BF-32 - opens the configuration store and replays what NVS holds. Call before
// start_tasks(), like registry_begin(): mqtt_task answers `config/set` from it. Returns
// the number of stored values put back, and never fails the boot - a store that will not
// open leaves every parameter at its default and every set APPLIED_NOT_PERSISTED.
size_t config_begin();

// Queue sends. EVERY ONE OF THESE IS NON-BLOCKING and returns false when the queue
// was full, having counted the drop.
//
// There is no blocking variant, and that absence is the design: a blocking send is
// how a stalled consumer reaches back and stops lora_task, which is the one thing
// Impl Plan 5.2 and PRD 1.3 property 2 forbid. tools/checks/lora_task_never_blocks.py
// fails the build's checks if a blocking primitive appears on the LoRa path.
bool send_rx(const RxMessage& msg);
bool send_tx(const TxMessage& msg);

// lora_task's side of the TX queue. Zero ticks, like every send: true when a frame was
// waiting and has been copied into *out.
bool take_tx(TxMessage* out);

// Queue a publication for mqtt_task. Build the message with make_publish(), which
// refuses an oversized payload, a retained event topic (spec 16.3) and a bench node's
// production topic (spec 16.6). BF-24's policy (publish.h) decides what reaches it.
bool send_publish(const PublishMessage& msg);

// Start WiFi and the broker client. Called from setup() with the values from
// secrets.h, which main.cpp is the only translation unit to see.
//
// Neither connects here: association and the broker handshake happen in mqtt_task,
// on the backoff in net_policy.h. Boot does not wait for a network.
bool net_begin(const char* ssid, const char* wifi_password, const char* mqtt_host,
               uint16_t mqtt_port, const char* mqtt_user, const char* mqtt_password);

// The transport, behind its seam. For diagnostics and for the tasks that publish;
// nothing outside task_runtime.cpp should know which implementation D5 chose.
MqttTransport& mqtt();

// Whether lora_task is idle. ota_task defers until it is (R-5.3d).
//
// BF-16: no frame waiting or on the air, the radio receiving, and no reassembly set
// incomplete. BF-17: no poll awaiting its reply. TODO(BF-18): no command awaiting its
// COMMAND_ACK either.
bool lora_task_idle();

// BF-27's dummy publish. One line from the USB serial console, without its line ending; a
// `dummy` line is answered on the console and may queue a STATUS or EVENT for app_task
// (dummy.h). Any other line is ignored. Called from loop(), the lowest priority there is.
void console_line(const char* line);

// The per-queue counters, for the diagnostic topics and the OLED page.
const QueueAccounting& queue_accounting();

}  // namespace bridge
