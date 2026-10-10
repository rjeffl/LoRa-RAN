// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// lora_task's half of the LoRa/BLE interlock (PRD R-4.3h, Impl Plan 5.2). bms_task holds it
// from controller start to de-init; the radio takes it before a frame's media access starts
// and gives it back when the frame is sent or dropped. task_runtime.cpp holds the lock, and
// bms_link.h's tx_gate() makes the decision.

#pragma once

#include <cstddef>
#include <cstdint>

namespace gatelink {

// True when the radio may start media access for `frame`. False means wait a pass: bms_task
// holds the interlock, and if the bridge is waiting on this frame, bms_task has been asked
// to end its window. Past bms_window_max_ms it returns true without the lock, and counts
// the overrun. Called on lora_task only.
bool ble_interlock_tx_take(const uint8_t* frame, size_t len, uint32_t now_ms);

// Gives the interlock back if the radio holds it. Called on lora_task only.
void ble_interlock_tx_release();

}  // namespace gatelink
