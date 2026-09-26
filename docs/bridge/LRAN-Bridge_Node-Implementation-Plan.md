# LRAN Bridge Node Implementation Plan

**Document:** `LRAN-Bridge_Node-Implementation-Plan`
**Version:** 0.70
**Node:** Bridge Node (`lran-bridge`), node ID `0x00`
**Firmware targets:** `lran-bridge`, `lran-simnode` (§10), `lran-rangetest` (§11.2)
**Status:** Ready for build. No blocking measurements.
**Requirements source:** [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md) v0.17
**Binding protocol:** [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) **v0.16**
**Shared codec:** [`LRAN-Protocol-Library-Implementation-Plan`](../shared/LRAN-Protocol-Library-Implementation-Plan.md) v0.22 — **built first, gates this node**
**Decision status:** [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md)
**Last updated:** 2026-09-26

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
| 2+ | **915 MHz antennas** | One per board. **The bridge uses the range test's own 3.0 dBi 19 cm stick** (Bridge PRD R-4.3a.1) — the part B1a and B1b measured through, and the gain D1's ceiling is computed against. Bench boards need one each regardless of type |
| 2+ | USB-C supplies and cables | Mains, indoors |
| *1* | *Extension cable / remote antenna mount* | *Only if siting requires the antenna away from the board* |
| *1* | *SMA attenuator, 20–30 dB* | *Bench hygiene — see the warning in §2.2* |

**No enclosure, no level shifting, no regulator, no carrier.** The bridge's BOM is the
board, an antenna and a power supply.

### 2.1 How many boards — two Heltecs and the XIAO cover every test

**Two boards develop everything except real RF contention, and the XIAO + Wio-SX1262
closes that gap (§2.3). A third Heltec is a cold spare, not a test-coverage
requirement.**

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

### 2.2 Bench RF hygiene — one hazard, one data-quality trap

**Never transmit without an antenna attached.** An unterminated SX1262 PA reflects its
own output power back into the final stage. On a board that has been keyed up bare, the
failure is usually not immediate or total — it presents later as degraded TX power and a
link that is inexplicably worse than the range test predicted. Attach antennas before
first flash, not before first test.

**Attenuate, or separate, for bench work — but D33 has made this much less dangerous
than it was.** This paragraph was written against the SX1262's `+22 dBm` maximum, and
**no configuration in this project transmits there.** Envelope A, the plan of record,
caps the fitted 3.0 dBi antenna at **−4 dBm conducted**; Envelope B's ceiling is the
modules' own tested powers, 19.6 dBm (Wio) and 13.9 dBm (Heltec), and a Heltec driven at
+22 dBm is roughly 8 dB outside its own grant (`LRAN-M21-FCC-Grant-Findings` §6, §2).

At +22 dBm, two boards a foot apart put roughly **−20 dBm** into a receiver designed to
work at −120 dBm: front-end saturation, meaningless RSSI, packet errors that look like a
protocol bug, and at sustained power a real risk to the LNA. Apply that same ~42 dB of
separation loss to the **−4 dBm** ceiling and the figure is around **−46 dBm** — still far
above the design point, so RSSI read at desk range remains a relative number rather than a
measurement, but no longer anywhere near the damage region. **Fit a 20–30 dB SMA
attenuator when the RSSI numbers themselves matter**; reach for it as a data-quality tool
now, not as protection.

**The working point is fixed, and it is the D33 ceiling.** **D1 closed 2026-09-10**:
**917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted** with the fitted 3.0 dBi antenna,
under §15.249 Envelope A. Protocol Spec §12.1 states them and Decision Register §3.4
records why. Two consequences for this build: **`backoff_max_ms` defaults to 1500** rather
than 500, because a maximum `PING` at SF9 runs 1107 ms (§12.3), and **the PHY parameters
change at runtime only through spec §12.4's commit-and-revert** (**D56**, v0.13). A node
that boots on the wrong channel is a walk to the gate with a laptop, and the revert window
exists for that case. **BF-33 built it, and it ran on the bench on 2026-09-24**; the
injected radio config beside the pin map supplies only the compiled defaults. **Spec v0.14's D59
makes the change a fleet operation**: Home Assistant sets the PHY on the bridge's topic
alone, and no node commits until the bridge has heard every node on the new settings
(spec §12.4.1).

**"Full power" is the D33 ceiling, not the driver's maximum.** RadioLib accepts −9 to
+22 dBm and the range test firmware clamps into the permitted envelope
(`clamp_conducted()`, and `tools/rangetest/eirp_check.py` verifies it ran). Bench work may
sit at −9 dBm, the SX1262's hard floor; **restore the D33 ceiling before a range test**,
and **record conducted power in dBm rather than a RadioLib power index** — the Heltec and
Wio certified powers differ by about 6 dB, so an index does not carry between them (root
`CLAUDE.md` rule 10, **M6**). A range test run at bench power is a range test that has to
be repeated.

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
(**D26**, **D27**). A ~118 mA step at +22 dBm through a perfboard regulator is precisely
the thing that works on a bench supply and browns out on the carrier — **though D33 has
taken the worst case off the table**, since the node transmits at the Envelope A ceiling
rather than at the driver's maximum, and the step scales with it. **The XIAO validates the
module; only the carrier validates the carrier.**

> **GateLink's own documents carried the same figure and have been corrected**
> (GateLink Implementation Plan **v0.6**): §3.4 sized the carrier LDO against *"~120 mA
> peak SX1262 TX at +22 dBm"* and milestone **M0** accepted on *"LDO holds ≥3.2 V through
> SX1262 TX at +22 dBm"*. Both now work from the powers D33 permits: the rail is sized
> against the Wio's tested 19.6 dBm as the worst permitted case, and **M0 accepts at the
> −4 dBm operating point**. Nothing in that node's rail, part or budget decisions moved;
> every one of them gained headroom.

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

##### The two findings as they were written, superseded 2026-09-05

**Everything from here to the end of §2.3.1 is the open-question text the note above
closed.** It is kept verbatim — its present tense, its imperatives and its "on arrival"
instructions all describe what was true before the boards were assembled — because the
reasoning is what made the measurement worth taking, and rewriting a dated record into
today destroys the thing that made it useful (root `CLAUDE.md`). **Read the note above
for what is true now; read this for why it was asked.** Both sub-questions it raises are
answered there except finding 1's sleep-current cost, which is still open and still
belongs to B1b.

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

- GateLink and WellLink are at similar distances — **~87 m and ~100 m**, measured, not
  the ~500 ft estimated before either was walked to — on **different bearings**.
- **The antenna is settled and the position is not.** The bridge keeps the **3.0 dBi
  19 cm stick the range test ran on** (Bridge PRD R-4.3a.1) — omnidirectional, and already
  the part behind every measured margin. **What is left here is where the board goes.**
- **Range-test both bearings before committing to a location** (**M6**, **V-B1**). Done, and
  B1b's initiator sat indoors at the intended location — evidence for that position, not a
  commitment to it.
- Record the chosen position and the measured RSSI/SNR on both bearings in
  `/docs/bridge/engineering-log.md`, so a later "why is the well link marginal?" has a
  baseline to compare against. **Record the antenna gain with it** even though it is not
  changing: it is a term in the EIRP arithmetic, not a note about a part.

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
| `cmd_seq` | maintained | Reset to `1` on learning a new `ctx_id`, including one a context roll produced (spec §10.6) |
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

#### 4.2.1 What BF-15 built, 2026-09-14

**The registry is `registry.{h,cpp}`, Arduino-free and covered by `test_registry`.**
`registry_runtime.{h,cpp}` adds the two things the host cannot run: mbedTLS and a FreeRTOS
mutex. `main.cpp` calls `registry_begin()` before `start_tasks()`, which derives every key
and hands `lora_task` its `PeerKeys` and `IMac`.

| Choice | Why |
|---|---|
| **The table is `kNodeTable` in `registry.h`**, six rows: `0x01`, `0x02`, `0xF0`–`0xF3` | "Loaded from build configuration" needs no secret, so the table is code. A `static_assert` refuses a duplicate, `0x00`, `0xFF`, or more rows than §11.3's reassembly slots |
| **WellLink is provisioned** | Spec 5.3 reserves the address and its key derives today, so commissioning a WellLink is a node flash, not a bridge change |
| **An entry has two halves.** What a node *is* (id, type, key, `is_bench`) is written once at load and read lock-free, `lora_task` included. What the bridge has *learned* is written under a mutex, never from `lora_task` | `lora_task` needs keys on every frame and must never wait. Nothing writes the first half after the tasks start, so no lock is needed there |
| **Keys are checked against the W4 vectors** | A registry that fed HKDF the wrong address byte would agree with itself and with no node. `test_registry` compares every row's key with `tools/vectors`' independently generated one |
| **`app_task` records each reception**: `last_seen`, RSSI, SNR, `proto_ver`, and the §10.1 `ctx_id`, resetting `cmd_seq` to 1 on a new one (§10.2) | Spec 10.1 says the bridge learns a context from any frame. A zero `ctx_id` is not adopted |
| **The ladder refuses a source the registry does not know**, after stage 9 and before stage 10 | §11.3 sizes reassembly per provisioned node, so an unprovisioned transmitter must not take a slot. **Spec v0.12 gives this stage 9a and the counter `rx_unknown_src`**, inside `rx_dropped` — see below |

**What BF-15 does not do:** advance `cmd_seq` (BF-18), count `missed_polls` (BF-17) or act
on them (BF-20), downgrade on `proto_ver` (BF-22), take `poll_interval_s` from Home
Assistant (BF-23), or gate bench publication (BF-26).

> **The gap this raised is closed, and the code follows the specification.** A `STATUS`
> carries no MAC, so a frame from an address no row provisions passed all ten stages §14
> defined through v0.11. Root rule 4 wants every discard to have a named counter, a
> `Status` value and a §14 stage, and this one had only the first. **Spec v0.12 adds stage
> 9a**; **BF-15a implemented it on 2026-09-16**: `lran::Status::UnknownSrc`, the counter
> `rx_unknown_src` in `lran::Counters` and `kCounterRegistry`, summed into `rx_dropped`,
> and never answered (§14.2). The registry is **22** rows.
>
> **The bridge-local `unregistered_src` is gone**, along with the third output of
> `lora_diag_snapshot()` and `diag_rx_json()`'s second argument. The name BF-15 chose was
> non-`rx_` precisely so it would not squat whatever the specification picked, and it did
> not have to be renamed in place — it was replaced.

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

#### 4.2a.1 What BF-26 built, 2026-09-23

**The flag is read from `/lib/lran-config/` and reaches three gates**: bench availability
and bench diagnostics on `sched_task`, and bench discovery on `mqtt_task`. Before this, all
three read a compile-time `false`. Host-tested, and **confirmed on air 2026-09-23**; the
engineering log's *BF-26 on air* entry has the run.

| Choice | Why |
|---|---|
| **The flag rides BF-23's lever board** | It is not timing, but it travels the same way: from `mqtt_task`'s store to `sched_task`, which owns the gates for availability and diagnostics. A second carrier would need a second generation counter to get right. `mqtt_task` owns discovery and reads the value it publishes |
| **Switched on, everything goes out at once** | Every known node's availability, the bench nodes' diagnostics and the whole discovery set are published on the next pass, not a `diag_interval_s` or a broker reconnect later. The discovery set restarts from the top, which re-sends the production configs too. That costs about a second, once per switch, and keeps one path |
| **Switched off, one `offline` per bench node, then nothing** | The table above has the bridge publish `offline` when the flag clears, and spec §16.6 publishes nothing else while it is clear. A bench node the bridge has not heard since boot gets nothing, because the bridge has no state for it to withdraw |
| **Every bench entity is `entity_category: diagnostic`**, its buttons included | Spec §16.6's third axis. `command_allowed()` gives a simnode every command (§6.2), so without this a simnode's Open, Close and Hold buttons would reach a dashboard as ordinary controls |

**The flag gates only the two topics §16.6 names, and it gates nothing else.** A simnode's
`config/state`, `config/ack` and `cmd/ack` are published whichever way it is set, as they
were before BF-26. Spec v0.15 settles that this is right: §16.6 publishes a bench node's
answers whatever the flag says (**D65**), because each answers a request an operator made.

**An `applied_not_persisted` answer left one gap until 2026-09-26.** If the flag is set but
not persisted and the bridge reboots, it comes back with the flag clear, and the `online`
it published before the reboot stayed retained. §6.7.8 closes it: NVS records which bench
topics hold a retained `online`, and the boot withdraws each one while the flag is clear.

### 4.3 MQTT

Topic grammar and retention rules are in Protocol Spec §16.

**Library selection (D5).** Define a thin **`MqttTransport` interface and implement it
first against PubSubClient.** **BF-37 took the designated fallback on 2026-09-25**, because
PubSubClient publishes at QoS 0 only (§4.3.3). Bridge traffic is modest — a poll per node every 1–5
minutes plus occasional commands and events — so throughput and QoS 2 are irrelevant.
What matters is reliable reconnect, LWT, and publishing discovery-config JSON.

| Option | License | Assessment |
|---|---|---|
| **PubSubClient** | MIT | Tiny, synchronous, extremely stable, ubiquitous. **Gotcha: default max packet 256 bytes — Discovery configs exceed this and fail confusingly.** Set `MQTT_MAX_PACKET_SIZE` ≥ **1024** |
| **espMqttClient** | MIT | Actively maintained, sync and async variants, large payloads, QoS 0/1/2. **Designated fallback** |
| AsyncMqttClient | MIT | Effectively unmaintained. Avoid for new work |
| esp-mqtt (IDF native) | Apache-2.0 | Most robust reconnect and TLS, but pulls the design toward IDF. Reserve for a later migration |

> The `MQTT_MAX_PACKET_SIZE` default is the single most likely early time-sink on this
> node: discovery configs do not appear, with no error that points at the cause.
> **Set it in the build flags on day one.**

#### 4.3.1 What BF-12 fixed, 2026-09-10

`firmware/bridge/src/` is the authority; this records the choices §4.3 left open.

- **Reconnect: capped exponential, 1 s doubling to 30 s, no jitter.** One bridge, so
  lockstep avoidance buys nothing and a recognizable sequence in a log buys a lot. The
  cap is shorter than the fleet's own poll interval, so a recovery is noticed within
  one cycle. **WiFi and the broker back off independently** — a flapping AP must not
  also spend the broker's retries.
- **Two Arduino-ESP32 defaults are off.** `WiFi.persistent(false)`, because an NVS
  credential cache produces a bridge on a network the build no longer names; and
  `WiFi.setAutoReconnect(false)`, because the cadence belongs in one tested place.
- **Keepalive 30 s, socket timeout 5 s.** PubSubClient's 15 s default is shorter than
  this bridge's traffic pattern, which produces reconnects that look like faults.
- **`setBufferSize()` is called as well as the build flag.** The compile-time macro does
  not reach PubSubClient when it is built as a separate archive, and the failure mode is
  the one this section already warns about: discovery configs that never appear.
- **LWT on `lran/bridge/availability`, retained, payload `offline`** (spec §16.5), with
  `online` republished on **every** connect — a broker restart loses retained state and
  the bridge is the only thing that can put its own back.
- **Spec §16.3 is enforced on the publish path, twice.** A retained publication on an
  `lran/<node>/event/` topic is refused when the message is built and again before the
  wire, matched on the topic *segment*. **Refused rather than corrected:** clearing the
  flag silently leaves a caller believing something untrue.
- **Nothing is truncated.** An oversized topic or payload is refused and counted.
- **A failed publish loses that message**, counted, and leaves the rest queued.
  Re-queueing would reorder it behind newer state for the same entity. **An event stays at
  the head of its own queue instead** (§4.3.3).

#### 4.3.3 What BF-37 and BF-38 changed, 2026-09-25

**espMqttClient 1.7.3 replaced PubSubClient behind `MqttTransport`** (`mqtt_esp.{h,cpp}`),
and events now reach the broker at QoS 1. BF-12's choices above carry over: the reconnect
cadence, the 30 s keepalive, the LWT and both spec §16.3 checks. On 2026-09-25 a subscriber
at QoS 1 received the bridge's events at QoS 1. Two events raised while the broker
restarted reached it after the reconnect.

| Choice | Why |
|---|---|
| **No internal task.** `mqtt_task` calls the library's `loop()` | The library runs where PubSubClient ran, at §5.2.1's priority and core. The inbound sink still runs on `mqtt_task` |
| **A static packet pool** (`EMC_USE_MEMPOOL`, 48 blocks of 512 bytes) | Root rule 3. By default the library takes each outgoing packet from the heap |
| **`mqtt_task` hands work over only while fewer than `kMaxPendingPublishes` (8) are unfinished** | The library queues a publish and writes it in `loop()`, where PubSubClient wrote at once. Without the gate the drain would move the whole publish queue into the pool |
| **`connect_once()` waits up to 5 s for the CONNACK** | The seam promises one attempt that reports its outcome. The library connects over several `loop()` passes. PubSubClient's 5 s socket timeout bounded the same wait |
| **A publish while not connected is refused** (`EMC_ALLOW_NOT_CONNECTED_PUBLISH=0`) | The bridge's own queues ride out an outage. The pool is not sized to |
| **An accepted QoS 1 event is the library's until the broker acknowledges it** | A clean session, so the broker keeps nothing across a reconnect. The library keeps its unacknowledged publications and sends them again after the CONNACK |
| **`InboundAssembler` rebuilds an inbound payload from its pieces** | The library delivers a payload in pieces cut at its read buffer. `test_net` covers the cut points. `set_inbound()` is no longer a concession to one library: the callback captures the transport |
| **`MQTT_MAX_PACKET_SIZE` is gone** | The library sizes each packet to its contents |

**Events have a queue of their own (BF-38)**, depth 8, and `mqtt_task` drains it before the
state queue. Until then, state could fill all 32 publish slots during a broker outage and
the queue then refused the next event. An event is peeked, published and only then
removed, which replaces BF-25's held-event slot. **State stays DropNewest**: a state
document the queue refuses is not recorded as published, so the node's next frame replaces
it. Dropping the oldest instead would lose a change the policy had already recorded. The
event queue reports `q_event_dropped` and `q_event_high_water` with the other queues.

**The cost is about 42 KB of static RAM**: 197,328 bytes on `main` before BF-37 and 239,260
after, for the event queue and the pool. After a broker reconnect on 2026-09-25 the bridge
reported 72,580 bytes of heap free and 60,188 at the lowest. It prints both on every
connect.

**AsyncTCP (LGPL-3.0) is compiled and not linked.** espMqttClient lists it as a dependency
on every ESP32 build. `THIRD_PARTY_NOTICES.md` gives the linker-map check that would show
it linked.

#### 4.3.2 What BF-19 publishes, 2026-09-14

Every §14.1 counter reaches the broker under its normative name. `diag_json.{h,cpp}` builds
the documents, host-tested; `sched_task` publishes them, retained, every
`diag_publish_interval_s` and on the tick after each broker connect. Spec §16.2 names
`lran/<node>/diag/state` and defines no payload, so these documents are the bridge's choice,
as `lran/bridge/version`'s was (BF-13).

