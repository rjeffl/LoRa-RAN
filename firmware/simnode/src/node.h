// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The protocol engine: every identity's receive path and its roles' answers. Tasks BF-3,
// BF-5; Impl Plan 10.2, 10.3; spec 6.4, 6.6, 7.5, 9.4, 11, 14, 17.3.
//
// ARDUINO-FREE. Frames come in as bytes and go out as encoded frames in an Outbox;
// radio.cpp moves them to and from the SX1262, and applies spec 12.3 on the way out.
//
// THE PROTOCOL IS LRAN-NODE'S since GateLink task L1. Node runs lran::node::Engine once per
// identity, and its Application (App, below) is what makes each role different: which
// types it answers, what its status and events hold, its RAM parameter store, the
// simulated MPPT and the fault hooks. What stays here is what only a bench board has:
// several identities on one radio, PING initiation, schema 0xF0 answers and the console's
// emitters.
//
// EACH IDENTITY HEARS EVERY FRAME, as a physical node would. A frame for 0xF2 is counted
// rx_not_addressed by 0xF0, exactly as a board at 0xF0 across the room would count it.
//
// WHAT EACH ROLE ANSWERS (Impl Plan 10.2). ROLE_RANGE echoes PING (spec 6.6, 17.3) and answers
// POLL with schema 0xF0; ROLE_HEALTH answers POLL with 0xF0. ROLE_GATELINK (BF-6, gatelink.cpp)
// answers POLL with 0xFE, COMMAND with COMMAND_ACK through its CommandGate, CONFIG with
// CONFIG_ACK, HEX_REQ with HEX_RSP from a simulated MPPT (BF-36), and sends 0x11 events on
// request. ROLE_FAULT answers nothing: every fault is
// armed from the console on any identity (BF-8). Since BF-33, ROLE_RANGE and ROLE_HEALTH
// also answer a CONFIG, for spec 12.4's PHY group and nothing else, because a fleet change
// must move every node the bridge polls.
//
// THE SYNTHETIC MARKER ON 0xF0. Schema 0xF0 has no status_reason, so it cannot carry
// DEBUG_SYNTHETIC. Every 0xF0 this firmware emits sets health_flags bit 0, "any debug mode
// active" (spec 7.5) - the one field that schema offers for it. Schema 0xFE with
// status_reason = DEBUG_SYNTHETIC arrives with ROLE_GATELINK.

#pragma once

#include <cstddef>
#include <cstdint>

#include "identity.h"
#include "lran/config.h"
#include "lran/counters.h"
#include "lran/frame.h"
#include "lran/mac.h"
#include "lran/messages.h"
#include "lran/node/application.h"
#include "lran/node/engine.h"
#include "lran/node/outbox.h"
#include "lran/schema/gatelink_event_v1.h"
#include "phy_trial.h"
#include "sink.h"

namespace simnode {

using lran::node::kOutboxDepth;
using lran::node::kOutFrameMax;
using lran::node::LogLevel;
using lran::node::log_level_name;
using lran::node::OutFrame;
using lran::node::Outbox;
using lran::node::parse_log_level;

enum class PingResult : uint8_t {
  Ok,
  NoIdentity,
  Disabled,
  BadLength,   // n above spec 6.6.1's 202
  BadChunk,    // a chunk that would need more than 15 fragments
  Pending,     // this identity is still waiting for its last echo
  OutboxFull,
  EncodeFailed,
};
const char* ping_result_name(PingResult r);

// spec 7.5 - identity `e`'s schema 0xF0 payload, marked synthetic through health_flags bit 0.
// Writes lran::schema::kNodeHealthV1Len bytes; returns the count, or 0 on failure. Shared by
// the POLL answer and the fault carrier frame, so both report the same thing. `boot_count` is
// the board's, 0 when NVS gave none.
size_t build_health_payload(const Identity& e, const lran::Counters& radio, uint16_t boot_count,
                            uint32_t now_ms, uint8_t* out, size_t cap);

// spec 7.2 / 7.1 - identity `e`'s schema 0xFE payload with `reason`, from its synthetic
// telemetry. Writes kGateLinkStatusV1Len bytes; returns the count, or 0. gatelink.cpp.
size_t build_gatelink_status(const Identity& e, lran::StatusReason reason, uint32_t now_ms,
                             uint8_t* out, size_t cap);

// spec 7.3 - a new event from `e`, consuming the next event_id. gatelink.cpp.
lran::schema::GateLinkEventV1 make_event(Identity& e, lran::EventType type, uint32_t now_ms);

enum class EmitResult : uint8_t {
  Ok,
  NoIdentity,
  Disabled,
  WrongRole,        // push and event are ROLE_GATELINK's
  NothingToRepeat,  // `event <hex> again|follow` before any event
  OutboxFull,
  EncodeFailed,
};
const char* emit_result_name(EmitResult r);

// `event <hex> <type>` sends a new event_id; `again` resends the last event byte for byte;
// `follow` resends its event_id with the spec 7.3 follow-up bit and a classified direction.
enum class EventMode : uint8_t { New, Again, FollowUp };

// How long an initiator waits for an echo before reporting none. A 15-fragment set each
// way at SF9, plus backoffs, fits well inside it.
inline constexpr uint32_t kDefaultPingTimeoutMs = 30000;

// What the radio last handed the node, for the OLED (BF-9). Board-wide, not per identity:
// it answers "is anything reaching this board", which is the first question at a bench.
enum class RxKind : uint8_t {
  None,           // nothing received since boot
  Frame,          // at least one identity decoded the header; src, dst and type are real
  HeaderDiscard,  // no identity got past decode_header - not addressed here, or malformed
  PhyCrc,         // spec 14 stage 1
};

struct LastRx {
  RxKind        kind     = RxKind::None;
  uint32_t      at_ms    = 0;
  size_t        len      = 0;
  int16_t       rssi_dbm = lran::kI16NotAvailable;  // PhyCrc carries none
  lran::NodeId  src      = 0;
  lran::NodeId  dst      = 0;
  lran::MsgType type     = lran::MsgType::Poll;
};

class Node {
 public:
  Node(IdentityTable* ids, Outbox* outbox, lran::IMac* mac, Sink* log);

