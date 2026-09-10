# LRAN Bridge Node PRD

**Document:** `LRAN-Bridge_Node-PRD`
**Version:** 0.7
**Node:** `LoRaBridge`, node ID `0x00`
**Status:** Requirements settled. Antenna siting and PHY parameters pending the range test.
**Parent document:** [`LRAN-System-PRD`](../LRAN-System-PRD.md)
**Binding protocol:** [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) **v0.9**
**Companion:** [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md)
**Last updated:** 2026-09-10

> **This document states goals and requirements only.** Library selection, task
> structure, OTA partitioning and bring-up procedure live in the implementation plan.
> Frame layouts, enumerations and MQTT topic grammar live in the protocol specification
> and are never restated here.

---

## Table of contents

1. [Overview](#1-overview)
2. [Goals and non-goals](#2-goals-and-non-goals)
3. [Interfaces and protocols](#3-interfaces-and-protocols)
4. [Hardware requirements](#4-hardware-requirements)
5. [Firmware requirements](#5-firmware-requirements)
6. [Home Assistant entity requirements](#6-home-assistant-entity-requirements)
7. [Safety and integrity requirements](#7-safety-and-integrity-requirements)
8. [Test and verification requirements](#8-test-and-verification-requirements)
9. [Changelog](#9-changelog)

---

## 1. Overview

### 1.1 What the bridge is

A **general-purpose LoRa↔MQTT gateway**, not a gate-specific bridge. It receives and
sends LoRa frames to any registered node, connects to the LAN over WiFi, and bridges to
the existing Mosquitto broker on the Home Assistant host. HA entities are created via
MQTT Discovery, one HA device per node.

It sits in the house, is mains powered, has no power constraint, and is the **only node
in the fleet that supports OTA.**

### 1.2 Why the bridge carries the fleet's complexity

Everything that must be told to the system **once rather than N times**, and everything
likely to change after the remote nodes are sealed into their enclosures, lives here:

- the poll schedule and per-node availability,
- MQTT Discovery publication and entity mapping,
- publication policy — publish-on-change, rounding, staleness handling,
- VE.Direct HEX register interpretation and write authorization,
- link diagnostics and protocol version skew reporting.

> **The single test behind all of it:** *does changing this require a physical visit to a
> node?* If yes, it belongs on the bridge. GateLink is ~87 m away with no OTA; the
> bridge is in the house on the LAN. Every allocation of responsibility in this document
> follows from that asymmetry.

### 1.3 Installation context

Indoors, near the LAN, on USB-C mains power. Physically accessible for a USB reflash if
an OTA goes wrong. Its antenna must serve **two bearings** — GateLink and WellLink are
at similar distances in different directions — which makes antenna siting a system
concern rather than a bridge implementation detail (§4.3).

---

## 2. Goals and non-goals

### 2.1 Goals

- **BG-1. Serve multiple independent nodes** over one LoRa channel, with per-node
  addressing, keying, sequence tracking and availability.
- **BG-2. Adding a node requires reflashing only the bridge and the new node**, never an
  existing node.
- **BG-3. Present every node to HA natively** via MQTT Discovery, one HA device per node,
  with stable non-colliding unique IDs.
- **BG-4. Poll each node on its own schedule**, runtime-configurable from HA.
- **BG-5. Know and publish whether each node is alive**, independently of the broker
  connection.
- **BG-6. Proxy VE.Direct HEX** between HA and a node, and **enforce write
  authorization**.
- **BG-7. Decide publication policy** — what gets published, when, and marked how.
- **BG-8. Publish link diagnostics** sufficient to distinguish a marginal RF link from a
  firmware bug without visiting a node.
- **BG-9. Update over the air**, safely, with rollback.
- **BG-10. Provide bench tooling** that exercises the fleet without any node being
  present.

### 2.2 Non-goals (v1)

- **Any gate-specific logic.** The bridge decodes GateLink's schemas because they are
  registered, not because it knows what a gate is.
- **Persisting telemetry.** HA owns history. The bridge is a gateway, not a datastore.
- **Mesh or relaying.** Star topology; nodes never address each other.
- **Local control logic or automations.** HA owns automation. A bridge that also
  automates is a second place to look when something misfires.
- **TLS to the broker.** Local LAN, existing Mosquitto. Reserved for a later migration.
- **Serving a web UI.** Diagnostics go to MQTT, where HA already renders them.

---

## 3. Interfaces and protocols

### 3.1 LoRa

Framing, addressing, authentication, sequencing, fragmentation, media access and version
tolerance are defined in
[`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md). Bridge-specific
obligations:

- **R-3.1a.** The bridge is node `0x00` and is the **only node that initiates polls.**
- **R-3.1b.** The bridge SHALL maintain a **per-node registry**: node ID, node type,
  derived key, `ctx_id`, command `seq`, `last_seen`, missed-poll count, last-heard
  protocol version, and RSSI/SNR.
- **R-3.1c.** The bridge SHALL hold **only the `master_key`** and derive each node's key
  by HKDF. It SHALL NOT store per-node keys as separate provisioned secrets.
- **R-3.1d.** The bridge SHALL **serialize polls** — never more than one outstanding poll
  across the whole fleet. This removes the largest predictable collision source for free.
- **R-3.1e.** The bridge SHALL accept protocol version **N and N−1**, and SHALL downgrade
  per node based on the version last heard from that node.
- **R-3.1f.** A node running an unsupported version SHALL be marked unavailable with a
  **distinct reason**, never silently ignored.
- **R-3.1g.** The bridge SHALL retry an unacknowledged command with backoff, and SHALL
  stop after a bounded number of attempts rather than looping.

> **Retries interact with a hardware safety property.** A retried command reaching
> GateLink is a **second relay pulse**, not an idempotent re-send. The protocol's
> `(ctx_id, seq)` deduplication is what makes bridge-side retry safe — the bridge must
> not work around it by varying `seq` on a retry.

### 3.2 WiFi

- **R-3.2a.** WiFi station mode, credentials in untracked build config.
- **R-3.2b.** The bridge SHALL reconnect automatically and SHALL NOT block LoRa receive
  while doing so. **A node's status frame arriving during a WiFi outage must still be
  received**, even if publication is deferred.
- **R-3.2c.** WiFi RSSI SHALL be published as a diagnostic.
- **R-3.2d.** BLE SHALL be disabled.

### 3.3 MQTT

Topic grammar, retention rules and discovery conventions are in Protocol Spec §16.
Bridge-specific obligations:

- **R-3.3a.** The bridge SHALL connect to the existing Mosquitto broker on the HA host,
  with credentials in untracked config, and SHALL register a **Last Will and Testament**.
- **R-3.3b.** The bridge SHALL publish **MQTT Discovery configuration on boot and on
  every broker reconnect** — one HA device per registered node plus the bridge itself.
- **R-3.3c.** Discovery `unique_id`s SHALL be prefixed per node so they are stable and
  non-colliding as the fleet grows.
- **R-3.3d.** Every node entity's discovery config SHALL reference **that node's**
  availability topic, not the bridge's LWT.
- **R-3.3e.** The bridge SHALL subscribe to command and configuration topics and
  translate them into authenticated LoRa frames addressed to the target node.
- **R-3.3f.** The MQTT client SHALL support a **payload large enough for a Discovery
  configuration**. A default packet limit smaller than a discovery config fails
  confusingly rather than loudly, and is a known trap in this class of library.

### 3.4 Per-node availability

MQTT LWT covers only the bridge's connection to the broker. **It says nothing about
whether a node is alive.**

- **R-3.4a.** The bridge SHALL maintain `last_seen` per node.
- **R-3.4b.** A node SHALL be marked `offline` after `missed_poll_threshold` (default
  **3**) consecutive unanswered polls, and `online` on any valid frame received.
- **R-3.4c.** Availability SHALL be published **retained** to that node's availability
  topic.
- **R-3.4d.** The bridge's own LWT SHALL mark all nodes unavailable implicitly.

### 3.5 VE.Direct HEX proxy

HA must be able to read and write **all** MPPT configuration and status registers. The
node is transport only; **the bridge is where the register model and the authorization
live.**

- **R-3.5a.** The bridge SHALL accept a raw HEX request from HA, wrap it in a protocol
  frame addressed to the target node, and publish the node's response.
- **R-3.5b. Three independent gates SHALL apply to write-class requests**, and all three
  must pass:

| # | Gate | Enforced by |
|---|---|---|
| 1 | **A valid MAC** on Set and Restart commands | Protocol Spec §7.6, verified at the node |
| 2 | **An armed write-enable switch**, HA-visible, **default off**, with **auto-expiry after `mppt_write_arm_timeout_s` (default 300)** | **The bridge** — it refuses to build a write frame while disarmed |
| 3 | **A retained audit trail** of every write attempt: request payload, authorization outcome, and the MPPT's response | **The bridge** |

- **R-3.5c.** Gate 2 SHALL be deliberately a **two-step operation** — arm, then write.
- **R-3.5d.** The bridge SHALL interpret register semantics for readback and publish
  charge-parameter readback as diagnostic sensors, **so that a wrong charge profile is
  visible in HA rather than latent.**
- **R-3.5e.** Raw HEX in and out is the **v1 interface**. Once the small set of registers
  actually adjusted in practice is known, promote those to named `number` / `select`
  entities. **Do not attempt to model 100+ registers as entities.**

> **Why the bridge and not the node.** Register semantics are the most change-prone part
> of this interface, and getting one wrong under LiFePO4 is a battery-damage path.
> Putting the model on the OTA-capable, mains-powered device in the house means a
> correction is a deploy, not a walk to the gate with a laptop.

---

## 4. Hardware requirements

### 4.1 Platform

- **R-4.1a.** The bridge SHALL be a **Heltec WiFi LoRa 32 V3** (ESP32-S3 + SX1262 +
  OLED).

> **Why this platform stays, when GateLink left it.** GateLink moved to an industrial
> controller because it needed four relays and six isolated inputs. **The bridge needs
> none of that** — its I/O is a radio, WiFi and a status display, all of which the Heltec
> integrates on one board with an antenna connector and USB-C power. The platform
> question that forced GateLink's change does not arise here. Keeping the Heltec
> also preserves it as the **reference radio** for the range test (§8, V-B1), where
> having two identical known-good radios is worth more than matching the node's hardware.

- **R-4.1b.** The radio pin map, TCXO voltage and RF-switch mode SHALL be **injected by
  configuration**, not compiled in, so one SX1262 driver serves both this board's fixed
  internal pins and GateLink's carrier.
- **R-4.1c.** The onboard OLED MAY remain on. There is no power constraint, and a
  glanceable "N nodes online" display in the house is useful.

### 4.2 Power

- **R-4.2a.** USB-C mains power. No battery, no power budget, no sleep states.
- **R-4.2b.** The bridge SHALL recover automatically from a power interruption with no
  manual intervention — it will be on household mains and will see them.

### 4.3 Antenna

- **R-4.3a.** A **915 MHz antenna appropriate to the chosen PHY parameters**, on the
  board's existing connector. **Confirmed 2026-09-06:** a vertically mounted 19 cm stick of
  nominally **3.0 dBi**, the same part fitted at the GateLink end. That figure is a direct
  term in the EIRP calculation (Protocol Spec §18.2) and SHALL be what is configured as
  `antenna_gain_dbi`, giving a conducted ceiling of **−4 dBm** under Envelope A.
- **R-4.3b. Antenna siting is a two-bearing problem and SHALL be resolved by measurement,
  not by assumption.** GateLink and WellLink sit at similar distances in **different
  directions**. Favour an **omnidirectional antenna in a central, elevated position**
  over anything with a pattern optimized toward the gate.
- **R-4.3c.** **Both bearings SHALL be range-tested before the bridge's location is
  committed** (**M6**).

> Siting the bridge for the gate and discovering later that the well is in a null is a
> mistake that costs a re-run of the install, not a firmware change. It is cheap to
> measure first and expensive to measure second.

**The bridge sits indoors, and the walk measured what that costs.** The 2026-09-04 trace
was taken with the initiator at the bridge's target location in the NW office, so every
reading in it crosses at least one framed exterior wall. Two positions ~40 m out on
opposite faces differed by **18.2 dB**, consistently across all 24 matched test points —
the structure, not terrain or height. **R-4.3b's "measure, do not assume" applies to the
wall as much as to the bearing**, and no distance-based estimate reproduces that term.

### 4.4 Radio coexistence — the bridge deliberately does not enforce mutual exclusion

Recorded because **GateLink does** (GateLink PRD R-4.3h), and an undocumented asymmetry
reads as an oversight to the next person.

- **R-4.4a.** The bridge SHALL NOT gate LoRa transmit against WiFi activity. Its WiFi must
  hold a live network connection; a radio that cannot be silenced cannot be interlocked.
- **R-4.4b.** The bridge SHALL publish LoRa RSSI/SNR and PER diagnostics sufficient to
  detect coexistence degradation after the fact (already required by R-5.5b and §6.1).
- **R-4.4c.** If **M22** shows PER degrading under WiFi load, the remedy is **physical
  antenna separation via the IPEX pigtail**, not firmware arbitration.

**Three reasons the asymmetry is safe, in descending order of how much weight they carry.**

1. **The link that cannot be gated is the one that needs protecting least.** WiFi to the AP
   typically arrives at −40 to −60 dBm with 20–40 dB of blocking headroom. GateLink's BLE
   link to the BMS has no such margin and shares a metal cavity with the transmitter.
2. **There is no mechanism to build on.** ESP-IDF's coexistence arbitration exists because
   WiFi and Bluetooth share one radio on the ESP32-S3. It has **no visibility into an
   SPI-attached SX1262**. Strict mutual exclusion here is not a matter of effort — the hook
   does not exist.
3. **The reverse direction is implausible.** WiFi TX desensing the bridge's LoRa RX would
   need broadband PA noise at 915 MHz, 1.5 GHz below the WiFi fundamental and well outside
   the SX1262 front-end passband; harmonics do not help it either. Heltec also certified
   both radios on this board as one finished product at comparable powers, so the
   combination has been through a chamber — **as evidence about the hardware, not as
   authorization** (Protocol Spec §18.2).

**No regulatory driver either.** Part 15 does not prohibit simultaneous transmitters; the
multi-transmitter procedures are a *certification* concern, and this project is on §15.23
regardless.

> **What would falsify this.** **M22**: run a sustained MQTT or iperf flood while the
> bridge receives a known `PING` sequence, and compare PER and RSSI against the WiFi-idle
> baseline. Tracked in the Decision Register §5.3.

---

## 5. Firmware requirements

### 5.1 Responsibilities

1. **LoRa RX** → verify → decode per source node → publish to MQTT state topics.
2. **MQTT command subscribe** → build an authenticated LoRa frame addressed to the target
   node → transmit → await ACK → publish the result; retry with backoff.
3. **VE.Direct HEX proxy**, including write-authorization enforcement (§3.5).
4. **Publish MQTT Discovery** on boot and reconnect, one HA device per registered node
   plus the bridge (§3.3).
5. **Availability** — its own via LWT, per node via watchdog (§3.4).
6. **Per-node poll scheduler**, each runtime-configurable from HA.
7. **Per-node link diagnostics** — RSSI, SNR, last-seen, missed polls, protocol version,
   receive-path discard counters.
8. **Republish node events** to non-retained MQTT event topics.
9. **Debug tooling** (§5.4).
10. **Serve OTA** (§5.3).

### 5.2 Publication policy — the bridge decides

Nodes transmit everything they know in each status frame. **The bridge decides what to
publish and when** (Protocol Spec §16.4).

- **R-5.2a. Publish on change** for noisy values. Per-cell battery voltages jitter 1–2 mV
  between polls from ADC noise and SHALL NOT be pushed into HA history at the poll rate.
- **R-5.2b. Mark stale rather than republish.** When a node reports a staleness flag or
  age exceeding a threshold, the affected entities SHALL be marked **unavailable**. **The
  last good value SHALL NOT be republished as though current.**
- **R-5.2c. Absence is not zero.** Sentinel values indicating "not available" SHALL NOT
  be published as numeric readings.
- **R-5.2d. Propagate synthetic marking.** Frames marked as debug-synthetic SHALL be
  marked as such all the way into HA history.
- **R-5.2e. Event topics are never retained** (Protocol Spec §16.3), and the bridge SHALL
  deduplicate on the node's event ID so a retransmitted event publishes once.

> **R-5.2b is the requirement most easily lost in implementation**, because republishing
> the cached value is the path of least resistance and produces a dashboard that looks
> healthy. A dead VE.Direct link showing plausible unchanged numbers indefinitely is
> worse than an obviously unavailable entity.

### 5.3 OTA

- **R-5.3a.** The bridge SHALL support OTA over WiFi **and** USB.
- **R-5.3b.** The OTA endpoint SHALL be **authenticated**, with the password or key in
  untracked config.
- **R-5.3c. Dual-partition (A/B) with rollback SHALL be used.** A bricked bridge takes
  the whole property's telemetry offline.
- **R-5.3d.** OTA SHALL be **disabled during an active LoRa transaction** and deferred
  until idle.
- **R-5.3e.** The firmware version SHALL be published and exposed as a diagnostic sensor.

> **The asymmetry is deliberate** (**D16**). The bridge is on the LAN, mains powered,
> physically accessible, and the node whose firmware changes most often. Remote nodes are
> ~87 m away, a bad flash is a walk with a laptop, and there is no second radio path to
> recover through. **The bridge gets OTA precisely because it can afford to fail at it.**

### 5.4 Debug and bench tooling requirements

- **R-5.4a. Packet loopback** — RF echo and internal loopback with no radio.
- **R-5.4b. Dummy status publish** — synthetic decoded payloads pushed to MQTT with no
  node present, exercising discovery, entity mapping and publication policy end to end.
- **R-5.4c. Per-node simulators**, so HA integration can be developed and demonstrated
  before any node exists.
- **R-5.4d.** A **raw frame log** — every frame received and transmitted, with source,
  type, schema, RSSI, SNR and the discard reason where applicable, published or logged
  for bench capture.
- **R-5.4e.** Leveled serial logging.

> **R-5.4b and R-5.4c together are what let HA integration proceed on the bench**, which
> is what keeps the critical path off a node that is still on a workbench with its
> carrier half-populated.

### 5.5 Diagnostics

- **R-5.5a.** The bridge SHALL publish, per node: RSSI, SNR, last-seen, missed-poll
  count, protocol version, and the receive-path discard counters defined in Protocol Spec
  §14.
- **R-5.5b.** The bridge SHALL publish its own uptime, WiFi RSSI, firmware version and a
  count of nodes currently online.

> **Silent discards are the enemy of field debugging.** The counters are how a marginal
> link is distinguished from a firmware bug when the node is ~87 m away in the rain, and
> they are worth nothing if they stay inside the bridge.

---

## 6. Home Assistant entity requirements

### 6.1 Bridge's own device

| Entity | Type | Notes |
|---|---|---|
| Bridge availability | via LWT | |
| WiFi RSSI | `sensor` (diagnostic) | |
| Bridge uptime | `sensor` (diagnostic) | |
| Firmware version | `sensor` (diagnostic) | §5.3 |
| Nodes online | `sensor` (count, diagnostic) | |

### 6.2 Published on behalf of each node

The bridge owns these even though they describe a node, because the bridge is what
measures them:

| Entity | Type | Notes |
|---|---|---|
| Node availability | — | Retained, per node (§3.4) |
| LoRa RSSI / SNR | `sensor` (diagnostic) | Per node |
| Missed polls | `sensor` (diagnostic) | Feeds §3.4 |
| Protocol version | `sensor` (diagnostic) | Shows node/bridge skew |
| Receive discard counters | `sensor` (diagnostic) | Per node |
| Poll interval | `number` (config) | Per node, runtime-configurable |
| MPPT config write enable | `switch` | Armed, default off, auto-expiring (§3.5) |

Node application entities — gate state, battery, solar, detection — are defined in the
owning node's PRD. The bridge publishes them; it does not define them.

---

## 7. Safety and integrity requirements

- **BS-1. The bridge is not a safety authority and holds no control logic.** It
  translates. Any behaviour that looks like a decision about the physical world belongs in
  HA or in a node.
- **BS-2. The bridge enforces two of the three gates on charge-controller writes**
  (§3.5b). These are safety requirements, not conveniences: the failure mode is battery
  damage, and it is invisible until it is not.
- **BS-3. The bridge SHALL NOT vary a command's sequence number on retry.** Doing so
  defeats node-side deduplication and turns a retry into a second relay pulse.
- **BS-4. The bridge SHALL NOT publish a stale value as current** (§5.2b).
- **BS-5. Secrets — `master_key`, WiFi and MQTT credentials, OTA password — SHALL live in
  untracked config and SHALL never be committed.**
- **BS-6. A bridge failure SHALL leave every node in a safe state by construction.**
  Nodes assert nothing without a command, so a dead bridge means loss of remote control
  and telemetry, never an actuation.
- **BS-7. OTA SHALL be rollback-capable** (§5.3c).

---

## 8. Test and verification requirements

| # | Must be proven | Why it is not optional |
|---|---|---|
| **V-B1** | **Range and RSSI at each node site on both bearings**, with a second radio, and a bridge location chosen from the result | Resolves **D1** and sites the antenna. **Both bearings measured; M6 closed 2026-09-09** — §8.1. **Still outstanding: D1 itself, and the bridge antenna choice** |
| **V-B2** | Per-node registry behaviour: addressing, per-node key derivation, context resync, sequence tracking — **against `simnode`, with more than one node present** | The multi-node design is where a protocol error would be most expensive to find late, and `simnode` is the only way to find it before WellLink exists |
| **V-B3** | Availability watchdog marks a node offline after the threshold and online again on the next valid frame | This is the only thing that distinguishes "node is dead" from "node is quiet," and LWT does not do it |
| **V-B4** | Discovery publishes one device per node with correct availability references, and **republishes correctly on broker reconnect** | The reconnect path is the one that gets skipped and the one that runs at 3 AM |
| **V-B5** | Command round-trip end to end, including **retry behaviour with a deliberately dropped ACK**, confirming the node executes once | **BS-3.** A relay pulse is not idempotent |
| **V-B6** | HEX proxy: read passes, write rejected while disarmed, write accepted while armed, **arm auto-expires**, and every attempt appears in the retained audit trail | Three gates are only three gates if each is tested independently |
| **V-B7** | Publication policy: publish-on-change suppresses jitter, staleness marks entities unavailable, sentinels are not published as numbers, synthetic frames stay marked | **R-5.2b** is the one that silently degrades into "republish the cache" |
| **V-B8** | Events publish non-retained, exactly once, and **do not replay on HA restart or discovery refresh** | These drive email and SMS |
| **V-B9** | OTA succeeds, and **a deliberately bad image rolls back** | An untested rollback is not a rollback |
| **V-B10** | Version tolerance: a node announcing N−1 decodes correctly; a node announcing an unsupported version is marked unavailable with a distinct reason | This is what makes an incremental protocol rollout possible instead of a flag day |
| **V-B11** | Full fleet operation with **no node hardware present**, using simulators and dummy publish | **R-5.4b/c.** If this cannot be done, HA integration is blocked behind a workbench |
| **V-B12** | **LoRa PER with WiFi idle vs. saturated**, against a known `PING` sequence | **M22.** The evidence for R-4.4's deliberate lack of mutual exclusion. Without it the asymmetry rests on argument alone |

### 8.1 V-B1 — what the measurements closed, and what they did not

**The range work is deliberately host-independent.** Two Heltec boards characterize the
PHY faster than waiting on GateLink's carrier, and the parameter selection transfers
unchanged. Link margin does not transfer, which is why B1b re-measured on the module
GateLink will carry (Bridge Implementation Plan §2.3).

**Both bearings are measured and M6 closed 2026-09-09.** The gate is **~87 m** and closed
0 % PER on the deployed pairing (**B1b**, 2026-09-09, within 6 in of the final antenna
position). The well is **~100 m** and closed 2.08 % PER — the 2026-09-04 walk's P3,
identified as `welllink-well` on 2026-09-09.

**The "~500 ft" V-B1 originally asked for was a guess predating any walk**, and the
measurements retire it rather than confirm it. Decision Register §5.1.1 carries the full
closure, including which of the two bearings the original wording overstated.

**D1 and the bridge antenna choice remain open.** M6 answered where the nodes are and
what the link does there; it did not fix a PHY configuration or site the antenna.

---

## 9. Changelog

| Version | What changed |
|---|---|
| **v0.7** | Readability pass — **new §8.1** takes V-B1's measurement detail out of the table cell; §9 gains a version index |
| **v0.6** | §8's duplicate `V-B2` resolved — the coexistence row becomes **`V-B12`** |
| **v0.5** | **§4.4**, the bridge's deliberate lack of LoRa/WiFi mutual exclusion, with **M22** as its falsifier; R-4.3a's confirmed antenna |
| **v0.4** | Spec v0.8 citation; `cad_backoffs` now has two candidate causes at this end |
| **v0.3** | Spec v0.7 citation; **D34** reaches an empty set here — the bridge receives no authenticated types |
| **v0.2** | Spec v0.6 citation, body reconciled first; cross-document links repaired |
| **v0.1** | Initial release, extracted from `lran-prd-v0_8` and restated as requirements |

- **v0.7** — **Readability pass; no requirement changed and no measurement restated.**
  **New §8.1** carries what V-B1's table cell had grown to hold — M6's closure, both
  distances, both PER figures, the retired "~500 ft" and the two items still open. At
  roughly 700 characters it was a paragraph wearing a table row, in the one column a
  reader scans to find out what a criterion asks for. The row now states the criterion and
  its status and points at §8.1. §9 gains a **version index** above the entries, which stay
  at full length as the dated records they are.

- **v0.6** — **The duplicate `V-B2` in §8 is resolved.** v0.5 added the WiFi/LoRa
  coexistence row as `V-B2`, an identifier §8 already used for the per-node registry
  verification, leaving twelve rows under eleven identifiers. The coexistence row becomes
  **`V-B12`** and moves to the end of the table; the registry row keeps `V-B2`, which is
  what Bridge Implementation Plan §7.1 and milestone **B3** already resolve it to. The
  v0.5 entry below is left as written — it records what that revision did — and this entry
  is the correction. **No requirement changed**, and the coexistence criterion is
  unaltered apart from its number. Commits and PR descriptions cite these identifiers
  (root `CLAUDE.md`), so a colliding one is a defect rather than an untidiness.

- **v0.5** — **M21's coexistence and antenna findings folded in.** New **§4.4** records
  that the bridge **deliberately** does not enforce LoRa/WiFi mutual exclusion while
  GateLink does (R-4.3h) — the link that cannot be gated is the one with 20–40 dB of
  headroom, ESP-IDF's coexistence arbitration has no visibility into an SPI-attached
  SX1262 so there is no hook to build on, and the reverse direction is implausible at
  1.5 GHz offset. **M22** is named as the check that would falsify it, and **V-B2** added.
  **R-4.3a** now carries the confirmed antenna — a 19 cm stick at nominally 3.0 dBi, same
  part both ends — which fixes the conducted ceiling at −4 dBm under Envelope A. §4.3 gains
  the walk's measured structure term: two positions on opposite faces of the house at the
  same range differed by **18.2 dB**. Binding protocol advanced to **v0.9**.

- **v0.4** — Citation refresh only. Protocol specification **v0.7 → v0.8**, which closes
  **W9** (the full-size and fragmented `PING` bench runs both passed over RF on
  2026-09-05) and changes **no frame layout, header field, authentication scope or
  schema length**; no vector regenerates. **Relevant here through `cad_backoffs`.**
  §12.3 now records that a backoff window shorter than one frame's airtime cannot
  outlast the frame it backed off for, which spends `cad_retries` against a single
  neighbour. The bridge is the end §12.3 already expects to see the higher count — it
  lives where the third-party equipment is — so a raised `cad_backoffs` here has two
  candidate causes now, not one, and the SF is what separates them.
- **v0.3** — Citation refresh only. Protocol specification **v0.6 → v0.7**, which
  captures **D34** (Protocol Spec W12: §9.4 steps 4–5 become `CommandGate` in
  `/lib/lran-protocol/`, dispatch stays in the application) and changes **no frame
  layout, header field, authentication scope or schema length**. **R-3.1e** and §5's
  counter requirements are unaffected: per §9.2 the bridge receives no authenticated
  types today, so §9.4 steps 4–6 apply to an empty set here and `rx_rejected_seq` /
  `rx_dup_command` reading zero on the bridge is correct.
- **v0.2** — Housekeeping revision; **no requirement changed**. The binding protocol
  citation moves **v0.2 → v0.6**. The body was reconciled against the v0.3–v0.6 changes
  before the citation was moved: this document states no frame layout, header size,
  counter name or schema length of its own, so the 16-byte header (v0.3), the
  `HEALTH 0xF0` → `STATUS 0xF0` rename (v0.4), the §14.1 counter registry (v0.5) and the
  §11.2 single-frame rule (v0.6) reach it only through references that are already
  correct. **Nothing was found to conflict.** Cross-document links repaired for the
  `docs/` reorganization into `shared/`, `bridge/`, `gatelink/`, `welllink/` and
  `rangetest/` — every relative link in this document previously resolved to nothing.
- **v0.1** — Initial release. Extracted from `lran-prd-v0_8` §3.1, §6.1–6.3, §6.6.3–6.6.5,
  §7.5 and the bridge-relevant parts of §6.4.5, §6.4.6 and §6.7. **Restated as
  requirements**, with library selection, OTA mechanics and task structure moved to
  [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md), and
  all frame, enumeration and topic detail replaced by references to
  [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) v0.2. **Added:**
  §1.2, stating explicitly why the bridge carries the fleet's complexity — the
  does-this-need-a-site-visit test was implicit throughout v0.8 but never written down as
  the governing principle; §5.2, publication policy as bridge requirements rather than
  scattered remarks, including the staleness rule that is easiest to lose in
  implementation; §4.1 rationale for why the bridge keeps the Heltec platform that
  GateLink left; §7, seven bridge-specific safety and integrity requirements, of which
  BS-3 (never vary `seq` on retry) was previously only implied by the node-side
  deduplication requirement; §8, eleven numbered verification requirements. **No design
  change.**
