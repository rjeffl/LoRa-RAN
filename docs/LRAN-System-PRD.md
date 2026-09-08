# LRAN System PRD

**Document:** `LRAN-System-PRD`
**Version:** 0.5
**Status:** Architecture settled. PHY parameters and several field measurements remain open.
**Supersedes:** `lran-prd-v0_8` §1–3, §7.1, §10, §12 (that document is retired — see §11)
**Last updated:** 2026-09-08

---

## Table of contents

1. [Overview](#1-overview)
2. [System goals and non-goals](#2-system-goals-and-non-goals)
3. [System architecture](#3-system-architecture)
4. [Node overviews](#4-node-overviews)
5. [Interfacing protocols](#5-interfacing-protocols)
6. [Home Assistant integration — system level](#6-home-assistant-integration--system-level)
7. [Proof-of-concept and verification subprojects](#7-proof-of-concept-and-verification-subprojects)
8. [System bring-up sequence](#8-system-bring-up-sequence)
9. [Repository, build and development environment](#9-repository-build-and-development-environment)
10. [System safety principles](#10-system-safety-principles)
11. [Third-party code and licenses](#11-third-party-code-and-licenses)
12. [Document set](#12-document-set)
13. [Changelog](#13-changelog)

---

## 1. Overview

### 1.1 Purpose

Provide a **property-wide point-to-multipoint LoRa network** linking a Home Assistant
(HA) instance to remote, low-power monitoring and control nodes that are outside
practical WiFi range.

The first node is **GateLink**: a remote, solar-powered driveway gate operator. HA
opens and closes the gate, holds it open on request, reads gate and solar state, reads
and writes charge-controller configuration, and detects and classifies vehicle traffic
through the gate.

A second node, **WellLink**, is planned. The network is designed for it from the outset
rather than retrofitted later.

### 1.2 System summary

One house-side bridge and N remote nodes, in a star topology.

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

### 1.3 Naming conventions

| Thing | Value |
|---|---|
| Repo | `lran` |
| Firmware targets | `lran-bridge`, `lran-gatelink`, `lran-welllink` (future), `lran-simnode` |
| MQTT topic root | `lran/` |
| Node topic form | `lran/<node>/...` — e.g. `lran/gatelink/...` |
| HA device names | "LoRa Bridge", "GateLink", "WellLink" |
| C++ namespace | `lran` |
| Node IDs | `0x00` bridge, `0x01` gatelink, `0x02` welllink, `0xF0`–`0xFE` bench, `0xFF` broadcast |

Node ID assignments are normative in
[`LRAN-Protocol-Specification`](./shared/LRAN-Protocol-Specification.md) §5.3; the table above
is a convenience copy.

---

## 2. System goals and non-goals

### 2.1 Goals — network

- A single house-side bridge serving **multiple** independent remote nodes over one
  LoRa channel, with per-node addressing, keying, and availability.
- **Adding a node requires reflashing only the bridge and the new node** — never the
  existing nodes. This is the constraint behind explicit payload schema IDs and N/N−1
  protocol version tolerance.
- A single shared protocol library that is the fleet-wide contract, defined and tested
  once, and exercised by the bench tools and simulators.
- Lightweight, native-feeling HA integration — `cover`, `sensor`, `binary_sensor`,
  `button`, `switch`, `number` and `event` entities via MQTT Discovery, one HA device
  per node.
- Per-node runtime configurability from HA, so that tuning a remote node never requires
  a walk with a laptop.
- Built-in bench debug tooling: packet loopback, dummy status pushes, device
  simulators, and input/event injection.
- Best-practice repo, build, and development workflow (GitHub + VS Code + Claude Code,
  PlatformIO, CI on every target).

### 2.2 Goals — per node

Node-specific goals are stated in each node's PRD and are not duplicated here. In
summary:

| Node | Goal summary | Document |
|---|---|---|
| **LoRaBridge** | General-purpose LoRa↔MQTT gateway; per-node poll scheduling, discovery publication, availability watchdog, VE.Direct HEX proxy, OTA | [`LRAN-Bridge_Node-PRD`](./bridge/LRAN-Bridge_Node-PRD.md) |
| **GateLink** | Gate command and state, vehicle detection and direction, held-open alerting, MPPT telemetry and configuration transport, battery SOC over BLE, full runtime configurability | [`LRAN-GateLink_Node-PRD`](./gatelink/LRAN-GateLink_Node-PRD.md) |
| **WellLink** | Well level monitoring with battery telemetry | [`LRAN-WellLink_Node-PRD`](./welllink/LRAN-WellLink_Node-PRD.md) |

### 2.3 Non-goals (v1)

- **Strong cryptographic security.** Command and MPPT-write authentication only; no
  confidentiality. Accepted limitations are stated plainly in Protocol Spec §9.5.
- **OTA for remote nodes.** The bridge supports OTA; GateLink and WellLink are
  USB-only. This is a deliberate constraint that shapes the protocol (§5.1).
- **Persisting a sequence counter across reboots.** Replaced by the context-ID scheme.
- **Mesh or multi-hop routing.** The topology is a star with the bridge at the centre.
  Nodes never address each other.
- **Multiple gate operators or multiple charge controllers.** Multiple *nodes* are
  explicitly in scope; multiple instances of the same peripheral behind one node are
  not.
- **BusT4.** The Nice/Apollo protocol bus is not used in v1. See
  [`LRAN-Research-Archive`](./archive/LRAN-Research-Archive.md) for the full bench findings and
  the optional Phase 2 design.
- **Replacing or modifying any operator's own safety logic.** §10.

---

## 3. System architecture

### 3.1 Node inventory

| | LoRaBridge | GateLink | WellLink (planned) |
|---|---|---|---|
| Board | Heltec WiFi LoRa 32 V3 | **M5Stack StamPLC** (ESP32-S3FN8) + external SX1262 | TBD |
| Node ID | `0x00` | `0x01` | `0x02` |
| Firmware | LoRa↔MQTT gateway + decoders | Custom (discrete I/O + interfaces) | TBD |
| Wired interfaces | none functional | 1050 accessory I/O (relays + isolated inputs), VE.Direct (MPPT) | level sensor |
| Wireless | LoRa + WiFi (BLE off) | LoRa + **BLE (duty-cycled)**; WiFi **off** | LoRa |
| Power | USB-C (mains) | 12 V **LiFePO4** direct to VIN | **TBD** (**D19**) |
| Update | **OTA + USB** | USB only | USB only |
| Configuration | build config | **runtime from HA** | TBD |
| Display | May stay on | 1.14" LCD, backlight off in normal operation | TBD |
| Location | House, near LAN | Inside existing gate controller enclosure | At the well |

### 3.2 Shared code

All nodes link a common **LoRa protocol/packet library** — framing, addressing, HMAC,
CRC, sequence handling, fragmentation — so the wire format is defined and tested once.
This shared library is the **fleet-wide contract** and is what the bench tools and
simulators exercise.

Because remote nodes have no OTA, this library carries a **compatibility obligation**:
the bridge must accept protocol version *N* and *N−1*, so a protocol revision can be
rolled out node-by-node rather than in a single flag-day flash of every device on the
property. Payload schema IDs extend the same idea to payload evolution — a node can
gain a field without any other node being touched.

### 3.3 Decode placement

Every node is a **discrete-I/O and sensor device**; interpretation that changes often
lives on the bridge.

| Layer | Runs on | Why |
|---|---|---|
| Debounce, edge detection, direction classification, hold-state tracking | **Node** | Sub-second local decisions; a LoRa round trip per edge is not viable |
| VE.Direct text parsing | **Node** | Continuous ~1 Hz stream; only the cached snapshot is transmitted |
| VE.Direct HEX register interpretation | **Bridge** | Bridge is mains-powered, OTA-capable and in the house. The node is transport only |
| Publication policy — publish-on-change, staleness, rounding | **Bridge** | Changes often; must not require a walk to the node |
| Entity mapping, discovery config | **Bridge** | Same reason |

> **Rationale.** Every rule above resolves to the same test: *does changing this
> require a physical visit to a node?* If yes, it belongs on the bridge. This is the
> single most consequential architectural consequence of the no-OTA non-goal (§2.3).

### 3.4 Data flows

- **Command (HA → node).** HA publishes to an MQTT command topic → bridge builds an
  authenticated LoRa command frame addressed to the node → node verifies MAC, context
  and sequence, deduplicates, and actuates → node returns a command-ACK → bridge
  publishes the result.
- **Status (node → HA).** Each node caches its latest state and transmits on (a) a poll
  request, (b) a locally significant state change, or (c) an event.
- **Event (node → HA).** Immediate unsolicited push for anything requiring
  notification, delivered to a **non-retained** MQTT topic so it fires exactly once.
- **MPPT config (HA → MPPT).** HA publishes a HEX request → bridge wraps and
  authorizes it → node transports it verbatim to the MPPT → response transported back →
  bridge publishes it. Writes require a MAC **and** an armed write-enable switch.
- **Poll.** A per-node poll scheduler on the bridge, each node's interval
  runtime-configurable from HA.

### 3.5 Platform convergence with AquaLink

AquaLink, the water-system controller project, independently selected the M5Stack
StamPLC. GateLink and AquaLink therefore share a host platform. **This is a platform
and framework convergence, not a feature one** — the two nodes share almost no
application behaviour.

**Shared:** the platform HAL (relay and input abstraction over the AW9523B, LCD,
buttons, buzzer, INA226, LM75, RTC, SD logging); the `lran-protocol` library and the
SX1262 driver; configuration, persistence and logging; the bench debug tooling; the
PlatformIO environment and CI.

**Not shared:** AquaLink is mains-powered indoors and publishes MQTT over WiFi
directly. GateLink publishes over LoRa and is bridged.

**Requirements.**

- `/lib/lran-platform/` SHALL abstract the host so that both node applications compile
  against the same HAL.
- The **radio pin map, TCXO voltage and RF-switch mode SHALL be injected by
  configuration rather than hardcoded**, so one SX1262 driver serves both the Heltec
  bridge (fixed internal pins) and the StamPLC carrier.

> **Useful side effect.** Because the transport is abstracted, GateLink can be
> bench-exercised over WiFi/MQTT with the radio entirely out of the loop — a strictly
> easier bring-up path than a LoRa-only design would have.

---

## 4. Node overviews

Each node has its own PRD and implementation plan. What follows is the system-level
summary only.

### 4.1 LoRaBridge (`0x00`)

A **general-purpose LoRa↔MQTT gateway**, not a gate-specific bridge. Receives and sends
LoRa to any registered node, connects to the LAN over WiFi, and bridges to the existing
Mosquitto broker on the HA host. HA entities are created via MQTT Discovery, one HA
device per node. Mains powered, no power constraint, OTA capable.

It owns everything the fleet needs to be told once rather than N times: the poll
schedule, discovery publication, per-node availability, publication policy, VE.Direct
HEX register interpretation and write authorization, and link diagnostics.

Detail: [`LRAN-Bridge_Node-PRD`](./bridge/LRAN-Bridge_Node-PRD.md) ·
[`LRAN-Bridge_Node-Implementation-Plan`](./bridge/LRAN-Bridge_Node-Implementation-Plan.md)

### 4.2 GateLink (`0x01`)

A remote node roughly **500 ft** from the house, inside the existing gate controller
enclosure. An **M5Stack StamPLC** with an external **SX1262** radio on a carrier board.

- Interfaces to a Nice/Apollo **1050** control board through its **documented accessory
  I/O** — four momentary dry-contact relay outputs and six opto-isolated inputs.
  Nothing is reverse-engineered and nothing touches a 24 V rail.
- Interfaces to a Victron **MPPT 75/15** over **VE.Direct**, both the ~1 Hz text stream
  and the bidirectional HEX protocol.
- Reads the battery **BMS over BLE**.
- Powered **directly** from the 12 V **100 Ah LiFePO4** pack — no intermediate adapter.
- Every timing interval, window and threshold is **runtime-configurable from HA**.

Detail: [`LRAN-GateLink_Node-PRD`](./gatelink/LRAN-GateLink_Node-PRD.md) ·
[`LRAN-GateLink_Node-Implementation-Plan`](./gatelink/LRAN-GateLink_Node-Implementation-Plan.md)

### 4.3 WellLink (`0x02`, planned)

Well level monitoring at roughly the same distance as GateLink but on a **different
bearing** — which is a system-level constraint on bridge antenna siting (§5.1).

| Attribute | Status |
|---|---|
| Distance / bearing | ~500 ft, different direction from GateLink |
| Power | **TBD** (**D19**) — mains is possible; battery/solar must remain viable |
| Function | Well level monitoring. Possible expansion later |
| Reporting | Fixed-interval poll/push, **plus** an event push on rapid level change |
| Payload | Level + **battery status** — packet space reserved for both |
| Commands | None currently anticipated |

Two design consequences are already carried:

- The **RX duty-cycling and extended-preamble design is retained** as a reserved
  feature in the protocol spec rather than deleted, because WellLink may be battery
  powered even though GateLink is not.
- Payload schemas are **per-node and versioned**, so WellLink can define its own
  payload without touching GateLink's.

Detail: [`LRAN-WellLink_Node-PRD`](./welllink/LRAN-WellLink_Node-PRD.md) *(placeholder)*

### 4.4 simnode (`0xF0`–`0xFE`, bench only)

A simulated-node firmware target that registers on the bench address range, answers
polls and emits events on demand. It validates addressing, per-node keying, the
availability watchdog, fragmentation and CAD/backoff **before WellLink exists**.

---

## 5. Interfacing protocols

Five distinct protocols meet in this system. Only the first two are LRAN's own.

| Protocol | Between | Owned by | Specified in |
|---|---|---|---|
| **LRAN LoRa frame format** | bridge ↔ any node | **LRAN** | [`LRAN-Protocol-Specification`](./shared/LRAN-Protocol-Specification.md) §3–§15 |
| **LRAN MQTT interface** | bridge ↔ HA | **LRAN** | Protocol Spec §16 |
| **VE.Direct** (text + HEX) | GateLink ↔ MPPT 75/15 | Victron | Vendor documentation; transported verbatim |
| **TDT BLE BMS** | GateLink ↔ battery pack | Pack vendor | `/docs/gatelink/bms-protocol.md` — **still to be written up** from the PoC workspace (`/wattcycle-reader/`) |
| **1050 accessory I/O** | GateLink ↔ gate controller | Nice/Apollo | Vendor manual; GateLink PRD |

### 5.1 LoRa link — system-level parameters

- Band: **US 915 MHz**, point-to-multipoint star (not LoRaWAN), private sync word.
- SF / BW / CR / TX power: **open (D1)**, to be fixed after the range test. Starting
  point for ~150 m near-LOS: SF7–9, BW 125 kHz, CR 4/5, moderate TX power. The
  protocol spec's airtime analysis establishes that **SF may be chosen on link margin
  alone, not on power**.
- **Bridge antenna placement is a two-bearing problem.** GateLink and WellLink are at
  similar distances in different directions. Favour an omnidirectional antenna in a
  central, elevated position over anything with a pattern optimized toward the gate.
  **Range-test both bearings before committing to a location.**
- FCC Part 15 operating mode is an open item (Protocol Spec §18.1) and should be
  settled **before** D1 fixes a TX power.

### 5.2 Fleet-wide protocol obligations

Every node PRD and implementation plan in this set **references the protocol
specification and does not restate it**. In particular:

- Frame layouts, enumerations and schema IDs are defined once, in the protocol spec.
- A node needing a new field gets a **new schema ID**, not a header change — the
  mechanism that keeps a change from becoming a fleet-wide reflash.
- Protocol changes require a version bump and an entry in
  `/docs/protocol-changelog.md`.
- MQTT topic grammar, retention rules and the **hard never-retain rule for event
  topics** are protocol-spec requirements binding on the bridge.

---

## 6. Home Assistant integration — system level

### 6.1 Device model

**One HA device per node, plus one for the bridge.** Each node's entities carry that
node's `device` block and reference that node's availability topic.

| HA device | Node | Availability source |
|---|---|---|
| LoRa Bridge | `0x00` | MQTT LWT |
| GateLink | `0x01` | `lran/gatelink/availability` (bridge watchdog) |
| WellLink | `0x02` | `lran/welllink/availability` (bridge watchdog) |

Discovery `unique_id`s are prefixed per node (`lran_gatelink_*`) so they are stable and
non-colliding as the fleet grows.

> **Why per-node devices rather than one LRAN device.** A single device would make
> every entity share one availability state, so a bridge restart or a single node going
> quiet would mark the whole property unavailable. Per-node devices also mean a new
> node appears in HA as a new device rather than as thirty new entities on an existing
> one.

### 6.2 Entity tables

Per-node entity tables live in the node PRDs. The system-level rules that bind all of
them:

- The **event topic is the automation trigger**, never a retained `binary_sensor`. A
  retained sensor replays on HA restart and on discovery refresh; anything driving
  email or SMS must not.
- Where an event has dashboard value, publish **both** — retained sensor for
  visibility, non-retained event for automation.
- Entities backed by a sensor with a staleness indicator are marked **unavailable**
  when stale, never republished with the last good value.
- Synthetic data (debug pushes, `simnode`) is marked as such all the way into HA
  history.

---

## 7. Proof-of-concept and verification subprojects

Discrete pieces of work carved out of the main build because they retire a specific
unknown. Each has its own workspace and its own record.

### 7.1 WattCycle BMS BLE reader — **complete**

**Question:** can the battery pack's BMS be read over BLE from an ESP32-S3, and by what
protocol?

**Outcome — resolved.** The pack is a **TDT** BMS (advertising `XDZN_001_xxxx`), not
the JBD/Xiaoxiang or Daly families originally assumed. The access sequence is
documented, and an independent client was written and validated over **32 consecutive
polls with zero CRC failures**, in the `/wattcycle-reader/` PoC workspace. **The protocol
write-up and reference captures have not yet been lifted out of that workspace into
`/docs/gatelink/bms-protocol.md`** — doing so is a prerequisite for the GateLink BMS
port, since the PoC workspace is not part of the LRAN build. The C++ client for GateLink is a port of that implementation,
testable offline against the same captures.

The dead ends — the 20-combination JBD/Daly sweep, the wrong-characteristic writes, the
hypothesis matrix — are preserved in
[`LRAN-Research-Archive`](./archive/LRAN-Research-Archive.md), because they are the reason the
final answer is trusted.

**Residual open item:** the pack-current sign convention, which needs one capture under
charge and one under load.

### 7.2 Carrier board bring-up — **required before GateLink phase 1**

**Question:** does an external SX1262 module on a perfboard carrier work on the
StamPLC's pin budget and 5 V-only rail?

Scoped in [`LRAN-GateLink_Node-Implementation-Plan`](./gatelink/LRAN-GateLink_Node-Implementation-Plan.md).
Until it passes, GateLink has no radio — and failure here is one of three documented
triggers for reconsidering a LoRa/BLE co-processor.

### 7.3 RF range and bearing characterization — **system level**

**Question:** what SF/BW/CR/TX power does a ~500 ft link need, on **both** the gate
bearing and the well bearing, and where does the bridge antenna go?

**Deliberately host-independent.** Two Heltec V3 boards characterize the PHY faster
than waiting on the GateLink carrier, and the result transfers unchanged. This is the
subproject that resolves **D1** and sites the bridge antenna.

### 7.4 Multi-node protocol validation — `simnode`

**Question:** does the fleet-wide protocol actually behave as a fleet before a second
real node exists?

Runs `simnode` alongside GateLink to exercise addressing, per-node keying, the
availability watchdog, fragmentation and CAD/backoff. This is the only way to test the
multi-node design before WellLink is built, and the protocol is where a design error
would be most expensive to discover late.

---

## 8. System bring-up sequence

Cross-node ordering only. **Per-node milestones with acceptance criteria live in each
node's implementation plan** and are not duplicated here.

| # | Phase | Nodes involved | Gate |
|---|---|---|---|
| 0 | Carrier board bring-up | GateLink | Radio responds on the carrier pin map |
| 1 | **RF link characterization** | Bridge + a second radio | **D1** resolved, both bearings measured, bridge antenna sited |
| 2 | Gate controller rewiring and manual validation | *(no LRAN hardware)* | Runs in parallel with phase 1 |
| 3 | Protocol and framing, with `simnode` | Bridge + GateLink + simnode | Addressing, keying, availability, fragmentation, CAD/backoff, config round-trip |
| 4 | VE.Direct | GateLink | Text parsing **and** HEX round-trip proven |
| 5 | Battery and BMS | GateLink | Live pack decoded; BLE margin measured from the final mounting position |
| 6 | Node inputs live, read-only | GateLink | State derivation validated against real gate cycles |
| 7 | Node outputs live | GateLink | Dry-run, then real, with manual recovery paths confirmed first |
| 8 | **HA integration** | Bridge + GateLink | Discovery per device, command round-trip, per-node availability, **events verified to fire once and not replay on HA restart** |
| 9 | **Field soak** | All | Measured daily consumption against budget; seasonal thermal log begun |

Phases 1 and 2 are independent of each other and of the carrier. Phase 3 onward is
sequential.

---

## 9. Repository, build and development environment

### 9.1 Layout

```
/firmware/bridge/        # LoRaBridge PlatformIO project             [planned]
    CLAUDE.md            #   subproject context for Claude Code       [exists]
/firmware/gatelink/      # GateLink PlatformIO project                [planned]
    CLAUDE.md
/firmware/welllink/      # WellLink                                   [planned]
/firmware/simnode/       # simulated node for multi-node bench testing
    CLAUDE.md            #                                            [exists]
/firmware/range-test/    # D1 / M6 / M20 / W9 — pass 1 complete;      [built]
                         # pass 2 adds the XIAO + Wio-SX1262 profile
    CLAUDE.md
/lib/lran-platform/      # host HAL: relays, inputs, display, buttons, INA226,
                         #   LM75, RTC, SD. Shared with AquaLink
/lib/lran-protocol/      # shared framing/addressing/HMAC/CRC/fragmentation [built]
    /src/schema/         #   versioned per-node payload schemas
    /test/               #   Unity suites; W4 vectors embedded from /tools/vectors/
/lib/lran-config/        # runtime config: parameter table, SD persistence,
                         #   CONFIG frame handling
/lib/vedirect/           # VE.Direct text + HEX (osh-labs port) [MIT]
/lib/bms-ble/            # TDT BLE BMS client
/tools/                  # bench scripts, simulators, MQTT helpers
    /vectors/            #   W4 generator, checker and vector JSON      [built]
    /checks/             #   build-time guards (e.g. no_mbedtls_hkdf.py) [built]
    /simctl/             #   simnode console scripting                 [planned]
/ha/                     # example discovery payloads + automations
/hardware/carrier/       # GateLink carrier: perfboard layout, module list, BOM
/docs/                   # this document set, organized by owner:
    /shared/             #   Protocol Specification, Decision Register,
                         #     Protocol Library Implementation Plan
    /bridge/  /gatelink/  /welllink/  /rangetest/     #   per-node documents
    /protocol-lib/       #   engineering log for the shared codec
    /archive/            #   superseded revisions and the Research Archive
                         # planned, not yet written:
    1050-config.md       #   as-programmed gate controller settings
    bms-protocol.md      #   TDT BLE protocol + reference captures
    mppt-config.md       #   as-configured MPPT settings
    gatelink-config.md   #   generated parameter reference
    /<node>/engineering-log.md   # one per node, created at its bring-up
LICENSE                  # MIT — holder name pending D31   [not yet created]
THIRD_PARTY_NOTICES.md                                    [not yet created]
```

### 9.2 Per-subproject context files

Two files per node, for two different audiences:

| File | Audience | Contains |
|---|---|---|
| `/firmware/<node>/CLAUDE.md` | **Claude Code** | Build commands, target layout, coding conventions, which spec sections bind this target, what not to change without a protocol version bump |
| `/docs/<node>/engineering-log.md` | **Humans, and future Claude sessions** | Dated running record: what was tried, what was measured, what was decided and why, what is still unknown |

> **Why both.** The formal documents state what the system *should* be. Neither of
> them is the right place for "tried X on the bench, it did not work because Y" — but
> that is exactly the information that is most expensive to lose and most useful to
> hand to a fresh session. The need for this became obvious during the BMS PoC, where
> the reasoning behind several dead ends was nearly lost.

### 9.3 Build and CI

- **PlatformIO** multi-environment build, one environment per firmware target, in
  VS Code with Claude Code.
- **CI: GitHub Actions building all firmware targets on push.** With several targets
  sharing `/lib/lran-protocol/`, CI catches a protocol change breaking a node nobody
  rebuilt locally. This is not optional in a project whose whole compatibility story
  rests on one shared library.
- **Protocol test vectors run in CI.** A fixed key, known frames, and expected MACs and
  CRCs, committed to `/lib/lran-protocol/test/`. Two firmwares developed independently
  against a prose specification will diverge; test vectors are what stop that.

### 9.4 Configuration — one source of truth

`/lib/lran-config/` declares every runtime parameter **once** — name, type, unit,
range, default. The firmware defaults, the HA `number` discovery payloads and
`/docs/gatelink-config.md` are all **generated** from that table.

> Three hand-maintained copies of a parameter table drift, and the drift is silent: HA
> offers a range the firmware clamps, or documentation describes a default that changed
> two revisions ago.

### 9.5 Secrets and updates

- **Secrets:** LoRa `master_key`, WiFi credentials, MQTT credentials and the OTA
  password live in untracked config / build flags and are never committed.
- **Updates:** bridge by OTA + USB; remote nodes USB only.
- Nice's own reference documents and Victron's protocol documents are third-party
  copyrighted: **link them, do not vendor them.**

---

## 10. System safety principles

Node-specific safety requirements are in the node PRDs. These four bind the whole
system.

1. **LRAN is never a safety authority.** Each controlled device's own controller
   retains obstruction, interlock and detector logic. LRAN issues commands through
   documented accessory interfaces and *observes* state; it must never be relied on to
   prevent unsafe operation. Detection and alerting features are **monitoring and
   notification features, not safety features.**
2. **No safety interlock is bypassed, ever.** Where a controller exposes an input that
   clears a latched safety state, LRAN does not drive it. It may sense it and report
   it.
3. **No maintained assertions.** Every LRAN output is a momentary pulse on a
   normally-open contact. An unpowered, crashed or removed node asserts nothing, and
   the controlled device behaves exactly as it does without LRAN present.
4. **Authenticated configuration.** A timing parameter is a command in every sense that
   matters — pulse widths and detection windows change how a device is driven and when
   alerts fire. Configuration frames are authenticated on the same terms as commands.

---

## 11. Third-party code and licenses

### 11.1 Inventory

| Component | Source | License |
|---|---|---|
| VE.Direct parser + HEX | `osh-labs/VE.Direct_mppt_arduino` | **MIT — confirmed** |
| LoRa radio driver | RadioLib | MIT |
| BLE stack | NimBLE-Arduino | Apache-2.0 |
| **BMS client** | **Own implementation of the TDT protocol**, written against `aiobmsble` as a behavioural reference | **No third-party code vendored.** If any is later taken from `aiobmsble` / `BMS_BLE-HA`, verify its license first |
| Platform HAL | `m5stack/M5StamPLC` + `M5Unified` | **MIT** — verify at the pinned commit |
| LCD driver | LovyanGFX (via M5Unified) | BSD-2-Clause |
| Arduino-ESP32 core | Espressif | LGPL-2.1-or-later |
| ESP-IDF components | Espressif | Apache-2.0 |
| mbedTLS (HMAC, HKDF) | via ESP-IDF | Apache-2.0 |
| MQTT client | PubSubClient | MIT |
| JSON | ArduinoJson | MIT |
| Display | U8g2 (Heltec OLED targets only) | BSD-2-Clause |
| SD / filesystem | via ESP-IDF / Arduino core | Apache-2.0 / LGPL-2.1-or-later |
| NVS / Preferences | via ESP-IDF | Apache-2.0 |
| *Nice BusT4 protocol logic* | *`pruwait` / `xdanik` / `makstech` lineage* | ***GPL-3.0 — not used in v1.*** Research archive only |

### 11.2 Project license — MIT (**D11**)

The stack above is MIT, Apache-2.0, BSD-2-Clause and LGPL-2.1-or-later (the last
dynamically satisfied by the Arduino core in the usual embedded way). **None imposes
copyleft**, so MIT is available, and it is the simplest thing that meets the project's
aims: maximum reuse, attribution only.

> **How this was reopened and closed.** An earlier revision resolved the license to
> GPL-3.0 because porting the Nice BusT4 community lineage would have made the firmware
> a derivative work of GPL-3.0 code. When BusT4 left v1, that obligation disappeared
> and the question became a free choice.

**Done:** `LICENSE` at the repo root carries the MIT text and
`Copyright (c) 2026 Robert J. Lee`. **D31 closed 2026-09-08** — a personal name rather
than a project or entity name — and every source file carries it in place of the former
`<holder>` placeholder. The remaining §11.3 obligation is `THIRD_PARTY_NOTICES.md`.

**If BusT4 is ever pursued**, a port linking the GPL-3.0 community lineage would make
*that binary* GPL-3.0 regardless of this repo's stated license. Keep such a port in its
own clearly-marked subtree so the copyleft scope is explicit at that point rather than
assumed now.

### 11.3 Repo obligations

- `LICENSE` at root — MIT, `Copyright (c) 2026 Robert J. Lee` (**D31**, closed 2026-09-08). **Done.**
- `THIRD_PARTY_NOTICES.md` at root — MIT and BSD components require attribution
  retention. **Done 2026-09-08.** It records what is **actually in a build**, which is not
  the same set as §11.1's planned inventory, and its §3 lists the three differences: §11.1
  omits Unity and the ThingPulse SSD1306 driver; its LCD row names M5Unified where the
  build resolves M5GFX (MIT, with LovyanGFX BSD-2-Clause inside); and PubSubClient,
  ArduinoJson, U8g2 and the VE.Direct parser have no build yet. **Update it in the same
  commit as any `lib_deps`, platform-pin or framework-version change.**
- Vendor reference documents (Nice 1050 manual, TTPCI manual, DMBM integration protocol;
  Victron VE.Direct protocol documents): **link, do not vendor.**

---

## 12. Document set

| Document | Covers | Status |
|---|---|---|
| **`LRAN-System-PRD`** *(this document)* | System architecture, node overviews, protocol overview, repo and build, licenses | v0.3 |
| [`LRAN-Protocol-Specification`](./shared/LRAN-Protocol-Specification.md) | All LoRa frame and MQTT protocol definitions. **Referenced by every node document** | **v0.8** (`ver = 2`) |
| [`LRAN-Decision-Register`](./shared/LRAN-Decision-Register.md) | **D1–D34** and the measurement backlog **M1–M23**. Single source of truth for decision status | v0.6 |
| [`LRAN-Protocol-Library-Implementation-Plan`](./shared/LRAN-Protocol-Library-Implementation-Plan.md) | `/lib/lran-protocol/` API, tests and milestones. **P1–P7 complete; P8 (`CommandGate`, D34) outstanding** | v0.3 |
| [`LRAN-Bridge_Node-PRD`](./bridge/LRAN-Bridge_Node-PRD.md) | LoRaBridge goals and requirements | v0.3 |
| [`LRAN-Bridge_Node-Implementation-Plan`](./bridge/LRAN-Bridge_Node-Implementation-Plan.md) | LoRaBridge BOM, firmware architecture, milestones; also owns `lran-simnode` (§10) | v0.6 |
| [`LRAN-GateLink_Node-PRD`](./gatelink/LRAN-GateLink_Node-PRD.md) | GateLink goals and requirements | v0.3 |
| [`LRAN-GateLink_Node-Implementation-Plan`](./gatelink/LRAN-GateLink_Node-Implementation-Plan.md) | GateLink BOM, interconnect, firmware architecture, milestones, integration observations | v0.3 |
| [`gatelink-expansion-board`](./gatelink/gatelink-expansion-board.md) | GateLink carrier board: schematic intent, net assignments, BOM, mechanical | rev 0.3 |
| [`LRAN-WellLink_Node-PRD`](./welllink/LRAN-WellLink_Node-PRD.md) | WellLink — placeholder, to be developed | v0.3 |
| [`LRAN-Range-Test-Firmware-Pass1-Tasks`](./rangetest/LRAN-Range-Test-Firmware-Pass1-Tasks.md) | Range test firmware task list — **the next firmware target**. Answers D1, hosts W9, M6 and M20 | pass 1 |
| [`LRAN-Research-Archive`](./archive/LRAN-Research-Archive.md) | BusT4 bench findings and Phase 2 design, superseded design history, BMS investigation dead ends | v0.1 |

> **Document versions are the reader's staleness check.** A node document citing a
> protocol version older than the specification's own means its body has not been
> reconciled with the intervening revisions — which was true of every node document
> until v0.2 of this one.

### 12.1 Document conventions

- **The protocol specification is authoritative** for anything on the wire. Node
  documents reference it and never restate a layout.
- **Requirements documents** state goals and requirements only. Where a requirement
  names a specific part or value, it is because the choice *is* the requirement.
- **Implementation plans** are the basis for firmware development and validation and
  are what is handed to Claude Code for a given node.
- **The decision register is the only place a decision's status is recorded.** Other
  documents reference decisions by number and describe the outcome, never the status.
- Rationale appears in the body where it aids understanding, and in blockquoted
  footnotes where it would otherwise interrupt the flow.
- `TBM` marks a value to be measured; every `TBM` has a row in the measurement backlog.

---

## 13. Changelog

- **v0.5** — **D31 closed** (Decision Register v0.6): the copyright holder is
  **Robert J. Lee**, a personal name rather than a project or entity name. §11.2's
  pending action becomes a statement of fact — `LICENSE` exists at the repo root with the
  MIT text and `Copyright (c) 2026 Robert J. Lee` — and the `<holder>` placeholder is gone
  from every source file. **§11.3's second obligation is met in the same revision:**
  `THIRD_PARTY_NOTICES.md` is written. §11.2 previously said the holder name was "the only
  thing" standing between this repo and a public push, which was not accurate — the
  attribution obligation of the MIT and BSD components in §11.1 was always there too, and
  is now discharged rather than restated. **The notices file also audits §11.1 against the
  build and finds it incomplete in three ways**, recorded in its §3 rather than corrected
  here, because §11.1 describes the design as planned and the discrepancies are findings
  about the build. Document set table refreshed: the Decision Register is v0.6 and its
  backlog runs to **M23**, not M21.
- **v0.4** — Citation refresh only. Protocol specification **v0.7 → v0.8**, which closes **W9** (the full-size and fragmented `PING` bench runs both passed over RF on 2026-09-05) and changes **no frame layout, header field, authentication scope or schema length**; no vector regenerates. **Pass 1 of the range test firmware is complete**: R1–R11, M20's occupant inventory and W9 all closed. **D1 now waits only on M21**, the modules' FCC grant conditions, which is paperwork rather than bench work — and D1 is what blocks node firmware.
- **v0.3** — Document set table refreshed for **D34**, which closes Protocol Spec
  **W12** by splitting §9.4 steps 4–6: the replay and dedup gate becomes `CommandGate`
  in `/lib/lran-protocol/` (library milestone **P8**), dispatch stays in the
  application. The scheduling consequence is worth carrying at system level — §9.2
  makes **every authenticated type bridge → node**, so the obligation binds the first
  firmware that accepts a `COMMAND` (simnode B0, GateLink M3) and **does not block the
  range test firmware**, which is the next target. Spec v0.7, register v0.3, protocol
  library plan v0.3, bridge implementation plan v0.6.

- **v0.2** — Synchronization revision; **no architectural change**. The `docs/`
  reorganization into `shared/`, `bridge/`, `gatelink/`, `welllink/`, `rangetest/`,
  `protocol-lib/` and `archive/` had left **every relative link in this document
  resolving to nothing**; all are repaired, here and in every other live document.
  **§12's document set table was three revisions behind** — it listed the protocol
  specification at v0.2 against an actual v0.6 and omitted the Protocol Library
  Implementation Plan, the carrier board document and the range test tasks entirely —
  and now carries current versions plus a note on how to read them. Decision range
  updated to **D1–D33** and the backlog to **M1–M21** (see the register's own v0.2
  entry for D32, RadioLib, and D33, the fixed channel at §15.249 power). **§9.1's
  layout is marked with what exists, what is planned and what is next**, since it was
  read as a description of the repo rather than as a target and several of its paths
  have never existed. §5 and §7.1 corrected: the BMS protocol write-up is still in the
  `/wattcycle-reader/` PoC workspace and has **not** been lifted into `/docs/`, which
  the previous text asserted as done — that lift is a prerequisite for the GateLink BMS
  port.

- **v0.1** — Initial release. Created by compartmentalizing `lran-prd-v0_8`, which had
  grown to cover three nodes, two protocols, a hardware platform selection, a power
  budget and a decision register in one document and was no longer reviewable as a
  whole. This document takes v0.8's §1 (overview, naming, WellLink scope), §2 (goals
  and non-goals, with node-specific goals moved to node PRDs), §3 (architecture,
  shared code, decode placement, data flows, AquaLink convergence), §7.1 (HA device
  model), §10 (repo and build), §11 (safety, reduced to the four system-level
  principles), §12 (licenses) and the cross-node parts of §14 (bring-up sequence).
  **Added:** §7, naming the proof-of-concept and verification subprojects explicitly;
  §9.2, per-subproject `CLAUDE.md` and engineering-log files; §12, the document set and
  the conventions binding it. **Moved out:** the decision register and measurement
  backlog to `LRAN-Decision-Register`, so that decision churn does not require editing
  the system overview; all protocol detail to `LRAN-Protocol-Specification` v0.2; all
  node-specific hardware, firmware and test content to the node documents; and BusT4,
  the superseded design history and the BMS investigation dead ends to
  `LRAN-Research-Archive`. **No design change.** `lran-prd-v0_8` is retired as
  superseded; its content is preserved in full across this document set.