  Node(const Node&)            = delete;
  Node& operator=(const Node&) = delete;

  // One frame the radio received with a good PHY CRC.
  void on_rx(const uint8_t* buf, size_t len, int16_t rssi_dbm, int16_t snr_db10,
             uint32_t now_ms);

  // spec 14 stage 1 - heard by every enabled identity, as by every node in range.
  void on_phy_crc_error(uint32_t now_ms);

  const LastRx& last_rx() const { return last_rx_; }

  // Reassembly expiry (spec 11.2) and echo timeouts. Call often, not only on arrival.
  void tick(uint32_t now_ms);

  // Emit a PING from identity `id` to `dst` (spec 6.6). frag_chunk 0 sends one frame; a
  // chunk smaller than the payload fragments it (spec 6.6.2). The seq comes from the
  // identity's own space.
  PingResult ping(lran::NodeId id, uint8_t n, bool pattern, uint8_t frag_chunk,
                  lran::NodeId dst, uint32_t now_ms);

  // `push <hex> [reason]` - an unsolicited 0xFE status to the bridge (Impl Plan 10.4).
  EmitResult push(lran::NodeId id, lran::StatusReason reason, uint32_t now_ms);

  // `event <hex> ...` - a 0x11 event to the bridge. `event_id` receives the id sent.
  EmitResult event(lran::NodeId id, lran::EventType type, EventMode mode, uint32_t now_ms,
                   uint32_t* event_id = nullptr);

  // spec 10.7 - what every boot owes, called once from setup() with the chip's reset cause
  // and the board's count from NVS (0 when it has none). Each enabled ROLE_GATELINK identity
  // sends STATUS with BOOT, then a BOOT event carrying `cause` (spec 8.14). The other roles
  // have no status_reason and no event schema, and report the count in 0xF0.
  void     on_boot(lran::ResetCause cause, uint16_t boot_count, uint32_t now_ms);
  uint16_t boot_count() const { return boot_count_; }

  // `reboot <hex> [cause]` - ONE identity's simulated reboot: a new context, what a reboot
  // clears, then the same STATUS and BOOT event as on_boot(). The board and every other
  // identity keep running, so a resync test can reboot one node of four.
  EmitResult reboot_identity(lran::NodeId id, lran::ResetCause cause, uint32_t now_ms);

  // spec 8.1 - an accepted REBOOT resets the BOARD, once its ACK is on the air. main.cpp
  // reads this, waits for the radio to drain, and calls esp_restart(). The simnode's four
  // identities share one chip, so a REBOOT to one restarts them all.
  bool restart_owed() const { return engine_.restart_owed(); }

  // The radio's spec 12.3 instrument. Shared, because the channel is: every identity reports
  // it in 0xF0.
  lran::Counters* radio_counters() { return &radio_counters_; }

  void     set_log_level(LogLevel l) { engine_.set_log_level(l); }
  LogLevel log_level() const { return engine_.log_level(); }

  // A bench instrument, but root rule 8 still holds: no timing constant is fixed.
  void set_ping_timeout_ms(uint32_t ms) { ping_timeout_ms_ = ms; }

  // spec 12.4.2 - the board's PHY group. Without one the node holds its own, with no
  // store, and answers every PHY row READ_ONLY (step 2).
  void      set_phy(PhyTrial* phy) { engine_.set_phy(phy); }
  PhyTrial* phy() { return engine_.phy(); }

