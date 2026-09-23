// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-23's lever half. levers.h says why the values travel this way.

#include "levers.h"

#include "lran/config/table.h"

namespace bridge {

namespace {

constexpr bool name_equal(const char* a, const char* b) {
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

// A bridge row's id, looked up by its name at compile time. The name is the permanent
// half of a row (table.h), so the static_asserts below fail the build if a row a lever
// reads is renamed or removed, rather than letting that lever read 0.
constexpr uint16_t kNoRow = 0xFFFF;

constexpr uint16_t bridge_row(const char* name) {
  for (size_t i = 0; i < lran::config::kBridgeParamCount; ++i) {
    if (name_equal(lran::config::kBridgeParams[i].name, name)) {
      return lran::config::kBridgeParams[i].id;
    }
  }
  return kNoRow;
}

constexpr uint16_t kDiagIntervalS         = bridge_row("diag_interval_s");
constexpr uint16_t kMissedPollThreshold   = bridge_row("missed_poll_threshold");
constexpr uint16_t kPollReplyTimeoutMs    = bridge_row("poll_reply_timeout_ms");
constexpr uint16_t kCommandAckTimeoutMs   = bridge_row("command_ack_timeout_ms");
constexpr uint16_t kCmdRetries            = bridge_row("cmd_retries");
constexpr uint16_t kCadRetries            = bridge_row("cad_retries");
constexpr uint16_t kBackoffMaxMs          = bridge_row("backoff_max_ms");
constexpr uint16_t kFragReassemblyTimeout = bridge_row("frag_reassembly_timeout_ms");
constexpr uint16_t kErrorMinIntervalMs    = bridge_row("error_min_interval_ms");
constexpr uint16_t kConfigReadbackTimeout = bridge_row("config_readback_timeout_ms");
constexpr uint16_t kConfigAckTimeoutMs    = bridge_row("config_ack_timeout_ms");
constexpr uint16_t kPollIntervalS         = bridge_row("poll_interval_s");

static_assert(kDiagIntervalS != kNoRow && kMissedPollThreshold != kNoRow &&
                  kPollReplyTimeoutMs != kNoRow && kCommandAckTimeoutMs != kNoRow &&
                  kCmdRetries != kNoRow && kCadRetries != kNoRow &&
                  kBackoffMaxMs != kNoRow && kFragReassemblyTimeout != kNoRow &&
                  kErrorMinIntervalMs != kNoRow && kConfigReadbackTimeout != kNoRow &&
                  kConfigAckTimeoutMs != kNoRow && kPollIntervalS != kNoRow,
              "every lever reads a row of kBridgeParams");

// `cad_retries`, `backoff_max_ms` and `frag_reassembly_timeout_ms` are in the node block
// too (config_store.h). bridge_row() searches kBridgeParams alone, so it cannot return
// the node's row, and this says the ids are the bridge's own.
static_assert(kCadRetries < 0x0100 && kBackoffMaxMs < 0x0100 &&
                  kFragReassemblyTimeout < 0x0100,
              "the bridge's radio levers are the bridge's rows, not a node's");

}  // namespace

Levers levers_from(const ConfigStore& store) {
  Levers v;
  v.diag_interval_s            = static_cast<uint16_t>(store.global_value(kDiagIntervalS));
  v.missed_poll_threshold      = static_cast<uint16_t>(store.global_value(kMissedPollThreshold));
  v.poll_reply_timeout_ms      = static_cast<uint32_t>(store.global_value(kPollReplyTimeoutMs));
  v.command_ack_timeout_ms     = static_cast<uint32_t>(store.global_value(kCommandAckTimeoutMs));
  v.cmd_retries                = static_cast<uint8_t>(store.global_value(kCmdRetries));
  v.cad_retries                = static_cast<uint8_t>(store.global_value(kCadRetries));
  v.backoff_max_ms             = static_cast<uint32_t>(store.global_value(kBackoffMaxMs));
  v.frag_reassembly_timeout_ms = static_cast<uint32_t>(store.global_value(kFragReassemblyTimeout));
  v.error_min_interval_ms      = static_cast<uint32_t>(store.global_value(kErrorMinIntervalMs));
  v.config_readback_timeout_ms = static_cast<uint32_t>(store.global_value(kConfigReadbackTimeout));
  v.config_ack_timeout_ms      = static_cast<uint32_t>(store.global_value(kConfigAckTimeoutMs));
  for (size_t i = 0; i < kNodeCount; ++i) {
    v.poll_interval_s[i] =
        static_cast<uint16_t>(store.node_value(kNodeTable[i].id, kPollIntervalS));
  }
  return v;
}

void LeverBoard::publish(const Levers& v) {
  gen_.fetch_add(1, std::memory_order_relaxed);  // odd: a publish is under way
  std::atomic_thread_fence(std::memory_order_release);

  constexpr auto r = std::memory_order_relaxed;
  diag_interval_s_.store(v.diag_interval_s, r);
  missed_poll_threshold_.store(v.missed_poll_threshold, r);
  poll_reply_timeout_ms_.store(v.poll_reply_timeout_ms, r);
  command_ack_timeout_ms_.store(v.command_ack_timeout_ms, r);
  cmd_retries_.store(v.cmd_retries, r);
  cad_retries_.store(v.cad_retries, r);
  backoff_max_ms_.store(v.backoff_max_ms, r);
  frag_reassembly_timeout_ms_.store(v.frag_reassembly_timeout_ms, r);
  error_min_interval_ms_.store(v.error_min_interval_ms, r);
  config_readback_timeout_ms_.store(v.config_readback_timeout_ms, r);
  config_ack_timeout_ms_.store(v.config_ack_timeout_ms, r);
  for (size_t i = 0; i < kNodeCount; ++i) poll_interval_s_[i].store(v.poll_interval_s[i], r);

  gen_.fetch_add(1, std::memory_order_release);  // even: complete
}

bool LeverBoard::take_if_changed(uint32_t* seen, Levers* out) const {
  const uint32_t before = gen_.load(std::memory_order_acquire);
  if ((before & 1u) != 0 || before == *seen) return false;

  constexpr auto r = std::memory_order_relaxed;
  Levers v;
  v.diag_interval_s            = diag_interval_s_.load(r);
  v.missed_poll_threshold      = missed_poll_threshold_.load(r);
  v.poll_reply_timeout_ms      = poll_reply_timeout_ms_.load(r);
  v.command_ack_timeout_ms     = command_ack_timeout_ms_.load(r);
  v.cmd_retries                = cmd_retries_.load(r);
  v.cad_retries                = cad_retries_.load(r);
  v.backoff_max_ms             = backoff_max_ms_.load(r);
  v.frag_reassembly_timeout_ms = frag_reassembly_timeout_ms_.load(r);
  v.error_min_interval_ms      = error_min_interval_ms_.load(r);
  v.config_readback_timeout_ms = config_readback_timeout_ms_.load(r);
  v.config_ack_timeout_ms      = config_ack_timeout_ms_.load(r);
  for (size_t i = 0; i < kNodeCount; ++i) v.poll_interval_s[i] = poll_interval_s_[i].load(r);

  std::atomic_thread_fence(std::memory_order_acquire);
  if (gen_.load(r) != before) return false;

  *out  = v;
  *seen = before;
  return true;
}

}  // namespace bridge
