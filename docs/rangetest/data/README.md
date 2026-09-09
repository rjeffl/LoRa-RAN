# Range test traces

Committed output of `firmware/range-test/`. **These files are the evidence for D1** and
the input to **W7**'s airtime regeneration.

A channel choice that cannot be justified from a committed measurement will be reopened
by the first unexplained `cad_backoffs` reading — Protocol Spec §12.1 says so directly.
That is what this directory is for.

Capture with:

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py \
    --port /dev/cu.usbserial-0001 --reset \
    --out docs/rangetest/data/YYYY-MM-DD-<place>.csv \
    --note "bearing 120deg, 1.2m AGL both ends, +70ft relief, 3.0dBi, dry, foliage full"
```

Full option reference and recipes: [`CAPTURE-PY.md`](../CAPTURE-PY.md).

**PlatformIO's python, not a bare `python3`** — pyserial lives in PlatformIO's venv, and
the `python3` first on `PATH` is usually a different install that has never seen it.

`--reset` has the tool drive the board itself (reset, `--role`, `--key`, then capture).
With `--key` it also echoes the board's own `#` lines, so a setup command shows you its
confirmation instead of swallowing it.
Use it: two processes on one serial port open without an exclusive lock on macOS and split
the incoming bytes between them, which puts holes in the trace and looks like it worked.

**One capture spans the whole walk.** The default runs until **Ctrl-C**, which is what a
position walk (R10) wants: start it once, walk the positions, stop it when you come back.
Rows are appended as they arrive, so an unplugged cable or a sleeping laptop costs you the
rest of the walk and not the part already done. `--sweeps N` stops on its own after N
sweeps, for an unattended bench run.

`--idle-timeout` (default 1800 s) is measured from the **last serial byte**, not from the
start. The initiator is silent for the whole gap between positions while you walk, so a
wall-clock deadline would end the capture mid-walk.

Every trace ends with a `# capture ended:` line saying how it stopped. A file without one
was truncated by something that did not get to finish — treat the last sweep in it as
suspect.

## Two schemas in this directory

| Trace | First columns | Produced by |
|---|---|---|
| **Sweep** (R7) | `position,tp_index,...` | INITIATOR, the position walk |
| **Survey** (R8) | `site_index,site_name,...` | SURVEY mode, the ambient scan |
| **Responder log** (R6) | `RESP,position,...` | RESPONDER, dumped at boot |

**Reading the two walk traces together.** For each position, the sweep trace's
`sum(probes_sent) - sum(echoes_recv)` should equal `sum(probes_sent) - probes_heard` from
the responder log's row for that position. When they agree, the instrumentation is
consistent and any loss is real. When they do not, one end counted something the other
did not, and the difference is the bug — not the link.

Separate schemas on purpose. A sweep row is a test point at a position with a link at the
far end; a survey row is a frequency bin with no far end at all. Widening one to cover
both would give every survey row twenty empty sweep columns and vice versa, and a reader
could not tell a real sentinel from a column that never applied.

`capture.py` recognises either and validates data rows against **that** header's field
count, so adding a column to one schema cannot silently start dropping rows of the other.

## File layout

Lines beginning `#` are comments. The first block records when the trace was captured
and any operator note; the second is the **R3 settings dump**, verbatim, so a trace
always carries the configuration that produced it. Then the header row, then data.

## Columns

27 columns. **Every value suffixed `10` is in tenths of its unit** — `-455` is
−45.5 dBm. No decimal points anywhere: the firmware computes in tenths and printing
decimals would insert a rounding step between the measurement and the file.

**`-32768` and `65535` are "not available" sentinels, never readings** (Protocol Spec
§4.6). A consumer must be able to tell "0.0 dB" from "no measurement", and at the far
edge of a walk the difference matters.

