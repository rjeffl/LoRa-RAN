// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The protocol engine: every identity's receive path and its roles' answers. Tasks BF-3,
// BF-5; Impl Plan 10.2, 10.3; spec 6.4, 6.6, 7.5, 9.4, 11, 14, 17.3.
//
// ARDUINO-FREE. Frames come in as bytes and go out as encoded frames in an Outbox;
// radio.cpp moves them to and from the SX1262, and applies spec 12.3 on the way out.
//
// EACH IDENTITY HEARS EVERY FRAME, as a physical node would. A frame for 0xF2 is counted
// rx_not_addressed by 0xF0, exactly as a board at 0xF0 across the room would count it.
//
// WHAT EACH ROLE ANSWERS (Impl Plan 10.2). ROLE_RANGE echoes PING (spec 6.6, 17.3) and answers
// POLL with schema 0xF0; ROLE_HEALTH answers POLL with 0xF0. ROLE_GATELINK (BF-6, gatelink.cpp)
// answers POLL with 0xFE, COMMAND with COMMAND_ACK through its CommandGate, CONFIG with
// CONFIG_ACK, and sends 0x11 events on request. ROLE_FAULT answers nothing: every fault is
// armed from the console on any identity (BF-8).
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
#include "lran/schema/node_config_v1.h"
#include "lran/schema/gatelink_event_v1.h"
#include "sink.h"

namespace simnode {

// A fragmented PING's full set is 15 frames (spec 6.6.2). One more for a health answer.
inline constexpr size_t kOutboxDepth = 16;

// Not LRAN_MAX_FRAME: the SX1262 transmits up to 255 bytes, and the `oversize` fault (Impl
// Plan 10.5) exists to put one on the air. fault.cpp asserts this equals lran-sim's
// kPhyMaxFrame.
inline constexpr size_t kOutFrameMax = 255;

struct OutFrame {
  uint8_t bytes[kOutFrameMax] = {0};
  size_t  len                 = 0;
};

// Encoded frames waiting for the radio. Fixed storage (root rule 3). A frame that does not
// fit is refused and counted by the caller's check, never half-queued: a set missing its
// last fragments would time out at the far end and read as an RF loss.
class Outbox {
 public:
  size_t free_slots() const { return kOutboxDepth - count_; }
  size_t size() const { return count_; }
  bool   push(const uint8_t* bytes, size_t len);
  bool   pop(OutFrame* out);

 private:
  OutFrame frames_[kOutboxDepth];
  size_t   head_  = 0;
  size_t   count_ = 0;
};

enum class LogLevel : uint8_t { Quiet, Info, Debug };
const char* log_level_name(LogLevel l);
bool        parse_log_level(const char* token, LogLevel* out);

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
// the POLL answer and the fault carrier frame, so both report the same thing.
size_t build_health_payload(const Identity& e, const lran::Counters& radio, uint32_t now_ms,
                            uint8_t* out, size_t cap);

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

  // The radio's spec 12.3 instrument. Shared, because the channel is: every identity reports
  // it in 0xF0.
  lran::Counters* radio_counters() { return &radio_counters_; }

  void     set_log_level(LogLevel l) { level_ = l; }
  LogLevel log_level() const { return level_; }

  // A bench instrument, but root rule 8 still holds: no timing constant is fixed.
  void set_ping_timeout_ms(uint32_t ms) { ping_timeout_ms_ = ms; }

  // Answers dropped because the outbox had no room. Local; the frames they answered were valid.
  uint32_t answers_dropped() const { return answers_dropped_; }

 private:
  void deliver(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
               uint8_t fragments, int16_t rssi_dbm, int16_t snr_db10, uint32_t now_ms);
  void answer_poll(Identity& e, const lran::Header& hdr, uint32_t now_ms);

  // The `silent` fault (Impl Plan 10.5): true when this answer is to be withheld, which uses
  // one of the armed count.
  bool silenced(Identity& e, const char* what, const lran::Header& hdr);

