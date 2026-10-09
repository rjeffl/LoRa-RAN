// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The FreeRTOS half of the task table: creation, and each task's body. Task L6; GateLink
// Impl Plan 5.2. Arduino-only; tasks.h holds the half the native build tests.

#pragma once

#include <cstddef>
#include <cstdint>

#include "tasks.h"
#include "ui_pages.h"

namespace gatelink {

// Creates every task in tasks.h's table from static storage (root rule 3). Returns the
// number started, which is kTaskCount unless a creation failed. ui_task draws boot_page
// again after anything else has used the whole panel. lora_task copies the node key into
// its context and never prints it.
size_t start_tasks(const PageText& boot_page, const uint8_t* node_key, size_t node_key_len);

// Sets the task watchdog's timeout to watchdog_timeout_s, held to its row's range. start_tasks()
// calls it with the default before any task starts; the CONFIG path calls it again when
// the parameter is set (Impl Plan 5.2, decided 2026-10-07). ESP-IDF 4.4's init reconfigures
// a watchdog already running. Returns false if ESP-IDF refused, and the old timeout stands.
bool apply_watchdog_timeout(uint32_t seconds);

// The timeout in force, in seconds, or 0 if the watchdog never armed.
uint32_t watchdog_timeout_s();

// Passes each task has completed since boot. log_task prints them, which is how a bench
// session sees that every stub is being scheduled.
uint32_t task_passes(TaskId id);

}  // namespace gatelink
