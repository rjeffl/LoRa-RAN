# LRAN Decision Register

**Document:** `LRAN-Decision-Register`
**Version:** 0.2
**Status:** Living document. Updated whenever a decision changes state.
**Parent document:** [`LRAN-System-PRD`](../LRAN-System-PRD.md)
**Last updated:** 2026-08-30

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

**Adding a decision.** New numbers continue from the highest issued, currently **D33**.
A decision belongs here rather than in a node document when its answer would change more
than one section, or when it is blocking work.

---

## 2. Open decisions

| # | Decision | Owner | Notes | Gate |
|---|---|---|---|---|
| **D1** | **LoRa PHY parameters** — SF / BW / CR / TX power | System PRD §5.1 | **Open, but bounded** — see §2.1. Pick after the range test at ~500 ft **on both bearings**. The protocol spec's airtime analysis establishes that SF may be chosen on **link margin alone, not on power** — SF9 is affordable if the link wants it. **TX power is capped by D33**; the **frequency requires the ambient survey (M20)** first. Range test results alone do not close this | Phase 1 |
| **D19** | **WellLink power source** | WellLink PRD | Mains vs. battery/solar. Determines whether the reserved RX duty-cycling design (Protocol Spec §17.1) is needed, and whether battery telemetry is required in the WellLink schema | Before WellLink design |
| **D25** | **VE.Direct TX translator** | GateLink Impl Plan | BSS138 retained by default but may fail against a weak symmetric 5 V driver. Settled by **one measurement**: 10 kΩ from the MPPT TX pin to GND with the port streaming, observe the low excursions. Fallback ADuM1201 or 74LVC1G17. **The BSS138 stays on the RX direction either way** | Before carrier build |
| **D28** | **BLE link margin from the StamPLC mounting position** | GateLink Impl Plan | The Stamp-S3A's 2.4 GHz antenna is internal to the DIN case with no external option, and the pack's own transmitter is weak (~−80 dBm from inches away, confirmed independently with a phone — this is the battery, not the test hardware). Measure RSSI from the intended mounting position. Fallbacks: SmartShunt, or the D30 co-processor | Phase 5 |
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

**W7 still follows.** The airtime table regenerates once SF/BW/CR are fixed (**M19**).

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
| **D33** | FCC Part 15 operating mode (**closes W5**) | **A single fixed channel, no frequency hopping, transmitting at or below the Part 15.249 power provisions.** Reasoning, link budget and standing conditions in Protocol Spec §18.1; it constrains §12.1 and §12.3 and **bounds D1** (§2.1). See §3.1 | Protocol Spec §18.1 |


### 3.1 Notes on D32 and D33

Both were settled on **2026-08-30**. Their outcomes are written up in the owning
documents; what follows is the part that has no other home.

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

---

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
| M5 | **BLE RSSI to the BMS from the final StamPLC mounting position** | **D28** | GateLink Impl Plan |
| M6 | **Range and RSSI at ~500 ft on both bearings** | **D1**, bridge antenna siting | Range Test Tasks |
| M20 | **Ambient RSSI sweep of 902–928 MHz**, run at the bridge location **and** at the most distant node location | **D1's frequency** (Protocol Spec §12.1) and **D33 standing condition 3**. The two locations do not see the same picture, and it is the node's noise floor that sets its margin. Also settles empirically whether the site's YoLink network is on a LoRaWAN band plan or proprietary | Range Test Tasks |
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
| M21 | **Confirm the SX1262 modules' own FCC grant conditions** — antenna type and gain, and what each grant assumes about power and hopping | **D33's "not a compliance determination" caveat**, and D1's TX power figure |
| M18 | ~~Protocol test vectors — fixed key, known frames, expected MACs and CRCs~~ | **Done.** `/tools/vectors/` holds 72 vectors from an independent Python generator, passing on host and on target with zero divergence. Protocol Spec **W4 is closed**; §13.2's standing requirement to regenerate on every protocol change continues to apply |
| M19 | Airtime table regeneration once D1 fixes SF/BW/CR | Protocol Spec §15.1 (W7) |

---

## 6. Changelog

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
