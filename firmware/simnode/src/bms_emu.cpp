// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink task L7 - the emulated TDT BMS. bms_emu.h says why it replays rather than builds.

#include "bms_emu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace simnode {

namespace {

// bms-protocol 3 - the handshake, ASCII "HiLink", written to FFFA.
constexpr uint8_t kHiLink[] = {0x48, 0x69, 0x4c, 0x69, 0x6e, 0x6b};

// bms-protocol 5 - the three requests the pack answers, as the client sends them.
constexpr uint8_t kReq8C[] = {0x1e, 0x00, 0x01, 0x03, 0x00, 0x8c, 0x00, 0x00, 0xb1, 0x44, 0x0d};
constexpr uint8_t kReq8D[] = {0x1e, 0x00, 0x01, 0x03, 0x00, 0x8d, 0x00, 0x00, 0x71, 0x15, 0x0d};
constexpr uint8_t kReq92[] = {0x1e, 0x00, 0x01, 0x03, 0x00, 0x92, 0x00, 0x00, 0xb7, 0x24, 0x0d};

// bms-protocol 9 - the gate battery's answers, at rest and fully charged. Copied from the
// document, not from lib/bms-ble's tests. Do not edit one to suit a client.
constexpr uint8_t kRsp8C[] = {
    0x7e, 0x00, 0x01, 0x03, 0x00, 0x8c, 0x00, 0x20, 0x04, 0x0d, 0x89, 0x0d, 0xa1, 0x0d, 0x9c,
    0x0d, 0x9b, 0x04, 0x0b, 0x82, 0x0b, 0x9d, 0x0b, 0x7f, 0x0b, 0x7e, 0x40, 0x00, 0x05, 0x70,
    0x03, 0xe7, 0x03, 0xe8, 0x00, 0x01, 0x03, 0xe8, 0x00, 0x64, 0x55, 0xa3, 0x0d,
};
constexpr uint8_t kRsp8D[] = {
    0x7e, 0x00, 0x01, 0x03, 0x00, 0x8d, 0x00, 0x18, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x06, 0x29, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbd, 0x3f, 0x0d,
};
constexpr uint8_t kRsp92[] = {
    0x7e, 0x00, 0x01, 0x03, 0x00, 0x92, 0x00, 0x3c, 0x57, 0x54, 0x33, 0x30, 0x5f, 0x31, 0x30,
    0x30, 0x30, 0x34, 0x53, 0x57, 0x31, 0x34, 0x5f, 0x4c, 0x5f, 0x30, 0x31, 0x00, 0x31, 0x31,
    0x31, 0x31, 0x32, 0x32, 0x32, 0x32, 0x33, 0x33, 0x33, 0x33, 0x34, 0x34, 0x34, 0x34, 0x35,
    0x35, 0x35, 0x35, 0x49, 0x4b, 0x4b, 0x4b, 0x4b, 0x30, 0x30, 0x30, 0x30, 0x41, 0x49, 0x49,
    0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x2e, 0xb7, 0x0d,
};
static_assert(sizeof(kRsp92) == kBmsMaxFrame, "kBmsMaxFrame is the longest capture");

struct Exchange {
  const uint8_t* req;
  const uint8_t* rsp;
  size_t         rsp_len;
};
constexpr Exchange kExchanges[] = {
    {kReq8C, kRsp8C, sizeof(kRsp8C)},
    {kReq8D, kRsp8D, sizeof(kRsp8D)},
    {kReq92, kRsp92, sizeof(kRsp92)},
};
constexpr size_t kRequestLen = sizeof(kReq8C);

struct FaultName {
  BmsFault    fault;
  const char* name;
};
constexpr FaultName kFaultNames[] = {
    {BmsFault::None, "none"},          {BmsFault::BadCrc, "bad_crc"},
    {BmsFault::BadTerm, "bad_term"},   {BmsFault::Split, "split"},
    {BmsFault::NoResponse, "no_response"}, {BmsFault::DropMid, "drop_mid"},
};

}  // namespace

const char* bms_fault_name(BmsFault f) {
  for (const FaultName& n : kFaultNames) {
    if (n.fault == f) return n.name;
  }
  return "?";
}

