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

// Passes each task has completed since boot. log_task prints them, which is how a bench
// session sees that every stub is being scheduled.
uint32_t task_passes(TaskId id);

}  // namespace gatelink