| # | Column | Meaning |
|---:|---|---|
| 1 | `position` | Operator position, incremented by PRG on the responder (R5) |
| 2 | `tp_index` | Test point within the sweep, 0-based |
| 3 | `freq_hz` | Carrier frequency |
| 4 | `sf` | Spreading factor, 7–12 |
| 5 | `cr_denom` | Coding rate denominator: 5 means 4/5 |
| 6 | `conducted_dbm` | **Conducted** TX power at the SX1262 |
| 7 | `antenna_gain_dbi10` | Antenna gain, tenths of a dBi |
| 8 | `payload_len` | Bench frame length in bytes, including its 16-byte header |
| 9 | `probes_sent` | Probes the initiator transmitted and counted |
| 10 | `echoes_recv` | Echoes that came back |
| 11 | `resp_heard` | Probes the **responder** heard — see below |
| 12 | `per_pct100` | Round-trip PER in hundredths of a percent; `10000` = 100% |
| 13–15 | `init_rssi_{mean,min,max}10` | RSSI at the **initiator**, on the returning echo |
| 16–18 | `init_snr_{mean,min,max}10` | SNR at the initiator |
| 19–21 | `resp_rssi_{mean,min,max}10` | RSSI at the **responder**, on the outbound probe |
| 22–24 | `resp_snr_{mean,min,max}10` | SNR at the responder |
| 25 | `phy_crc_err` | Frames that arrived and failed the PHY CRC (§14 stage 1) |
| 26 | `foreign` | Frames that parsed as LoRa but were not ours |
| 27 | `filler_err` | Frames that passed CRC but whose payload pattern was wrong |

### A `# board rebooted here` line mid-file

The initiator reset during the capture. Rows before the line are still real measurements
of real positions and are kept deliberately — an hour of walking is not thrown away
because the far end browned out. But **`position` is owned by the responder** (R5), so if
the *responder* was what restarted, its numbering restarts at 0 and positions after the
seam collide with earlier ones. Renumber from the operator's notes before merging the two
halves, or treat them as two traces.

## The survey schema (R8 / M20)

Ten columns. Same conventions as the sweep: **every value suffixed `10` is in tenths**,
and `-32768` is the "not available" sentinel, never a reading.

| # | Column | Meaning |
|---:|---|---|
| 1 | `site_index` | 0-6, the NVS slot the run was stored in |
| 2 | `site_name` | `bridge-house`, `gatelink-gate`, `weather-island`, `welllink-well`, `irrigation-pump`, `hopyard-lower`, `propane-tank` |
| 3 | `bin_index` | 0-129 |
| 4 | `freq_hz` | Bin centre. 902.0 MHz + 200 kHz x `bin_index`, so the top bin is 927.8 |
| 5 | `passes` | Complete sweeps of all 130 bins in this run |
| 6 | `samples` | RSSI readings folded into this bin |
| 7 | `peak_dbm10` | **Peak hold** across every pass |
| 8 | `mean_dbm10` | Mean over `samples` |
| 9 | `floor_dbm10` | Minimum — the noise floor |
| 10 | `dropped` | Samples refused after the 60000-per-bin cap |

The site travels in every row so **one file holds the whole campaign**. Seven separate
files would have to be correlated by filename, and a filename is not evidence.

### Why there is no bin at 928.0 MHz

Bin 129 is 927.8. A 125 kHz receiver centred on the band edge would be listening half
outside the band, and the reading would not mean what the column says it means.

### Read the peak and the absence of a peak differently

This is the one thing about a survey trace that is easy to get wrong.

One radio listening to one 125 kHz slice at a time samples each bin roughly **1/130 of the
time**. So:

- **The floor and the mean are solid.** The noise floor is stationary; any pass sees it,
  and the mean over hundreds of passes is a good figure. This is what sets a node's
  margin and what §12.1 actually asks for.
- **A peak well above the floor is real evidence of an occupant.**
- **The absence of a peak is NOT evidence of an empty bin.** A sensor keying up for 100 ms
  every five minutes is missed by most runs.

That asymmetry is why `passes` and `samples` are recorded per bin: a reader has to be able
to see how hard the survey looked before believing that it found nothing.

### `# hold_discipline=` — whether the peaks mean what they say

**R11, 2026-09-05.** Every site in a survey trace carries this line.

| Value | Means |
|---|---|
| `1` | The scan was **held between sites**. `peak_dbm10` is attributable to the site it is filed under. |
| `0` | The scan ran continuously between sites. Everything the operator's radio heard while walking to a site was folded into that site's run, and because the peak is a *hold*, one burst heard in transit is credited permanently to the destination. |

At `0`, **floor and mean survive; the occupant list does not.** Floor is stationary and
min-held, and a few minutes of walking against a five-minute dwell hardly moves a mean
already sitting on the floor.

