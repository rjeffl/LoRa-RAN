# M21 — SX1262 Module FCC Grant Conditions and the D1 Envelope Determination

**Document:** `LRAN-M21-FCC-Grant-Findings`
**Version:** 0.3
**Status:** Findings note. Closes **M21**; reopens **D33**; adds a fourth bound to **D1**.
**Parent document:** [`LRAN-Decision-Register`](./LRAN-Decision-Register.md)
**Companion:** [`LRAN-M21-Handoff`](./LRAN-M21-Handoff.md) — implementation obligations and site-specific engineering
**Last updated:** 2026-09-06

> **Not legal advice and not a compliance determination.** This note records what the two
> grants say and what follows for D1 and D33. It certifies nothing. The §15.23 discussion
> in §5 is the frame under which this project is lawful; it is not a substitute for
> reading the rule.

---

## Table of contents

1. [Summary of findings](#1-summary-of-findings)
2. [Grant record — Seeed Wio-SX1262](#2-grant-record--seeed-wio-sx1262)
3. [Grant record — Heltec WiFi LoRa 32 V3](#3-grant-record--heltec-wifi-lora-32-v3)
4. [The finding that changes D33](#4-the-finding-that-changes-d33)
5. [Why the grants do not transfer to LRAN](#5-why-the-grants-do-not-transfer-to-lran)
6. [The two operating envelopes](#6-the-two-operating-envelopes)
7. [Calculating EIRP without instruments](#7-calculating-eirp-without-instruments)
8. [D1 determination](#8-d1-determination)
   — including [§8.2, what the site measured](#82-what-the-site-actually-measured-which-supersedes-the-calculation)
9. [Register edits](#9-register-edits)
10. [What remains unconfirmed](#10-what-remains-unconfirmed)
11. [Changelog](#11-changelog)

---

## 1. Summary of findings

1. **Neither module is certified under §15.249.** Both are certified under **§15.247**,
   each carrying two grants — one **DTS** (digital transmission system) and one **DSS**
   (spread spectrum). D33's premise, "at or below the §15.249 power provisions," is a
   *different rule section* from the one either module was tested against. The grants
   cannot discharge D33's "not a compliance determination" caveat; they answer a question
   D33 did not ask.

2. **D33's fixed-channel, no-hopping mode does not exist in either grant at BW125.** Both
   manufacturers split LoRa identically: the **500 kHz** channels are certified as
   **DTS**, the **125 kHz** channels as **DSS**, i.e. as frequency hopping. A single
   fixed 125 kHz channel is too narrow for DTS and is not hopping, so it is neither.
   **The only configuration where D33's operating mode and the grants overlap is BW500
   within the DTS frequency range.** This bears directly on D1's `BW`.

3. **The grants do not transfer to LRAN under any reading**, and — following confirmation
   that co-located LoRa, WiFi and BLE are a hard project requirement — that path is now
   **permanently closed**, not deferred. See §5. The operative frame is **§15.23
   home-built**.

4. **§15.249 is nonetheless sufficient for this link, and this is now measured rather
   than calculated.** The 2026-09-04 gate-bearing walk ran at the Envelope A ceiling and
   closed at all six positions (§8.1). Given the deployment is rural with significant
   distance between homes, and the stated primary objective is reliable operation without
   causing undue interference, **Envelope A is the plan of record and Envelope B is a
   documented fallback with explicit triggers.**

5. **The working point is −4 dBm conducted with the 3.0 dBi antenna, not −9 dBm.**
   v0.2 derived −9 dBm from a speculative 5 dBi ground-plane gain assumption stacked on a
   3 dB tolerance budget. Both terms are revised in §7.4 and §7.5: the fitted antenna is a
   half-wave-class stick that is largely decoupled from its mount, and −9 dBm is the
   SX1262's hard floor rather than a comfortable operating point. The field data settles
   it — −9 dBm showed 12.5–25 % PER at SF7 on this property where −4 dBm was clean.

---

## 2. Grant record — Seeed Wio-SX1262

| Field | Value |
|---|---|
| **FCC ID** | `Z4T-WIO-SX1262` |
| **Grantee** | Seeed Technology Co., Ltd. |
| **Date of grant** | 2024-10-16 |
| **Modular type** | **Single Modular Approval** (both applications) |
| **Rule part** | 15C (§15.247) |
| **Equipment classes** | DTS and DSS — two applications, one FCC ID |
| **Conducted power** | **92 mW (≈19.6 dBm)** on both applications |
| **Frequency range (DTS)** | 903.0 – 914.2 MHz |
| **Frequency range (DSS)** | 902.3 – 914.9 MHz |
| **Antennas on file** | Dipole, spring, FPC (separate spec sheets each) |
| **Test firm** | Shenzhen BALUN Technology Co., Ltd. |

**Grant conditions, as filed:**

- Power output listed is **conducted**.
- Valid **only when the module is sold to OEM integrators** and installed by the OEM or
  OEM integrators.
- Antenna installed to provide **≥20 cm separation from all persons**.
- Antenna **must not be co-located or operating in conjunction with any other antenna or
  transmitter**.
- **End-users may not be provided with the module installation instructions.**

The co-location clause is the one LRAN cannot satisfy — see §5.

---

## 3. Grant record — Heltec WiFi LoRa 32 V3

| Field | Value |
|---|---|
| **FCC ID** | `2A2GJ-HTIT` |
| **Grantee** | Heltec Automation Technology Co., Ltd |
| **Date of grant** | 2022-04-07 |
| **Series model** | `HTIT-WS` |
| **Modular type** | **Does not apply** — finished-product certification, **not** modular |
| **Rule part** | 15C (§15.247) |
| **Test firm** | Shenzhen Toby Technology Co., Ltd. |

**Certified emissions:**

| Service | Range | Conducted power |
|---|---|---|
| LoRa **DSS** (125 kHz) | 902.3 – 914.9 MHz | 22.2 mW (**≈13.5 dBm**) |
| LoRa **DTS** (500 kHz) | 903.0 – 914.2 MHz | 24.3 mW (**≈13.9 dBm**) |
| Bluetooth / BLE | 2402 – 2480 MHz | 4.5 mW / 2.6 mW |
| WiFi 2.4 GHz | 2412 – 2462 MHz | 44.8 mW |

The DSS/DTS split is labelled explicitly in the test report's EUT description
(`DTS: LoRa(500KHz): 903MHz-914.2MHz` / `DSS: LoRa(125KHz): 902.3MHz-914.9MHz`), which is
what makes finding 2 a read rather than an inference.

**Antenna declaration:** LoRa internal antenna, **3.0 dBi**; BT/WiFi internal antenna,
3.0 dBi; *"The mounting of the antenna is fixed to the radio module and no other antenna
should be used."*

**Three things to carry forward.**

- **Certified LoRa power is ~14 dBm, not the SX1262's 22 dBm.** A Heltec V3 driven at
  +22 dBm is roughly 8 dB outside its own grant.
- **The declared antenna is internal and fixed.** The V3 exposes an IPEX/U.FL connector
  and every practical range test uses an external whip. That is outside the declaration.
- **Heltec and Wio certified powers differ by ~6 dB.** Range test pass 1 (Heltec ↔
  Heltec) and pass 2 (Heltec ↔ Wio) are not the same link budget.

---

## 4. The finding that changes D33

D33 is recorded as *settled with standing conditions*, the first being that TX power stays
at or below the §15.249 ceiling, with the note: "Confirm the radio modules' own FCC grant
conditions … **before D1 fixes a number**." That confirmation is now done. It does not
support the rule section D33 named — though, as §8 concludes, it does support the
*ceiling* D33 chose, for reasons D33 did not state.

| | §15.249 | §15.247 |
|---|---|---|
| Ceiling | 50 mV/m @ 3 m ≈ **−1.2 dBm EIRP** | up to **1 W conducted** (DTS, or FHSS ≥50 channels) |
| Bandwidth requirement | **none** | **≥500 kHz** 6 dB bandwidth for DTS |
| Hopping requirement | **none** | required for the FHSS path, not for DTS |
| Frequency | anywhere in 902–928 | anywhere in 902–928 |
| PSD limit | n/a | ≤8 dBm in any 3 kHz |

The §15.247 DTS path imposes no hopping requirement — exactly what D33 wanted — at the
cost of the 500 kHz bandwidth floor. At 19.6 dBm spread over 500 kHz, PSD lands near
−2.6 dBm/3 kHz, so PSD is never binding for LRAN.

---

## 5. Why the grants do not transfer to LRAN

**Four independent grounds, the last of which is now permanent:**

1. **Custom firmware.** Certification attaches to the device *as tested, including its
   software*. LRAN drives SF, BW, CR, frequency and power arbitrarily through RadioLib
   (**D32**). The grant does not travel with an arbitrary reconfiguration.
2. **The Heltec grant is not modular.** "Modular type: does not apply." There is no
   mechanism by which it extends to a board LRAN builds.
3. **The Heltec antenna declaration excludes an external antenna**, which every practical
   deployment and range test uses.
4. **Co-location is a hard project requirement.** Confirmed 2026-09-06: co-located LoRa,
   WiFi and BLE transceivers are required, not incidental. The Wio's modular grant forbids
   co-location with any other antenna or transmitter, and **no version of this design
   satisfies that**. Record as closed, not deferred, so it is not re-opened as an option
   later.

**The frame that does apply is §15.23 (home-built devices).** Not more than five units,
built for personal use, not marketed and not offered for sale. Such devices are not
required to be certified, but the builder is expected to use **good engineering
practices** and should aim to meet the applicable technical standards. LRAN sits squarely
inside that.

**What that changes about M21's purpose.** The question was never "does the grant cover my
operating mode" — it cannot. M21 actually answers **"which technical standard am I
self-declaring against, and what does the hardware's own test data tell me about meeting
it."** On that reading the grants are excellent evidence: third-party proof that these
exact modules pass §15.247's bandwidth and power tests with a 2–3 dBi antenna.

**Corollary:** the repo must not represent LRAN nodes as FCC certified, and must not carry
a "Contains FCC ID …" label anywhere — README, LICENSE header, enclosure label, or HA
device metadata. That is a claim about a certified integration LRAN cannot make.

---

## 6. The two operating envelopes

### 6.1 Envelope A — §15.249, fixed channel — **plan of record**

| Parameter | Value |
|---|---|
| Rule | §15.249 |
| EIRP ceiling | ≈ **−1.2 dBm** |
| Bandwidth | **free** — BW125, BW250 or BW500 |
| Hopping | not required |
| Frequency | **free anywhere in 902–928 MHz** |
| Grant support | none — neither module was tested here |

**Frequency freedom is the underrated advantage.** Both grants stop at 914.9 MHz, and the
site's YoLink network is a consumer 915 MHz LoRa star network (register §2.1). Envelope A
permits the whole of **902–928 MHz**, including **915–928**, which is outside both grant
ranges. At 500 ft, a clean channel is worth more than raw power.

**Correction to v0.2: 915–928 MHz is not entirely outside the LoRaWAN US915 band plan.**
US915 *uplink* is 902.3–914.9 MHz, but its eight 500 kHz *downlink* channels occupy
**923.3–927.5 MHz**. Envelope A's genuinely uncommitted region is therefore roughly
**915.2–923.0 MHz**, plus a narrow strip above 927.5. This matters because M20 has run and
its trace covers 902.0–927.8 MHz: the survey can be read against that boundary rather than
against the wrong one.

**The measured occupancy does not leave either envelope clean.** Per the M20 re-walk trace
(`docs/rangetest/data/2026-09-05-survey-campaign-r11.csv`), `weather-island` peaks
**−80 dBm at 915.0 MHz** against a −115 dBm floor — the one confirmed in-channel occupant,
and it sits at the bottom edge of Envelope A's preferred region. `gatelink-gate` peaks
**−66 dBm at 914.0 MHz**, the strongest near-band neighbour at any node site, and that one
falls just outside Envelope B's 903.0–914.2 upper edge. Both are bursty rather than
carriers. **The range-test firmware's provisional 915.0 MHz is the confirmed occupant's own
peak** and D1 must move off it.

### 6.2 Envelope B — §15.247 DTS, fixed channel — **documented fallback**

| Parameter | Value |
|---|---|
| Rule | §15.247, DTS path |
| Conducted ceiling | 1 W by rule; **19.6 dBm (Wio) / 13.9 dBm (Heltec)** as tested |
| Bandwidth | **BW500 required** — BW125 and BW250 excluded |
| Hopping | **not required** |
| Frequency | **903.0 – 914.2 MHz only** |
| PSD | ≤8 dBm/3 kHz — not binding |
| Grant support | both modules tested in this mode (as evidence, not authorization) |

**Fallback triggers**, in the pattern used for the D30 co-processor:

1. M6 fails to close at SF12/BW125 on **both** bearings at the Envelope A ceiling, or
2. M20 finds no channel anywhere in 902–928 MHz with an acceptable noise floor at the
   **node** end, or
3. Measured PER at the working point leaves margin below ~10 dB after seasonal foliage
   change.

**If Envelope B is ever triggered, D28 must be re-opened at the same time.** The
co-location arithmetic changes by more than 20 dB — see the handoff note §4.

---

## 7. Calculating EIRP without instruments

No RF power meter or field-strength meter is available, so the ceiling is met by
**calculation with a documented tolerance budget**. This section is the good-engineering-
practice record §15.23 asks for.

### 7.1 EIRP vs ERP — fix this in the document set

```
ERP (dBd reference) = EIRP (dBi reference) − 2.15 dB
```

§15.249 is a field-strength limit: 50 mV/m at 3 m = **0.75 mW EIRP = −1.25 dBm EIRP**,
which is **−3.4 dBm ERP**. §15.247's antenna provisions are stated in dBi. **Work in EIRP
and dBi everywhere**, and sweep the document set for stray uses of "ERP" — a silent
2.15 dB error makes the record unauditable.

### 7.2 The calculation

```
EIRP (dBm) = P_conducted (dBm) + G_antenna (dBi) − L_feedline (dB)
```

**`L_feedline` is a real term on GateLink and it is not currently credited anywhere.**
The confirmed RF path is not one pigtail. The Wio's IPEX runs to an SMA-female bulkhead on
the *plastic* GateLink enclosure, an SMA jumper crosses to a second SMA-female bulkhead on
the *steel* gate-controller enclosure, and the antenna mounts on that outer bulkhead — a
pigtail, a jumper and three connector pairs, realistically **0.5–1.5 dB**.

Per §7.3 the two directions take opposite ends of that range: **0 dB for compliance**
(assume the loss is not there, so the EIRP estimate cannot be flattered by it) and
**1.5 dB for link budget**. The range-test firmware's ceiling computation has no feedline
term at all, which is the compliance-side assumption by construction and therefore correct
as it stands — recorded here so that a later reader does not mistake the omission for an
oversight and "fix" it in the direction that raises EIRP.

### 7.3 Assumption discipline — the two directions differ

| Purpose | Antenna gain assumption | Why |
|---|---|---|
| **Compliance headroom** | **highest plausible** gain | if the antenna outperforms its datasheet, you are over the limit |
| **Link budget** | **lowest plausible** gain | if it underperforms, the link does not close |

Never use one figure for both. Record which was used where.

### 7.4 The antenna, and why v0.2's 5 dBi ground-plane assumption is withdrawn

**The fitted antenna is confirmed (2026-09-06): a third-party 19 cm stick, claimed
3.0 dBi, vertically mounted, the same part at both ends.** It mounts on an SMA bulkhead on
the outside of the steel gate-controller enclosure at the GateLink end.

v0.2 argued that the metal enclosure acts as a ground plane and could lift the antenna
above its datasheet gain, so the compliance-side assumption should be raised to 5 dBi.
**That argument is withdrawn, because it was written for a different antenna.** At 915 MHz
λ is 32.8 cm, so 19 cm is ≈ 0.58 λ — a half-wave-class sleeve design with a matching coil,
not a quarter-wave monopole. The ground-plane gain mechanism the 5 dBi figure came from is
specific to the quarter-wave case, where the plane supplies the missing half of the
radiator. A half-wave stick carries its own counterpoise and is largely decoupled from what
it is bolted to; a nearby metal wall distorts its pattern more than it adds gain, and
pattern distortion is not a uniform increase in EIRP in the direction the rule measures.

**The compliance-side assumption is therefore the nameplate 3.0 dBi**, which is also the
figure the range-test firmware and every committed trace already use.

Two residual uncertainties are recorded rather than folded into the ceiling as a silent
derate, because a guessed derate is not auditable and this one would drive the design below
the hardware's floor (§7.5):

- **Counterpoise coupling.** The stick's claimed 3.0 dBi is a vendor figure for a
  half-wave-class part whose theoretical maximum is 2.15 dBi; it is already a mild
  overclaim, and coupling to the coax outer or the enclosure wall could add a decibel or
  two in some direction. **The check is §7.6's short-range RSSI run**, which is sensitive
  to exactly this and is now a precondition of M6.
- **Uncredited feedline loss.** §7.2's 0.5–1.5 dB is assumed to be zero on the compliance
  side, so any of it that is real is unclaimed headroom in the conservative direction.

Mounting the antenna external to the steel enclosure remains entirely correct, and is what
makes the EIRP calculation meaningful at all — an antenna inside a steel box has an
unpredictable and largely unknowable radiated pattern.

### 7.5 The working point, and the floor it must not be driven into

**The Envelope A working point is −4 dBm conducted with the 3.0 dBi antenna**, giving a
nominal −1 dBm EIRP at the §15.249 ceiling. This is what `conducted_ceiling_dbm()` in
`firmware/range-test/src/phy_params.cpp` already computes from a 3.0 dBi gain input, and
what the clean runs in the 2026-09-04 walk used.

**Why v0.2's −9 dBm is withdrawn — two independent reasons.**

*First, the arithmetic that produced it rested on the 5 dBi assumption §7.4 has now
withdrawn.* Recomputed at nameplate gain:

```
Envelope A EIRP ceiling               -1.2 dBm
Compliance-side antenna gain (§7.4)   -3.0 dBi
                                     ──────────
Target conducted power                ≈ -4 dBm
```

*Second, and more seriously: −9 dBm is the SX1262's hard floor, not a mid-range setting.*
RadioLib 7.7.1's `SX1262::checkOutputPower()` enforces a range of **−9 to +22 dBm**, and
`phy_params.h` already encodes this as `kSx1262MinDbm`. v0.2's design target of −9.2 dBm
was therefore **below what the hardware can emit**: `clamp_conducted()` would have returned
`BelowRadioFloor`, and any further derate — a higher gain assumption, a larger tolerance —
would have had nowhere to go. **A working point with zero downward adjustment range is not
a conservative choice; it is an unfalsifiable one.** At −4 dBm there are 5 dB of range left
between the design point and the floor, which is what makes the tolerance budget below
meaningful rather than decorative.

**Correction: the SX1262 has no low-power PA.** v0.2 stated that −9 dBm sits inside the
SX1262's LP PA range and asked for the LP selection to be verified and pinned. The LP PA
belongs to the **SX1261**. RadioLib always configures the SX1262's high-power PA
(`RADIOLIB_SX126X_PA_CONFIG_SX1262`) and selects `paDutyCycle`/`hpMax` from `paOptTable`
indexed by the requested power. **There is no LP/HP choice to make.** What is worth logging
at boot is the `paOptTable` entry actually applied and the state of `setOutputPower`'s
`optimize` flag — a real obligation, and a different one.

**Tolerance budget.** Budget **~3 dB against yourself** for the gap between RadioLib's
requested power and actual conducted power at the connector — PA calibration spread,
RF-switch insertion loss, and board matching, which differ between the Heltec and the Wio.
That budget is **not** subtracted from the working point here, because §7.4's assumptions
are already conservative in the same direction and stacking them is what produced the
sub-floor target. It is instead the tolerance the §7.6 sanity check is read against: a
measured EIRP within ~3 dB of the calculated figure confirms the setting applied; a larger
divergence is a defect to find, not a margin to absorb.

### 7.6 Sanity check using existing hardware

No power meter needed. Two nodes at a **known short distance with clear line of sight**,
log RSSI, back out EIRP:

```
EIRP = RSSI + FSPL(d) − G_rx
```

SX1262 RSSI is good to roughly ±3–6 dB — enough to catch a gross error such as the wrong
PA selected, a disconnected antenna, or a power setting that silently failed to apply.
**Run this before M6.**

---

## 8. D1 determination

**M21 is closed. D33 reopens. D1 gains a fourth bound and stays open.**

D33's own standing condition 1 anticipated this: *"a later move to higher conducted power
… reopens it."* The reopen is the mechanism working as designed. D33 also chose the right
*ceiling*, though for a reason it did not state — §15.249 was the only rule section
permitting a single fixed narrow channel, and it happens also to be sufficient.

### 8.1 The link budget

At 915 MHz over 152 m, free-space path loss is **75.3 dB**:

```
FSPL = 32.44 + 20·log10(0.152 km) + 20·log10(915 MHz)
     = 32.44 − 16.4 + 59.2 = 75.3 dB
```

Envelope A at the §7.5 working point, computed with **pessimistic** gain at both ends per
§7.3 — 0 dBi each, 1.5 dB feedline loss:

```
Conducted power           -4.0 dBm
Feedline loss             -1.5 dB
TX antenna gain (min)     +0.0 dBi
                         ──────────
EIRP (worst case)         -5.5 dBm
FSPL @ 152 m             -75.3 dB
RX antenna gain (min)     +0.0 dBi
                         ──────────
RX power (free space)    -80.8 dBm
```

**That figure is an a priori check and nothing more.** This property has been walked, and
the free-space number is not what the link sees.

### 8.2 What the site actually measured, which supersedes the calculation

The 2026-09-04 gate-bearing walk (`docs/rangetest/data/2026-09-04-walk-gatelink.csv`) ran
at **−4 dBm conducted / 3.0 dBi both ends — the Envelope A ceiling exactly** — with the
initiator at the bridge's real indoor location. Six positions out to 106 m along the gate
bearing:

| | SF7 @ −4 dBm (ceiling) | SF7 @ −9 dBm (floor) |
|---|---|---|
| Best position (P2, 40 m) | −75.2 dBm, 0 % PER | −80.7 dBm, 0 % PER |
| Worst position (P1, 85 m, vegetation) | **−96.8 dBm, 0 % PER** | −104.4 dBm, **12.5 % PER** |
| P3, barn in path | −95.9 dBm, 0 % PER | −103.3 dBm, **25 % PER** |
| Measured noise floor, all seven survey sites | ≈ −115 to −118 dBm | — |

**Two conclusions, and the second is the one that killed v0.2's working point.**

*Envelope A closes this link, measured rather than modelled.* At the ceiling the worst
position read −96.8 dBm, roughly **26 dB above SF7 sensitivity** and ~19 dB above the
measured floor, at 0 % PER. The gate at 152 m adds ~3 dB of free-space over P4's 106 m and
sits ~70 ft higher, which helps obstruction clearance. SF9 and SF12 add 6 and 14 dB on top.

*The v0.2 working point was the measured marginal case.* At −9 dBm the same two positions
lost packets at SF7 where the ceiling was clean. A design point chosen for compliance
conservatism landed on the one setting the site had already shown to be inadequate.

**Excess loss on this property is measured, not assumed.** The engineering log's
2026-09-05 table gives 10.4 dB for clear LOS, 16–29 dB through vegetation and the house,
and ~35–49 dB through the barn — **every figure bundling at least one exterior wall**,
because the bridge is indoors. A node on the SE face starts ~18.2 dB down on one at the
same range on the NW face. v0.2's blanket "20–30 dB excess loss" allowance is in the right
region but is not a substitute for the table, and no distance-based estimate reproduces
the structure term.

**What is still not measured: the 500 ft leg itself.** The walk reached 106 m. **M6 is not
closed and B1b is still owed.** The determination below is made on the strength of six
closing positions along the correct bearing, not on a completed 152 m link.

### 8.3 The determination

1. **D1 shall not fix `BW` independently of the rule section.** `BW` and the Part 15
   envelope are one decision. BW125 forces Envelope A; Envelope B forces BW500.
2. **Envelope A is the plan of record.** Envelope B is a documented fallback with the
   three triggers in §6.2. Rationale: the link **measured** clean at 0 % PER across six
   positions along the gate bearing at the §15.249 ceiling (§8.2), the deployment is rural
   with the stated primary objective of avoiding undue interference, and Envelope A
   additionally frees 915–928 MHz for channel selection — bearing §6.1's correction that
   923.3–927.5 is US915 downlink.
3. **D1's TX power figure is recorded as conducted power and antenna gain separately**,
   per D33 condition 1 — now more important, because §7.3 uses different gain figures for
   compliance and for link budget, and a single combined EIRP number cannot be re-derived.
4. **M20 is closed** (§9.3) — field work 2026-09-05, re-integration 2026-09-06. **D1's
   frequency must move off 915.0 MHz** and **must not move to 915.8–916.4**, which the
   re-integration found to be a property-wide occupant cluster peaking at −54 dBm.
   Recommendations: **917.2–917.6 MHz** under Envelope A, **909.4 MHz** under Envelope B.
   Decision Register §5.4.
5. **M6 records conducted power in dBm**, never a RadioLib power index, and runs the §7.6
   short-range sanity check first. The range-test firmware already logs `conducted_dbm`,
   `antenna_gain_dbi10` and `eirp_ceiling_dbm` as separate CSV fields, so this obligation
   is met by construction; the §7.6 check is the part still owed, and it is also the
   named check for §7.4's counterpoise uncertainty.
6. **D1 remains open** pending M6. What has changed is that the frequency now has a
   survey behind it, the power figure is bounded, calculable **and measured at the
   ceiling**, and the bandwidth is tied to the envelope. The residual question is the
   500 ft leg (B1b), not the envelope.

7. **The emerging shape of D1, recorded as evidence rather than as a decision.** SF9 at
   −9 dBm was 0 % PER at all six walk positions where SF7 was not, and SF9 costs a
   `backoff_max_ms` raise above its 1107 ms full-frame airtime (Protocol Spec §12.3, W9's
   finding). SF7 at the −4 dBm ceiling was also clean everywhere. Both are live; the
   choice is D1's and belongs in the register, not here.

---

## 9. Register edits

### 9.1 Move D33 from §3 to §2 (open decisions)

```markdown
| **D33** | **FCC Part 15 operating mode** — *reopened 2026-09-06* | Protocol Spec §18.1 | **Reopened by M21.** Neither module is certified under §15.249; both carry §15.247 DTS + DSS grants, and the fixed-channel no-hopping mode exists in those grants **only at BW500**. Two envelopes documented in the M21 findings note §6. **Envelope A (§15.249, BW125) is the plan of record** — the link closes with ~14 dB worst-case margin; **Envelope B is a fallback with three explicit triggers**. Standing condition 1 tripped exactly as written | Before D1 fixes a number |
```

### 9.2 Add to §2.1, D1's bounds

```markdown
- **The bandwidth and the rule section are one decision.** Per M21, a single fixed
  channel with no hopping exists inside the modules' grants **only at BW500**, in
  903.0–914.2 MHz (§15.247 DTS). BW125 forces the §15.249 envelope and its ≈ −1.2 dBm
  EIRP ceiling — which the link budget shows is sufficient, so **Envelope A is the plan
  of record**. D1 shall not fix `BW` without fixing the envelope in the same motion.
```

### 9.3 Amend M20 — noting that it has already run

**Correction to v0.2, which wrote M20 as pending.** The survey ran on **2026-09-05**: the
R11 re-walk, seven sites, 902.0–927.8 MHz in 200 kHz bins, 130/130 bins, `dropped = 0`,
committed as `docs/rangetest/data/2026-09-05-survey-campaign-r11.csv`. The occupant
inventory built from it is closed.

> **Done 2026-09-06, and M20 is closed.** `tools/rangetest/survey_reintegrate.py`, output
> committed as `docs/rangetest/data/2026-09-06-m20-reintegration.csv`. Results and the
> channel recommendations are in Decision Register **§5.4**. The headline is one the
> occupant inventory had missed: a **915.8–916.4 MHz cluster at six of seven sites**
> peaking at **−54 dBm**, sitting exactly where a small move off the provisional 915.0 MHz
> would land. **Envelope A: 917.2–917.6 MHz. Envelope B: 909.4 MHz**, with half the US915
> 500 kHz grid unusable at this site.

**The amendment below is therefore a post-processing task on a committed trace, not a
second field campaign.** 200 kHz bins re-integrate to 500 kHz arithmetically, and the
903.0–914.2 / 915–928 split is a partition of bins the trace already holds. Re-walking
seven sites to answer a question the existing data answers would be an expensive mistake.

```markdown
| M20 | ~~**Ambient RSSI sweep of 902–928 MHz**, run at the bridge location **and** at the most distant node location~~ | **Field work done (2026-09-05).** R11 re-walk, seven sites, 902.0–927.8 MHz in 200 kHz bins, committed. One confirmed in-channel occupant (`weather-island`, −80 dBm at 915.0); strongest near-band neighbour `gatelink-gate` −66 dBm at 914.0; floor −115 to −118 dBm and uniform. **Residual, and it is analysis not fieldwork:** re-integrate the committed trace over **500 kHz** as well as 125 kHz, and report occupancy separately for **903.0–914.2** (Envelope B) and **915.2–923.0** (Envelope A's uncommitted region per §6.1) | **D1's frequency**, **D33's envelope choice**, and D33 standing condition 3 | Range Test Tasks |
```

### 9.4 Amend M6

```markdown
| M6 | **Range and RSSI at ~500 ft on both bearings.** **Record conducted TX power in dBm**, not a RadioLib power index — the Heltec (≈13.9 dBm) and Wio (≈19.6 dBm) certified powers differ by ~6 dB. Run the M21 findings note §7.6 short-range RSSI EIRP sanity check **before** the 500 ft passes, so a gross power or antenna error is caught at 10 ft rather than at 500 ft | **D1**, bridge antenna siting | Range Test Tasks |
```

### 9.5 Close M21

```markdown
| M21 | ~~Confirm the SX1262 modules' own FCC grant conditions~~ | **Done (2026-09-06).** Both grants recorded in the M21 findings note. Heltec `2A2GJ-HTIT` (finished-product, not modular, ≈13.9 dBm, internal 3 dBi antenna declared); Seeed `Z4T-WIO-SX1262` (single modular, 92 mW, no-co-location condition). **Neither is §15.249.** D33 reopened; D1 gains a fourth bound |
```

### 9.6 Changelog entry for the register

```markdown
- **v0.4** — **M21 closed, D33 reopened, D1 amended.** Neither module is certified under
  §15.249 — both carry §15.247 DTS + DSS grants — and D33's fixed-channel, no-hopping
  mode exists inside those grants **only at BW500** within 903.0–914.2 MHz. Two envelopes
  are documented; **Envelope A (§15.249, BW125) is the plan of record**, the link budget
  showing ~14 dB of worst-case margin at 500 ft under pessimistic gain assumptions, with
  Envelope B retained as a fallback behind three explicit triggers. D1 gains a fourth
  bound tying `BW` to the rule section. M20's scope widens to 500 kHz integration and a
  903–915 / 915–928 split; M6 gains a conducted-power logging requirement and a
  short-range EIRP sanity check. Backlog gains **M22** (bridge LoRa PER under WiFi load)
  and **M23** (BLE RSSI from the Stamp-S3A inside the closed enclosure, superseding M5's
  scope). Recorded separately: the grants **do not transfer** to LRAN — custom firmware,
  a non-modular Heltec grant, an external antenna outside the Heltec declaration, and a
  co-location requirement the Wio's modular grant forbids — so the operative frame is
  **§15.23 home-built**, permanently, and the repo must not represent nodes as certified.
```

---

## 10. What remains unconfirmed

Two loose ends, neither blocking.

1. **Whether `HTIT-WB32LA` (the V3) is a listed model on `2A2GJ-HTIT`.** The grant's
   series model is `HTIT-WS` and the test report states all covered models share an
   identical PCB, layout and electrical circuit — a claim that does not obviously extend
   to the V3, a different board from the Wireless Stick. The covered-model list was not
   retrievable. **This changes no conclusion above**, because §5 establishes the grant
   does not transfer regardless; it matters only if the register wants to cite the Heltec
   grant as evidence for the V3 specifically rather than for the SX1262 front end
   generally. Resolvable from the grant's model-list exhibit on the FCC EAS.
2. **Which of the Wio's two applications is DTS and which is DSS.** The published summary
   lists both classes and both ranges, but the pairing was inferred from the Heltec
   report's explicit labelling of the same two ranges. The mapping is near-certain —
   903.0–914.2 is the eight 500 kHz LoRaWAN US915 channels, 902.3–914.9 the sixty-four
   125 kHz channels — but it is inference. Confirmable from the `Test Report_DTS` exhibit.

---

## 11. Changelog

- **v0.3** — **Reconciled against the repository and the committed field data**, which the
  session that produced v0.1 and v0.2 did not have. Four corrections, two of which move a
  number. **§7.4's 5 dBi ground-plane assumption is withdrawn**: the fitted antenna is
  confirmed as a 19 cm (≈ 0.58 λ) half-wave-class stick at a claimed 3.0 dBi, and the
  ground-plane gain mechanism the 5 dBi figure came from is specific to a quarter-wave
  monopole. **§7.5's −9 dBm working point is withdrawn and replaced by −4 dBm** — v0.2's
  target of −9.2 dBm sat *below* the SX1262's −9 dBm hard floor, leaving no adjustment
  range, and the 2026-09-04 walk shows −9 dBm was the measured marginal case at SF7 where
  −4 dBm was clean. v0.2's claim that −9 dBm sits in the SX1262's low-power PA range is
  **wrong** — the LP PA is the SX1261's; RadioLib always configures the HP PA — and the
  associated firmware obligation is restated as logging the applied `paOptTable` entry.
  **§9.3 corrects M20 from pending to done**: the field work ran on 2026-09-05 and the
  amendment is post-processing of a committed trace, not a second campaign. **§6.1 corrects
  the US915 claim** — 923.3–927.5 MHz is US915 downlink, so Envelope A's uncommitted region
  is ≈ 915.2–923.0 — and records the measured occupancy, including that the range test's
  provisional 915.0 MHz is the one confirmed occupant's peak. New **§7.2** paragraph makes
  `L_feedline` a named term now that the RF path is confirmed to cross two bulkheads. New
  **§8.2** replaces the free-space budget with the measured walk as the governing evidence
  and states plainly that the 500 ft leg is still unwalked.
- **v0.2** — **Recommendation reversed to Envelope A** following confirmation that the
  deployment is rural with significant distance between homes and that the primary
  objective is reliable operation without undue interference. §8.1 adds the link budget
  that drove it: the 500 ft path closes with ~44 dB free-space margin at SF9/BW125 under
  pessimistic gain assumptions at both ends, retaining ~14 dB after a 30 dB excess-loss
  allowance, so Envelope B's +17 dB is margin the link does not need and its larger
  interference footprint is contrary to the objective. Envelope B demoted to a fallback
  with three explicit triggers. New **§7** folds in the EIRP calculation method — the
  EIRP/ERP 2.15 dB distinction, split compliance/link gain assumptions, a
  ground-plane-raised compliance gain figure now that the LoRa antenna is confirmed
  external to a grounded metal enclosure, a 3 dB tolerance budget yielding a −9 dBm
  conducted working point, and an RSSI-based sanity check. §5 gains a fourth
  non-transfer ground — co-location is a confirmed hard requirement, closing the grant
  path permanently rather than deferring it. Register edits and register changelog
  updated to match.
- **v0.1** — Initial grant review closing M21.