  // spec 10.6 - EVERY role answers ROLL_CONTEXT, although only ROLE_GATELINK takes other
  // commands. A bridge rolls every node it hears after its own boot, and a role that
  // stayed silent would fail each roll and draw another on every frame the bridge heard,
  // which puts roll traffic inside a sweep's measurement. Decided 2026-09-23.
  void on_roll(Identity& e, const lran::Header& hdr, const lran::msg::Command& c);
  // A COMMAND to a role without a command path. True when it was a roll and was answered;
  // false leaves the caller to count it unhandled.
  bool answer_roll_only(Identity& e, const lran::Header& hdr, const uint8_t* payload,
                        size_t len);
  void on_ping(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
               uint8_t fragments, int16_t rssi_dbm, int16_t snr_db10, uint32_t now_ms);

  // BF-19a - logs the bridge's spec 14.2 ERROR and acts on none of it. The err_code is the
  // catalogue's only on-air evidence of which spec 14 stage fired.
  void on_error(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len);

  // Encodes `payload` from identity `e`, fragmenting at `chunk` when it is non-zero and
  // smaller than the payload, and queues every frame or none.
  bool send(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
            uint8_t chunk);

  // ROLE_GATELINK - gatelink.cpp.
  void            on_command(Identity& e, const lran::Header& hdr, const uint8_t* payload,
                             size_t len, uint32_t now_ms);
  void            on_config(Identity& e, const lran::Header& hdr, const uint8_t* payload,
                            size_t len);
  void            answer_poll_gatelink(Identity& e, const lran::Header& hdr, const uint8_t* payload,
                                       size_t len, uint32_t now_ms);
  void            refuse_authenticated(Identity& e, const lran::Header& hdr, lran::Status why);
  lran::AckResult execute(Identity& e, const lran::msg::Command& c, AfterAck* after);
  void            finish_command(Identity& e, const PendingAck& p, uint32_t now_ms);
  void            tick_gatelink(Identity& e, uint32_t now_ms);
  bool            send_ack(Identity& e, lran::NodeId peer, lran::Seq ack_seq,
                           lran::AckResult result, uint8_t detail);
  void            send_fresh_ack(Identity& e, lran::NodeId peer, lran::Seq seq,
                                 lran::AckResult result, uint8_t detail);
  bool            send_status(Identity& e, lran::NodeId dst, lran::StatusReason reason,
                              uint32_t now_ms);
  bool            send_event(Identity& e, lran::NodeId dst, const lran::schema::GateLinkEventV1& ev);
  // spec 7.4.1 - a SOLICITED answer repeats the request's `seq`, because correlation is
  // by `seq` (spec 9.2). An UNSOLICITED readback takes one from this identity's own
  // status space (D45), because it answers no request. `reply_seq` carries the first and
  // kUseStatusSeq asks for the second, so the difference is stated at every call site
  // rather than implied by which function was reached.
  static constexpr uint32_t kUseStatusSeq = 0x10000;  // outside the uint16 seq space
  bool            send_config_ack(Identity& e, lran::NodeId dst,
                                  const lran::schema::NodeConfigAckV1& ack,
                                  uint32_t reply_seq);
  bool            send_config_readback(Identity& e, lran::NodeId dst);
  void            apply_config(Identity& e, const lran::schema::NodeConfigV1& in,
                               lran::schema::NodeConfigAckV1* out);

  IdentityTable* ids_;
  Outbox*        out_;
  lran::IMac*    mac_;
  Sink*          log_;

  lran::Counters radio_counters_;
  LogLevel       level_           = LogLevel::Info;
  uint32_t       ping_timeout_ms_ = kDefaultPingTimeoutMs;
  uint32_t       answers_dropped_ = 0;
  LastRx         last_rx_;

  // A full CONFIG and CONFIG_ACK are several hundred bytes each; held here rather than on
  // the loop task's stack. Only one config is ever in progress, because the node is one loop.
  lran::schema::NodeConfigV1    cfg_rx_;
  lran::schema::NodeConfigAckV1 cfg_ack_;
};

}  // namespace simnode
