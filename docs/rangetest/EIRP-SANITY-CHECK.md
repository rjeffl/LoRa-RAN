# The §7.6 EIRP sanity check — procedure

**Document:** `EIRP-SANITY-CHECK`
**Status:** Procedure. Written 2026-09-06, not yet run.
**Source requirement:** [`LRAN-M21-FCC-Grant-Findings`](../shared/LRAN-M21-FCC-Grant-Findings.md) §7.6
**Gates:** **M6** — the register's M6 row requires this to run *before* the 500 ft passes
**Tool:** `tools/rangetest/eirp_check.py`

> **Read [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md) first**, in particular *"Power down
> every board you are not measuring with."* An idle ARMED board beacons once a second and
> has already cost one campaign up to 60 % PER. **Three boards are connected to this
> bench.** Two of them are in this measurement.

---

## 1. What this is for, and what it cannot do

There is no RF power meter on this project. The D33 EIRP ceiling is therefore met by
**calculation** (findings §7.2) and the calculation is checked against the only instrument
on hand — the SX1262's own RSSI at a known short distance.

**The three checks are not equally strong, and the order matters.** A reader who takes the
absolute number as the headline has read this backwards.

| # | Check | Strength | Catches |
|---|---|---|---|
| **1** | **Power step** — RSSI difference between the sweep's two conducted powers | **Sharp, and immune to geometry.** Distance, height, multipath and antenna gain all cancel in a difference | A requested power that never reached the PA. Reads 0 dB when it did not |
| **2** | **D33 clamp** — the high point's logged conducted power | **Binary, and this is the one that matters most** | A clamp that did not run. Not a 5 dB step but a ~26 dB one |
| **3** | **Absolute EIRP back-out** — `EIRP = RSSI + FSPL(d) − G_rx` | **Coarse. ±6 dB at best** | A disconnected antenna, a configuration that silently did not apply |
| **4** | **Path-loss slope across distances** | Tells you whether **3** is worth reading at all | A reflective site, where the absolute figure measures the ground |

**Check 3 is not a compliance measurement and findings §7.6 does not claim it is.** SX1262
RSSI is good to roughly ±3–6 dB, the antenna's 3.0 dBi is an unverified vendor claim, and
ground reflection at these distances moves readings several dB on its own. It is a
gross-error detector. Quoting it as an EIRP measurement would be exactly the kind of
unauditable number §7.3 exists to prevent.

### Why check 2 is the valuable one

The default sweep plan's two power points are `kSx1262MinDbm` and **`kSx1262MaxDbm`** —
−9 dBm and **+22 dBm as requested** — and the high point reaches the air only after
`clamp_conducted()` has brought it down to the D33 ceiling (`sweep.cpp`, `phy_params.cpp`).

**Nothing in this repository has ever verified that path over the air.** The clamp is host
tested and the ceiling arithmetic is host tested, but "the number the firmware computed is
the number the PA emitted" has only ever been assumed. This check closes that by
construction, because a broken clamp does not produce a wrong-looking reading — it produces
a 26 dB step where a 5 dB one was expected, and it is a **compliance fault** rather than a
measurement error.

---

## 2. What you need

| | |
|---|---|
| Boards | **Two Heltec V3s** — a matched pair, same module, same antenna, same configured gain. Start here rather than with the XIAO; see §7 |
| Antennas | The **19 cm 3.0 dBi sticks**, one per board, **vertical**, connected **before power** |
| Third board | **Powered down.** Not in a backpack, not in `SURVEY`. Off |
| Firmware | **Reflash both boards from current `main`.** The check itself still needs no firmware change — but the PA record (handoff §6 requirement 7) landed 2026-09-06 and a board flashed before it produces a trace with no `pa_*` lines |
| Site | Outdoors, **grass not pavement**, clear line of sight, nothing metal within a couple of metres of either end |
| Measure | A tape or a laser. **Not** GPS — the walk already learned that arcsecond fixes cannot support a distance-dependent reading |
| Mounts | Two non-metallic stands — tripods, plastic buckets, a wooden post. Both ends at the **same height** |

> **Never transmit without the antenna connected.** The GateLink expansion-board notes say
> this for +22 dBm; it applies to any power, and a disconnected antenna is also one of the
> failures this check exists to detect — detect it in the trace, not by damaging a PA.

---

