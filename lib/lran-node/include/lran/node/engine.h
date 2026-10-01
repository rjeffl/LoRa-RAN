// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The node-side protocol engine. GateLink task L1 (Impl Plan 5.2, 5.4, 8.1), extracted from
// the simnode's Node, where it was built as BF-3, BF-5, BF-6, BF-21, BF-33, BF-34 and
// BF-36. Spec 6.3-6.7, 7.4, 8.1, 9.4, 10.1-10.7, 11, 12.4.2, 14.
//
// ARDUINO-FREE. Frames come in as bytes and go out as encoded frames in an Outbox; the
// firmware's radio driver moves them, and applies spec 12.3 on the way out. Time is an
// argument.
//
// THE COMMAND PATH IS TWO STEPS (D34 as amended, spec 9.4). receive() runs
// CommandGate::check() and execute(); finish_command() runs record() and sends the
// COMMAND_ACK. An application that executes at once finishes in the same call. One that
// defers leaves the command in flight, and a retry arriving in that window gets no answer.
// The engine holds no lock: on a node whose receive and execution run on different tasks,
// call finish_command() on the task that calls receive(), or guard both (Impl Plan 5.2).
//
// ROLL_CONTEXT BYPASSES THE GATE (spec 9.4, 10.6, D58). While any command is in flight it
// answers ACTUATOR_BUSY and changes nothing; otherwise it rolls the context and ACKs under
// the new ctx_id.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/frame.h"
#include "lran/mac.h"
#include "lran/messages.h"
#include "lran/node/application.h"
#include "lran/node/context.h"
#include "lran/node/outbox.h"
#include "lran/node/phy_trial.h"
#include "lran/node/sink.h"
#include "lran/schema/node_config_v1.h"

namespace lran::node {

class Engine {
 public:
  Engine(Outbox* outbox, IMac* mac, Sink* log);

  Engine(const Engine&)            = delete;
  Engine& operator=(const Engine&) = delete;

  // What receive() learned about the frame, for a board's last-frame display.
  struct RxResult {
    bool   decoded = false;  // the header decoded for this context; `hdr` is real
    Header hdr;
  };

  // One frame with a good PHY CRC, through spec 14 stages 2-11 for one context, then
  // answered. Every frame goes to every context, as it reaches every node in range.
  RxResult receive(Context& c, Application& app, const uint8_t* buf, size_t len,
                   int16_t rssi_dbm, int16_t snr_db10, uint32_t now_ms);

  // spec 14 stage 1.
  static void on_phy_crc_error(Context& c);

  // Reassembly expiry (spec 11.2) and the HEX TIMEOUT (spec 8.13). Call often.
  void tick(Context& c, uint32_t now_ms);
  void tick_hex(Context& c, uint32_t now_ms);

  // spec 9.4 step 6 - the pending command's record() and its COMMAND_ACK, then what
  // follows the ACK. A no-op when nothing is pending. The application may update
  // `c.pending.result` and `detail` first.
  void finish_command(Context& c, Application& app, uint32_t now_ms);

  // spec 8.13 - the answer to a HexReply::Pending. False when nothing was pending.
  bool complete_hex(Context& c, HexStatus status, const char* hex, size_t n);

  // spec 10.7 - STATUS with BOOT, then the BOOT event carrying `cause` (spec 8.14). False
  // when either was not queued.
  bool announce_boot(Context& c, Application& app, ResetCause cause, uint32_t now_ms);

  // spec 12.4.2 step 8 - a revert, owed to the bridge as PHY_REVERTED and a CONFIG_CHANGE.
  static void note_phy_revert(Context& c, RevertCause cause);

  // spec 8.1 - an accepted REBOOT. The firmware resets once the ACK is on the air.
  bool restart_owed() const { return restart_owed_; }