**The flag is stored in the blob, not printed by the firmware doing the dump.** The
question a reader asks is how the data was *collected*, and the firmware reading NVS is
not the firmware that collected it. The first version of this line was a constant in the
dump path, which made a re-dump of the pre-R11 campaign claim a discipline it never had —
caught on hardware within an hour. Blob v1 has no flag and is reported as `0`, which is
correct: that firmware did not hold.

`2026-09-05-survey-campaign.csv` predates R11 entirely and has no such line at all. Its
peak column is caveated in its own header. `2026-09-05-survey-campaign-r11.csv` is the
first trace to carry `1`, at all seven sites.

### The survey does not tile the band — 62.5 % coverage

**This is the caveat to read before quoting any aggregate over more than one bin.**
The receiver bandwidth is **125 kHz** and the bins are **200 kHz** apart, so the survey
measured 125 kHz out of every 200 kHz and **37.5 % of the band was never looked at.**

For a **floor or a mean** this hardly matters: the noise floor is stationary and spatially
uniform, so scaling the measured slices up to the full channel width is sound, and
`survey_reintegrate.py` does exactly that. The arithmetic is worth stating because it is
the number Envelope B turns on — integrating thermal noise over 500 kHz instead of 125 kHz
raises the floor by `10·log10(500/125)` = **6.0 dB**, which is 6 dB of sensitivity a BW500
receiver gives up before any occupant is considered.

For a **narrowband occupant** it matters a great deal. A transmitter sitting entirely in
one of the 75 kHz gaps is invisible to this survey at any peak level, and no amount of
post-processing recovers it. **Read this together with the peak/absence asymmetry above:**
absence of a peak was already weak evidence, and the gaps make it weaker.

### The trace header carries the PA configuration, from 2026-09-06

**New fields, and no trace committed before this date has them.** The firmware prints five
`key=value` lines after the radio comes up and before the CSV header, so `capture.py`
collects them into the trace's own header block alongside the settings dump:

| Field | Meaning |
|---|---|
| `pa_optimize` | RadioLib's `setOutputPower` optimize flag, **as passed by this firmware**. `1` selects the measured `paOptTable`; `0` selects the datasheet default |
| `pa_duty_cycle`, `pa_hp_max`, `pa_val` | the PA configuration applied at the boot test point |
| `pa_table` | which RadioLib version's table the entry came from, e.g. `RadioLib-7.7.1-paOptTable` |
| `pa_entry=none` | printed **instead of** the three fields when the power is outside the SX1262's −9..+22 range. An explicit absence, not an omitted block |

**Why it is only the boot point.** The configuration is a pure function of conducted power,
and every row carries its own `conducted_dbm` — so with the flag and the table version on
the record, any row's entry is recoverable. The flag is the part that could not be
recovered, because RadioLib's one-argument overload hides it.

**These fields cannot be back-filled onto an older trace.** Deriving an entry needs the
flag, and the flag was never recorded before this date. A trace is a dated record: if you
need the PA configuration behind an older number, say what it was *probably* set to and why,
in a new note — do not write the fields into the file.

**The mirror behind them has its own check**, because `paOptTable` is file-static in
RadioLib and the SX1262's PA config cannot be read back:

```bash
python3 tools/rangetest/check_pa_table.py
```

### Columns 6 and 7 are separate on purpose

**D33 standing condition 1.** The Part 15.249 ceiling is on **EIRP**, which is conducted
power plus antenna gain. A single combined figure cannot be audited later, so both are
recorded and a reader can recompute the EIRP and check it:

```
EIRP_dBm = conducted_dbm + antenna_gain_dbi10 / 10
```

The firmware clamps every test point to the ceiling in code, not by operator discipline
— but the clamp's arithmetic is only checkable from a file that kept both terms.

### Columns 9, 10 and 11 separate downlink loss from uplink loss

Round-trip PER (column 12) conflates the two directions **deliberately**: a command that
gets no `COMMAND_ACK` has failed regardless of which leg dropped it, so round-trip is
what the system actually cares about.

These three columns recover the direction anyway:

| Comparison | Tells you |
|---|---|
| `probes_sent` vs `resp_heard` | **Downlink** loss — initiator → responder |
| `resp_heard` vs `echoes_recv` | **Uplink** loss — responder → initiator |

