# LRAN Research Archive

**Document:** `LRAN-Research-Archive`
**Version:** 0.1
**Status:** Reference only. **Nothing in this document is a requirement or a plan.**
**Parent document:** [`LRAN-System-PRD`](./LRAN-System-PRD.md)
**Last updated:** 2026-08-18

> **What this document is.** Everything the project learned that is no longer on the
> current path: the BusT4 investigation and its preserved Phase 2 design, the design
> history from v0.1–v0.5 and why each element was dropped, and the BMS investigation's
> dead ends.
>
> **Why keep it.** Two reasons. Bench measurements do not expire — the BusT4 findings are
> as valid today as when they were taken, and re-measuring would cost hours. And the dead
> ends are the reason the final answers are trusted: an investigation that only records
> its conclusion cannot be audited, and the same wrong turn gets taken twice.
>
> **How to read it.** Nothing here has been re-verified against the current design.
> Section numbers referenced as "PRD v0.5 §4.2" and similar refer to **retired revisions
> of `lran-prd`**, which are preserved in version control. Where this document and a
> current document disagree, **the current document wins.**

---

## Table of contents

1. [BusT4 — the investigation](#1-bust4--the-investigation)
2. [BusT4 — preserved Phase 2 design](#2-bust4--preserved-phase-2-design)
3. [Why BusT4 left v1](#3-why-bust4-left-v1)
4. [Superseded design history](#4-superseded-design-history)
5. [The BMS investigation — dead ends](#5-the-bms-investigation--dead-ends)
6. [Retired decisions](#6-retired-decisions)
7. [Document lineage](#7-document-lineage)
8. [Changelog](#8-changelog)

---

## 1. BusT4 — the investigation

**BusT4** is the Nice/Apollo proprietary protocol bus exposed on the 1050's 6P4C "Oview"
jack. v0.1–v0.5 of the PRD assumed GateLink would talk to the controller over it.

### 1.1 Measured pinout

All established by **direct measurement on the installed board.**

| Position | Signal | Measured |
|---|---|---|
| 1 | — | NC |
| 2 | VCC | **24 V** |
| 3 | data | 2.5 V |
| 4 | data | 2.5 V |
| 5 | GND | 0 V |
| 6 | — | NC |

**Resistance:** pin 3 ↔ pin 4 = **145 Ω** powered (standby), **174 Ω** unpowered.
Pin 3 ↔ GND and pin 4 ↔ GND both **open** when unpowered.

### 1.2 The physical layer is differential — how that was determined

**The interpretation is the useful part, not the numbers.**

Two independent single-ended UART lines would show a resistance **to a rail** (the
pull-up) and be effectively **open between each other.** The measurement is the exact
inverse: **open to ground on both, finite between them.** That is a terminated
differential pair.

The **145 Ω → 174 Ω shift across power states** further indicates active silicon in
parallel with the terminator — the 1050's own transceiver, whose input impedance drops
when biased.

**D13 resolved to differential**, with the SN65HVD230 as the transceiver of record.

> This determination stands on its own and does not depend on anything about the current
> design. If BusT4 is ever revisited, **this measurement does not need repeating.**

### 1.3 Standby behaviour — the finding that ended the approach

- Entering standby **sheds pin 2 (24 V) and both data pins.**
- **BusT4 is entirely unavailable during controller standby.**

This is the single measurement that moved BusT4 off the critical path. Any BusT4 feature
must either tolerate the bus being dead for most of every day, or be preceded by a wake
pulse on a command input — at which point the accessory I/O the wake pulse uses turns out
to be sufficient on its own.

### 1.4 Related standby findings

Two other things were established in the same bench session and **remain live in the
current design** — they are recorded here for completeness but are not archive material:

- The Diablo DSP-7LP loop detector was powered from **gated** V+, and was therefore
  unpowered — not merely idle — during standby. *(Corrected in the field; see the
  GateLink implementation plan.)*
- The exit wand is powered from **ungated** V+ and remains live.

---

## 2. BusT4 — preserved Phase 2 design

*Not required for any current goal. Preserved because the bench work is done, the findings
are solid, and the hardware is on hand.*

### 2.1 What it would add

Everything in the current goal set is already met without it. BusT4 would **additionally**
provide:

- **1050 configuration read/write** — auto-close time, force, speed, standby timeout, and
  the rest, from HA instead of the front panel.
- **Specific error and diagnostic codes**, where the current design sees only the
  hard-shutdown latch on IN6.
- **Cycle counters and service interval.**
- **Encoder position**, on controllers that expose it — **the only route to the position
  reporting deferred in the GateLink PRD.**
- **Movement cause as reported by the board**, rather than inferred from input timing.

### 2.2 What it costs

- A **GPL-3.0 dependency**, with the licensing consequence in §3.2.
- A differential transceiver wired to a connector whose adjacent pin carries **24 V**.
- A **custom UART implementation**: 19200 8N1 with a **519–590 µs break preceding each
  burst**, which a stock `HardwareSerial` configuration will not produce.
- A characterization phase — laptop, logic analyzer, read-only tap — before any frame is
  transmitted.
- **It is unavailable during standby** (§1.3).

### 2.3 Transceiver notes

Use the **SN65HVD230 with its onboard 120 Ω terminator lifted.** The 1050 end already
presents a defined differential impedance (§1.1), and doubling the load buys nothing at
19200 baud over inches of cable.

- **Do not substitute the SN65HVD231** — it disables the receiver in standby.
- **Confirm the '230 has no TXD dominant timeout** before relying on sustained dominant
  for the break. The feature belongs to the later SN65HVD25x parts, but verify against the
  datasheet timing table.

### 2.4 The StamPLC may make this cheaper

BusT4 is UART framing on a differential pair, and the ESP32-S3 GPIO matrix permits routing
a UART to **G42/G43**, which are wired to the StamPLC's onboard **SIT1044 CAN transceiver**
on the PWR-CAN port. If that works, **the SN65HVD230 is not needed and PORT.C stays free.**

Two things must be verified first, neither worth doing unless Phase 2 is actually pursued:

1. **Termination.** Whether M5 fitted a 120 Ω terminator on the PWR-CAN pair. The §1.1
   measurement of 145 Ω across the 1050's data pins indicates that side is already
   terminated, and a second terminator on a point-to-point link is undesirable. The
   terminator-lifting note in §2.3 applies equally here.
2. **XT30 pinout.** The PWR-CAN connector's power pins are tied **directly to VIN**. The
   1050's Oview VCC pin carries **24 V** and is not connected — **under no circumstances
   may these meet.**

### 2.5 Community sources

| Repo | Note |
|---|---|
| `pruwait/Nice_BusT4` | Original |
| `xdanik/Nice_BusT4` | English |
| `makstech/esphome-BusT4` | ESP-IDF, **most complete** |
| `karol27/Nice_BusT4_WT32-ETH01` | — |
| `bpietroiu/esphome-nice-bidiwifi` | Clean-room rewrite |
| `gashtaan/nice-bidiwifi-firmware` | BiDi-WiFi schematics |

**All GPL-3.0 except the last two — verify each before use.**

Known command frames and the `INF_IO` request used for limit-switch confirmation are
documented in those repos. Note that `makstech` exposes `set_standby(bool)` and
`set_auto_close(bool)` as SET commands, **which is the natural Phase 2 entry point: 1050
configuration from HA.**

### 2.6 If pursued

**PRD v0.5 §4.2 remains the authoritative reference for the BusT4 physical layer**,
including the full bring-up procedure, the Branch A / Branch B physical-layer analysis,
and the safety gating. It should be preserved in `/docs/appendix-b-bust4/` if this is ever
taken up.

Also note that **the 1050's protocol bus port is not connected in the current design**,
and its VCC pin carries 24 V. Any Phase 2 work begins with that hazard, not with firmware.

---

## 3. Why BusT4 left v1

### 3.1 The pivot

v0.1–v0.5 assumed GateLink would talk to the 1050 over BusT4. **Bench probing dismantled
that plan in one session:**

1. **BusT4 is dead during the 1050's low-power standby** (§1.3).
2. **The loop detector was unpowered during standby**, so detection needed a wiring change
   regardless of how the contact was acquired.
3. A **wake mechanism would have been needed anyway** — and every candidate wake mechanism
   was itself an accessory input.
4. Meanwhile, the same probing established that **every v1 goal is reachable through
   terminals Nice documented for third-party use.**

Point 4 is what settled it. Once the accessory I/O was known to be sufficient, BusT4 was
not a cheaper path to the same place — it was a more expensive path to a slightly larger
place, guarded by a 24 V pin and a copyleft dependency.

**The result is simpler, lower-risk, removes a reverse-engineering dependency, and removes
the project's only GPL obligation.**

### 3.2 The licensing consequence

An earlier revision resolved the project license to **GPL-3.0**, because porting the Nice
BusT4 community lineage (§2.5) would have made the firmware a **derivative work of GPL-3.0
code.**

**With BusT4 out of v1, that obligation disappeared** and the license became a free
choice. The remaining stack — MIT, Apache-2.0, BSD-2-Clause and LGPL-2.1-or-later —
imposes no copyleft, and the project resolved to **MIT**.

> **If BusT4 is ever pursued**, a port linking the GPL-3.0 lineage would make **that
> binary** GPL-3.0 regardless of the repo's stated license. Keep such a port in its own
> clearly-marked subtree so the copyleft scope is explicit at that point rather than
> assumed now.

### 3.3 What left the critical path with it

Removed when BusT4 moved out of v1:

- The safety-critical BusT4 bring-up procedure
- The Branch A / Branch B physical-layer analysis
- The pin-identification gate
- **Raw BusT4 frame streaming** and the `RAW` frame type
- The laptop sniffing phase
- Decisions **D3**, **D13** and **D18** as blocking items
- The SN65HVD230 and the logic analyzer from the critical-path BOM
- The hybrid node/bridge decode split (**D14** became moot — there is nothing left to
  decode)

Two second-order consequences are worth noting because they simplified unrelated parts of
the design:

- **Raw frame streaming was the one feature likely to stress a blocking MQTT publish.**
  Its removal is why the bridge's MQTT load is uniformly light and PubSubClient is a
  comfortable fit.
- **It was also the only feature that could occupy the LoRa channel for extended periods.**
  Expected occupancy is now dominated by short status frames on a 1–5 minute cadence.

---

## 4. Superseded design history

*Each of these was a real design at some point. Recorded with why it was dropped, because
"why isn't it done this way?" is a question that recurs.*

### 4.1 Two Heltec V3 nodes (v0.1–v0.6)

**The original design put a bare Heltec WiFi LoRa 32 V3 at the gate**, matching the bridge.

**What that required, once the I/O profile was known:** an external 4-channel relay module,
per-channel resistive dividers with clamp diodes for the 12 V sense inputs, and a
12 V→USB-C adapter to power the board. Three subassemblies and several hand-built discrete
circuits.

**Dropped for the M5Stack StamPLC**, which meets the same I/O profile natively — four SPDT
relays on screw terminals, eight opto-isolated 5–36 V inputs, 6–36 V supply input, DIN
mount. **The platform change absorbed all three subassemblies into the host and spent the
saving on a radio carrier.** Net part count roughly flat, net assembly count lower,
hand-built discrete circuits zero.

**The bridge stayed on the Heltec**, because the platform question that forced the change
never arose there — the bridge's I/O is a radio, WiFi and a display.

### 4.2 Night / low-PV power profile and RX duty-cycling (v0.2–v0.5)

**The design:** a PV-aware adaptive power profile with hysteresis. The node reads its
charge controller, and when PV output falls off it enters light sleep with SX1262 hardware
RX duty-cycling and a reduced poll rate. To keep a sleeping receiver reachable, the bridge
transmits with an **extended preamble** spanning the node's sleep window — worst-case
latency ≈ one duty-cycle period, with the airtime cost landing entirely on the
mains-powered bridge.

**Dropped when the battery changed.** The measured saving was **~0.36 Ah/day** — **0.36% of
100 Ah usable capacity** — against real costs in complexity, latency and overnight data
continuity. Against the previous 37.5 Ah usable lead-acid pack the calculus had been
closer; against 100 Ah it was not close at all.

Two supporting findings:

- **The radio was never the constraint.** Continuous LoRa receive adds ~5 mA on top of an
  ESP32-S3 that dominates at tens of mA while awake.
- **The MCU stays awake regardless**, to parse the ~1 Hz VE.Direct stream. There was no
  sleep to get to.

**The design is not archived — it is preserved as a reserved feature in
[`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) §17.1**, because
**WellLink may be battery powered without that storage reserve (D19).** Retired decisions
**D2** (duty-cycle period and preamble length) and **D9** (profile thresholds) reopen only
in that case.

> **Do not adopt it by default for a battery-powered node.** It is an option, not a plan.
> The reason it exists in the protocol spec is that WellLink *might* need it, not that
> battery power automatically implies it.

### 4.3 Lead-acid power budget (v0.1–v0.5)

The budget was built on a **Group 24 FLA, 75 Ah nameplate, 37.5 Ah usable at a 50%
derate**, giving ~5.1 days of autonomy at zero harvest and a 1.45× winter harvest ratio.

**Replaced by a 100 Ah LiFePO4 with 100% of nameplate usable** — ~14.3 days of autonomy at
a similar daily draw.

**What the bigger battery actually bought is worth stating plainly, because it is easy to
misread:** it did **not** fix a harvest deficit. Harvest is unchanged. What it bought is
**cloud tolerance and time to notice** — and, as a second-order effect, it made firmware
power optimization (§4.2) not worth doing.

It also introduced two new obligations that did not exist with lead-acid: the charge
controller must be reconfigured for LiFePO4 **before first charge**, and the pack's
low-temperature charge inhibition must be **observable**, because its symptom — a battery
not recharging on a sunny day — is otherwise indistinguishable from a failing panel.

### 4.4 Boot-time UNLOCK pulse (v0.7 only)

**The design:** on boot, GateLink pulses UNLOCK to clear any lock a previous instance might
have left behind.

**Withdrawn immediately in v0.8**, once `OUT = Moving` semantics made hold state directly
observable. GateLink now **reads** the real hold state at boot and adopts it, marking the
source unknown if it cannot attribute it.

> **The reasoning is worth keeping.** The boot pulse was designed to prevent GateLink
> stranding a gate in a locked state. But it could not distinguish its own stale lock from
> a hold **a person had deliberately set** — so its failure mode was closing a gate someone
> had propped open for a delivery, after a power blip, with nobody watching. **That is the
> worse failure**, and it argues for releasing a hold remaining an explicit act.

### 4.5 The 30-second arming delay (v0.5–v0.6)

**The design:** the held-open alert armed only after the gate had been open for 30 seconds,
to suppress false positives.

**Why it existed:** the alert was built on an *inferred* "statically open" condition. With
no way to distinguish a held gate from a gate mid-auto-close-countdown, a normal traversal
would have fired the alert, and the delay was the suppression mechanism.

**Retired** when hold state became observable. A routine pass leaves the auto-close
countdown running, which asserts MOVING, which is not a hold — so there are no false
positives to suppress. **Alerts now fire on the first edge with no dead window**, and the
retirement closed **D18**.

> A representative case of a pattern worth noticing: **a workaround built on an inference
> disappeared entirely when the underlying condition became measurable.** The delay was
> never a requirement; it was a symptom.

### 4.6 Hybrid BusT4 decode split (v0.5)

v0.5 specified a hybrid split of BusT4 decoding between node and bridge, with opt-in raw
frame streaming. **Moot once BusT4 left v1** — the node reads discrete inputs and drives
discrete relays, and there is nothing to decode. **D14** was resolved as moot rather than
answered.

### 4.7 VE.Direct as 3.3 V TTL (v0.1–v0.2)

Early drafts stated VE.Direct is 3.3 V TTL requiring no level shifting. **This is
incorrect. All Victron MPPTs are 5 V devices.** Corrected in v0.3.

Recorded because it is the kind of error that propagates into a BOM and is not caught until
bring-up — and because the correction is what introduced the BSS138, which is what later
raised **D25**.

### 4.8 Naming and scope history

| Revision | Name | Scope |
|---|---|---|
| v0.1–v0.2 | *(unnamed / gate node + bridge)* | Two Heltec nodes, BusT4 + VE.Direct |
| v0.3–v0.4 | **LoRa GateLink** | Single gate node plus bridge |
| v0.5+ | **LRAN** (LoRa Remote Automation Network) | Multi-node network; MQTT root `gatelink/` → `lran/`; one HA device per node (**D17**) |

The v0.5 rename accompanied the expansion of the bridge from a gate-specific bridge into a
general-purpose multi-node LoRa↔MQTT gateway, and is the point at which most of the
multi-node machinery entered the design.

---

## 5. The BMS investigation — dead ends

*The conclusion is in `/docs/bms-protocol.md` and the GateLink implementation plan. This is
the part that explains why the conclusion is trusted.*

### 5.1 The wrong family

The pack was **assumed to be a JBD/Xiaoxiang unit**, which is the default assumption for an
inexpensive Bluetooth LiFePO4 pack and was wrong.

### 5.2 The 20-combination sweep

A systematic sweep was run against both JBD and Daly protocol families:

- **JBD basic and version frames**, and **Daly with two host addresses**
- each sent to **all five writable characteristics**
- while **subscribed to three notify characteristics**

**Result: zero notifications**, and a consistent **~3.9 s disconnect every time.**

### 5.3 The clue was the consistency, not the failure

**The link dropped at the same moment regardless of what was sent.**

That is **not the signature of a wrong protocol.** A wrong protocol produces varied
behaviour — some writes rejected at the ATT layer, some accepted and ignored, occasionally
a malformed notification. **A uniform timeout is the signature of a connection that was
never authorised in the first place**, with the peripheral tearing down an unauthenticated
link on a timer.

Reading the failure that way is what redirected the investigation from "try more protocol
variants" to "find out what this device wants before it will talk at all" — and that is the
transferable lesson from the whole exercise.

### 5.4 Re-identification and what was actually missing

Cross-checking against the `aiobmsble` Python library's **TDT** plugin produced a full
decode. The protocol was then **reimplemented independently** and instrumented against the
reference transport, which revealed the missing step:

**A `HiLink` handshake must be written to characteristic `FFFA` — not `FFF2` — and read
back for a `0x01` acknowledgement, before any request to `FFF2` is answered.**

Without it, writes to `FFF2` are **ATT-acknowledged and then silently ignored**, and the
pack drops the link at ~4 s. **That is precisely the behaviour that made every earlier
probe look like a wrong protocol.**

A second detail: this unit answers request head **`0x1E`** and **never** answers `0x7E`,
which is the head most reference material uses.

### 5.5 Validation

The independent client ran **32 consecutive polls over ~81 s on one continuous connection,
with zero CRC failures and no dropped frames**, decoding SOC, pack voltage, per-cell
voltages, four temperatures, capacity, cycle count and MOSFET state **in agreement with the
reference implementation.**

That is the basis on which **D15** resolved to the BLE BMS and the SmartShunt was demoted
from an SOC source to a physical-layer contingency behind **D28**.

### 5.6 What remains open

**The pack-current sign convention.** Bit `0x4000` is believed to be the discharge flag but
has only ever been observed at 0.0 A. It needs one capture under charge and one under load
(**M7**). It is the last open item in an otherwise complete protocol.

### 5.7 Link margin — a real concern, recorded here because it is easily dismissed

The pack advertises at about **−80 dBm from inches away.** This was **confirmed
independently with a phone**, which needs to rest on top of the battery to beat −60 dBm —
so **this is the battery's own transmitter, not the test hardware.** The reading is
consistent with a weak transmitter or an antenna shielded under the BMS heat sink.

This is why **D28** exists, and why it asks for an RSSI measurement from the *final
mounting position* rather than from a bench.

---

## 6. Retired decisions

Recorded in full in [`LRAN-Decision-Register`](./LRAN-Decision-Register.md) §4. Summarized
here because they are the decisions most likely to be encountered while reading archived
material:

| # | Decision | Disposition |
|---|---|---|
| **D2** | RX duty-cycle period and preamble length | Retired for GateLink (§4.2). **Reopens only if WellLink is battery powered (D19)** |
| **D9** | PV-aware profile thresholds | Same |
| **D10** | Rx-boosted gain on or off | Retired **as a power question** — the difference is 1.1 mA, or 0.026 Ah/day. **Remains a link question** under D1: if the range test shows benefit from the +3 dB, take it |
| **D7** | BusT4 VCC handling | Superseded — the port is not connected (§2.6) |
| **D13** | BusT4 physical layer | Resolved to differential (§1.2), **archive use only** |
| **D14** | Decode placement | Moot — nothing left to decode (§4.6) |
| **D3** | Detector and movement-cause coverage | Resolved to discrete inputs; the bus alternative is here |

---

## 7. Document lineage

`lran-prd` v0.1 through v0.8, retired and superseded by the current document set. Preserved
in version control. Condensed history:

| Rev | The change that defines it |
|---|---|
| **v0.1** | Initial draft. Two custom Heltec V3 nodes, BusT4 + VE.Direct on the gate node, LoRa 915 MHz, bridge as LoRa↔MQTT gateway via MQTT Discovery |
| **v0.2** | BusT4 sniff/monitor as a retained diagnostic (**D3**); PV-aware day/night power profile with extended-preamble wake; poll scheduler fixed to the bridge (**D4**); **D9**, **D10** added |
| **v0.3** | Renamed **LoRa GateLink**. **VE.Direct corrected to 5 V** (§4.7). BusT4 electrical characteristics and a pin identification procedure. Loop detection promoted to a hard requirement with direction classification. License inventory and **D11** |
| **v0.4** | Physical installation context (single enclosure, no motor inside); **D12** resolved on that basis. Signal conditioning consolidated on one BSS138 with rise-time analysis. BusT4 electrical restructured into Branch A / Branch B; **D13** added |
| **v0.5** | Renamed **LRAN**; MQTT root → `lran/`; one HA device per node (**D17**). **Bridge expanded to a general-purpose multi-node gateway** — per-node addressing and HKDF keys, sequence tables, availability watchdog, explicit schema IDs, fragmentation, N/N−1 version tolerance, CAD + backoff. WellLink scoped; `simnode` added. Detection topology corrected to the DSP-7LP with a single combined safety contact plus a separate exit wand. **VE.Direct HEX** added with the three-gate write protection. **FLA → 100 Ah LiFePO4**; night profile dropped (**D2**, **D9**, **D10**). **BLE enabled for the BMS** (**D15**). OTA for the bridge only (**D16**). **D11** resolved to GPL-3.0 |
| **v0.6** | **The pivot** (§3). 1050 interface rebuilt on documented accessory I/O; BusT4 to Phase 2. Four momentary relays, six inputs. **Wake problem dissolves** (**D21**); standby retained (**D20**). Hold-open resolves to OPEN+LOCK / UNLOCK (**D22**); FIRE and SHADOW rejected on safety grounds. Held-open alert redefined on explicit hold state; **30 s arming delay removed** (§4.5, **D18**). **D3** → discrete inputs, **D14** moot, **D13** archive-only, **D7** superseded, **D11 reopened**. Diablo moved to unswitched power. Relay dry-run and input injection added. **D23**, **D24** added. Command deduplication added because relay pulses are not idempotent |
| **v0.7** | **Host platform → M5Stack StamPLC** (§4.1); LoRaBridge stays on the Heltec. **D25** (VE.Direct translator), **D26** (no 3.3 V rail), **D27** (carrier fabrication), **D28** (BLE margin), **D29** (thermal envelope), **D30** (co-processor) added. Carrier bring-up added as a test phase. Display switched to backlight-only, three dedicated buttons. **Nothing above §4 changed** — protocol, command model, detection logic, HA entity model, keying and safety posture all carried forward from v0.6 intact |
| **v0.8** | **A measurement and closure revision.** `OUT = Moving` measured to stay energized through the auto-close countdown, making **hold state observable rather than remembered** — which extended the held-open alert to manual holds, let movement cause distinguish a manual hold from a manual momentary open, and **withdrew the v0.7 boot-time UNLOCK pulse** (§4.4). Any energized OUT relay measured to prevent standby (**D23**). First current-clamp figures recorded **but not adopted**, because they fail an internal sanity check. Runtime configuration management added. **D15** closed on the validated TDT BMS protocol (§5) |

---

## 8. Changelog

- **v0.1** — Initial release. Assembled from `lran-prd-v0_8` Appendix B, §1.5 (BusT4
  pinout and differential determination), §4.3, §9.1, §12.2 (the GPL analysis), the
  superseded portions of §1.2, §5.7.4 and §8.1.1, and the v0.1–v0.8 changelog.
  **Everything is preserved in substance**; the reorganization is by *why it was dropped*
  rather than by which section it came from, so that a reader arriving with the question
  "why isn't it done this way?" lands on an answer instead of a fragment.
  **Added:** §4, superseded design history as a first-class section — v0.8 carried this
  only as changelog entries, which meant the reasoning behind a dropped design was
  recoverable but not readable; §5.3, an explicit statement that the *consistency* of the
  BMS failure was the diagnostic clue, which is the transferable lesson from that
  investigation and was previously implicit; §3.3, the two second-order simplifications
  that followed BusT4's removal (light bridge MQTT load, negligible channel occupancy),
  which were noted in scattered v0.6/v0.7 remarks and are easy to lose. **Appendix A of
  v0.8 is not here** — the RX duty-cycling design is a live reserved feature and lives in
  [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) §17.1; §4.2 of this
  document records only why it left GateLink.
