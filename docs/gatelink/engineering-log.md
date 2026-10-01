# GateLink Node — engineering log

**Dated record, appended to and never rewritten.** Measurements, surprises and the
things that cost an hour. Where this disagrees with a document, the document is the
current statement and this is what was true on the day. Implementation Plan §5.3 names
this file.

[`docs/README.md`](../README.md#conventions) says when the log is split.

---

## 2026-10-01 — L1: `lib/lran-node/` extracted from the simnode, host-proven, not yet on air

**The simnode's protocol engine is now `lib/lran-node/`, and the simnode runs on it.** The
simnode's 144 host tests pass, as they did before the move, and both of its target
environments build. The library's own suite adds 12. **No bench check has run**: plan §8.1
asks for `ROLE_GATELINK` back on air against the bridge, and for the BF-34 roll, the §10.7
reboot and the §12.4.2 PHY trial each to pass its bench check again. Until they do, L1 is
not closed.

**The boundary, as built.** `lran::node::Engine` holds every rule the specification
decides: the receive ladder after the codec, the `REJECTED_CTX` and `REJECTED_MAC`
refusals, the roll, both `CommandGate` calls, the `CONFIG` path through the PHY trial, the
HEX transport with `BUSY` and `TIMEOUT`, and the `BOOT` announcement.
`lran::node::Context` holds what one node owns. `lran::node::Application` is what the
node supplies: its capabilities, what a command does, its status and event bodies, its
parameters beyond the PHY group, and the device behind its HEX UART. The simnode's
`Identity` extends `Context`, and `Node::App` implements `Application` for all four roles.
`phy_trial.{h,cpp}`, the `Outbox`, the `Sink` and the spec's token names moved with it.

**`check()` and `record()` are separate steps**, as plan §5.2 requires. An application's
`execute()` may return `deferred`. The command then stays in flight, a retry gets no
answer, a roll answers `ACTUATOR_BUSY`, and `Engine::finish_command()` later records it and
sends the ACK. The simnode's `ack <hex> delay <ms>` now runs through that path rather than
its own `PendingAck`. `test_deferred_command_holds_the_execution_window` covers it on the
host. The engine holds no lock, so GateLink must call `finish_command()` on the task that
calls `receive()`, or guard both; `engine.h` says so.

**Four things changed beyond the move, and each is deliberate:**

- **The engine has no VE.Direct dependency.** The HEX response buffer is sized to what a
  `HEX_RSP` can carry, not to `vedirect::kMaxChars`. A node without an MPPT should not
  link a VE.Direct header to answer a HEX request.
- **The fault hooks are `Application` methods with no-op defaults.** `withhold()` carries
  the `silent` fault, `force_reject_ctx()` carries `ctx_reject` and still acts before the
  gate, and `fresh_ack()` carries `ack_suppress` and `ack_dup`. GateLink leaves them alone.
- **Four log lines read differently.** A deferred command logs `executing, ACK deferred`,
  not `ACK in <n> ms`. A HEX timeout logs `no answer by its deadline`, not `no MPPT answer
  in <n> ms`. `EVENT PHY_REVERTED` no longer prints the `event_id`. An unqueued status names
  its schema in place of the literal `0xFE`. The engine knows none of those values.
  `tools/simctl/` parses none of these lines; checked with a search on the day.
- **Five lines of the simnode's tests changed, and only their field paths.** `executions`,
  `hex_requests`, `hex_pending` and `phy_revert_detail` moved from `GateLinkState` to the
  `Context`, so `e.gl.executions` is now `e.executions`. No expected value changed.

**What the move did not settle.** `Engine::complete_hex()` exists for a device that
answers after `hex_forward()` returns, which is what a real UART does. Only the library's
test calls it: the simulated MPPT always answers at once or not at all. The `BOOT` and
`PHY_REVERTED` event bodies come from the application, because schema `0x11` is GateLink's.
WellLink will need its own when it is built.
