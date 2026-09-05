# Range test — session handoff

**Written 2026-09-05, after the M20 re-walk was captured and analysed.**

> **This file goes stale.** It records *session state and next actions*, nothing else.
> Where it disagrees with the documents below, they win — check the engineering log's
> last entry against the date above before trusting anything here.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) — **the 2026-09-05 entries** | what happened and why. Seven entries that day: the field data, the tool defect that ate a third of the survey, R11, its own provenance bug, the boards being staged, the site conditions that reframe the walk, and **the re-walk that closes M20's occupant inventory** |
| 3 | [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md) | the two field jobs, start to finish. **Read before going outside** |
| 4 | [`CAPTURE-PY.md`](./CAPTURE-PY.md) | every `capture.py` option, and a complete command per job |
| 5 | [`data/README.md`](./data/README.md) | the three trace schemas, and how to read them together |
| 6 | [`/firmware/range-test/CLAUDE.md`](../../firmware/range-test/CLAUDE.md) | board gotchas and this firmware's rules |
| 7 | [`LRAN-Range-Test-Firmware-Pass1-Tasks.md`](./LRAN-Range-Test-Firmware-Pass1-Tasks.md) | what to build, and the branch plan |
| — | [`/CLAUDE.md`](../../CLAUDE.md) | repo-wide invariants — **authoritative, conflicts resolve here** |

## Where things stand

| | |
|---|---|
| Branch | `docs/handoff-post-r11` off `main`, green **2026-09-05** at f2defc4 |
| Merged today | **#19** (field data + capture.py), **#20** (R11 hold state), **#21** (blob provenance), **#22** (boards staged), **#23** (site conditions) |
| Done | **R1–R11, the M20 re-walk, and R9.** All gates passed on hardware |
| Not started | — **every Pass 1 task is complete** |
| **Next** | **D1.** Both blocking measurements are in: **M20 closed**, **W9 passed**. D1 waits only on **M21** |

```bash
pio test -d firmware/range-test -e native   # 152 passed
pio run  -d firmware/range-test -e heltec   # SUCCESS
pio test -d lib/lran-protocol -e native     # 107 passed
python3 tools/vectors/check.py              # 72 vectors OK
python tools/rangetest/test_capture.py      # capture tool, PlatformIO's python
```

`pio` is at `~/.platformio/penv/bin/pio` and is **not on `PATH`**. `capture.py` needs
PlatformIO's python: `~/.platformio/penv/bin/python`.

## Hardware state

