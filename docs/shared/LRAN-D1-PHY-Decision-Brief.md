# LRAN D1 — PHY parameter decision brief

**Document:** `LRAN-D1-PHY-Decision-Brief`
**Version:** 0.1
**Status:** **D1 is open.** This document presents the options and a recommendation; it does
not record a decision
**Parent document:** [`LRAN-System-PRD`](../LRAN-System-PRD.md)
**Binding protocol:** [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) **v0.9**
**Decision status:** [`LRAN-Decision-Register`](./LRAN-Decision-Register.md) — **the only
place D1's status is recorded**
**Last updated:** 2026-09-10

> **This document decides nothing.** It assembles evidence that is currently spread across
> the register, the protocol specification, the M21 findings and the range-test engineering
> log, states the options, and recommends one. **When D1 closes, the register is the file
> that changes** — and this brief should then be marked superseded rather than edited to
> agree with it.

---

## Table of contents

1. [Why D1 is a decision and not a measurement](#1-why-d1-is-a-decision-and-not-a-measurement)
2. [The five knobs, and which are actually open](#2-the-five-knobs-and-which-are-actually-open)
3. [Envelope and bandwidth](#3-envelope-and-bandwidth)
4. [Frequency](#4-frequency)
5. [Spreading factor — the real decision](#5-spreading-factor--the-real-decision)
6. [Coding rate](#6-coding-rate)
7. [TX power](#7-tx-power)
8. [Recommendation, and what must move with it](#8-recommendation-and-what-must-move-with-it)
9. [Changelog](#9-changelog)

---

## 1. Why D1 is a decision and not a measurement

Register §2.1 names four bounds on D1. **All four have reported:**

| Bound | Closed by |
|---|---|
| TX power capped at the §15.249 EIRP ceiling | **D33** / **M21**, 2026-09-06 |
| Frequency requires an ambient survey at both ends | **M20**, field work 2026-09-05, re-integrated 2026-09-06 |
| The site's known occupants must be characterized empirically | **M20**, same trace |
| `BW` and the rule section are one decision | **M21**, 2026-09-06 |

Both bearings are measured (**M6**, closed 2026-09-09). Nothing is being waited on.
**What remains is choosing SF, BW, CR, frequency and conducted power in one motion and
recording it in the register.**

**D1 does not block B0 or B2.** Bench work runs on whatever provisional channel the range
test used. It blocks **M19**'s airtime regeneration, **W7**, and anything that ships — and
because remote nodes have no OTA, a PHY choice revisited after deployment is a USB reflash
at the gate. Close it before **B3**.

---

## 2. The five knobs, and which are actually open

| Knob | State | Where the evidence is |
|---|---|---|
| **Envelope** (rule section) | Effectively forced | `LRAN-M21-FCC-Grant-Findings` §6 |
| **BW** | Follows the envelope | Register §2.1, fourth bound |
| **Frequency** | Settled by measurement | Register §5.4 |
| **TX power** | Capped above, measured floor below | **D33**; register §2.2 |
| **CR** | Free — nothing measured constrains it | B1b, 24 configurations |
| **SF** | **Genuinely contested** | W9 and B1b point opposite ways |

---

## 3. Envelope and bandwidth

**Envelope A (§15.249) is the plan of record and should stay so.** Envelope B buys
conducted power — the Wio's tested 19.6 dBm against Envelope A's −4 dBm — at three costs:

- **BW500 is mandatory.** The fixed-channel, no-hopping mode exists inside both modules'
  grants only at BW500.
- **The channel set shrinks to 903.0–914.2 MHz**, eight US915 500 kHz channels, of which
  **half carry strong occupants** — including 914.2 MHz, where `gatelink-gate`, the site
  GateLink will occupy, reads **−66 dBm** (register §5.4).
- **D28 must be reopened in the same motion**, because the co-location arithmetic moves by
  more than 20 dB (M21 findings §6.2).

**Envelope A closed 0 % PER at both bearings at its own ceiling.** There is no measured
reason to spend the fallback, and its three triggers stay documented against the day there
is one.

**Envelope A therefore fixes `BW = 125 kHz`**, and the register's fourth bound is satisfied
by fixing both in the same motion.

> **Whichever envelope is chosen, the grants do not transfer.** The operative frame is
> **§15.23 home-built**, and **no node may be represented as FCC certified** in a README, a
> header, an enclosure label or HA device metadata (Protocol Spec §18.2).

---

## 4. Frequency

**Recommend 917.4 MHz.** Register §5.4 ranked the candidates on two independent scorings —
worst peak across all seven survey sites, and worst peak at the deployed sites only — and
both agree.

| Candidate | Worst peak, all seven | Worst peak, deployed | Nearest strong neighbour |
|---|---|---|---|
| **917.4 MHz** | **−110.0 dBm** | −111.0 dBm | −104 dBm at 917.0 |
| 917.2 MHz | −109.0 dBm | −111.0 dBm | −104 dBm at 917.0 |
| 917.6 MHz | −105.0 dBm | **−112.0 dBm** | −104 dBm at 917.0 |

917.6 edges 917.4 on the deployed-sites scoring and loses on the all-sites one. **The
difference is inside the survey's resolution and is not worth a decision** — any of the
three is defensible, and 917.4 wins on the scoring that uses more data.

**The trap is that a small move off 915.0 is worse than no move.** The range test's
provisional 915.0 MHz sits on `weather-island`'s own peak at −80 dBm, which is why it must
change. But the **915.8–916.4 MHz cluster is property-wide and peaks at −54 dBm at the
bridge's own location** — 62 dB above the floor and the loudest thing in the campaign.
Nudging a few hundred kHz lands in something 26 dB stronger than the signal being avoided.
917.4 sits ~1.2 MHz clear of that cluster.

**One bound on every "clear" verdict above.** The survey measured 125 kHz every 200 kHz, so
**37.5 % of the band was never looked at**, and a transmitter sitting entirely in a gap is
invisible at any level. This is a reason to keep `cad_backoffs` under observation after D1
closes rather than treating the channel as settled.

---

## 5. Spreading factor — the real decision

**Two measurements point opposite ways, and both are sound.**

| | SF7 | SF9 | SF12 |
|---|---|---|---|
| Mean SNR at the gate | 9.6 dB | 8.0 dB | 4.7 dB |
| Margin on the mean | 17.1 dB | 20.5 dB | 24.7 dB |
| **Worst single probe** | **2.2 dB, at −119.0 dBm** | 14.8 dB | 23.5 dB |
| PER at the gate, B1b | 0 % | 0 % | 0 % |
| `STATUS` (`0x10`) airtime | 164 ms | 534 ms | seconds |
| Maximum `PING` airtime | 348 ms | 1107 ms | ~8.4 s observed per probe |
| §12.3 `backoff_max_ms` default (500 ms) | **covers it** | **must rise above 1107** | far too small |

Margins are from B1b, 2026-09-09, **on the deployed pairing** — Heltec V3 indoors at the
bridge's target location, XIAO + Wio at the gate controller, within 6 in of where GateLink's
antenna will sit. Airtimes are Protocol Spec §15.1.

### The argument for SF7

**W9's case.** SF7 keeps §12.3's media-access defaults valid as written: a 222-byte frame is
348 ms and `backoff_max_ms` of 500 covers it. At SF9 it is 1107 ms and the default does not,
by more than a factor of two. Lower airtime is also lower channel occupancy and lower TX
energy per frame.

### The argument for SF9

**B1b's case.** One probe at the gate came within **2.2 dB of the demodulation limit** and
still decoded, with **15 dB of per-test-point RSSI spread**. SF9 buys about **13 dB of tail
margin** and cost nothing measurable in PER on the same walk.

**The mean is not the failure mode.** Both node sites are **obstruction-limited rather than
distance-limited** — a 24 in trunk on the gate path, a barn on the well path, and an exterior
wall at the bridge end of both, with **25–29 dB of excess loss over free space** at the
obstructed positions (register §5.1.1). Seasonal foliage change moves the mean; it moves a
tail that is already near zero into the floor.

### Why the recommendation is SF9

**The two costs are not comparable in kind.** SF9's cost is `backoff_max_ms`, which is
**runtime-configurable from HA by root `CLAUDE.md` rule 8** — a number changed from a
dashboard. SF7's cost, if the tail proves too thin in a wet January, is a **USB reflash at
the gate**, because PHY parameters are deliberately not runtime-configurable (Protocol Spec
§12.1) and GateLink has no OTA.

Spend the cheap knob to protect the expensive one.

**Channel occupancy does not argue against it.** Implementation Plan §9.4 records expected
occupancy as negligible — short frames on a 1–5 minute cadence per node — and the
media-access design is provisioned for a fleet that does not yet exist. **Nor does the power
budget:** GateLink Implementation Plan §9.7 finds the radio is not the constraint, the MCU
dominates while awake, and a 534 ms `STATUS` at SF9 is ~5 µAh.

**SF12 is over-insurance.** 23.5 dB of tail margin is more than the site needs, at airtime
that makes CAD and backoff genuinely awkward and at a probe period observed near 8.4 s.

### One alternative that did not exist before B1b

**A Heltec at the gate would see about 9 dB more margin than the Wio-based node**, because
the Wio's `(TX − RX)` sits ~3.1 dB below the Heltec's and the module's delivered power is
lower. If SF7's backoff simplicity is judged worth keeping, changing GateLink's radio is
another route to the same tail margin.

**It is a hardware change against a configuration change, and the configuration change is
cheaper.** Recorded because it is a real option and was raised rather than decided in the
range-test log.

---

## 6. Coding rate

**Recommend CR 4/5.**

All four coding rates closed **0 % PER** at the gate across B1b's 24 configurations, so
nothing measured constrains this knob.

**Extra FEC does not address the failure that matters here.** The SF7 tail is fade toward
the demodulation limit, and below sensitivity there is nothing to correct — the packet does
not arrive to be repaired. **SF is the lever for a fade tail; CR is not.** SF9 pulls it.

CR 4/5 also keeps §15.1's airtime table on its existing basis, so **M19** regenerates
against a single changed variable rather than two. CR 4/6 is defensible as roughly 20 % more
airtime for some burst tolerance against the bursty occupants M20 found; it is not
recommended, and it is a cheap thing to revisit if `cad_backoffs` or CRC errors say so.

---

## 7. TX power

**Recommend −4 dBm conducted, with the fitted 3.0 dBi antenna — the D33 Envelope A ceiling.**

**Do not derate below it for conservatism.** Root `CLAUDE.md` rule 10 is a measurement, not
a caution: the 2026-09-04 walk at **−9 dBm**, the SX1262's hard floor, showed **12.5–25 %
PER at SF7**, while the same walk at −4 dBm closed at 0 % across all six positions. A working
point below −4 dBm is both unnecessary and measurably worse.

**Record conducted power and antenna gain separately.** The ceiling is an EIRP figure and a
combined number cannot be audited. **Record dBm, never a RadioLib power index** — the Heltec
and Wio certified powers differ by about 6 dB, so an index does not carry between them.

---

## 8. Recommendation, and what must move with it

| Parameter | Recommended | Basis |
|---|---|---|
| **Rule section** | **§15.249, Envelope A** | Closed 0 % PER at its ceiling on both bearings |
| **BW** | **125 kHz** | Forced by Envelope A; bound to it as one decision |
| **Frequency** | **917.4 MHz** | Register §5.4, both scorings agree |
| **SF** | **9** | ~13 dB of tail margin for a runtime-configurable cost |
| **CR** | **4/5** | Nothing constrains it; FEC does not fix a fade tail |
| **TX power** | **−4 dBm conducted**, 3.0 dBi antenna | D33 ceiling; the floor measured worse |

**Five things move with the decision, and four of them are edits:**

1. **`backoff_max_ms` must rise above 1107 ms.** Suggest **1500**. This is the one
   configuration change SF9 forces (Protocol Spec §12.3), and it is the reason the SF choice
   is affordable.
2. **M19** — regenerate §15.1's airtime table at SF9 / BW125 / CR 4/5. **W7 follows.**
3. **Protocol Spec §12.1's frequency note** currently says D1 must move off 915.0 MHz. Once
   D1 fixes a frequency, that sentence describes a completed action.
4. **The register records the outcome.** D1's row moves from open to resolved, with the
   parameters and the reasoning. **This brief is then superseded**, not edited to match.
5. **The range-test firmware's provisional 915.0 MHz** sits on a confirmed occupant. It is a
   bench instrument and this is not urgent, but a re-run on the old channel produces data
   that will be distrusted later.

**Keep `cad_backoffs` under observation after D1 closes.** It is the instrument that would
catch an occupant the survey's 37.5 % coverage gap hid, and §12.3's defaults were chosen
against an empty channel.

---

## 9. Changelog

| Version | What changed |
|---|---|
| **v0.1** | Initial release — options and recommendation assembled for an open D1 |

- **v0.1** — Initial release. Written because **D1's inputs have all closed** — M6, M20 and
  M21 have reported — leaving a decision whose evidence was spread across the register's
  §2.1, §2.2 and §5.4, the M21 findings' §6, the protocol specification's §12 and §15, and
  the range-test engineering log's 2026-09-09 entry. **No new measurement, and none is
  needed.** The brief adds one judgement the sources state separately but never weigh
  against each other: **W9 wants SF7 and B1b's tail wants SF9**, and the tie-break is that
  SF9's cost is a runtime-configurable number while SF7's risk is a USB reflash at a gate
  with no OTA. **D1 remains open**, and the register remains the only place its status is
  recorded.