`resp_heard` is `65535` when no echo arrived at all: nothing is known about the
downlink, which is different from having heard nothing. **Do not read it as zero.**

An asymmetric link is the interesting case and the one this exists to catch. Both ends
run identical hardware at the same power, so a persistent gap means the *path* is
asymmetric — terrain, foliage, or one antenna sited worse than the other.

### Warmup probes are excluded

When the sweep changes radio configuration the responder needs time to find it. The
initiator sends uncounted **warmup probes** across that gap, marked as such on the wire
so neither end tallies them. They appear in no column here.

Without that, the responder's reacquisition time was charged to the link as packet loss
on the first test point of every configuration — measured at 2 of 8 entering SF9. See
the engineering log, 2026-08-31.

## Reading a trace

- **A whole row of sentinels with `per_pct100=10000`** means nothing came back at that
  test point. Expected at the far positions and at low power; that *is* the result.
- **`foreign` climbing** means other 915 MHz traffic. The site has known occupants — four
  YoLink sensors and a switch on an SX1276 (Decision Register §2.1) — and M20's survey
  is what characterises them.
- **`filler_err` non-zero** is not an RF fault. The frame passed its CRC and the bytes
  were still wrong, which points at a buffer or indexing bug. Treat it as a defect.
- **`phy_crc_err`** is §14 stage 1, the one discard path that cannot be produced at a
  desk. Seeing it at the edge of the walk is a result worth having.

## Traces

