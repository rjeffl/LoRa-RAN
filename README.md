# LRAN — LoRa Remote Automation Network

A property-wide 915 MHz LoRa network linking Home Assistant to remote, solar-powered
automation nodes that sit outside practical WiFi range. A mains-powered bridge in the
house speaks LoRa to the nodes and MQTT to Home Assistant.

The first node is **GateLink**, a driveway gate operator about **87 m (285 ft)** from the house.
Home Assistant opens and closes the gate, holds it open, reads gate and solar state,
reads and writes charge-controller configuration, and classifies vehicle traffic through
the gate. **WellLink**, a shallow-well level monitor, is planned; the network was
designed for a second node from the outset rather than retrofitted for one.

> **Status: pre-firmware.** The protocol library, its test vectors and the range-test
> bench instrument are built and passing. **No node firmware exists yet** — see
> [Current state](#current-state).

---

## Contents

- [Architecture](#architecture)
- [Nodes](#nodes)
- [The protocol](#the-protocol)
- [Repository layout](#repository-layout)
- [Build and test](#build-and-test)
- [Current state](#current-state)
- [Constraints that shape the design](#constraints-that-shape-the-design)
- [Radio and regulatory](#radio-and-regulatory)
- [Documentation](#documentation)
- [Contributing and workflow](#contributing-and-workflow)
- [License](#license)

---

## Architecture

One house-side bridge and N remote nodes in a star topology. Nodes never address each
other; there is no mesh and no multi-hop routing.

```
                       +---------------------+
   +--------------+    |                     |
   |   GateLink   |<-->|                     |
   | StamPLC      |    |                     |
   | + SX1262     |    |     LoRaBridge      |        +-------------------+
   | 4x relay out |    |     Heltec V3       |        |  Home Assistant   |
   | 6x iso. in   |    |                     |        |                   |
   |    <-> 1050  |    |                     |        | (Mosquitto broker,|
   | VE.Direct    |    |  LoRa 915 MHz  <--> |        |  MQTT Discovery)  |
   |   <-> MPPT   |    |  WiFi -> LAN        |------->|                   |
   | BLE <-> BMS  |    |  MQTT -> Mosquitto  |  MQTT  +-------------------+
   | 12V LiFePO4  |    |  OTA capable        |
   +--------------+    |                     |
                       |                     |
   +--------------+    |                     |
   |   WellLink   |<-->|                     |
   |  (planned)   |    |                     |
   +--------------+    +---------------------+
```

**The bridge knows nothing about gates.** It decodes a node's payloads because their
schemas are registered in a table, not because any code path understands what the node
does. Adding a node type is a registry entry, a decoder and a discovery template — never
a change to the scheduler, the availability watchdog or the MQTT layer.

## Nodes

| Node | ID | Hardware | Role | Power |
|---|---|---|---|---|
| **LoRaBridge** | `0x00` | Heltec WiFi LoRa 32 V3 | LoRa↔MQTT gateway: per-node poll scheduling, MQTT Discovery, availability watchdog, VE.Direct HEX proxy, OTA | Mains |
| **GateLink** | `0x01` | M5Stack StamPLC + SX1262 | Gate command and state, vehicle detection and direction, held-open alerting, MPPT telemetry and config transport, battery SOC over BLE | Solar / LiFePO4 |
| **WellLink** | `0x02` | To be decided | Well level monitoring with battery telemetry | To be decided |
| **simnode** | `0xF0`–`0xF3` | Heltec V3, XIAO + Wio-SX1262 | Bench instrument. Exercises the bridge's receive path, registry, scheduler, retry and fault handling — permanent infrastructure, not scaffolding | Bench |

**Remote nodes have no OTA.** The bridge is on the LAN, mains powered and physically
accessible, so it takes firmware over the air. GateLink and WellLink are USB-only: a
firmware change means a walk to the gate with a laptop. That single constraint shapes
most of what follows.

## The protocol

[`LRAN-Protocol-Specification`](docs/shared/LRAN-Protocol-Specification.md) is
authoritative for **every byte on the wire and every MQTT topic**. No other document —
this README included — may redefine a frame layout, an enum value, a schema ID or a
topic. **If code and the specification disagree, the specification is right.**

Wire version `ver = 2`, unchanged since specification v0.3.

```
        0        1        2        3        4        5        6        7
    +--------+--------+--------+--------+--------+--------+--------+--------+
  0 |  ver   |  type  |  src   |  dst   |      seq        |    ctx_id ...   |
    +--------+--------+--------+--------+--------+--------+--------+--------+
  8 |   ... ctx_id    |  frag  | schema |hdr_flag|         rsv (3 B)        |
    +--------+--------+--------+--------+--------+--------+--------+--------+
 16 |                       payload  (0..204 bytes)                         |
    +-----------------------------------------------------------------------+
    |            mac (8 B, authenticated types only)                        |
    +-----------------------------------------------------------------------+
    |     crc16 (2 B, always)     |
    +-----------------------------+
```

- **16-byte header, 222-byte maximum frame.** Overhead is 18 bytes unauthenticated,
  26 authenticated. The SX126x PHY allows 255; the cap leaves headroom and makes every
  buffer statically sized.
- **Message types:** `COMMAND`, `COMMAND_ACK`, `POLL`, `STATUS`, `EVENT`, `ERROR`,
  `PING`, `HEX_REQ` / `HEX_RSP`, `CONFIG` / `CONFIG_ACK`. `COMMAND` and `CONFIG` are
  authenticated bridge → node; `HEX_REQ` conditionally so.
- **Explicit schema IDs.** A payload declares its schema in the header, so adding a node
  requires reflashing the bridge and the new node — never the existing ones.
- **Authentication, not confidentiality.** Authenticated frames carry an 8-byte
  truncated HMAC-SHA256 under a per-node key, derived by HKDF-SHA256 from a single master
  key that only the bridge holds. Per-node rather than fleet-wide, so compromising the
  well sensor does not grant gate command authority. Nothing is encrypted, and the
  accepted limitations are stated plainly in spec §9.5.
- **Context IDs instead of persisted counters.** A node that reboots picks a new
  `ctx_id` rather than restoring a sequence number from flash.
- **Retries reuse `seq`.** The node's `(ctx_id, seq)` deduplication returns the cached
  ACK. Incrementing `seq` on a retry looks like a fix for a stuck command and is a
  second relay pulse at the gate.
- **Nothing is discarded silently.** Every discard increments a named counter and maps
  to one `Status` value and one processing stage.
- **Sentinels, not zero, for unavailable** — `INT16_MIN`, `UINT16_MAX`, `UINT32_MAX`.
  A consumer must be able to tell "0 A" from "no reading."

Home Assistant sees one device per node through MQTT Discovery, using `cover`, `sensor`,
`binary_sensor`, `button`, `switch`, `number` and `event` entities under the `lran/`
topic root.

## Repository layout

`[built]` is the only status marked here, deliberately — test counts and target dates
go stale faster than anyone re-reads a README. **Do not assume a path exists.**

```
lib/         lran-protocol/     the fleet-wide wire contract           [built]
             lran-config, lran-sim, vedirect, bms-ble                 [planned]
firmware/    range-test/        bench instrument, two board profiles   [built]
             bridge/, simnode/  context files only, no project yet
             gatelink/, welllink/                                     [planned]
tools/       vectors/           independent W4 vector generator        [built]
             rangetest/         capture, survey re-integration, checks [built]
             checks/            build-time invariants                  [built]
             simctl/            simnode console driver                [planned]
docs/        shared/ bridge/ gatelink/ welllink/ rangetest/ protocol-lib/ archive/
ha/          example discovery payloads                               [planned]
wattcycle-reader/   BLE BMS proof of concept, complete and self-contained
```

Each firmware is its own PlatformIO project reaching shared code through
`lib_extra_dirs = ../../lib`. The framework is `arduino` with ESP-IDF components
reachable for mbedTLS. Every project also defines a `native` environment running the
Unity suite on the host.

## Build and test

These work today:

```bash
pio test -d lib/lran-protocol -e native       # host Unity suite
pio test -d lib/lran-protocol -e esp32s3      # same suite on a Heltec V3
python3 tools/vectors/check.py                # W4 vectors, self-check
python3 tools/vectors/generate.py             # regenerate after any protocol change

pio test -d firmware/range-test -e native     # host Unity suite
pio run  -d firmware/range-test -e heltec     # Heltec V3 target build
pio run  -d firmware/range-test -e xiao       # XIAO ESP32S3 + Wio-SX1262 Kit
python3 tools/rangetest/check_pa_table.py     # PA table mirror vs. pinned RadioLib
python3 tools/checks/spec_citation_version.py # binding citations vs. the spec header
```

**The W4 test vectors are generated independently of the codec**, from the specification,
by a Python generator that never sees the C++. That independence is the entire value:
when a vector and the codec disagree, investigate which one is wrong — never edit the
vector to match. Regenerating after a protocol change is not optional (spec §13.2).

`main` stays buildable. [GitHub Actions](.github/workflows/ci.yml) runs the repository
checks, the host tools' own tests, both Unity suites and both firmware targets on every
pull request. No secrets are needed: every target built in CI is secrets-free by design.

## Current state

**Built and passing:**

- **`lib/lran-protocol/`** — framing, serialization, CRC-16, HMAC/HKDF, fragmentation
  and reassembly, sequence handling, schema codecs, counters. 107 host tests, and the
  same suite on target. Library milestones P1–P7 are met; **P8 (`CommandGate`) is
  outstanding** and gates simnode bring-up.
- **`tools/vectors/`** — 72 vectors from an independent generator, matching on host and
  target with zero divergence.
- **`firmware/range-test/`** — a bench instrument on two board profiles. 191 host tests.
  Pass 1 is complete: the sweep, the ambient survey, the D33 power clamp and the
  fragmented `PING` bench runs all closed.

**Not started:** bridge, GateLink, WellLink and simnode firmware. `firmware/bridge/` and
`firmware/simnode/` hold context files and nothing else.

**What blocks the next step.** Node firmware waits on **D1** — the LoRa PHY parameters,
SF / BW / CR / TX power. D1 is a decision rather than a measurement, and everything it
was waiting for has now landed: the ambient survey (M20) and the modules' FCC grant
conditions (M21) both closed on 2026-09-06, and the over-the-air EIRP sanity check
closed 2026-09-07. Nothing external blocks it.

Cross-node bring-up runs: RF link characterization → protocol and framing with simnode →
VE.Direct → battery and BMS → inputs read-only → outputs live → HA integration → field
soak. Per-node milestones with acceptance criteria live in each node's implementation
plan.

Six decisions remain open — **D1** (PHY parameters), **D33** (Part 15 operating mode,
reopened by M21), **D19** (WellLink power source), **D25** (VE.Direct TX translator),
**D28** (BLE link margin at the mounting position) and **D29** (enclosure thermal
envelope). [`LRAN-Decision-Register`](docs/shared/LRAN-Decision-Register.md) is the only
place a decision's status is recorded.

## Constraints that shape the design

These are repository-wide rules, not style preferences. Each exists because violating it
breaks something specific.

1. **Never `memcpy` a struct to or from the wire.** Serialize field by field, explicitly,
   little-endian. Host tooling uses a different compiler and architecture, so
   layout-dependent code works on two ESP32s and breaks the moment bench tooling is
   written.
2. **No dynamic allocation** in `lib/` or in any node firmware. Fixed, caller-owned
   buffers sized from `LRAN_MAX_FRAME`.
3. **`lib/lran-protocol/` must keep building for `native`.** No Arduino header, no
   ESP-IDF header, no `millis()`, no `Serial` — time is passed in as an argument. This
   is what lets a fragmented frame round-trip be tested at a desk instead of at the far
   end of a walk.
4. **Anything timing-related is runtime-configurable.** No timing constant is fixed at
   compile time in a node that cannot be reflashed without a walk to the gate.
5. **Reserved fields and bits are written zero and ignored on receive**, so the header
   extension space stays usable. The one validated exception is `hdr_flags` bit 7.
6. **RadioLib is the SX1262 driver everywhere**, pinned exactly in every
   `platformio.ini`. A driver shared by four firmwares is not left to float.
7. **C++17, `-Wall -Wextra -Werror`.** One narrow exception exists, documented at the
   flag that carries it.

## Radio and regulatory

915 MHz ISM, single fixed channel, no frequency hopping. **Transmit power is capped at
or below the FCC §15.249 EIRP ceiling** — approximately −1 dBm EIRP, which is −4 dBm
conducted with the fitted 3.0 dBi antenna. Conducted power and antenna gain are recorded
separately, because the ceiling is on EIRP and a combined figure cannot be audited.

**No node in this project is FCC certified, and none may be represented as certified** —
not in this README, not in a source header, not on an enclosure label, not in Home
Assistant device metadata. The SX1262 modules carry their own grants, but those grants
do not transfer to a device built around them. The operative frame for this project is
**§15.23, home-built**. Protocol Specification §18.2 is authoritative on all of this.

The power clamp is host-tested as pure arithmetic and has been verified over the air on
both board profiles. It is the one thing here that, if wrong, puts illegal power on the
air, and a bench with a spectrum analyser is the wrong place to discover a rounding bug.

## Documentation

The documents are the project's design record, and several of them are more current than
any code. Start at [`docs/README.md`](docs/README.md).

| Document | Authority over |
|---|---|
| [`LRAN-System-PRD`](docs/LRAN-System-PRD.md) | Architecture, node roles, repo layout |
| [`LRAN-Protocol-Specification`](docs/shared/LRAN-Protocol-Specification.md) | Every byte on the wire and every MQTT topic |
| [`LRAN-Decision-Register`](docs/shared/LRAN-Decision-Register.md) | Decision status `D1`–`D34` and measurement backlog `M1`–`M23` |
| [`LRAN-Protocol-Library-Implementation-Plan`](docs/shared/LRAN-Protocol-Library-Implementation-Plan.md) | `lib/lran-protocol/` API and tests |
| [`LRAN-Bridge_Node-PRD`](docs/bridge/LRAN-Bridge_Node-PRD.md) / [Impl Plan](docs/bridge/LRAN-Bridge_Node-Implementation-Plan.md) | Bridge requirements and build; the plan also owns `lran-simnode` |
| [`LRAN-GateLink_Node-PRD`](docs/gatelink/LRAN-GateLink_Node-PRD.md) / [Impl Plan](docs/gatelink/LRAN-GateLink_Node-Implementation-Plan.md) | GateLink requirements and build |
| [Range test tasks](docs/rangetest/) | Range test passes 1 and 2, field procedure, findings |

Engineering logs at `docs/<node>/engineering-log.md` are **dated, append-only records** —
what was tried, measured and decided, and why. They are corrected with a new entry or a
marked-superseded note, never by rewriting. `docs/archive/` holds superseded revisions
deliberately, so that a reference in old material lands on an explanation rather than a
gap.

**Check document versions before building against one.** A node document citing an older
protocol version than the specification's own header has not been reconciled with the
intervening revisions.

## Contributing and workflow

This is a personal project, developed with Claude Code against a maintained document set.
[`CLAUDE.md`](CLAUDE.md) at the repository root is the authoritative context file and
describes the working rules in full.

- **Branch per milestone**, named for it: `p4-schemas`, `b0-simnode-bringup`.
- **Cite requirement identifiers** (`R-*`, `BG-*`, `BS-*`, `D*`, `M*`, `W*`) in commits
  and pull request descriptions, and state which acceptance criteria a branch satisfies
  **and which it does not**.
- **Append findings to the engineering log as they happen** — measurements, surprises,
  anything that cost an hour.
- **Documents are docs-as-code.** A document update ships in the same commit as the work
  that proved it necessary.
- **If a document looks wrong, say so and propose the change.** The governing set was
  written before any firmware was built or any hardware was in hand. Much of it has held
  up; some of it was a guess that development has since tested. Working around a document
  silently is the one response that is not wanted.

`secrets.h` is gitignored. `secrets.h.example` is committed and documents every field.

## License

MIT — see [`LICENSE`](LICENSE). Copyright (c) 2026 Robert J. Lee.

Third-party components and their attribution requirements are recorded in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). Nothing is vendored: PlatformIO
fetches every dependency at build time at a version pinned in the relevant
`platformio.ini`. Publishing this repository's source does not distribute them, but
**distributing a compiled binary does** — ship the notices file alongside one.
