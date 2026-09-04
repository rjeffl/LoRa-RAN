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

**No range data yet.** M6 is untouched, and D1 stays open pending M20 (R8's ambient
survey) and M21 (the modules' FCC grant conditions).