| File | What it is |
|---|---|
| `2026-08-31-bench.csv` | **Format proof, not range data.** Both boards ~1 m apart on the build-machine desk. Every point should read 0% PER; anything else is a firmware fault, not a link finding. Committed so the schema, the tooling and the reader above are exercised end to end before anyone walks a bearing. |
| `2026-09-04-walk-gatelink.csv` | **R10 position walk, and the first real M6 data.** Six positions on the gate bearing, P0 the fixed initiator at the house. 1152 probes, 2 lost downlink, 7 lost uplink; no dead test points. **Not a clear-field test:** the initiator was indoors at the bridge's target location, so every path crosses at least one framed wall, and P5/P6 cross the house. **Position 7 is not a location.** Height was 2–4 ft, not the 1.2 m the capture note claimed. All three are explained in the file's own header. Read it with the resplog below. |
| `2026-09-04-walk-gatelink-resplog.csv` | The responder's own log for that walk, six positions. Closes against the sweep trace at every one. |
| `2026-09-05-survey-campaign.csv` | **R8 / M20, all seven sites, 910 rows.** Re-dumped from NVS after the first capture lost 19184 bytes to a `capture.py` defect; see the engineering log, 2026-09-05. **`peak_dbm10` is not reliably site-attributable in this trace** — the scan ran while walking between sites. Floor and mean are sound. Site 0 `bridge-house` was measured **indoors** at the bridge's target location: its floor matches the outdoor sites, but its peaks are wall-attenuated and not like-for-like. |
| `2026-09-05-survey-campaign-r11.csv` | **The M20 re-walk, all seven sites, 910 rows.** The first trace with `# hold_discipline=1` at every site, so **`peak_dbm10` is site-attributable here** — this is the trace the occupant inventory is built from. 68–74 passes per site, 130 of 130 bins, `dropped=0` throughout. Site 0 `bridge-house` is again indoors at the bridge's target location, by design, and says so in its own note. **Supersedes the row above for peaks;** the pre-R11 trace is kept for its floor and mean, and as the record of what the transit contamination looked like. |
| `2026-09-05-bench-pass2-heltec.csv` | **Pass 2 bench reference, not range data.** Heltec V3 pair on the desk, pass 2 firmware. **192/192, 0% PER — reproduces `2026-08-31-bench.csv` exactly**, which is what Pass 2 Tasks §4.0.1 asked step 0 to prove: the display refactor was a no-op and the board selection is correct. **The third board was parked in `SURVEY` for this run**, and that is load-bearing — see the row below and the engineering log. |
| `2026-09-05-bench-pass2-xiao.csv` | **Pass 2 bench, and NOT the B1b delta.** XIAO ESP32S3 + Wio-SX1262 Kit as initiator, Heltec responder. **192/192, 0% PER — the first over-air proof that the Wio's discrete RF switch line works**; `begin()` returning success could not show this, because a wrong `rf_sw` initialises cleanly and transmits into a dead end. RSSI reads ~13 dB stronger than the Heltec run, **but bench geometry is uncontrolled and dominates**: the Heltec reference itself moved −24 → −42 dBm between two runs on board placement alone. The module contribution to link margin is B1b, on the gate bearing, and is not this number. |
| `2026-09-06-m20-reintegration.csv` | **DERIVED, not captured** — the only file in this directory that is not a measurement. M20's residual, added by M21: the R11 trace re-integrated over 500 kHz on the US915 grid for Envelope B, and split out per 125 kHz bin across Envelope A's uncommitted 915.2–923.0 MHz. Regenerate with `python3 tools/rangetest/survey_reintegrate.py <source> --out <this>`; **do not hand-edit it**, and if the source trace is ever superseded, regenerate rather than patch. Its own header carries the coverage caveat below. |
| `2026-09-07-eirp-sanity.csv` | **The §7.6 EIRP sanity check, and the run that closed it.** Heltec pair, matched module both ends, three tape-measured distances (3.0 / 6.0 / 12.0 m) at 1.45 m AGL on grass. **All four checks PASS**; 72/72 test points, 0% PER, zero error counters. **Check 2 is the one that mattered** — the D33 clamp reaching the PA over the air, which nothing in this repository had ever verified. Slope −15.8 to −16.3 dB/decade at **1.0–1.2 dB rms residual**, where the procedure calls 2–3 dB a normal outdoor result. **The capture ran with the field card's `--note` placeholders unedited**; the geometry was filled in the same day from the operator's account and the note says so. Read with its resplog. |
| `2026-09-07-eirp-sanity-resplog.csv` | The responder's log for that run. **Closes exactly** — 192 sent, 192 heard, 192 echoed, 192 received at all three positions, zero loss either direction. |
| `2026-09-07-eirp-sanity-xiao.csv` | **§7's Wio repeat.** XIAO ESP32S3 + Wio-SX1262 Kit as initiator, Heltec responder, same site and geometry. Checks 1, 2 and 4 pass; **the D33 clamp holds on the Wio as well**, which is the second module verified over the air rather than assumed. 72/72, 0% PER. Check 3's six WARNs are the module asymmetry below showing against a figure computed from the *requested* power — **not a geometry fault**, residual was 1.1–1.3 dB. |
| `2026-09-07-eirp-sanity-xiao-resplog.csv` | The Heltec responder's log for the Wio run. Closes exactly, 192/192/192/192. |
| `2026-09-07-eirp-sanity-swap.csv` | **The role swap, and a negative result worth keeping.** Heltec initiator, XIAO + Wio responder — the same pair with roles reversed, run to try to separate the Wio's transmit from its receive. **It cannot, and neither can any number of such runs:** swapping roles relabels which direction the tool calls uplink, path loss is reciprocal and cancels, so both runs measure the one combination `(TX−RX)_Wio − (TX−RX)_Heltec`. Kept as an **independent repeat with roles, tethering and which board walked all changed** — magnitudes agree within 0.21 dB. **Its absolute figures are not usable:** residuals 2.3–2.7 dB, check 3 FAILED at 12 m, one slope WARN, and the tool's own advice to discard check 3 fired correctly. Checks 1 and 2 stand. |
| `2026-09-07-eirp-sanity-swap-resplog.csv` | The XIAO responder's log for the swap. Closes exactly, 192/192/192/192. |
| `2026-09-09-b1b-walk-gate.csv` | **B1b, and the trace that closes the gate question.** Four sweeps in one file, not two — read them in **file order**, because the `position` column does not identify them and two different locations share `position=1`. Sweeps 1 and 2 are B1b proper: XIAO+Wio walking to **G1** (mid-driveway) and **G2** (the gate controller), Heltec #2 indoors at the bridge's target location. **The gate closed 192/192, 0 % PER, at every one of the 24 configurations**, at both −9 and −4 dBm conducted. Sweeps 3 and 4 are the optional Heltec A/B, both at the gate; **sweep 3 carries `position=0`, which the firmware is not supposed to produce**, and its first 8 test points are contaminated by placement. **The A/B is a board substitution at one position and it separates the Wio's TX term from its RX term** — see below. The capture note's placeholders were unedited and its "third Heltec POWERED DOWN" claim covers the first half of the run only; both are corrected in the file's own header. Read with both resplogs. |
| `2026-09-09-b1b-walk-gate-resplog-wio.csv` | The XIAO+Wio's own log, **sweeps 1 and 2 only**. Closes exactly, 192/192/192/192 at both positions. Its position log was cleared before the run. |
| `2026-09-09-b1b-walk-gate-resplog-heltec.csv` | Heltec #1's log, **sweeps 3 and 4 only** — the A/B at the gate, same mount as the Wio. Closes exactly at both. **Its `position=1` is the gate, not G1**; the Wio log's `position=1` is G1. |
| `2026-09-09-b1b-field-notes.md` | **Not a trace.** The operator's field notes for the B1b run: the two positions with decimal-degree fixes, AGL, elevation and obstructions, and the account of why a third and fourth sweep exist. It is the source the sweep trace's header annotation was filled in from, and the only record of the G2 mount and the 24 in trunk. **Its same-day clarification is what makes the A/B readable** — G2 is 2026-09-04's P1, all three gate sweeps used one mount, and the deployed GateLink antenna lands within 6 in of it. |
| `2026-09-05-w9-bench.log` | **W9 / R9, both runs, on the bench (2026-09-05).** §6.6.1's 222-byte maximum frame and §6.6.2's full 15-fragment set, 64 round trips, **zero faults at either end**; responder inbound agrees at 512 frames. **Not a link measurement and not a CSV** — see below. |

