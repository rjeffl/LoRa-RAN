# Range test — session handoff

**Written 2026-09-05, after R11 merged and the boards were staged.**

> **This file goes stale.** It records *session state and next actions*, nothing else.
> Where it disagrees with the documents below, they win — check the engineering log's
> last entry against the date above before trusting anything here.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) — **the 2026-09-05 entries** | what happened and why. Six entries that day: the field data, the tool defect that ate a third of the survey, R11, its own provenance bug, the boards being staged, and the site conditions that reframe the walk |
| 3 | [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md) | the two field jobs, start to finish. **Read before going outside** |
| 4 | [`CAPTURE-PY.md`](./CAPTURE-PY.md) | every `capture.py` option, and a complete command per job |
| 5 | [`data/README.md`](./data/README.md) | the three trace schemas, and how to read them together |
| 6 | [`/firmware/range-test/CLAUDE.md`](../../firmware/range-test/CLAUDE.md) | board gotchas and this firmware's rules |
| 7 | [`LRAN-Range-Test-Firmware-Pass1-Tasks.md`](./LRAN-Range-Test-Firmware-Pass1-Tasks.md) | what to build, and the branch plan |
| — | [`/CLAUDE.md`](../../CLAUDE.md) | repo-wide invariants — **authoritative, conflicts resolve here** |

## Where things stand

| | |
|---|---|
| Branch | `main`, verified green **2026-09-05** at f2defc4 |
| Merged today | **#19** (field data + capture.py), **#20** (R11 hold state), **#21** (blob provenance), **#22** (boards staged), **#23** (site conditions) |
| Done | **R1–R8, R10 fieldwork, R11.** All gates passed on hardware |
| Not started | **R9** (`range/w9`) — the only remaining code task |
| **Next** | **The M20 re-walk. No code needed.** Boards are erased and staged |

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

