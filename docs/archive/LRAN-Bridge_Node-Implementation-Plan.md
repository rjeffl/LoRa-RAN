# LRAN Bridge Node Implementation Plan

**Document:** `LRAN-Bridge_Node-Implementation-Plan`
**Version:** 0.1
**Node:** `LoRaBridge`, node ID `0x00`
**Firmware target:** `lran-bridge`
**Status:** Ready for build. No blocking measurements.
**Requirements source:** [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md) v0.1
**Binding protocol:** [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) v0.2
**Decision status:** [`LRAN-Decision-Register`](./LRAN-Decision-Register.md)
**Last updated:** 2026-08-18

> **This document is the basis for firmware development and validation, and is what is
> handed to Claude Code for this node.** Requirement identifiers (`R-*`, `BG-*`, `BS-*`,
> `V-B*`) refer to the Bridge PRD.

---

## Table of contents

1. [Overview](#1-overview)
2. [Bill of materials](#2-bill-of-materials)
3. [Hardware interconnect](#3-hardware-interconnect)
4. [Interfaces and protocols in detail](#4-interfaces-and-protocols-in-detail)
5. [Firmware architecture](#5-firmware-architecture)
6. [Implementation specifics](#6-implementation-specifics)
7. [Test and verification plan](#7-test-and-verification-plan)
8. [Milestones and acceptance criteria](#8-milestones-and-acceptance-criteria)
9. [Integration observations](#9-integration-observations)
10. [Changelog](#10-changelog)

---

## 1. Overview

### 1.1 What is being built

A Heltec WiFi LoRa 32 V3 running gateway firmware. **There is no hardware build** — the
board integrates the ESP32-S3, the SX1262, an antenna connector, an OLED and USB-C power.
The work is entirely firmware plus antenna selection and siting.

### 1.2 Why this node comes first in practice

Two properties make the bridge the natural place to start, independent of the node it
will eventually serve:

- **It is the range-test instrument.** Two Heltec boards characterize the PHY and resolve
  **D1** without waiting on GateLink's carrier board, and the result transfers unchanged
  (§9.1).
- **It can run the whole fleet in simulation.** With `simnode` and the dummy-publish path,
  discovery, entity mapping, availability and publication policy can all be developed and
  demonstrated with **no node hardware present** (**V-B11**). That keeps HA integration
  off the critical path of a workbench.

### 1.3 Non-obvious properties to preserve

1. **No gate knowledge anywhere.** The bridge decodes GateLink's schemas because they are
   registered in a table, not because any code path knows what a gate is. The moment
   `if (node == gatelink)` appears outside the registry, **BG-2** is broken.
2. **LoRa receive must survive a WiFi or broker outage.** A status frame arriving while
   the network is down must still be received and queued, even if publication is
   deferred.
3. **Never vary `seq` on retry** (**BS-3**). It looks like a fix for a stuck command and
   is actually a second relay pulse at the gate.

---

## 2. Bill of materials

| Qty | Item | Notes |
|----:|------|-------|
| 1 | **Heltec WiFi LoRa 32 V3** | ESP32-S3 + SX1262 + 0.96" OLED + antenna connector. ~$20 |
| 1 | **915 MHz antenna** | Selected after **M6**. Favour omnidirectional; see §3.2 |
| 1 | USB-C supply and cable | Mains, indoors |
| *1* | *Extension cable / remote antenna mount* | *Only if siting requires the antenna away from the board* |
| *1* | *Second Heltec V3* | **Strongly recommended for the range test.** Also the permanent bench peer and `simnode` host |

**No enclosure, no level shifting, no regulator, no carrier.** The bridge's BOM is the
board, an antenna and a power supply.

### 2.1 On buying the second board

The range test needs two radios, and buying a second Heltec rather than waiting for
GateLink's carrier decouples **D1** from the carrier bring-up entirely. Afterwards it
becomes the permanent `simnode` host, which is what **V-B2** needs to exercise multi-node
behaviour before WellLink exists. **For roughly $20 it removes a dependency from the
critical path and provides the multi-node test rig.**

---

## 3. Hardware interconnect

### 3.1 On-board — nothing to wire

| Function | Connection |
|---|---|
| SX1262 | Internal SPI, fixed pins, **1.8 V TCXO**, DIO2 RF switch |
| OLED | Internal I²C, **Vext-controlled** |
| Power | USB-C |
| Antenna | On-board U.FL / SMA per board revision |

**The radio pin map, TCXO voltage and RF-switch mode are supplied by configuration**
(**R-4.1b**), not hardcoded — the same driver serves GateLink's carrier, which has none
of these values in common with this board.

> **Two Heltec V3 quirks worth knowing before they cost an afternoon.** The TCXO runs at
> **1.8 V**, not the 3.3 V some libraries default to, and a wrong value shows up as a
> radio that will not calibrate rather than as an obvious error. The OLED sits behind the
> **Vext** power control, so it must be enabled before initialisation — a display that is
> dark on boot is usually Vext, not the driver.

### 3.2 Antenna and siting

The only physical decision in this node, and **the one with the longest lead time on
being wrong.**

- GateLink and WellLink are at similar distances (~500 ft) on **different bearings**.
- **Favour an omnidirectional antenna in a central, elevated position.** A pattern
  optimized toward the gate buys margin on one link and may put the other in a null.
- **Range-test both bearings before committing to a location** (**M6**, **V-B1**).
- Record the chosen position, antenna type and the measured RSSI/SNR on both bearings in
  `/docs/bridge/engineering-log.md`, so a later "why is the well link marginal?" has a
  baseline to compare against.

Practical siting constraints: within WiFi range of the LAN, on mains, and preferably
where the USB-C cable is not the tallest thing in the room. **Elevation usually matters
more than the antenna choice** at these distances.

---

## 4. Interfaces and protocols in detail

### 4.1 LoRa

Everything on the wire is defined in
[`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md). Bridge implementation
obligations:

| Item | Value |
|---|---|
| Node ID | `0x00` |
| Driver | RadioLib, SX1262 |
| Schemas decoded | `0x10`, `0x11`, `0x12`, `0xF0`, `0xFE`; unknown → `ERROR(UNKNOWN_SCHEMA)` and a counter |
| Version tolerance | Accept **N and N−1**; downgrade per node on the version last heard |
| Poll discipline | **Serialized across the fleet** — one outstanding poll at a time |
| Keys | `master_key` only; per-node keys derived by HKDF at registry load |

### 4.2 Per-node registry

The central data structure. One entry per node, loaded from build configuration at boot:

| Field | Source | Notes |
|---|---|---|
| `node_id` | config | `0x01` GateLink, `0x02` WellLink, `0xF0`–`0xFE` bench |
| `node_type` | config | Selects the decoder and the discovery template set |
| `key` | **derived** at load from `master_key` | Never stored as a separate provisioned secret |
| `ctx_id` | learned | The node's boot context; `0` = unknown |
| `cmd_seq` | maintained | Reset to `1` on learning a new `ctx_id` |
| `last_seen` | maintained | Drives availability |
| `missed_polls` | maintained | Threshold default **3** |
| `proto_ver` | learned | Published as a diagnostic |
| `rssi`, `snr` | learned | Published as diagnostics |
| `poll_interval_s` | **runtime**, from HA | Per node |

**Adding a node is a registry entry plus a decoder plus a discovery template.** If it
requires touching the scheduler, the availability watchdog or the MQTT layer, the
abstraction has leaked and **BG-2** is at risk.

### 4.3 MQTT

Topic grammar and retention rules are in Protocol Spec §16.

**Library selection (D5).** Define a thin **`MqttTransport` interface and implement it
first against PubSubClient.** Bridge traffic is modest — a poll per node every 1–5
minutes plus occasional commands and events — so throughput and QoS 2 are irrelevant.
What matters is reliable reconnect, LWT, and publishing discovery-config JSON.

| Option | License | Assessment |
|---|---|---|
| **PubSubClient** | MIT | Tiny, synchronous, extremely stable, ubiquitous. **Gotcha: default max packet 256 bytes — Discovery configs exceed this and fail confusingly.** Set `MQTT_MAX_PACKET_SIZE` ≥ **1024** |
| **espMqttClient** | MIT | Actively maintained, sync and async variants, large payloads, QoS 0/1/2. **Designated fallback** |
| AsyncMqttClient | MIT | Effectively unmaintained. Avoid for new work |
| esp-mqtt (IDF native) | Apache-2.0 | Most robust reconnect and TLS, but pulls the design toward IDF. Reserve for a later migration |

> The `MQTT_MAX_PACKET_SIZE` default is the single most likely early time-sink on this
> node: discovery configs simply do not appear, with no error that points at the cause.
> **Set it in the build flags on day one.**

### 4.4 Home Assistant discovery

- Published on boot **and on every broker reconnect** (**R-3.3b**). The reconnect path is
  the one that gets skipped in development and the one that runs unattended.
- One device per registered node, plus the bridge.
- `unique_id` prefixed per node (`lran_gatelink_*`) — stable and non-colliding as the
  fleet grows.
- Every node entity references **that node's** availability topic, not the bridge LWT
  (**R-3.3d**).
- Discovery templates live per node type alongside the decoder, so adding a node type is
  one directory rather than edits scattered through the MQTT layer.
- Example payloads are committed to `/ha/` for reference and for bench testing without a
  running HA.

---

## 5. Firmware architecture

### 5.1 Framework and libraries

| Concern | Choice | License |
|---|---|---|
| Build | **PlatformIO**, ESP32-S3 target | — |
| LoRa | **RadioLib** (SX1262) | MIT |
| Protocol | **`/lib/lran-protocol/`** — the same library every node links | — |
| WiFi | Arduino-ESP32 | LGPL-2.1-or-later |
| MQTT | **`MqttTransport` → PubSubClient** (**D5**) | MIT |
| JSON | ArduinoJson | MIT |
| Display | U8g2 | BSD-2-Clause |
| HMAC / HKDF | mbedTLS via ESP-IDF | Apache-2.0 |
| OTA | ArduinoOTA or `esp_https_ota` | LGPL / Apache-2.0 |
| VE.Direct HEX register model | **`/lib/vedirect/`**, shared with GateLink | MIT |

### 5.2 Task structure

Less delicate than GateLink's — there is no real-time I/O to protect — but two ordering
properties matter:

| Task | Trigger | Priority | Owns |
|---|---|---|---|
| `lora_task` | RX interrupt / TX queue | **Highest** | RadioLib, frame serialize/deserialize, MAC verify, reassembly |
| `sched_task` | 1 s tick | High | Per-node poll scheduler, retry/backoff, availability watchdog |
| `mqtt_task` | queue / 100 ms tick | Normal | Broker connection, publish queue, subscription dispatch, discovery |
| `app_task` | queue | Normal | Decode per schema, publication policy, event dedup, HEX proxy authorization |
| `ota_task` | on request | Low | Deferred until LoRa idle |
| `ui_task` | 500 ms tick | Low | OLED status page |
| `log_task` | queue | Lowest | Leveled serial log, raw frame log |

**Rules:**

- **`lora_task` is highest priority and never blocks on the network.** A node's status
  frame arriving during a WiFi outage must still be received and queued (**§1.3**). This
  is the one place where a naive "publish inline on receive" implementation quietly loses
  data.
- **Publication is queued, not inline.** A blocking publish on a reconnecting broker must
  not stall frame reception or the poll schedule.
- `ota_task` defers until `lora_task` reports idle (**R-5.3d**).
- Watchdog fed from `sched_task`.

### 5.3 Module map

```
/firmware/bridge/
  src/
    main.cpp            task creation, WiFi/MQTT init, registry load
    registry.cpp        per-node table, key derivation, availability   [sched_task]
    scheduler.cpp       per-node poll scheduling, retry/backoff        [sched_task]
    lora_link.cpp       RadioLib, frame in/out, MAC, reassembly        [lora_task]
    mqtt_transport.cpp  MqttTransport iface + PubSubClient impl        [mqtt_task]
    discovery.cpp       Discovery config generation and publication    [mqtt_task]
    publish.cpp         publication policy: on-change, staleness       [app_task]
    hex_proxy.cpp       HEX wrap/unwrap, arm state, audit trail        [app_task]
    decode/
      gatelink.cpp      schema 0x10 / 0x11 / 0x12 decoders
      health.cpp        schema 0xF0 decoder, all node types
      welllink.cpp      schema 0x20 — stub
    ui.cpp              OLED status page                               [ui_task]
    debug.cpp           loopback, dummy publish, simulators
  lib deps ->
    /lib/lran-protocol/
    /lib/vedirect/      HEX register model (bridge side)
  CLAUDE.md             subproject context for Claude Code
```

`/docs/bridge/engineering-log.md` carries the dated running record, including the range
test results and the antenna siting decision.

---

## 6. Implementation specifics

### 6.1 Poll scheduler

- One timer per node, interval from that node's `poll_interval_s` (HA `number`, default
  **60**).
- **Polls are serialized fleet-wide** (**R-3.1d**): when a poll is due and another is
  outstanding, it queues rather than transmitting. This removes the largest predictable
  collision source for free and costs nothing at a 1–5 minute cadence.
- An unanswered poll increments `missed_polls`; a valid frame from that node resets it.
- Node-initiated pushes are **not** polls and do not reset the schedule, but they do reset
  `missed_polls` and `last_seen`.

### 6.2 Command path and retry

```
MQTT command topic
  -> validate against the target node's capability set
  -> registry: node key, ctx_id, next cmd_seq
  -> build authenticated frame, transmit
  -> await COMMAND_ACK within command_ack_timeout_ms (default 3000)
       ACK           -> publish result, done
       no ACK        -> retry with backoff, SAME seq, up to cmd_retries (default 3)
       REJECTED_CTX  -> adopt the ACK's ctx_id, reset cmd_seq to 1, retry ONCE
       2nd REJECTED_CTX -> stop; publish a diagnostic fault
  -> exhausted      -> publish failure; do NOT keep trying
```

**Two rules that are easy to get wrong and expensive at the gate:**

- **The retry reuses the same `seq`** (**BS-3**). This is what lets the node's
  `(ctx_id, seq)` deduplication recognise the retry and return the cached ACK instead of
  pulsing a relay a second time. Incrementing `seq` on retry looks like a fix for a stuck
  command and is actually a second gate command.
- **The context resync retries exactly once** before giving up. A resync loop on a shared
  channel is a transmit storm affecting every other node, not just this one.

### 6.3 Publication policy

Implemented in `publish.cpp`, applied uniformly across node types:

| Rule | Implementation |
|---|---|
| **Publish on change** | Per-field comparison against the last published value, with an optional rounding step. Applied to per-cell battery voltages and other jittery telemetry |
| **Staleness → unavailable** | On a node's staleness flag or an age exceeding threshold, publish `unavailable` to the affected entities. **Never republish the cached value** |
| **Sentinels are not numbers** | `INT16_MIN` / `UINT16_MAX` map to `unavailable`, not to a reading |
| **Synthetic stays marked** | A debug-synthetic status reason propagates into the published payload |
| **Events never retained** | Retain flag clear, QoS 1, deduplicated on `(src, ctx_id, event_id)` |
| **Periodic heartbeat** | An unchanged value is republished at most every `republish_interval_s` (default 900), so an HA restart repopulates rather than showing blanks until the next change |

> The heartbeat is the counterweight to publish-on-change: without it, a value that has
> not changed in six hours is missing from a freshly restarted HA. Retained topics cover
> most of this, but the interval makes it robust to a broker without persistence.

### 6.4 HEX proxy and write arming

```
lran/<node>/vedirect/hex/request  (HA -> bridge)
  -> parse the command nibble
  -> if write-class (Set 0x8 / Restart 0x6):
       is write_enable armed AND not expired?   no -> reject, publish audit, stop
  -> wrap in HEX_REQ, MAC if write-class, transmit
  -> HEX_RSP -> publish response + status
  -> publish audit entry (retained): request, authorization outcome, response
```

- `write_enable` is a retained HA `switch`, **default off**, auto-expiring after
  `mppt_write_arm_timeout_s` (default **300**). On expiry the bridge publishes the switch
  back to off, so HA reflects reality rather than showing armed indefinitely.
- The audit topic is **retained**, so the last write attempt survives an HA restart. This
  is deliberate — it is the record you want when a charge parameter turns out to be wrong
  and nobody remembers changing it.
- On boot the bridge issues read-class HEX requests for the charge parameters and
  publishes them as diagnostic sensors, **so a wrong charge profile is visible rather than
  latent** (**R-3.5d**).

### 6.5 OTA

- Dual-partition A/B with rollback (**R-5.3c**). Configure the partition table
  accordingly — this is a build-time decision that cannot be retrofitted to a deployed
  bridge without a USB flash.
- Authenticated endpoint, password from untracked config.
- **Deferred while `lora_task` reports a transaction outstanding**; a poll or command in
  flight completes first.
- Version published to the bridge's version topic and exposed as a diagnostic sensor.
- **Test the rollback with a deliberately bad image** (**V-B9**). An untested rollback is
  not a rollback, and the bridge is the one node where losing this costs the whole
  property's telemetry.

### 6.6 Debug tooling

| Tool | Implementation note |
|---|---|
| **Dummy publish** | Synthetic decoded payloads pushed through the real publication policy and discovery path, with **no node and no radio**. This is what unblocks HA integration on the bench |
| **Node simulators** | Bridge-side generators per node type. Complements `simnode`, which tests the RF path; these test the MQTT path |
| **Packet loopback** | RF echo and internal loopback with no radio |
| **Raw frame log** | Every frame in and out with source, type, schema, RSSI, SNR and discard reason. Published to a debug topic and to serial |
| **MQTT as harness** | `mosquitto_sub -t 'lran/#'` to watch every decoded payload live; `mosquitto_pub` to inject commands, decoupled from HA and from the RF link |

---

## 7. Test and verification plan

### 7.1 Coverage matrix

| Requirement | Verified by | Milestone |
|---|---|---|
| V-B1 range, both bearings | Two Heltec boards, field walk | B1 |
| V-B2 multi-node registry | `simnode` ×2 on the bench | B3 |
| V-B3 availability watchdog | Power down a simnode mid-poll | B3 |
| V-B4 discovery incl. reconnect | Restart the broker with entities live | B4 |
| V-B5 command retry / dedup | Suppress an ACK deliberately | B3 |
| V-B6 HEX three gates | Each gate tested independently | B5 |
| V-B7 publication policy | Injected jitter, staleness flags, sentinels | B4 |
| V-B8 events fire once | HA restart + discovery refresh with an event in history | B4 |
| V-B9 OTA + rollback | Deliberately bad image | B2 |
| V-B10 version tolerance | simnode announcing N−1, then N−2 | B3 |
| V-B11 fleet with no node hardware | Dummy publish + simulators | B4 |

### 7.2 Bench harness

The bridge is the easiest node in the fleet to test, and that should be exploited:
`mosquitto_sub -t 'lran/#'` shows every decoded payload live, `mosquitto_pub` injects
commands, and `simnode` supplies the RF side. **Every verification except V-B1 is
reachable at a desk.**

---

## 8. Milestones and acceptance criteria

| # | Milestone | Depends on | Acceptance criteria |
|---|---|---|---|
| **B1** | **RF link characterization** | Two Heltec boards | RSSI and SNR measured at ~500 ft **on both the gate bearing and the well bearing**, across candidate SF/BW/CR settings. **D1 resolved** with a stated link margin. Bridge antenna type and position chosen and recorded. Airtime table regenerated (**M19**). FCC operating mode question (**W5**) settled before a TX power is fixed |
| **B2** | **Board bring-up and OTA** | Board in hand | WiFi connects and reconnects; MQTT connects with LWT registered; A/B partitioning configured; OTA succeeds over WiFi; **a deliberately bad image rolls back**. Version published. OLED shows a status page |
| **B3** | **Protocol and registry, with simnode** | B2, `/lib/lran-protocol/` | Frames round-trip against the committed test vectors. **Two simnodes registered simultaneously**, each with its own derived key, context and sequence space. Context resync retries once and then faults. **A suppressed ACK produces a retry with the same `seq`, and the simnode reports a deduplicated hit rather than a second execution.** Availability marks offline after 3 missed polls and online on the next frame. Version tolerance accepts N−1 and rejects N−2 with a distinct reason |
| **B4** | **MQTT, discovery and publication policy — no node hardware** | B3 | Discovery publishes one device per node, correct availability references, **and republishes on broker restart**. All §6.3 policy rules demonstrated: jitter suppressed, staleness marks unavailable, sentinels not published as numbers, synthetic marked, heartbeat republish works. **Events publish non-retained and do not replay on HA restart or discovery refresh.** The whole fleet is demonstrable with dummy publish and simulators only |
| **B5** | **HEX proxy** | B4, a real MPPT reachable via GateLink or a simulator | Read passes. Write rejected while disarmed, accepted while armed, **and the arm auto-expires with the switch published back to off**. Every attempt appears in the retained audit trail. Charge-parameter readback published as diagnostic sensors on boot |
| **B6** | **GateLink integration** | B5, GateLink M6 | End-to-end with the real node: command round-trip, status decode, event delivery, per-node availability, diagnostics populated |
| **B7** | **Soak** | B6 | Continuous operation across broker restarts, WiFi outages and a node power cycle, with no lost frames on reconnect and no stuck availability state |

**Critical path:** B2 → B3 → B4 → B5 → B6 → B7. **B1 is independent and should be done
first in wall-clock terms**, because it gates GateLink's PHY configuration as well as this
node's antenna siting.

---

## 9. Integration observations

*Recorded as they are established. Sparse at this stage — this node has no field history
yet.*

### 9.1 The range test is deliberately host-independent

Characterizing the link with **two Heltec V3 boards** rather than waiting for GateLink's
carrier is a deliberate decoupling:

- It removes **D1** from the carrier's critical path entirely. Carrier bring-up can fail,
  be re-spun, or wait on a part, and the PHY answer is already in hand.
- The result **transfers unchanged.** SF, BW, CR, TX power and the resulting link margin
  are properties of the radio, the antennas and the path — not of the host MCU.
- It produces a **known-good reference pair.** When GateLink's carrier is later brought
  up, a link failure can be bisected against a radio pair already proven on that path,
  which is the difference between "the carrier is wrong" and "something is wrong."

The one thing it does **not** transfer is antenna placement at the node end — GateLink's
antenna will be on an SMA bulkhead through an enclosure wall, not on a bench board. Treat
the range test as establishing the PHY parameters and the bridge siting, and re-measure
RSSI once GateLink is installed.

### 9.2 Two-bearing siting is the long-lead decision

GateLink and WellLink are at similar distances on different bearings. **Both bearings must
be measured before the bridge location is committed** (**M6**). Siting for the gate and
discovering later that the well sits in a null costs a re-run of the install rather than a
firmware change — and WellLink does not exist yet to complain about it, which is exactly
why the measurement has to happen now rather than when the second node is built.

Record in `/docs/bridge/engineering-log.md`: chosen position, antenna type, and measured
RSSI/SNR on both bearings at the selected PHY settings. That baseline is what a future
"why is this link marginal?" gets compared against.

### 9.3 Carried forward from earlier revisions

- **Bridge MQTT load is uniformly light.** An earlier design carried raw protocol-bus
  frame streaming from the gate node, which was the one feature likely to stress a
  blocking publish. That feature is gone, so PubSubClient is a comfortable fit and the
  synchronous-client concern that drove the fallback analysis no longer applies. **The
  `MqttTransport` abstraction is retained anyway** — it costs little and keeps the
  espMqttClient fallback open.
- **Expected channel occupancy is negligible** — short status frames on a 1–5 minute
  cadence, per node. The media-access design (CAD, backoff, serialized polls) is
  provisioned for a fleet that does not yet exist rather than for current load.

---

## 10. Changelog

- **v0.1** — Initial release. Extracted from `lran-prd-v0_8` §6.1, §6.2, §6.6.3–6.6.5 and
  the bridge-relevant parts of §3.1, §6.4 and §14. All requirements moved to
  [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md) and referenced by identifier; all
  frame and topic detail replaced by references to
  [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) v0.2. **Added:** §4.2,
  the per-node registry as an explicit data structure with the test for whether the
  abstraction has leaked — v0.8 described per-node state across several sections but never
  as one table; §5.2, task structure with the rule that `lora_task` never blocks on the
  network, which v0.8 implied but did not state; §5.3, module map; §6.2, the command retry
  state machine written out, including the same-`seq` rule whose consequence is a second
  relay pulse at the gate; §6.3, publication policy as implementable rules including a
  heartbeat republish interval that v0.8 did not have; §8, seven milestones with
  acceptance criteria; §9.1, the reasoning behind keeping the range test host-independent.
  **No design change.**
