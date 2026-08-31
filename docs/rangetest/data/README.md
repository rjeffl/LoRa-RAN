# Range test traces

Committed output of `firmware/range-test/`. **These files are the evidence for D1** and
the input to **W7**'s airtime regeneration.

A channel choice that cannot be justified from a committed measurement will be reopened
by the first unexplained `cad_backoffs` reading — Protocol Spec §12.1 says so directly.
That is what this directory is for.

Capture with:

```bash
python3 tools/rangetest/capture.py --port /dev/cu.usbserial-0001 \
    --out docs/rangetest/data/YYYY-MM-DD-<place>.csv --sweeps 1 \
    --note "bearing 120deg, 1.2m antenna both ends, dry, foliage full"
```

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