## 3. Geometry — the part that decides whether check 3 means anything

**This is the hard part, and the repository already has the evidence for why.** The
engineering log's 2026-09-05 entry records the Heltec bench reference moving from
**−24 dBm to −42 dBm between two runs on board placement alone**. That is 18 dB of
geometry against the ±6 dB the absolute check is trying to resolve. An uncontrolled desk
run cannot answer §7.6, and running one anyway produces a confident wrong number.

**Three rules, in order of how much they buy.**

1. **Same height, both ends, as high as the stands allow — 1.5 m or better.** Ground
   reflection is the dominant error at these distances and its strength falls as the
   antennas rise.
2. **Grass, not pavement or water.** Reflection coefficient at 915 MHz is materially lower
   over vegetation. A gravel drive is the worst of both.
3. **Keep bodies out of the path.** A person is a lossy dielectric the size of several
   wavelengths. Stand to the side, and put the tethered board far enough from the laptop
   that the screen is not in the first Fresnel zone. **If the operator must stand near one
   end, stand in the same place for every distance** — a constant error cancels in check 1
   and shifts only the intercept in check 4.

### Distances: double them

Use **3 m, 6 m and 12 m**, and add 24 m if the site allows. Doubling is chosen so the
expected step is a round number: free space loses exactly **6.02 dB per doubling**, so the
readings should fall −6, −6, −6 and any departure is visible without arithmetic.

**Three distances is the minimum**, because the slope fit needs three points and the slope
is what tells you whether to believe the absolute figure at all. **Checks 1 and 2 work at a
single distance** — if the site only permits one, run it anyway and accept that check 3 is
unvalidated.

> **Expect fringes, and do not be alarmed by them.** At 1.5 m height these distances sit
> below the two-ray breakpoint (~27 m), so direct and ground-reflected paths interfere and
> individual readings can sit several dB off the free-space line. That is what the fitted
> **rms residual** reports. A residual of 2–3 dB is a normal outdoor short-range result; a
> residual above ~5 dB means the absolute figure should be discarded and only checks 1
> and 2 carried forward.

---

## 4. Running it

**Two boards. The initiator is tethered and fixed; the responder walks.** This is the R10
walk flow at short range, so the mechanics are already proven.

**Step 0 — power the third board down.** Confirm it, do not assume it.

**Step 1 — set the responder up at 3 m**, on its stand, on a power bank, antenna vertical.

**Step 2 — start the capture on the initiator.** One capture spans all three distances;
do not start a new one per position.

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset --role initiator \
    --out docs/rangetest/data/2026-09-06-eirp-sanity.csv \
    --note "findings 7.6 EIRP sanity check. Heltec pair, 3.0dBi sticks vertical, both ends 1.5m AGL on plastic stands, grass, clear LOS, operator standing 2m off-path at the initiator end for every position. P1=3.0m P2=6.0m P3=12.0m, tape measured. Third board POWERED DOWN."
```

**Read `board=` off the settings dump before going further.** A wrong board selection is
silent and writes the wrong pin map and antenna gain into a normal-looking CSV.

**Step 3 — press PRG on the responder.** The initiator boots ARMED and runs no sweep until
the first press, so **this position is P1**, not P0.

**Step 4 — wait for the sweep to finish.** The responder's display says when: the ARMED
beacon is distinguishable from a warmup probe precisely so it can. Do not move early.

**Step 5 — move to 6 m, press PRG. Then 12 m, press PRG.** Re-measure the distance with the
tape at each one; do not pace it.

**Step 6 — Ctrl-C the capture.** Confirm the file ends with a `# capture ended:` line.

**Step 7 — dump the responder's own log** as the cross-check the two-schema design exists
for: connect it and send `d`. `sum(probes_sent) − sum(echoes_recv)` from the sweep trace
should equal `sum(probes_sent) − probes_heard` from the responder's row for each position.
Disagreement is an instrumentation fault, not a link result.

---

## 5. Reading the result

```bash
python3 tools/rangetest/eirp_check.py \
    docs/rangetest/data/2026-09-06-eirp-sanity.csv \
    --distance 1=3.0 --distance 2=6.0 --distance 3=12.0
```

The tool exits non-zero on a failure and prints the findings in words. **Distances are
passed on the command line because the firmware has no way to know them** — the trace
records a position index, and the metres live in the capture note and in this invocation.
Record the exact command in the engineering log alongside the result.

