// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The publication policy. Tasks BF-24 and BF-25; Impl Plan 6.3; PRD R-5.2a-e; spec 7.2,
// 7.3, 7.5, 16.3, 16.4, 16.6.
//
// ARDUINO-FREE, like diag_json.h. app_task hands a STATUS or an EVENT here; everything that
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
//
// AN EVENT IS NOT A DOCUMENT (BF-25, R-5.2e, V-B8). It goes to `lran/<node>/event/<name>`
// with retain clear at QoS 1, once. Events drive email and SMS, so a retained one replays
// on every HA restart and discovery refresh, and a duplicate is a second message about one
// gate opening. Nothing here republishes an event: not on a heartbeat, and not after a
// broker connect.

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

// Counted per STATUS or EVENT the policy is handed. No frame reaches here without passing
// spec 14, so none of these is a spec 14.1 discard; they say what the bridge chose to
// publish. The last three count both kinds of frame.
struct PublishStats {
  uint32_t status_frames  = 0;  // STATUS messages handed to on_status()
  uint32_t documents      = 0;  // documents queued
  uint32_t unchanged      = 0;  // rendered, identical to the last, withheld
  uint32_t heartbeats     = 0;  // identical, republished for republish_interval_s
  uint32_t event_frames   = 0;  // EVENT messages handed to on_event()
  uint32_t events         = 0;  // events queued
  uint32_t event_repeats  = 0;  // an event already queued, withheld (spec 7.3)
  uint32_t bench_withheld = 0;  // decoded from a bench node, not published (spec 16.6)
  uint32_t queue_refused  = 0;  // the sink refused a document or an event
  uint32_t undecodable    = 0;  // a (type, schema) this bridge has no document for
};

// `lran/bridge/diag/publish/state` - the counts above by their field names. Returns the
// length written, or 0.
size_t publish_stats_json(const PublishStats& s, char* out, size_t cap);

// Where a document or an event goes. app_task's sink builds a PublishMessage and queues it;
// the tests' sink records it. False means it did not leave, and the policy does not record
// it as published: the node's next frame tries a document again, and a retransmission of
// the event gets through.
class PublishSink {
 public:
  virtual bool emit(const char* topic, const char* payload, bool retain, uint8_t qos) = 0;

 protected:
  ~PublishSink() = default;
};

// Seconds since 1970 in UTC, or 0 when the bridge has no wall clock yet (SNTP has not
// answered since boot).
using UtcSeconds = int64_t;

// Before this the clock is the ESP32's power-on default, not SNTP's answer. 2026-01-01.
// Shared by BF-24's traversal time and BF-29's audit entry.
inline constexpr UtcSeconds kUtcPlausible = 1767225600;

// Spec 8.9's name for an event_type, lower case, which is also its topic leaf. Null for a
// value the table does not list. dummy.cpp reads a console's event name back through it.
const char* event_type_name(uint8_t v);

// ISO 8601, `2026-09-23T14:05:09Z`. Returns the length written, or 0 for a short `cap`.
size_t format_utc(UtcSeconds t, char* out, size_t cap);

class PublicationPolicy {
 public:
  void set_levers(const PublishLevers& v) { levers_ = v; }
  const PublishLevers& levers() const { return levers_; }

  // One received message. Anything but a STATUS is ignored and not counted: commands,
  // configuration and events have paths of their own, events on_event() below. `utc_at_rx` is the wall clock at
  // reception, or 0 when unknown.
  //
  // A bench node's STATUS is decoded and counted, and never published: spec 16.6 allows a
  // bench node `diag/state` and `availability` alone, whichever way simnode_diag_enable
  // is set, and sched_task publishes both of those.
  void on_status(const NodeInfo& info, const lran::Header& hdr, const uint8_t* payload,
                 size_t payload_len, uint32_t now_ms, UtcSeconds utc_at_rx,
                 PublishSink& sink);

  // One received EVENT (spec 7.3), published to `lran/<node>/event/<name>` unless this
  // (src, ctx_id, event_id, follow-up) has been published already. Anything but an EVENT
  // is ignored and not counted. A bench node's event is decoded and counted, and never
  // published, on the same spec 16.6 ground as its STATUS.
  //
  // THE FOLLOW-UP BIT IS PART OF THE KEY, and spec 7.3's text says the triple. The same
  // section has a follow-up reuse its first edge's event_id, so the triple alone would
  // withhold every follow-up and the classified direction would never reach HA. The first
  // edge and its follow-up are each published once. The specification's wording is raised
  // for its next revision (Impl Plan 6.3.2).
  //
  // `synthetic` IS THE CALLER'S KNOWLEDGE, NOT THE FRAME'S. Spec 7.3 gives an EVENT no
  // status_reason, so nothing on the wire can say DEBUG_SYNTHETIC. A frame from the radio
  // passes false; BF-27's dummy publish passes true, and the payload's `synthetic` key
  // carries it into HA as R-5.2d asks. An automation that sends email or SMS filters on it.
  void on_event(const NodeInfo& info, const lran::Header& hdr, const uint8_t* payload,
                size_t payload_len, bool synthetic, PublishSink& sink);

  // After a broker connect. The retained documents may be gone (spec 16.5 reasons the same
  // way about availability), so the next frame from each node publishes every document
  // whether or not it changed. Events are not forgotten: none was retained, and forgetting
  // them would let a late retransmission publish a second time.
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

  // The events published, per node, most recent kEventMemory of them. A retransmission
  // follows its original by one CAD backoff, at most backoff_max_ms (1500 ms on Envelope A,
  // spec 12.3); a node raising sixteen events inside that is not a node this sizing has to
  // serve. A ring rather than a high-water mark, because nothing makes a retransmission
  // arrive before the node's next event.
  static constexpr size_t kEventMemory = 16;
  struct Seen {
    bool        valid     = false;
    bool        follow_up = false;
    lran::CtxId ctx_id    = 0;
    uint32_t    event_id  = 0;
  };

  PublishLevers levers_;
  PublishStats  stats_;
  Slot          slots_[kNodeCount][kDomainCount];
  Held          held_[kNodeCount];
  Seen          seen_[kNodeCount][kEventMemory];
  uint8_t       seen_next_[kNodeCount] = {0};
  char          doc_[kMaxPayloadLen] = {0};
};

}  // namespace bridge
