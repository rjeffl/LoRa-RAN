# LRAN Decision Register

**Document:** `LRAN-Decision-Register`
**Version:** 0.4
**Status:** Living document. Updated whenever a decision changes state.
**Parent document:** [`LRAN-System-PRD`](../LRAN-System-PRD.md)
**Last updated:** 2026-09-06

> **This is the only place a decision's status is recorded.** Every other document in
> the set references decisions by number and describes the *outcome* where it is
> relevant, never the status. When a decision closes, this file is the one that
> changes.

---

## Table of contents

1. [How to use this register](#1-how-to-use-this-register)
2. [Open decisions](#2-open-decisions)
3. [Resolved decisions](#3-resolved-decisions)
4. [Retired decisions](#4-retired-decisions)
5. [Measurement backlog](#5-measurement-backlog)
6. [Changelog](#6-changelog)

---

## 1. How to use this register

| Field | Meaning |
|---|---|
| **#** | Permanent identifier. **Numbers are never reused**, even after a decision is retired — a retired D-number appearing in an old log entry must still resolve to the right thing |
| **Owner** | The document where the outcome is written up |
| **Status** | `open`, `resolved`, or `retired` |
| **Gate** | What the decision blocks, or the bring-up phase by which it must close |

**Statuses.** `resolved` means a choice was made and the consequences are written into
the owning document. `retired` means the question stopped applying — usually because a
design change removed the thing it was about. A retired decision is not a decision that
was answered; it is one that no longer needs answering, and the distinction matters when
reading old material.

**Adding a decision.** New numbers continue from the highest issued, currently **D34** for
decisions and **M23** for measurement-backlog items.
A decision belongs here rather than in a node document when its answer would change more
than one section, or when it is blocking work.

---

## 2. Open decisions

| # | Decision | Owner | Notes | Gate |
|---|---|---|---|---|
| **D1** | **LoRa PHY parameters** — SF / BW / CR / TX power | System PRD §5.1 | **Open, but bounded** — see §2.1. Pick after the range test at ~500 ft **on both bearings**. The protocol spec's airtime analysis establishes that SF may be chosen on **link margin alone, not on power** — SF9 is affordable if the link wants it. **TX power is capped by D33**; the **frequency requires the ambient survey (M20)** first. Range test results alone do not close this | Phase 1 |
| **D33** | **FCC Part 15 operating mode** — *reopened 2026-09-06* | Protocol Spec §18.1 | **Reopened by M21**, exactly as standing condition 1 anticipated. Neither module is certified under §15.249; both carry §15.247 DTS **and** DSS grants, and D33's fixed-channel no-hopping mode exists inside those grants **only at BW500**. Two envelopes are documented in `LRAN-M21-FCC-Grant-Findings` §6. **Envelope A (§15.249) is the plan of record** — the 2026-09-04 walk closed at 0 % PER across six positions at its ceiling — with **Envelope B a fallback behind three explicit triggers**. Separately and permanently: the grants **do not transfer**, so the operative frame is **§15.23 home-built**. See §3.3 | Before D1 fixes a number |
| **D19** | **WellLink power source** | WellLink PRD | Mains vs. battery/solar. Determines whether the reserved RX duty-cycling design (Protocol Spec §17.1) is needed, and whether battery telemetry is required in the WellLink schema | Before WellLink design |
| **D25** | **VE.Direct TX translator** | GateLink Impl Plan | BSS138 retained by default but may fail against a weak symmetric 5 V driver. Settled by **one measurement**: 10 kΩ from the MPPT TX pin to GND with the port streaming, observe the low excursions. Fallback ADuM1201 or 74LVC1G17. **The BSS138 stays on the RX direction either way** | Before carrier build |
| **D28** | **BLE link margin from the StamPLC mounting position** | GateLink Impl Plan | The Stamp-S3A's 2.4 GHz antenna is internal to the DIN case with no external option, and the pack's own transmitter is weak (~−80 dBm from inches away, confirmed independently with a phone — this is the battery, not the test hardware). Measure RSSI from the intended mounting position. Fallbacks: SmartShunt, or the D30 co-processor. **Amended 2026-09-06 — see §2.2. Still open, but the margin looks considerably better than this row's premise** | Phase 5 |
| **D29** | **Enclosure thermal envelope** | GateLink Impl Plan | **Narrowed to the high end.** Cold exposure affects no functional dependency; summer solar gain in a closed box is cumulative and does. Instrument LM75 + MPPT + BMS, verify the existing screened vents, add shade, and fit a thermostatic fan **only if logged maxima justify it** | Phase 9 / ongoing |
| **D31** | **Copyright holder name** | System PRD §11.2 | MIT text and the 2026 year are settled; the name on the copyright line is not. Personal name or a project/entity name. **Blocks the first public push, nothing else** | Before first public push |

### 2.1 D1 — what bounds it (2026-08-30)

D1 remains open pending range test results, but it is no longer unbounded. Three
constraints now apply, and **range test results do not close D1 on their own**:

- **TX power is capped** by **D33** at the §15.249 EIRP ceiling. The range test sweep
  starts at the bottom of the SX1262's range and climbs only if the link fails. A
  working point chosen at a power that cannot be used is a result that has to be
  thrown away.
- **The frequency requires an ambient survey first.** Per Protocol Spec §12.1, D1 shall
  not fix a frequency until an RSSI sweep of 902–928 MHz has been run at **both** the
  bridge location and the most distant node location (**M20**). The two do not see the
  same picture, and it is the node's noise floor that sets its margin.
- **The site has known occupants.** Four YoLink temperature sensors plus a switch talk
  to a YoLink hub inside the dwelling. YoLink uses LoRa at 915 MHz and a hub teardown
  found a Semtech SX1276, so this is real CSS modulation and CAD will see it. Whether
  it is LoRaWAN band-plan or proprietary is **unconfirmed** — a consumer star network
  to a vendor hub is more likely proprietary. M20 settles it empirically, which is
  better evidence than a datasheet either way.
- **The power figure also needs the grant conditions** recorded under D33.
- **The bandwidth and the rule section are one decision** *(fourth bound, added
  2026-09-06 by M21)*. A single fixed channel with no hopping exists inside the modules'
  grants **only at BW500**, within 903.0–914.2 MHz (§15.247 DTS). BW125 forces the
  §15.249 envelope and its ≈ −1.2 dBm EIRP ceiling — which the walk shows is sufficient,
  so **Envelope A is the plan of record**. **D1 shall not fix `BW` without fixing the
  envelope in the same motion.**

**W7 still follows.** The airtime table regenerates once SF/BW/CR are fixed (**M19**).

### 2.2 D1 and D28 — what M21 changed, 2026-09-06

Recorded here rather than by editing the rows above, because the rows are the *current*
state and this is the *dated* account of how they got there.

**The frequency now has a survey behind it, and one candidate is ruled out.** M20's field
work closed on 2026-09-05 (§5.1). `weather-island` shows the one confirmed in-channel
occupant at **−80 dBm on 915.0 MHz** against a −115 to −118 dBm floor; `gatelink-gate`
carries the strongest near-band neighbour at **−66 dBm on 914.0 MHz**, at the site GateLink
will occupy. Both bursty, no carrier anywhere in 902–928. **The range-test firmware's
provisional 915.0 MHz is the confirmed occupant's own peak and D1 must move off it.**
Note also that 923.3–927.5 MHz is LoRaWAN US915 *downlink*, so Envelope A's genuinely
uncommitted region is roughly **915.2–923.0 MHz**.

**The power figure is bounded, and it was measured at the ceiling rather than modelled.**
The fitted antenna is confirmed as a 19 cm stick claiming **3.0 dBi**, vertical, the same
part at both ends, so the conducted ceiling is **−4 dBm**. The 2026-09-04 walk ran at
exactly that and closed at 0 % PER across all six positions; the same walk at −9 dBm — the
SX1262's hard floor — showed 12.5–25 % PER at SF7. **A working point below −4 dBm is not
available**: it is both unnecessary and, at −9 dBm, measurably worse.

**The SF question the survey did not answer.** SF9 at −9 dBm was clean at all six positions
where SF7 was not, but SF9 costs a `backoff_max_ms` raise above its 1107 ms full-frame
airtime (Protocol Spec §12.3). SF7 at the −4 dBm ceiling was also clean. Both live.

**D28's premise has improved, and its own recorded figure stays as written.** The GateLink
node sits in a plastic enclosure **inside** the steel gate-controller enclosure — which
also contains the pack and its BMS, 6–8 in away. The BLE link therefore never crosses a
metal wall; both ends are inside the same cavity. A reading of **−50 to −60 dBm** was taken
in that enclosure with a Heltec V3, 20–30 dB better than D28's row records. That
discrepancy is **not silently reconciled**: candidate explanations are a different
measurement position, a standing-wave null in the cavity, pack state, or the earlier
reading having been taken outside the box. **D28's −80 dBm is a dated observation and is
left standing**; this entry supersedes its *outlook*, not its record. D28 closes on
**M23**, which is now a cavity-position and antenna question rather than a survival one.

---

## 3. Resolved decisions

| # | Decision | Outcome | Owner |
|---|---|---|---|
| **D3** | Detector and movement-cause coverage: bus vs. discrete I/O | **Discrete inputs.** The detectors are tapped directly; movement cause is derived locally from input timing | GateLink PRD |
| **D4** | Poll scheduler location | **Bridge firmware**, per node, runtime-configurable from an HA `number` entity | Bridge PRD |
| **D5** | MQTT client library | **`MqttTransport` abstraction, PubSubClient first.** `MQTT_MAX_PACKET_SIZE` ≥ 1024 — the default 256 is smaller than a Discovery config and fails confusingly | Bridge Impl Plan |
| **D6** | Gate-node display trigger | **Button toggle + automatic on in any debug mode**, with an inactivity timeout on the manual path only | GateLink PRD |
| **D7** | BusT4 VCC handling | **Superseded** — the BusT4 port is not connected in v1. Its VCC pin carries 24 V and stays untouched | Research Archive |
| **D8** | HA entity modeling | **`cover` (device_class `gate`) as the primary control surface, plus a "Hold gate open" `switch`.** Position reporting deferred | GateLink PRD |
| **D11** | Project license | **MIT.** The remaining stack imposes no copyleft once BusT4 left v1. Holder name outstanding as **D31** | System PRD §11.2 |
| **D12** | VE.Direct isolation vs. level shifting | **No isolation needed.** Single enclosure, short leads, worst-case ground offset ~16–40 mV against a 5 V threshold. Level-shifter choice tracked separately as **D25** | GateLink Impl Plan |
| **D13** | BusT4 physical layer | **Differential.** Measured: open to ground on both data pins, 145–174 Ω between them — a terminated differential pair. **Research archive only** | Research Archive |
| **D14** | Decode placement | **Moot.** The node reads discrete inputs and drives discrete relays; there is nothing to decode | System PRD §3.3 |
| **D15** | Battery SOC source | **BLE BMS.** The pack is a **TDT** unit; the access sequence is documented and an independent client validated over 32 consecutive polls with zero CRC failures. SmartShunt demoted to a physical-layer contingency behind **D28** | GateLink PRD |
| **D16** | OTA policy | **Bridge yes, remote nodes no.** The bridge is on the LAN, mains powered and physically accessible; a bad flash 500 ft away is a walk with a laptop and there is no second radio path to recover through | Bridge PRD |
| **D17** | Naming | **LRAN umbrella; `lran/` MQTT root; GateLink / WellLink / LoRaBridge** | System PRD §1.3 |
| **D18** | Auto-close observability | **Resolved, and better than expected.** `OUT = Moving` stays energized through the auto-close countdown, so auto-close state *is* observable — and hold state falls out of it for free | GateLink PRD |
| **D20** | Gate controller standby policy | **Standby retained**, timeout measured at **60 s**. Command relays wake the board on their own, so nothing is lost by keeping it | GateLink PRD |
| **D21** | Wake mechanism | **None needed.** Every command relay drives a command-class input, so the pulse that carries the command is also the pulse that wakes the board | GateLink PRD |
| **D22** | Hold-open mechanism | **OPEN+LOCK / UNLOCK**, confirmed on the bench. Replicates the mechanism the installation already uses | GateLink PRD |
| **D23** | OUT1/OUT2 sense polarity | **No inversion.** An energized OUT relay *prevents* the board entering standby, so an asserted output is always valid. Use normally-open contacts, OUT1 = OPEN, OUT2 = MOVING | GateLink PRD |
| **D24** | Manual UNLOCK path | **Two independent paths**: the handheld remote already programmed with UNLOCK, and the control-panel pushbutton rewired to AUX2 | GateLink PRD |
| **D26** | StamPLC 3.3 V rail | **No 3.3 V rail is exposed.** Bus power pins are VIN, GND and EXT_5V only, and EXT_5V sits near 4.76 V under load. The carrier LDO stays in the BOM, and an AMS1117 is excluded on dropout | GateLink Impl Plan |
| **D27** | Carrier board fabrication | **Perfboard populated with prefabricated modules**; regulator and discretes mounted directly. Preserves the "no hand-built discrete circuits" property. Remaining sub-item: pick a DIN-rail carrier and cut the board to it | GateLink Impl Plan |
| **D30** | LoRa/BLE co-processor | **Not adopted.** A direct SX1262 on the carrier is the plan of record. The Heltec-class co-processor is retained as a documented fallback with three explicit triggers | GateLink Impl Plan |
| **D32** | SX1262 driver library | **RadioLib**, for every firmware in the repo — bridge, GateLink, WellLink, simnode, range test. One API across the Heltec V3's internal SX1262 and the Wio-SX1262 on the XIAO and GateLink carriers, direct CAD access, no vendor board package. See §3.1 | System PRD §11.1 |
| **D33** | FCC Part 15 operating mode (**closes W5**) | **Moved to §2 — reopened 2026-09-06 by M21.** The 2026-08-30 outcome and its standing conditions remain readable in §3.1; what changed is in §3.3 | Protocol Spec §18.1 |
| **D34** | Home for Protocol Spec §9.4 steps 4–6 (**closes W12**) | **Split, not placed whole.** Steps 4, 5 and the state half of 6 become `lran::CommandGate` in `/lib/lran-protocol/` — one per peer, immediately after `Reassembler`. The **dispatch** half of step 6 stays in the application. The gate returns a verdict; the caller decides. See §3.2 | Protocol Library Impl Plan §3, §6 (**P8**) |


### 3.1 Notes on D32 and D33

Both were settled on **2026-08-30**. Their outcomes are written up in the owning
documents; what follows is the part that has no other home.

> **D33 was reopened on 2026-09-06.** This subsection is left as written — it is the record
> of what was decided and why on 2026-08-30, and rewriting it would destroy the thing that
> makes it useful. **§3.3 records what changed.** Read them in that order.

**D32 — RadioLib.** Rejected alternatives: Heltec's own library, convenient on one
board, useless on the StamPLC carrier, and it would have forced a second driver for
GateLink; a raw Semtech HAL, more control than this project needs plus CAD, calibration
and TCXO handling to write from scratch.

*Consequences, and they are standing ones.* **Pin the RadioLib version in every
`platformio.ini`** — a driver shared by four firmwares is not a thing to let float. The
two settings that fail silently on the Heltec V3, **TCXO reference voltage** and
**DIO2-as-RF-switch**, belong in the injected board config (Protocol Spec §12.2) from
the first commit, not discovered per firmware.

**D33 — fixed channel at §15.249 power.** Status is *settled, with standing conditions*.
Losing any one of the three reopens it:

1. TX power stays at or below the 15.249 ceiling. This is **EIRP**: conducted power plus
   antenna gain. With a 2 dBi antenna the conducted figure is around −3 dBm. **Record
   conducted power and antenna gain separately** or the number cannot be audited.
2. Aggregate channel occupancy stays low — a handful of nodes at status cadence.
3. The ambient survey (**M20**) finds no co-channel occupant on the chosen frequency.

**This is not a compliance determination.** Confirm the radio modules' own FCC grant
conditions — antenna type and gain, and what each grant assumes about power and hopping
— **before D1 fixes a number**.

**On the site's existing 915 MHz equipment** (four YoLink temperature sensors and a
switch, on a YoLink hub, all inside the dwelling): this has **no bearing on the
operating mode**. Part 15 compliance is per device; a certified product nearby
establishes that *some* compliant mode exists, not that this one is it. It bears on
channel selection and CAD tuning instead — see §2.1 and Protocol Spec §12.3.

### 3.2 D34 — why a split, and why it lands in the protocol library

Settled **2026-08-31**, closing **W12**. The specification asked "where do steps 4–6
live" as one question; it is two, and asking it as one is why it stayed open.

**Steps 4, 5 and the high-water update in step 6 are validation against receiver
state** — the same logic on every side, no allocation, no I/O, clock injected.
**Dispatch is node behaviour** and stays in the application. `CommandGate::check()`
returns `Execute` / `ReturnCached` / `Reject`; it never executes anything.

*Why `/lib/lran-protocol/` rather than a new library or four applications.*

1. **`Counters` already owns `rx_rejected_seq` and `rx_dup_command`**, and
   `total_dropped()` already sums the first. Until the incrementer shares that
   `Counters` instance, schema `0xF0`'s `rx_dropped` under-reports on exactly the
   frames that move a gate. An incrementer in a different library from the counter
   struct recreates the split that produced the v0.5 naming drift.
2. **`Reassembler` is the precedent and settles the scope question**: per-peer,
   stateful, `now_ms` injected, no allocation, returns a `Status`. `CommandGate` is
   the same shape. If reassembly is in scope, this is.
3. **`Status` must gain `DuplicateCached` and `RejectedSeq`** so `Counters::bump()`
   stays the single mapping point — its `-Werror=switch` guard only works if the
   values are in the enum, which they cannot be from outside the library.
4. A separate `/lib/lran-rx/` would need `Frame`, `Status`, `Counters`, `AckResult`
   and `seq_newer`. A library whose entire surface is another library's types is a
   header with a build system attached.

**Rejected:** leaving it to each application. §10.4 is not advisory — a relay pulse is
not idempotent — and four independent implementations of a replay check is three too
many.

**Consequences.**

- **`check` and `record` are two calls.** The cached value is the *result of
  execution*, so one call cannot produce it, and caching before execution would
  return a success ACK for a command that then failed.
- **`check → execute → record` must be atomic with respect to frame arrival**, and is
  recorded as a documented precondition in the manner of `Reassembler`'s monotonic
  clock rather than engineered around. A retry landing inside that window finds no
  cache entry *and* fails the `seq` check, answering `REJECTED_SEQ` where §10.4
  requires the cached ACK. Unreachable on a single-threaded receive loop, which is
  what both sides use. **§10.4 is silent on this window** — a specification gap
  recorded rather than patched locally.
- `dedup_cache_depth` (§10.4, default 8) becomes a `/lib/lran-config/` parameter,
  runtime-settable.
- Cost is **32 B per peer** — the gate holds one `ctx_id` and entries store
  `(seq, result, detail)`. 32 B on a node, 160 B on a five-node bridge.
  `reset_context()` clearing the cache then falls out for free, which is exactly
  §10.4's "lost on reboot, which is correct."

**The deadline is later than the specification implied, and this is the useful part.**
§9.2 makes **every authenticated type bridge → node** — `COMMAND`, `CONFIG` and
write-class `HEX_REQ`. Nodes emit only unauthenticated frames, so **on the bridge
steps 4–6 apply to an empty set today**. The deadline is therefore not "before the
second firmware is written" but **before the first firmware that accepts a
`COMMAND`**: simnode `ROLE_GATELINK` at **B0**, and GateLink **M3**. `ROLE_RANGE`
echoes unauthenticated `PING`, so **W12 does not block the range test firmware**.
Land `CommandGate` as library milestone **P8**, before B0.

---

### 3.3 D33 reopened — what M21 found, 2026-09-06

**The mechanism worked as designed.** §3.1's standing condition 1 required the modules' own
FCC grant conditions to be confirmed *before D1 fixes a number*. M21 did that, and the
confirmation did not support the premise. Full record in
[`LRAN-M21-FCC-Grant-Findings`](./LRAN-M21-FCC-Grant-Findings.md) and its implementation
companion [`LRAN-M21-Handoff`](./LRAN-M21-Handoff.md).

**What the grants say.** Heltec V3 — `2A2GJ-HTIT`, **finished-product certification, not
modular**, LoRa DSS 125 kHz at ≈13.5 dBm and DTS 500 kHz at ≈13.9 dBm over 902.3–914.9 and
903.0–914.2 MHz respectively, with an **internal 3.0 dBi antenna declared and fixed**.
Seeed Wio-SX1262 — `Z4T-WIO-SX1262`, **single modular approval**, 92 mW (≈19.6 dBm), with
a grant condition that the antenna **must not be co-located or operating in conjunction
with any other antenna or transmitter**.

**Three findings, in order of consequence.**

1. **Neither module is certified under §15.249.** Both are §15.247. D33's premise named a
   different rule section from the one either module was tested against.
2. **D33's operating mode exists inside those grants only at BW500.** Both manufacturers
   split LoRa the same way — 500 kHz as DTS, 125 kHz as DSS, i.e. as frequency hopping. A
   single fixed 125 kHz channel is too narrow for DTS and is not hopping, so it is neither.
   **This is what makes `BW` and the rule section one decision**, and it is D1's fourth
   bound (§2.1).
3. **The grants do not transfer, permanently.** Four independent grounds: LRAN runs custom
   firmware and drives the PHY arbitrarily through RadioLib (D32); the Heltec grant is not
   modular; the Heltec antenna declaration excludes the external antenna every deployment
   and range test uses; and **co-located LoRa, WiFi and BLE are a hard project requirement**,
   which the Wio's modular grant forbids outright. The fourth is recorded as **closed, not
   deferred**, so it is not re-opened later as an option.

**The operative frame is §15.23 (home-built devices)** — not more than five units, personal
use, not marketed, builder expected to follow good engineering practice and aim at the
applicable technical standards. **Corollary, and it is a standing one: no LRAN node may be
represented as FCC certified**, and no "Contains transmitter module FCC ID: …" label may
appear in the README, a LICENSE header, an enclosure label, or HA device metadata.

**D33's chosen ceiling survives its reasoning.** §15.249 was the only rule section
permitting a single fixed narrow channel, and it happens also to be sufficient — the
2026-09-04 walk closed at 0 % PER at all six positions at that ceiling. What is replaced is
the *justification*, not the number.

**One arithmetic correction to §3.1's condition 1.** It states a conducted figure of "around
−3 dBm" from a 2 dBi antenna. The fitted antenna claims **3.0 dBi**, so the conducted
ceiling is **−4 dBm**. The requirement to record conducted power and antenna gain
**separately** is unchanged and is now more important, not less: the findings note uses
*different* gain assumptions for compliance and for link budget, and a single combined EIRP
figure cannot be re-derived into either.

**What reopening D33 does and does not block.** It does not block the range test firmware,
which already clamps to this ceiling and logs both terms. It blocks **D1 fixing a number**,
which was always the gate.

## 4. Retired decisions

| # | Decision | Why retired |
|---|---|---|
| **D2** | RX duty-cycle period and preamble length | Retired for GateLink when the night/low-PV profile was dropped — the saving was ~0.36 Ah/day against 100 Ah usable. **The design is preserved** as a reserved feature in Protocol Spec §17.1 and this decision **reopens only if WellLink is battery powered (D19)** |
| **D9** | PV-aware profile thresholds | Same — went with the night profile |
| **D10** | Rx-boosted gain on or off | **Retired as a power question.** The difference is 1.1 mA, or 0.026 Ah/day. It remains a **link** question: if the range test shows any benefit from the +3 dB, take it (**D1**) |

> **Why retired decisions stay listed.** Each of these appears in older material and in
> the research archive. A reader following a reference from that material needs to land
> on an explanation, not a gap — and in D2's case, on the fact that the design still
> exists and where it went.

---

## 5. Measurement backlog

Ordered by consequence. Every `TBM` in the document set has a row here.

### 5.1 Blocking or high-consequence

| # | Measurement | Blocks | Owner |
|---|---|---|---|
| M1 | **Gate controller branch current in all four gate states**, by inline meter or shunt on a mA range, or by low-range clamp with ten turns through the jaw and the reading divided by ten. LED lights separately accounted | **The top item — power-budget credibility.** First clamp figures fail their own sanity check | GateLink Impl Plan |
| M2 | **`MOVING` behaviour at the open limit and through the auto-close countdown** | Hold detection and the `hold_confirm_ms` default. If MOVING dips at the open limit, the confirmation timer must absorb it | GateLink Impl Plan |
| M3 | **IN5 (FIRE) and IN6 (alarm) idle and asserted voltages** | Sense polarity and idle state. Not a damage risk — the inputs are rated 5–36 V — but wiring them the wrong way round inverts an emergency alert | GateLink Impl Plan |
| M4 | **MPPT VE.Direct TX low excursion under a 10 kΩ load to GND**, preferably on a scope | **D25**, carrier BOM | GateLink Impl Plan |
| M5 | ~~**BLE RSSI to the BMS from the final StamPLC mounting position**~~ | **Superseded by M23 (2026-09-06)**, which asks the question the confirmed enclosure stack actually poses: multiple positions and orientations inside a reverberant steel cavity, with the Stamp-S3A's own antenna. M5's single-position wording predates that | GateLink Impl Plan |
| M6 | **Range and RSSI at ~500 ft on both bearings.** Procedure for the §7.6 precondition is `docs/rangetest/EIRP-SANITY-CHECK.md`; the reader is `tools/rangetest/eirp_check.py`. **Record conducted TX power in dBm**, not a RadioLib power index — the Heltec (≈13.9 dBm) and Wio (≈19.6 dBm) certified powers differ by ~6 dB and an index does not carry between them. **Run the M21 findings note §7.6 short-range RSSI/EIRP sanity check first**, so a gross power or antenna error is caught at 10 ft rather than at 500 ft; that check is also the named test for §7.4's antenna-counterpoise uncertainty. **Partly answered:** the 2026-09-04 walk closed at six positions to 106 m along the gate bearing at the −4 dBm ceiling. **The last ~46 m is unwalked — B1b** | **D1**, bridge antenna siting | Range Test Tasks |
| M20 | ~~**Ambient RSSI sweep of 902–928 MHz**, run at the bridge location **and** at the most distant node location~~ | **Field work done (2026-09-05).** R11 re-walk, seven sites, 902.0–927.8 MHz in 200 kHz bins, 130/130 bins, `dropped = 0`; committed as `docs/rangetest/data/2026-09-05-survey-campaign-r11.csv`. One confirmed in-channel occupant (`weather-island`, −80 dBm at 915.0), strongest near-band neighbour `gatelink-gate` at −66 dBm on 914.0, floor −115 to −118 dBm and uniform, every occupant bursty, no carrier anywhere in the band. **Residual done (2026-09-06)** by `tools/rangetest/survey_reintegrate.py`, output committed as `docs/rangetest/data/2026-09-06-m20-reintegration.csv`. **M20 is closed.** Results in §5.4 | **D1's frequency** (Protocol Spec §12.1) and **D33 standing condition 3** | Range Test Tasks |
| M7 | **BMS pack-current sign convention**, captured once under charge and once under load | Last open item in the BMS protocol (Protocol Spec §18, W6). Bit `0x4000` is believed to be the discharge flag but has only been observed at 0.0 A | GateLink Impl Plan |

### 5.2 Informative

| # | Measurement | Informs | Owner |
|---|---|---|---|
| M8 | **Whether the 12 V lamp output also tracks the auto-close countdown** | Viability of the lamp as a backup MOVING source if OUT2 is ever wanted elsewhere. If it does not track, it is not a drop-in | GateLink Impl Plan |
| M9 | Real EXIT→SAFETY gap times, by driving the vehicle | `detect_sequence_window_ms` default | GateLink Impl Plan |
| M10 | Exit wand hold / de-assert behaviour with a stationary vehicle | Debounce and re-trigger lockout. Some wands de-assert after a hold time, producing repeated edges from an idling vehicle | GateLink Impl Plan |
| M11 | Which loop input the loop detector occupies; whether EDGE is in use | Spare-capacity record | GateLink Impl Plan |
| M12 | Node supply current via the onboard INA226 | Budget confidence — the node measures itself, so this closes several rows by construction | GateLink Impl Plan |
| M13 | Enclosure temperature, seasonal, across LM75 + MPPT + BMS | **D29** | GateLink Impl Plan |
| M14 | One week of MPPT yield (H19/H20/H21) and battery Vmin baseline before install | Install go/no-go, and the *actual* present margin for free | GateLink Impl Plan |
| M15 | DIN-rail carrier selection, then cut the perfboard to it | Carrier build (**D27** sub-item) | GateLink Impl Plan |
| M16 | StamPLC IO schematic, netlist-level 3.3 V check | Confirms **D26**; cannot change it | GateLink Impl Plan |

### 5.3 Administrative

| # | Item | Blocks |
|---|---|---|
| M17 | **Copyright holder name for the LICENSE file** | **D31**, first public push |
| M21 | ~~**Confirm the SX1262 modules' own FCC grant conditions**~~ | **Done (2026-09-06).** Both grants recorded in `LRAN-M21-FCC-Grant-Findings`. Heltec `2A2GJ-HTIT` — finished-product, **not modular**, ≈13.9 dBm DTS, internal 3.0 dBi antenna declared and fixed. Seeed `Z4T-WIO-SX1262` — single modular approval, 92 mW, **no-co-location condition**. **Neither is §15.249**, and the fixed-channel no-hopping mode exists in both grants only at BW500. **D33 reopened** (§3.3); **D1 gains a fourth bound** (§2.1). Backlog gains M22 and M23 |
| M18 | ~~Protocol test vectors — fixed key, known frames, expected MACs and CRCs~~ | **Done.** `/tools/vectors/` holds 72 vectors from an independent Python generator, passing on host and on target with zero divergence. Protocol Spec **W4 is closed**; §13.2's standing requirement to regenerate on every protocol change continues to apply |
| M19 | Airtime table regeneration once D1 fixes SF/BW/CR | Protocol Spec §15.1 (W7) |
| M22 | **Bridge LoRa packet error rate with WiFi idle vs. saturated.** Run a sustained MQTT or iperf flood while the bridge receives a known `PING` sequence; compare PER and RSSI against the WiFi-idle baseline | Confirms the deliberate "**no** mutual exclusion on the bridge" policy (Bridge PRD). If PER degrades, the fallback is **physical antenna separation via the IPEX pigtail**, not firmware arbitration — ESP-IDF's coexistence arbitration has no visibility into an SPI-attached SX1262, so there is no hook to build on | Bridge Impl Plan |
| M23 | **BLE RSSI to the BMS from the Stamp-S3A at its final mounting position**, inside the plastic enclosure inside the closed **steel** gate-controller enclosure, ~6–8 in from the pack. Sample **at least three positions and two orientations** — both ends share one reverberant cavity, so the risk is a standing-wave null, not attenuation. In the same session, measure **LoRa-to-BLE isolation** by logging BLE RSSI with the LoRa transmitter keyed and unkeyed | **D28**, superseding **M5**. Prior figures (−80 dBm, and −50 to −60 dBm) both used a Heltec V3 rather than the Stamp-S3A's internal antenna. Run before committing the mounting hardware; it does **not** gate M6 or B1b. A poor reading is a cable, connector and null question before it is an antenna verdict | GateLink Impl Plan |

---

### 5.4 M20's re-integration — the channel evidence for D1, 2026-09-06

Derived from `2026-09-05-survey-campaign-r11.csv` by
`tools/rangetest/survey_reintegrate.py`; the tool is host-tested and refuses a trace
without hold discipline, because it ranks channels by peak. **No new field work was
required and none should be done** — re-walking to answer this would have been an
expensive, invisible mistake.

**The finding that changes D1, and it was not in the occupant inventory.** There is an
occupant cluster at **915.8–916.4 MHz**, seen at six of seven sites and by a wide margin
the loudest thing in the campaign: **−54 dBm at `bridge-house` on 916.0**, 62 dB above the
floor, with −86 and −80 dBm at `gatelink-gate` on 915.8 and 916.4. `bridge-house` was
measured indoors at the bridge's target location and the YoLink hub is inside the dwelling,
which is the obvious candidate — **unconfirmed, and it does not need confirming to be
avoided.**

This matters because it sits exactly where a reasonable person moves *to*. The inventory
recorded `weather-island` at −80 dBm on 915.0 as the one confirmed in-channel occupant, and
915.0 is the range test's provisional frequency; **nudging a few hundred kHz off it lands
in something 26 dB stronger.** By contrast the 915.0 signal appears at `weather-island`
only — every other site reads floor there — so it is local to that site, while the
915.8–916.4 cluster is property-wide.

**Envelope A — 125 kHz candidates in 915.2–923.0 MHz.** Scored on the worst peak across
sites, then on the strongest neighbour within ±600 kHz, since a candidate reading floor
next to a −54 dBm burst is an untested channel rather than a quiet one.

| Candidate | Worst peak, all seven | Worst peak, deployed sites | Nearest strong neighbour |
|---|---|---|---|
| **917.4 MHz** | −110.0 dBm | −111.0 dBm | −104 dBm at 917.0 |
| **917.2 MHz** | −109.0 dBm | −111.0 dBm | −104 dBm at 917.0 |
| **917.6 MHz** | −105.0 dBm (`propane-tank`) | **−112.0 dBm** | −104 dBm at 917.0 |
| 918.2 MHz | −109.0 dBm | −109.0 dBm | −103 dBm at 918.8 |
| *915.8 / 916.0 / 916.4* | *−78 / −54 / −80 dBm* | — | **the cluster above — avoid** |

**917.2–917.6 MHz is the recommendation**, and the two scorings agree on it. It is ~1.2 MHz
clear of the 915.8–916.4 cluster, its floor is the campaign-wide −116 dBm, and its nearest
neighbour of any strength is −104 dBm.

**Envelope B — the eight US915 500 kHz channels.** Their centres are exactly the DTS grant
range's endpoints, 903.0 + 1.6 MHz × k. Integrated floor is ≈ **−110 dBm**, i.e. 6.0 dB
above the per-bin −116 — the bandwidth penalty a BW500 receiver pays before any occupant.

| Channel | Integrated floor | Worst peak | |
|---|---|---|---|
| **909.4 MHz** | −109.6 dBm | **−107.0 dBm** | **clear — the pick if Envelope B is ever triggered** |
| 911.0 MHz | −110.3 dBm | −102.0 dBm | clear, second choice |
| 906.2 / 907.8 | −110.0 dBm | −96 / −89 dBm | occupied |
| 903.0 / 912.6 / 914.2 / 904.6 | ≈ −110 dBm | −79 / −77 / −66 / −64 dBm | **strong occupants — do not use** |

**Half the Envelope B grid is unusable**, including 914.2 MHz, where `gatelink-gate` — the
site GateLink will occupy — sees −66 dBm. That is worth carrying into any future Envelope B
trigger: the fallback is real but it is not a free eight-channel choice.

**One limit that post-processing cannot lift.** The survey measured 125 kHz every 200 kHz,
so **37.5 % of the band was never looked at** and a transmitter sitting entirely in a gap
is invisible at any level. Absence of a peak was already weak evidence (`data/README.md`);
the gaps make it weaker. This bounds every "clear" verdict above and is a reason to keep
`cad_backoffs` under observation after D1 rather than treating the channel as settled.

---

## 6. Changelog

- **v0.5** — **M20 closed.** Its M21 residual is done as post-processing on the committed
  R11 trace — `tools/rangetest/survey_reintegrate.py`, host-tested, output committed as
  `2026-09-06-m20-reintegration.csv` — and the results are in new **§5.4**. The
  re-integration found an occupant cluster at **915.8–916.4 MHz** that the original
  inventory missed, including a **−54 dBm** peak at `bridge-house` on 916.0, 62 dB above
  the floor and the loudest signal in the campaign; it sits exactly where a small move off
  the provisional 915.0 MHz would land. **917.2–917.6 MHz is the Envelope A
  recommendation** on both the all-sites and deployed-sites scorings, and **909.4 MHz** is
  the Envelope B pick — with the finding that **half the US915 500 kHz grid is unusable
  here**, 914.2 MHz included, where the GateLink site sees −66 dBm. D1's frequency now has
  a ranked, reproducible answer rather than a survey to read.
- **v0.4** — **M21 closed, D33 reopened, D1 amended, D28's outlook improved.** Neither
  radio module is certified under §15.249 — both carry §15.247 DTS **and** DSS grants — and
  D33's fixed-channel, no-hopping mode exists inside those grants **only at BW500** within
  903.0–914.2 MHz. Two envelopes are documented in `LRAN-M21-FCC-Grant-Findings`;
  **Envelope A (§15.249) is the plan of record**, and the reason is measured rather than
  modelled: the 2026-09-04 gate-bearing walk ran at its **−4 dBm conducted ceiling** with
  the confirmed 3.0 dBi antenna and closed at 0 % PER across all six positions, while the
  same walk at −9 dBm — the SX1262's hard floor — lost 12.5–25 % at SF7. Envelope B is
  retained behind three explicit triggers. **D1 gains a fourth bound** tying `BW` to the
  rule section (§2.1), and **§2.2** records what M21 changed for D1 and D28 as a dated
  entry rather than by editing their rows. Recorded separately in **§3.3**: the grants
  **do not transfer** — custom firmware, a non-modular Heltec grant, an external antenna
  outside the Heltec declaration, and a co-location requirement the Wio's modular grant
  forbids — so the operative frame is **§15.23 home-built**, permanently, and no node may
  be represented as FCC certified. §3.1 is left as written and annotated, per the rule that
  a dated record is corrected by a new entry rather than rewritten. Backlog: **M20's field
  work is marked done** (the 2026-09-05 R11 re-walk) with its M21 amendment reduced to
  post-processing of the committed trace; **M6 gains the conducted-power and sanity-check
  requirements** and records how far the walk actually reached; **M5 is superseded by M23**;
  **M21 closes**; and **M22** (bridge LoRa PER under WiFi load) and **M23** (BLE RSSI and
  LoRa isolation inside the steel enclosure) are added.
- **v0.3** — **D34 added, closing Protocol Spec W12**: §9.4 steps 4–6 are **split**
  rather than placed whole — steps 4, 5 and the state half of 6 become
  `lran::CommandGate` in `/lib/lran-protocol/`, dispatch stays in the application.
  Reasoning in **§3.2**, including the finding that changes the schedule: §9.2 makes
  every authenticated type bridge → node, so **steps 4–6 apply to nothing on the
  bridge today** and the real deadline is the first firmware that accepts a `COMMAND`
  — simnode B0 and GateLink M3, **not** the range test. Two consequences worth
  carrying: `dedup_cache_depth` becomes a `lran-config` parameter, and §10.4 is
  **silent on the check/execute/record window**, recorded here as a specification gap
  rather than patched locally.
- **v0.2** — **D32 and D33 merged in** from a standalone entries file, which is now
  deleted; a second file holding decisions is exactly the split this register exists to
  prevent. **D32** fixes RadioLib as the SX1262 driver for every firmware in the repo.
  **D33** closes Protocol Spec **W5** — a single fixed channel, no hopping, at or below
  the §15.249 power provisions — and is recorded as *settled with standing conditions*
  rather than as a bare outcome, because the power ceiling is **EIRP** and a later move
  to higher conducted power, a larger fleet, or a co-channel occupant each reopens it.
  Their rationale beyond a table cell is in **§3.1**. **D1 is amended, not closed**: new
  **§2.1** records the three things that now bound it, the most consequential being that
  **range test results do not close D1 on their own** — the frequency needs the ambient
  survey and the power needs D33's grant conditions. Backlog gains **M20** (ambient RSSI
  sweep at both ends, blocking D1's frequency and D33's third standing condition) and
  **M21** (module FCC grant conditions); **M18 is marked done** with W4's closure; M6's
  owner moves to the range test tasks document. Parent link repaired for the `docs/`
  reorganization.
- **v0.1** — Initial release. Extracted from `lran-prd-v0_8` §13 and §13.1 so that
  decision churn no longer requires editing the system overview, and so that a single
  status is authoritative rather than restated across six documents. Content carried
  forward unchanged in substance; **D1–D31 retain their numbers**. Reorganized into
  open / resolved / retired sections, since a flat table of thirty-one rows in which
  twenty-two are done buries the six that need attention. Added an owner column mapping
  each decision to the document where its outcome is written up. The measurement backlog
  is renumbered **M1–M19** and split by consequence — previously it was an unordered
  list whose "top item" was identified only in prose.