### What each outcome means

| Result | Reading | Do this |
|---|---|---|
| **All four PASS** | The power setting applies, the clamp runs, the calculated EIRP is consistent with the measurement, and the geometry supported the comparison | Record it. **M6's precondition is met** |
| **1 fails** — step ≠ logged step | The requested conducted power is not reaching the PA. Check `paOptTable` selection and whether `setOutputPower` returned an error | **Stop.** Every power figure in every trace is suspect until this is understood |
| **2 fails** — a logged power exceeds the ceiling, or the high point logged +22 | **A compliance fault, not a measurement error.** The clamp did not run | **Stop transmitting.** This is the one outcome that is not a data-quality question |
| **3 fails** — >12 dB from calculated | Most likely a disconnected or damaged antenna, or a wrong `board=`. Check the connector first | Re-seat, re-run. If it persists with checks 1 and 2 passing, the antenna or the feedline is the suspect, not the radio |
| **3 WARNs** — 6–12 dB out | Within gross-error bounds. Read check 4 before concluding anything | If check 4 is also poor, the site is the explanation. Improve the geometry or accept checks 1–2 only |
| **4 WARNs** — slope far from −20 dB/decade | The site is reflective and the absolute figure measures the ground | **Discard check 3.** Checks 1 and 2 survive; they never depended on geometry |

### The numbers to expect

At **−4 dBm conducted with 3.0 dBi** — the Envelope A working point — EIRP is −1 dBm, so:

| distance | FSPL | expected RSSI |
|---|---|---|
| 3 m | 41.2 dB | ≈ **−39 dBm** |
| 6 m | 47.2 dB | ≈ **−45 dBm** |
| 12 m | 53.3 dB | ≈ **−51 dBm** |

At −9 dBm conducted, subtract 5 dB from each. All of these sit comfortably inside the
SX1262's linear RSSI range and well below receiver saturation, which is why 3 m is the
closest distance recommended and 1 m is not.

---

## 6. What this check does *not* discharge

- **It is not a compliance determination.** §15.23 asks for good engineering practice, and
  this check is part of that record. It is not a measurement against §15.249's field-strength
  limit, which needs a calibrated field-strength meter at 3 m.
- **It does not verify the antenna's 3.0 dBi.** The claim is a vendor figure for a
  half-wave-class part whose theoretical maximum is 2.15 dBi (findings §7.4). Check 3 folds
  antenna gain and RSSI accuracy into one ±6 dB band and cannot separate them. What it can
  do is rule out a *gross* gain error, which is the residual §7.4 actually left open.
- **It is no longer blocked on the PA record, which landed 2026-09-06.** Handoff §6
  requirement 7 — log the applied `paOptTable` entry and the `optimize` flag — is
  **implemented**: the boot record prints `pa_optimize`, `pa_duty_cycle`, `pa_hp_max`,
  `pa_val` and `pa_table`, and `capture.py` folds them into the trace header. At the −4 dBm
  working point the entry is `paDutyCycle = 1, hpMax = 2, paVal = 3`.
  **The boards have not been reflashed**, so a trace only carries the record once they are.
  Reflash before this run — it is the measurement the record exists to support.
- **It says nothing about the 500 ft path.** That is M6 and B1b.

---

## 7. The Wio repeat, which is worth doing and is not the same measurement

Run the Heltec pair first. It is the matched case: same module, same antenna, same
configured gain at both ends, so the uplink and downlink EIRP figures should agree and any
disagreement is instrumentation rather than hardware.

**Then repeat with the XIAO + Wio-SX1262 Kit as the initiator.** This is cheap once the
site is set up and it answers something the Heltec pair cannot: the Wio's own conducted
power at the same requested dBm. The two modules' *certified* powers differ by ~6 dB, and
findings §7.5 budgets ~3 dB for the gap between a requested figure and the connector — the
Wio's share of that is unmeasured.

**Two cautions.** The XIAO run is **not B1b** — B1b is the gate-bearing walk, and the
2026-09-05 desk runs are explicitly not it either. And the Kit is **not** GateLink's board:
GateLink uses the header board (p-6379) and only the carrier validates the carrier. What
transfers from a Wio EIRP check is the *module's* power behaviour, which is real and worth
having, and nothing about the pin map.