bool bms_fault_from_name(const char* name, BmsFault* out) {
  for (const FaultName& n : kFaultNames) {
    if (std::strcmp(n.name, name) == 0) {
      *out = n.fault;
      return true;
    }
  }
  return false;
}

void BmsEmu::on_connect(uint32_t now_ms) {
  connected_  = true;
  handshaken_ = false;
  subscribed_ = false;
  mtu_        = 23;
  connect_ms_ = now_ms;
  owed_       = nullptr;
  ++stats_.connects;
}

void BmsEmu::on_disconnect() {
  connected_  = false;
  handshaken_ = false;
  subscribed_ = false;
  mtu_        = 23;
  owed_       = nullptr;
}

void BmsEmu::on_write(BmsChar ch, const uint8_t* data, size_t len, uint32_t now_ms) {
  (void)now_ms;  // a write the pack ignores does not reset its drop timer (bms-protocol 8)
  if (!connected_) return;
  if (ch == BmsChar::Handshake) {
    if (len == sizeof(kHiLink) && std::memcmp(data, kHiLink, len) == 0) {
      handshaken_ = true;
      ++stats_.handshakes;
    } else {
      ++stats_.handshake_rejected;
    }
    return;
  }
  if (ch != BmsChar::Tx) return;  // FFF1 is not writable; the stack refuses it first
  // bms-protocol 3 - before the handshake the pack acknowledges a write and ignores it.
  if (!handshaken_) {
    ++stats_.ignored_before_hs;
    return;
  }
  for (const Exchange& x : kExchanges) {
    if (len == kRequestLen && std::memcmp(data, x.req, len) == 0) {
      ++stats_.requests;
      if (owed_ != nullptr) ++stats_.busy_requests;  // the later request replaces it
      owed_     = x.rsp;
      owed_len_ = x.rsp_len;
      return;
    }
  }
  ++stats_.unknown_requests;
}

void BmsEmu::tick(uint32_t now_ms) {
  if (!connected_) return;
  if (!handshaken_ && now_ms - connect_ms_ >= kBmsNoHandshakeDropMs) {
    ++stats_.dropped_no_hs;
    on_disconnect();
    link_->drop();
    return;
  }
  if (owed_ == nullptr) return;
  const uint8_t* frame = owed_;
  const size_t   len   = owed_len_;
  owed_                = nullptr;
  if (!subscribed_) {
    ++stats_.unsubscribed;
    return;
  }
  respond(frame, len);
}

void BmsEmu::respond(const uint8_t* capture, size_t len) {
  uint8_t frame[kBmsMaxFrame];
  std::memcpy(frame, capture, len);  // a byte copy of the capture, so a fault leaves it intact
  // bms-protocol 7 - one frame per notification at a large MTU; MTU - 3 bytes each otherwise.
  size_t chunk = mtu_ > 3 ? static_cast<size_t>(mtu_ - 3) : kBmsMinChunk;

  const BmsFault f = fault_left_ > 0 ? fault_ : BmsFault::None;
  if (f != BmsFault::None) {
    ++stats_.faults_applied;
    if (--fault_left_ == 0) fault_ = BmsFault::None;
  }
  switch (f) {
    case BmsFault::BadCrc:     frame[len - 2] ^= 0xFF; break;
    case BmsFault::BadTerm:    frame[len - 1] = 0x00; break;
    case BmsFault::Split:      chunk = kBmsMinChunk; break;
    case BmsFault::NoResponse: return;
    case BmsFault::DropMid: {
      if (!link_->notify(frame, kBmsMinChunk)) ++stats_.notify_failures;
      on_disconnect();
      link_->drop();
      return;
    }
    case BmsFault::None: break;
  }

  for (size_t off = 0; off < len; off += chunk) {
    const size_t n = len - off < chunk ? len - off : chunk;
    if (!link_->notify(frame + off, n)) {
      ++stats_.notify_failures;
      return;
    }
  }
  ++stats_.responses;
}

void BmsEmu::arm(BmsFault f, uint32_t count) {
  fault_      = count > 0 ? f : BmsFault::None;
  fault_left_ = fault_ == BmsFault::None ? 0 : count;
}