  // The identities that must accept a PHY group before the board retunes: every enabled
  // one whose role answers CONFIG, which is every role but ROLE_FAULT (phy_trial.h).
  uint8_t phy_members() const;

  // spec 12.4.2 step 8 - a revert, from the window or a reboot. Every ROLE_GATELINK identity
  // owes the bridge one PHY_REVERTED; the other roles have no event schema and report nothing.
  void on_phy_revert(RevertCause cause);

  // Answers dropped because the outbox had no room. Local; the frames they answered were valid.
  uint32_t answers_dropped() const { return engine_.answers_dropped(); }

 private:
  // Each role's half of the engine. Every Context the engine hands it is an Identity,
  // because Node passes nothing else.
  class App final : public lran::node::Application {
   public:
    explicit App(Node* node) : node_(node) {}
    uint8_t              capabilities(const lran::node::Context& c) const override;
    lran::node::RandomFn random() const override;
    bool on_poll(lran::node::Engine&, lran::node::Context& c, const lran::Header& hdr,
                 const uint8_t* payload, size_t len, uint32_t now_ms) override;
    bool on_ping(lran::node::Engine&, lran::node::Context& c, const lran::Header& hdr,
                 const uint8_t* payload, size_t len, uint8_t fragments, int16_t rssi_dbm,
                 int16_t snr_db10, uint32_t now_ms) override;
    lran::node::CommandOutcome execute(lran::node::Context& c, const lran::msg::Command& cmd,
                                       uint32_t now_ms) override;
    size_t build_status(const lran::node::Context& c, lran::StatusReason reason,
                        uint32_t now_ms, uint8_t* out, size_t cap, uint8_t* schema) override;
    size_t build_event(lran::node::Context& c, lran::EventType type, uint16_t detail,
                       uint32_t now_ms, uint8_t* out, size_t cap, uint8_t* schema) override;
    bool   config_set(lran::node::Context& c, const lran::schema::ConfigEntry& in,
                      lran::schema::ConfigAckEntry* out) override;
    bool   config_get(const lran::node::Context& c, uint16_t id,
                      lran::schema::ConfigAckEntry* out) override;
    void   config_list(const lran::node::Context& c, lran::node::ConfigSink* sink) override;
    void   config_restore_defaults(lran::node::Context& c) override;
    bool   config_unpersisted(const lran::node::Context& c) const override;
    size_t  phy_slot(const lran::node::Context& c) const override;
    uint8_t phy_members() const override { return node_->phy_members(); }
    lran::node::HexReply hex_forward(lran::node::Context& c, const char* req, size_t n,
                                     char* rsp, size_t cap, size_t* rsp_n,
                                     uint32_t now_ms) override;
    uint32_t hex_timeout_ms(const lran::node::Context& c) const override;
    bool     withhold(lran::node::Context& c, const char* what, const lran::Header& hdr) override;
    bool     force_reject_ctx(lran::node::Context& c, const lran::Header& hdr) override;
    lran::node::AckDelivery fresh_ack(lran::node::Context& c, lran::Seq seq) override;

   private:
    Node* node_;
  };

  static Identity&       as_identity(lran::node::Context& c) { return static_cast<Identity&>(c); }
  static const Identity& as_identity(const lran::node::Context& c) {
    return static_cast<const Identity&>(c);
  }

  void answer_poll(Identity& e, const lran::Header& hdr, uint32_t now_ms);
  void answer_poll_gatelink(Identity& e, const lran::Header& hdr, const uint8_t* payload,
                            size_t len, uint32_t now_ms);
  bool on_ping(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
               uint8_t fragments, int16_t rssi_dbm, int16_t snr_db10, uint32_t now_ms);

  // The `silent` fault (Impl Plan 10.5): true when this answer is to be withheld, which uses
  // one of the armed count.
  bool silenced(Identity& e, const char* what, const lran::Header& hdr);

  bool   send_event(Identity& e, const lran::schema::GateLinkEventV1& ev);
  size_t slot_of(const Identity& e) const;
  // spec 10.7 - STATUS with BOOT, then the BOOT event. False when either was not queued.
  bool   announce_boot(Identity& e, lran::ResetCause cause, uint32_t now_ms);

  IdentityTable* ids_;
  Outbox*        out_;
  Sink*          log_;

  lran::node::Engine engine_;
  App                app_{this};

  lran::Counters radio_counters_;
  uint32_t       ping_timeout_ms_ = kDefaultPingTimeoutMs;
  uint16_t       boot_count_      = 0;  // 0 is unavailable (spec 7.2.4)
  LastRx         last_rx_;
};

}  // namespace simnode