Two Heltec V3 boards, **both flashed from `main` at 19e605f (PR #21), 2026-09-05**. They
were survey-erased before the walk; **both now hold the completed seven-site campaign in
NVS**, cursor at site 6, `HELD` at boot.

**The M20 re-walk is done and captured.** Both boards still hold that campaign in NVS —
it is committed as `data/2026-09-05-survey-campaign-r11.csv`, so they can be erased freely
whenever the next campaign needs the slots.

Note that a stray PRG press in survey mode calls `survey_save_site()` on the current slot
**before** anything else, so it overwrites that slot with whatever is in memory. That is
not hypothetical: the second board was found holding two junk runs of 3 and 4 passes
(against 71–91 for a real one), stored by the DTR-presses-PRG trap during tethered
sessions. Both were erased and neither was ever committed.

**Site cursors are at 6 `propane-tank`,** the end of the captured campaign. Erase before
starting a new one.

**Antennas are the 3.0 dBi production pair**, set as `-DLRAN_ANTENNA_GAIN_DBI10=30` in
`platformio.ini`. Changing antennas means changing that flag and reflashing — the gain
feeds the D33 clamp, not just the CSV. Conducted ceiling is **−4 dBm** at this gain.

**Neither board has a battery fitted.** The laptop→power-bank move is a power cycle. A
LiFePO4 module exists but is **untested** — ten-minute bench check described in the field
procedure.

**XIAO + Wio-SX1262 hardware is not on hand.** Pass 2 stays out of scope.

## Committed traces

**There is real range data in the repo now.** M6 has evidence; **M20 is closed.**

| File | What it is |
|---|---|
| `2026-08-31-bench.csv` | Format proof, ~1 m bench link. **Not range data** |
| `2026-09-04-walk-gatelink.csv` | **R10 walk, six positions.** 1152 probes, 2 lost downlink, 7 lost uplink, no dead test points. **Not a clear-field test** — the initiator was indoors at the bridge's target location, so every path crosses at least one wall. **Position 7 is not a location.** All caveats are in the file's own header |
| `2026-09-04-walk-gatelink-resplog.csv` | The responder's log for that walk. Closes against the sweep trace at all six positions |
| `2026-09-05-survey-campaign.csv` | **All seven sites, 910 rows, pre-R11.** Re-dumped from NVS. **Superseded for peaks** by the trace below — its `peak_dbm10` is not site-attributable. Floor and mean are sound. Site 0 `bridge-house` was measured **indoors** at the bridge's target location |
| `2026-09-05-survey-campaign-r11.csv` | **The M20 re-walk. All seven sites, 910 rows, `hold_discipline=1` at every one** — the first trace whose peaks are site-attributable, and the one the occupant inventory is built from. 68–74 passes, 130/130 bins, `dropped=0` |
| `2026-09-05-w9-bench.log` | **W9 / R9, both runs, on the bench.** The 222-byte frame and the 15-fragment set, 64 round trips, zero faults at either end. **Not a link measurement** — the path is ~1 m on purpose, so a fault would be the codec and not the RF. Not a CSV; W9 emits console lines only |

**The link closes with margin at every walked position at the D33 ceiling.** Both ends
agree within 0.8 dB, `filler_err` is zero throughout.

**Read the walk as a deployment measurement, not a propagation one.** The initiator sat
indoors at the bridge's real target location, so every reading bundles at least one framed
wall and P5/P6 cross the house — P2 and P6 are both ~40 m out and differ by **18.2 dB**
purely by which face the path leaves by. That is the right geometry for M6 and the wrong
data for a path-loss model. Arcsecond GPS (±15 m) and a height recorded only as a 2–4 ft
range compound it.

### The results to carry forward

**The occupant inventory is closed, off the R11 trace.** Read peaks from
`2026-09-05-survey-campaign-r11.csv` only; the pre-R11 campaign's peak column is not
site-attributable and one of its two headline findings did not survive.

- **`weather-island` peaks −80 dBm at 915.0** against a −115 dBm median floor, reproducing
  within 1 dB across both campaigns. **The one confirmed in-channel occupant.**
  `propane-tank` sees −106 at 915.2, also reproducing.
- **Retracted: `irrigation-pump` at 915.2.** −77 dBm pre-R11, −112 (floor) under hold
  discipline — picked up walking in. **One in-channel occupant site, not two.**
- **`gatelink-gate` peaks −66 dBm at 914.0**, 1 MHz off channel and the strongest
  near-band neighbour any node site has — at the site GateLink will live at. Only visible
  once the peaks were attributable.
- **No carrier anywhere in 902–928.** No bin at any site has a mean meaningfully above its
  own floor; every occupant is bursty, which is what §12.1 assumes when it plans to surface
  contention as `cad_backoffs`.
- **The floor is uniform and receiver-thermal-limited** — −115.0 dBm median at all seven
  sites, the indoor one included.

**The pre-R11 trace warned in its own header that the peak column was not attributable, and
it was right.** A caveat on a column is a claim to go back and test.

## First actions next session

1. `git checkout main && git pull --ff-only`, then run the checks above.
2. **Nothing is queued.** Pass 1 is complete: R1–R11 built, M20 captured and analysed,
   W9 passed on the bench.
3. **Read the W9 backoff finding before D1 picks an SF** — §12.3's default
   `backoff_max_ms` of 500 covers a full-size frame at SF7 and at nothing above it.

## The M20 re-walk — done 2026-09-05

Seven sites, two PRG presses each, ~5 minutes of dwell per site, walk not measured. It ran
as the procedure describes and the trace came off the board in one boot-time dump.
`FIELD-PROCEDURE.md` still holds if a campaign needs repeating — the only thing to add is
that the site-conditions note went in `--note` this time and is worth keeping that way.

What it closed: **M20's occupant inventory**, and with it every measurement D1 needs except
**M21**.

## Behaviour that changed on 2026-09-04 — expect the traces to look different

- **The initiator boots ARMED.** No sweep runs until the first PRG press, so
  **positions start at 1, not 0.** A trace with a position 0 predates this.
- **Tap PRG = RESPONDER, hold ~1.5 s = SURVEY.** Confirmed on hardware. The survey is
  reachable untethered, which is what makes the seven-site campaign one trip.
- **The survey's site cursor is persisted**; the role is not. A power cycle re-asks the
  role and resumes the campaign.
- **The responder saves each position as its sweep completes**, not on the next press.
- **`capture.py` drives the board** — `--reset`, `--role`, `--key`, `--key-after`,
  `--run-for`. Never run a serial console alongside it.

## Traps that cost real time here

The engineering log has the full account; this is the index.

- **Opening a serial port presses PRG.** GPIO 0 is also IO0, driven by the bridge's DTR.
  Any host tool must set `dtr = False` **before** opening — construct the port unopened.
  Presents as "the erase does not stick".
- **A tool that does not read the port while it waits loses everything the board says.**
  `capture.py` sent role keys for 3.5 s in a blind `time.sleep` loop; the tty buffer held
  ~17.9 kB of a boot-time survey dump and discarded the next 19184 bytes. Deterministic, so
  it repeated to the byte and read as a firmware bug. Fixed 2026-09-05.
- **`capture.py` will not overwrite an existing `--out`** (needs `--force`). It used to,
  truncating on the CSV header before knowing any rows were coming — that destroyed a
  committed walk trace once.
- **Completion markers match as substrings anywhere in a `#` line.** Wording a new
  firmware message carelessly truncates captures.
- **`--idle-timeout` cannot end a run against a board that never goes quiet.** A responder
  hunting for an initiator talks forever; use `--run-for` for setup commands.
- **Never ask RadioLib's `getPacketLength()` whether a packet arrived**, and never use
  `getRSSI()` without `false` for an ambient reading. Both hold stale values.
- **The responder must follow the initiator's retune**, or a clean 100% PER looks like a
  real result.
- **TCXO 1.8 V and `setDio2AsRfSwitch(true)`** fail *silently* on this board.
- **Both CP2102 bridges report `SER=0001`.** Port names swap between invocations — trust
  the confirmation from the command that did the work, not a separate check afterwards.
- **The OLED needs hand-shading in direct sunlight.** Procedure, not a defect.

## Open, and not closable from this firmware alone

- **D1** — **M20 is closed** (re-walk captured and analysed 2026-09-05). Waits only on
  **M21**, the modules' FCC grant conditions — paperwork, not bench work.
- **M6** — has data (six positions, all closing with margin) but is **not closed**:
  arcsecond GPS cannot support an RSSI-vs-distance curve. It answers "does it work
  there", not "what is the path loss".
- **W7** — the §15.1 airtime table regenerates once D1 fixes SF/BW/CR.
- **W9** — **PASSED on the bench 2026-09-05.** Both runs, 64 round trips, zero faults at
  either end, no late fragments in either direction. It left one finding for D1:
  §12.3's default `backoff_max_ms` of 500 covers a full-size frame at SF7 and at no SF
  above it (615 ms at SF8, 1107 ms at SF9). Raised, not patched.
- **D31** — copyright holder. Every file carries the `<holder>` placeholder.
