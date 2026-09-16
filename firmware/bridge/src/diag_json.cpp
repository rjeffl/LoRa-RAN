// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The diagnostic documents. Task BF-19.

#include "diag_json.h"

#include <cstdarg>
#include <cstdio>

namespace bridge {
namespace {

// Appends `"key":value` pairs, refusing rather than truncating: a document that did not
// fit reports 0 and leaves an empty string (mqtt_transport.h on truncated JSON).
class JsonObject {
 public:
  JsonObject(char* out, size_t cap) : out_(out), cap_(cap) {
    if (out_ == nullptr || cap_ == 0) {
      ok_ = false;
      return;
    }
    append("{");
  }

  void u32(const char* key, uint32_t v) { field(key, "%lu", static_cast<unsigned long>(v)); }
  void i32(const char* key, int32_t v) { field(key, "%ld", static_cast<long>(v)); }
  void null(const char* key) { field(key, "null"); }

  size_t finish() {
    append("}");
    if (!ok_) {
      if (out_ != nullptr && cap_ > 0) out_[0] = '\0';
      return 0;
    }
    return len_;
  }

 private:
  void field(const char* key, const char* fmt, ...) {
    append(first_ ? "\"%s\":" : ",\"%s\":", key);
    first_ = false;
    va_list ap;
    va_start(ap, fmt);
    vappend(fmt, ap);
    va_end(ap);
  }

  void append(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vappend(fmt, ap);
    va_end(ap);
  }

  void vappend(const char* fmt, va_list ap) {
    if (!ok_) return;
    const int n = std::vsnprintf(out_ + len_, cap_ - len_, fmt, ap);
    if (n < 0 || static_cast<size_t>(n) >= cap_ - len_) {
      ok_ = false;
      return;
    }
    len_ += static_cast<size_t>(n);
  }

  char*  out_;
  size_t cap_;
  size_t len_   = 0;
  bool   ok_    = true;
  bool   first_ = true;
};

// queues.h's QueueId order.
constexpr const char* kQueueKeys[][2] = {
    {"q_rx_dropped", "q_rx_high_water"},
    {"q_publish_dropped", "q_publish_high_water"},
    {"q_tx_dropped", "q_tx_high_water"},
    {"q_log_dropped", "q_log_high_water"},
};
static_assert(sizeof(kQueueKeys) / sizeof(kQueueKeys[0]) == kQueueCount,
              "a queue added to QueueId needs its diagnostic keys");

}  // namespace

size_t diag_rx_json(const lran::Counters& c, uint32_t unregistered_src, char* out,
                    size_t cap) {
  JsonObject j(out, cap);
  for (const lran::CounterField& f : lran::kCounterRegistry) j.u32(f.name, c.*(f.field));
  j.u32("rx_dropped", c.total_dropped());
  j.u32("rx_frames", c.rx_frames);
  j.u32("unregistered_src", unregistered_src);
  return j.finish();
}

size_t diag_radio_json(const RadioDiag& r, char* out, size_t cap) {
  JsonObject j(out, cap);
  j.u32("tx_frames", r.tx_frames);
  j.u32("cad_backoffs", r.cad_backoffs);
  j.u32("cad_deferred", r.stats.cad_deferred);
  j.u32("cad_errors", r.stats.cad_errors);
  j.u32("tx_forced", r.stats.tx_forced);
  j.u32("tx_errors", r.stats.tx_errors);
  j.u32("tx_timeouts", r.stats.tx_timeouts);
  j.u32("tx_dropped_no_radio", r.stats.tx_dropped_no_radio);
  j.u32("rx_driver_errors", r.stats.rx_driver_errors);
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
  return j.finish();
}

}  // namespace bridge