Two Heltec V3 boards, **both flashed from `main` at 19e605f (PR #21), 2026-09-05**, and
**both survey-erased**. Verified on hardware: empty NVS, cursor at site 0, `HELD` at boot.

**Ready for the M20 re-walk.** Nothing to prepare.

Note that a stray PRG press in survey mode calls `survey_save_site()` on the current slot
**before** anything else, so it overwrites that slot with whatever is in memory. That is
not hypothetical: the second board was found holding two junk runs of 3 and 4 passes
(against 71–91 for a real one), stored by the DTR-presses-PRG trap during tethered
sessions. Both were erased and neither was ever committed.

**Site cursors are at 0, so the first PRG press starts the dwell at `bridge-house`.**

**Antennas are the 3.0 dBi production pair**, set as `-DLRAN_ANTENNA_GAIN_DBI10=30` in
`platformio.ini`. Changing antennas means changing that flag and reflashing — the gain
feeds the D33 clamp, not just the CSV. Conducted ceiling is **−4 dBm** at this gain.

**Neither board has a battery fitted.** The laptop→power-bank move is a power cycle. A
LiFePO4 module exists but is **untested** — ten-minute bench check described in the field
procedure.

**XIAO + Wio-SX1262 hardware is not on hand.** Pass 2 stays out of scope.

## Committed traces

**There is real range data in the repo now.** M6 has evidence; M20's campaign is captured.

| File | What it is |
|---|---|
| `2026-08-31-bench.csv` | Format proof, ~1 m bench link. **Not range data** |
| `2026-09-04-walk-gatelink.csv` | **R10 walk, six positions.** 1152 probes, 2 lost downlink, 7 lost uplink, no dead test points. **Not a clear-field test** — the initiator was indoors at the bridge's target location, so every path crosses at least one wall. **Position 7 is not a location.** All caveats are in the file's own header |
| `2026-09-04-walk-gatelink-resplog.csv` | The responder's log for that walk. Closes against the sweep trace at all six positions |
| `2026-09-05-survey-campaign.csv` | **All seven sites, 910 rows.** Re-dumped from NVS; `peak_dbm10` is caveated, see below. Site 0 `bridge-house` was measured **indoors** at the bridge's target location |

**The link closes with margin at every walked position at the D33 ceiling.** Both ends
agree within 0.8 dB, `filler_err` is zero throughout.

**Read the walk as a deployment measurement, not a propagation one.** The initiator sat
indoors at the bridge's real target location, so every reading bundles at least one framed
wall and P5/P6 cross the house — P2 and P6 are both ~40 m out and differ by **18.2 dB**
purely by which face the path leaves by. That is the right geometry for M6 and the wrong
data for a path-loss model. Arcsecond GPS (±15 m) and a height recorded only as a 2–4 ft
range compound it.

### The one result to carry forward

**915.0 MHz is not clean everywhere.** `weather-island` peaks at **−81 dBm at 915.0** and
`irrigation-pump` at **−77 dBm at 915.2**, against a −116 to −118 dBm floor uniform across
the property. The other five sites see nothing more than 10 dB over floor.

The mean in those bins sits *at* the floor, so they are rare bursts, not carriers — a
collision risk at two sites rather than a blocked channel, and exactly what §12.1 expects
to surface later as `cad_backoffs`.

**Both of those sites were missing from the first capture**, and the first analysis
concluded from the survivors that the channel was clean everywhere. Remember that the next
time a trace comes up short.

## First actions next session

1. `git checkout main && git pull --ff-only`, then run the checks above.
2. **Walk the M20 campaign.** Both boards are already flashed and erased; nothing to
   prepare. Procedure below.
3. All feature branches through #23 are merged and deleted. `main` is the only local
   branch.

## The M20 re-walk — what is actually next

Seven sites, roughly a 40 minute loop, **two PRG presses per site**:

1. Arrive. **Press PRG** — the inverted `HELD` bar clears and the pass count climbs.
2. Stand still ~5 minutes.
3. **Press PRG again** — stores, advances, returns to `HELD`.
4. Walk to the next site. The walk is **not** measured, which is the whole point of R11.

Cursors are at 0, so the first press starts the dwell at `bridge-house`.

**Put the site conditions in `--note` this time.** Indoor/outdoor and wall penetrations at
each end, and height per site. The 2026-09-04 walk omitted that the initiator was indoors,
and it turned out to be worth 18.2 dB between two positions at the same range. Site 0 is
indoors at the bridge's target location by design — say so in the note.

Back at the house, reset into `SURV` with a capture running: the board dumps all seven
sites at boot in one file. **Every site should read `# hold_discipline=1`** — the first
trace that will. Commit it as soon as it comes off the board.

That closes **M20's occupant inventory**, and **D1 then waits only on M21**.

### What the last campaign already told us, to compare against

- The noise floor is uniform across the property at **−116 to −118 dBm** and is
  **receiver-thermal-limited** — even the indoor site matches within 2 dB.
- **915.0 MHz is not clean everywhere.** `weather-island` peaked at −81 dBm at 915.0 and
  `irrigation-pump` at −77 dBm at 915.2; the other five saw nothing over 10 dB above
  floor. Rare bursts, not carriers — a collision risk at two sites.
- Those two sites were the ones lost in the truncated first capture, and the first
  analysis concluded from the survivors that the channel was clean everywhere. **Worth
  remembering the next time a trace comes up short.**

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

- **D1** — needs **M20** (campaign captured 2026-09-05; its occupant inventory waits on
  R11's hold state) and **M21** (the modules' FCC grant conditions, paperwork not bench
  work).
- **M6** — has data (six positions, all closing with margin) but is **not closed**:
  arcsecond GPS cannot support an RSSI-vs-distance curve. It answers "does it work
  there", not "what is the path loss".
- **W7** — the §15.1 airtime table regenerates once D1 fixes SF/BW/CR.
- **W9** — R9's 222-byte and fragmented `PING` runs. **R9 is the first work here that
  links `/lib/lran-protocol/`.**
- **D31** — copyright holder. Every file carries the `<holder>` placeholder.