| Topic | Carries |
|---|---|
| `lran/bridge/diag/state` | Every §14.1 counter in `kCounterRegistry` order — **22 rows since BF-15a** — then `rx_dropped` (the codec's sum) and `rx_frames`. Spec §16.2.1 fixes this payload's shape. **`unregistered_src` is no longer published**: it is `rx_unknown_src`, a registry row, inside `rx_dropped` |
| `lran/bridge/diag/radio/state` | `tx_frames`, `cad_backoffs`, the driver's `LoraStats`, and each queue's `dropped` and `high_water` |
| `lran/<node>/diag/state` | `rssi_dbm`, `snr_db`, `last_seen_s` (an age), `missed_polls`, `proto_ver`. Watched nodes only; a bench node only with `simnode_diag_enable` (§4.2a) |

| Rule | Value |
|---|---|
| **Discard counters are the bridge's, not a node's** (decided with the operator) | Most discards happen before the MAC check, where `src` may be corrupt or forged. **Spec v0.12's §14.1 now says this normatively**: a counter raised before stage 9 is the receiver's own, and only stage 9 onward may be attributed per node |
| Sentinels | `null`, never a number (root rule 6) |
| `diag_publish_interval_s` | **60**, runtime-settable. No document gave a cadence; one default poll interval |
| Consistency | `lora_task` copies its counters under a spinlock once a second; readers take that copy (`lora_diag_snapshot`), so `rx_dropped` always agrees with the counters beside it |
| `kMaxPayloadLen` | **768**, from 512: the §14.1 document is 681 bytes with every counter at `UINT32_MAX`. The publish queue grows from ~19 KB to ~28 KB |
| A refused publication | Not retried; the next interval carries newer numbers |
| `ERROR` replies (spec §14) | **Built 2026-09-16 (BF-19a), and confirmed on air the same day**: one reply per §14 stage that names one, read at the simnode. Spec §14.2: registered sources only, rate-limited by `error_min_interval_ms` (default 1000, runtime-settable), `src` the bridge, `ctx_id` `0`, `ref_seq` the offending frame's. A frame from an unknown source is discarded at stage 9a and never answered. `error_reply.{h,cpp}` decides; `lora_task` builds and queues, so a reply takes its turn at media access like any other frame. **`BAD_CRC` and `BAD_VERSION` stay optional and unbuilt** — a frame that failed CRC has a `src` that cannot be trusted to name its sender, and an unreadable `ver` is **BF-22**'s to answer |
| Replies the rate limit withheld | `errors_suppressed`, on `lran/bridge/diag/radio/state` with the queue statistics. **Not a §14.1 counter and not a discard**: the frame that provoked it is already counted by the stage that discarded it |

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

#### 4.4.1 What BF-23 built, 2026-09-17

`discovery.{h,cpp}` builds the configs; `mqtt_task` publishes them, retained. Six choices
this section left open, and the reasoning for each.

| Choice | Why |
|---|---|
| **A boot and a reconnect take the same path** | `on_mqtt_connected()` restarts a cursor and the task loop drains it. R-3.3b's reconnect case is not a branch that can be got wrong, because it is the only branch — there is no "first time" flag |
| **Drained a few per task iteration, published directly** | The set is a few dozen documents; publishing them back to back would hold `mqtt_task` inside PubSubClient without a `loop()` between them, on a socket that has just reconnected. Direct rather than through `g_publish_queue`, which is sized for state: a reconnect would otherwise put a few dozen configs in front of every node's current reading |
| **Abbreviated discovery keys and a `~` base topic** | `kMaxPayloadLen` is 768 and the long forms put a node's config within a hundred bytes of it. Growing that buffer costs RAM in every publish queue slot, and §4.3.2 grew it once already |
| **The buttons come from `command_allowed()`** | The same capability filter the command path uses, so a button cannot exist for a command the bridge would refuse to send. WellLink gets no gate buttons and discovery never learns what a gate is (**BG-2**) |
| **No `reboot` button, and none for the argument-carrying commands** | Spec §8.1 guards `REBOOT` with `0xA5` in `arg` precisely so it cannot be issued by accident, and a dashboard button is that accident. `set_debug_mode` and its neighbours carry a bitmask in `arg2`, which a button cannot express. Both stay reachable from the topic |
| **A bench node produces no discovery until BF-26** | Spec §16.6 gates publication and discovery is publication. HA's registry remembers a `unique_id` forever and a retained config survives a reflash, so four simnode devices whose entities could never update is a cost paid once and kept. The gate is `bench_publication_allowed()`, already shared with BF-20 |

**The itemised §14.1 counters are deliberately not entities.** Twenty-two rows per node in
HA's registry, paid forever, for numbers read during a bench session; they stay readable on
`lran/bridge/diag/state`. **R-3.5e** reasons the same way about the MPPT's registers. The
two totals and `tx_frames` are entities.

**The examples under `/ha/` are generated from `discovery.cpp` itself**
(`tools/ha/dump_discovery.cpp`), and `tools/checks/ha_examples.py` fails CI when the
committed files stop matching. This section asked for examples; an example nothing reads
drifts from the code the first time a table row changes, silently, in the one artifact
somebody reaches for when Home Assistant is not cooperating.

**What BF-23 does not do: the runtime timing levers.** Every `TODO(BF-23)` on a timing
constant is still there — `g_diag_interval_s`, `poll_interval_s`, `reply_timeout_ms`,
`missed_poll_threshold`, `error_min_interval_ms` and the media-access config. Root rule 8
wants them settable and **V-B12's saturated arm needs the first of them**, but the path
from Home Assistant to a stored value is `lran/<node>/config/set`, whose payload spec
§16.2.1 leaves undefined until the work is scheduled, and `/lib/lran-config/`, which does
not exist and has no task. **That gap is real and predates this task**; it is the same one
that deferred BF-26 on 2026-09-14.

> **Closed on paper 2026-09-19.** Spec v0.13 §16.7 defines the `config/*` payloads
> (D43–D54), Protocol Library Plan §4 lists the bridge's parameters, and **BF-32** owns the
> build. Every `TODO(BF-23)` above becomes a read of that table once BF-32 lands.

#### 4.4.2 BF-23's lever half, built 2026-09-23

**Every bridge row in `/lib/lran-config/`'s table now reaches the code it configures**,
except `simnode_diag_enable`, which gates publication and belongs to BF-26. Before this,
`ConfigStore` held each row's effective value, and no code outside the tests read one, so
every lever ran its compile-time default whatever Home Assistant set. Host-tested, and
**confirmed on air 2026-09-23**. The engineering log's second entry that day has the run.

`levers.{h,cpp}` carries the values. `levers_from()` reads a `ConfigStore` into a plain
`Levers` struct, and `LeverBoard` carries that struct from the task that wrote the store
to the tasks that own each consumer.

| Row | Applied by | To |
|---|---|---|
| `diag_interval_s` | `sched_task` | `g_diag_interval_s` |
| `missed_poll_threshold` | `sched_task` | `AvailabilityWatchdog::set_threshold()` |
| `poll_reply_timeout_ms` | `sched_task` | `PollScheduler::set_reply_timeout_ms()` |
| `command_ack_timeout_ms`, `cmd_retries` | `sched_task` | `CommandPath::set_ack_timeout_ms()`, `set_retries()` |
| `config_readback_timeout_ms` | `sched_task` | `ConfigPath::set_readback_timeout_ms()` |
| `config_ack_timeout_ms` | `sched_task` | `ConfigPath::set_ack_timeout_ms()` |
| `poll_interval_s`, per node | `sched_task` | `NodeState::poll_interval_s` through `registry_set_poll_interval()`, and `PollScheduler::retime()` |
| `cad_retries`, `backoff_max_ms`, `frag_reassembly_timeout_ms` | `lora_task` | `lora_configure()` |
| `error_min_interval_ms` | `lora_task` | `lora_configure_errors()` |

Five choices, and the reasoning for each.

| Choice | Why |
|---|---|
| **A lock-free board rather than a read of the store** | `mqtt_task` writes `ConfigStore`. The consumers belong to `sched_task` and `lora_task`, and `lora_task` must never wait on a lock (`tools/checks/lora_task_never_blocks.py`). `mqtt_task` reads its own store and publishes the result as atomics under a generation counter. A reader that catches a publish mid-copy sees an odd or moved generation, takes nothing, and takes the new values on its next pass. It never applies a mix of two publishes. **One writer**: `setup()` before the tasks start, then `mqtt_task` alone |
| **Published after the NVS restore, and after every set that changes a bridge-held value** | `config_begin()` publishes once the store holds what NVS restored, so each task's first pass applies the saved values. Published before the restore, a reboot would run the defaults until the next set, which is the silent revert this task had to rule out. After a set, `handle_config_set()` publishes as soon as the store holds the value, *before* a node half's job is queued. That function returns early once the job is queued, so a `poll_interval_s` riding with a node's own rows would otherwise reach the store and never its consumer |
| **Applied on the owning task** | `lora_configure()` writes state `lora_service()` reads without a lock, so `lora_task` calls it itself when the board changes. The other consumers are `sched_task`'s. `sched_levers()` runs first on each tick, holds the scheduler's lock across its own calls only, and keeps its two `Levers` in static storage rather than on the stack. `sched_task` has the deepest stack in this firmware |
| **A changed poll interval counts from the last poll** | `on_sent()` fixes a node's due time when its poll goes out. Without `retime()`, an operator who drops 3600 s to 60 s would see no poll for up to an hour. Only the nodes whose interval moved are retimed, so a set of another lever leaves the schedule alone |
| **`cmd_retries` changes a count, never a `seq`** | Root rule 2. A command in flight keeps the `seq` its first attempt took. A lower count ends its retries sooner, and nothing else moves |

**`test_levers` checks that every consumer's compile-time default equals its row's
default.** A consumer's own constant is still what runs until `sched_task` or `lora_task`
first takes the board. If the two differ, the bridge runs one value briefly and another
after, and `config/state` shows only the second.

**What the host suite cannot show.** It cannot show that the tasks apply what they take,
or that a value survives a reboot. Both need the board. `sched_levers()` prints a
`levers: gen N` line each time it applies a generation. That line, and the diagnostic
publication's spacing after a `diag_interval_s` set, are the bench evidence to look for.

#### 4.4.3 BF-35's controls, built 2026-09-25

**Home Assistant now shows every configuration row as an entity on the device whose topic
sets it.** The rows come from `/lib/lran-config/`'s table (D44), so a row added there gains
its entity without an edit to `discovery.cpp`. A control offers the table's own range and
cannot ask for a value the bridge would clamp. Host-tested, and **shown in the sandbox HA
2026-09-25**. The engineering log's entry that day has the run.

| Device | Rows | Entity |
|---|---|---|
| LoRa Bridge | The 15 global read-write rows | `number`, except `simnode_diag_enable`, a `switch` |
| LoRa Bridge | The six PHY rows (D59) | `number`, except `bandwidth_khz`, a `select` offering 125, 250 and 500 |
| Each node | `poll_interval_s`, `deployed` | `number`, `switch` |
| Each node | The four node-common read-write rows | `number` |
| Each node | The six PHY rows | Diagnostic `sensor` |

Each entity writes `{"set": {<name>: <value>}}` to its device's `config/set` (spec §16.7.2)
and reads `value_json.<name>.value` from its `config/state` (§16.7.4). A switch writes
`true` and `false`, and it reads the 1 and 0 that `config/state` reports for a bool.

The operator settled the four choices below on 2026-09-25, before anything was published.

| Choice | Why |
|---|---|
| **The table name is the `object_id` and the entity name**, under the unique_id `lran_<node>_<name>` | Spec §16.7 says so. HA derives a new entity's id from the device and entity names, so the entity reads `number.gatelink_poll_interval_s`, the key a person would publish by hand. The name can be changed in HA; the unique_id cannot |
| **The bridge's PHY rows are controls, in box mode** | The bridge's topic is the only place a PHY change can start (D59), and a failed change reverts under §12.4. Box mode means a slider cannot pass through a fleet-wide change on its way to a value. `bandwidth_khz` is a `select`, because the row's 125–500 range would take 300, which is not an SX1262 bandwidth |
| **A node's PHY rows are diagnostic sensors** | Spec §16.7.1 answers a PHY row `read_only` on a node's topic, so a control there could only fail. The sensors show whether each node holds the fleet's settings after a change |
| **`poll_interval_s` and `deployed` follow the bridge's availability** | The bridge applies them. `deployed` must be settable on a node that has never been heard (D61), which is exactly when that node reads offline. A node's own rows follow the node's availability, as R-3.3d requires |

**Every control is `ent_cat: config`**, which keeps it off a default dashboard. **A bench
node gets no table entities**, even with `simnode_diag_enable` set. Spec v0.15's §16.6 publishes a
bench node's `config/*` answers whatever the flag says (**D65**), so the specification no
longer stands in the way of bench controls. They are not built, and HA's registry never
forgets a unique_id once published. A bench row is still settable on its topic.

**`test_discovery` renders each control's command as HA would, and parses the result
through `parse_config_set()`.** A template that drifted from the parser would publish, and
the bridge would answer it `not_applied` on `config/ack`, a topic nobody watches.

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
| MQTT | **`MqttTransport` → espMqttClient** (**D5**'s fallback, BF-37) | MIT |
| JSON | ArduinoJson | MIT |
| Display | **ThingPulse SSD1306 driver** — `thingpulse/ESP8266 and ESP32 OLED driver for SSD1306 displays`, version pinned. Not U8g2; see §5.1.1 | MIT |
| HMAC / HKDF | mbedTLS via ESP-IDF | Apache-2.0 |
| OTA | ArduinoOTA or `esp_https_ota` | LGPL / Apache-2.0 |
| VE.Direct HEX register model | **`/lib/vedirect/`**, shared with GateLink | MIT |

#### 5.1.1 Why the SSD1306 driver rather than U8g2

**U8g2 was this table's original choice and no target ever used it.** Two independent
Heltec V3 implementations picked the ThingPulse driver instead: the BMS proof of concept
in `/wattcycle-reader/`, whose design note gives the reason — *U8g2 is the alternative if
you want more font control; heavier, and unnecessary here* — and `firmware/range-test/`,
which lifted its Vext bring-up sequence from that PoC and inherited the driver with it.

**The one requirement that could have argued for U8g2 has been met without it.** Range
test task R6 wants link figures readable outdoors at arm's length in sunlight;
`src/ui_oled.cpp` does that with the ThingPulse driver's 24 px font. Font control was the
stated reason to prefer U8g2, and it was not needed.

**For this node it remains a choice rather than a fact**, because no bridge firmware
exists yet. R-4.1c asks only for a glanceable "N nodes online" display and is MAY-level,
which is the lightest display requirement in the project — so the case for a heavier
library is weaker here than in either firmware that has already declined it. Consistency
across the Heltec targets is the argument; nothing forces it. **If a bridge display
requirement ever needs what U8g2 offers, change this row and say why** rather than
reaching for a second display library alongside the first.

#### 5.1.2 What BF-14 fixed, 2026-09-10

**The display is the ThingPulse driver, pinned to the range test's version**, driven only
from `ui_task`. What the page shows and how it is laid out is decided in
`status_page.cpp`, which is Arduino-free and host-tested; `ui.cpp` only draws it.

- **A dead panel is reported once and then ignored.** R-4.1c is MAY-level, and a bridge
  that stopped relaying telemetry because its status display failed would have its
  priorities backwards.
- **Unknown reads `--`, never `0`** (root rule 6). The node count is unknown until the
  registry (BF-15) and the availability watchdog (BF-20) exist, and "nodes 0" would read
  as every node down.
- **Burn-in is designed against, not hoped about.** The panel is on permanently on a node
  expected to run for years. Contrast is 96 rather than the range test's 255 — this panel
  is read indoors — and every element shifts 0–3 px on a five-minute cycle.
- **The displayed uptime comes from `esp_timer`, not `millis()`.** `millis()` wraps at
  ~49.7 days, and an uptime that returns to zero every seven weeks reads as a reboot that
  did not happen.
- **Every string is held to a per-font character budget by a host test.** The range test's
  panel truncated two strings on hardware before anyone noticed; this one's budgets caught
  two overruns before it was ever flashed.

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

#### 5.2.1 The numbers, chosen by BF-11 on 2026-09-10

§5.2's table gives bands. `firmware/bridge/src/tasks.cpp` gives numbers, and this is
where they are argued rather than merely declared. **The table there is the
authority; if these disagree, the code is what runs and this section is stale.**

| Task | Priority | Core | Stack (bytes) | Trigger |
|---|---:|---:|---:|---|
| `lora` | **6** | **1** | **8192** | queue and radio |
| `sched` | 4 | 1 | 3072 | 1000 ms |
| `mqtt` | 3 | **0** | 6144 | 100 ms + queue |
| `app` | 3 | any | 6144 | queue |
| `ota` | 2 | any | 4096 | on request |
| `ui` | 2 | any | 3072 | 500 ms |
| `log` | **1** | any | 3072 | queue |

**Stack sizes are bytes, and until BF-16 this table said words.** ESP-IDF's FreeRTOS
takes the depth in bytes, and `StackType_t` is `uint8_t` on the ESP32-S3; upstream
FreeRTOS counts words. Every size here was chosen as though it were four times larger.
BF-16 raised `lora` to 8192 because it now runs RadioLib's `begin()`, and `lora_link.cpp`
logs that task's high-water mark after bring-up. **The other six sizes are unmeasured**,
and `uxTaskGetStackHighWaterMark` is how to correct them.

**The gaps in the priority numbers are deliberate.** A task added later at "just
above `mqtt`" takes an unused number instead of forcing a renumbering of everything
above it. **`lora` is strictly highest and `log` strictly lowest**, and both are
asserted by a host test rather than left to review — a tie at the top means the frame
path can be made to wait for whatever it tied with.

**Nothing sits below priority 1**, which is where Arduino's own `loopTask` runs. A
task beneath it is starved by a `loop()` that never yields, which is the default
shape of an Arduino sketch.

**`lora` is pinned to core 1 and `mqtt` to core 0**, where the WiFi and lwIP stacks
already run. This is the structural half of the asymmetry PRD §4.4 records at the
radio level. **M22 found no PER cost from saturated WiFi on the bench** (§8.1.3). If PER
at the gate degrades with WiFi load, this pinning is one of the two levers, and antenna
separation is the other. Neither rescues a design that publishes inline.

**Queue depths: RX 8, publish 32, event 8, TX 4, log 16.** The publish queue is the deep one
because a single status frame fans out into a dozen entities and because it is what
rides out a broker reconnect. **The TX queue is shallow on purpose**: the bridge
serializes polls fleet-wide (§6.1, R-3.1d), so depth there would mean something
upstream had stopped honouring that, and a queue is the wrong place to discover it.

**A full queue drops the newest item and counts it.** Blocking is how a slow consumer
reaches back and stops `lora_task`, which §5.2 and PRD §1.3 forbid. Dropping the
oldest suits state, which is idempotent, and is wrong for events, which are not.
**BF-38 built the per-class refinement** as a separate event queue, depth 8 (§4.3.3).
Each queue carries `sent`, `dropped` and `high_water`; these are **bridge
diagnostics, not schema `0xF0`**, because §14.1 is the wire's registry and a queue
overflow has no §14 stage. The engineering log's 2026-09-10 entry records why root
rule 4 is not stretched to cover it.

**Everything above is static.** Queue storage and task stacks are fixed arrays
(root rule 3), so a creation failure is a table defect rather than a memory
condition — and `setup()` halts on one rather than running a fleet with a task
missing.

**The never-block rule has a check:** `tools/checks/lora_task_never_blocks.py` fails
if `portMAX_DELAY`, `delay()`, a WiFi or publish call, or a queue call with a
non-zero timeout appears in the code `lora_task` owns. It reads one function's text —
a tripwire on the shape of the mistake, not a proof.

#### 5.2.2 The leveled log and the watchdog, built by BF-11a and BF-11b on 2026-09-26

**`log_printf()` formats a line on the caller's stack and queues it without waiting.**
`log_task` prints it. A full queue drops the newest line and counts it as
`q_log_dropped`, the counter every queue already had. A serial write blocks its caller
while the UART buffer is full, and Arduino's `printf` takes a heap block for any line over
64 bytes; `sched_task` and `lora_task` are the two tasks that must do neither. **Their
lines moved to the queue, and no other task's did**: `mqtt_task`, `app_task` and the boot
path still print directly, by operator choice, until a reason to move them appears.

| Choice | Value | Why |
|---|---|---|
| Line length | 192 bytes | The `levers:` line reaches 182 characters with every field at its widest. `test_log` checks that it fits; 160 cut it |
| Levels | `ERROR`, `WARN`, info | An info line prints as it did before, so nothing that reads the serial log changes. The other two gain a prefix |
| Before `start_tasks()` | Straight to `Serial` | There is no queue yet and no task to protect |
| Cost | ~3.1 KB static, ~200 bytes of the caller's stack | The stack cost replaces `Serial.printf`'s 64-byte buffer. Read `sched_task`'s high-water mark on the bench |

**The task watchdog watches `sched_task` alone**, fed once at the end of each tick, so a
tick that hangs part way through is the one that starves it. Its timeout is **10 s**,
`kWatchdogTimeoutS` in `tasks.h`: ten ticks, against ESP-IDF's default of 5 s, because a
tick can wait behind the configuration lock while NVS erases a page. The value is
compile-time on purpose. Root rule 8 protects a node that cannot be reflashed, the bridge
takes OTA, and a timeout that Home Assistant could set to 1 s is a way into a reset loop.
**A watchdog that fails to arm is logged, and the bridge runs unwatched.** The boot banner
prints `Reset:` with `esp_reset_reason()`, because a watchdog reset leaves no other trace
on a bridge nobody watches over serial.

**What the watchdog does not see.** It catches a hung `sched_task`, and with it a lock
that some other task never releases. It does not catch a hung `lora_task`, `mqtt_task` or
`app_task` whose locks stay free. Making the feed conditional on those tasks' progress is
a separate decision, not taken here.

`mqtt_task` logs its own high-water mark each time the mark reaches a new low, checked
every 10 s. No single call site is its deepest, the way the configuration resolution is
`sched_task`'s.

### 5.3 Module map

```
/firmware/bridge/
  src/
    main.cpp            task creation, WiFi/MQTT init, registry load
    registry.cpp        per-node table, key derivation (§4.2.1)        [any; keys lock-free]
    registry_runtime.cpp  the instance, its mutex, mbedTLS HMAC/HKDF
    scheduler.cpp       per-node poll scheduling                      [sched_task]
    command.cpp         §6.2's command state machine, retry/backoff    [sched_task]
    lora_link.cpp       RadioLib, frame in/out, CAD, transmit          [lora_task]
    rx_ladder.cpp       spec 14 stages 1-10: decode, MAC, reassembly   [lora_task]
    (lib/lran-link)     spec 12.3 CAD, backoff, transmit regardless     [lora_task]
    radio_config.h      RadioPins and the fixed PHY (spec 12.1, 12.2)
    mqtt_transport.cpp  MqttTransport iface, inbound reassembly       [mqtt_task]
    mqtt_esp.cpp        the espMqttClient implementation (§4.3.3)      [mqtt_task]
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

#### 5.3.1 What BF-16 built, 2026-09-13

**The radio link is split three ways so that its decisions are host-tested.**
`rx_ladder.{h,cpp}` and `media_access.{h,cpp}` are Arduino-free and covered by
`test_lora`; `lora_link.cpp` only moves bytes and interrupts between them and the SX1262.
`tools/checks/lora_task_never_blocks.py` reads all three.

**`media_access` moved to `lib/lran-link/` on 2026-09-14**, with its tests, when simnode
B0 became its second user. One implementation of §12.3 serves both firmwares; the bridge
names it through `using` declarations in `lora_link.h`.

| Choice | Why |
|---|---|
| **`lora_task` runs stages 1–10 and queues a decoded header with the complete payload** | §5.2's table already said so. BF-11's `RxMessage` of raw bytes contradicted it, and made a rejected frame cost a queue slot |
| **A frame that should carry a MAC and was not verified is refused** | The codec returns `Ok` with `mac_verified = false` when it has no key. Keys arrive through `PeerKeys`, which BF-15 implements |
| **One reassembly slot per peer, eight in all** | §11.3 asks for one per peer the bridge can receive from; six identities are provisioned. A single frame never takes a slot (§11.2). Displacing a live set for capacity counts `rx_reassembly_abandoned` |
| **CAD and transmit are started, then read back against a deadline** | RadioLib's `scanChannel()` has no timeout and `transmit()` busy-waits. `lora_task`'s one wait is a bounded `ulTaskNotifyTake` on DIO1 |
| **A backoff is state; `lora_task` keeps receiving** | The busy channel is usually a node, often talking to the bridge |
| **No CAD while a valid header is less than 1500 ms old** | A CAD takes the radio out of receive and destroys the arriving frame; that case counts as a busy CAD |
| **`ArduinoHal` in static storage** | RadioLib's `SPIClass` `Module` constructor allocates its HAL on the heap |
| **The OTA verdict requires `radio_ok`** | An image whose radio never initialises is a bad image. **This is a §6.5.2 re-run trigger** |

**What BF-16 does not do:** ERROR replies (BF-19), N−1 acceptance (BF-22), slots for
registered nodes only (BF-15), timing from Home Assistant (BF-23), the raw frame log
(BF-27). **Nothing has been sent or received over the air.**

> **Spec §12.1's node-address filtering has no implementation, and none is possible in
> LoRa mode.** RadioLib 7.7.1 exposes no `setNodeAddress()` for the SX126x, and the
> datasheet says why: `AddrComp` is a **GFSK** packet parameter and the LoRa packet
> handler has no address field (SX1261/2 Rev 1.1, §13.4.6.1 against §13.4.6.2).
> **Verified 2026-09-16 as M24, and spec v0.12 withdraws the requirement** — addressing is
> §14 stage 5, in software. Raised in the engineering log's BF-16 entry.

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
    lran-sim/           malformed-frame primitives (BF-7). Its tests
                        reproduce the W4 negative vectors byte for byte
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
> malformed frames in §10.5 are needed on air, from the simnode, and must agree with the
> committed negative vectors (**W4**) on the host.
>
> **Corrected in v0.25.** This note used to say one generator served both consumers, and
> the tree above said `/tools/vectors/` shared the library. It cannot: the vector generator
> is Python, and §9.3 requires it to be written from the specification without calling the
> C++ code, because that independence is its whole value. Shared code would make the
> simnode and the vectors wrong in the same way, which is the failure this note warned
> about. The two stay independent, and `lib/lran-sim`'s host suite compares them: it
> rebuilds 18 of the 21 negative vectors from `encode()` plus a patch and requires the same
> bytes (§10.5.2).

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

#### 6.1.1 What BF-17 built, 2026-09-14

`scheduler.{h,cpp}` decides, host-tested; `sched_task` sends. The engineering log's BF-17
entry argues each choice.

| Parameter or rule | Value |
|---|---|
| `poll_reply_timeout_ms` | **10 000**, runtime-settable. A poll is outstanding until a frame from that node arrives or this window closes. Derived from spec §12.3's worst-case node backoff (7.5 s) plus an SF9 answer; this section is its first home |
| Who is polled | **A row whose `deployed` lever is set (D61), from the tick `sched_task` applies it.** Any other row, bench or production, **once any frame from it has been heard** this boot (bench rows decided with the operator 2026-09-14). `deployed` defaults to 0, and clearing it takes effect at the next restart |
| Next due | One `poll_interval_s` after the send. A zero interval is held to 1 s |
| Order | The most overdue enrolled row; a never-polled row first; ties in `kNodeTable` order |
| `POLL` | `poll_flags` bit 0, the node's learned `ctx_id` (0 until heard), no MAC, `seq` from the scheduler's own counter |
| OTA | An upload in progress holds new polls; an outstanding poll keeps `lora_task_idle()` false (R-5.3d) |
| One exchange on the air | **An outstanding poll holds every other exchange**: a command, a roll, a `CONFIG`, a HEX request and the start of a PHY change. **Each of those holds the others too**, which until 2026-09-25 a command and a `CONFIG` did not: both could be in flight to one node, each with a seq from the same command space. **No scheduled poll starts** while one of those waits for its answer, while a PHY change blocks traffic, or while a command or configuration job waits in its queue. `air_turn.h` states the rule, and `exchange_may_start()` is the only place it is checked. **A PHY change's step-6 `POLL`s go one at a time**, each held until the last is answered or its `config_ack_timeout_ms` has passed. The engineering log's *poll clash* entry, 2026-09-24, has the defect it closes |
| Reply windows | **A command's, a roll's, a `CONFIG`'s, a HEX request's and a PHY change's window opens when the frame leaves `lora_task`**, not when `sched_task` queues it, because media access can hold a frame for seconds (spec §12.3). `lora_tx_finished()` reports each ticketed frame, sent or not, and the path's `next()` waits for that report, for 10 s at most. **The scheduled poll is the exception**: its window and its answer time count from the queue, and `poll_reply_timeout_ms` above is sized for that. The engineering log's 2026-09-25 *air-timing defects* entry has the defect this closes |

#### 6.1.2 What BF-20 built, 2026-09-14

`node_availability.{h,cpp}` judges, host-tested; `sched_task` publishes, once a tick after the
scheduler. The engineering log's BF-20 entry argues each choice.

| Parameter or rule | Value |
|---|---|
| `missed_poll_threshold` | **3**, runtime-settable (PRD R-3.4b). 0 is held to 1 |
| `offline` | `missed_polls` at or above the threshold |
| `online` | Any valid frame since the previous tick: the registry's new `frames_heard` count moved |
| Unknown | Neither, since boot. **Not published**: the broker's retained value stands until the node settles it |
| Watched | The rows §6.1.1 polls: a deployed row from the tick its lever is applied, any other once heard. **The watched rows are spec §12.4.1's fleet**, so a node not deployed and not heard cannot refuse a PHY change (D61) |
| Publication | `lran/<node>/availability`, retained, QoS 0, through the publish queue. A refused publication is retried on the next tick; every judged node is published again after each broker connect |
| Bench rows | Judged and printed on the serial console. **Published only with `simnode_diag_enable`** (spec §16.6), which BF-26 builds; until then, never |
| Status page | `nodes <online>/<watched>` |
| R-3.4d | No bridge code. BF-23's discovery configs must list the bridge's LWT topic and the node's availability topic together |

### 6.2 Command path and retry

```
MQTT command topic
  -> validate against the target node's capability set
  -> refuse while the node's context roll is pending (spec 10.6, BF-34)
  -> registry: node key, ctx_id, next cmd_seq
  -> build authenticated frame, transmit
  -> await COMMAND_ACK within command_ack_timeout_ms (default 3000)
       ACK           -> publish result, done
       no ACK        -> retry with backoff, SAME seq, up to cmd_retries (default 3)
       REJECTED_CTX  -> adopt the ACK's ctx_id, reset cmd_seq to 1, then
                          0x00-0x0F or REBOOT -> publish `unconfirmed`, done (spec 10.7)
                          anything else       -> retry ONCE
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

**An actuation command or a `REBOOT` is never resynced** (Protocol Spec §10.7, **D70**).
A `REJECTED_CTX` on its retry may mean the node reset after it executed, and the resync
would then be a second relay pulse or a second reboot. The command ends with `outcome`
`unconfirmed` on `cmd/ack`, the registry adopts the node's context, and
`cmd_unconfirmed` on `lran/bridge/diag/cmd/state` counts it. The `STATUS` that follows
settles what happened: `GATE_STATE_CHANGE` for motion, `BOOT` for a reboot. `resync_may_retry()`
in `command.{h,cpp}` holds the rule, and `test_command` has three cases for it.

**After a bridge restart, each node's commands wait on its context roll** (Protocol Spec
§10.6, **D58**, Bridge PRD **R-3.1h**). A restart resets `cmd_seq`, and a node that did not
restart answers a `seq` it has cached with `DUPLICATE_CACHED` and does not run the command.
The bridge therefore polls every registered node at boot and sends `ROLL_CONTEXT` when each
is first heard, and it refuses commands and `CONFIG` to that node until the roll completes.
**BF-34** built it on 2026-09-23, and §6.2.2 records the choices.

**A retry that reaches the node while it is still executing gets no answer** (Protocol
Spec §9.4, v0.11). The node counts it in `rx_dup_command` and stays silent, so the retry
takes the `no ACK` branch above, and the next retry receives the cached result. No new
branch is needed. **The consequence to know:** if one execution outlasts every retry —
`cmd_retries` × `command_ack_timeout_ms` plus backoff, roughly ten seconds at the
defaults — the bridge publishes failure for a command that ran. The node's
`rx_dup_command` is how that is told apart from a lost link.

#### 6.2.1 What BF-18 built, 2026-09-16

**The command path is `command.{h,cpp}`, built like the poll scheduler**: Arduino-free,
doing no I/O, deciding while `sched_task` acts. 20 host tests, and the on-air record is
in the engineering log's 2026-09-16 entry.

**BF-18 also built the bridge's MQTT receive path**, which no task owned. The Firmware
Tasks changelog (v0.19) raised the gap and left it unassigned; `subscribe()` had no
caller and the transport had no way to hand anything back, so §6.2's first line — *"MQTT
command topic"* — had nothing behind it. Folded into BF-18 rather than given a new task
number, because a command path with no command source cannot be tested.

| Decision | Why |
|---|---|
| `MqttTransport` gains an inbound sink | The seam's one concession to PubSubClient, whose callback is a bare function pointer with no user context. A second instance is refused rather than allowed to steal the first's callbacks. **Since BF-37 the concession is gone** (§4.3.3) |
| Action tokens are spec §8.1's `cmd` names lowercased — `open`, `hold_open` | §16.1 fixes the topic shape and §16.2.1 leaves the vocabulary to the bridge. One term names one concept from Home Assistant to the wire. **Breaking to rename once B4 builds discovery on them** |
| Inbound payload buffer is 64 bytes against the outbound 768 | Nothing the bridge *acts on* should arrive in a large buffer |
| An unreadable payload is refused, never defaulted to `0` | `arg` carries `REBOOT`'s `0xA5` guard, so a silent default turns something unreadable into a different command |
| One command in flight across the fleet | Not airtime, which the poll scheduler already serializes. A resync resets a node's command `seq` to 1, so a second command in flight during one would be refused as a replay and read at the bridge as a node fault |
| The resync's retry restarts the attempt budget | It is the first attempt in a sequence space the node will accept. Spending the pre-resync attempts against the new context would give a late resync fewer tries than an early one |
| `seq` allocation and §10.5's wrap live in the registry | The wrap belongs with the counter it wraps, and there is then one place to read when asking what the next `seq` will be. The wrap skips `0` |
| A simnode is allowed every command | A bench identity takes a role at runtime and the bridge cannot know from the address which one. Refusing actuation there makes the bench exercise a different path from the fleet, which is what §4.2a says defeats having bench nodes. They are gated at publication, never here |

**`lran/bridge/diag/cmd/state` was added during the bench run, not designed before it.**
A resync and a command that never resynced publish the same `cmd/ack`, so the only
evidence of one was a serial cable. `cmd_resyncs` is the number that separates a healthy
link from a node rebooting underneath it.

**What is not proved on hardware: the second `REJECTED_CTX`.** §10.3 step 3's stop is
host-tested and the bench could not force it — the window between the node's rejection
and the bridge's retry is under one second. A simnode `ctx_reject` fault would make it
deterministic; raised for **BF-21**.

#### 6.2.2 What BF-34 built, 2026-09-23

**The roll is `context_roll.{h,cpp}`, built like the command path**: Arduino-free, doing no
I/O, deciding while `sched_task` acts. It has 13 host tests in `test_context_roll`, and
`test_command`, `test_diag` and `test_config_store` each gained a case. **It was confirmed on
air on 2026-09-23**: after a bridge reflash, the first command executed with no
`DUPLICATE_CACHED`, and the `REJECTED_CTX`, `ACTUATOR_BUSY` and `ctx_roll_failed` branches
passed. The engineering log's *BF-34 on air* entry has the trace.

| Decision | Why |
|---|---|
| A state machine of its own, not a mode of `CommandPath` | The two read the same answers differently. `REJECTED_CTX` completes a roll and resyncs a command. `ACTUATOR_BUSY` retries a roll and ends a command. A roll publishes nothing on `cmd/ack`. One class holding both readings would branch on every answer |
| The roll and the command path serialize | A roll resets the node's command `seq` space, which is the reason §6.2.1 gives for one command in flight. A roll does not start while a command is in flight, and a command is not admitted while a roll is |
| Timed by `command_ack_timeout_ms` and `cmd_retries` | A roll is a `COMMAND` on the air. A lever of its own would be a new table row for a wait that is the same wait (root rule 8) |
| `ACTUATOR_BUSY` is retried after the ACK window, and spends an attempt | The node sends nothing more after a BUSY, so an immediate retry finds it still busy. An attempt per BUSY means a node stuck busy ends in `ctx_roll_failed` rather than holding the command path off without limit |
| A failed roll waits for the node's next frame, and the answer that failed it does not count | `app_task` reports a frame as heard before it reports the ACK inside it. Counting that ACK would restart the roll at once, and a node without `ROLL_CONTEXT` would then be rolled back to back instead of once per frame it sends |
| The boot `POLL` is the poll scheduler's | It polls every deployed row on its first ticks (D61). **A row not deployed, bench or production, keeps the 2026-09-14 heard-first rule**, confirmed by the operator on 2026-09-23: it rolls when first heard (spec §10.6 step 2), and a simnode not on the bench costs no airtime. This departs from step 1's "each registered node" for rows not deployed |
| **Every simnode role answers a roll**, decided with the operator on 2026-09-23 | A `ROLE_RANGE` or `ROLE_HEALTH` identity that ignored it would fail every roll and draw another on each frame the bridge heard, which puts roll traffic inside a sweep. Other commands stay `ROLE_GATELINK`'s alone (§10.9.2) |
| A command is refused on `sched_task`, when it leaves the queue | `sched_task` owns the pending state and already publishes `cmd/ack`. The payload is `{"outcome":"context_roll_pending"}` with no `seq`, because none was taken |
| A `config/set` is refused on `mqtt_task`, before either half applies | Spec §10.6 refuses it **whole**, and the bridge half applies on `mqtt_task`. `config_set_reaches_node()` picks out the sets that would send a `CONFIG`, and its test checks it against `ConfigStore::apply()`'s split. `mqtt_task` reads the pending bits through one atomic that `sched_task` alone writes. A bit only clears after boot, so a clear bit can be trusted without the lock |
| `ctx_rolls` and `ctx_roll_failed` are spelled in `diag_json.cpp`, not added to `kCounterRegistry` | The registry is what a node's schema `0xF0` is built from, and no node counts these. The roll's detail rides on `diag/cmd/state`: `roll_sent`, `roll_retries`, `roll_busy`, `roll_by_rejected_ctx` and `cmd_refused_roll_pending` |

### 6.3 Publication policy

Implemented in `publish.cpp`, applied uniformly across node types:

| Rule | Implementation |
|---|---|
| **Publish on change** | Per-field comparison against the last published value, with an optional rounding step. Applied to per-cell battery voltages and other jittery telemetry |
| **Staleness → unavailable** | On a node's staleness flag or an age exceeding threshold, publish `unavailable` to the affected entities. **Never republish the cached value** |
| **Sentinels are not numbers** | `INT16_MIN` / `UINT16_MAX` map to `unavailable`, not to a reading |
| **Synthetic stays marked** | A debug-synthetic status reason propagates into the published payload |
| **Events never retained** | Retain flag clear, QoS 1, deduplicated on `(src, ctx_id, event_id)` and the follow-up bit (§6.3.2) |
| **Periodic heartbeat** | An unchanged value is republished at most every `republish_interval_s` (default 900), so an HA restart repopulates rather than showing blanks until the next change |

> The heartbeat is the counterweight to publish-on-change: without it, a value that has
> not changed in six hours is missing from a freshly restarted HA. Retained topics cover
> most of this, but the interval makes it robust to a broker without persistence.

#### 6.3.1 What BF-24 built, 2026-09-23

**`publish.{h,cpp}` renders each `STATUS` into documents, and `app_task` queues them.** The
policy is host-tested in `test_publish` against V-B7's four properties and the bench gate.
No GateLink exists yet, so no document has been published from a real frame. The three
values it reads are bridge rows `0x000D`–`0x000F` (Library Plan §4), and they reach
`app_task` on BF-23's lever board.

**Schema `0x10` becomes five retained documents**, one per spec §16.1 domain. Spec §16.2.1
leaves their payloads to the bridge, so the keys below are this document's choice. Home
Assistant's value templates read them, so a key published is a key frozen:

| Topic | Keys |
|---|---|
| `lran/<node>/gate/state` | `state`, `held_open`, `hold_source`, `movement_cause`, `last_direction`, `in_open`, `in_moving`, `in_safety`, `in_exit`, `in_fire`, `in_alarm`, `input_bits`, `synthetic` |
| `lran/<node>/detect/state` | `safety`, `exit`, `classifying`, `vehicle_while_held`, `suppressed`, `last_traversal`, `traversal_persisted`, `synthetic` |
| `lran/<node>/solar/state` | `available`, `batt_mv`, `batt_ma`, `pv_mv`, `pv_w`, `load_ma`, `yield_today_kwh`, `yield_yesterday_kwh`, `pmax_today_w`, `yield_total_kwh`, `charge_state`, `error`, `tracker`, `load_on`, `charge_inhibited`, `temp_c`, `hex_pending`, `synthetic` |
| `lran/<node>/battery/state` | `available`, `soc`, `soc_source`, `pack_mv`, `pack_ma`, `cell_count`, `cell1_mv`–`cell4_mv`, `cell1_temp_c`–`cell4_temp_c`, `cycles`, `capacity_ah`, `alarms`, `charge_fet`, `discharge_fet`, `charge_inhibited`, `protection`, `balancing`, `ble_rssi_dbm`, `age_s`, `synthetic` |
| `lran/<node>/node/state` | `uptime_s`, `boot_count`, `node_mv`, `node_ma`, `enclosure_temp_c`, `config_persisted`, `sd_ok`, `dry_run`, `bms_ble`, `debug`, `shutdown_latch`, `reason`, `synthetic` |
| `lran/<node>/node/health/state` | Schema `0xF0`: `uptime_s`, `boot_count`, `rx_frames`, `tx_frames`, `rx_dropped`, `cad_backoffs`, `last_rssi_dbm`, `last_snr_db`, `proto_ver`, `debug` |

| Choice | Why |
|---|---|
| **Enumerations are spec §8's names in lower case**; a value the table does not list is `null` | One term names one concept from HA to the wire, as the command tokens do. A newer node's value reads as unknown rather than as a guess, and `input_bits` keeps the raw evidence |
| **Units are converted exactly**: 10 mV to mV, 10 Wh to kWh with two decimals, 0.1 °C and 0.1 Ah to one decimal | Integer arithmetic, so no float round trip reaches HA's history |
| **R-5.2b: a stale block publishes `available: false` with every reading `null`** | `mppt_flags` bit 1 stales `solar`. `battery` is stale when `bms_flags` bit 0 is clear, `bms_age_s` is the sentinel, or it exceeds `bms_stale_s`. The entities list that document as a second availability topic with `avty_mode: all`, so HA shows them unavailable while the node is online. `ble_rssi_dbm`, `age_s` and `hex_pending` stay readable, because they say why |
| **R-5.2a: publish on change compares whole documents**, by an FNV-1a hash | A document is queued when any value in it changes, or when `republish_interval_s` has passed since it was last queued. HA records an entity's state only when its own value changes, so the deadband is what keeps jitter out of history. The hash replaces a kilobyte per node and domain; a collision delays one change to the heartbeat, once in 2³² |
| **Cell voltages move only by `cell_mv_deadband`**, measured from the value last published | Measured from the published value, a drift of 1 mV a poll still crosses the band. Measured from the last reading, it never would |
| **A heartbeat needs a frame.** The interval is checked when a node's `STATUS` arrives | A silent node republishes nothing, which is R-5.2b from the other side. Its availability goes `offline` through BF-20 |
| **`last_traversal` is an ISO 8601 UTC time, from SNTP** (spec §7.2.9) | `mqtt_task` starts SNTP against `pool.ntp.org` when WiFi first connects. Until it answers, the value is `null`. A move of 2 s or less is the age's rounding and is not republished |
| **R-5.2d: `synthetic` is in every document**, true when `status_reason` is `DEBUG_SYNTHETIC` | Each document is a separate history in HA, so each carries the mark. One diagnostic binary sensor shows it |
| **A bench node's `STATUS` is decoded, counted as `bench_withheld` and never published**, whichever way `simnode_diag_enable` is set | Spec §16.6 allows a bench node `diag/state` and `availability` alone. `make_publish()` also refuses `lran/simnode<N>/{gate,detect,battery,solar,event}/`, the second check on the path every publication takes |
| **A broker connect makes every document due again** | The retained documents may be gone. Each is republished on its node's next frame, from that frame |

**The counts go to `lran/bridge/diag/publish/state`**, published with the other bridge
diagnostics: `status_frames`, `documents`, `unchanged`, `heartbeats`, `bench_withheld`,
`queue_refused` and `undecodable`. None is a spec §14.1 discard, because every frame here
has passed the ladder.

**Discovery gains 41 GateLink entities** (`discovery.cpp`, `ha/discovery/`). A table per
node type is BG-2's "template"; WellLink and the bench identities have none. A binary
sensor matches HA's rendering of a JSON boolean, `True` or `False`, so a `null` reads as
unknown rather than off. `test_discovery` renders a document with the policy and checks
that every entity's key is in it.

**§5.3's `decode/` directory was not created.** The library's `deserialize()` for each
schema is the decoder, and `publish.cpp` renders from its struct. A second layer would have
copied the struct field for field.

**What is not done.** Events are **BF-25**. B4's criterion asks for §6.3 "demonstrated".
Nothing on the bench sends a production schema, because a simnode is a bench node. BF-27's
dummy publish showed these rules at the broker on 2026-09-23 (§6.6.2).

#### 6.3.2 What BF-25 built, 2026-09-23

**`PublicationPolicy::on_event()` publishes each `EVENT` once, with retain clear, at QoS 1.**
It sits in `publish.cpp` beside BF-24's documents and shares their enumeration names and
counters. `test_events` carries it on the host. No node on the bench sends an `EVENT`: a
simnode is a bench node, and spec §16.6 keeps its events off every topic. BF-27's dummy
publish sent events through it on air (§6.6.2). **V-B8 passed in Home Assistant on
2026-09-24**, with synthetic events and no replay across HA restarts and discovery
refreshes (§6.6.2).

**The topic is `lran/<node>/event/<name>`**, where `<name>` is spec §8.9's name in lower case:
`fire_asserted`, `vehicle_while_held_open` and the rest. Each type gets a topic of its own,
which is what §8.9 asks for FIRE. The payload is a JSON object, and HA automations read its
keys, so these keys are frozen:

| Key | Value |
|---|---|
| `event_id`, `ctx_id` | Spec §7.3's key. `event_id` restarts with each boot, so HA needs both to recognise a repeat |
| `event_type` | §8.9's name, or `null` for a value the table does not list |
| `event_code` | The raw `event_type`, so an unnamed type still says what it was |
| `follow_up` | `event_flags` bit 0 |
| `hold_source`, `direction`, `gate_state` | §8's names, `null` when unlisted, as in §6.3.1 |
| `input_bits`, `detail`, `uptime_s` | Passed through. `detail` is event-specific (§7.3) |
| `synthetic` | `true` for BF-27's dummy publish, `false` for a frame from the radio. Spec §7.3 gives an `EVENT` no `status_reason`, so the mark is the caller's, not the frame's (§6.6.2). Added 2026-09-23 |

| Choice | Why |
|---|---|
| **The deduplication key is `(src, ctx_id, event_id, follow-up bit)`**, spec §7.3's key since v0.15. Chosen with the operator | §7.3 has a follow-up reuse its first edge's `event_id`, so the v0.14 triple alone withheld every follow-up and the classified direction never reached HA. The first edge and its follow-up are each published once |
| **The bridge remembers the last 16 events per node**, in a ring | A retransmission follows its original by one CAD backoff, at most `backoff_max_ms`. Nothing makes it arrive before the node's next event, so a high-water mark could withhold a first transmission that was lost |
| **An unlisted `event_type` goes to `event/unknown`**, not dropped | A newer node's event may be an alert |
| **A refused event is not remembered** | The node's retransmission of it, if one comes, then gets through. Counted as `queue_refused` |
| **A broker connect forgets the documents, not the events** | None was retained. Forgetting them would let a late retransmission publish a second time |
| **`make_publish()` refuses an event topic asked for at QoS 0**, as it refuses a retained one | The two halves of spec §16.3's rule are checked in the same place |
| **A refused publish leaves an event at the head of its queue** (`drain_publish_queue()`). A held-event slot until BF-38 (§4.3.3) | State is lost on a failed publish and the next frame replaces it. An event has no replacement, because the policy has already recorded it as published |
| **A bench node's event is decoded, counted as `bench_withheld` and never published** | Spec §16.6, as for its `STATUS` |

**PubSubClient 2.8 published at QoS 0, whatever the message asked**, although spec §16.3
requires QoS 1. BF-37 moved the bridge to espMqttClient, D5's designated fallback, and
events leave at QoS 1 (§4.3.3).

**`lran/bridge/diag/publish/state` gains three counts**: `event_frames`, `events` and
`event_repeats`. `bench_withheld`, `queue_refused` and `undecodable` now count both kinds of
frame.

**Discovery has no event entities.** An HA automation triggers on the MQTT topic itself, and
spec §16.3 makes the event topic the automation trigger. A dashboard's view of an event is
the retained binary sensor in `<domain>/state` that §16.3 describes, such as `detect`'s
`vehicle_while_held`.

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
- When it first hears a node, and again after each write the node answers, the bridge
  issues read-class HEX requests for the charge parameters and publishes them as
  diagnostic sensors, **so a wrong charge profile is visible rather than latent**
  (**R-3.5d**).

#### 6.4.1 What BF-28 to BF-30 built, 2026-09-25

**The proxy is `hex_proxy.{h,cpp}`, built like the command path**: Arduino-free, doing no
I/O, deciding while `sched_task` acts. `test_hex_proxy` has 28 host tests, and it tests
each of V-B6's three gates on its own. **V-B6 passed on the bench on 2026-09-25**, against
the simulated MPPT that BF-36 gave simnode `ROLE_GATELINK` (§10.2). The engineering log's
*V-B6 on the bench* entry has the run. Gate 1 is host-tested only, because no bench tool
sends a write with a bad MAC.

**The three gates on a write stand alone** (PRD R-3.5b, BS-2). The bridge judges a write
from the command nibble alone, Set (`0x8`) or Restart (`0x6`), as spec §7.6 does.

1. **A MAC.** The library adds one to every write-class `HEX_REQ` it encodes, and the node
   refuses one without it. `build_hex_req_frame()` produces no frame at all when the node
   key is missing.
2. **An armed switch that expires** (`WriteArm`). `HexProxy::next()` asks whether the node
   is armed before every write transmission, a resync's included. It asks with the clock,
   so a lapsed arm refuses a write before `sched_task` has published the switch off. A
   refused write builds no frame and resolves `refused_disarmed`.
3. **An audit entry for every write attempt**, refusals included, published retained on
   `hex/audit`. The entry is the response document plus `authorization` (`armed` or
   `disarmed`) and `at`, which is ISO 8601 UTC, or `null` before SNTP has set the clock.

| Decision | Why |
|---|---|
| **A write takes its `seq` from the command space**, and one HEX transaction is in flight across the fleet | Spec §9.4 steps 4–6 apply to every authenticated type, and a resync resets that space to 1 (spec §10.3). The proxy therefore serializes with the command path and the roll, for §6.2.1's reason |
| **A Restart refused with `REJECTED_CTX` is not resynced.** It resolves `unknown`, and the node's context is adopted (spec §10.7, **D70**) | The node may have reset after the MPPT restarted. A Set keeps the resync, because writing a register twice writes one value. `test_hex_proxy` has the case |
| **A write is never retried.** One with no answer resolves `unknown` | The node's deduplication cache holds an ACK result, not the MPPT's answer, so a retried write would draw `COMMAND_ACK(DUPLICATE_CACHED)` and never the register value. A Get settles an `unknown` write, as a readback settles a lost `CONFIG_ACK` (§6.7.3). Spec §10.3's resync is the one exception: it retries once, because the node refused the first at step 2 and never forwarded it. Decided with the operator on 2026-09-25 |
| **A read is retried under the same `seq`**, twice by default | A read changes nothing, and its `seq` is in neither sequence space (spec §10.2). A read with no answer resolves `no_response` |
| **A retained `write_enable/set` is ignored and cleared** | Spec §16.2 marks the topic retained. A retained `ON` would re-arm writes on every broker reconnect. Decided with the operator on 2026-09-25, and raised for spec v0.16. The broker marks a message retained only when it replays it on a subscribe, so the rule acts on a reconnect; a retained `ON` published while the bridge is connected arms it, as any `ON` does |
| **A request that is not a VE.Direct HEX frame is refused before any airtime** | `classify_hex()` uses `lib/vedirect`'s parser. A frame the MPPT would answer with a frame error would otherwise cost a solar node a transmission. The refusal answers on `hex/response` as `malformed`; `busy` and `context_roll_pending` answer the same way |
| **`hex/response` is not retained** | An answer replayed on an HA restart would report a request nobody had just made, as `config/ack`'s would (spec §16.7.3) |
| **Two bridge rows**: `hex_rsp_timeout_ms` (3000) and `mppt_write_arm_timeout_s` (300) | Root rule 8. The read retry count is a count, not a time, and stays a constant |
| **A bench node's answers publish whatever `simnode_diag_enable` says; its readback follows the flag** | D65's reason: gated, a request on a bench node's own topic would go unanswered. The readback is data the node did not report in answer to anyone. The VE.Direct discovery entities are GateLink's alone (spec §16.6) |

**The readback is `charge_readback.{h,cpp}`** (BF-30, R-3.5d). It reads ten charge registers
in one pass and publishes them retained on `lran/<node>/vedirect/charge/state`, where
Home Assistant shows them as diagnostic sensors. A pass runs when the bridge first hears a
node after boot, and again after any write the node answered. A setting changed behind
the bridge, with VictronConnect for example, shows at the next boot. A register the node refuses
or cannot read is `null`, never 0 V. A read that fails outright abandons the rest of the
pass, because a node with no MPPT behind it would otherwise spend a timeout on every
register. `hex_allowed()` limits the proxy and the readback to GateLink and simnode rows.

| Decision | Why |
|---|---|
| **Register IDs, widths, signs and scales follow [`osh-labs/VE.Direct_mppt_arduino`](https://github.com/osh-labs/VE.Direct_mppt_arduino)**, `src/VeDirectRegisters.h` | It is the VE.Direct reference of record (GateLink Impl Plan §4.2.4). `0xEDE0`, the low-temperature charge level, is not in that library, so it comes from Victron's *BlueSolar HEX protocol* §1.1 |
| **The system voltage setting is `0xEDEA`**, the library's `SYSTEM_VOLTAGE` | B5 first read `0xEDEF`, which the library does not name. The engineering log's *B5's HEX code against osh-labs* entry records the cross-check |
| **Every key is prefixed `charge_`**, chosen with the operator on 2026-09-25 | It marks a setting read back, beside GateLink's readings such as `pack_voltage` and `mppt_batt_voltage`. The keys are HA `object_id`s and are frozen |
| **Ten registers, not the MPPT's whole table** | R-3.5e forbids modelling a hundred registers as entities. These ten decide how the pack is charged |

**No scale has been observed on the MPPT 75/15.** Every value comes from the reference, and
B6's readback against the real MPPT is what confirms them. A wrong scale publishes a
plausible wrong voltage, which is the failure R-3.5d exists to expose. A disagreement at B6
is a finding to record, not a number to adjust.

**Four spec readings were raised for v0.16** in the engineering log's *B5's spec readings*
entry: the answer to a refused write, `HEX_RSP`'s `seq`, the retained `write_enable/set`,
and the `charge/state` topic that §16.2 does not list.

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

#### 6.5.1 What BF-13 built, and why rollback needed more than a partition table

**The partition table was the easy half.** `firmware/bridge/partitions.csv` is
Arduino-ESP32's `default_8MB.csv` — two 3.2 MB OTA slots, `otadata`, a reserved
`spiffs` and a `coredump` — **committed rather than referenced**, so a platform bump
that changed the board's default cannot change this table silently.
`tools/checks/bridge_partitions.py` fails CI on an unequal pair, a missing `otadata`, a
`factory` slot, an overlap, or an image past 90 % of a slot.

**The other half is that Arduino-ESP32 2.0.x defeats rollback by default.** The prebuilt
bootloader for this board has `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` set (checked in
the installed framework's `qio_qspi/sdkconfig.h`), so a new image boots in
`PENDING_VERIFY` and is rolled back if it resets before being marked valid. **But
`initArduino()` marks it valid itself, before `setup()` runs** — its weak
`verifyOta()` returns true. Out of the box, any image that reaches `initArduino()` is
kept, including one that never finds the LAN again. On this node that is the failure
that matters: **a bridge that cannot reach the network cannot be OTA'd back**, and
recovery is a USB cable.

**`ota.cpp` takes the decision back** by overriding `verifyRollbackLater()`, and
`ota_policy.cpp` makes it: keep the image once **every task started and the broker is
connected, after at least 120 s**; roll it back if that has not happened by **600 s**.
The minimum stops an image that connects and then falls over from being blessed in its
first seconds. The deadline outlasts a slow AP plus the broker's 30 s capped backoff;
when it fires on a *good* image — the broker was down for ten minutes during the update
— the result is the previous, known-good image, which is the safe direction.
**TODO(BF-16):** once the radio exists, an image that cannot hear the fleet is bad too.

> **The override has one load-bearing detail, and its falsifier is in CI.** The weak
> default lives in `esp32-hal-misc.c` — a C file — and no header declares it, so the
> override must be `extern "C"`. A C++ definition gets a mangled name, overrides
> nothing, **links cleanly**, and leaves the core blessing every image. The firmware
> job runs `bridge_partitions.py --elf`, which fails unless the linked image carries
> `verifyRollbackLater` as a **strong** symbol. On the bench, the boot banner's
> `Image state:` line must read `pending_verify` on the first boot after an upload.

**ArduinoOTA, not `esp_https_ota`.** The bridge is on the LAN, and a push from
PlatformIO (`-e heltec_ota`, password from `LRAN_OTA_PASSWORD`, never written into
`platformio.ini`) needs no server. It authenticates with an MD5 challenge-response
(R-5.3b) — the password does not cross the network in the clear, but this is an
authenticated endpoint on a LAN, not a hardened one. **USB stays the recovery path**
(R-5.3a): an esptool upload rewrites the bootloader, the table and `otadata`.

**R-5.3d is honoured by when ArduinoOTA is serviced**: `ArduinoOTA.handle()` is where
an upload invitation is answered, and `ota_task` calls it only when `lora_task` is idle.
An upload in progress runs to completion inside `handle()`; its flash writes stall both
cores briefly, so `lora_task` can miss a frame during an upload. **R-5.3e**:
`lran/bridge/version` carries `version`, `git`, `slot` and `ota_state`, retained, on
every broker connect. **Spec §16.2 names the topic but not its payload**; this document
is the bridge's choice, recorded in the engineering log as a gap for the next spec
revision rather than redefined here.

#### 6.5.2 V-B9 — the procedure

**Run 2026-09-13; all four steps passed.** The banner lines, and one failed OTA attempt
that a retry cleared, are in the bridge [engineering log](./engineering-log.md). Re-run
this procedure after any change to `ota.cpp`, `ota_policy.cpp`, `partitions.csv` or the
Arduino-ESP32 version. **Owed again since BF-16**, which added `radio_ok` to the verdict
(§5.3.1). The flat-case Heltec, the dev broker, and
`LRAN_OTA_PASSWORD` set to the value in `secrets.h`. Watch the serial log throughout;
the three banner lines — `Version:`, `Slot:`, `Image state:` — are what is read.

1. **USB-flash the good image:** `pio run -d firmware/bridge -e heltec -t upload`.
   Expect `Slot: app0`, `Image state: not_pending`.
2. **OTA a second good build** (change `custom_bridge_version`):
   `pio run -d firmware/bridge -e heltec_ota -t upload`. Expect `Slot: app1`, **`Image
   state: pending_verify`**, and after two minutes `OTA: image verified`. **If the state
   reads `not_pending` here, stop** — the override is not in effect, and step 3 would
   report a pass it did not earn. **This step is V-B9's "OTA succeeds".**
3. **OTA the no-network bad image:** `-e v_b9_no_network`. Expect `Slot: app0`,
   `pending_verify`, the V-B9 banner, then at 90 s `ROLLING BACK` and a reboot into
   **`Slot: app1`** with step 2's version and git.
4. **OTA the panic image:** `-e v_b9_panic`. Expect the V-B9 banner once, an abort, and a
   reboot into the step-2 image with no code of the bad image run again. **If the banner
   prints twice, the bootloader is not rolling back.**
5. Record all four in `docs/bridge/engineering-log.md`, with the banner lines verbatim.

### 6.6 Debug tooling

| Tool | Implementation note |
|---|---|
| **Dummy publish** | Synthetic decoded payloads pushed through the real publication policy and discovery path, with **no node and no radio**. This is what unblocks HA integration on the bench |
| **Node simulators** | Bridge-side generators per node type. Complements `simnode` (§10), which tests the RF path; these test the MQTT path. **The two must not share a generator** — a bridge-side simulator that feeds the decoder its own output tests nothing |
| **Packet loopback** | RF echo and internal loopback with no radio |
| **Raw frame log** | Every frame in and out with source, type, schema, RSSI, SNR and discard reason. Published to a debug topic and to serial |
| **MQTT as harness** | `mosquitto_sub -t 'lran/#'` to watch every decoded payload live; `mosquitto_pub` to inject commands, decoupled from HA and from the RF link |

#### 6.6.1 What BF-27 built, 2026-09-17

**The raw frame log only.** The other four tools in the table above were unbuilt then,
and none of them blocked anything: BF-27 was pulled forward for the receive path's 1 s
knee, which needed the log and nothing else. The dummy publish followed on 2026-09-23
(§6.6.2).

| Decision | What was built | Why not the obvious alternative |
|---|---|---|
| Where a record is made | In `lora_task`, at every outcome the radio can produce | A record made after the queue describes frames that already passed the ladder, which is the half that was never in doubt |
| How it crosses tasks | A lock-free ring in `frame_log.h`, drained by `log_task` | A queue would cost `lora_task` a copy and refuse under load; the ring overwrites instead, which for a timeline is the right failure |
| What a full ring does | Overwrites its oldest record, counting each one | §5.2's queues drop the newest, which is right for events driving email and SMS. A DropNewest log stops recording when the load arrives, which is the passage to keep |
| How an overwrite is found | Every record carries a monotonic index | A count says how many were lost; the index says which. It also lets the reader detect a slot the producer tore under it |
| What says the radio was busy | The running `rx_deaf_ms` in every record | Deafness between two arrivals is then a subtraction, so the reader needs no assumption about where in a window it fell |
| Where stage 1 lives | `RxOutcome`, beside `lran::Status` | A PHY CRC error has no `Status` value and must not gain one — §5.2's ladder keeps it apart from `Status::BadCrc` because the two lead to opposite conclusions about RF versus software |

**The record carries what §6.6 asks for** — source, type, schema, RSSI, SNR and discard
reason — **plus the timestamp, the fragment byte and the deaf total the knee needed.**
`type`, `schema` and `frag` are read off the wire at §5's offsets, because a frame refused
before stage 6 has no decoded header and what it *claimed* to be is the diagnosis.

**Two things BF-27 does not do.**

**It has no runtime enable.** A lever needs the HA-visible configuration path, which does
not exist and whose route is an open operator decision (`docs/bridge/HANDOFF.md`). Building
one here would settle that decision by default. What is left running is a 24-byte store per
frame, bounded by the frame rate the link already carries.

**It publishes unretained, and §16.2's table says a `/state` leaf is retained.** The
deviation is deliberate. This topic carries a rolling window of arrivals, and a retained
one replays a finished burst as though it were arriving now — §16.3's argument, reaching a
topic §16.3 does not cover. **Raise it against the specification rather than treating this
paragraph as permission:** §16.2's table predates any streaming diagnostic.

> **Settled in spec v0.15 (D66).** §16.1 gains a `log` leaf, never retained, and the topic
> becomes `lran/bridge/diag/rxlog/log`. The bridge and `tools/simctl/rxlog.py` moved to it
> on 2026-09-25.

**`tools/simctl/rxlog.py` reads the topic and `rxlog_analyze.py` does the arithmetic**,
split the way `per_measure.py` and `per_window.py` are. Three guards earn their place, and
each of them prevents a confident wrong answer:

- **W11.** A `PING` responder echoes the initiator's `seq` (§6.6), so a node's `PING`
  answers carry numbers from the bridge's sequence space. Streams are keyed on
  `(peer, type)`, which separates them.
- **§10.3's resync** resets a node's sequence, and so do a reboot and a context roll
  (spec §10.6). A jump of tens of
  thousands is a new sequence, not 40 000 losses.
- **The ring's overwrites are reported apart from records lost in transit.** Added
  together they would blame the receive path for a dropped MQTT message.

**What it reports is a ceiling, not a classification.** `rx_deaf_ms` is a total and says
nothing about where in a gap the deafness fell, so the share of the gap it occupies is the
most of that gap it could have covered. Summing those shares gives the largest number of
losses the bridge's own radio could account for. The engineering log's 2026-09-17 entry
records the draft that got this wrong and what the bench printed.

---

#### 6.6.2 What BF-27's dummy publish built, 2026-09-23

**A serial console line becomes a `STATUS` or an `EVENT` that `app_task` hands to the real
publication policy.** The documents, events and discovery that follow are the ones a
node's frame would produce, with no node and no radio. `dummy.{h,cpp}` builds the frame
from the library's schema struct and encodes it with the library's serializer, so
`publish.cpp` decodes the codec's output rather than its own. `test_dummy` carries it on
the host.

```text
dummy help
dummy show
dummy set <field>=<value> [<field>=<value> ...]    `na` sets the field's sentinel
dummy status <node>
dummy event <node> <type> [follow]                 spec §8.9's name in lower case, or its number
```

| Decision | What was built | Why not the obvious alternative |
|---|---|---|
| Which identity a dummy frame carries | The production node the console names, `gatelink` today. Chosen with the operator | A new `dummy` node needs a spec §5.3 address and a §16.1 token. A simnode identity is refused by §16.6 on every production topic. GateLink's own address exercises the topics and entities B6 will use |
| How a dummy `STATUS` is marked | `status_reason` is `DEBUG_SYNTHETIC` whatever the console asks, and `dummy set status_reason` is refused | Spec §8.7 and R-5.2d. Every document then says `synthetic: true` through BF-24's existing rule |
| How a dummy `EVENT` is marked | `on_event()` takes a `synthetic` flag, and the event payload gains a `synthetic` key (§6.3.2) | Spec §7.3 gives an `EVENT` no `status_reason`, so nothing on the wire can carry the mark. The caller knows, so the caller says |
| How it is triggered | The USB serial console, read in `loop()`. Chosen with the operator | An MQTT trigger would let anything on the broker inject a gate event, and events drive email and SMS |
| What a dummy frame touches | The policy alone. `RxMessage::dummy` makes `app_task` skip `registry_observe()`, `sched_on_heard()` and the ACK paths | No node sent it. It must not teach the registry a `ctx_id`, answer a poll or move availability (spec §16.5) |
| What it refuses | A bench address; a node the bridge has heard this boot; a `set` with any bad pair, which applies none of them | A bench node's frames are withheld by §16.6 anyway. A heard node is real, and synthetic history mixed into its own is what R-5.2d exists to stop |
| The template's clock | `uptime_s` and `last_traversal_age_s` advance by the time between frames | Held still, the traversal age computes to a new absolute time on every frame (spec §7.2.9), and `detect/state` republished each time. Found on the bench |

**The console exists in the production image**, outside `v_b12_blaster`, which owns the
serial port for its own commands. It needs a USB cable at the bridge. It refuses a node
heard this boot, so it cannot mark a deployed GateLink's history. A dummy event on a
GateLink that has not been heard still reaches `lran/gatelink/event/*`, so **an automation
that sends email or SMS filters on `synthetic`**.

**Availability is not the dummy's to move.** On a bench bridge GateLink's `deployed` lever
is 0 (D61), so the bridge neither polls nor watches it and publishes no availability for
it. For a bench session, publish `online` retained to `lran/gatelink/availability` by hand,
and it stands. With `deployed` set, GateLink is watched, never answers, and goes `offline`
three missed polls after the bridge boots and again at every broker connect, overwriting a
hand-set `online` each time. That was found on 2026-09-24, before D61.

**On air, 2026-09-23**, against the sandbox broker, with `republish_interval_s` set to 60
for the run and restored to 900 after. The engineering log has the capture.

| §6.3 rule | Shown by |
|---|---|
| Publish on change | A second identical `STATUS` published nothing; `unchanged` counted it |
| Jitter suppressed | `cell1_mv` 3310 → 3313 withheld; → 3320 published `battery/state` |
| Sentinels are not numbers | `enclosure_temp_c10=na` and `load_ma=na` published `null` |
| Staleness → unavailable | `mppt_flags=2` published `solar/state` with `available: false` and every reading `null`; `bms_age_s=900` did the same to `battery/state` |
| Synthetic stays marked | Every document and every event carried `synthetic: true` |
| Events never retained | Three events published once each, the follow-up among them; a retained-only subscription afterwards found no `event` topic |
| Heartbeat | An unchanged `STATUS` 65 s later republished all five documents; `heartbeats` rose by 4 |

**In Home Assistant, 2026-09-24**, on HA 2026.9.3 with the same image. The rules read the
same in HA: the synthetic sensor was `on`, and 20 solar and battery entities went
`unavailable` and stayed so through HA restarts. **V-B8 passed.** An automation on
`lran/gatelink/event/#` logged each of three events once, through two HA restarts, an MQTT
integration reload and a broker restart that republished discovery. The engineering log
has the sequence.

**What is not shown.** `node/state` republishes on every frame, because `uptime_s` is in
it and changes every poll. A real GateLink will do the same. Whether uptime belongs in the change hash is an open question (`HANDOFF.md`).

### 6.7 The configuration path — BF-32, built 2026-09-21

**Spec §16.7 from Home Assistant to the node and back, in four files.** `config_json` reads
§16.7.2's payload and writes `config/ack` and `config/state`; `config_store` holds what the
bridge owns; `config_path` is the node half's state machine; `nvs_persist` is the store
behind it (**D49**). The first three are Arduino-free and host-tested.

#### 6.7.1 The topic decides which row a name means

**Three names are in both blocks of `/lib/lran-config/`'s table** — `cad_retries`,
`backoff_max_ms` and `frag_reassembly_timeout_ms` — because the bridge and every node each
have their own. §16.7.1 resolves them by where the set arrived:

| Topic | Rows it exposes |
|---|---|
| `lran/bridge/config/set` | `Owner::BridgeGlobal` |
| `lran/<node>/config/set` | `Owner::Node` **and** `Owner::BridgePerNode` for that node |

**A lookup that searched both blocks would answer the bridge's row for a set aimed at a
node**, and the write would land on the wrong radio while the ack said `ok`. `find_param`
takes a scope for that reason, and **a name the scope does not hold is `unknown_param`**,
never a fall-through to the other block.

#### 6.7.2 One answer for two halves

**§16.7.1 asks for ONE `config/ack`, published when every half has an outcome.** A set on a
node's topic may name parameters of both kinds, so the bridge's half — already applied on
`mqtt_task` — travels with the job to `sched_task` and is published beside the node's.
Publishing it early would give Home Assistant two answers to one set, the first incomplete.

`persist` combines by §16.7.3's rule: `unknown` if either half is unknown, otherwise the
less persisted of the two.

#### 6.7.2a What spec v0.15 changed here, built 2026-09-25

**`source` in `config/state` is the node's own marking** (**D68**). The readback mirror
keeps each result's `OVERRIDE` bit beside its value, so an override equal to its default
reads `override`. A bridge-held row reads `Store::marked_override()`, which also marks a
PHY trial value, because `config/state` reports the trial value while it runs. **The mirror
takes a `CLAMPED` or `INVALID_VALUE` result as well as an `OK` one**, because spec §8.12
has both carry the effective value. Until 2026-09-25 it took `OK` alone, so a clamped set
left `config/state` showing the value from before the set.

**A node's `CONFIG_CHANGE` is answered with a readback** (**D69**, spec §8.7). `app_task`
marks the node, and `sched_config()` starts a `readback_only` job once Home Assistant's
queued jobs are done. The job begins at `POLL` bit 1 and sends no `CONFIG`. It republishes
`config/state` and publishes no `config/ack`, because no set is being answered. The
simnode's `ROLE_GATELINK` sends one after its own PHY revert (§10.2), so the path runs on
the bench without setting the reason by hand.

**A bandwidth off the list is refused alone** (**D64**). On the bridge's topic, the entry
reads `invalid_value` and the change target keeps the current bandwidth, so the other PHY
rows of the same set may still change. The status survives an abandon or a revert, because
that entry never joined the change. **A set whose every PHY row was refused answers
`persist` `not_applied`** (spec §8.11), through `phy_unchanged_persist()`. It answered
`persisted` until the 2026-09-25 bench run, because the unchanged-group path read every
PHY-only set as answered from the committed group. On a node's topic a PHY row answers
`read_only` whatever its value (spec §16.7.1), so a bandwidth of 300 there never reaches
D64's check.

**The bridge's own events carry `boot`** (**D67**). `boot_count.cpp` keeps the count in its
own NVS namespace, apart from `cfg`, because `restore_defaults` cleared `cfg` whole until
§6.7.8 changed it. A failed
commit write publishes `phy_reverted` with `reason` `commit_failed` (**D63**).

#### 6.7.3 A lost `CONFIG_ACK` is recovered by readback

**This is the opposite of §6.2's command rule and the difference is the point.** A command
is idempotent through the node's `(ctx_id, seq)` dedup, so **BS-3** retries it with the same
`seq`. A configuration write is not idempotently repeatable, and repeating one cannot tell
you whether the first took effect. §7.4: the outcome is **`unknown`**, never failure, and
the bridge resolves it with a `POLL` carrying bit 1.

**Home Assistant is told `unknown` first and told the truth when the node answers.** An
operator watching a dashboard learns the bridge does not know inside the ACK timeout,
rather than after a second round trip that may also fail. **That timeout is
`config_ack_timeout_ms`**, 8000 ms by default, added in Library Plan v0.14 because the
bridge had fixed it at compile time.

#### 6.7.4 The split readback, and what it costs to get wrong

Spec §7.4.1 and **D57**. Three rules, each with a host test:

| Rule | What breaking it does |
|---|---|
| Accept **more than one** `CONFIG_ACK` bearing a given `seq` | A bridge that closes on the first strands the rest and reports a configuration it did not finish reading |
| Close on the message whose `MORE_FOLLOWS` is **clear** | As above |
| Publish **nothing** from an incomplete answer | A retained `config/state` holding half of this answer and half of the last cannot be read back apart |

**`config_readback_timeout_ms` runs from the FIRST message of the answer**, not from the
request, so a node several seconds into a long answer is not cut off by a clock that started
earlier. A node that answers the poll with nothing at all is still bounded, by the window
the poll opened. An abandoned answer counts **`config_readback_abandoned`**, which is
bridge-local and deliberately not a §14.1 counter — nothing was discarded on the wire.

#### 6.7.5 What the bench found that the host could not

**Three defects, none visible at a desk.** The engineering log's 2026-09-21 entry has the
detail; they are named here because each is a shape worth recognising again:

1. **The simnode answered a solicited `CONFIG_ACK` under its status `seq`** rather than
   repeating the request's, so correlation failed and a set that had applied was reported
   `unknown`. Both sides were internally consistent and both suites passed.
2. **`sched_task`'s stack overflowed** on the first set aimed at a node — a ~1 KB
   `ConfigJob` as a local. Fixed by moving it to static storage, and the stack raised from
   3072 to **5120 from a high-water measurement of 84 bytes free**, not from the crash.
3. **A set's ACK blanked the rows it did not name**, because the state mirror replaced
   wholesale. A readback replaces; a set's results merge.

> **What would falsify this section.** It assumes a node's whole table can be read back
> inside `kMaxConfigAckMessages` (§7.4.1) and staged inside the bridge's 64 rows. GateLink's
> counted 25 parameters (**W10**) fit; a node that does not has outgrown the mechanism, and
> `staging_overflow` in `lran/bridge/diag/*` is what says so rather than a truncated answer.

#### 6.7.6 Two tasks share the store, under one lock

**`g_config` is used from two tasks, and neither `ConfigStore` nor `Store` takes a lock.**
`mqtt_task` writes the stores in `apply()` and `restore_defaults()`, reads them in
`read_all()`, and reads the mirror in `state()`. `sched_task` writes the mirror in
`note_readback()` and `note_set_results()` when a node transaction resolves, then reads it
in `state()`. Found 2026-09-23, from BF-32. Without a lock, a set arriving while a
transaction resolves can read a half-written mirror or store.

**`ConfigLock` in `task_runtime.cpp` is held across `g_config` calls and nothing else.** It
is never held across a publish, a queue send, a registry call or `SchedLock`, so it nests
with no other lock. `mqtt_task` holds it from the store write through `levers_from()`, so
the lever board carries the values that set left. A holder can wait on an NVS write inside
`apply()`, which `sched_task`'s 1 s tick absorbs. `lora_task` never takes it, and
`config_begin()` runs before the tasks and needs none.

**Handing the resolution to `mqtt_task` was the alternative**, making it the only task that
touches `g_config`, as it is the only writer of the lever board. It would also take stack
off `sched_task`. It was not chosen: it needs a new queue item of about a kilobyte, and it
moves the `config/ack` publish to another task, for a race that one short lock closes.

#### 6.7.7 A node reboot owes a readback, built 2026-09-25

**The readback mirror said what a node ran before its reboot.** A simnode holds its
overrides in RAM, so a reboot cleared them while `config/state` went on reporting them as
current. A node with a store (**D49**) keeps them, and the bridge cannot tell the two apart
from the reboot alone. So the bridge asks: **a node reboot sets the same pending bit a
`CONFIG_CHANGE` does**, and `sched_config()` starts the same `readback_only` job (§6.7.2a).
It costs one `POLL` per reboot.

**A new `ctx_id` is not the trigger.** Spec §10.1 says a new context no longer means a
reboot, because a roll (§10.6) makes one and keeps the configuration. The same section
names what does report a reboot, `boot_count` and `uptime_s`. `RebootWatch` in
`config_path.h` reads three signs from a node's `STATUS`, on schemas `0x10`, `0xFE` and
`0xF0`, and any one is enough:

| Sign | Why it is needed |
|---|---|
| `status_reason` `BOOT` (spec §8.7) | The first `STATUS` after a boot may be the first the bridge hears this run, with nothing to compare. `0xF0` has no reason field |
| `boot_count` changed, both readings non-zero | A simulated GateLink reboot on the simnode, `reboot <hex>`, keeps the board's uptime. `0` is unavailable (§7.2.4) and never compared |
| `uptime_s` below the last reading plus the time since it arrived | A node with no store reports `boot_count` `0`. Adding the gap catches a node heard again only after it ran longer than it had before the reboot |

The third sign allows 5 s plus 1/256 of the gap. The 5 s covers truncation to whole
seconds and a frame's wait for media access (§12.3, up to `backoff_max_ms` a turn), and
the fraction covers clock drift at about forty times a crystal's tolerance. A roll moves
none of the three. `app_task` alone calls `RebootWatch`, so it takes no lock.

**On the bench on 2026-09-25**, each of two reboots of `simnode1` put `config/state` back
on the node's defaults within 8 s: a `REBOOT` command, caught by `BOOT`, and a board reset,
caught by `uptime_s` alone. The bridge engineering log's *mirror readback* entry has the
run.

#### 6.7.8 Three restart edges, built 2026-09-26

B4b's bench runs found three ways a bridge restart left Home Assistant with the wrong
answer. None is common. Each now survives the restart in NVS.

| Edge | What went wrong | What the bridge does now |
|---|---|---|
| **A committed PHY change's `config/ack`** (spec §16.7.5) | A reset 150 ms after the commit beat `mqtt_task` to the broker, and the set got no answer | The commit's one blob write also records the answer as owed, with two bits per PHY row for its status. `mqtt_task` clears the record only once the publish queue is empty and the transport has sent what it held. After a reset, `sched_task` rebuilds the answer from the record and the committed group |
| **A `restore_defaults` during a PHY trial** | `NvsPersist::clear_all()` erased the `cfg` namespace, and `Store::restore_defaults()` rewrote the group with the trial marker clear. A restart in that trial went unreported | `clear_all()` removes the table's keys one at a time and never touches the blob. `Store::restore_defaults()` no longer rewrites the group. The erase and rewrite also left a moment with no group on flash, and that is gone too |
| **A retained bench `online` after a reboot** (§4.2a.1) | A flag set `applied_not_persisted` came back clear, and nothing withdrew the `online` published before the reboot | NVS keeps a mask of the bench topics holding a retained `online`, one bit per address from `0xF0`, written only when the mask changes. A boot with the flag clear queues `offline` for each topic in the mask |

**The rebuilt `config/ack` carries the PHY rows alone.** A set's non-PHY entries are not in
the blob, and the bridge's `config/state` carries their values. An answer that invented
their statuses would say something the bridge does not know. Its `persist` is `persisted`,
because the commit wrote the group.

**A refused publication sends the answer again**, because the drain cannot tell whether
the refused message was the answer. Home Assistant may then see the same answer twice,
which is safer than never seeing it.

**The blob is version 2**, with a `u16` of answer rows after the flags. Version 1 still
decodes, as a blob owing nothing, so a bridge flashed over a committed group boots on it.
The simnode shares the layout and writes the new field as zero.

**Host-tested only.** `test_phy_change`, `test_config_path`, `test_availability` and
lran-config's `test_table` cover the blob, the rebuilt answer, the withdrawal rule and the
restore. None of the three edges has been run on the bench.

---

## 7. Test and verification plan

### 7.1 Coverage matrix

| Requirement | Verified by | Milestone |
|---|---|---|
| V-B1 range, both bearings | Two Heltec boards, field walk | B1 |
| V-B2 multi-node registry | `simnode` ×2 on the bench | B3a; keys by command in B3b |
| V-B3 availability watchdog | Power down a simnode mid-poll | B3a |
| V-B4 discovery incl. reconnect | Restart the broker with entities live | B4. **Met 2026-09-24**: all 67 production configs republished within 5 s of the broker's return, §8.2 |
| V-B5 command retry / dedup | Suppress an ACK deliberately | B3b |
| V-B6 HEX three gates | Each gate tested independently | B5 |
| V-B7 publication policy | Injected jitter, staleness flags, sentinels | B4. **Met 2026-09-23** at the broker and 2026-09-24 in HA, on BF-27's dummy publish, §6.6.2 |
| V-B8 events fire once | HA restart + discovery refresh with an event in history | B4. **Met 2026-09-24** on synthetic events, §6.6.2 |
| V-B9 OTA + rollback | Deliberately bad image | B2 |
| V-B10 version tolerance | simnode announcing N−1, then N−2 | B3b |
| V-B11 fleet with no node hardware | Dummy publish + simulators | B4. **Met 2026-09-24 for GateLink**, by dummy publish, with the operator. WellLink waits for its schema, and BF-27's bridge-side simulators do not gate it, §8.2 |
| V-B12 LoRa PER, WiFi idle vs. saturated | A UDP blaster loading the bridge's WiFi, against a known frame sequence (**M22**, §8.1.2) | **B4** — moved from B3b 2026-09-17, §8.1. **Met 2026-09-23**, §8.1.3 |
| §14 discard ladder, stages 2–9 | `simnode` `ROLE_FAULT`, §10.5 catalogue | B3a by hand; B3b scripted |
| §14 stage 1 (PHY CRC) | **Not injectable** — collect at the far edge of the B1 range walk (§10.5) | B1 |
| §5.8 `UNKNOWN_HDR_EXT` | `fault crit_ext`; and `fault hdr_rsv` must be **accepted** | B3a |
| §16.6 bench publication gate | `simnode_diag_enable` set at runtime through `lran/bridge/config/set` (spec §16.6), both states | B4. **Met 2026-09-23**, §4.2a.1. This row said *"toggled from HA"* until 2026-09-24, §8.2 |
| W9 full-size and fragmented `PING` | `ping <id> 202 pattern` and `ping <id> <n> pattern frag` | B3a |
| Media access under real contention | **A second transmitter required** — the XIAO + Wio alongside a Heltec simnode, transmitting concurrently (§2.1, §2.3) | B3b |

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

#### 7.2.1 What BF-21 built, 2026-09-16

**`tools/simctl/` runs the §10.5 catalogue and judges it from `lran/bridge/diag/state`.**
The bridge has no console, so the verdict comes from the broker.

- **`catalogue.py` does no I/O.** `judge()` takes two counter documents and returns a
  verdict, so the deciding half is host-tested at a desk — the same split `scheduler.h`
  and `command.h` make. 17 host tests, no board and no broker.
- **One 60 s publication window per row.** Chosen over a force-publish topic, which would
  have put a control surface on the receive path that `/lib/lran-config/` and BF-26 should
  own. A full run is about half an hour, unattended by design.
- **`rx_frames` must move before any other check is believed.** A silent pass and a frame
  that never arrived are identical in every counter a forward-compatibility row cares
  about. It is a floor, not equality: poll answers land in the same window.
- **Only the row's own counter may move**, which is the invariant that caught
  `set_displaced`.
- **A known divergence is neither a pass nor a failure.** `bad_ver` reads 2 today and is
  reported as `DIVERGED` naming **BF-22**.
- **`tools/checks/simctl_catalogue.py`** fails when the scenario table and `fault.cpp`
  disagree. Two lists that must agree fail silently and in the reassuring direction, as
  §10.8.1's pin-map premise did; its own tests make it drift and assert it fails.

**Credentials come from `LRAN_MQTT_USER` / `LRAN_MQTT_PASSWORD`**, never arguments: an
argument reaches argv, and argv reaches the process table.

**Observe TX power before every session, in dBm conducted.** Bench work may run below the
D33 ceiling per §2.2, and a range test that silently ran at bench power has to be repeated.
Log the conducted figure rather than a RadioLib power index, so a trace taken on the Heltec
can be compared with one taken on the Wio.

---

## 8. Milestones and acceptance criteria

| # | Milestone | Depends on | Acceptance criteria |
|---|---|---|---|
| **B1a** | **RF path characterization** | Two Heltec boards, `lran-rangetest` (§11.2) | RSSI and SNR measured **at each node site on both the gate bearing and the well bearing** — **~87 m to the gate and ~100 m to the well**, not the ~500 ft this row guessed before anything was walked (Decision Register §5.1.1), across candidate SF/BW/CR settings. **D1 resolved** with a stated link margin — **done 2026-09-10**, though the register rather than B1a is where it landed. Bridge antenna type and position chosen and recorded — **type chosen 2026-09-10** (the range test's 3.0 dBi stick, Bridge PRD R-4.3a.1); **the position is still owed**. Airtime table regenerated (**M19**). FCC operating mode question (**W5**) settled before a TX power is fixed. **Margin figure carries an explicit "Heltec radio" caveat until B1b** |
| **B1b** | **Target-radio confirmation** | B1a, XIAO + Wio-SX1262 delivered | Range re-measured on the gate bearing with the **Wio-SX1262** at the B1a settings. Delta from B1a recorded — this is the module contribution to link margin. **D1 confirmed** or revised — **confirmed**, and B1b's own fade-tail result is what chose SF9 over SF7. §2.3.1 findings settled by measurement: whether an RXEN-style line is required, and the exact module part number. **PHY-CRC discard counters observed at the far edge of the link** (§10.5) |
| **B0** | **Simnode bring-up** | Second board in hand, `/lib/lran-protocol/` **P6 and P8** | `lran-simnode` flashes and runs. Identity table holds four entries with independent keys, contexts and sequence spaces. Serial console (§10.4) accepts every command. `ROLE_RANGE` echoes `PING`. Faults arm, fire the specified count and self-disarm, with armed state shown on the OLED |
| **B2** | **Board bring-up and OTA** | Board in hand | WiFi connects and reconnects; MQTT connects with LWT registered; A/B partitioning configured; OTA succeeds over WiFi; **a deliberately bad image rolls back**. Version published. OLED shows a status page |
| **B3a** | **Radio, registry, polling, availability and counters, with simnode** | B2 (**V-B9 re-run**), `/lib/lran-protocol/`, **B0** | Frames round-trip against the committed test vectors. **The bridge polls simnode identities on air and each answers** (BF-17): four logical simnodes from one board heard and polled simultaneously, each with its own learned context, **with poll-to-answer times recorded against `poll_reply_timeout_ms`**. Availability marks offline after 3 missed polls and online on the next frame (**V-B3**), and a production node's retained `offline` is seen at the broker. **Every §14 counter the console can drive at the bridge increments as specified**, one hand-run fault at a time, read from `lran/bridge/diag/state` at the broker (BF-19), and `hdr_rsv` is accepted rather than discarded. Full-size (a 222 B frame, which the console's `ping` takes as `n` = 202) and fragmented `PING` both round-trip between simnodes (**W9**) |
| **B3b** | **Command path, version tolerance and the scripted catalogue** | **B3a**, spec v0.12's answers for BF-18 and BF-19a | Each simnode identity's derived key verified by a command round-trip. Context resync retries once and then faults. **A suppressed ACK produces a retry with the same `seq`, and the simnode reports a deduplicated hit rather than a second execution.** Version tolerance accepts N−1 and rejects N−2 with a distinct reason. **The whole §10.5 fault catalogue runs from a committed `simctl` script.** *With a second simnode transmitter — the XIAO + Wio alongside a Heltec — two boards transmitting concurrently exercise CAD and backoff.* **Accepted 2026-09-17**, on the tasks confirmed on air the day before. **V-B12 moved to B4** the same day, with the operator — §8.1 says what it needs and why B4 is where that exists |
| **B4** | **MQTT, discovery and publication policy — no node hardware** | B3a | Discovery publishes one device per node, correct availability references, **and republishes on broker restart**. All §6.3 policy rules demonstrated: jitter suppressed, staleness marks unavailable, sentinels not published as numbers, synthetic marked, heartbeat republish works. **Events publish non-retained and do not replay on HA restart or discovery refresh.** The whole fleet is demonstrable with dummy publish and simulators only. **V-B12** measured — the one criterion here that needs a board, §8.1. **Met 2026-09-23**, §8.1.3. **Accepted 2026-09-24** with the operator, on §8.2's tally. **BF-33 moved to B4b** the same day |
| **B4b** | **PHY commit-and-revert** | B3b; BF-32's configuration path | Spec §12.4 (**D56**, **D59**) on the bench, bridge and simnode: one atomic `CONFIG` carries frequency, SF, BW, CR and TX power. **A revert survives a reboot in the middle of a trial.** **Only a frame received on the new settings confirms them**; a frame sent does not. Silence for `phy_trial_s` reverts both ends, and an `EVENT` follows once the link is back. **The fleet moves together.** TX power is clamped by D33 in the table. **Split from B4 on 2026-09-24** with the operator, §8.2. **Met on the bench on 2026-09-24**, across three identities on two boards; the bridge engineering log's *BF-33 slice 4 on air* entry has each run. **Accepted 2026-09-24** with the operator |
| **B5** | **HEX proxy** | B4, a real MPPT reachable via GateLink or a simulator | Read passes. Write rejected while disarmed, accepted while armed, **and the arm auto-expires with the switch published back to off**. Every attempt appears in the retained audit trail. Charge-parameter readback published as diagnostic sensors on boot. **Met on the bench on 2026-09-25** against BF-36's simulated MPPT; the bridge engineering log's *V-B6 on the bench* entry has the run. The readback reached `charge/state`, but HA shows its sensors for GateLink only (spec §16.6). **Accepted 2026-09-25** with the operator |
| **B6** | **GateLink integration** | B5, **B4b**, GateLink M6 | End-to-end with the real node: command round-trip, status decode, event delivery, per-node availability, diagnostics populated |
| **B7** | **Soak** | B6 | Continuous operation across broker restarts, WiFi outages and a node power cycle, with no lost frames on reconnect and no stuck availability state |

**Critical path:** B2 → B3a → B3b → B5 → B6 → B7, with B4 after B3a and **B4b after B3b, before B6**. GateLink has no OTA, so it must deploy with its half of §12.4, and B4b's bridge half is what tests that half first. **B3 was split into B3a and B3b on 2026-09-14** (with the operator), so that the half provable on the bench could be accepted and merged while BF-18 waits for spec v0.12; B1a/B1b is the precedent. **B1a is independent of all firmware
work and should be done first in wall-clock terms** — it needs only two Heltecs and
`lran-rangetest` (§11.2), gates GateLink's PHY configuration as well as this node's
antenna siting, and can start before a line of shared code exists.

**B0 depends on the protocol library reaching P6 and P8** (see
[`LRAN-Protocol-Library-Implementation-Plan`](../shared/LRAN-Protocol-Library-Implementation-Plan.md)
§6, which gates B0 on both), sits before B3, and can be built in parallel with B2. It is
deliberately separated from B3 rather than folded into it: if the simnode's own framing
is wrong, every B3 failure is ambiguous between the instrument and the thing being
measured. Prove the instrument first, against the committed test vectors (**W4**),
before using it to judge the bridge.

### 8.1 V-B12 moved from B3b to B4, 2026-09-17

**V-B12's saturated arm cannot be run on this firmware, and the two things it needs are
both built in B4.** That is the whole reason it moved; B3b is otherwise complete and was
accepted the same day.

**What V-B12 asks for** is bridge LoRa PER with WiFi idle versus saturated, against a
known frame sequence (**M22**, Bridge PRD §4.4). It is the evidence for R-4.4's
deliberate lack of LoRa/WiFi mutual exclusion, and PRD §4.4 names it as the falsifier of
that policy. Three parts, examined 2026-09-17:

| Part | State on this firmware |
|---|---|
| A known sequence at the bridge | **Available.** `fault <id> flood <count> gap <ms>` sends one correct CRC-sealed `STATUS` per injection, and `rx_frames` counts every frame the radio delivered — including a PHY CRC error, which `rx_ladder.cpp` counts as heard before discarding it |
| A WiFi load on the bridge | **Not available.** Nothing reaches `g_diag_interval_s` at runtime and the bridge has no console, so its WiFi transmits one diagnostic document a minute. `task_runtime.cpp` carries `TODO(BF-23)` on that atomic — **BF-23 is the task that makes the saturated arm possible**, and it is in B4 |
| Per-node RSSI and SNR at the broker | **Not available for a bench node.** `simnode_diag_enable` gates it (spec §16.6), and **BF-26** builds that gate. Also B4 |

**An inbound MQTT flood is not a substitute, and it fails in a direction that would not
show up in the result.** Flooding `lran/<id>/cmd/<action>/set` for an unregistered id is
refused at `CommandInbound` before any transmit, so it loads the bridge without
contaminating the radio — but it saturates WiFi *receive*, and R-4.4's third argument is
about WiFi *transmit* desensing LoRa receive. Measuring the wrong direction and recording
it as V-B12 would close the criterion without testing the claim.

**The idle arm is not blocked**, and it is the baseline the saturated arm is compared
against, so it is measured before B4 rather than with it. `tools/simctl/per_measure.py`
is the instrument, and it is the same instrument both arms use — `--arm` records which
one ran and changes nothing else. The engineering log carries what the idle arm measured.

**The saturated arm must run at a spacing whose idle PER is zero, and that is a measured
constraint rather than a preference.** The idle arm was swept across five spacings on
2026-09-17, on a clean bench at one metre with nothing corrupt at any of them:

| gap | 250 ms | 400 ms | 700 ms | 1100 ms | 2000 ms |
|---|---|---|---|---|---|
| PER | 5.6 % | 5.0 % | 1.0 % | **0 %** | **0 %** |

**`--gap 2000` is the comparable configuration** — 1100 ms is the measured knee and there is
no reason to sit on the edge of it. Run the saturated arm denser than that and a WiFi effect
cannot be told from the receive-path one.

**The knee is not at frame airtime, and that is a separate finding this plan does not own.**
It straddles `kIrqReadMs` (1000, `lora_link.cpp`). The engineering log's 2026-09-17 entries
carry it; the dense case stays runnable because it is what made the question visible.

> **What would falsify the move rather than the policy.** If BF-23 lands without a
> runtime path to `g_diag_interval_s`, the saturated arm has no lever again and V-B12
> needs its own task rather than a criterion. Checked by reading that `TODO(BF-23)` is
> gone from `task_runtime.cpp` when BF-23 is accepted, and by `per_measure.py --arm
> saturated` returning a valid window.
>
> **The first check passed on 2026-09-23**, when BF-23's lever half was built: no
> `TODO(BF-23)` remains in `task_runtime.cpp`, and `diag_interval_s` reaches
> `g_diag_interval_s` (§4.4.2). A set moved `lran/bridge/diag/state` from 60 s to 20 s spacing
> on the bench the same day. **The second check never ran, because §8.1.2 replaced the
> lever.** The blaster table in `sweep_interleave.py` took its place, and §8.1.3 records
> the run.

**§8.1.2 replaces the lever above.** `diag_interval_s` reaches its consumer, but its 10 s
floor cannot load WiFi. The saturated arm's load is a bench-only UDP blaster instead.

**§8.1.1 corrects two things above**, measured 2026-09-21: `--gap 2000` is not a zero-PER
configuration, and the spacing effect this section assumes is real was in doubt when this
section was written. The table and the paragraphs above stand as taken on 2026-09-17.

### 8.1.1 The interleaved sweep, 2026-09-21: spacing is a variable, and so is something else

**Two interleaved sweeps put the spacing effect back on the evidence, and correct §8.1's
assumption that a 2000 ms gap loses nothing.** The engineering log's 2026-09-21 entry
carries the numbers; this section carries what they change for V-B12.

**Why an interleaved run was needed.** Every sweep before it ran one spacing to completion
before starting the next, so a slow change in the environment and an effect of spacing
produced the same table — and on 2026-09-17 the two readings disagreed, with a 2000 ms
control losing 8 % and a 250 ms arm losing nothing. `tools/simctl/sweep_interleave.py`
alternates the arms inside one session and pools the frames by arm, by position in the
session, and by both at once. The cross-tab is the one that separates them: an arm ordering
that holds inside both halves has survived the passage of time by construction.

| Pooled over both sweeps, 1280 frames | 250 ms | 2000 ms |
|---|---|---|
| Lost | 25 of 640 | 2 of 640 |
| PER | **3.91 %** | **0.31 %** |

**The denser arm lost more in all four run-by-half cells**, so the effect is not an artifact
of when the bursts ran.

**What changes for the saturated arm.** `--gap 2000` remains the comparable configuration
and the reasoning for it is unchanged, but **"a spacing whose idle PER is zero" is not
available on this bench** — 2000 ms measured 0.31 %, and both of those losses sat in a gap
containing a bridge transmission. **So the saturated arm needs an idle control in the same
session rather than a remembered zero**, which is what this instrument already does: run
both arms of V-B12 interleaved, not in blocks.

**What is not settled.** The dense arm's rate moved by a factor of four *within* each
session — up in run 1, down in run 2 — while the sparse arm held still. That second variable
is unidentified, and it is why a single block-ordered sweep can land anywhere between 0 %
and 8 %. Only two spacings ran, so §8.1's 1100 ms knee is neither confirmed nor moved.

### 8.1.2 The saturated arm's load is a UDP blaster, 2026-09-23

**`diag_interval_s` is not the saturated arm's lever, although it reaches its consumer.**
Its floor is 10 s (`lib/lran-config`'s row `0x0002`), and at the floor `sched_diag()`
sends three small documents every 10 s. M22 and PRD §4.4 ask for a sustained flood.
Running the saturated arm on this lever would close V-B12 without testing R-4.4's claim,
which is the failure §8.1 already names for an inbound MQTT flood.

**The load is `src/blaster.{h,cpp}`, in its own environment, `v_b12_blaster`.** It sends
UDP to the broker host's discard port at a rate given at run time, and counts the packets
the stack accepted and the ones it refused. It is controlled from the bridge's USB serial
port with `blast <kbps> [bytes]` and `blast 0`. That keeps it off MQTT and out of the
configuration table: every topic and row belongs to the protocol specification. The image
is otherwise the production build. It is never deployed.

**`sweep_interleave.py --bridge-port <port> --gaps 2000 --blast-kbps <rate>` runs both
arms**, interleaved as §8.1.1 requires. The two arms share one gap and differ in load
alone. The tool opens the bridge's port once and holds it, because opening it reboots the
bridge. Each loaded burst records the blaster's achieved rate beside its frame count.

**The frame log still counts the frames, and the bridge publishes it over the loaded
link.** A load that backs up `log_task` overwrites the ring, and the tool then reports
`RING OVERWROTE` and spoils the burst. The blaster stops before each burst's drain, so the
last batches travel an idle link. The rate for a run is chosen on the bench: the highest
rate at which no loaded burst overwrites the ring.

> **What would falsify this lever.** A loaded burst whose achieved rate falls far below
> the rate asked for had no load, whatever its PER. Checked on every run by the tool's
> per-burst blaster table.

### 8.1.3 V-B12 measured, 2026-09-23: saturating WiFi cost the bridge no measurable PER

**V-B12 is met.** The bridge's WiFi carried 16 to 19.5 Mbps of UDP, and its LoRa PER did
not measurably rise. Two interleaved sweeps ran at a 2000 ms gap, six pairs each, flooding
`f3` from the XIAO with the Heltec simnode quieted. The engineering log's 2026-09-23 entry
has the run detail. The frames are in `docs/bridge/data/sweep-vb12-2026-09-23-run1.json`
and `-run2.json`.

| Pooled over both sweeps | Idle | Saturated |
|---|---|---|
| Sent | 482 | 480 |
| Lost | 0 | 2 |
| PER | **0.00 %** | **0.42 %** |
| RSSI, median and range | −22 dBm, −35 to −21 | −22 dBm, −35 to −21 |
| SNR, median | +11 dB | +11 dB |

**Both losses fell in one loaded burst, and both carry the idle arm's signature.** Each sat
in a 4000 ms gap with a bridge transmission inside it, as both of §8.1.1's idle losses at
2000 ms did. §8.1.1 measured 0.31 % over 640 idle frames at the same gap, so 0.42 % is not
a difference. Two losses are also too few for the tool to order the arms; it draws that
line at eight.

**Every loaded burst carried its load**, by §8.1.2's falsifier. Run 1 achieved 19191 to
19521 kbps and run 2 achieved 16346 to 17849 kbps, against 20000 asked. The frame log's
ring never overwrote.

**The first attempt showed the falsifier working.** The IoT network had moved from channel
6 to channel 1 since the calibration pair. Two loaded bursts achieved 1996 and 473 kbps,
each with more than 70000 sends refused, and the run was stopped after three bursts. Once
the operator pinned the nearest access point to channel 6, the load held.

**What this confirms.** R-4.4's policy stands: the bridge does not gate LoRa against WiFi,
and a saturated WiFi transmitter cost it no frames that the idle arm would not also have
lost. R-4.4c's fallback, antenna separation, is not called for. M22 closes.

**What it cannot show.** Every frame arrived at about −22 dBm, far above the SF9
sensitivity floor, with SNR steady at +11 dB in both arms. A rise in the noise floor
smaller than that margin would cost nothing on this bench and could still cost frames at
87 m. So the bench rules out a gross coexistence failure, not desense near sensitivity.

> **What would reopen M22.** A node's PER at the gate that rises with the bridge's WiFi
> traffic. R-4.4b's published PER and RSSI/SNR are the check, once GateLink is deployed.

### 8.2 B4's acceptance tally, 2026-09-24

**All eight of B4's criteria are met, and B4 was accepted on 2026-09-24.** Six were met on
their evidence. The operator decided the other two, V-B11 and the bench gate, in the same
session, and moved BF-33 out to a milestone of its own, B4b. Two runs that day supplied new
evidence. The first read the retained discovery set at the broker and Home Assistant's
device list. The second ran V-B4 with a broker restart. The engineering log's *B4's
acceptance tally* and *V-B4 passes* entries have them. The rest of the evidence is in the
2026-09-23 and 2026-09-24 entries.

| Criterion | Verdict | Evidence |
|---|---|---|
| Discovery publishes one device per node | **Met** | 115 retained configs at the broker, and the same seven devices in HA: the bridge (6 entities), GateLink (53), WellLink (8), and `simnode0`–`3` (12 each) |
| Correct availability references | **Met** | Every config lists its own node's `availability` topic. The 20 solar and battery entities also list their block's document, with `avty_mode: all`, and went `unavailable` in HA when that document said so (§6.6.2) |
| Republishes on broker restart (**V-B4**) | **Met** | A subscriber held through a Mosquitto restart received all 67 production configs from the bridge with the retain flag clear, 3 to 5 s after the broker came back, beside the broker's own stored copies. The 48 simnode configs came back as stored copies only, because `simnode_diag_enable` is off |
| All §6.3 rules (**V-B7**) | **Met** | BF-27's dummy publish, §6.6.2. A 3 mV cell step was withheld inside the deadband, `na` went out as `null`, a stale block as unavailable, every document carried `synthetic: true`, and the heartbeat resent all five documents after `republish_interval_s` |
| Events non-retained, no replay (**V-B8**) | **Met** | §6.6.2, on synthetic events, through two HA restarts, an integration reload and a broker restart |
| The whole fleet from dummy publish and simulators (**V-B11**) | **Met for GateLink**, by operator decision | GateLink's documents and events came from the dummy publish, and the four simnode identities from the bench gate |
| **V-B12** | **Met** | §8.1.3 |
| §16.6 bench gate, both states (§7.1) | **Met**, by operator decision | Both states, and a reboot in each, on 2026-09-23 (§4.2a.1), set through `lran/bridge/config/set` |

**V-B11 is met on GateLink, and the simulators do not gate it.** Bridge PRD R-5.4c has
required per-node simulators since its first draft, and §6.6 gives them one row, with no
behaviour beyond *"bridge-side generators per node type"*. The dummy publish already
drives the MQTT path those generators would test, and V-B7 and V-B8 passed on it in HA.
So V-B11's purpose holds for GateLink: HA integration is not blocked behind a workbench.
What a simulator would add is an unattended, time-varying source and a second node type.
WellLink's status schema, `0x20`, is reserved and not defined in the specification, so
neither tool can publish WellLink data yet. The simulators stay with BF-27 as unbuilt work.

**The bench gate is met on `config/set`, and §7.1's row now names it.** The row said
*"toggled from HA"*, after spec §16.6's *"settable at runtime from HA over
`lran/bridge/config/set`"*. That section's reason is a flag switched on a running bridge
with no reflash and no separate build, and the 2026-09-23 run met it. A toggle from HA today
would send the same message through HA's `mqtt.publish` action and show nothing new about
the bridge. **The gap behind the wording is that no configuration row has an HA
control.** D44 has HA `number` discovery read the configuration table, and Bridge PRD §6.2
lists a per-node poll interval `number`. Neither is built. Firmware Tasks **BF-35** owns
them, and no milestone gates on it yet.

**BF-33 is now B4b.** Its row in §8 carries the criteria from BF-33's own reasoning. B4b
precedes B6, because GateLink has no OTA and must deploy with its half of §12.4, and the
bridge half is the only thing that can test it first.

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

**Schema `0xF0` carries no `status_reason`** (Protocol Spec §7.5), so it cannot be marked
`DEBUG_SYNTHETIC`. Every `0xF0` a simnode emits sets `health_flags` bit 0, "any debug mode
active", instead. Found building B0, 2026-09-14: the rule above was written for status
schemas, and the health schema has one field that can carry it.

### 10.2 Roles

One firmware, four selectable roles. A role is a **behaviour profile**, assigned per
logical identity at runtime (§10.4), not a separate binary.

| Role | Answers | Purpose | Milestone |
|---|---|---|---|
| `ROLE_RANGE` | `PING` echo, `0xF0` on poll | Range test and link characterization. Minimal, so a failure is unambiguously RF | **B1** |
| `ROLE_HEALTH` | `0xF0` on poll | The generic node — what WellLink looks like before it has a schema. Registry, availability and scheduling filler for multi-node tests | **B3** |
| `ROLE_GATELINK` | `0xFE` on poll, `0x11` events, `COMMAND_ACK`, `0x12` config, `CONFIG_CHANGE` in the poll answer after a PHY revert (spec §8.7, **D69**), and `HEX_RSP` from a simulated MPPT (§10.9.3, **BF-36**) | The full peer. Exercises command retry, dedup, event dedup, config ACK semantics, the HEX proxy's three gates and the whole decode path with no gate present | **B3**, **B4**, **B5** |
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
| `reboot` / `reboot panic` | Reset the whole board through `esp_restart()` or `abort()`. Every identity boots again, and the chip reports `SOFTWARE` or `PANIC` (spec §8.14) — added 2026-09-26 |
| `reboot <hex> [cause]` | Simulate one `ROLE_GATELINK` identity's reboot: a new context, then the `BOOT` status and a `BOOT` event carrying `cause`, `SOFTWARE` by default. The board's other identities keep running — added 2026-09-26 |
| `push <hex> [reason]` | Emit an unsolicited status with a given `status_reason` |
| `event <hex> <type>` | Emit an event; repeat the same `event_id` to test bridge-side dedup |
| `ack <hex> <mode>` | `normal` \| `suppress` \| `delay <ms>` \| `dup` — command-path tests |
| `ping <hex> <n> [pattern] [frag [<chunk>]] [to <hex>]` | Emit a `PING` of length `n`, optionally `PATTERN_FILL`, optionally forced-fragmented (**W9**; a bare `frag` is spec §6.6.2's 14-byte chunk). `to` sets the destination, `00` by default, so two simnodes can echo each other — added 2026-09-14 |
| `stats <hex>` | One identity's §14.1 counters, plus the radio's `cad_backoffs` — added 2026-09-14 |
| `radio` | The driver's own counts: `TX_DONE`, TX errors and timeouts, forced transmissions, CAD errors. The only evidence on the board that a frame reached the air — added 2026-09-14 |
| `fault <hex> <name> [count]` | Inject a fault from §10.5, once or `count` times |
| `field <hex> <name> <value>` | Override a generated telemetry field — sentinels, out-of-range, stale flags |
| `mppt <hex> <mode>` | The simulated MPPT behind a `ROLE_GATELINK` identity: `list` \| `set <reg> <value>` \| `timeout [count]` \| `hex_timeout <ms>` \| `reset` — HEX proxy tests (§10.9.3), added 2026-09-25 |
| `log <level>` | Serial verbosity |

`/tools/simctl/` scripts these into repeatable scenarios so a regression run is one
command rather than a remembered sequence of keystrokes. **Scenario scripts are committed**
— an ad-hoc bench procedure that lives in someone's head is not a regression test.

### 10.5 Fault catalogue

Each entry drives one stage of Protocol Spec §14 or one rule in §9.4/§10. **This is the
only mechanism that produces these frames**, and without it every discard counter in the
bridge ships unverified.

**Built by BF-8, 2026-09-14: `firmware/simnode/fault.{h,cpp}`.** `fault <hex> <name>
[count] [gap <ms>] [to <hex>] [ctx <hex32>]` arms `name` on one identity; the first
injection fires at once and the rest as the outbox drains, then the fault self-disarms
(§10.6 rule 2). An injection is the whole frame sequence the row describes — `bad_ver` is
two frames, `set_displaced` two, `single_frame_interleave` four. Every malformed frame is
built with `lran::sim::FramePatch` (BF-7); the carrier is a real schema `0xF0` health
status, so each fault differs from an accepted frame in exactly the way its row states, and
authenticated faults carry `COMMAND(NOP)` so a receiver defect that accepts one moves
nothing. `silent` withholds the identity's next `count` answers instead of sending. The
five command-path entries (§10.5.1's two plus `ack_suppress`, `ack_dup`, `event_replay`)
answer `ERR` naming **BF-6**, and `bad_phy_crc` is refused as uninjectable (§10.5.2). The
host suite in `test/test_fault` feeds each fault into the codec's own receive ladder and
asserts that the §14 counter its row names — and only that counter — moves; the
forward-compatibility rows (`hdr_rsv`, `seq_wrap`, `single_frame_interleave`) assert that
`rx_dropped` does **not** move.

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
| `set_displaced` | A live set, then a **complete** second set from the same peer | `rx_reassembly_abandoned` | Protocol Spec §11.3 displacement, scoped to `frag` total > 1. **BF-21 decided it: `fault.cpp` completes the displacing set**, so the row moves one counter like every other. Until then the displacing set was a lone fragment 0, which expired on the tick and also moved `rx_reassembly_timeout` — measured twice on 2026-09-16 — making this the one row to break the table's own invariant. A row that moves two counters cannot tell a displacement defect from a timeout defect, and `frag_timeout` owns the second. Confirmed at the broker 2026-09-16: `rx_reassembly_timeout` flat |
| `bad_mac` | Authenticated frame, one MAC byte flipped | `rx_rejected_mac` | §9.4 step 3 rejection, **no state change**, and the fragment is **not buffered** |
| `ctx_jump` | New `ctx_id` mid-session with no reboot | `rx_rejected_ctx` on the node side | Bridge adopts, resets `cmd_seq`, **retries once only** (§6.2) |
| `seq_jump` | Large forward `seq` step | — | Accepted — RFC 1982 arithmetic, no lockout |
| `seq_wrap` | `seq` wrapping through `0xFFFF` | — | Accepted. **The failure this guards against is a plain `>` comparison rejecting every frame until reboot** |
| `ack_suppress` | `COMMAND` received, no ACK | — | Bridge retries with the **same `seq`**; simnode reports a dedup hit (**V-B5**, **BS-3**) |
| `ack_dup` | Two ACKs for one command | — | Second ignored, not counted as a second result |
| **`ctx_reject`** | `COMMAND_ACK(REJECTED_CTX)` to the next `count` `COMMAND`s, whatever context they carried | — | **Added by BF-21**, and the only way to reach Protocol Spec §10.3 **step 3** deliberately: `ctx_reject 2` makes the bridge resync once, meet a second rejection, and **stop rather than loop**. **Since spec v0.16 it needs a command §10.7 lets the bridge resync**, such as `request_status`. An actuation command or a `reboot` ends `unconfirmed` at the first rejection (D70), which is the fault's other use. It acts **before the dedup gate** — §9.4 checks context at step 2 and the gate at steps 4–6, so no `seq` is consumed and no result is cached; cached, the resync retry would meet a `DUPLICATE_CACHED` instead of the second rejection. BF-18 failed three times to force this by racing the console, because the rejection-to-retry window is under a second |
| `event_replay` | Same `(ctx_id, event_id)` twice | — | Bridge publishes **once** (Protocol Spec §16.3) |
| `flood` | Frames at maximum rate | — | Bridge stays responsive; `lora_task` does not block (§1.3) |
| `silent` | Identity stops answering | — | Availability → offline after `missed_poll_threshold` (**V-B3**) |

**A row that emits more than one frame cannot confirm its own ERROR from the same board.**
Measured 2026-09-16 on `bad_length` and on a four-frame `unknown_type` burst. The bridge
answers at once and cannot receive while it transmits, so the row's next frame arrives into
a deaf receiver; the sender is transmitting that frame, so it cannot hear the reply. Both
losses are the same half-duplex property and neither end is at fault. **`gap` spaces
injections, not the frames inside one injection**, so the spacing that makes single-frame
rows clean does not reach inside a multi-frame one. **BF-21 has to read those rows from the
bridge's counters, or drive the row from one board and listen on a second.**

**`single_frame_interleave` is the highest-value entry in this table**, and the only test
of spec v0.6's sole behavioural change. §11.2 states that a single-frame frame never
begins, joins, displaces or expires a set. The defect it fixes is a receiver routing every
frame through one slot per peer, where **a node's periodic `STATUS` destroys that same
node's in-progress fragmented set**. The v0.6 example was a fragmented `CONFIG_ACK`,
which spec v0.12 made impossible (D38); `PING` is now the only fragmentable type, and the
defect is the same for it. It is silent by construction: the offending frame belongs to no set, so nothing
is counted. That is why the expected result is a *set that completes and a counter that
does not move*, and why no other entry can substitute for it.

#### 10.5.1 Two more once **P8** lands — and the direction reverses

**D34**'s `CommandGate` is the receiver of these, so they test the **simnode's own** gate,
driven from `simctl` rather than from the bridge. They belong here because §10.5 is the
document of record for what the fleet's discard paths are verified against, and because
per Protocol Spec §9.2 **every authenticated type is bridge → node** — so these two paths
exist *only* on a node and would otherwise be verified nowhere.

| `fault` name | Produces | Counter | Expected node behaviour |
|---|---|---|---|
| `cmd_replay` | The same `(ctx_id, seq)` `COMMAND` twice, after the first has executed | `rx_dup_command` | The **cached** ACK is returned and **the relay does not pulse a second time**. Excluded from `rx_dropped` — the retry mechanism working as §10.4 requires is not a fault |
| `cmd_stale_seq` | A `COMMAND` whose `seq` is below the high-water mark | `rx_rejected_seq` | `COMMAND_ACK(REJECTED_SEQ)`, counted **into** `rx_dropped` |

**`cmd_replay` sends its second copy after the first has executed.** A copy that arrives
*during* execution receives nothing (Protocol Spec §9.4, v0.11) and is covered by library
P8's `test_retry_inside_the_window_never_executes`. It reaches a simnode only if
`ROLE_GATELINK` executes on another task, which §10 does not require.

`ack_suppress` above already exercises the dedup path, but it asserts it from the
bridge's side. `cmd_replay` asserts it **at the gate, where the relay is** — which is the
assertion root rule 2 and **BS-3** actually care about. A second pulse at a driveway gate
is the failure this whole mechanism exists to prevent, and it has never been tested at
the end that pulses.

#### 10.5.2 `/lib/lran-sim/` needs a post-encode patch primitive

Three of the new entries cannot be built by post-processing a correct frame the obvious
way: `oversize` needs a frame longer than `encode()` will emit, `frag_zero` needs a `frag`
byte the encoder overwrites, and `frag_command` needs a `frag` total on a type
`encode_fragment()` refuses with `NotFragmentable`. A narrow **patch-after-encode**
surface — set a header byte, extend or truncate the trailer, recompute or deliberately
skip the CRC — keeps simnode rule 2 intact (**never a second serializer**) while making
them reachable. Scope it before B0 rather than during it; discovering it mid-milestone is
how a second serializer gets written.

**Built by BF-7, 2026-09-14: `lran::sim::FramePatch`** in
`lib/lran-sim/include/lran/sim/frame_patch.h`. It works on a caller-owned buffer of
`kPhyMaxFrame` (255) bytes.

| Step | Operations |
|---|---|
| Start | `encode()` or `encode_fragment()`, the codec's own functions and arguments. FramePatch writes no frame of its own |
| Patch the header | `set_header(HdrByte, value)` for the single-byte fields `ver`, `type`, `src`, `dst`, `frag`, `schema`, `hdr_flags` and the three reserved bytes; `set_frag(index, total)`, packed by `Header::set_frag` |
| Patch the body | `set_payload(i, v)`; `resize_payload(n, fill)`, which moves the MAC and CRC; `strip_mac()`; `flip_mac(i, mask)`; `truncate_body(n)` |
| Seal | `seal(Seal::Crc)` or `seal(Seal::MacAndCrc)`, then optionally `flip_crc(i, mask)` |

| Fault | Patch |
|---|---|
| `runt` | `truncate_body` |
| `oversize` | `resize_payload`, up to 255 bytes |
| `bad_crc` | `flip_crc` |
| `bad_ver`, `wrong_dst`, `crit_ext`, `hdr_rsv`, `unknown_type`, `unknown_schema` | `set_header` |
| `frag_zero`, `frag_command` | `set_frag` |
| `bad_length` | `resize_payload` |
| `bad_mac` | `flip_mac` |

The remaining faults are sequences of correct frames, and BF-8 sends them with `encode()`.

Three properties hold the primitive to rule 1:

- **Every patch unseals the frame.** `frame()` returns `nullptr` until `seal()` runs. A
  forgotten reseal therefore yields no frame, rather than a frame that also fails its CRC,
  which stage 3 would count and so hide the stage the fault targets.
- **The only layout it states is the `HdrByte` offsets**, and a test checks each one by
  patching it and reading the frame back through `decode_header()`. `seq` and `ctx_id` have
  no patch: the faults that need them are correct frames.
- **The W4 negative vectors check it.** `tools/vectors/generate.py` built them by editing a
  correct frame and resealing it, which is this primitive's model, in code that never read
  it. `test_frame_patch` rebuilds 18 of the 21 from `encode()` and requires identical bytes.
  Of the other three, two are correct frames and the third repeats one of the 18. Changing
  one offset in `HdrByte` fails two tests, which shows the comparison is sensitive to it.

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
   alternative is guessing about a node ~87 m away that has been outdoors through two
   winters.
5. **Cold spare.** The bridge is the single point of failure for all property telemetry.
   A board already flashed, already on the bench and already known to work is a
   fifteen-minute recovery instead of a shipping wait.

**Retire it only when there is a second permanently-installed bench peer**, which in
practice means: keep one.

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
build_flags = -DLRAN_PROFILE_XIAO_WIO_KIT
```

The flag names the **Kit**, as the profile below does: a flag that said only `XIAO_WIO`
would fit the header board too, which is GateLink's module and has a different map.
Corrected 2026-09-14, when `firmware/simnode/platformio.ini` was written from this sketch.

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
  float  tcxo_v;         // built as uint16_t tcxo_mv - bridge radio_config.h, BF-16
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

### 10.9 What B0's first slice built, 2026-09-14

**BF-2, BF-3, BF-5 and the core of BF-4.** Two Heltecs running it completed every PING
round trip §6.6 defines on the D1 PHY: 8 bytes, the 222-byte frame, and the 15-fragment
set in both directions. The engineering log has the transcript.

| Choice | Why |
|---|---|
| **§12.3 media access and the PHY constants live in `lib/lran-link/`**, shared with the bridge | Two copies of a backoff rule drift. The cost is that B0's branch is stacked on B3's |
| **The simnode reads `LRAN_MASTER_KEY` from the root `secrets.h`**, and nothing else | The simnode and the bridge derive the same keys. CI builds against the template, as for the bridge |
| **Every enabled identity decodes every frame, with itself as `self`** | A frame for another identity is `rx_not_addressed`, exactly as on a second board, so four identities count what four boards would |
| **The identity table, protocol engine and console are Arduino-free**, 31 host tests | Only `radio.cpp` and `main.cpp` need a board. Identity keys are checked against the W4 vectors |
| **`radio.cpp` follows the bridge's `lora_link.cpp`** | Same RadioLib traps: no `scanChannel()`, no blocking `transmit()`, no `getPacketLength()` as an arrival test, no heap `Module` |
| **Boot identities: Heltec `0xF0 ROLE_RANGE` and `0xF2 ROLE_HEALTH`; XIAO `0xF1 ROLE_RANGE`** | §10.8.1's assignment, with `ROLE_RANGE` standing in for the roles that do not exist yet. Nothing persists |
| **A frame set is queued whole or not at all** | A fragmented echo missing its tail would time out at the far end and read as RF loss |

**What this slice does not do:** `ROLE_GATELINK`, `push`, `event`, `ack` and `field`
(BF-6); the fault catalogue and `fault` (BF-8); self-disarm and the OLED (BF-9).
**The XIAO profile built and had not been flashed** when this slice landed; it first ran
`simnode-xiao-wio` on 2026-09-15, at B0's acceptance. `/lib/lran-sim/` (BF-7) followed the
slice and is described in §10.5.2; the simnode does not call it until BF-8.

> **A gap on the bridge, not the simnode.** §17.3 makes RF loopback "required of every node
> build", and no `BF-*` task gives it to the bridge. Until one does, a simnode's PING to
> `0x00` reports no echo.

#### 10.9.1 The OLED page (BF-9)

`oled_page.{h,cpp}` builds the page as text, and it is host-tested. `ui.cpp` draws it with
the bridge's ThingPulse driver at the bridge's pinned version. The panel's pins are a
`PanelPins` struct in `profiles.h`, checked against the radio's pins at compile time. Both
profiles have one: the Heltec's own panel, and the XIAO's on the Seeeduino XIAO expansion
board (SDA 5, SCL 6, no reset line, no Vext), with the values the range test confirmed.

| Row | Shows |
|---|---|
| 0 | The last frame received: `src>dst`, type, RSSI and age. Otherwise `<len>B <rssi> drop`, `phy crc error`, or an inverted `RADIO DOWN` |
| 1–4 | One identity per row, with its exact role token and `off` when disabled. **An armed fault replaces the row as an inverted bar** with the injections left, or the answers left for `silent` |

#### 10.9.2 `ROLE_GATELINK` (BF-6)

`gatelink.{h,cpp}` holds the role, and `test_gatelink` its host suite. Four points left open
by §10.2, §10.4 and §10.5.1 were decided with the operator on 2026-09-14:

| Point | Decision |
|---|---|
| Synthetic marking | **Schema `0xFE` is the marker.** `push <hex> [reason]` takes any spec §8.7 name and defaults to `DEBUG_SYNTHETIC` |
| `CONFIG` without `/lib/lran-config/` | **A generic RAM store** of 21 entries: any `param_id` with a consistent `ptype` and `len`. Every override is RAM-only, so a write that took effect reads `APPLIED_NOT_PERSISTED`, a `SET` with every entry rejected `NOT_APPLIED`, and a read with no override held `PERSISTED` (spec §7.4, D53). `RESTORE_DEFAULTS` empties the store and answers as `GET_ALL` does (D52). No `param_id` is declared here |
| `ack` modes | `suppress [count]` and `dup [count]` **arm the bounded `ack_suppress` and `ack_dup` faults**. `delay <ms>` is a setting that lasts until `ack <hex> normal` |
| `cmd_replay`, `cmd_stale_seq` targets | **A target on the same board is fed through the node's receive path and never transmitted.** Another board's target needs `to <hex> ctx <hex32>`, and `seq <n>` when its high-water mark is above zero |

**What a command does on a simnode.** No output exists. `NOP`, the settings commands and the
actuation commands answer per spec §8.2, and an actuation command is counted in `actuations`.
`SET_RELAY_DRY_RUN 1` turns later actuations into `DRY_RUN`. `REQUEST_STATUS` and
`REQUEST_CONFIG` follow their ACK with a status or a readback. **`REBOOT` with `0xA5` resets
the board** once its ACK is on the air (spec §8.1), so every identity on it boots again.
Each boot, of any cause, gives every enabled `ROLE_GATELINK` identity a `BOOT` status and
then a `BOOT` event carrying the reset cause (spec §10.7, §8.14). An `RTC_NOINIT` marker
separates `REBOOT_COMMAND` from `SOFTWARE`, and NVS keeps the board's `boot_count`. **`ROLL_CONTEXT` is answered by every role**
since BF-34 (spec §10.6, §6.2.2). It skips the gate and answers `ACTUATOR_BUSY` while a
command is in flight. Otherwise it takes a new `ctx_id` and resets the gate and the status
`seq`, and nothing else. The `ACCEPTED` ACK goes out under the new `ctx_id`.

The engineering log's BF-6 entry records three questions for spec v0.12: how a
`DUPLICATE_CACHED` ACK carries the cached result, what answers a repeated `CONFIG`, and
§7.4's reliance on fragmentation that §3.1's cap on a reassembled set rules out.


#### 10.9.3 The simulated MPPT (BF-36), 2026-09-25

**`ROLE_GATELINK` answers `HEX_REQ` from a simulated MPPT**, so B5 can run at a desk.
§10.2 had it answer none, which left B5 waiting on GateLink although §8 lets it run
against a simulator. `sim_mppt.{h,cpp}` is the MPPT and `gatelink.cpp`'s `on_hex_req()`
is the node around it. The HEX frame codec is `lib/vedirect/`, shared with the bridge's
readback (BF-30).

**The node stays transport only (spec §7.6); the MPPT holds the registers.** The node
inspects the command nibble and the leading colon, and nothing else. The MPPT holds twelve
registers from Victron's battery-settings table, set to a LiFePO4 profile as GateLink PRD
R-6.1b asks. A Set accepted while armed can therefore be read back changed, which is the
only evidence V-B6 has that a write reached anything. The values are plausible, not
GateLink's pack specification.

**Four answers follow a reading of the specification decided with the operator on
2026-09-25**, and raised for spec v0.16 because the text does not settle them:

| Case | Answer | Why |
|---|---|---|
| Write-class, MAC absent or wrong | `HEX_RSP(REJECTED_UNAUTHENTICATED)` | Spec §8.13 names it for this case; §9.4 step 3 names `COMMAND_ACK(REJECTED_MAC)` |
| Write-class, wrong `ctx_id` | `COMMAND_ACK(REJECTED_CTX)` | That ACK carries the node's own `ctx_id`, which the bridge's resync needs (spec §10.3) |
| Write-class, replayed or stale `seq` | `COMMAND_ACK(DUPLICATE_CACHED)` or `(REJECTED_SEQ)`, not forwarded | §9.4 applies steps 4–6 to every authenticated type. `on_config()` answers the same way |
| Any answer | `HEX_RSP` repeats the request's `seq` | Correlation is by `seq` (spec §9.2), as a solicited `CONFIG_ACK` does (§7.4.1) |

**Two statuses come from the node, the rest from the MPPT.** A string with no colon or no
hex command digit is `MALFORMED_REQUEST` and never reaches the MPPT. A shaped string with a
bad checksum does reach it, and the MPPT answers Victron's frame error, `:4AAAAFD`, under
`OK`. A second request while the node waits on the MPPT is `BUSY`. A Restart, which
Victron's MPPT never answers, and the `timeout` fault are both `TIMEOUT` after
`hex_timeout_ms` (default 1000), with `mppt_flags` bit 2 set in any `STATUS` meanwhile.

**A node reboot clears the transaction and keeps the registers**, because a GateLink reboot
does not touch the MPPT.

---

## 11. Development environment and workflow

### 11.1 Build order

| Order | Work | Gate |
|---|---|---|
| 0 | **B1a range walk, Heltec ↔ Heltec** (§11.2) | None — starts today, no shared code |
| 1 | `/lib/lran-protocol/` **P1–P6**, incl. W4 vectors | Own plan document |
| 2 | `simnode` **B0** | P6 **and P8** |
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
and has to be done again after the next iteration. A dev instance can be reverted.

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

- **v0.70** — **The simnode meets spec v0.16's reset obligations.** §10.9.2: a `REBOOT`
  resets the board through `esp_restart()` once its ACK is on the air, and every boot sends
  a `BOOT` status and a `BOOT` event with the reset cause. `ctx_id` is drawn with the
  entropy source on, and `boot_count` comes from NVS. §10.4 adds `reboot`, `reboot panic`
  and `reboot <hex> [cause]`.

- **v0.69** — **BF-11a and BF-11b are built**, new §5.2.2: the leveled log's queue, with
  `sched_task`'s and `lora_task`'s lines moved onto it, and the task watchdog fed from
  `sched_task` at 10 s. `mqtt_task` logs its stack high-water mark.

- **v0.68** — **Three restart edges are closed.** New §6.7.8: a committed PHY change's
  `config/ack` is owed in NVS until `mqtt_task` has sent it, a `restore_defaults` no
  longer clears an open trial's marker, and a boot withdraws a bench `online` the last
  boot left retained. §4.2a.1's gap paragraph and §6.7.2a's boot-count note are updated to
  match.

- **v0.67** — **Protocol specification v0.15 → v0.16.** What the bridge inherits is
  **D70**: §6.2's resync no longer retries an actuation command or a `REBOOT`, which now
  ends `unconfirmed`, and §6.4 does not resync a VE.Direct Restart. Both are built on this
  revision. The other v0.16 changes are node obligations: `REBOOT` acknowledges then resets,
  a `BOOT` event's `reset_cause` (D71), active alarms sent again at boot (D72), and a
  `ctx_id` from true entropy. The bridge publishes a node's event `detail` as it does today,
  so `reset_cause` reaches Home Assistant as a number.

- **v0.66** — **A node reboot owes a readback.** New §6.7.7: the bridge reads a reboot
  from a node's `STATUS`, never from a new `ctx_id`, and asks for a readback, so
  `config/state` stops reporting overrides a reboot cleared.

- **v0.65** — **Three air-timing defects from B4b's bench runs are fixed.** §6.1.1's
  exchange row now says every exchange holds every other, and that a PHY change's step-6
  `POLL`s go one at a time. A new row says a reply window opens when its frame leaves
  `lora_task`.

- **v0.64** — **B5 is met on the bench and accepted.** §8's row records both. New §6.4.1 records BF-28's HEX
  proxy, BF-29's write gates and BF-30's charge readback, whose registers follow
  `osh-labs/VE.Direct_mppt_arduino`. §6.4's readback bullet now says when a pass runs.
  §10.2 and §10.9.3 already record BF-36's simulated MPPT.

- **v0.63** — **Events at QoS 1, from their own queue.** New §4.3.3 records BF-37's move to
  espMqttClient, D5's designated fallback, and BF-38's event queue. §4.3, §4.3.1, §5.2.1,
  §6.2.1 and §6.3.2 point to it.

- **v0.62** — **Spec v0.15's code, on air.** §6.7.2a records two bridge fixes from the
  2026-09-25 bench run: a set refusing every PHY row answers `not_applied`, and the
  readback mirror takes a `CLAMPED` result. §10.2's `ROLE_GATELINK` sends `CONFIG_CHANGE`
  after its own PHY revert.

- **v0.61** — **The code spec v0.15 owed the bridge is built.** New §6.7.2a records D63's
  `commit_failed`, D64's bandwidth refusal, D67's boot count, D68's `source` and D69's
  readback. §6.6.1's rename note is closed. The Library Plan citation moves to v0.21, whose
  bandwidth list and `OVERRIDE` marking §6.7.2a relies on.

- **v0.60** — **Protocol specification v0.14 → v0.15.** §4.2a and §4.4.3 no longer carry
  §16.6's open question, because **D65** publishes a bench node's answers whatever
  `simnode_diag_enable` says. §6.3.2's deduplication key is now spec §7.3's own. §6.6.1
  records **D66**'s `log` leaf, and the rename it owes. The Bridge PRD citation moves from
  v0.15 to v0.16 and the Library Plan's from v0.19 to v0.20.

- **v0.59** — **Header citations reconciled**: the Bridge PRD moves from v0.14 to v0.15 and
  the Library Plan from v0.14 to v0.19. Nothing in the body changes. The PRD's v0.15 is
  D59, which this plan took in v0.54. The Library Plan's v0.15–v0.19 record library work
  that BF-34, BF-24 and BF-33 built, and §6.2.2, §6.3.1 and B4b's row already describe
  it.

- **v0.58** — **BF-35 is built.** New §4.4.3: Home Assistant controls for the configuration
  table, generated from `/lib/lran-config/`, and the four naming and availability choices
  the operator made before the first publish.

- **v0.57** — **The poll clash is fixed.** §6.1.1 adds the rule that one exchange is on
  the air at a time. A scheduled `POLL` and any other frame no longer wait for their
  answers together.

- **v0.56** — **B4b is met on the bench and accepted.** §8's row records both, and §2.2
  no longer says BF-33 is unbuilt.

- **v0.55** — **D61 is built.** A bridge per-node lever, `deployed`, decides whether a row
  is polled and watched from boot. §6.1.1, §6.1.2 and §6.2.2 say so, and the dummy's
  availability note in §6.6.2 no longer assumes GateLink is watched.

- **v0.54** — **Protocol specification v0.13 → v0.14.** D59 fills in how §12.4's PHY
  change moves the fleet, and adds `PHY_REVERTED` to §8.9. §2.2's PHY paragraph and B4b's
  row in §8 follow. §6.3.2 needs no change: `PHY_REVERTED` publishes on
  `event/phy_reverted` under its §8.9 naming rule, and `publish.cpp` now names it.

- **v0.53** — **New §8.2**: B4's acceptance tally, and **B4 accepted**. V-B4 passed on a
  broker restart. The operator met V-B11 on GateLink's dummy publish and the bench gate on
  `config/set`, whose §7.1 row now names it. **New milestone B4b** takes BF-33's PHY
  commit-and-revert and precedes B6. §7.1's V-B4, V-B7, V-B11 and bench-gate rows and §8's
  B4 and B6 rows follow.

- **v0.52** — **New §6.6.2**: BF-27's dummy publish built and run on air. A serial console
  line becomes a `STATUS` or `EVENT` for the real publication policy, under GateLink's
  address and marked synthetic. §6.3.2's event payload gains a `synthetic` key. §6.3.1,
  §6.3.2 and §6.6.1 follow.

- **v0.51** — **New §6.3.2**: BF-25 built and host-tested. An `EVENT` goes to
  `lran/<node>/event/<name>` once, with retain clear, deduplicated on spec §7.3's triple
  plus the follow-up bit. A failed publish holds an event for retry. §6.3.2 records that
  PubSubClient publishes at QoS 0 only. §4.3.1 and §6.3's table follow.

- **v0.50** — **New §6.3.1**: BF-24 built and host-tested. Schema `0x10` becomes five
  retained documents and `0xF0` a sixth, with a stale block published as unavailable and
  cell voltages behind a deadband. SNTP supplies `last_traversal`'s wall clock. Discovery
  gains GateLink's state entities, and §6.3.1 records that §5.3's `decode/` was not built.

- **v0.49** — **New §4.2a.1**: BF-26 built, and confirmed on air. `simnode_diag_enable`
  reaches the gates for bench availability, diagnostics and discovery through BF-23's lever
  board. Clearing it publishes one `offline` per bench node, and every bench entity is
  diagnostic. §4.2a.1 also records that a simnode's `config/*` and `cmd/ack` go out
  ungated.

- **v0.48** — **V-B12 is met**, and new **§8.1.3** records it. Two interleaved sweeps lost
  0 of 482 idle frames and 2 of 480 with WiFi saturated, both in the idle arm's signature.
  M22 closes, and R-4.4's policy stands. §8.1's falsifier, §7's V-B12 row and B4's row
  say so. The PRD citation moves to v0.14.

- **v0.47** — **New §8.1.2**: the saturated arm's load is a bench-only UDP blaster in the
  `v_b12_blaster` environment, driven over the bridge's serial port, because
  `diag_interval_s`'s 10 s floor cannot load WiFi. §8.1 points to it.

- **v0.46** — **BF-34 is confirmed on air**, and §6.2.2 says so. The bench run passed all
  six steps and the three branches that were host-tested only.

- **v0.45** — **BF-34 is built**, and §6.2.2 records its choices. Two were decided with the
  operator on 2026-09-23. A bench row keeps the heard-first poll rule and rolls when first
  heard, so spec §10.6 step 1's boot `POLL` is the poll scheduler's for production rows
  only. Every simnode role answers `ROLL_CONTEXT`. §6.2 and §10.9.2 no longer say the roll
  is unbuilt.

- **v0.44** — **Protocol specification v0.12 → v0.13.** **D58** reaches the command path:
  after a bridge restart, each node's commands wait on its context roll (spec §10.6, Bridge
  PRD R-3.1h). §6.2 says so and names **BF-34**, which builds it. §10.9.2 says a simnode
  answers `ROLL_CONTEXT` `REJECTED_UNKNOWN_CMD` until then. §2.2's PHY paragraph follows
  **D56**: the parameters change at runtime only through §12.4's commit-and-revert, which
  is BF-33's. §6.7 was built against v0.13's §7.4.1 and §16.7 and does not change. The
  header's requirements and shared-codec citations had fallen behind, at v0.6 and v0.5,
  and now read v0.13 and v0.14.

- **v0.33** — **Protocol specification v0.11 → v0.12, and B3a accepted.** Four things land
  on this node. **§4.2.1's specification gap is closed**: the ladder's refusal of an
  unknown source is now §14 stage 9a with the counter **`rx_unknown_src`**, inside
  `rx_dropped`, so the bridge owes a rename — **BF-15a** in the tasks document, and until
  it lands the published name stays `unregistered_src` beside the registry rather than in
  it. **§4.3.2's "counters are the bridge's, not a node's" is now the specification's own
  rule**, not a decision taken locally and raised upward. **BF-19a has its answer**: spec
  §14.2 rules `ERROR` replies to registered sources only, rate-limited by
  `error_min_interval_ms` (default 1000), `ctx_id` `0`. **§5.3.1's node-address-filtering
  discrepancy is settled against the datasheet** as M24 — the feature is GFSK-only, and
  v0.12 withdraws the §12.1 requirement. **B3a was accepted on 2026-09-16** on the §10.5
  catalogue and W9; §10.5's `set_displaced` row gains the second counter that entry
  necessarily moves, and §8's B3a row states the `n` the console's `ping` takes. Decision
  Register **D35–D42**.

| Version | What changed |
|---|---|
| **v0.49** | **New §4.2a.1**: BF-26's bench publication gate, built and confirmed on air |
| **v0.48** | **§8.1.3**: V-B12 is met and M22 closes. WiFi saturated at 16 to 19.5 Mbps cost no measurable PER |
| **v0.47** | **§8.1.2**: V-B12's saturated arm is loaded by a UDP blaster, not `diag_interval_s` |
| **v0.46** | **§6.2.2**: BF-34 is confirmed on air |
| **v0.45** | **§6.2.2**: BF-34 is built. A bench row rolls when first heard, and every simnode role answers a roll |
| **v0.44** | Spec v0.13 citation. §6.2 gains D58's context roll and **BF-34**; §2.2's PHY paragraph follows D56 |
| **v0.43** | **§4.4.2**: BF-23's lever half is confirmed on air, and it gains `config_ack_timeout_ms`. **§8.1**'s falsifier records that a `diag_interval_s` set moved the diagnostics spacing. Its second check is still owed. **New §6.7.6**: `ConfigLock`, and why a lock rather than handing the resolution to `mqtt_task`. **§6.7.3** names the ACK timeout's row |
| **v0.42** | **New §4.4.2**: BF-23's lever half. Each bridge row of the configuration table now reaches the code it configures, except `simnode_diag_enable`. Values travel on a lock-free board, are applied on the owning task, and are published after the NVS restore. Host-tested, not yet on air. **§8.1**'s falsifier records that its first check passed |
| **v0.41** | **New §6.7** — BF-32's configuration path, built and confirmed on air 2026-09-21: §16.7.1's scoping, the one answer for two halves, §7.4's readback rather than retransmission, and §7.4.1's split answer. §6.7.5 records the three defects the bench found that the host tests could not. **v0.40 is the interleaved sweep's** |
| **v0.40** | **New §8.1.1** — two interleaved sweeps on 2026-09-21 separate spacing from the passage of time, and **correct §8.1's assumption that a 2000 ms gap loses nothing**: it measured 0.31 % over 640 frames. V-B12's two arms run interleaved rather than in blocks |
| **v0.39** | **Two stale statuses corrected**: §4.3.2's `ERROR` row said BF-19a was not on air, and §10.9 said the XIAO had never been flashed. **§10.9.2's `CONFIG` row follows D52 and D53**, as the simnode now does. **§4.4.1's timing-lever gap gains a closing note** — spec v0.13 §16.7 and **BF-32** answer it. **§10.5's `single_frame_interleave` explanation corrected.** Its example was a fragmented `CONFIG_ACK`, which spec v0.12 made impossible (D38); the defect it guards against is unchanged. Found by `LRAN-Config-Set-Brief` §2 |
| **v0.38** | **New §6.6.1** — BF-27's raw frame log, the one debug tool of §6.6 built so far. Records the deviation from §16.2's retention rule and the reason it is raised against the specification rather than settled locally |
| **v0.37** | **New §8.1** — **B3b accepted** and **V-B12 moved to B4**; §7.1's milestone column follows. The saturated arm needs BF-23's runtime lever and BF-26's bench diagnostics, and neither exists on this firmware |
| **v0.36** | **New §7.2.1** — BF-21's `simctl`; §10.5's `set_displaced` completes its displacing set and gains `ctx_reject` |
| **v0.35** | **New §6.2.1** — BF-18's command path, and the MQTT receive path it had to build first; §5.3 names `command.cpp` |
| **v0.34** | **§10.5** — a multi-frame row cannot confirm its own ERROR from the same board, measured 2026-09-16; what that costs BF-21 |
| **v0.32** | **§8** — B3 split into **B3a** and **B3b**; §7.1's milestone column follows |
| **v0.31** | **New §4.3.2** — BF-19's diagnostic documents; `kMaxPayloadLen` 768; ERROR replies split to BF-19a |
| **v0.30** | **New §6.1.2** — BF-20's availability watchdog; bench availability waits for BF-26 |
| **v0.29** | **New §6.1.1** — BF-17's scheduler; names `poll_reply_timeout_ms`, which no document did |
| **v0.28** | **New §10.9.2** — BF-6's `ROLE_GATELINK`; four operator decisions; three spec questions |
| **v0.27** | **New §10.9.1** — BF-9's OLED page, on both profiles |
| **v0.26** | **§10.5** — BF-8's catalogue built: `fault.{h,cpp}`, `fault` console command, `silent`, and the host suite that checks each counter |
| **v0.25** | **§10.5.2** — BF-7's patch primitive, built; §5.4's claim that the vectors share `lran-sim` corrected |
| **v0.24** | **New §10.9** — B0's first slice; `media_access` and the PHY move to `lib/lran-link/` |
| **v0.23** | **New §4.2.1** — BF-15's registry; spec §14 has no stage for an unregistered source |
| **v0.22** | **New §5.3.1** — BF-16's radio link; §5.2.1's stacks are **bytes**, not words |
| **v0.21** | **V-B9 run on the bench**, §6.5.2 says so; §7.1 moves **V-B12** from B2 to B3 |
| **v0.20** | Spec v0.11 citation — §6.2 and §10.5.1 say what a retry during execution receives |
| **v0.19** | **§5.1.2** — BF-14's status page: sentinels, burn-in, and a non-fatal display |
| **v0.18** | **§6.5.1–§6.5.2** — BF-13's OTA, why Arduino's default defeats rollback, and V-B9's procedure |
| **v0.17** | **§4.3.1** — BF-12's reconnect, keepalive, LWT and the enforced retain rule |
| **v0.16** | **§5.2.1** — BF-11's task priorities, cores, stacks, queue depths and drop policy |
| **v0.15** | The bridge keeps the range test's 3.0 dBi stick — §2.1's BOM and §3.2 say so |
| **v0.14** | **D1 closed** — §2.2 states the working point; B1a and B1b's D1 criteria discharged |
| **v0.13** | §2.2's bench power reconciled with **D33**; header names the node **Bridge Node** |
| **v0.12** | Readability pass — §2.2 and §10.7 moved into numeric order, §2.3.1's superseded text labelled as such, §12 gains a version index |
| **v0.11** | Six defects from a readability audit — duplicate `V-B12`, stale sibling citations, B0 gated on P8, §2.1's heading, §4.2's split table |
| **v0.10** | §5.1's display library corrected to the ThingPulse SSD1306 driver; **new §5.1.1** says why |
| **v0.9** | Spec v0.9 citation — regulatory, and it reaches the bridge as a transmitter |
| **v0.8** | Spec v0.8 citation; **W9** closed; range test pass 1 complete |
| **v0.7** | §11.2 marked superseded; §10.8.1's Heltec pin map **confirmed** against the vendor variant |
| **v0.6** | §10.5's fault catalogue rebuilt to 27 entries with a normative counter column; **new §10.5.1, §10.5.2** |
| **v0.5** | Spec v0.6 citation, body reconciled first; §14.1 is now the counter registry of record |
| **v0.4** | **New §10.8.1** — the `RadioPins` struct and the populated pin maps |
| **v0.3** | **New §2.3** the XIAO + Wio as target-radio simnode, **§10.8** profiles, **§11** workflow; B1 split into B1a/B1b |
| **v0.2** | **New §10**, `simnode` as buildable firmware: roles, multi-identity, console, fault catalogue |
| **v0.1** | Initial release, extracted from `lran-prd-v0_8` with requirements moved to the PRD |

- **v0.32** — **B3 is split into B3a and B3b** (§8, decided with the operator 2026-09-14).
  B3 as written could not be accepted until spec v0.12, BF-18, BF-21 and BF-22, and the
  three-PR stack could not merge until it was. B3a holds what BF-15, BF-16, BF-17, BF-19
  and BF-20 built and a bench can prove; B3b holds the command path, version tolerance, the
  scripted catalogue, real contention and V-B12. **No criterion is dropped**; two are
  narrowed in B3a and completed in B3b: keys are verified by command in B3b, and the
  catalogue runs by hand in B3a and from `simctl` in B3b. B3a adds a criterion B3 lacked, the
  measured poll-to-answer time. B4 now follows B3a.

- **v0.31** — **BF-19 publishes every §14.1 counter**, new §4.3.2. The discard counters are
  published as the bridge's, not per node, and the ERROR replies §14 asks of a receiver are
  not built; both were decided with the operator and both are raised for spec v0.12. **BF-26
  is deferred**: §4.2a's `simnode_diag_enable` needs `/lib/lran-config/`, an MQTT receive path
  and a `config/set` payload, and none of the three exists or has a task. No requirement or
  milestone criterion changes.

- **v0.30** — **BF-20 built the availability watchdog**, new §6.1.2. A node neither heard nor
  judged since boot is published as nothing, so a bridge restart does not flap a live node.
  Bench availability is judged and logged but not published until BF-26 builds
  `simnode_diag_enable` (spec §16.6). No requirement or milestone criterion changes.

- **v0.29** — **BF-17 built the poll scheduler**, new §6.1.1. §6.1 said when a poll is
  unanswered without saying how long to wait: **`poll_reply_timeout_ms`, default 10 000**, is
  named here for the first time and argued in the engineering log. Bench rows are polled only
  once heard, decided with the operator. No requirement or milestone criterion changes.

- **v0.28** — **BF-6 built `ROLE_GATELINK`**, new §10.9.2: `0xFE` on poll, `COMMAND_ACK`
  through the command gate, `CONFIG_ACK` from a RAM store, `0x11` events, the console's
  `push`, `event`, `ack` and `field`, and the five command-path faults. Every §10.4 command
  now exists. §10.9.2 records four decisions made with the operator. No catalogue entry,
  counter or milestone criterion changes. Not yet on air.

- **v0.27** — **BF-9 built the simnode's OLED page**, new §10.9.1: the last frame, the
  identity table, and each armed fault as an inverted bar that clears when the fault
  disarms. Host-tested. The Heltec's panel answers at boot; the XIAO's, on the Seeeduino
  expansion board, has not been flashed. No milestone criterion changes.

- **v0.26** — **BF-8 built the fault catalogue.** `firmware/simnode/fault.{h,cpp}` arms every
  §10.5 and §10.5.1 entry on one identity through `fault <hex> <name> [count]`; each malformed
  frame comes from `lran::sim::FramePatch` (BF-7), never a second serializer. `silent` withholds
  answers; the command-path faults (`ack_suppress`, `ack_dup`, `event_replay`, `cmd_replay`,
  `cmd_stale_seq`) answer ERR naming BF-6, and `bad_phy_crc` is refused as uninjectable. The
  host suite feeds each fault into the codec's own receive ladder and asserts the spec §14
  counter its row names moves. No catalogue entry, counter or milestone criterion changes; the
  faults' behaviour column is now executable rather than prose.

- **v0.25** — **BF-7 built `lib/lran-sim/`, and §10.5.2 records its surface**, with the
  catalogue entry each operation serves. **§5.4 was wrong**: its tree said `/tools/vectors/`
  shares `lran-sim`, and its note said one generator serves both consumers. §9.3 requires
  the Python generator to stay independent of the C++ code, so sharing would remove the
  check it provides. §5.4 now says the two stay separate, and that `lran-sim`'s tests compare
  them byte for byte. No catalogue entry, counter or milestone criterion changes.

- **v0.24** — **Simnode B0's first slice is built, and §10.9 records its choices.** §10.4
  gains `stats`, `radio`, and `ping`'s `to` and chunk arguments. §10.1 says how `0xF0` is
  marked synthetic, since it has no `status_reason`. §10.8's sketch names the Kit flag,
  `LRAN_PROFILE_XIAO_WIO_KIT`. §5.3 and §5.3.1 record that `media_access` moved to
  `lib/lran-link/`. **One bridge gap raised**: §17.3's RF loopback has no task.

- **v0.23** — **BF-15 built the registry, and §4.2.1 records its choices.** §5.3's module
  map no longer places `registry.cpp` in `sched_task`: its keys are read lock-free by
  `lora_task`, and what it learns is written under a mutex by whichever task learns it,
  which is `app_task` today. §5.3 gains `registry_runtime.cpp`. **One specification gap is
  raised, not patched**: spec §14 defines no stage for a frame from an unregistered source.
  Availability, which §5.3 listed under `registry.cpp`, stays with BF-20.

- **v0.22** — **BF-16 built the radio link, and §5.3.1 records its choices.** §5.3's module
  map gains `rx_ladder`, `media_access` and `radio_config`, the three pieces BF-16 split
  `lora_link` into. **§5.2.1's stack column was wrong in its unit**: ESP-IDF counts bytes,
  so every task had a quarter of the stack BF-11 intended. `lora` rises to 8192 and the
  rest are marked unmeasured. **§6.5.2 is owed again**, because the OTA verdict now requires
  the radio. §10.8.1's struct sketch notes that the bridge built TCXO voltage as
  millivolts. §5.3.1 also raises a discrepancy with spec §12.1's node-address filtering,
  without changing the specification.

- **v0.21** — **B2's bench session, 2026-09-13.** §6.5.2 said V-B9 had not been run; it
  has, all four steps passed on the flat-case Heltec, and the section now points at the
  bridge engineering log's 2026-09-13 entry for the banner lines. **§7.1 assigned V-B12
  to B2**, which §8's B2 row never included and which cannot run before BF-16 gives the
  bridge a radio. It moves to B3, where BF-16 lands. The procedure text is unchanged.

- **v0.20** — **Protocol specification v0.10 → v0.11, reconciled first.** v0.11 answers
  §9.4's check/record window: a node that receives a retry while still executing the
  command counts it and sends nothing (D34 amended 2026-09-11). **§6.2 needed no new
  branch** — silence already takes the `no ACK` path — and gains a paragraph on the one
  consequence, a failure published for an execution that outlasts every retry. §10.5.1
  records that `cmd_replay` tests the post-execution case and library P8 the in-flight
  one. Nothing on the wire moved; `ver` stays `2`. The bridge firmware's banner and
  `platformio.ini` header cite v0.11 with it. *Numbered v0.20 because B2's rebase onto the
  P8 merge placed it after BF-11 to BF-14's v0.16–v0.19; it was written as v0.16 on the P8
  branch.*

- **v0.19** — **§5.1.1's driver choice gains its configuration, in new §5.1.2.** BF-14
  built the OLED status page R-4.1c asks for, and three of its choices are worth a reader's
  time: **a dead panel never stops the bridge**, **unknown reads `--` rather than `0`**,
  and **burn-in is designed against** on a panel that is on for years — lower contrast and
  a slow pixel shift. It also records a `millis()` wrap the page would have shown as a
  phantom reboot every seven weeks.

- **v0.18** — **§6.5 gains what BF-13 found, in new §6.5.1, and V-B9 gains a
  procedure, in §6.5.2.** §6.5 said to configure an A/B table with rollback, and that was
  the easy half. **Arduino-ESP32 2.0.x marks every image valid before `setup()` runs**,
  so with the table alone, an image that never finds the LAN again would be kept — and a
  bridge that cannot reach the network cannot be OTA'd back. §6.5.1 records the override
  that takes the decision back, the verdict that replaces it (healthy after 120 s, rolled
  back at 600 s), and **the one detail that makes the override real — `extern "C"` — with
  its falsifier in CI** rather than only in prose. §6.5.2 is the bench procedure, stated as
  **not yet run**: V-B9 is not met until it is.

- **v0.17** — **§4.3's library guidance becomes a configuration, in new §4.3.1.** BF-12
  built the WiFi station, the `MqttTransport` seam and the LWT, and the numbers it had to
  choose — backoff, keepalive, socket timeout — had no home. Three entries are worth
  reading even if the list is not: **two Arduino-ESP32 defaults are deliberately off**
  (persistent credentials, SDK auto-reconnect), **`setBufferSize()` is called as well as
  the build flag** because the macro does not reach the library when it is built as a
  separate archive, and **spec §16.3's never-retain-an-event rule is enforced on the path
  rather than remembered**. The bridge also entered CI with this task; the engineering
  log's 2026-09-10 entries carry the reasoning that is not a number.

- **v0.16** — **§5.2's bands become numbers, in new §5.2.1.** BF-11 built the task
  structure, and the choices it had to make — priorities, core pinning, stack sizes, queue
  depths and what a full queue does — had no home. §5.2 keeps its bands and its rules;
  §5.2.1 records what was chosen, argues each number, and **names `firmware/bridge/src/tasks.cpp`
  as the authority over itself.** Three things in it are worth reading even if the table is
  not: `lora` is pinned off the WiFi core and **M22 is the measurement that would say that is
  not enough**; a full queue **drops the newest and counts it**, with BF-24/BF-25 owning the
  per-class refinement; and the never-block rule now has
  **`tools/checks/lora_task_never_blocks.py`** rather than only a paragraph. The engineering
  log's 2026-09-10 entry carries the reasoning that is not a number, including why **root
  rule 4 is not stretched to cover a queue overflow** — a dropped frame there has already
  passed the whole §14 ladder and has no stage to map to.

- **v0.15** — **The bridge antenna is decided: the same 3.0 dBi 19 cm stick the range test
  ran on** (Bridge PRD **R-4.3a.1**, 2026-09-10). §2.1's BOM row said "selected after M6",
  which is now answered, and §3.2's siting section was written as though the antenna and the
  position were one open question. **They are not, and separating them is the point of this
  revision** — the antenna is the part B1a and B1b measured through and the gain D1's −4 dBm
  ceiling is computed against, so keeping it is what makes those figures transferable. **What
  is still open is where the board goes**, which is the last of **V-B1**. The engineering-log
  requirement gains the antenna gain explicitly: it stays a term in the EIRP arithmetic even
  when it is not changing.

- **v0.14** — **D1 closed 2026-09-10, so §2.2 states a working point instead of an
  envelope.** 917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted with the fitted 3.0 dBi
  antenna, under §15.249 Envelope A; **D33 closed in the same motion**, because `BW` and the
  rule section are one decision. Two consequences are written where a builder meets them:
  **`backoff_max_ms` defaults to 1500** rather than 500, above SF9's 1107 ms full-frame
  airtime, and **the PHY parameters belong in the injected radio config**, not in the
  HA-visible set that §12.1 deliberately excludes them from. §8's **B1a** and **B1b** rows
  are marked where their D1 criteria are discharged — B1b's fade-tail measurement is what
  chose SF9 over SF7, so the confirming pass did more than confirm. **This plan inherits
  Protocol Spec v0.10**; nothing on the wire moved and no milestone gate changed.

- **v0.13** — **§2.2 reasoned about a power no configuration in this project uses.** It was
  written against the SX1262's `+22 dBm` maximum: two boards a foot apart putting −20 dBm
  into the receiver, "drop TX power to the minimum the driver allows", "restore full power
  before the range test". **D33 has capped conducted power at −4 dBm** with the fitted
  3.0 dBi antenna under Envelope A, Envelope B's ceiling is the modules' own tested powers
  (19.6 dBm Wio, 13.9 dBm Heltec), and a Heltec at +22 dBm is roughly 8 dB outside its own
  grant. The section keeps the original arithmetic — it is what makes the change legible —
  and applies the same ~42 dB of separation loss to the real ceiling: about **−46 dBm** at
  desk range, which still makes RSSI a relative number but is nowhere near the damage
  region. **The attenuator becomes a data-quality tool rather than protection**, "full
  power" becomes the D33 ceiling rather than the driver's maximum, and both §2.2 and §7.2
  now ask for **conducted dBm rather than a RadioLib power index**, since the Heltec and
  Wio certified powers differ by about 6 dB. §2.2's title changes with its content: one
  hazard, one data-quality trap. **The antenna rule is untouched** — it applies at any
  power.
  **§2.3's regulator note is qualified in the same way**, and the same figure in GateLink's
  own documents — §3.4's LDO sizing, milestone **M0**'s acceptance criterion and §9.7's
  power budget — is corrected alongside it in **GateLink Implementation Plan v0.6**.
  **Header renamed** to **Bridge Node** — see System PRD v0.11 and the amended **D17**.

- **v0.12** — **Readability pass; no design, requirement or measurement changed.**
  **Two sections were out of numeric order** and had been since the revisions that added
  them: §2.2 sat after §2.3.1, and §10.7 after §10.8.1. A reader scanning for §2.2 passed
  it twice. Both are moved, not renumbered, so every inbound reference still resolves.
  §10.5.1 and §10.5.2 drop to `####`, the level their numbering already claimed.
  **§2.3.1 no longer makes a reader read the obsolete version to reach the current one.**
  Its "BOTH FINDINGS CLOSED 2026-09-05" note is followed by 40 lines of the original
  open-question text, in present-tense imperatives — *"must be measured on the board"*,
  *"On arrival, before flashing anything"* — which read as live instructions. The text is
  kept **verbatim**, because a dated record is not a draft, and now sits under a heading
  that says what it is and what is still open in it.
  **§12 gains a version index**, one line per revision; the entries themselves stay at
  full length. Two empty *simply*s removed, and over-long lines rewrapped to the file's
  prevailing width.

- **v0.11** — **Defects found by a readability audit of this document, the Bridge PRD and
  the System PRD.** None changes the bridge design; three were reader-visible errors and
  two are reconciliations the header citations should have forced earlier.
  **Header citations moved to the documents that exist:** the Bridge PRD from v0.1 to
  **v0.6** and the protocol library plan from v0.1 to **v0.5**. Reconciling before moving
  them surfaced one divergence — the library plan's §6 has said since its v0.3 that
  **P8 gates simnode B0 alongside P6**, while §8 and §11.1 here still gated B0 on P6
  alone. Both now name P6 and P8. `tools/checks/spec_citation_version.py` does not see
  citations like these; it checks the protocol specification only, which is why two of
  them sat four revisions stale.
  **§7.1 gains a row for `V-B12`**, the WiFi/LoRa coexistence PER measurement (**M22**),
  which the Bridge PRD added in its v0.5 under a duplicate `V-B2`. Verified against B2.
  **§2.1's heading argued the conclusion its own body retired.** It read *"two is
  workable, three is right"*, written when a third Heltec was the only route to real RF
  contention; §2.3 has since assigned that role to the XIAO + Wio-SX1262, leaving the
  third Heltec as a cold spare. The heading and the summary sentence now say what the
  section concludes, and **§7.1's contention row no longer asks for a third board**.
  **§4.2's registry table was split in two by a stray blank line**, so `is_bench`
  rendered as a headerless one-row table.

- **v0.10** — **§5.1's display library is corrected and new §5.1.1 says why.** The table
  named **U8g2**, which no firmware in this repository has ever used: the BMS proof of
  concept and `firmware/range-test/` both chose the **ThingPulse SSD1306 driver**, and the
  PoC records the reason — U8g2's extra font control was not needed and the library is
  heavier. Range test R6's outdoor-legibility requirement, the one that could have argued
  for U8g2, was met with the ThingPulse driver's 24 px font. The license changes with the
  row: U8g2 is BSD-2-Clause, this driver is **MIT**, and `THIRD_PARTY_NOTICES.md` moves the
  discrepancy from open finding to resolved. **§5.1.1 states plainly that this node's half
  is still a choice**, because no bridge firmware exists — R-4.1c asks only for a
  glanceable "N nodes online" display and is MAY-level, so the case for a heavier library
  is weaker here than in either firmware that already declined it. Consistency is the
  argument; nothing forces it, and a future display requirement that needs U8g2 should
  change the row and say why rather than add a second display library beside the first.
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
- **v0.8** — Citation refresh only. Protocol specification **v0.7 → v0.8**, which closes
  **W9** (the full-size and fragmented `PING` bench runs both passed over RF on
  2026-09-05) and changes **no frame layout, header field, authentication scope or
  schema length**; no vector regenerates. No build step changes. The range test
  firmware, whose `lran-simnode` section this plan owns, has completed Pass 1: R1–R11
  and W9 are done, and the boards it used are the two this plan's board-count guidance
  allocates.
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