### The three 2026-09-07 EIRP traces are one measurement, and must be read as a set

They were captured the same evening at the same site, and **the matched pair is what makes
the other two readable**. Alone, a 3 dB asymmetry between the two legs of a link is a number
with no scale on it; against a same-module-both-ends run it is a module difference.

| Run | uplink − downlink | sd |
|---|---|---|
| `-eirp-sanity.csv` — Heltec pair, matched | **+0.39 dB** | 0.14 |
| `-eirp-sanity-xiao.csv` — Wio initiator | **+3.26 dB** | 0.36 |
| `-eirp-sanity-swap.csv` — Wio responder | **−3.47 dB** | 0.63 |

The matched pair establishes **+0.39 dB as the instrumentation floor**. The other two are
the *same* quantity measured twice with the sign flipped by the role relabelling, mean
magnitude **3.37 dB**.

**The finding is `(TX − RX)` for the Wio sitting 3.37 dB below the Heltec's** — and that is
as far as these traces go. It is equally consistent with a PA 3.4 dB weak or an RSSI reading
3.4 dB optimistic, and **reciprocal RSSI cannot choose between them at any number of nodes**:
for any pair, `P_ij − P_ji = (TX_i − RX_i) − (TX_j − RX_j)`. Separation needs an absolute
reference this project does not have. **Do not run a third permutation expecting a different
answer.**

**Neither reading is a compliance problem.** A weak PA sits further under D33's ceiling; an
optimistic RSSI means the Wio's transmit equals the Heltec's, which passed check 2 against
the −4 dBm ceiling at three distances. **Check 2 passed on the Wio's own hardware in both
Wio runs regardless**, and that is the check that carries the compliance weight.

### B1b: the gate closed, and the A/B split the Wio's TX term from its RX term

**The result.** At **G2, the gate controller** — and the field notes put the deployed
GateLink antenna **within 6 in of that spot**, so this is where the node will radiate rather
than near it — the deployed pairing — Heltec V3 indoors at
the bridge's target location, XIAO + Wio-SX1262 Kit at the gate — returned **every one of
192 probes across all 24 configurations, 0 % PER**, at both −9 and −4 dBm conducted, with
`phy_crc_err`, `foreign` and `filler_err` all zero. That is B1b's question and the answer
is yes.

**The margin is comfortable on the mean and thin in the tail.** Against the LoRa
demodulation limits, at the gate:

| SF | mean SNR | margin on the mean | worst single probe |
|---|---|---|---|
| 7 | 9.6 dB | 17.1 dB | **−5.3 dB, a 2.2 dB margin**, at −119.0 dBm |
| 9 | 8.0 dB | 20.5 dB | 14.8 dB |
| 12 | 4.7 dB | 24.7 dB | 23.5 dB |

