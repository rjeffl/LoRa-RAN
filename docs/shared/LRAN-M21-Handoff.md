# M21 / D33 — Implementation Handoff

**Document:** `LRAN-M21-Handoff`
**Version:** 0.3
**Status:** Handoff. Implementation-facing companion to
[`LRAN-M21-FCC-Grant-Findings`](./LRAN-M21-FCC-Grant-Findings.md) v0.3.
**Date:** 2026-09-06

> **Division of labour.** The findings note holds the regulatory determination — grant
> records, the two envelopes, the EIRP calculation method, and the D1 determination. This
> document holds what falls out of it: site-specific engineering, firmware obligations,
> backlog changes, and the do-not list. Neither supersedes the other; they do not overlap.

---

## Table of contents

1. [Site configuration as confirmed](#1-site-configuration-as-confirmed)
2. [What the enclosure arrangement gets right](#2-what-the-enclosure-arrangement-gets-right)
3. [D28 — the BLE picture is better than v0.2 concluded](#3-d28--the-ble-picture-is-better-than-v02-concluded)
4. [Co-location engineering](#4-co-location-engineering)
5. [Radio mutual-exclusion policy — asymmetric by design](#5-radio-mutual-exclusion-policy--asymmetric-by-design)
6. [Firmware requirements](#6-firmware-requirements)
7. [New and amended backlog items](#7-new-and-amended-backlog-items)
8. [What the existing field and test data already answers](#8-what-the-existing-field-and-test-data-already-answers)
9. [Do-not list](#9-do-not-list)
10. [Changelog](#10-changelog)

---

## 1. Site configuration as confirmed

Confirmed **2026-09-06**, and this section is a **correction to v0.2**, which reasoned from
a single grounded metal enclosure with the StamPLC inside it and the BMS outside. The real
arrangement is two nested enclosures, and the difference reverses v0.2's central conclusion
about the BLE path (§3).

**The enclosure stack.**

```
steel gate-controller enclosure  (outdoors, at the gate)
├── Nice/HySecurity 1050 gate controller
├── MPPT charge controller
├── Reno loop detector
├── 100 Ah LiFePO4 pack + BMS              ← the BLE target
└── plastic GateLink enclosure             ← RF-transparent
    └── StamPLC (Stamp-S3A) + expansion board
        └── Wio-SX1262 IPEX
              → SMA-female bulkhead on the PLASTIC enclosure
              → SMA male-to-female jumper
              → SMA-female bulkhead on the STEEL enclosure
              → 3.0 dBi antenna, outside, vertical
```

| Item | Configuration |
|---|---|
| **LoRa antenna** | Third-party **19 cm stick, claimed 3.0 dBi, vertical**, same part both ends. Mounted **outside** the steel enclosure on an SMA bulkhead |
| **LoRa feedline** | IPEX pigtail + jumper + **three connector pairs across two bulkheads** — a real 0.5–1.5 dB term (findings §7.2) |
| **StamPLC BLE antenna** | Internal to the Stamp-S3A, no external option (**D28**), inside the plastic enclosure inside the steel one |
| **BMS / battery** | **Inside the same steel enclosure** as the GateLink node, **6–8 in** away |
| **Measured BLE RSSI to the BMS** | **−50 to −60 dBm**, taken **inside that steel enclosure** with a **Heltec V3 test board** |
| **Co-located radios** | LoRa + WiFi + BLE — a **hard project requirement** |
| **Site character** | Rural, significant distance between homes |
| **Primary objective** | Reliable operation without causing undue interference |

**The one fact that changes everything downstream: the BLE radio and the BMS are both
inside the steel box.** The link between them never crosses the steel wall.

---

## 2. What the enclosure arrangement gets right

Three things, all worth recording so a later mechanical change does not undo them by
accident.

**The LoRa antenna is external to the steel.** This is what makes the EIRP calculation
meaningful at all — an antenna inside a steel box has an unpredictable radiated pattern and
no amount of calculation establishes compliance. v0.2 added that the enclosure would act as
a ground plane and raise effective gain, and set the compliance-side assumption to 5 dBi on
that basis. **Findings §7.4 withdraws that**: the fitted 19 cm stick is a half-wave-class
part, largely decoupled from its mount, and the ground-plane gain mechanism belongs to the
quarter-wave case. The compliance assumption is the **nameplate 3.0 dBi**.

**The BLE radio and its target share one enclosure.** The steel wall is not in the BLE path
at all — it is around both ends of it. §3 is rewritten on this basis and v0.2's conclusion
there is withdrawn.

**The steel wall does sit between the LoRa antenna and the BLE receiver.** That part of
v0.2 survives and is the basis of §4's isolation argument. What v0.2 missed is that the
LoRa *feedline* runs inside the cavity, which is where §4's real risk now lives.

**What a mechanical change would invalidate.** Moving the antenna inboard of the steel, or
replacing the steel enclosure with a non-conductive one, breaks §4's isolation and the
premise that makes the EIRP figure meaningful. Changing the **plastic inner** enclosure
breaks nothing — it is RF-transparent and load-bearing for no analysis here.

---

## 3. D28 — the BLE picture is better than v0.2 concluded

**This section retracts v0.2's conclusion.** v0.2 held that the new BLE reading did not
close D28 because enclosure attenuation had become the dominant unknown in the path. That
rested on the StamPLC being sealed inside a metal box while the BMS sat outside it. **The
BMS is inside the same steel enclosure**, 6–8 in away (§1), and the link never crosses the
steel wall.

**What the corrected geometry does to each term.**

| Term v0.2 raised | Status under the confirmed geometry |
|---|---|
| Attenuation through the steel wall | **Not in the path.** Both ends are inside it |
| Coupling out via seams, gasket line, cable glands | **Not in the path**, for the same reason |
| The plastic inner enclosure | RF-transparent at 2.4 GHz. Negligible |
| Test board vs. Stamp-S3A antenna | **Still open.** A Heltec V3's antenna is not the Stamp-S3A's internal one |
| Position and orientation inside the cavity | **Still open, and now the dominant term** |

**A closed steel box containing both ends is a reverberant cavity, and that is broadly
favourable.** Energy that would radiate away in free space is returned, so mean received
power over a short intra-cavity path is typically *higher* than a free-space estimate, not
lower. The cost is not attenuation but **structure**: standing waves produce
position-dependent and orientation-dependent nulls, which can be deep and narrow. A cavity
does not attenuate a 6-inch link; it makes a particular six inches unpredictable.

That also makes the **−50 to −60 dBm reading far more representative than v0.2 allowed** —
it was taken inside this same steel enclosure, which is the deployed environment for that
half of the problem. What it does not carry across is the radio and antenna.

**On the discrepancy with D28's −80 dBm.** Still a 20–30 dB gap, still worth understanding
rather than silently reconciling, and cavity effects are now a strong candidate explanation
alongside the ones v0.2 listed: a reading taken outside the box, or at a null, or at a
different pack state. **D28's recorded −80 dBm is a dated observation and stays as
written** — per the repo's governing rule, it is corrected by a new dated entry, not
rewritten.

**What M23 is now for, and it is a much cheaper question.** Not "does the enclosure kill
the link" but "does the Stamp-S3A's internal antenna, at the final mounting position and
orientation, sit in a usable part of the cavity" — plus, in the same session, the §4
isolation measurement. A null is defeated by moving or rotating the node a few centimetres,
which is a mounting decision, not a redesign.

**Fallback pressure on D28 drops accordingly.** The SmartShunt physical-layer contingency
and the D30 co-processor stay recorded as fallbacks, but the case that would force one —
an enclosure that attenuates the BLE path beyond recovery — has been withdrawn. **Do not
retire them on this basis**; M23 has not run.

## 4. Co-location engineering

**Harmonic overlap is not a problem.** 915 MHz × 2 = 1830 MHz, × 3 = 2745 MHz. The 2.4 GHz
band is 2400–2483.5 MHz. Nothing lands in-band in either direction. The real mechanism is
**broadband noise and receiver blocking from a transmitter inches away**, not harmonics.

**The steel wall substantially mitigates the blocking case, and that part of v0.2 stands.**
The LoRa antenna is outside the steel; the BLE receiver is inside it. Isolation is no
longer the 20–30 dB of typical free-space spacing — the shield adds to it, plausibly a
great deal. **v0.2's claim that the two effects are coupled is withdrawn**, because §3
removes the BLE-path-through-the-wall term entirely: the isolation is now simply good, and
it is not traded against the BLE link's health. M23 still measures it, but as an
independent quantity rather than as the other side of the same coin.

**The coupling path that actually matters is inside the cavity, and v0.2 missed it.** The
LoRa feedline does not leave the box at the module. It runs from the Wio's IPEX to a
bulkhead on the plastic enclosure, across a jumper, to a second bulkhead on the steel —
**three connector pairs and two cable runs inside a reverberant metal box that also
contains the BLE receiver and the BMS**. Any leakage from a poorly made connector, a
damaged braid, or an unshielded pigtail radiates into a cavity it cannot escape, directly
onto the receiver it is meant to be isolated from.

Sharing that same cavity: the 1050's motor drive, the MPPT's switching converter, and the
Reno loop detector's oscillator. None of these is a new risk created by LRAN, but all of
them argue the same way.

**The mitigation is mechanical, not firmware.** Use properly shielded assemblies, keep the
internal run short and away from the motor harnesses (GateLink Impl Plan already requires
this for the reverse reason), and treat a bad BLE RSSI reading at M23 as a **cable and
connector fault first**, before concluding anything about the Stamp-S3A's antenna. It is
also an independent argument for keeping §5's mutual exclusion on GateLink: the interlock
costs nothing and it covers the one path that is hardest to characterise.

**Envelope A reduces the transmitted term by ~24 dB** relative to Envelope B, independent
of enclosure isolation. The working point is **−4 dBm conducted**, not v0.2's −9 dBm —
findings §7.5 withdraws that figure, which sat below the SX1262's hard floor:

| LoRa conducted power | Coupled into BLE front end (free-space isolation only) |
|---|---|
| +20 dBm (Envelope B, Wio at grant power) | ≈ −10 to 0 dBm — total blocking desense |
| **−4 dBm (Envelope A working point, findings §7.5)** | ≈ −34 to −24 dBm — disruptive but recoverable |

Add enclosure isolation on top and the Envelope A case is comfortable. **This is an
independent argument for Envelope A** that has nothing to do with interference to
neighbours — and it is why the findings note §6.2 requires D28 to be re-opened if an
Envelope B trigger ever fires.

---

## 5. Radio mutual-exclusion policy — asymmetric by design

Confirmed: strict mutual exclusion is **easy to enforce on GateLink** but **difficult on
the bridge**, because the bridge's WiFi must hold an active network connection.

**Adopt the asymmetry rather than working around it.** The radio that cannot be gated is
the one that needs protecting least:

| Node | Protected link | Typical RX level | Blocking headroom | Policy |
|---|---|---|---|---|
| **GateLink** | BLE to BMS | ≈ −50 to −60 dBm, measured **inside the deployed enclosure**; no wall in the path (§3) | good, pending M23's cavity check | **strict mutual exclusion** |
| **Bridge** | WiFi to AP | ≈ −40 to −60 dBm | 20–40 dB | **none, deliberately** |

**Why the reverse direction is low risk on the bridge.** WiFi TX desensing the bridge's
LoRa RX would require broadband PA noise at 915 MHz — 1.5 GHz from the WiFi fundamental
and well outside the SX1262 front-end passband — and harmonics do not help it either.
Heltec also certified both radios on this board as one finished product, so the
combination has been through a chamber at comparable powers.

**There is also no mechanism to build on.** ESP-IDF's coexistence arbitration exists
because WiFi and Bluetooth share one radio on the ESP32-S3. It has no visibility into an
SPI-attached SX1262. Strict mutual exclusion on the bridge is not a matter of effort — the
hook does not exist.

**No regulatory driver either.** Part 15 does not prohibit simultaneous transmitters; the
multi-transmitter procedures are a *certification* concern, and this project is on §15.23
regardless.

**Keep mutual exclusion on GateLink even though §3 and §4 both now read favourably.** It is
cheap, M23 has not run, and — the reason that survives a good M23 result — it covers the
in-cavity feedline coupling path in §4, which is the term least amenable to calculation and
the one most likely to degrade over years of outdoor thermal cycling on a connector.
Record the reasoning for the bridge's *lack* of it in the Bridge PRD so a future reader
does not mistake the asymmetry for an oversight.

---

## 6. Firmware requirements

Implementation obligations, not suggestions. Several exist specifically to make the §15.23
good-engineering-practice claim auditable.

**Status column added in v0.3.** Half of v0.2's list was already built — the range-test
firmware implemented D33 standing condition 1 at task R3/R4 — and a handoff that asks for
work already done wastes the next session's time.

| # | Requirement | Status |
|---|---|---|
| 1 | **TX power stored as two `lran-config` parameters**, `tx_conducted_dbm` and `antenna_gain_dbi`, never one combined EIRP figure | **Done in range-test**, owed by every other firmware. `PowerPoint` in `phy_params.h`; `csv.cpp:15` records D33 condition 1 as the reason for two columns |
| 2 | **Firmware computes EIRP and clamps**; no firmware that can key the radio may transmit above the configured envelope ceiling | **Done in range-test.** `conducted_ceiling_dbm()` / `clamp_conducted()` in `phy_params.cpp`, host-tested, and it **refuses** rather than clamping when the ceiling falls below the radio floor |
| 3 | **The envelope is a config parameter, not a constant** — A and B differ in *both* ceiling and legal bandwidth, and the clamp must reject a valid-in-one combination. **BW500 is mandatory under Envelope B; BW125 at Envelope B's power is the specific mistake to catch** | **Not built, and it is the only genuinely new obligation on this list.** `kEirpCeilingDbm10` is a compile-time constant and nothing couples BW to the envelope. Lands in `/lib/lran-config/` and the node firmwares, neither of which exists |
| 4 | **Range-test firmware must not expose "max power."** Sweep from the bottom and stop at the ceiling | **Done.** The sweep clamps every point through `clamp_conducted()` |
| 5 | **M6 logs conducted power in dBm**, never a RadioLib power index — Heltec and Wio differ by ~6 dB | **Done.** Every committed trace carries `conducted_dbm`, `antenna_gain_dbi10` and `eirp_ceiling_dbm` in its header and per row |
| 6 | **GateLink enforces LoRa/BLE mutual exclusion** — the BMS poll and the LoRa TX path share one interlock, not two independent schedulers | **Not built.** GateLink firmware does not exist. Recorded in the GateLink PRD |
| 7 | **Log the applied PA configuration at boot** — see the correction below | **Not built** |
| 8 | **Log BLE RSSI to the BMS as a diagnostic entity**, published to HA, so §3's residual cavity-null risk shows as a trend rather than as a silent failure — and so M23 gets a continuous dataset rather than one reading | **Not built.** Recorded in the GateLink PRD |

**Correction to v0.2's requirement 7.** v0.2 asked for RadioLib's low-power PA selection to
be verified and pinned at the working point. **The SX1262 has no low-power PA** — the LP PA
belongs to the SX1261. RadioLib 7.7.1 always configures the high-power PA
(`RADIOLIB_SX126X_PA_CONFIG_SX1262`) and derives `paVal`, `paDutyCycle` and `hpMax` from
`paOptTable` indexed by the requested power, unless `setOutputPower(power, false)` disables
that optimisation. There is no selection to make.

The real obligation is narrower and still worth having: **log which `paOptTable` entry was
applied and whether `optimize` was left true**, so a trace records the PA configuration that
produced its numbers. Two firmwares requesting the same dBm through different RadioLib
versions can land on different table entries.

**Two further notes for whoever builds requirement 3.**

- **The feedline term has no home in the current model.** `conducted_ceiling_dbm()` takes
  gain and returns a conducted ceiling; there is no `L_feedline` input. That is the
  compliance-side assumption (§7.2 of the findings note assumes 0 dB there) and is
  therefore *correct as it stands* — but a later reader will notice the omission, and the
  conservative direction is the one that must not be "fixed."
- **Repo rule 8 applies.** GateLink cannot be reflashed without a walk to the gate, so the
  envelope, the ceiling and the two power parameters are all runtime-configurable, in the
  same way and for the same reason as the timing constants.

## 7. New and amended backlog items

Register edits 9.1–9.6 in the findings note apply as written. **Two new items and one
amendment:**

```markdown
| M22 | **Bridge LoRa packet error rate with WiFi idle vs. saturated** — run a sustained MQTT or iperf flood while the bridge receives a known `PING` sequence; compare PER and RSSI against the WiFi-idle baseline | Confirms the "no mutual exclusion on the bridge" policy. If PER degrades, the fallback is **physical antenna separation via the IPEX pigtail**, not firmware arbitration | Bridge Impl Plan |
```

```markdown
| M23 | **BLE RSSI to the BMS from the Stamp-S3A at its final mounting position and orientation**, inside the plastic enclosure inside the closed steel gate-controller enclosure, ~6–8 in from the pack. Sample **at least three positions and two orientations** — the enclosure is a reverberant cavity and the risk is a standing-wave null, not attenuation. In the same session, measure **LoRa-to-BLE isolation** by logging BLE RSSI with the LoRa transmitter keyed and unkeyed | **D28.** Supersedes M5's scope. Both prior figures used a Heltec V3, not the Stamp-S3A's internal antenna; the −50 to −60 dBm reading was taken inside the deployed enclosure and is broadly representative of the environment, but not of the radio | GateLink Impl Plan |
```

**Amend M5** to point at M23 rather than duplicating it — M5's "final mounting position"
wording predates the confirmation of the enclosure stack, and it does not ask for the
multiple positions the cavity makes necessary.

**Correction to v0.2's framing of M23.** v0.2 wrote M23 as the measurement that would
settle whether the enclosure attenuates the BLE path fatally. Under the confirmed geometry
(§1, §3) there is no wall in that path and the question is smaller: antenna and cavity
position, not survival. M23 is still worth running before committing the mounting, and it
is no longer a gate on the design.

**The ordering dependency is weaker than v0.2 stated.** v0.2 argued M23 should precede the
range test because a D28 fallback would relocate the BLE radio, change the carrier layout,
change LoRa antenna placement, and therefore change the §7.4 gain assumption. Two links in
that chain have since broken: §7.4 no longer rests on the enclosure acting as a ground
plane, and the LoRa antenna's position is fixed by the steel bulkhead rather than by the
carrier. **Run M23 before committing the mounting hardware. It does not gate M6 or B1b.**

---

## 8. What the existing field and test data already answers

**This section is rewritten in v0.3.** v0.2 listed four open inputs. The repository holds
answers to three of them, and the fourth is now confirmed.

| v0.2 asked for | Where it is |
|---|---|
| Antenna part and claimed gain, both ends | **Confirmed 2026-09-06** (§1): 19 cm stick, claimed 3.0 dBi, vertical, same part both ends. Feedline is two bulkheads and a jumper. Findings §7.4 is rewritten on this |
| Path characterisation over the 500 ft run | **Partly measured.** The 2026-09-04 walk covers the gate bearing to 106 m with a per-position obstruction note, and the engineering log's 2026-09-05 excess-loss table quantifies it: 10.4 dB clear LOS, 16–29 dB vegetation and structure, ~35–49 dB through the barn. **The last ~46 m is unwalked — that is B1b** |
| Prior RSSI observations over the same path | **They exist and they are the walk.** Findings §8.2 now reads the budget off them instead of off free space |
| YoLink channel usage | **M20 answered it.** `weather-island` peaks −80 dBm at 915.0 against a −115 dBm floor; `propane-tank` −106 dBm at 915.2. Bursty, no carrier anywhere in 902–928. `irrigation-pump` was **retracted** under hold discipline |

**Genuinely still open, and neither blocks starting work:**

- **The conditions under which D28's −80 dBm figure was taken** (§3). Cavity nulls are now
  a candidate explanation alongside position and pack state. Worth knowing; not blocking.
- **The 500 ft leg itself.** B1b. This is the one measurement that would let M6 close.

**One trap the data already sprang, worth carrying into any new campaign:** an idle ARMED
board beacons once a second and cost up to 60 % PER on 2026-09-05, indistinguishable from
poor link margin. Read `docs/rangetest/FIELD-PROCEDURE.md` before M23 or B1b.

---

## 9. Do-not list

- **Do not represent any LRAN node as FCC certified**, and do not apply a "Contains
  transmitter module FCC ID: …" label. Applies to the README, LICENSE header, any
  enclosure label, and any HA device metadata.
- **Do not treat either grant as authorization.** They are evidence about the hardware.
  The findings note §5 gives four independent reasons they do not transfer.
- **Do not set BW without setting the envelope.** They are one decision. BW125 forces
  Envelope A; Envelope B forces BW500.
- **Do not carry a power setting between the Heltec and the Wio** as an index or a
  percentage. Only dBm conducted transfers.
- **Do not reason from a 2 dBi antenna.** The fitted part claims **3.0 dBi**, which is what
  the firmware, every committed trace and findings §7.4 use. The conducted ceiling is
  **−4 dBm**, not −3 and not −9.
- **Do not derate below −4 dBm conducted for compliance conservatism.** The SX1262's floor
  is −9 dBm and the site measured 12.5–25 % PER at SF7 there. A derate that reaches the
  floor removes the ability to derate at all — findings §7.5.
- **Do not let M20 be run 125 kHz-only.** If Envelope B is ever triggered the survey must
  support a 500 kHz channel decision, and re-running a field survey is expensive.
- **Do not reverse the §5 asymmetry** without re-reading the reasoning. The bridge's lack
  of mutual exclusion is deliberate.
- **Do not treat the −50 to −60 dBm BLE reading as the deployed figure.** It was taken
  inside the deployed enclosure, which is most of what matters, but with a Heltec V3's
  antenna rather than the Stamp-S3A's internal one. M23 is the figure that counts.
- **Do not change the *steel* enclosure to non-metallic, or add large apertures, without
  re-opening §4's isolation analysis.** The *plastic* inner enclosure is RF-transparent and
  carries no analysis — changing it invalidates nothing.
- **Do not treat a poor M23 reading as an antenna verdict** before ruling out the in-cavity
  feedline (§4) and a standing-wave null (§3). Move the node and re-measure first.
- **Do not re-walk the M20 survey.** It ran on 2026-09-05 and the trace is committed. The
  500 kHz re-integration and the envelope split are post-processing.

---

## 10. Changelog

- **v0.3** — **Rewritten against the confirmed enclosure stack and the repository's field
  data.** The mechanical arrangement is **two nested enclosures**, not one: a steel
  gate-controller enclosure holding the 1050, the MPPT, the loop detector, the battery and
  BMS **and** a plastic enclosure containing the StamPLC — with the LoRa antenna outside the
  steel via two bulkheads and a jumper. **§3 is retracted and rewritten**: the BLE radio and
  the BMS share one enclosure, so there is no wall in that path, "enclosure attenuation is
  the dominant unknown" is withdrawn, and M23 shrinks to a cavity-null and antenna question.
  **§4 keeps its isolation conclusion but gains the coupling path v0.2 missed** — the LoRa
  feedline runs *inside* the cavity that holds the BLE receiver, which is now the
  hardest-to-characterise term and an independent reason to keep GateLink's interlock.
  **§2 withdraws the ground-plane gain argument** (findings §7.4: the fitted antenna is a
  half-wave-class 19 cm stick, not a quarter-wave monopole) and the working point moves from
  −9 dBm to **−4 dBm** throughout. **§6 gains a status column** — five of its eight
  requirements were already built in the range-test firmware — and requirement 7 is
  corrected: the SX1262 has no low-power PA, so what is owed is logging the applied
  `paOptTable` entry. **§8 is rewritten from "what to look for" to "what the data already
  answers"**, three of v0.2's four open inputs being already in the repo. M23 is rescoped and
  its claimed ordering dependency on the range test is withdrawn.
- **v0.2** — Rewritten against confirmed site configuration (§1): external LoRa antenna on
  a grounded metal enclosure, StamPLC BLE operating inside the closed enclosure 6–8 in
  from the battery, and a measured BLE RSSI of −50 to −60 dBm from a Heltec V3 test board.
  New **§3** reassesses D28 in both directions — the new reading is 20–30 dB better than
  D28's premise and should become the working figure, but every measurement to date used a
  test board in open air, so **enclosure attenuation is now the dominant unknown** and
  D28 does not close. New **§2** records what the mechanical arrangement gets right and
  why changing it invalidates two analyses at once. **§4** revised: the grounded wall
  between the two antennas substantially mitigates LoRa-to-BLE blocking, and the isolation
  and BLE-path terms are coupled, so one measurement settles both. Backlog gains **M23**
  (BLE RSSI plus LoRa isolation in the final enclosure, superseding M5's scope) alongside
  **M22**. Firmware requirements gain a BLE RSSI diagnostic entity. The v0.1 errata
  section is removed — the findings note v0.2 now carries the Envelope A recommendation
  directly, and the two documents no longer overlap.
- **v0.1** — Initial handoff, carrying the Envelope A reversal as errata against findings
  note v0.1.
