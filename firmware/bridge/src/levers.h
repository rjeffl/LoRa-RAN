// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The runtime timing levers: what ConfigStore holds, carried to the task that applies it.
// Task BF-23, its lever half; root rule 8; spec 16.7.1; D47.
//
// ARDUINO-FREE, AND IT DOES NO I/O. levers_from() reads a ConfigStore into one plain
// struct; LeverBoard carries that struct from the task that wrote the store to the tasks
// that own each consumer. Applying a value is each owning task's job, in
// task_runtime.cpp.
//
// WHY A BOARD AND NOT A READ OF THE STORE. mqtt_task writes ConfigStore when a set
// arrives. The consumers belong to sched_task and lora_task, and lora_task must never
// wait on a lock (tools/checks/lora_task_never_blocks.py). A Store has no lock to take.
// So mqtt_task reads its own store, and publishes the result here as atomics. A task
// that sees a new generation copies the values and applies them on its own stack.
//
// ONE WRITER. The board is published from setup(), before start_tasks(), and after that
// from mqtt_task alone. Two writers could interleave field stores under one generation,
// and a reader would take a mix of two sets that it would not notice.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "config_store.h"
#include "registry.h"

namespace bridge {

// Every row of the table that a bridge task applies, as its consumer takes it.
//
// `simnode_diag_enable` is absent on purpose. It is not timing, and gating on it is
// BF-26's to build.
struct Levers {
  uint16_t diag_interval_s            = 0;
  uint16_t missed_poll_threshold      = 0;
  uint32_t poll_reply_timeout_ms      = 0;
  uint32_t command_ack_timeout_ms     = 0;
  uint8_t  cmd_retries                = 0;
  uint8_t  cad_retries                = 0;
  uint32_t backoff_max_ms             = 0;
  uint32_t frag_reassembly_timeout_ms = 0;
  uint32_t error_min_interval_ms      = 0;
  uint32_t config_readback_timeout_ms = 0;
  uint32_t config_ack_timeout_ms      = 0;

  // D47 - one value per node, in kNodeTable's order.
  uint16_t poll_interval_s[kNodeCount] = {};
};

// The effective value of every lever: an override where one is set, the table's default
// where none is. ConfigStore has already clamped every value to its row's range, so
// nothing here re-checks one.
Levers levers_from(const ConfigStore& store);

// The carrier between tasks. Lock-free and wait-free on both sides, so lora_task may read
// it.
class LeverBoard {
 public:
  // Marks the generation odd, stores every field, then makes it even again. The first
  // publish leaves it at 2, so a reader that starts at 0 applies it on its first look.
  void publish(const Levers& v);

  // True when the board carries a generation other than `*seen`, and a consistent copy of
  // it was taken into `*out`. Then `*seen` moves to that generation.
  //
  // A publish that lands mid-copy fails the generation check: the generation is odd, or
  // it moved between the first read and the last. The reader returns false,
  // leaves `*seen` where it was, and takes the new values on its next look. It never
  // applies a mix of two publishes.
  bool take_if_changed(uint32_t* seen, Levers* out) const;

  uint32_t generation() const { return gen_.load(std::memory_order_acquire); }

 private:
  std::atomic<uint32_t> gen_{0};

  std::atomic<uint16_t> diag_interval_s_{0};
  std::atomic<uint16_t> missed_poll_threshold_{0};
  std::atomic<uint32_t> poll_reply_timeout_ms_{0};
  std::atomic<uint32_t> command_ack_timeout_ms_{0};
  std::atomic<uint8_t>  cmd_retries_{0};
  std::atomic<uint8_t>  cad_retries_{0};
  std::atomic<uint32_t> backoff_max_ms_{0};
  std::atomic<uint32_t> frag_reassembly_timeout_ms_{0};
  std::atomic<uint32_t> error_min_interval_ms_{0};
  std::atomic<uint32_t> config_readback_timeout_ms_{0};
  std::atomic<uint32_t> config_ack_timeout_ms_{0};
  std::atomic<uint16_t> poll_interval_s_[kNodeCount] = {};
};

}  // namespace bridge
