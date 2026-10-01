// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// What a node's firmware supplies to the engine. GateLink Impl Plan 5.4, task L1.
//
// THE ENGINE DECIDES WHAT THE SPECIFICATION DECIDES: the receive ladder, the context and
// its roll, CommandGate's two calls, the BOOT announcement, the CONFIG path and the PHY
// trial, and the HEX transport rules. The application decides what the node IS: what a
// command does to its actuator, what its status and events contain, which parameters it
// holds beyond the PHY group, and what is on the far side of its HEX UART.
//
// Every method has a default that answers nothing, so a node implements only what it
// does. The bench hooks at the end exist for the simnode's fault catalogue; a production
// node leaves them alone.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/frame.h"
#include "lran/messages.h"
#include "lran/node/context.h"
#include "lran/schema/node_config_v1.h"
#include "lran/types.h"

namespace lran::node {

class Engine;

// The message types the engine answers for a context. ROLL_CONTEXT and ERROR need none:
// every node answers a roll (spec 10.6) and logs an ERROR.
enum Capability : uint8_t {
  kAnswersCommands      = 0x01,  // the full COMMAND path through the gate
  kAnswersConfig        = 0x02,  // CONFIG, for the PHY group at least
  kAnswersHex           = 0x04,  // HEX_REQ, spec 7.6
  kRefusesAuthenticated = 0x08,  // COMMAND_ACK(REJECTED_CTX | REJECTED_MAC), spec 9.4 steps 2-3
};

// What execute() did with a command. `deferred` leaves the command in flight: the gate
// holds it, a retry gets no answer, and the ACK waits for Engine::finish_command().
struct CommandOutcome {
  AckResult result   = AckResult::Accepted;
  uint8_t   detail   = 0;
  AfterAck  after    = AfterAck::None;
  bool      deferred = false;
};

// GET_ALL's collector: config_list() adds each parameter the application holds.
class ConfigSink {
 public:
  virtual ~ConfigSink()                                   = default;
  virtual void add(const schema::ConfigAckEntry& entry)   = 0;
};

enum class HexReply : uint8_t {
  Answered,  // `rsp` holds the device's answer
  Pending,   // no answer yet: Engine::complete_hex() or the TIMEOUT closes it
};

enum class AckDelivery : uint8_t { Send, Suppress, Twice };

class Application {
 public:
  virtual ~Application() = default;

  virtual uint8_t  capabilities(const Context& c) const = 0;
  virtual RandomFn random() const                       = 0;

  // A POLL or a PING. True when answered; false counts the frame unhandled.
  virtual bool on_poll(Engine&, Context&, const Header&, const uint8_t*, size_t, uint32_t) {
    return false;
  }
  virtual bool on_ping(Engine&, Context&, const Header&, const uint8_t*, size_t, uint8_t,
                       int16_t, int16_t, uint32_t) {
    return false;
  }

  // A COMMAND the gate passed, never ROLL_CONTEXT. One command at a time: while another is
  // pending the engine answers ACTUATOR_BUSY and does not call this.
  virtual CommandOutcome execute(Context&, const msg::Command&, uint32_t) {
    return {AckResult::RejectedUnknownCmd, 0, AfterAck::None, false};
  }

  // STATUS and EVENT bodies, for the engine's own sends: a poll's answer, the BOOT
  // announcement, PHY_REVERTED. Bytes written, 0 when the node has none; `schema` receives
  // the schema id.
  virtual size_t build_status(const Context&, StatusReason, uint32_t, uint8_t*, size_t,
                              uint8_t*) {
    return 0;
  }
  virtual size_t build_event(Context&, EventType, uint16_t, uint32_t, uint8_t*, size_t,
                             uint8_t*) {
    return 0;
  }

  // Parameters beyond the PHY group. False from set or get means not held here, which the
  // engine answers UNKNOWN_PARAM.
  virtual bool config_set(Context&, const schema::ConfigEntry&, schema::ConfigAckEntry*) {
    return false;
  }
  virtual bool config_get(const Context&, uint16_t, schema::ConfigAckEntry*) { return false; }
  virtual void config_list(const Context&, ConfigSink*) {}
  virtual void config_restore_defaults(Context&) {}
  // spec 7.4, D53 - true when an override is held that is not persisted.
  virtual bool config_unpersisted(const Context&) const { return false; }

  // The PHY group is the board's. A board with several identities names each one's bit and
  // the bits that must all accept before it retunes (phy_trial.h).
  virtual size_t  phy_slot(const Context&) const { return 0; }
  virtual uint8_t phy_members() const { return 0x01; }

  // spec 7.6 - the request, already authenticated and gated, to the device.
  virtual HexReply hex_forward(Context&, const char*, size_t, char*, size_t, size_t*,
                               uint32_t) {
    return HexReply::Pending;
  }
  // GateLink Impl Plan's hex_timeout_ms. Root rule 8: the node's to set, not the engine's.
  virtual uint32_t hex_timeout_ms(const Context&) const { return 1000; }

  // Bench hooks. withhold() is asked before each answer the engine would send, and true
  // withholds it. force_reject_ctx() acts BEFORE the gate, so a refusal consumes no seq.
  // fresh_ack() is asked for each ACK of a result just produced, never a cached one.
  virtual bool        withhold(Context&, const char*, const Header&) { return false; }
  virtual bool        force_reject_ctx(Context&, const Header&) { return false; }
  virtual AckDelivery fresh_ack(Context&, Seq) { return AckDelivery::Send; }
};

}  // namespace lran::node
