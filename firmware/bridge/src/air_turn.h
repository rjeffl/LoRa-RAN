// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// One exchange on the air at a time. The poll clash, found in BF-33 slice 4's bench run on
// 2026-09-24; PRD R-3.1d.
//
// ARDUINO-FREE AND IT DOES NO I/O. sched_task fills an AirTurn under its lock and asks the
// two questions below before it starts anything.
//
// WHY. sched_task runs five senders in one tick, and each may send a frame. On 2026-09-24
// the bridge sent f2 a POLL and then, 211 ms later, a PHY CONFIG SET. Neither was
// answered, and the change was abandoned. The second frame arrives while the node is
// turning round to answer the first. The simnode does not log a POLL, so that mechanism
// is inferred rather than shown. BF-34's bench run saw the same 210 ms gap before a roll.
//
// THE RULE. An exchange is a frame and the answer it waits for. A scheduled POLL is one,
// and so is a command, a roll, a CONFIG and a whole PHY change while it blocks traffic.
// None starts while another is waiting for its answer. The frames inside one exchange,
// such as a command's retries or a PHY change's own step-6 POLLs, are that exchange's own
// business.
//
// A WAITING OPERATOR REQUEST GOES BEFORE A DUE POLL. A command or configuration job in its
// queue holds the next scheduled poll, so a gate OPEN waits for at most the poll already
// in flight. Holding a poll is cheap: it is sent late rather than missed, and BF-20 counts
// only missed polls.

#pragma once

namespace bridge {

struct AirTurn {
  bool poll_outstanding   = false;  // PollScheduler::outstanding()
  bool command_busy       = false;  // CommandPath::busy()
  bool roll_busy          = false;  // ContextRoll::busy()
  bool config_busy        = false;  // ConfigPath::busy()
  bool phy_blocks_traffic = false;  // PhyChange::blocks_traffic()
  bool hex_busy           = false;  // HexProxy::busy() - BF-28
  bool request_waiting    = false;  // a command or configuration job queued, not yet admitted
};

// A scheduled POLL may start. PhyChange::busy() is deliberately not asked: its cooldown
// after an abandon lasts up to phy_trial_s, and no frame of the change is in flight then.
inline bool poll_may_start(const AirTurn& a) {
  return !a.command_busy && !a.roll_busy && !a.config_busy && !a.phy_blocks_traffic &&
         !a.hex_busy && !a.request_waiting;
}

// A command, a roll, a CONFIG, a HEX request or a PHY change may start.
inline bool exchange_may_start(const AirTurn& a) { return !a.poll_outstanding; }

}  // namespace bridge
