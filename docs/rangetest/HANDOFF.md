# Range test — session handoff

**Written 2026-09-05, after the R10 fieldwork.**

> **This file goes stale.** It records *session state and next actions*, nothing else.
> Where it disagrees with the documents below, they win — check the engineering log's
> last entry against the date above before trusting anything here.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) — **the last two entries** | what happened and why. The 2026-09-05 entry is the one that matters: the field data, and the tool defect that ate a third of it |
| 3 | [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md) | the two field jobs, start to finish. **Read before going outside** |
| 4 | [`CAPTURE-PY.md`](./CAPTURE-PY.md) | every `capture.py` option, and a complete command per job |
| 5 | [`data/README.md`](./data/README.md) | the three trace schemas, and how to read them together |
| 6 | [`/firmware/range-test/CLAUDE.md`](../../firmware/range-test/CLAUDE.md) | board gotchas and this firmware's rules |
| 7 | [`LRAN-Range-Test-Firmware-Pass1-Tasks.md`](./LRAN-Range-Test-Firmware-Pass1-Tasks.md) | what to build, and the branch plan |
| — | [`/CLAUDE.md`](../../CLAUDE.md) | repo-wide invariants — **authoritative, conflicts resolve here** |

## Where things stand

| | |
|---|---|
| Branch | `range/handoff`, verified green **2026-09-05** |
| Merged | #17 (field prep + R8 forward), #16, #14, #13, #12, #11 |
| Done | **R1–R8, and R10 fieldwork.** All gates passed on hardware |
| Not started | **R9** (`range/w9`), and **R11** (survey hold state) |
| **Next** | **R11 — the survey hold state.** See the bottom of this file |

```bash
pio test -d firmware/range-test -e native   # 133 passed
pio run  -d firmware/range-test -e heltec   # SUCCESS
pio test -d lib/lran-protocol -e native     # 107 passed
python3 tools/vectors/check.py              # 72 vectors OK
```

`pio` is at `~/.platformio/penv/bin/pio` and is **not on `PATH`**. `capture.py` needs
PlatformIO's python: `~/.platformio/penv/bin/python`.

## Hardware state

Two Heltec V3 boards, both flashed from `main` as of 2026-09-04.

**The survey board's NVS holds the seven-site campaign** and its site cursor is at 6.
That data is committed now, so it is no longer irreplaceable — but do not erase it
casually, and note that a stray PRG press in survey mode calls `survey_save_site()` on
the current slot **before** anything else, overwriting a stored site with whatever is in
memory.

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
| `2026-09-04-walk-gatelink.csv` | **R10 walk, six positions.** 1152 probes, 2 lost downlink, 7 lost uplink, no dead test points. **Position 7 is not a location** — annotated in the file's own header |
| `2026-09-04-walk-gatelink-resplog.csv` | The responder's log for that walk. Closes against the sweep trace at all six positions |
| `2026-09-05-survey-campaign.csv` | **All seven sites, 910 rows.** Re-dumped from NVS; `peak_dbm10` is caveated, see below |

**The link closes with margin at every walked position at the D33 ceiling.** Both ends
agree within 0.8 dB, `filler_err` is zero throughout.

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

1. `git checkout main && git pull --ff-only`, then run the four checks above.
2. Start **R11**, the survey hold state (below). It is the smallest change that unblocks
   the most, and one of the two things D1 waits on.
3. Local branches `range/sweep`, `range/survey`, `range/capture-walk`, `range/field-prep`,
   `range/field-prep2`, `range/handoff` and `docs/v0_6-sync` are merged into `main` and can
   be deleted.

## R11 — the survey hold state, and why it is next

`survey_store_and_advance()` calls `g_survey.reset()` and resumes scanning immediately, so
**the walk between sites is measured**. Peak-hold never forgets, so a burst heard in transit
is attributed permanently to the destination site.

**No press pattern avoids this** — pressing on arrival rather than departure only moves the
contamination to the site you just left. It needs a firmware state: the new site stays
paused until a second PRG press starts the dwell.

Floor and mean are unaffected and the channel result above is sound. What is not evidential
is the **occupant inventory**, and that is what M20 owes D1.

Worth doing in the same branch:

- The per-site preamble should say when a run includes a transit segment.
- `capture.py` needs a regression test for the drain-while-waiting path. That bug destroyed
  a third of a campaign and was invisible to every test in the repo.

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