bool bms_adv_name(const char* suffix, char* out, size_t cap) {
  const size_t n = std::strlen(suffix);
  if (n == 0 || n > 4) return false;
  return std::snprintf(out, cap, "XDZN_001_%s", suffix) == static_cast<int>(9 + n);
}

bool bms_command(char** argv, int argc, BmsEmu* emu, BmsControl* ctl, Sink* out) {
  if (std::strcmp(argv[0], "bms") != 0) return false;
  const char* sub = argc >= 2 ? argv[1] : "status";

  if (std::strcmp(sub, "on") == 0 && argc <= 3) {
    if (ctl->running()) {
      sink_printf(out, "ERR bms: already on - bms off first");
      return true;
    }
    char name[kBmsNameMax + 1];
    if (!bms_adv_name(argc == 3 ? argv[2] : ctl->default_suffix(), name, sizeof(name))) {
      sink_printf(out, "ERR bms: suffix is 1 to 4 characters");
      return true;
    }
    if (!ctl->start(name)) {
      sink_printf(out, "ERR bms: BLE stack did not start");
      return true;
    }
    sink_printf(out, "OK bms on, advertising %s", name);
    return true;
  }
  if (std::strcmp(sub, "off") == 0 && argc == 2) {
    ctl->stop();
    emu->on_disconnect();
    emu->disarm();
    sink_printf(out, "OK bms off");
    return true;
  }
  if (std::strcmp(sub, "fault") == 0 && (argc == 3 || argc == 4)) {
    BmsFault f;
    if (std::strcmp(argv[2], "off") == 0) {
      emu->disarm();
      sink_printf(out, "OK bms fault off");
      return true;
    }
    if (!bms_fault_from_name(argv[2], &f) || f == BmsFault::None) {
      sink_printf(out, "ERR bms fault: bad_crc|bad_term|split|no_response|drop_mid|off");
      return true;
    }
    uint32_t count = 1;
    if (argc == 4) {
      char*               end = nullptr;
      const unsigned long v   = std::strtoul(argv[3], &end, 10);
      if (*end != '\0' || v == 0 || v > 1000) {
        sink_printf(out, "ERR bms fault: count is 1 to 1000");
        return true;
      }
      count = static_cast<uint32_t>(v);
    }
    emu->arm(f, count);
    sink_printf(out, "OK bms fault %s armed for %lu response(s)", bms_fault_name(f),
                static_cast<unsigned long>(count));
    return true;
  }
  if (std::strcmp(sub, "status") == 0 && argc <= 2) {
    const BmsEmuStats& s = emu->stats();
    sink_printf(out, "OK bms %s, %s, handshake %s, FFF1 %s, MTU %u, fault %s x%lu",
                ctl->running() ? "on" : "off", emu->connected() ? "connected" : "no link",
                emu->handshaken() ? "done" : "no", emu->subscribed() ? "subscribed" : "off",
                static_cast<unsigned>(emu->mtu()), bms_fault_name(emu->fault()),
                static_cast<unsigned long>(emu->fault_left()));
    sink_printf(out, "  connects %lu handshakes %lu hs_rejected %lu ignored_before_hs %lu dropped_no_hs %lu",
                static_cast<unsigned long>(s.connects), static_cast<unsigned long>(s.handshakes),
                static_cast<unsigned long>(s.handshake_rejected),
                static_cast<unsigned long>(s.ignored_before_hs),
                static_cast<unsigned long>(s.dropped_no_hs));
    sink_printf(out, "  requests %lu unknown %lu busy %lu unsubscribed %lu responses %lu notify_failures %lu faults %lu",
                static_cast<unsigned long>(s.requests), static_cast<unsigned long>(s.unknown_requests),
                static_cast<unsigned long>(s.busy_requests), static_cast<unsigned long>(s.unsubscribed),
                static_cast<unsigned long>(s.responses), static_cast<unsigned long>(s.notify_failures),
                static_cast<unsigned long>(s.faults_applied));
    return true;
  }
  sink_printf(out, "ERR usage: bms on [suffix] | off | status | fault <name> [count] | fault off");
  return true;
}

}  // namespace simnode