One SF7 probe came within about 2 dB of failing and still decoded. Per-test-point RSSI
spread at the gate is 15 dB. **Read the mean and the tail separately when D1 picks an SF**:
SF7 is the choice that keeps §12.3's `backoff_max_ms` defaults valid under W9's airtime
table, and it is also the one whose margin at the gate occasionally approaches zero. SF9
cost nothing measurable in PER here.

**The two CRC errors are at G1, not at the gate.** G1 sits 16 dB stronger and produced two
`phy_crc_err` with `foreign=0`; the gate produced none. That is the signature of a bursty
local occupant rather than link margin, and the run used **915.0 MHz**, which the M20 survey
puts an occupant on.

**The Heltec A/B separates the Wio's transmit term from its receive term.** Operator
confirmation, same day: sweeps 2, 3 and 4 all put the responder in the **same place** —
resting on top of the gate controller enclosure, antenna vertical — and the initiator was
the same board in the same room throughout, tethered and untouched. **Only the responder
board changed**, so path loss cancels. §7 ruled out **role permutation** within a pair,
which remains true: for any pair, `P_ij − P_ji = (TX_i − RX_i) − (TX_j − RX_j)`, and no
amount of swapping escapes it. **Substituting one node against a common initiator at a
fixed position is a different experiment**, and it is the one this capture contains.

| | sum/2 = (init+resp)/2 | diff = init − resp |
|---|---|---|
| Wio at G2 (sweep 2) | −98.94 dBm | −3.27 dB |
| Heltec at G2 (sweep 4) | −89.69 dBm | −0.43 dB |
| **difference** | **−9.25 dB** = `(TX+RX)` term | **−2.84 dB** = `(TX−RX)` term |

**TX_wio − TX_heltec ≈ −6.0 dB. RX_wio − RX_heltec ≈ −3.2 dB.** First separation of the two
terms in this project.

**Carry the uncertainty with the numbers.** The two Heltec sweeps differ by 2.10 dB (−91.79
while the board was still being placed, −89.69 settled), so placement at that mount is worth
about 2 dB — an order of magnitude under the 9.25 dB gap, but it puts roughly ±1 dB on each
split term. **The sum term rests on one pair of sweeps; the difference term is measured twice
here** at two geometries 16 dB apart and matches the bench. Treat the split as approximate
and the difference as firm.

**The 2026-09-04 walk is not a control on this, and the temptation to use it is worth naming.**
That walk measured a **Heltec** at this same spot at −98.25 dBm sum/2 — 0.7 dB from this
capture's **Wio** and 8.6 dB from its **Heltec**. The 8.6 dB is not a board difference: only
two Heltecs existed on 2026-09-04, so both walks used the same pair, and `sum/2` is unchanged
by which end each board sat at. What was never pinned down is the **initiator**, specified in
that trace only as "the office on the NW side." **Indoor multipath at 915 MHz moves more than
8.6 dB over inches**, and this project has measured a desk rig wandering 24 dB between two
sweeps at nominally identical placement. Per-position AGL was recorded as a 2–4 ft range
rather than a value, and vegetation moisture differed. **A five-day-apart reading with the
indoor end respecified to a room is not a check on a same-hour substitution at one mount.**

**Compliance is unaffected and the direction is safe.** §7.6 check 3 backed every measurement
out **below** its calculated figure on both boards, so neither transmits above its setpoint,
and a Wio delivering ~6 dB less at the same commanded power sits further under the D33
ceiling. **Check 2 is a step-size test** — 5 dB expected against 26 dB for a broken clamp —
so it never could see a common-mode difference in delivered power between two boards, and its
passing on both is not in tension with this.

**Nothing above needs adjusting.** The gate sweep was taken **with the Wio**, so the 0 % PER
and every margin figure already carry the Wio's penalty. Those are the deployed numbers. The
corollary is new: **a Heltec at the gate would see about 9 dB more margin than GateLink
will** — relevant if the SF7 fade tail is judged too thin, and a question for GateLink's
module choice rather than for this directory.

**The difference term, at two geometries.** `init_rssi − resp_rssi` cancels path loss
whatever the siting, so these rows stand independently of everything above:

