// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The diagnostic documents. Task BF-19.

#include "diag_json.h"

#include "json_writer.h"

namespace bridge {
namespace {

// queues.h's QueueId order.
constexpr const char* kQueueKeys[][2] = {
    {"q_rx_dropped", "q_rx_high_water"},
    {"q_publish_dropped", "q_publish_high_water"},
    {"q_tx_dropped", "q_tx_high_water"},
    {"q_log_dropped", "q_log_high_water"},
    {"q_command_dropped", "q_command_high_water"},
    {"q_config_dropped", "q_config_high_water"},
};
static_assert(sizeof(kQueueKeys) / sizeof(kQueueKeys[0]) == kQueueCount,
              "a queue added to QueueId needs its diagnostic keys");

}  // namespace

size_t diag_rx_json(const lran::Counters& c, const RollStats& roll, char* out, size_t cap) {
  JsonObject j(out, cap);
  for (const lran::CounterField& f : lran::kCounterRegistry) j.u32(f.name, c.*(f.field));
  j.u32("rx_dropped", c.total_dropped());
  j.u32("rx_frames", c.rx_frames);
  // spec 14.1's bridge counters. Spelled here because they are not in kCounterRegistry:
  // no node counts them, and the registry is what a node's schema 0xF0 is built from.
  j.u32("ctx_rolls", roll.ctx_rolls);
  j.u32("ctx_roll_failed", roll.ctx_roll_failed);
  return j.finish();
}

size_t diag_command_json(const CommandStats& s, const RollStats& roll, char* out, size_t cap) {
  JsonObject j(out, cap);
  j.u32("cmd_submitted", s.submitted);
  j.u32("cmd_refused_busy", s.refused_busy);
  j.u32("cmd_sent", s.sent);
  j.u32("cmd_retries", s.retries);
  j.u32("cmd_acked", s.acked);
  j.u32("cmd_no_ack", s.no_ack);
  j.u32("cmd_resyncs", s.resyncs);
  j.u32("cmd_resync_failed", s.resync_failed);
  j.u32("cmd_ack_ignored", s.ack_ignored);
  j.u32("cmd_refused_roll_pending", roll.cmd_refused);
  j.u32("roll_sent", roll.sent);
  j.u32("roll_retries", roll.retries);
  j.u32("roll_busy", roll.busy);
  j.u32("roll_by_rejected_ctx", roll.by_rejected_ctx);
  return j.finish();
}

size_t diag_radio_json(const RadioDiag& r, char* out, size_t cap) {
  JsonObject j(out, cap);
  j.u32("tx_frames", r.tx_frames);
  j.u32("cad_backoffs", r.cad_backoffs);
  j.u32("errors_suppressed", r.errors_suppressed);
  j.u32("cad_deferred", r.stats.cad_deferred);
  // The CAD outcome cad_backoffs does not see, and beside it the milliseconds the whole
  // transmit path held the radio out of receive (rx_deaf.h). Read rx_deaf_ms against the
  // window it was differenced over, not on its own.
  j.u32("cad_free", r.stats.cad_free);
  j.u32("rx_deaf_ms", r.stats.rx_deaf_ms);
  j.u32("cad_errors", r.stats.cad_errors);
  j.u32("tx_forced", r.stats.tx_forced);
  j.u32("tx_errors", r.stats.tx_errors);
  j.u32("tx_timeouts", r.stats.tx_timeouts);
  j.u32("tx_dropped_no_radio", r.stats.tx_dropped_no_radio);
  j.u32("rx_driver_errors", r.stats.rx_driver_errors);
  // The receive path's interrupt accounting (rx_wake.h). Beside the radio's other
  // numbers rather than among the spec 14.1 counters, for the reason lora_stats.h gives.
  j.u32("rx_no_interrupt", r.stats.rx_no_interrupt);
  j.u32("rx_wake_empty", r.stats.rx_wake_empty);
  j.u32("begin_failures", r.stats.begin_failures);
  j.i32("last_begin_status", r.stats.last_begin_status);
  for (size_t i = 0; i < kQueueCount; ++i) {
    j.u32(kQueueKeys[i][0], r.queues[i].dropped);
    j.u32(kQueueKeys[i][1], r.queues[i].high_water);
  }
  return j.finish();
}

size_t diag_node_json(const NodeState& s, uint32_t now_ms, char* out, size_t cap) {
  JsonObject j(out, cap);
  if (s.rssi_dbm == kRssiUnknown) j.null("rssi_dbm"); else j.i32("rssi_dbm", s.rssi_dbm);
  if (s.snr_db == kSnrUnknown) j.null("snr_db"); else j.i32("snr_db", s.snr_db);
  // Unsigned subtraction is the wrap-safe age across millis() rolling over.
  if (s.heard) j.u32("last_seen_s", (now_ms - s.last_seen_ms) / 1000u); else j.null("last_seen_s");
  // Saturated at UINT16_MAX, which is still a count and not a sentinel.
  j.u32("missed_polls", s.missed_polls);
  if (s.proto_ver == kVerUnknown) j.null("proto_ver"); else j.u32("proto_ver", s.proto_ver);
  // R-3.1f (BF-22) - the version this node speaks that the bridge cannot parse, or null.
  // THE DISTINCT REASON LIVES HERE AND NOT ON THE AVAILABILITY TOPIC: spec 16.5 fixes
  // that topic's payloads at `online` and `offline`, and Home Assistant depends on the
  // two tokens. So an unsupported node goes offline like any other and this field is what
  // says why - which is the difference between "marked unavailable with a distinct
  // reason" and "silently ignored".
  if (s.unsupported_ver == 0) j.null("unsupported_ver");
  else j.u32("unsupported_ver", s.unsupported_ver);
  return j.finish();
}

}  // namespace bridge
