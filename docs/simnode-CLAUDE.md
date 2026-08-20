# `lran-simnode` — nodes `0xF0`–`0xF3`

**Subordinate to `/CLAUDE.md`.** Everything there applies. This file adds only what is
specific to the simnode.

**Primary document:** `LRAN-Bridge_Node-Implementation-Plan` §10.

## What this is

The fleet's test instrument. It is the only thing that exercises the bridge's receive
path, registry, scheduler, command retry and error handling **before GateLink exists** —
and the only thing that will exercise them **after GateLink is installed and unreachable
without a walk to the gate**. Treat it as permanent infrastructure, not scaffolding.

It is a protocol-level peer. It has no gate state machine, no relays, no I/O, and its
telemetry values are plausible rather than physical.

## Hardware profiles — build environments, not roles

| Env | Board | Typical use |
|---|---|---|
| `simnode-heltec` | Heltec V3 | `ROLE_FAULT`, `ROLE_HEALTH` at `0xF0`/`0xF2` |
| `simnode-xiao-wio` | XIAO ESP32S3 + Wio-SX1262 | `ROLE_GATELINK` at `0xF1` — **GateLink's actual radio module** |

A profile supplies exactly one thing: a `RadioPins` struct (NSS, RST, BUSY, DIO1, SPI pins,
TCXO voltage, DIO2-as-RF-switch, optional RXEN). Everything above the driver is identical.
**If the pin map turns out not to be cleanly injectable, that is a finding about R-4.1b —
report it rather than working around it with `#ifdef`s in the role code.**

Roles are runtime, assigned per identity. Profiles are compile-time. Do not mix them.

## Multi-identity

Up to four logical identities per board, each with its own `node_id`, HKDF-derived key,
`ctx_id`, sequence spaces, announced `ver` and enabled flag. The radio is shared and
serialized internally.

**From the bridge's side this must be indistinguishable from four physical nodes.** If it
isn't, something in the bridge is keyed on the radio rather than on `node_id` — that is a
bridge defect and worth reporting, not compensating for here.

The simnode holds `master_key` for bench convenience. **A real node never does** — it is
flashed with only its own derived key.

## Rules specific to this target

1. **Every emitted payload is marked synthetic.** `status_reason = DEBUG_SYNTHETIC`, and
   schema `0xFE` rather than `0x10` for status. Synthetic data reaching HA history unmarked
   is a bug in two nodes at once, and it fails silently — it looks like real history until
   someone tries to explain a reading.
2. **`/lib/lran-sim/` builds malformed frames by post-processing a correct frame from
   `/lib/lran-protocol/`.** Never write a second serializer. A separate one drifts, and
   then a fault test passes while testing a frame the system would never produce.
3. **Every fault arms for a bounded count and self-disarms.** A simnode left in a fault
   mode looks exactly like a broken bridge, and the session where that costs an hour is the
   one where you were debugging something else. Show armed faults on the OLED.
4. **No scenario requires a reflash.** Everything is driven from the serial console and
   scriptable from `/tools/simctl/`. If a test needs a rebuild, the console is missing a
   command — add it.

## Roles

`ROLE_RANGE` (PING echo + `0xF0`) · `ROLE_HEALTH` (`0xF0` only) · `ROLE_GATELINK`
(`0xFE` status, `0x11` events, ACKs, `0x12` config) · `ROLE_FAULT` (§10.5 catalogue).

`ROLE_RANGE` is deliberately impoverished: during a range test, every line of protocol
logic in the way can produce a symptom indistinguishable from poor link margin.

## Fault catalogue

21 entries in Impl Plan §10.5, each mapped to one Protocol Spec §14 stage or §9.4/§10 rule.
This is the only mechanism that produces these frames — without it every discard counter in
the bridge ships unverified.

**`bad_phy_crc` cannot be injected.** The SX1262 computes the PHY CRC in hardware, so no
transmitter can emit a frame that fails it. §14 stage 1 is verified only at the far edge of
a real link during the range walk.

The entries most likely to be skipped by hand are the ones whose correct result is that
**nothing happens** — `hdr_rsv` accepted, `seq_wrap` accepted, `wrong_dst` discarded with
no `ERROR`. Those are the forward-compatibility rules, and they break quietly. Commit them
as `simctl` scripts so a regression run is one command.

## Milestone

**B0** — flashes and runs; four identities with independent keys, contexts and sequence
spaces; console accepts every command; `ROLE_RANGE` echoes `PING`; faults arm, fire the
specified count, self-disarm, and show armed state on the OLED.

B0 depends on protocol library **P6** (W4 vectors committed). This ordering is
deliberate: a simnode validated only against the bridge is a mirror, not an instrument.
