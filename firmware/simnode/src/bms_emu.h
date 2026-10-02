// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The TDT BMS that the gate battery carries, emulated for the bench. GateLink task L7;
// GateLink Impl Plan 8.1 and 4.3, bms-protocol 2-9. bms_ble_peripheral.cpp puts it on air.
//
// ARDUINO-FREE, so the protocol half is host-tested. The BLE stack is behind BmsLink.
//
// REPLAYED, NOT BUILT. Every response is a bms-protocol 9 capture copied byte for byte
// into bms_emu.cpp. Nothing here includes lib/bms-ble/: an emulator that framed its answers
// with the client's codec would agree with any defect in that codec, and the test would
// pass on both ends. A fault corrupts a copy of the capture, never the capture.
//
// OUTSIDE THE IDENTITY TABLE. The emulator holds no node ID, no key and no role, and the
// LoRa side never sees it. It is off at boot; `bms on` starts it (Impl Plan 8.1, L7).

#pragma once

#include <cstddef>
#include <cstdint>

#include "sink.h"

namespace simnode {

// bms-protocol 2 - the three characteristics of service 0xFFF0 that carry the data path.
enum class BmsChar : uint8_t { Rx, Tx, Handshake };  // FFF1, FFF2, FFFA

// What the emulator asks of the BLE stack. The peripheral implements it; the host tests
// record it.
class BmsLink {
 public:
  virtual ~BmsLink() = default;
  // One notification on FFF1. False when the stack refused it.
  virtual bool notify(const uint8_t* data, size_t len) = 0;
  // Drops the connection, as the pack does.
  virtual void drop() = 0;
};

// Console-armed faults. Each is armed for `count` responses and then disarms itself, as
// Impl Plan 10.6 rule 2 requires of every simnode fault.
enum class BmsFault : uint8_t {
  None,
  BadCrc,      // bad_crc - the CRC's low byte inverted
  BadTerm,     // bad_term - 0x00 where the 0x0D terminator goes
  Split,       // split - 20-byte notifications, as at MTU 23, whatever MTU was negotiated
  NoResponse,  // no_response - the request is taken and nothing is sent
  DropMid,     // drop_mid - the first 20 bytes, then the link is dropped
};

const char* bms_fault_name(BmsFault f);
bool        bms_fault_from_name(const char* name, BmsFault* out);

// Every request and every discard is counted (root rule 4).
struct BmsEmuStats {
  uint32_t connects            = 0;
  uint32_t handshakes          = 0;  // "HiLink" accepted on FFFA
  uint32_t handshake_rejected  = 0;  // anything else written to FFFA
  uint32_t ignored_before_hs   = 0;  // FFF2 writes before the handshake (bms-protocol 3)
  uint32_t dropped_no_hs       = 0;  // links dropped at the pre-handshake timeout (8)
  uint32_t requests            = 0;  // a 5 request recognised on FFF2
  uint32_t unknown_requests    = 0;  // anything else on FFF2 after the handshake
  uint32_t busy_requests       = 0;  // a request while one was still owed
  uint32_t unsubscribed        = 0;  // a response owed with FFF1 not subscribed
  uint32_t responses           = 0;  // responses whose every notification was sent
  uint32_t notify_failures     = 0;  // notifications the stack refused
  uint32_t faults_applied      = 0;
};

// bms-protocol 8 - before a handshake the pack drops the link about 4 s after it is made.
// A constant, not a parameter: the simnode is reflashed at the bench (root rule 8 binds a
// node that cannot be).
inline constexpr uint32_t kBmsNoHandshakeDropMs = 4000;
// bms-protocol 7 - an ATT MTU of 23 leaves 20 bytes for each notification.
inline constexpr size_t kBmsMinChunk   = 20;
inline constexpr size_t kBmsMaxFrame   = 71;  // the 0x92 response, the longest in 9
inline constexpr size_t kBmsNameMax    = 16;  // "XDZN_001_" and a 4-character suffix

class BmsEmu {
 public:
  explicit BmsEmu(BmsLink* link) : link_(link) {}

  // Connection events from the stack. `mtu` is the negotiated ATT MTU.
  void on_connect(uint32_t now_ms);
  void on_disconnect();
  void on_mtu(uint16_t mtu) { mtu_ = mtu; }
  void on_subscribe(bool subscribed) { subscribed_ = subscribed; }

  // A write to `ch`. A request is answered from tick(), so a client's write returns before
  // its response arrives, as it does at the pack.
  void on_write(BmsChar ch, const uint8_t* data, size_t len, uint32_t now_ms);

  // What a read of FFFA returns: 0x01 after the handshake (bms-protocol 3). 0x00 before it,
  // which is the simnode's choice; the pack's value then was never captured.
  uint8_t handshake_value() const { return handshaken_ ? 0x01 : 0x00; }

  // Sends an owed response and enforces the pre-handshake drop. Call it from the loop.
  void tick(uint32_t now_ms);

  void arm(BmsFault f, uint32_t count);
  void disarm() { arm(BmsFault::None, 0); }

  bool               connected() const { return connected_; }
  bool               handshaken() const { return handshaken_; }
  bool               subscribed() const { return subscribed_; }
  uint16_t           mtu() const { return mtu_; }
  BmsFault           fault() const { return fault_; }
  uint32_t           fault_left() const { return fault_left_; }
  const BmsEmuStats& stats() const { return stats_; }
  void               clear_stats() { stats_ = BmsEmuStats{}; }

 private:
  void respond(const uint8_t* frame, size_t len);

  BmsLink*    link_;
  BmsEmuStats stats_;
  bool        connected_   = false;
  bool        handshaken_  = false;
  bool        subscribed_  = false;
  uint16_t    mtu_         = 23;
  uint32_t    connect_ms_  = 0;
  const uint8_t* owed_     = nullptr;  // the capture owed to the client, if any
  size_t      owed_len_    = 0;
  BmsFault    fault_       = BmsFault::None;
  uint32_t    fault_left_  = 0;
};

// The advertised name: "XDZN_001_" and `suffix`, which is 1 to 4 characters (bms-protocol 2).
// False when the suffix is empty or too long.
bool bms_adv_name(const char* suffix, char* out, size_t cap);

// The `bms` console command (Impl Plan 10.4, amended by L7). The radio-side control is the
// board's, so `on` and `off` call these; everything else is host code.
class BmsControl {
 public:
  virtual ~BmsControl() = default;
  virtual bool start(const char* adv_name) = 0;  // false when the stack failed to start
  virtual void stop()                      = 0;
  virtual bool running() const             = 0;
  virtual const char* default_suffix() const = 0;
};

// Handles `bms ...`. Returns false when argv[0] is not `bms`.
bool bms_command(char** argv, int argc, BmsEmu* emu, BmsControl* ctl, Sink* out);

}  // namespace simnode