  // The sends the application uses for its own answers. Each takes its seq from the
  // context's status space unless the comment says otherwise.
  bool send(Context& c, const Header& hdr, const uint8_t* payload, size_t len, uint8_t chunk);
  bool send_ack(Context& c, NodeId peer, Seq ack_seq, AckResult result, uint8_t detail);
  void send_fresh_ack(Context& c, Application& app, NodeId peer, Seq seq, AckResult result,
                      uint8_t detail);
  // spec 8.7, D69 - a POLL_RESPONSE carries an owed CONFIG_CHANGE in its place.
  bool send_status(Context& c, Application& app, NodeId dst, StatusReason reason,
                   uint32_t now_ms);
  bool send_event(Context& c, NodeId dst, const uint8_t* payload, size_t len, uint8_t schema);
  // spec 7.4.1 - unsolicited, so from the status space (D45).
  bool send_config_readback(Context& c, Application& app, NodeId dst);

  // A header from this context, its version and ctx_id, without a seq.
  static Header header(const Context& c, MsgType type, NodeId dst, uint8_t schema);

  void      set_phy(PhyTrial* phy) { phy_ = phy != nullptr ? phy : &no_store_; }
  PhyTrial* phy() { return phy_; }

  void     set_log_level(LogLevel l) { level_ = l; }
  LogLevel log_level() const { return level_; }

  // Answers dropped because the outbox had no room. Local; the frames they answered were valid.
  uint32_t answers_dropped() const { return answers_dropped_; }
  void     count_dropped() { ++answers_dropped_; }

 private:
  void deliver(Context& c, Application& app, const Header& hdr, const uint8_t* payload,
               size_t len, uint8_t fragments, int16_t rssi_dbm, int16_t snr_db10,
               uint32_t now_ms);
  void refuse_authenticated(Context& c, const Header& hdr, Status why);
  void on_error(Context& c, const Header& hdr, const uint8_t* payload, size_t len);
  void on_roll(Context& c, Application& app, const Header& hdr, const msg::Command& cmd);
  bool answer_roll_only(Context& c, Application& app, const Header& hdr,
                        const uint8_t* payload, size_t len);
  void on_command(Context& c, Application& app, const Header& hdr, const uint8_t* payload,
                  size_t len, uint32_t now_ms);
  void on_config(Context& c, Application& app, const Header& hdr, const uint8_t* payload,
                 size_t len, uint32_t now_ms);
  void apply_config(Context& c, Application& app, const schema::NodeConfigV1& in,
                    schema::NodeConfigAckV1* out, uint32_t now_ms);
  void on_hex_req(Context& c, Application& app, const Header& hdr, const uint8_t* payload,
                  size_t len, uint32_t now_ms);
  bool send_hex_rsp(Context& c, NodeId dst, Seq seq, HexStatus status, const char* hex,
                    size_t n);
  void send_phy_reverted(Context& c, Application& app, NodeId dst, uint32_t now_ms);

  // spec 7.4.1 - a SOLICITED answer repeats the request's `seq`, because correlation is by
  // `seq` (spec 9.2). An UNSOLICITED readback takes one from the status space (D45).
  // `reply_seq` carries the first and kUseStatusSeq asks for the second, so the difference
  // is stated at every call site.
  static constexpr uint32_t kUseStatusSeq = 0x10000;  // outside the uint16 seq space
  bool send_config_ack(Context& c, NodeId dst, const schema::NodeConfigAckV1& ack,
                       uint32_t reply_seq);

  Outbox* out_;
  IMac*   mac_;
  Sink*   log_;

  LogLevel level_           = LogLevel::Info;
  uint32_t answers_dropped_ = 0;
  bool     restart_owed_    = false;

  // A full CONFIG and CONFIG_ACK are several hundred bytes each; held here rather than on
  // the receiving task's stack. One config is in progress at a time.
  schema::NodeConfigV1    cfg_rx_;
  schema::NodeConfigAckV1 cfg_ack_;

  PhyTrial  no_store_{nullptr};
  PhyTrial* phy_ = &no_store_;
};

}  // namespace lran::node
