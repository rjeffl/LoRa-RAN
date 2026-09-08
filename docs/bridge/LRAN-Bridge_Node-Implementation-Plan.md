# LRAN Bridge Node Implementation Plan

**Document:** `LRAN-Bridge_Node-Implementation-Plan`
**Version:** 0.9
**Node:** `LoRaBridge`, node ID `0x00`
**Firmware targets:** `lran-bridge`, `lran-simnode` (§10), `lran-rangetest` (§11.2)
**Status:** Ready for build. No blocking measurements.
**Requirements source:** [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md) v0.1
**Binding protocol:** [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) **v0.9**
**Shared codec:** [`LRAN-Protocol-Library-Implementation-Plan`](../shared/LRAN-Protocol-Library-Implementation-Plan.md) v0.1 — **built first, gates this node**
**Decision status:** [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md)
**Last updated:** 2026-09-08

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
10. [Simnode firmware and the bench fleet](#10-simnode-firmware-and-the-bench-fleet)
11. [Development environment and workflow](#11-development-environment-and-workflow)
12. [Changelog](#12-changelog)

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
- **It can run the whole fleet in simulation.** With `simnode` (§10) and the dummy-publish
  path, discovery, entity mapping, availability and publication policy can all be
  developed and demonstrated with **no node hardware present** (**V-B11**). That keeps HA
  integration off the critical path of a workbench.

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
| 1 | **Heltec WiFi LoRa 32 V3** | Bridge proper. ESP32-S3 + SX1262 + 0.96" OLED + antenna connector. ~$20 |
| 1 | **Heltec WiFi LoRa 32 V3** | **Permanent bench peer.** Range-test partner, then `simnode` host for the life of the system (§10.7) |
| 1 | **Seeed XIAO ESP32S3 + Wio-SX1262** | **Target-radio simnode.** Same LoRa module as GateLink's carrier. B1b range peer, then permanent `ROLE_GATELINK` at `0xF1` (§2.3) |
| *1* | *Heltec WiFi LoRa 32 V3* | *Third Heltec — cold spare only. No longer needed for contention (§2.1)* |
| 2+ | **915 MHz antennas** | One per board. Bridge antenna selected after **M6**; favour omnidirectional (§3.2). Bench boards need one each regardless of type |
| 2+ | USB-C supplies and cables | Mains, indoors |
| *1* | *Extension cable / remote antenna mount* | *Only if siting requires the antenna away from the board* |
| *1* | *SMA attenuator, 20–30 dB* | *Bench hygiene — see the warning in §2.2* |

**No enclosure, no level shifting, no regulator, no carrier.** The bridge's BOM is the
board, an antenna and a power supply.

### 2.1 How many boards — two is workable, three is right

**Two boards develop everything. A third converts one class of test from "logically
demonstrated" to "actually observed," and doubles as the spare for the system's single
point of failure.**

**What two boards get you.** A single simnode board hosts **up to four logical node
identities simultaneously** (§10.3). Each has its own `node_id`, its own HKDF-derived
key, its own `ctx_id` and its own sequence space; the board transmits as whichever
identity is due. **The bridge cannot tell the difference**, because every piece of
per-node state in §4.2 is keyed on `node_id` and none of it is keyed on the radio the
frame arrived from. That is not a shortcut — it is a direct consequence of the registry
abstraction, and if it *were* distinguishable, **BG-2** would already be broken.

So on two boards: **V-B2** (multi-node registry), **V-B3** (availability watchdog, by
stopping one logical identity), **V-B5**, **V-B10**, the whole §10.5 fault catalogue, and
every milestone from **B2** to **B5** are reachable.

**What two boards cannot do.** One radio serializes its own transmissions by
construction. It therefore **cannot generate a genuine collision**, which means:

| Untestable on one simnode board | Why it matters |
|---|---|
| CAD detecting a real in-progress transmission | Protocol Spec §12.3 media access is provisioned for a fleet that does not exist yet. Its first real test would otherwise be the day WellLink is commissioned |
| Randomized backoff actually de-conflicting two senders | The backoff distribution can be unit-tested; the *behaviour under contention* cannot |
| Capture effect / near-far behaviour | Two nodes at different distances is the real deployment |
| Bridge RX during a foreign transmission | The `lora_task` never-blocks rule (§1.3, §5.2) under actual RF load |

**The XIAO ESP32S3 + Wio-SX1262 supplies the second transmitter.** Contention testing
needs two simultaneous transmitters, not two Heltecs specifically. With two Heltecs and
the XIAO, all four rows above are reachable. See §2.3.

**The remaining case for a third Heltec is the cold spare** — the bridge is the single
node whose failure takes the entire property's telemetry with it, and a board already
flashed and known-working is a fifteen-minute recovery rather than a shipping wait. That
is a real but separable argument; it is no longer a test-coverage requirement. See §10.7
for why the bench peers stay in service permanently rather than being reclaimed.

### 2.3 The XIAO + Wio-SX1262 as a target-radio simnode

GateLink's carrier will host a **Seeed Wio-SX1262** module. The XIAO ESP32S3 + Wio-SX1262
kit puts that exact module on a dev board, and that changes what the bench fleet can
prove.

**What it validates that a Heltec cannot:**

| Item | Why it transfers |
|---|---|
| RadioLib configuration block for the target radio | Same module: TCXO supplied via DIO3, `setDio2AsRfSwitch`, RF-switch handling, PA ramp |
| **Actual radiated power and sensitivity of the Wio-SX1262** | Link margin is a property of the specific module, not of "an SX1262" |
| TX / RX / sleep current on the target radio | Feeds the GateLink power budget directly |
| Driver portability | XIAO is ESP32-S3, as is StamPLC — the code ports without an MCU-family change |

> **This corrects a claim in §9.1.** That section says the range result transfers
> unchanged because SF, BW, CR and margin are properties of the radio, the antennas and
> the path. The parameter *selection* does transfer. The **margin does not**, because it
> depends on the TX power and RX sensitivity of the specific module. A range test run
> Heltec-to-Heltec characterizes a link that will never be deployed. Run the confirming
> pass against the Wio-SX1262 — see **B1a/B1b** in §8.

**What it still does not validate:** the StamPLC carrier's SPI routing and bus speed,
contention with StamPLC peripherals on a shared bus, and — the one to watch — the
hand-built 3.3 V regulator on the perfboard carrier under TX current transients
(**D26**, **D27**). Roughly 118 mA steps at +22 dBm through a perfboard regulator is
precisely the thing that works on a bench supply and browns out on the carrier. **The
XIAO validates the module; only the carrier validates the carrier.**

#### 2.3.1 Two hardware findings to confirm on arrival

> ### BOTH FINDINGS CLOSED 2026-09-05, on the assembled hardware
>
> Recorded here because the two closed in opposite directions and the second one changes
> what the XIAO buys. Full detail in
> `docs/rangetest/LRAN-Range-Test-Firmware-Pass2-Tasks.md` §2 and the range-test
> engineering log for that date.
>
> **Finding 1 — CLOSED POSITIVE. The RF switch line is required.** Both Wio-SX1262
> products' board-support definitions set a discrete RXEN **and** `DIO2_AS_RF_SWITCH`.
> This confirms `gatelink-expansion-board.md` §7.3 as written and vindicates rev 0.3's
> decision to treat `RF_SW` as required and route it. `range-test` now drives it —
> `setRfSwitchPins(rf_sw, RADIOLIB_NC)`, parameter order checked against the pinned
> RadioLib 7.7.1. **Sub-question (b), the sleep-current cost of holding the line, is
> still open** and still belongs to B1b.
>
> **Finding 2 — CLOSED NEGATIVE. The kit is not the carrier's module.** Seeed sells two
> Wio-SX1262 products that are **not pin-compatible** outside the three SPI nets:
>
> | | Kit, p-5982 (B2B) | Header board, p-6379 (2.54 mm) |
> |---|---|---|
> | NSS / RST / BUSY / DIO1 / RF_SW | 41 / 42 / 40 / 39 / 38 | 5 / 3 / 4 / 2 / 6 |
>
> *The header-board column is shown here only to make the difference visible. It is not a
> build reference and this document does not own it —* `gatelink-expansion-board.md` §6.1
> *does.*
>
> **The board that arrived is the Kit.** §2.3's premise is therefore narrowed, exactly as
> the warning below §10.8.1 anticipated: the XIAO validates the **module** — SX1262
> silicon, RF performance, RadioLib on a second board, and the injected-config seam — but
> **not the carrier's net list**. *XIAO validates the module; only the carrier validates
> the carrier.*
>
> The carrier's own pad column did gain an independent corroboration (the header-board map
> above, from meshtastic/firmware issue #8409) that matches
> `gatelink-expansion-board.md` §6 value for value. **Two agreeing derivations are not a
> continuity check**, and the Kit cannot supply one because it does not use those pads.
> §10's ring-out item stays open.
>
> Identification was by **interconnect, not part number** — the stack is zip-tied and the
> underside is unreachable. B1b's request for "the exact module part number" is answered
> by variant, which is what it actually needed.


**1. The Wio-SX1262 appears to require a host-driven RF switch line.** The module
datasheet brings out an `RF_SW` pin described as enabling receiver mode on logic high,
while also stating that TX/RX switching is determined by DIO2. Published Meshtastic
configurations for the XIAO ESP32-S3 pairing set **both** `DIO2_AS_RF_SWITCH` **and** an
`RXEN` GPIO, with TXEN unconnected, and community measurements report roughly 55 µA of
additional draw while that pin is held high.

This contradicts the earlier assumption that the Wio-SX1262 avoids TXEN/RXEN-style
control. Seeed has not published a schematic for the module itself — only a wiring
diagram for the XIAO carrier — so **this cannot be settled from documentation and must be
measured on the board.** That is an argument for the evaluation purchase independent of
its simnode role.

> **Status as of GateLink expansion board rev 0.3.** The carrier design has resolved the
> budget half of this question by treating `RF_SW` as required: it is allocated to
> **StamPLC Bus 15 (G40) → Wio pad D5**, and the board is routed. The GPIO cost is
> therefore already absorbed and is no longer a reason to hope the line is unnecessary.
> What B1b still has to settle is narrower: **(a)** whether `RF_SW` is functionally
> required or merely permitted — a `setRfSwitchPins()` build versus a `DIO2` build, both
> testable on the XIAO without rewiring — and **(b)** the sleep-current cost of holding it
> high, which is a GateLink power-budget input, not a bridge one. Test both configurations
> on the XIAO and record which one the module actually needs.

**2. Wio-SX1262 variants are not pin-compatible.** The module shipped with the nRF52840
kit is documented as having a different pin configuration from the one shipped with the
XIAO ESP32-S3 kit, and the "for XIAO" header board is a different product from the
"with XIAO ESP32S3" kit.

> **This is now the sharper of the two findings, not the softer one.** The module destined
> for the GateLink carrier is **already in hand** — a *Wio-SX1262 for XIAO* header board
> with 2.54 mm rows fitted — while the simnode's module arrives inside the *XIAO ESP32S3
> kit*. Those are two purchases of two products. If their pad assignments differ, the
> consequence is not a carrier re-spin (the carrier is built around the board on hand);
> it is worse in a quieter way: **§10.8.1's premise fails and the XIAO stops being a
> validation of GateLink's radio configuration** while still appearing to work.
>
> **On arrival, before flashing anything:** ring out D1–D5 and D8–D10 on both modules and
> compare, or at minimum compare the silkscreened part numbers. Record both part numbers
> in `/docs/simnode/engineering-log.md`. A five-minute continuity check here protects
> every conclusion B1b produces.

> **Naming correction.** Earlier revisions referred to this module as `win-sx1262`. The
> correct designation is **Wio-SX1262** (Seeed Studio), corrected document-wide in v0.3.

### 2.2 Bench RF hygiene — two ways to damage a board

**Never transmit without an antenna attached.** An unterminated SX1262 PA reflects its
own output power back into the final stage. On a board that has been keyed up bare, the
failure is usually not immediate or total — it presents later as degraded TX power and a
link that is inexplicably worse than the range test predicted. Attach antennas before
first flash, not before first test.

**Attenuate, or separate, for bench work.** Two boards at +22 dBm sitting a foot apart on
the same desk put roughly −20 dBm into a receiver designed to work at −120 dBm. The
result is front-end saturation: RSSI figures that are meaningless, packet errors that
look like a protocol bug, and — at sustained power — a real risk to the LNA. For bench
work either **drop TX power to the minimum** the driver allows (this is a configuration
value already, per **R-4.1b**) or fit a 20–30 dB SMA attenuator. **Restore full power
before the range test** and record the setting in the log, because a range test run at
bench power is a range test that will have to be repeated.

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
of these values in common with this board. **The bridge's own values are the
`LRAN_PROFILE_HELTEC` entry in §10.8.1**, which is the single place all three targets'
pin maps are held; do not restate them elsewhere.

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
[`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md). Bridge implementation
obligations:

| Item | Value |
|---|---|
| Node ID | `0x00` |
| Driver | RadioLib, SX1262 |
| Schemas decoded | `0x10`, `0x11`, `0x12`, `0xF0`, `0xFE`; unknown → `ERROR(UNKNOWN_SCHEMA)` and a counter |
| Header extensions | None implemented. `hdr_flags` bit 7 set → discard + `ERROR(UNKNOWN_HDR_EXT)` (Protocol Spec §5.8, §14 stage 5a) |
| Version tolerance | Accept **N and N−1**; downgrade per node on the version last heard |
| Poll discipline | **Serialized across the fleet** — one outstanding poll at a time |
| Keys | `master_key` only; per-node keys derived by HKDF at registry load |

### 4.2 Per-node registry

The central data structure. One entry per node, loaded from build configuration at boot:

| Field | Source | Notes |
|---|---|---|
| `node_id` | config | `0x01` GateLink, `0x02` WellLink, `0xF0`–`0xF3` bench simnodes (Protocol Spec §5.3) |
| `node_type` | config | Selects the decoder and the discovery template set |
| `key` | **derived** at load from `master_key` | Never stored as a separate provisioned secret |
| `ctx_id` | learned | The node's boot context; `0` = unknown |
| `cmd_seq` | maintained | Reset to `1` on learning a new `ctx_id` |
| `last_seen` | maintained | Drives availability |
| `missed_polls` | maintained | Threshold default **3** |
| `proto_ver` | learned | Published as a diagnostic |
| `rssi`, `snr` | learned | Published as diagnostics |
| `poll_interval_s` | **runtime**, from HA | Per node |

| `is_bench` | derived | True for `0xF0`–`0xFE`. Gates publication per Protocol Spec §16.6 |

**Adding a node is a registry entry plus a decoder plus a discovery template.** If it
requires touching the scheduler, the availability watchdog or the MQTT layer, the
abstraction has leaked and **BG-2** is at risk.

**The four bench IDs are ordinary registry entries.** `0xF0`–`0xF3` are provisioned,
keyed and scheduled exactly like `0x01`, with no special case anywhere except the
publication gate below. This is deliberate and is itself a test: if a bench node needs
different handling in the scheduler or the watchdog, the registry abstraction is not
doing its job, and GateLink will find the same seam later.

### 4.2a Bench-node publication gate (`simnode_diag_enable`)

Protocol Spec §16.6 governs how bench nodes appear on a production bridge. The bridge
implements it as follows:

| Item | Implementation |
|---|---|
| Parameter | `simnode_diag_enable`, bool, **default `false`**, in `/lib/lran-config/` as a bridge parameter |
| Settable from | `lran/bridge/config/set`, with the same per-entry ACK and `persist_status` semantics as any node parameter — **no reflash, no separate build** |
| When `false` | Frames from `0xF0`–`0xFE` are received, authenticated, decoded and **counted in the bridge's own diagnostics**, then dropped before publication. They are *not* discarded at §14 — a bench node still exercises the full receive path |
| When `true` | Published to `lran/simnode<N>/diag/state` and `lran/simnode<N>/availability` only |
| Never, in either state | `gate`, `detect`, `battery`, `solar`, `event`. **A bench node cannot reach the email/SMS path §16.3 exists to protect** |
| Discovery | `entity_category: diagnostic`, `unique_id` prefix `lran_simnode<N>_`. Entities are **not** removed when the flag is cleared — the bridge publishes `offline` to their availability topic instead, so re-enabling does not churn `unique_id` registrations |

> **The gate is on publication, not on reception.** Dropping bench frames at the radio
> would mean the bench node exercises a different code path from a real node, which
> defeats the purpose of having one. Decode everything; publish selectively.

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
| Build | **PlatformIO**, own project per firmware (§5.4). Framework `arduino` with ESP-IDF components reachable | — |
| Host tests | PlatformIO `native` + **Unity** | MIT |
| Host tooling | **Python 3** — `/tools/simctl/`, `/tools/vectors/` | — |
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
    publish.cpp         publication policy: on-change, staleness,      [app_task]
                        bench-node publication gate (§4.2a)
    hex_proxy.cpp       HEX wrap/unwrap, arm state, audit trail        [app_task]
    decode/
      gatelink.cpp      schema 0x10 / 0x11 / 0x12 decoders
      health.cpp        schema 0xF0 decoder, all node types
      synthetic.cpp     schema 0xFE decoder — mirrors 0x10, marks synthetic
      welllink.cpp      schema 0x20 — stub
    ui.cpp              OLED status page                               [ui_task]
    debug.cpp           loopback, dummy publish, bridge-side simulators
  lib deps ->
    /lib/lran-protocol/
    /lib/lran-config/
    /lib/vedirect/      HEX register model (bridge side)
  CLAUDE.md             subproject context for Claude Code
```

`/docs/bridge/engineering-log.md` carries the dated running record, including the range
test results and the antenna siting decision.

### 5.4 Repository layout for the bench fleet

`simnode` is a **separate firmware target, not a build variant of the bridge.** It links
the same protocol library and nothing else of the bridge's. Keeping it separate is what
prevents the failure mode where the simulator and the thing it tests share a bug and
agree with each other.

**Each firmware is its own PlatformIO project**, not an environment inside one project.
The targets differ structurally — different boards, different peripheral sets, different
partition tables — and a single project accumulates conditional build flags until nobody
can tell which target a given define applies to.

```
lran/
  CLAUDE.md             repo-wide invariants — AUTHORITATIVE (§11.5)
  lib/                  shared, referenced via lib_extra_dirs
    lran-protocol/      frame codec. Own plan: LRAN-Protocol-Library-Impl-Plan
    lran-config/        parameter registry, hand-written C++ headers
    lran-sim/           synthetic generators + malformed-frame primitives.
                        Shared by simnode AND /tools/vectors/ so the on-air
                        fault injector and the host vectors agree byte-for-byte
    vedirect/
  firmware/
    bridge/    platformio.ini  src/  CLAUDE.md
    simnode/   platformio.ini  src/  CLAUDE.md
    gatelink/  platformio.ini  src/  CLAUDE.md
    rangetest/ platformio.ini  src/                (§11.2 — no lib deps)
  tools/
    simctl/             Python — drives a simnode over USB serial
    vectors/            Python — W4 generation and checking
  docs/
    bridge/engineering-log.md
    simnode/engineering-log.md
    protocol-lib/engineering-log.md
  ha/                   example discovery payloads
  secrets.h.example     committed template; secrets.h is gitignored (§11.4)
```

Each firmware project reaches the shared code with:

```ini
lib_extra_dirs = ../../lib
```

**Every firmware project also defines a `native` environment** that builds and runs the
shared-library tests. A library that only compiles for ESP32-S3 has quietly acquired a
platform dependency, and the place that surfaces is the host build.

> **Why `/lib/lran-sim/` exists rather than living inside `/firmware/simnode/`.** The
> malformed frames in §10.5 are needed in two places: on air, from the simnode, and on the
> host, in the committed test vectors (**W4**). If the two are generated by separate code,
> a passing unit test and a passing bench run can both be wrong in the same way and
> neither will say so. One generator, two consumers.

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
| **Node simulators** | Bridge-side generators per node type. Complements `simnode` (§10), which tests the RF path; these test the MQTT path. **The two must not share a generator** — a bridge-side simulator that feeds the decoder its own output tests nothing |
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
| §14 discard ladder, stages 2–9 | `simnode` `ROLE_FAULT`, §10.5 catalogue | B3 |
| §14 stage 1 (PHY CRC) | **Not injectable** — collect at the far edge of the B1 range walk (§10.5) | B1 |
| §5.8 `UNKNOWN_HDR_EXT` | `fault crit_ext`; and `fault hdr_rsv` must be **accepted** | B3 |
| §16.6 bench publication gate | `simnode_diag_enable` toggled from HA at runtime, both states | B4 |
| W9 full-size and fragmented `PING` | `ping <id> 202 pattern` and `ping <id> <n> pattern frag` | B3 |
| Media access under real contention | **Third board required** — two simnode boards transmitting concurrently (§2.1) | B3 |

### 7.2 Bench harness

The bridge is the easiest node in the fleet to test, and that should be exploited:
`mosquitto_sub -t 'lran/#'` shows every decoded payload live, `mosquitto_pub` injects
commands, and `simnode` (§10) supplies the RF side. **Every verification except V-B1,
§14 stage 1 and real-contention media access is reachable at a desk.**

Regression scenarios are committed as `/tools/simctl/` scripts rather than kept as bench
procedure. The §10.5 catalogue is long enough that a hand-run pass will silently skip
entries, and the entries most likely to be skipped are the ones whose expected result is
"nothing happens" — `hdr_rsv` accepted, `seq_wrap` accepted, `wrong_dst` discarded with no
`ERROR`. Those are exactly the forward-compatibility rules that break quietly.

**Observe TX power before every session.** Bench work runs at reduced power per §2.2; a
test that silently ran at +22 dBm across a desk produced meaningless RSSI, and a range
test that silently ran at bench power has to be repeated.

---

## 8. Milestones and acceptance criteria

| # | Milestone | Depends on | Acceptance criteria |
|---|---|---|---|
| **B1a** | **RF path characterization** | Two Heltec boards, `lran-rangetest` (§11.2) | RSSI and SNR measured at ~500 ft **on both the gate bearing and the well bearing**, across candidate SF/BW/CR settings. **D1 resolved** with a stated link margin. Bridge antenna type and position chosen and recorded. Airtime table regenerated (**M19**). FCC operating mode question (**W5**) settled before a TX power is fixed. **Margin figure carries an explicit "Heltec radio" caveat until B1b** |
| **B1b** | **Target-radio confirmation** | B1a, XIAO + Wio-SX1262 delivered | Range re-measured on the gate bearing with the **Wio-SX1262** at the B1a settings. Delta from B1a recorded — this is the module contribution to link margin. **D1 confirmed** or revised. §2.3.1 findings settled by measurement: whether an RXEN-style line is required, and the exact module part number. **PHY-CRC discard counters observed at the far edge of the link** (§10.5) |
| **B0** | **Simnode bring-up** | Second board in hand, `/lib/lran-protocol/` | `lran-simnode` flashes and runs. Identity table holds four entries with independent keys, contexts and sequence spaces. Serial console (§10.4) accepts every command. `ROLE_RANGE` echoes `PING`. Faults arm, fire the specified count and self-disarm, with armed state shown on the OLED |
| **B2** | **Board bring-up and OTA** | Board in hand | WiFi connects and reconnects; MQTT connects with LWT registered; A/B partitioning configured; OTA succeeds over WiFi; **a deliberately bad image rolls back**. Version published. OLED shows a status page |
| **B3** | **Protocol and registry, with simnode** | B2, `/lib/lran-protocol/`, **B0** | Frames round-trip against the committed test vectors. **Four logical simnodes registered simultaneously from one board** (§10.3), each with its own derived key, context and sequence space. Context resync retries once and then faults. **A suppressed ACK produces a retry with the same `seq`, and the simnode reports a deduplicated hit rather than a second execution.** Availability marks offline after 3 missed polls and online on the next frame. Version tolerance accepts N−1 and rejects N−2 with a distinct reason. **The whole §10.5 fault catalogue runs from a committed `simctl` script**, every §14 counter increments as specified, and `hdr_rsv` is accepted rather than discarded. Full-size (222 B) and fragmented `PING` both round-trip (**W9**). *With a third board: two simnode boards transmitting concurrently exercise CAD and backoff* |
| **B4** | **MQTT, discovery and publication policy — no node hardware** | B3 | Discovery publishes one device per node, correct availability references, **and republishes on broker restart**. All §6.3 policy rules demonstrated: jitter suppressed, staleness marks unavailable, sentinels not published as numbers, synthetic marked, heartbeat republish works. **Events publish non-retained and do not replay on HA restart or discovery refresh.** The whole fleet is demonstrable with dummy publish and simulators only |
| **B5** | **HEX proxy** | B4, a real MPPT reachable via GateLink or a simulator | Read passes. Write rejected while disarmed, accepted while armed, **and the arm auto-expires with the switch published back to off**. Every attempt appears in the retained audit trail. Charge-parameter readback published as diagnostic sensors on boot |
| **B6** | **GateLink integration** | B5, GateLink M6 | End-to-end with the real node: command round-trip, status decode, event delivery, per-node availability, diagnostics populated |
| **B7** | **Soak** | B6 | Continuous operation across broker restarts, WiFi outages and a node power cycle, with no lost frames on reconnect and no stuck availability state |

**Critical path:** B2 → B3 → B4 → B5 → B6 → B7. **B1a is independent of all firmware
work and should be done first in wall-clock terms** — it needs only two Heltecs and
`lran-rangetest` (§11.2), gates GateLink's PHY configuration as well as this node's
antenna siting, and can start before a line of shared code exists.

**B0 depends on the protocol library reaching P6** (see
[`LRAN-Protocol-Library-Implementation-Plan`](../shared/LRAN-Protocol-Library-Implementation-Plan.md)),
sits before B3, and can be built in parallel with B2. It is deliberately separated
from B3 rather than folded into it: if the simnode's own framing is wrong, every B3
failure is ambiguous between the instrument and the thing being measured. Prove the
instrument first, against the committed test vectors (**W4**), before using it to judge
the bridge.

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

### 9.3 The instrument must be provable independently of the thing it measures

`simnode` and the bridge link the same `/lib/lran-protocol/`. That is correct — a
simulator built on a second, parallel codec tests agreement between two implementations,
not conformance to the spec — but it creates one blind spot worth naming: **a bug in the
shared codec is invisible to any test that uses both ends of it.** Both sides serialize
`last_traversal_age_s` as 16 bits, both agree, everything passes, and the frame is wrong.

The committed test vectors (**W4**) are the only thing that closes this. They are
generated on the host, checked against hand-computed MACs and CRCs, and are the reference
that neither firmware target can vote on. **This makes W4 a prerequisite for B0, not a
parallel task** — a simnode validated only against the bridge is a mirror, not an
instrument. The vectors are owned by
[`LRAN-Protocol-Library-Implementation-Plan`](../shared/LRAN-Protocol-Library-Implementation-Plan.md)
§5, generated by an independent Python implementation rather than by calling the C++
library, for exactly this reason.

`/lib/lran-sim/` deliberately post-processes real frames rather than building its own
(§10.6), which keeps the fault injector honest for the same reason and by the same
mechanism.

### 9.4 Carried forward from earlier revisions

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

## 10. Simnode firmware and the bench fleet

**Firmware target:** `lran-simnode` — `/firmware/simnode/`
**Node IDs:** `0xF0`–`0xF3` (Protocol Spec §5.3)
**Hardware profiles:** `simnode-heltec` (Heltec V3) and `simnode-xiao-wio`
(XIAO ESP32S3 + Wio-SX1262) — two PlatformIO environments, one firmware (§10.8)

`simnode` is the fleet's test instrument. It is the only thing that exercises the
bridge's receive path, registry, scheduler, command retry and error handling **before
GateLink exists**, and the only thing that will exercise them **after GateLink is
installed and unreachable without a walk to the gate** (§10.7).

### 10.1 Scope — what simnode is and is not

| It is | It is not |
|---|---|
| A protocol-level peer: correct frames, correct keying, correct sequencing | A functional model of a gate. It has no state machine, no relays, no I/O |
| A deliberate producer of **wrong** frames (§10.5) | A validator of GateLink's radio bring-up — different module, pin map, TCXO (§2.1) |
| Runtime-scriptable over serial, no reflash per scenario | A source of realistic telemetry values. Its numbers are plausible, not physical |
| Multi-identity: several logical nodes on one board (§10.3) | A substitute for real RF contention with one board (§2.1) |

**The synthetic marker is not optional.** Every payload simnode emits sets
`status_reason = DEBUG_SYNTHETIC` (Protocol Spec §8.7) and uses schema `0xFE` rather than
`0x10` where a status is called for. The bridge propagates that marking into MQTT (§6.3).
**Synthetic data that reaches HA history unmarked is a bug in both nodes at once**, and
the failure is silent — it looks like real history until someone tries to explain a
reading.

### 10.2 Roles

One firmware, four selectable roles. A role is a **behaviour profile**, assigned per
logical identity at runtime (§10.4), not a separate binary.

| Role | Answers | Purpose | Milestone |
|---|---|---|---|
| `ROLE_RANGE` | `PING` echo, `0xF0` on poll | Range test and link characterization. Minimal, so a failure is unambiguously RF | **B1** |
| `ROLE_HEALTH` | `0xF0` on poll | The generic node — what WellLink looks like before it has a schema. Registry, availability and scheduling filler for multi-node tests | **B3** |
| `ROLE_GATELINK` | `0xFE` on poll, `0x11` events, `COMMAND_ACK`, `0x12` config | The full peer. Exercises command retry, dedup, event dedup, config ACK semantics and the whole decode path with no gate present | **B3**, **B4** |
| `ROLE_FAULT` | Deliberately malformed frames (§10.5) | The **only** test vehicle for the §14 discard ladder and its counters | **B3** |

> **`ROLE_RANGE` is deliberately impoverished.** During **B1** the question is what the
> radio and the path do, and every line of protocol logic in the way is a line that can
> produce a symptom indistinguishable from poor link margin. A range peer that only echoes
> `PING` and reports health means a failed measurement is an RF problem, full stop.

### 10.3 Multi-identity — several nodes on one board

A simnode board holds an **identity table** of up to four entries:

| Field | Notes |
|---|---|
| `node_id` | `0xF0`–`0xF3` |
| `key` | Derived by HKDF from `master_key` exactly as the bridge derives it (Protocol Spec §9.1). The simnode is provisioned with `master_key` for bench convenience; **a real node never is** |
| `role` | §10.2 |
| `ctx_id` | Independent random non-zero value per identity, regenerated on demand |
| `tx_seq`, `cmd_seq` | Independent sequence spaces per identity, per Protocol Spec §10.2 |
| `enabled` | A disabled identity stops transmitting and stops answering — this is how the availability watchdog is tested without power-cycling anything |
| `proto_ver` | Per identity, so N and N−1 can be on air simultaneously |

The radio is shared and access to it is serialized internally. **From the bridge's side
this is indistinguishable from four physical nodes**, because §4.2's per-node state is
keyed on `node_id` and never on the radio. See §2.1 for the one class of test this cannot
cover.

> **This is a test of the bridge, not just a convenience.** If four identities on one
> radio behave differently at the bridge from four radios, something in the bridge is
> keyed on the wrong thing. The multiplexing is cheap; discovering that seam is the
> valuable part.

### 10.4 Control interface — serial console

**No scenario requires a reflash.** The simnode exposes a line-oriented console on USB
serial, driven by hand or by `/tools/simctl/`.

| Command | Effect |
|---|---|
| `id add <hex> <role>` / `id del <hex>` | Add or remove a logical identity |
| `id list` | Table of identities, roles, ctx, seq, enabled |
| `enable <hex>` / `disable <hex>` | Stop or resume an identity — availability watchdog test |
| `ver <hex> <n>` | Set the `ver` an identity announces — version-tolerance test (**V-B10**) |
| `ctx <hex> [new]` | Show or regenerate `ctx_id` — resync test |
| `push <hex> [reason]` | Emit an unsolicited status with a given `status_reason` |
| `event <hex> <type>` | Emit an event; repeat the same `event_id` to test bridge-side dedup |
| `ack <hex> <mode>` | `normal` \| `suppress` \| `delay <ms>` \| `dup` — command-path tests |
| `ping <hex> <n> [pattern] [frag]` | Emit a `PING` of length `n`, optionally `PATTERN_FILL`, optionally forced-fragmented (**W9**) |
| `fault <hex> <name> [count]` | Inject a fault from §10.5, once or `count` times |
| `field <hex> <name> <value>` | Override a generated telemetry field — sentinels, out-of-range, stale flags |
| `log <level>` | Serial verbosity |

`/tools/simctl/` scripts these into repeatable scenarios so a regression run is one
command rather than a remembered sequence of keystrokes. **Scenario scripts are committed**
— an ad-hoc bench procedure that lives in someone's head is not a regression test.

### 10.5 Fault catalogue

Each entry drives one stage of Protocol Spec §14 or one rule in §9.4/§10. **This is the
only mechanism that produces these frames**, and without it every discard counter in the
bridge ships unverified.

**The counter column is normative and comes from Protocol Spec §14.1**, not from this
table. §14.1 gained a wire-code column in spec v0.6 for exactly the reason this column
exists here: the bridge publishes these names to MQTT, Home Assistant charts them, and a
rename after that is breaking. A row whose counter does not appear in
`kCounterRegistry` is a defect in this table.

| `fault` name | Produces | Counter | Expected bridge behaviour |
|---|---|---|---|
| `runt` | Frame shorter than `LRAN_HDR_LEN + LRAN_CRC_LEN` | `rx_runt` | §14 stage 2 |
| `oversize` | A frame longer than `LRAN_MAX_FRAME` — the PHY hands up 255 bytes | `rx_oversize` | **Stage 2a**, **no ERROR emitted.** Means a foreign transmitter or a misconfigured PHY, which is a different diagnosis from a bad length and is why v0.4 split the counter out |
| `bad_crc` | Correct frame, corrupted CRC16 | `rx_bad_crc` | Stage 3. **Not** `rx_crc_err`, which is the PHY CRC and belongs to the radio driver |
| `bad_ver` | `ver` = N−1, then N−2 | `rx_bad_ver` | Stage 4. N−1 accepted, **N−2 rejected with a distinct reason** (**V-B10**) |
| `wrong_dst` | `dst` = an unrelated node ID | `rx_not_addressed` | Stage 5, **no ERROR emitted** |
| `crit_ext` | `hdr_flags` bit 7 set, no extension defined | `rx_unknown_hdr_ext` | **Stage 5a**, `ERROR(UNKNOWN_HDR_EXT)`. The only test of Protocol Spec §5.8 |
| `hdr_rsv` | Non-zero bytes 13–15, bit 7 clear | — | **Accepted and ignored** — the forward-compatibility rule (§4.3). A discard here is a bug |
| `frag_zero` | A `frag` total of `0` | `rx_bad_frag` | **Stage 5b**, `ERROR(BAD_LENGTH)`. Protocol Spec §5.6 declares it malformed |
| `unknown_type` | `type` = `0x0C` | `rx_unknown_type` | Stage 6, `ERROR(UNKNOWN_TYPE)` |
| `unknown_schema` | `schema` = `0x7F` | `rx_unknown_schema` | Stage 7, `ERROR(UNKNOWN_SCHEMA)` |
| `bad_length` | Valid schema, payload one byte short and one byte long | `rx_bad_length` | Stage 8, `ERROR(BAD_LENGTH)` |
| `frag_command` | A `COMMAND` carrying a `frag` total > 1 | `rx_not_fragmentable` | **Stage 8a**, `ERROR(BAD_LENGTH)`. Protocol Spec §11.4 rules the type single-frame; the wire answer and the counter deliberately disagree, and **the counter is the diagnosis** |
| `frag_timeout` | Fragment 1 of 3, then silence | `rx_reassembly_timeout` | Expires after `frag_reassembly_timeout_ms`, `ERROR(REASSEMBLY_TIMEOUT)`. **Must fire from the periodic tick**, not only on the next arrival — otherwise a peer's silence looks like nothing happened |
| `frag_overflow` | Fragment index ≥ declared total | `rx_fragment_overflow` | `ERROR(FRAGMENT_OVERFLOW)` |
| `frag_oversize` | Reassembled set exceeding `LRAN_MAX_SCHEMA_PAYLOAD` | `rx_fragment_overflow` | `ERROR(FRAGMENT_OVERFLOW)` |
| `frag_dup` | A duplicate index within a live set | `rx_frag_duplicate` | **Set still completes.** Counted but **excluded from `rx_dropped`** — assert `rx_dropped` does not move |
| `frag_late` | A complete set, then a repeat of one of its fragments | `rx_frag_late` | Discarded, **not** started as a new set. Excluded from `rx_dropped`. A late RF echo and a sender retry both produce this legitimately |
| **`single_frame_interleave`** | Fragment 0 of a set, then a **single-frame** frame sharing `(src, ctx_id, schema)`, then the remaining fragments | **none** | **The set completes normally and `rx_reassembly_abandoned` does not move.** See below |
| `set_displaced` | A live set, then fragment 0 of a **different** set from the same peer | `rx_reassembly_abandoned` | Protocol Spec §11.3 displacement, scoped to `frag` total > 1 |
| `bad_mac` | Authenticated frame, one MAC byte flipped | `rx_rejected_mac` | §9.4 step 3 rejection, **no state change**, and the fragment is **not buffered** |
| `ctx_jump` | New `ctx_id` mid-session with no reboot | `rx_rejected_ctx` on the node side | Bridge adopts, resets `cmd_seq`, **retries once only** (§6.2) |
| `seq_jump` | Large forward `seq` step | — | Accepted — RFC 1982 arithmetic, no lockout |
| `seq_wrap` | `seq` wrapping through `0xFFFF` | — | Accepted. **The failure this guards against is a plain `>` comparison rejecting every frame until reboot** |
| `ack_suppress` | `COMMAND` received, no ACK | — | Bridge retries with the **same `seq`**; simnode reports a dedup hit (**V-B5**, **BS-3**) |
| `ack_dup` | Two ACKs for one command | — | Second ignored, not counted as a second result |
| `event_replay` | Same `(ctx_id, event_id)` twice | — | Bridge publishes **once** (Protocol Spec §16.3) |
| `flood` | Frames at maximum rate | — | Bridge stays responsive; `lora_task` does not block (§1.3) |
| `silent` | Identity stops answering | — | Availability → offline after `missed_poll_threshold` (**V-B3**) |

**`single_frame_interleave` is the highest-value entry in this table**, and the only test
of spec v0.6's sole behavioural change. §11.2 states that a single-frame frame never
begins, joins, displaces or expires a set. The defect it fixes is a receiver routing every
frame through one slot per peer, where **a node's periodic `STATUS` destroys that same
node's in-progress fragmented `CONFIG_ACK`** — recoverable by readback, and reliably
recurring. It is silent by construction: the offending frame belongs to no set, so nothing
is counted. That is why the expected result is a *set that completes and a counter that
does not move*, and why no other entry can substitute for it.

### 10.5.1 Two more once **P8** lands — and the direction reverses

**D34**'s `CommandGate` is the receiver of these, so they test the **simnode's own** gate,
driven from `simctl` rather than from the bridge. They belong here because §10.5 is the
document of record for what the fleet's discard paths are verified against, and because
per Protocol Spec §9.2 **every authenticated type is bridge → node** — so these two paths
exist *only* on a node and would otherwise be verified nowhere.

| `fault` name | Produces | Counter | Expected node behaviour |
|---|---|---|---|
| `cmd_replay` | The same `(ctx_id, seq)` `COMMAND` twice, after the first has executed | `rx_dup_command` | The **cached** ACK is returned and **the relay does not pulse a second time**. Excluded from `rx_dropped` — the retry mechanism working as §10.4 requires is not a fault |
| `cmd_stale_seq` | A `COMMAND` whose `seq` is below the high-water mark | `rx_rejected_seq` | `COMMAND_ACK(REJECTED_SEQ)`, counted **into** `rx_dropped` |

`ack_suppress` above already exercises the dedup path, but it asserts it from the
bridge's side. `cmd_replay` asserts it **at the gate, where the relay is** — which is the
assertion root rule 2 and **BS-3** actually care about. A second pulse at a driveway gate
is the failure this whole mechanism exists to prevent, and it has never been tested at
the end that pulses.

### 10.5.2 `/lib/lran-sim/` needs a post-encode patch primitive

Three of the new entries cannot be built by post-processing a correct frame the obvious
way: `oversize` needs a frame longer than `encode()` will emit, `frag_zero` needs a `frag`
byte the encoder overwrites, and `frag_command` needs a `frag` total on a type
`encode_fragment()` refuses with `NotFragmentable`. A narrow **patch-after-encode**
surface — set a header byte, extend or truncate the trailer, recompute or deliberately
skip the CRC — keeps simnode rule 2 intact (**never a second serializer**) while making
them reachable. Scope it before B0 rather than during it; discovering it mid-milestone is
how a second serializer gets written.

**One fault cannot be injected: `bad_phy_crc`.** The SX1262 computes and checks the PHY
CRC in hardware, so a transmitter cannot emit a frame that fails it. §14 stage 1 is
therefore verified only by real marginal RF — collect it during the **B1** range walk at
the far edge of the link, where corrupt frames occur naturally, and record the counter
behaviour then. This is worth knowing in advance: it is the one discard path that cannot
be closed at a desk.

### 10.6 Firmware module map

```
/firmware/simnode/
  src/
    main.cpp          task creation, radio init, identity table load
    identity.cpp      multi-identity table, per-id key/ctx/seq state
    radio.cpp         RadioLib, shared-radio arbitration across identities
    console.cpp       serial command interface (§10.4)
    roles/
      role_range.cpp      PING echo + 0xF0
      role_health.cpp     0xF0 only
      role_gatelink.cpp   0xFE status, 0x11 events, ACKs, 0x12 config
      role_fault.cpp      fault selection and dispatch (§10.5)
    ui.cpp            OLED: identity table, last frame, fault armed
  lib deps ->
    /lib/lran-protocol/
    /lib/lran-sim/    generators and malformed-frame primitives
  CLAUDE.md           subproject context for Claude Code
```

**Two implementation rules for Claude Code, both load-bearing:**

1. **`/lib/lran-sim/` builds malformed frames by *post-processing* a correct frame from
   `/lib/lran-protocol/`.** It must not contain a second, hand-written serializer. A
   separate serializer drifts from the real one, and then a fault test passes while
   testing a frame the system would never produce.
2. **Every fault is armed for a bounded number of frames and then self-disarms.** A
   simnode left in a fault mode looks exactly like a broken bridge, and the bench session
   where that costs an hour is the one where you were debugging something else. `ui.cpp`
   shows armed faults on the OLED for the same reason.

### 10.8 Hardware profiles, not roles

The two board types are **build environments over one firmware**, not variants of it. A
role is behaviour and is assigned at runtime; a profile is a pin map and is fixed at
compile time.

```ini
; /firmware/simnode/platformio.ini
[env:simnode-heltec]
board = heltec_wifi_lora_32_V3
build_flags = -DLRAN_PROFILE_HELTEC

[env:simnode-xiao-wio]
board = seeed_xiao_esp32s3
build_flags = -DLRAN_PROFILE_XIAO_WIO
```

The profile supplies exactly one thing: a `RadioPins` struct — NSS, RST, BUSY, DIO1, the
SPI pins, TCXO voltage, DIO2-as-RF-switch, and (pending §2.3.1) an optional RF-switch pin.
Everything above the driver is identical.

> **This is also a free test of R-4.1b.** The requirement says the radio pin map is
> supplied by configuration rather than hardcoded, because GateLink's carrier shares none
> of the Heltec's values. If the pin map turns out not to be cleanly injectable, that is
> discovered on a $20 dev board rather than during carrier bring-up — which is exactly
> the kind of finding that is cheap in August and expensive in November.

#### 10.8.1 The pin maps

> ### THIS SECTION'S PREMISE FAILED. Corrected 2026-09-05.
>
> It read: *"The Wio pad assignment is a property of the module, not of the host. The
> Wio-SX1262 occupies the same eight XIAO-footprint pads wherever it is used... That
> identity is the entire reason the XIAO profile validates anything about GateLink — if it
> ever stops being true, §2.3's claim collapses."*
>
> **It stopped being true.** Seeed sells two Wio-SX1262 products. The **header board**
> (p-6379) does occupy those eight pads and is GateLink's module. The **Kit** (p-5982,
> the board that arrived) connects over a **B2B connector** instead, on GPIO 38–42, and
> touches none of them. The pad topology is *not* identical across products, so the
> identity this section rested on does not hold for the hardware in hand.
>
> §2.3's claim is therefore narrowed rather than collapsed — see §2.3.1 finding 2.
>
> **The old table's "XIAO ESP32S3 GPIO" column was the header board's**, which is
> GateLink's module and not this section's subject. It has been **moved to
> `gatelink-expansion-board.md` §6.1** rather than kept here in a corrected form: this
> section describes what the simnode builds, and a second copy of a map that belongs to
> another board is precisely how the wrong column reached two other documents.

**The header board's pad map now lives in `gatelink-expansion-board.md` §6.1**, moved
there 2026-09-05. It is GateLink's module and its pad assignment is a property of that
board's design; this section is about what the **simnode** builds, and the simnode builds
the Kit. Keeping a second copy here is exactly how the wrong column got copied into two
other documents in the first place.

> **PREMISE CHECK.** The claim that the header board's pads map to the carrier's nets as
> `gatelink-expansion-board` §6 describes is **falsified by a continuity test**, tracked as
> the *"Wio socket pad mapping — ring out each D-pad"* item in that document's §10,
> *Verify before soldering*. It remains unticked. Named because the previous version of
> this section stated its own falsification condition in prose, tracked it nowhere, and did
> not notice when it came true (root `CLAUDE.md`, *A load-bearing premise must name the
> check that would falsify it*).

**For the Kit (p-5982) — the board in hand.** No D-pad column, because the module does not
use the D-pads: these cross the B2B connector.

| Function | XIAO GPIO (Kit) |
|---|---|
| MISO / SCK / MOSI | 8 / 7 / 9 — the only three nets the two products share |
| NSS | 41 |
| RST | 42 |
| BUSY | 40 |
| DIO1 | 39 |
| RF_SW | 38 |

TCXO is **1.8 V via DIO3 on all three**, and differs from the Heltec's value — this is the
one radio constant that cannot be shared across profiles.

**`rf_sw` is a real pin on every Wio profile.** Confirmed 2026-09-05: Seeed does not tie
DIO2 to the RF switch internally, so both mechanisms are needed (§2.3.1 finding 1), and
`firmware/range-test` has since driven it over the air — 192 probes out, 192 echoes back
on the Kit. `setRfSwitchPins(rxEn, txEn)`, RF_SW being the **RX enable**.

```c
// /firmware/simnode/src/profiles.h
struct RadioPins {
  int8_t nss, rst, busy, dio1;
  int8_t sck, miso, mosi;
  int8_t rf_sw;          // RADIOLIB_NC when DIO2 alone drives the switch
  float  tcxo_v;
  bool   dio2_as_rf_switch;
};

#if defined(LRAN_PROFILE_HELTEC)
  // Heltec WiFi LoRa 32 V3 — SX1262 on a dedicated internal SPI bus
  constexpr RadioPins kRadio = {
    .nss = 8, .rst = 12, .busy = 13, .dio1 = 14,
    .sck = 9, .miso = 11, .mosi = 10,
    .rf_sw = RADIOLIB_NC, .tcxo_v = 1.8f, .dio2_as_rf_switch = true };

#elif defined(LRAN_PROFILE_XIAO_WIO_KIT)
  // Kit p-5982, B2B connector — THE BOARD IN HAND. Control lines cross the B2B
  // connector, which is why they are GPIO 38-42 and not D-pad numbers.
  constexpr RadioPins kRadio = {
    .nss = 41, .rst = 42, .busy = 40, .dio1 = 39,
    .sck = 7, .miso = 8, .mosi = 9,
    .rf_sw = 38, .tcxo_v = 1.8f, .dio2_as_rf_switch = true };
// A header-board profile is deliberately NOT defined here. That module is GateLink's;
// its map is gatelink-expansion-board.md 6.1, and GateLink's firmware supplies it as a
// third instance of this struct without any driver change - which is the whole of R-4.1b.
#endif
```

**Provenance, updated 2026-09-05.** The Heltec values were the community-standard V3
assignment and are now the vendor's — see the confirmation below. The Kit values are
transcribed from meshtastic/firmware `variants/esp32s3/seeed_xiao_s3/variant.h` and have
since been proven over the air. The XIAO ESP32S3 D-pad → GPIO numbering
(D0–D10 = GPIO 1, 2, 3, 4, 5, 6, 43, 44, 7, 8, 9) is transcribed from the vendor variant at
the pinned framework version. ~~**Ring out the header-board column and correct this table
in place**~~ — that column has moved to `gatelink-expansion-board.md` §6.1 and the
instruction moved with it — it is the reference every subsequent document
will copy from, and a wrong entry here propagates silently.

> **The Heltec column is now confirmed, 2026-08-31.** Range test R2 transcribed it from
> the vendor board definition shipped with the Arduino core —
> `framework-arduinoespressif32/variants/heltec_wifi_lora_32_V3/pins_arduino.h` at
> `3.20017.241212`, the version `espressif32@6.13.0` resolves — and every value above
> matches: `SS 8`, `SCK 9`, `MISO 11`, `MOSI 10`, `RST_LoRa 12`, `BUSY_LoRa 13`. It is no
> longer "the community-standard assignment"; it is the vendor's.
>
> **One trap the vendor header sets.** It calls GPIO 14 **`DIO0`** — the SX127x name. On
> the SX1262 that line is **DIO1**, which is what RadioLib wants as its IRQ pin. The
> number in the table above is right and the vendor's label is legacy; anyone who trusts
> the symbol over the number will go hunting for a GPIO that does not exist.
>
> **The Kit column is now transcribed and proven, 2026-09-05.** Taken from
> meshtastic/firmware `variants/esp32s3/seeed_xiao_s3/variant.h`, and then confirmed on
> hardware the only way a radio pin map can be — 192 frames out and 192 echoes back.
> `begin()` succeeding proves nothing here: a wrong `rf_sw` initialises just as cleanly
> and transmits into a dead end.
>
> **The header board is no longer described here.** Its map, its corroboration and its
> outstanding ring-out are `gatelink-expansion-board.md` §6.1's, as of 2026-09-05.

**`rf_sw` is deliberately present in the Heltec entry as `RADIOLIB_NC`, not absent.** The
struct shape is fixed across profiles so the driver has no conditional compilation in it.
A profile that omits fields is a profile that will grow an `#ifdef` in the role code, which
is the failure mode §10.8 exists to prevent.

**Do not `#define` these into the driver.** `kRadio` is passed to the radio wrapper's
constructor. The point of R-4.1b is that GateLink's firmware supplies a third instance of
this struct without any code in the driver changing; a profile that reaches the driver
through the preprocessor has not tested that.

**Assign roles to profiles as follows:**

| Board | ID | Role | Rationale |
|---|---|---|---|
| Heltec #2 | `0xF0`, `0xF2` | `ROLE_FAULT` + `ROLE_HEALTH` | Fault injection needs no radio fidelity |
| XIAO + Wio | `0xF1` | `ROLE_GATELINK` | **The identity pretending to be GateLink runs GateLink's actual radio** |

### 10.7 Keep a board permanently — five reasons

**Yes: plan on a dedicated Heltec for the life of the system, not just through bring-up.**

1. **GateLink has no OTA.** Every protocol change, schema addition and `ver` bump is a USB
   reflash at the gate, in whatever weather. Without a bench peer, **the validation
   vehicle for a protocol change is the production gate.** One board is cheap insurance
   against a walk down the driveway with a laptop.
2. **The fault catalogue is a regression suite, not a bring-up tool.** Protocol Spec
   **W8** anticipates a header extension using `hdr_flags` bit 7; §13.2 anticipates new
   schema IDs as nodes gain capabilities. Each of those needs the §10.5 ladder re-run.
   That is a permanent need with a permanent hardware requirement.
3. **WellLink development.** `ROLE_HEALTH` is WellLink's stand-in today, and WellLink's
   schema `0x20` will be developed against a simnode before its hardware exists — exactly
   as GateLink's is now. The pattern repeats for every node added to the property.
4. **Field triage.** §9.1 argues that a known-good radio pair lets a link failure be
   bisected into "the path degraded" versus "the node's radio failed." That argument does
   not expire at commissioning; it is *more* valuable in eighteen months, when the
   alternative is guessing about a node 500 ft away that has been outdoors through two
   winters.
5. **Cold spare.** The bridge is the single point of failure for all property telemetry.
   A board already flashed, already on the bench and already known to work is a
   fifteen-minute recovery instead of a shipping wait.

**Retire it only when there is a second permanently-installed bench peer**, which in
practice means: keep one.

---

## 11. Development environment and workflow

### 11.1 Build order

| Order | Work | Gate |
|---|---|---|
| 0 | **B1a range walk, Heltec ↔ Heltec** (§11.2) | None — starts today, no shared code |
| 1 | `/lib/lran-protocol/` **P1–P6**, incl. W4 vectors | Own plan document |
| 2 | `simnode` **B0** | P6 |
| 3 | `bridge` **B2** | P7 |
| 4 | **B1b** confirming range pass with the XIAO + Wio | XIAO delivery |
| 5 | **B3 → B7** | as before |

Two things move off the critical path here. The range walk needs no shared code at all
(§11.2), and the XIAO's delivery gates only the confirming pass, not the parameter
selection.

### 11.2 `lran-rangetest` — starting the RF work today

> **Superseded as of 2026-08-31. This section no longer describes the range test
> firmware.** The owning document is
> [`LRAN-Range-Test-Firmware-Pass1-Tasks`](../rangetest/LRAN-Range-Test-Firmware-Pass1-Tasks.md),
> and the target lives at **`firmware/range-test/`** — the path root `CLAUDE.md` uses,
> not the `/firmware/rangetest/` this section wrote. Three of the requirements below
> have been overtaken and are recorded here so the change is visible rather than
> silent:
>
> - **"No `/lib/` dependency whatsoever — not even `lran-protocol`" no longer holds.**
>   It was right when it was written: the range walk was item 0 in §11.1's build order
>   and the library did not exist, so every line of protocol logic in the path was a
>   line that could imitate poor link margin. The library is now built (P1–P7), and
>   Protocol Spec v0.7 §18 assigns **W9** — the 222-byte and fragmented `PING` bench
>   runs — to this firmware, which cannot be done without the codec. The tasks document
>   keeps the two apart by branch instead: **R4–R8 use raw RadioLib frames and touch no
>   `/lib/`**, and only **R9** links the codec. The isolation this section wanted is
>   preserved where it matters and dropped where it would block W9.
> - **Mode selection is the PRG button, not a serial keypress** — the walking end is
>   untethered, so a keypress needs a laptop it does not have. (Implemented as a
>   post-boot window; PRG is the BOOT strapping pin and cannot be held through reset.
>   See `docs/rangetest/engineering-log.md`, 2026-08-31.)
> - **The roles are `INITIATOR` / `RESPONDER`, not beacon / listen**, and the sweep is
>   automated over a test-point table rather than driven by hand over serial.
>
> Everything else below still stands, and the last two paragraphs — that this is a
> permanent instrument, and that **B1a is deliberately not conclusive** — are load
> bearing. Read them.

**The range walk should not wait for the protocol library.** B1a asks what the radio and
the path do; every line of protocol logic in the way is a line that can produce a symptom
indistinguishable from poor link margin (§10.2).

`/firmware/rangetest/` is therefore a **deliberately trivial, dependency-free** target:

- RadioLib only. **No `/lib/` dependency whatsoever** — not even `lran-protocol`
- Two modes selected by a serial keypress: beacon (transmit a numbered packet every
  2 s) and listen (receive, print index, RSSI, SNR, and running loss rate)
- SF / BW / CR / TX power settable over serial without reflashing, so a bearing can be
  swept in one walk instead of one walk per setting
- OLED shows the last RSSI/SNR in text large enough to read at arm's length outdoors —
  the listener is in a pocket or on a fence post, not on a desk with a laptop
- Logs one CSV line per packet to serial for later plotting

It is throwaway code with a permanent purpose: it stays in the repo as the instrument
that answers "is this link worse than it was?" for the life of the system.

**B1a is deliberately not conclusive.** It selects SF/BW/CR and characterizes both
bearings on Heltec radios. It must **not** be treated as fixing TX power for **W5**, and
its margin figure carries an explicit caveat until B1b re-measures against the
Wio-SX1262 (§2.3). Record both numbers; the delta is the module contribution, measured
once.

### 11.3 MQTT and Home Assistant environments

**Develop against the dev HA VM and a dev broker. Move to production only at B6.**

The reason is specific and is not about risk to production data. HA's entity registry
**remembers every `unique_id` it has ever seen.** Iterating on discovery payloads —
which is the whole of B4 — produces orphaned entities, and the second attempt at
`sensor.gate_state` arrives as `sensor.gate_state_2`. Cleaning that up is manual, tedious,
and has to be done again after the next iteration. A dev instance can simply be reverted.

The second reason is retained topics. A wrong discovery config published with the retain
flag **survives a bridge reflash** and will re-register the bad entity on the next HA
restart. During B4, `mosquitto_sub -t 'homeassistant/#' -v --retained-only` and a
retained-clear pass should be routine, and that is not something to be doing against a
live broker.

| Environment | Broker | Used for |
|---|---|---|
| **Dev** | Mosquitto on the build machine, alongside the dev HA VM | B2 – B5 |
| **Production** | Existing Mosquitto | B6 – B7, and only after discovery payloads are stable |

**The dev VM's ethernet requirement is not a constraint on the bridge.** The bridge
reaches the broker over WiFi and the broker host is already a configuration value; it
only needs the build machine's LAN address. Bridge the VM's adapter and both live on the
same subnet.

> Record the broker address in `secrets.h`, not in a source file, so switching
> environments is a rebuild rather than a diff that could be committed by accident.

### 11.4 Secrets

Committed template, gitignored real file:

```
/secrets.h.example      committed, placeholder values, documents every field
/secrets.h              gitignored, real values, referenced via -I by each project
```

Contents: `LRAN_MASTER_KEY` (32 bytes), WiFi SSID and password, MQTT host, port, username,
password, OTA password.

**Add `secrets.h` to `.gitignore` in the very first commit, before it exists.** The
failure mode here is a 32-byte master key in public git history, and the mitigation for
that is key rotation across every provisioned node — which, for GateLink, means a USB
reflash at the gate.

`secrets.h.example` is the documentation for what a fresh clone needs. Every field gets a
comment; a missing field should fail the build with a clear message rather than produce a
node that cannot authenticate for reasons nobody can see.

### 11.5 Git workflow

- **One branch per milestone**, named for it: `b0-simnode-bringup`, `p4-schemas`,
  `b3-protocol-registry`
- **Claude Code opens the PR and writes the description.** The description states which
  acceptance criteria from §8 the branch satisfies and which it does not, and links the
  engineering-log entries made during the work
- **`main` stays buildable.** Every PR builds all firmware targets *and* the `native`
  test environment before merge
- Commits reference the requirement or milestone identifier they serve (`R-3.3b`, `V-B5`,
  `B3`) so a later "why is this here" has an answer in the document set

> **The PR description is where the acceptance criteria get checked honestly.** A
> milestone table is easy to declare complete in conversation and hard to declare complete
> in writing next to the criteria it did not meet. That asymmetry is the point.

### 11.6 `CLAUDE.md` files

Four files, drafted alongside this revision.

| File | Contents | Authority |
|---|---|---|
| `/CLAUDE.md` | Repo-wide invariants, document map, build and test commands, coding standard, the rules that must never be broken anywhere | **Authoritative.** Conflicts resolve here |
| `/firmware/bridge/CLAUDE.md` | Bridge-specific context: task rules, registry model, the gotchas in §4.3 and §3.1 | Subordinate |
| `/firmware/simnode/CLAUDE.md` | Simnode-specific: roles, identity model, profiles, fault self-disarm rule | Subordinate |
| `/firmware/gatelink/CLAUDE.md` | Written when GateLink starts | Subordinate |

**The subordinate files state only what is specific to their target.** Anything that
applies in two places belongs in the root file — duplicated guidance drifts, and the copy
that drifts is the one that gets followed.

---

## 12. Changelog

- **v0.9** — Citation refresh only. Protocol specification **v0.8 → v0.9**: `ver` stays at
  `2`, and **no frame layout, header field, enumeration value, schema or authentication
  scope changes**; no vector regenerates and no build step here changes. **What reaches
  this node is regulatory and it reaches the bridge as a transmitter, not only as a
  document.** New spec **§18.2** records that neither SX1262 module is certified under
  §15.249, that module grants do not transfer, and that the operative frame is **§15.23
  home-built** — so **the bridge may not be represented as FCC certified** in a README, a
  header, an enclosure label or its HA device metadata. **B1a's acceptance criterion is
  affected in one respect worth naming**: it requires W5 settled "before a TX power is
  fixed", and W5 *is* settled — but **D33 is reopened in the register**, `BW` and the rule
  section are now one decision with D1, and §12.1's measured survey rules out the range
  test's provisional frequency. B1a's wording is left as written because its intent is
  unchanged; what changed is which document answers it. Read §18.2, not §18.1.
- **v0.8** — Citation refresh only. Protocol specification **v0.7 → v0.8**, which closes **W9** (the full-size and fragmented `PING` bench runs both passed over RF on 2026-09-05) and changes **no frame layout, header field, authentication scope or schema length**; no vector regenerates. No build step changes. The range test firmware, whose `lran-simnode` section this plan owns, has completed Pass 1: R1–R11 and W9 are done, and the boards it used are the two this plan's board-count guidance allocates.
- **v0.7** — **§11.2 superseded and §10.8.1's Heltec column confirmed**, both from the
  range test firmware's R1–R3 build (`docs/rangetest/engineering-log.md`, 2026-08-31).
  §11.2 described a `lran-rangetest` with **no `/lib/` dependency whatsoever**, written
  when the range walk was item 0 in §11.1's build order and the protocol library did not
  exist. The library is built and Protocol Spec v0.7 §18 assigns **W9** to that firmware,
  which needs the codec; the tasks document keeps the isolation per branch instead
  (R4–R8 raw, R9 against `/lib/`). The path, the mode-selection mechanism and the role
  names had all moved too. Marked superseded in place rather than deleted, so the
  divergence is visible. §10.8.1's Heltec pin map, which described itself as *derived*
  and "the community-standard V3 assignment", now matches the vendor variant value for
  value and is recorded as **confirmed** — with the vendor header's `DIO0` label for the
  SX1262's DIO1 line called out, because the number is right and the name is not. **The
  XIAO column remains unrung.**
- **v0.6** — **§10.5's fault catalogue brought up to the current receive path**, 21
  entries to 27, and given a **counter column** taken from Protocol Spec §14.1 rather
  than restated — the same defect §14.1's own wire-code column was created to retire,
  one layer down, and the bridge publishes these names to MQTT where a later rename is
  breaking. The catalogue was written against spec v0.3 and had no fault for stage 2a
  (`oversize`), stage 5b (`frag_zero`), stage 8a (`frag_command`), §11.3 displacement
  (`set_displaced`), or the two §11.2 non-fault paths (`frag_dup`, `frag_late`) whose
  correct result is that `rx_dropped` does **not** move. **`single_frame_interleave` is
  the one that matters**: it is the only test of spec v0.6's sole behavioural change,
  and the defect it catches — a node's periodic `STATUS` destroying that node's
  in-progress fragmented `CONFIG_ACK` — is silent by construction, so its expected
  result is a set that completes and a counter that stays still. **New §10.5.1** adds
  the two faults that arrive with **D34**/**P8**, noting that their direction reverses:
  per §9.2 every authenticated type is bridge → node, so `cmd_replay` and
  `cmd_stale_seq` test the *simnode's* gate. `ack_suppress` already touches dedup but
  asserts it from the bridge; `cmd_replay` asserts it **at the end that pulses a relay**,
  which is what BS-3 is about. **New §10.5.2** records that `/lib/lran-sim/` needs a
  patch-after-encode primitive for three of the new entries, to be scoped before B0 —
  discovering it mid-milestone is how a second serializer gets written. **No change to
  bridge design.**
- **v0.5** — Housekeeping revision; **no change to bridge design**. Binding protocol
  citation moves **v0.3 → v0.6**, and the body was reconciled against v0.4–v0.6 first.
  Two things to carry into B3 that the citation bump does not by itself record:
  **§10.5's fault catalogue predates the v0.4/v0.5 receive-path stages** and names no
  fault for stage 2a (`rx_oversize`), stage 5b (`rx_bad_frag`), stage 8a
  (`rx_not_fragmentable`) or the v0.6 §11.2 rule that **a single frame never touches
  reassembly state** — the last is the one worth adding, since the defect it fixes is
  precisely a node's periodic `STATUS` destroying that same node's in-progress
  fragmented `CONFIG_ACK` on *this* node. **§14.1 is now a normative counter registry
  with a wire-code column**, and the bridge's published counter names must come from it
  rather than from §10.5's table. Cross-document links repaired for the `docs/`
  reorganization.
- **v0.4** — Adds the concrete radio pin maps needed to start firmware development.
  **New §10.8.1**: the `RadioPins` struct with populated Heltec and XIAO+Wio profiles, a
  three-column table mapping each Wio pad to its XIAO GPIO and its StamPLC GPIO under
  `gatelink-expansion-board` rev 0.3, provenance notes for the derived values, and the
  rule that profiles reach the driver by construction rather than by preprocessor.
  **§3.1** now points at §10.8.1 as the single home for all three pin maps rather than
  implying the bridge's live elsewhere. **§2.3.1 finding 1 revised**: the carrier already
  allocates `RF_SW` to Bus 15 / pad D5, so the StamPLC GPIO cost is absorbed and B1b's
  remaining question narrows to whether the line is functionally required and what holding
  it high costs in sleep current. **§2.3.1 finding 2 sharpened**: GateLink's module is
  already in hand and the simnode's arrives in a separate kit, so the variant check is now
  a continuity comparison between two boards on the bench, and its failure mode is a
  silently invalid §10.8.1 rather than a carrier re-spin.

- **v0.3** — Folds in the Seeed XIAO ESP32S3 + Wio-SX1262 evaluation board and the
  decisions needed to start firmware development. **New §2.3**: the XIAO+Wio as a
  target-radio simnode, validating the RadioLib configuration, radiated power, sensitivity
  and current draw of the *actual* module GateLink will carry — and **correcting §9.1**,
  which claimed range results transfer unchanged. Parameter selection transfers; link
  margin does not, because it depends on the specific module's TX power and RX
  sensitivity. **New §2.3.1** records two findings to confirm on arrival: the Wio-SX1262
  appears to require a host-driven RXEN-style line in addition to DIO2-as-RF-switch —
  contradicting the earlier assumption and costing a GPIO in the StamPLC pin budget — and
  Wio-SX1262 variants are documented as not pin-compatible, so the exact part number must
  be recorded and re-ordered. Neither is resolvable from documentation, since Seeed has
  not published a module schematic. **Naming corrected**: `win-sx1262` → **Wio-SX1262**.
  **§2.1 revised**: the XIAO supplies the second transmitter for contention testing, so a
  third Heltec is now a cold-spare argument only, not a coverage requirement. **New
  §10.8**: hardware profiles (`simnode-heltec`, `simnode-xiao-wio`) as build environments
  over one firmware, with roles assigned so the identity impersonating GateLink runs
  GateLink's radio — and noting this is itself a free test of **R-4.1b**. **B1 split into
  B1a** (Heltec pair, starts immediately) **and B1b** (Wio-SX1262 confirming pass), so the
  XIAO's delivery gates only confirmation, not parameter selection. **New §11**,
  development environment and workflow: build order; `lran-rangetest` as a
  dependency-free target so RF work can begin today; the recommendation to develop against
  the dev HA VM and a dev broker until B6, with HA's permanent `unique_id` registry and
  retained discovery configs as the specific reasons; `secrets.h` handling; branch-per-
  milestone with Claude Code driving PRs; and the four-file `CLAUDE.md` scheme with the
  root file authoritative. **§5.4 rewritten** for separate PlatformIO projects per
  firmware with shared code via `lib_extra_dirs`, and **§5.1** gains the host test and
  tooling rows. `/lib/lran-protocol/` now has its own owning document,
  [`LRAN-Protocol-Library-Implementation-Plan`](../shared/LRAN-Protocol-Library-Implementation-Plan.md),
  which gates B0 at P6 and B2 at P7. **No change to bridge design.**
- **v0.2** — Rebased on [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md)
  **v0.3** and expanded to define the bench fleet as buildable firmware rather than a
  named idea. **New §10, `simnode` firmware:** scope, four behaviour roles
  (`ROLE_RANGE`, `ROLE_HEALTH`, `ROLE_GATELINK`, `ROLE_FAULT`), a multi-identity model
  putting up to four logical nodes on one board, a serial control console so no scenario
  requires a reflash, a 21-entry fault catalogue mapped stage-by-stage onto Protocol Spec
  §14, and a module map. **New §5.4, repository layout for the bench fleet**, adding
  `/firmware/simnode/`, `/lib/lran-sim/` and `/tools/simctl/`, with the rule that
  malformed frames are produced by post-processing real ones rather than by a second
  serializer. **New §4.2a**, the `simnode_diag_enable` publication gate implementing
  Protocol Spec §16.6 — gated on publication, never on reception, so a bench node
  exercises the same receive path as a real one. **§2.1 rewritten** to answer the board-
  count question: two boards develop everything, a third is required for genuine RF
  contention and doubles as the cold spare for the property's single point of failure; a
  fourth is explicitly *not* recommended, because a Heltec cannot validate GateLink's
  `Wio-SX1262` carrier bring-up. **New §2.2**, bench RF hygiene — never key up without an
  antenna, and attenuate or reduce power for desk work. **New milestone B0** (simnode
  bring-up) placed before B3, so a B3 failure is not ambiguous between the instrument and
  the bridge; **B3 expanded** to four simultaneous logical identities, the full fault
  catalogue run from a committed script, and the W9 `PING` cases. **New §9.3** naming the
  blind spot created by simnode and the bridge sharing one codec, and making the committed
  test vectors (**W4**) a prerequisite for B0 rather than a parallel task. §7.1 gains
  coverage rows for the discard ladder, `UNKNOWN_HDR_EXT`, the publication gate and
  contention; §7.2 gains the note that the catalogue entries most likely to be skipped by
  hand are the ones whose correct result is that nothing happens. **Recorded but not
  injectable:** §14 stage 1 (PHY CRC) cannot be produced by a transmitter and must be
  observed at the far edge of the B1 range walk. **No change to bridge design.**
- **v0.1** — Initial release. Extracted from `lran-prd-v0_8` §6.1, §6.2, §6.6.3–6.6.5 and
  the bridge-relevant parts of §3.1, §6.4 and §14. All requirements moved to
  [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md) and referenced by identifier; all
  frame and topic detail replaced by references to
  [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) v0.2. **Added:** §4.2,
  the per-node registry as an explicit data structure with the test for whether the
  abstraction has leaked — v0.8 described per-node state across several sections but never
  as one table; §5.2, task structure with the rule that `lora_task` never blocks on the
  network, which v0.8 implied but did not state; §5.3, module map; §6.2, the command retry
  state machine written out, including the same-`seq` rule whose consequence is a second
  relay pulse at the gate; §6.3, publication policy as implementable rules including a
  heartbeat republish interval that v0.8 did not have; §8, seven milestones with
  acceptance criteria; §9.1, the reasoning behind keeping the range test host-independent.
  **No design change.**
