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
// WHAT THIS SLICE ANSWERS. ROLE_RANGE echoes PING (spec 6.6, 17.3) and answers POLL with
// schema 0xF0; ROLE_HEALTH answers POLL with 0xF0 (Impl Plan 10.2). ROLE_GATELINK and
// ROLE_FAULT can be assigned and answer nothing yet: TODO(BF-6), TODO(BF-8).
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
  void on_ping(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
               uint8_t fragments, int16_t rssi_dbm, int16_t snr_db10, uint32_t now_ms);

  // Encodes `payload` from identity `e`, fragmenting at `chunk` when it is non-zero and
  // smaller than the payload, and queues every frame or none.
  bool send(Identity& e, const lran::Header& hdr, const uint8_t* payload, size_t len,
            uint8_t chunk);

  IdentityTable* ids_;
  Outbox*        out_;
  lran::IMac*    mac_;
  Sink*          log_;

  lran::Counters radio_counters_;
  LogLevel       level_           = LogLevel::Info;
  uint32_t       ping_timeout_ms_ = kDefaultPingTimeoutMs;
  uint32_t       answers_dropped_ = 0;
  LastRx         last_rx_;
};

}  // namespace simnode
