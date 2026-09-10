# LRAN WellLink Node PRD

**Document:** `LRAN-WellLink_Node-PRD`
**Version:** 0.6
**Node:** `WellLink`, node ID `0x02`
**Status:** **PLACEHOLDER.** Scope and reserved allocations only. Not ready for design or build.
**Parent document:** [`LRAN-System-PRD`](../LRAN-System-PRD.md)
**Binding protocol:** [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) **v0.10**
**Last updated:** 2026-09-10

> **This document exists to hold ground, not to specify a node.** It records what is
> already known, what the rest of the system has reserved on WellLink's behalf, and what
> must be answered before design begins. Sections marked *(to be developed)* are
> deliberately empty.
>
> **Nothing here is buildable yet.** The one thing this document is currently good for
> is making sure the decisions being taken *now*, on the bridge and on the protocol, do
> not quietly foreclose WellLink's options.

---

## Table of contents

1. [Scope](#1-scope)
2. [Known requirements](#2-known-requirements)
3. [Reserved allocations](#3-reserved-allocations)
4. [Open questions before design](#4-open-questions-before-design)
5. [Interfaces and protocols](#5-interfaces-and-protocols)
6. [Hardware requirements](#6-hardware-requirements)
7. [Firmware requirements](#7-firmware-requirements)
8. [Test and verification requirements](#8-test-and-verification-requirements)
9. [What the rest of the system already does for WellLink](#9-what-the-rest-of-the-system-already-does-for-welllink)
10. [Changelog](#10-changelog)

---

## 1. Scope

A remote LRAN node at the well, providing **well level monitoring**, with battery
telemetry if the node turns out to be battery powered.

| Attribute | Status |
|---|---|
| Distance | **~100 m (330 ft)** from the house, similar to GateLink's ~87 m. **Measured** — walk P3 of 2026-09-04, identified as this site on 2026-09-09. The earlier "~500 ft" was a guess predating any walk (Decision Register §5.1.1) |
| **Bearing** | **A different direction from GateLink** — a system-level constraint on bridge antenna siting |
| Power | **Open (D19)** — mains is possible; battery/solar must remain viable |
| Primary function | Well level monitoring |
| Expansion | Possible later; not scoped |
| Reporting | Fixed-interval poll/push, **plus an event push on rapid level change** |
| Payload | Level **and** battery status — packet space reserved for both |
| Commands | **None currently anticipated** |

### 1.1 Why it is scoped now and not later

The network was designed for a second node from the outset rather than retrofitted for
one. Three properties of the current design exist because of WellLink, and they are all
cheaper to have now than to add later:

- **8-bit addressing and per-node keys.** A single fleet-wide key would mean compromising
  the well sensor grants gate command authority — an unacceptable coupling between a
  low-value node and the only node that moves a large motorized object.
- **Per-node payload schemas.** WellLink defines its own payload without touching
  GateLink's, and the bridge decodes both.
- **The retained RX duty-cycling design.** Preserved as a reserved protocol feature
  because WellLink *may* be battery powered even though GateLink is not.

---

## 2. Known requirements

Small, and deliberately so. Everything here is carried forward from system-level scoping;
nothing has been designed.

- **WG-1.** Report well level to Home Assistant.
- **WG-2.** Report on a fixed poll or push interval, runtime-configurable from HA like
  every other node's interval.
- **WG-3. Push an event on rapid level change**, independent of the poll schedule.
- **WG-4.** Report battery status **if** the node is battery powered (**D19**).
- **WG-5.** Appear in HA as its own device, with its own availability.

### 2.1 Non-goals, provisionally

- **Commands.** None are currently anticipated. If pump control is ever wanted, that is a
  materially different node with a safety posture of its own, and it should be scoped as
  such rather than added to a sensor.
- **OTA.** Like GateLink, WellLink will be USB-only unless something changes.

---

## 3. Reserved allocations

Already claimed on WellLink's behalf across the document set, so that nothing else takes
them:

| Resource | Reserved value | Reserved in |
|---|---|---|
| Node ID | `0x02` | Protocol Spec §5.3 |
| Status schema | `0x20` | Protocol Spec §7.1 |
| Event schema | `0x21` | Protocol Spec §7.1 |
| MQTT namespace | `lran/welllink/...` | Protocol Spec §16.1 |
| HA device name | "WellLink" | System PRD §6.1 |
| Firmware target | `lran-welllink` | System PRD §9.1 |
| Repo path | `/firmware/welllink/` | System PRD §9.1 |

> **The bench-simulator address range was moved off `0x02` specifically to protect this
> reservation.** An earlier draft had `simnode` registering as node `0x02`, which would
> have meant a simulator left running collides with WellLink the day it is commissioned.
> Simulators now live at `0xF0`–`0xFE`.

---

## 4. Open questions before design

**These must be answered before this document can become a specification.**

| # | Question | Consequence |
|---|---|---|
| **D19** | **Mains or battery/solar?** | The largest fork. Determines whether the reserved RX duty-cycling design (Protocol Spec §17.1) is needed, whether battery telemetry is required in schema `0x20`, and effectively the whole hardware design |
| **W-1** | **What kind of level sensor?** Pressure transducer, ultrasonic, float/reed chain, submersible | Determines the analog front end, the power profile, and whether the node needs a stable excitation supply |
| **W-2** | **What does "rapid level change" mean numerically?** | WG-3 is not implementable as written. Needs a rate threshold and a window, and both are properties of the well, not of the firmware |
| **W-3** | **Is the well head enclosure conditioned, sheltered, or exposed?** | Feeds a WellLink equivalent of **D29**. GateLink's answer was "outdoors and instrumented"; this may differ |
| **W-4** | **Absolute level, or level relative to a datum?** | Determines whether calibration state must persist on the node and therefore whether it needs nonvolatile storage |
| **W-5** | **What is the failure consequence of a missed reading?** | Sets the poll interval, the event thresholds, and how hard the availability watchdog needs to work. A node whose data is nice-to-have and one that warns of a dry well are different designs |

> **W-5 is the question that should be answered first.** Poll interval, power budget,
> event thresholds and the duty-cycling decision all flow from what the readings are
> actually for, and answering it may well settle **D19** by implication.

---

## 5. Interfaces and protocols

**LoRa.** WellLink is a standard LRAN endpoint. Framing, addressing, authentication,
sequencing, fragmentation, media access and version tolerance are defined in
[`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) and require **no
WellLink-specific extension** beyond defining schema `0x20`.

**Schema `0x20`** is reserved and undefined. It must carry level and, if **D19** goes that
way, battery status. **A node with no application schema yet is still fully observable
through the generic node-health schema `0xF0`** — which means a WellLink can be brought up,
registered, polled and monitored *before* its payload is designed.

**MQTT.** Namespace reserved; topic grammar per Protocol Spec §16.

*(Sensor interface: to be developed — see W-1.)*

---

## 6. Hardware requirements

*(To be developed. Blocked on D19 and W-1.)*

What is already known:

- **R-W6a.** The LoRa radio SHALL be an SX1262 at 915 MHz, sharing the fleet's PHY
  parameters (**D1**) and the shared radio driver with its pin map injected by
  configuration.
- **R-W6b.** The node SHALL be reachable from the chosen bridge antenna position on its
  own bearing, **verified during the range test rather than after installation** (**M6**).

> **R-W6b is doing real work right now.** It is the reason the bridge's range test
> measures two bearings rather than one, on a node that does not exist and whose sensor is
> not chosen. Getting this wrong costs a re-run of the bridge install.

---

## 7. Firmware requirements

*(To be developed. Blocked on D19, W-1 and W-2.)*

What is already known:

- **R-W7a.** WellLink SHALL link the shared `/lib/lran-protocol/` library like every other
  node.
- **R-W7b.** WellLink SHALL emit generic node-health schema `0xF0` from first bring-up,
  before its application schema exists.
- **R-W7c.** If **D19** resolves to battery power, the RX duty-cycling and extended-preamble
  design in Protocol Spec §17.1 SHALL be evaluated — **and adopted only if the measured
  budget justifies it.**

> **Do not adopt the duty-cycling design by default.** Measured against GateLink's storage
> it bought single-digit percentages of the budget at real cost in complexity, latency and
> overnight data continuity, which is why it was dropped there. It is retained for this
> node because WellLink *might* lack that storage reserve — not because a battery-powered
> node automatically needs it. **The reserved section in the protocol spec is an option,
> not a plan.**

---

## 8. Test and verification requirements

*(To be developed.)*

Two are already known:

- ~~**V-W1.** Range and RSSI on the **well bearing** at ~500 ft~~ — **SATISFIED, and earlier than anyone noticed.** Walk **P3** of 2026-09-04 is this site: a Heltec pair, initiator indoors at the bridge's target location, **~100 m** through the NW wall plus the barn, **2.08 % PER over 192 probes** at the D33 ceiling (init −99.5 / resp −98.7 dBm). **It is the thinnest margin any node site showed and the only position in that walk with any `phy_crc_err`** — carry both into this node's design. If WellLink lands on a Wio rather than a Heltec, B1b's module delta applies: ~3 dB on `(TX − RX)`, ~9 dB round trip. The original wording asked for ~500 ft; the site is ~100 m and is obstruction-limited rather than distance-limited. Measured during the bridge
  range test (**M6**), before the bridge location is committed.
- **V-W2.** Registration, polling, availability and diagnostics working through schema
  `0xF0` alone, **before the application schema is defined.**

---

## 9. What the rest of the system already does for WellLink

A summary of the design debt already paid, so a future reader does not re-litigate
choices that look over-engineered against a one-node system:

| Capability | Where it lives | Why WellLink needed it |
|---|---|---|
| 8-bit node addressing | Protocol Spec §5.3 | More than two nodes |
| Per-node HKDF keys | Protocol Spec §9.1 | So compromising a well sensor does not grant gate command authority |
| Per-node context and sequence tables | Protocol Spec §10 | Independent nodes rebooting independently |
| Explicit payload schema IDs | Protocol Spec §7.1 | So WellLink's payload can be added without touching GateLink |
| Generic node-health schema `0xF0` | Protocol Spec §7.5 | So a node is observable before its payload exists |
| N/N−1 version tolerance | Protocol Spec §13.1 | So adding WellLink does not mean reflashing GateLink |
| CAD + randomized backoff | Protocol Spec §12.3 | Channel contention, which two nodes do not have |
| Reserved RX duty-cycling design | Protocol Spec §17.1 | **D19** |
| Per-node availability watchdog | Protocol Spec §16.5, Bridge PRD §3.4 | Per-node liveness, which LWT cannot provide |
| Per-node poll scheduling | Bridge PRD §3.1d | Independent intervals |
| One HA device per node | System PRD §6.1 | So a quiet WellLink does not mark the gate unavailable |
| `simnode` on `0xF0`–`0xFE` | Protocol Spec §5.3 | Validates all of the above **before WellLink exists** |
| Two-bearing antenna siting | Bridge PRD §4.3 | The well is on a different bearing from the gate |

---

## 10. Changelog

- **v0.6** — Citation refresh only. Protocol specification **v0.9 → v0.10**: `ver` stays at
  `2` and nothing on the wire changes. Still a placeholder. **Recorded because v0.10 hands
  this node its radio configuration rather than leaving it open.** **D1 closed 2026-09-10** —
  917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted with a 3.0 dBi antenna, under §15.249
  Envelope A — and **R-W6a's "sharing the fleet's PHY configuration" now names specific
  numbers.** Two consequences arrive with it. The `backoff_max_ms` question this document's
  v0.4 entry flagged is answered: the default is **1500**, above SF9's 1107 ms full-frame
  airtime. And the well site is the thinner of the two bearings — **2.08 % PER at ~100 m**,
  obstruction-limited through the barn, the only position in the 2026-09-04 walk with any
  `phy_crc_err` — so **SF9's ~13 dB of tail margin matters more here than at the gate**, and
  a WellLink design that wants to revisit SF is reopening a closed decision rather than
  making an open one.

- **v0.5** — Citation refresh only. Protocol specification **v0.8 → v0.9**: `ver` stays at
  `2` and nothing on the wire changes. Still a placeholder. **Recorded because a
  placeholder node inherits both of v0.9's consequences rather than deciding either.**
  First, the regulatory one binds WellLink the day it exists: spec **§18.2** puts the
  project under **§15.23 home-built**, so **no WellLink document, header, label or HA
  device entry may represent it as FCC certified**, and that is easier to honour before
  anything is written than to correct afterwards. Second, `BW` is now part of the same
  decision as the Part 15 rule section, so **D1 hands this node a channel *and* a
  bandwidth** as a given, on top of the SF constraint already recorded at v0.4.
- **v0.4** — Citation refresh only. Protocol specification **v0.7 → v0.8**, which closes **W9** (the full-size and fragmented `PING` bench runs both passed over RF on 2026-09-05) and changes **no frame layout, header field, authentication scope or schema length**; no vector regenerates. Still a placeholder. Recorded because the v0.8 finding is one a WellLink inherits rather than decides: §12.3's default `backoff_max_ms` of 500 covers a full-size frame at SF7 and at no SF above it, so whatever **D1** fixes reaches this node as a given.
- **v0.3** — Citation refresh only. Protocol specification **v0.6 → v0.7**, which captures **D34** (Protocol Spec W12: §9.4 steps 4–5 become `CommandGate` in `/lib/lran-protocol/`, dispatch stays in the application) and changes **no frame layout, header field, authentication scope or schema length**. Still a placeholder. Recorded because **W-* design questions inherit it**: any WellLink that accepts a command inherits `CommandGate` rather than writing a replay check.
- **v0.2** — Housekeeping revision; **still a placeholder, no design work done**.
  Binding protocol citation moves **v0.2 → v0.6**; §9's reserved-allocation table cites
  spec sections that all still exist and still say what the table claims, so **nothing
  was found to conflict**. **D32** now fixes RadioLib as this node's SX1262 driver too,
  ahead of its design. Cross-document links repaired for the `docs/` reorganization.
- **v0.1** — Initial release, **as a placeholder**. Extracted from `lran-prd-v0_8` §1.7,
  §7.6 and the WellLink-relevant parts of §3.1 and D19. **Added:** §3, the reserved
  allocations gathered into one table so they are visible as reservations rather than
  scattered as incidental mentions; §4, five open questions that must be answered before
  design, of which only D19 previously existed — W-2 in particular records that "push an
  event on rapid level change" is not implementable as written without a rate threshold
  and a window; §9, a summary of the design debt the rest of the system has already paid
  on this node's behalf, so that the multi-node machinery is not later mistaken for
  over-engineering against a one-node fleet. **No design work has been done.**
