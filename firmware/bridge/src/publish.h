// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The publication policy. Task BF-24; Impl Plan 6.3; PRD R-5.2a-d; spec 7.2, 7.5, 16.4,
// 16.6.
//
// ARDUINO-FREE, like diag_json.h. app_task hands a decoded STATUS here; everything that
// decides what Home Assistant sees - the documents, their keys, when one is republished,
// when an entity is unavailable - is in this file and has host tests (V-B7).
//
// SPEC 16.2.1 LEAVES THESE PAYLOADS TO THE BRIDGE under 16.4. The keys below are chosen
// here, and discovery.cpp's value templates read them, so A KEY PUBLISHED IS A KEY FROZEN
// for the same reason a unique_id is. Impl Plan 6.3.1 lists them.
//
// FIVE DOCUMENTS FROM SCHEMA 0x10, one per spec 16.1 domain: `gate`, `detect`, `solar`
// (the MPPT block), `battery` (the BMS block) and `node`. Schema 0xF0 goes to
// `lran/<node>/node/health/state`, a document of its own, so a node that sends both never
// overwrites one with the other.
//
// R-5.2b, WHICH THIS FILE EXISTS TO KEEP. A stale block is published with `available`
// false and every reading null, and discovery makes that document one of the entity's
// availability topics, so Home Assistant shows the entity as unavailable. The cached
// reading is never republished as current. Nothing here keeps a cached reading to
// republish: a document is rendered from the frame in hand or not at all.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "lran/frame.h"
#include "mqtt_transport.h"
#include "registry.h"

namespace bridge {

enum class Domain : uint8_t { Gate, Detect, Solar, Battery, Node, Health, kCount };
inline constexpr size_t kDomainCount = static_cast<size_t>(Domain::kCount);

// Spec 16.1's `<domain>` token, with Health's item: `node/health`.
const char* domain_path(Domain d);

// True for the documents that carry `available` - the MPPT and BMS blocks, whose staleness
// spec 16.4 names. discovery.cpp gives their entities that document as a second
// availability topic.
bool domain_carries_availability(Domain d);

// The same rule by state topic, for discovery.cpp, which knows an entity by its topic
// suffix. Inline so the discovery examples' generator needs no more of the bridge than it
// builds today (tools/checks/ha_examples.py).
inline bool state_carries_availability(const char* state_suffix) {
  return state_suffix != nullptr && (std::strcmp(state_suffix, "solar/state") == 0 ||
                                     std::strcmp(state_suffix, "battery/state") == 0);
}

// The keys of those documents that stay readable while `available` is false, because they
// say why the block is stale: the BMS link's RSSI and age, and whether a HEX transaction is
// outstanding. An entity reading one of these follows its node's availability alone.
inline bool key_survives_staleness(const char* key) {
  return key != nullptr &&
         (std::strcmp(key, "ble_rssi_dbm") == 0 || std::strcmp(key, "age_s") == 0 ||
          std::strcmp(key, "hex_pending") == 0 || std::strcmp(key, "synthetic") == 0);
}

// The values the policy reads from /lib/lran-config/ (Library Plan 4, rows 0x000D-0x000F).
struct PublishLevers {
  uint16_t republish_interval_s = 900;
  uint16_t bms_stale_s          = 600;
  uint8_t  cell_mv_deadband     = 5;
};

// Counted per STATUS the policy is handed. No frame reaches here without passing spec 14,
// so none of these is a spec 14.1 discard; they say what the bridge chose to publish.
struct PublishStats {
  uint32_t status_frames  = 0;  // STATUS messages handed to on_status()
  uint32_t documents      = 0;  // documents queued
  uint32_t unchanged      = 0;  // rendered, identical to the last, withheld
  uint32_t heartbeats     = 0;  // identical, republished for republish_interval_s
  uint32_t bench_withheld = 0;  // decoded from a bench node, not published (spec 16.6)
  uint32_t queue_refused  = 0;  // the sink refused a document; retried on the next frame
  uint32_t undecodable    = 0;  // a (type, schema) this bridge has no document for
};

// `lran/bridge/diag/publish/state` - the counts above by their field names. Returns the
// length written, or 0.
size_t publish_stats_json(const PublishStats& s, char* out, size_t cap);

// Where a document goes. app_task's sink builds a PublishMessage and queues it; the tests'
// sink records it. False means the document did not leave, and the policy does not
// record it as published, so the next frame tries again.
class PublishSink {
 public:
  virtual bool emit(const char* topic, const char* payload, bool retain) = 0;

 protected:
  ~PublishSink() = default;
};

// Seconds since 1970 in UTC, or 0 when the bridge has no wall clock yet (SNTP has not
// answered since boot).
using UtcSeconds = int64_t;

// ISO 8601, `2026-09-23T14:05:09Z`. Returns the length written, or 0 for a short `cap`.
size_t format_utc(UtcSeconds t, char* out, size_t cap);

class PublicationPolicy {
 public:
  void set_levers(const PublishLevers& v) { levers_ = v; }
  const PublishLevers& levers() const { return levers_; }

  // One received message. Anything but a STATUS is ignored and not counted: commands,
  // configuration and events have paths of their own. `utc_at_rx` is the wall clock at
  // reception, or 0 when unknown.
  //
  // A bench node's STATUS is decoded and counted, and never published: spec 16.6 allows a
  // bench node `diag/state` and `availability` alone, whichever way simnode_diag_enable
  // is set, and sched_task publishes both of those.
  void on_status(const NodeInfo& info, const lran::Header& hdr, const uint8_t* payload,
                 size_t payload_len, uint32_t now_ms, UtcSeconds utc_at_rx,
                 PublishSink& sink);

  // After a broker connect. The retained documents may be gone (spec 16.5 reasons the same
  // way about availability), so the next frame from each node publishes every document
  // whether or not it changed.
  void forget_published();

  const PublishStats& stats() const { return stats_; }

 private:
  struct Slot {
    bool     valid  = false;
    uint32_t hash   = 0;  // FNV-1a of the last document the sink accepted
    uint32_t at_ms  = 0;
  };

  // The deadbanded values, per node. What was last PUBLISHED, not last received: a
  // reading is compared with the published one, so a slow drift of 1 mV a poll still
  // crosses the deadband instead of hiding under it forever.
  struct Held {
    bool       cells_valid = false;
    uint16_t   cell_mv[4]  = {0, 0, 0, 0};
    bool       traversal_valid = false;
    UtcSeconds traversal       = 0;
  };

  void offer(size_t node_index, Domain d, const char* node_token, size_t len,
             uint32_t now_ms, PublishSink& sink);

  PublishLevers levers_;
  PublishStats  stats_;
  Slot          slots_[kNodeCount][kDomainCount];
  Held          held_[kNodeCount];
  char          doc_[kMaxPayloadLen] = {0};
};

}  // namespace bridge