| pair | (init − resp) |
|---|---|
| Heltec ↔ **Wio**, G1 (−84 dBm) | **−2.91 dB** |
| Heltec ↔ **Wio**, the gate (−100 dBm) | **−3.27 dB** |
| Heltec ↔ Heltec, sweep 3 | −0.15 dB |
| Heltec ↔ Heltec, sweep 4 | −0.43 dB |
| Heltec ↔ Heltec, 2026-09-04, all six positions | −0.51 to −0.85 dB |

The Wio's `(TX − RX)` sits about **3.1 dB below** the Heltec's, at two geometries 16 dB
apart in received power. The Heltec-Heltec rows put the instrument's own bias at about
0.5 dB, so the figure is outside it. **This reproduces the 3.37 dB measured on the bench on
2026-09-07, over the air and at range** — the first confirmation that the asymmetry is not
a bench artifact, and the input the split above needs alongside the sum term.

### SF12 reads about 7 dB lower SNR than SF7 at the same RSSI, and always has

Pooled across the B1b sweeps: SF7 11.4 dB, SF9 10.1 dB, SF12 4.9 dB, at RSSI means within
1 dB of each other. **This is not a B1b finding and not a link property.** The same gap sits
in `2026-08-31-bench.csv` at −45 dBm (12.90 / 11.30 / 5.44) and in
`2026-09-05-bench-pass2-xiao.csv` at −33 dBm (12.46 / 10.97 / 5.08) — four received-power
levels spanning 75 dB, same offset. Treat it as a property of the estimator. It means SF12's
margin figures above are understated, not overstated.

### The W9 trace is not a CSV, and not a range measurement

`2026-09-05-w9-bench.log` is the odd one out in this directory and both halves of that
matter.

**Not a CSV.** W9 emits console lines only — a per-run tally, not a row per observation.
There is nothing to tabulate: a run is 32 PINGs that either all came back correct or did
not, and the interesting output is *which byte offset* diverged when one did not (§6.6.3).
A consequence worth knowing before you think something broke: **`capture.py` prints "no
CSV header seen" and exits non-zero on a completely successful W9 run.**

**Not a range measurement.** The two boards were ~1 m apart on purpose. W9 measures the
**protocol** — the codec, fragmentation, reassembly and the buffer path at
`LRAN_MAX_FRAME` — and the path is made trivial so that any fault is one of those and not
the RF. Do not read link margin out of it; that is what the sweep and the walk are for.

**M6 has data**: the link closes with margin at all six walked positions at the D33
ceiling. It is not closed — arcsecond GPS cannot support an RSSI-vs-distance curve (see
the log), so this trace answers "does it work there", not "what is the path loss".

**M20's occupant inventory is closed** by the R11 re-walk: seven sites, attributable
peaks, a uniform floor, one in-channel occupant confirmed and one retracted. **D1 now waits
only on M21** — the modules' FCC grant conditions, which is paperwork rather than bench
work.

### The channel is not clean everywhere

The in-channel result is the one thing to carry out of the survey. **Read it from
`2026-09-05-survey-campaign-r11.csv`** — it is the trace whose peaks are attributable.

**`weather-island` peaks at −80 dBm at 915.0**, against a −115 dBm median floor. It
reproduced within 1 dB across both campaigns, the second under hold discipline. The mean
in that bin sits at the floor, so it is rare bursts rather than a carrier: a collision risk
at that one site, not a blocked channel, and exactly what §12.1 expects to surface later as
`cad_backoffs`. `propane-tank` sees −106 at 915.2, also reproducing, also at floor in the
mean.

**One earlier finding was retracted by the re-walk.** The pre-R11 trace showed
`irrigation-pump` at −77 dBm at 915.2; under hold discipline that bin reads −112, the
floor. It was almost certainly picked up walking in, which is the exact failure the pre-R11
header warned about. **The property has one in-channel occupant site, not two.**

**The strongest near-band neighbour is at the gate:** `gatelink-gate` peaks −66 dBm at
914.0 MHz, 1 MHz off channel. Not in-channel at 125 kHz, but it is where GateLink will
live, and it only became visible once the peaks were attributable.

No bin at any site has a mean meaningfully above its own floor. **Every occupant on this
property is bursty; there is no carrier anywhere in 902–928.**

**Two of the interesting sites were the ones missing from the first, truncated capture**,
and that analysis concluded from the survivors that the channel was clean everywhere. Worth
remembering the next time a trace is short.
