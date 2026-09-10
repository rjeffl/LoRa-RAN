# Range test — session handoff

**Written 2026-09-09, at the end of the session that ran and analysed B1b.** It replaces the
2026-09-07 file (and its 2026-09-08 addendum) wholesale.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**This directory owes no more measurements on the gate bearing.** B1b ran on 2026-09-09 and
the gate closed. What is left splits three ways, and only the first two are work:

1. **D1 — a decision, not a measurement.** Everything it needs now exists. **It does not
   start in this directory:** read Protocol Spec §18.2 and Decision Register §2.1 and §2.2
   first. B1b added one new bound to it, described under *What B1b established*.
2. **M6's well bearing.** Still unwalked, and **it is a Heltec-pair walk, not a Wio one.**
   `B1B-FIELD-CARD.md` supplies the procedure and the traps; **its board table does not
   transfer** — that table walks the Wio because B1b existed to measure GateLink's module.
   B1a specifies two Heltecs, and the well walk is the one that matches it. See *What the
   well bearing is for* below before scheduling it.
3. **Two optional runs that would each answer something real**, neither blocking anything:
   the fixed-mount A/B that would separate the Wio's transmit term from its receive term,
   and a `capture.py` guard against `--note` placeholders. Both under *Worth doing*.

`FIELD-PROCEDURE.md` comes first for any field session. The three rules that decide whether
a run is worth anything have not changed:

1. **Power the third board OFF.** Not in a backpack, not in `SURVEY` — off.
2. **Clear the responder's position log**, and see `# position log cleared` come back.
3. **Antennas connected before power**, every time.

**And one new rule, from B1b:** **reset the initiator whenever you change responders.** A
fresh responder starts a sweep with no button press. Section below; it cost a sweep on
2026-09-09.

## What B1b established — 2026-09-09

**The gate link closes on the pairing that will be deployed.** Heltec #2 (the bridge's
target unit) indoors at the bridge's target location, XIAO + Wio-SX1262 Kit at the gate
controller: **192 of 192 probes returned at all 24 configurations, 0 % PER**, at both −9 and
−4 dBm conducted, with `phy_crc_err`, `foreign` and `filler_err` all zero.

| | |
|---|---|
| **G1** | mid-driveway, 0.5 m AGL. 190/192, 1.04 % PER, **2 CRC errors** |
| **G2, the gate** | on top of the gate controller enclosure, antenna vertical, 0.8 m AGL, 47 m past G1. **192/192, 0 %** |
| Heltec A/B | two further sweeps, **same spot and mount**. Both 192/192 |

**G2 is the 2026-09-04 walk's P1, and the deployed GateLink antenna lands within 6 in of it.**
So B1b measured the link **where the node will radiate**, not near it — a better answer to
M6's question than any position on the earlier walk could give.

**Three results to carry forward, and one non-result:**

- **Margin is comfortable on the mean and thin in the SF7 tail.** Mean SNR margins at the
  gate: 17.1 dB at SF7, 20.5 at SF9, 24.7 at SF12. But **the worst single SF7 probe reached
  −5.3 dB SNR, a 2.2 dB margin, at −119.0 dBm**, against 14.8 dB for the worst SF9 probe.
  Per-test-point RSSI spread at the gate is 15 dB. **This is a new constraint on D1 and it
  pulls against W9's:** SF7 is both the SF that keeps §12.3's `backoff_max_ms` defaults valid
  and the SF whose margin at the gate occasionally approaches zero. Decision Register §2.2.
- **The Wio's module asymmetry is real, not a bench artifact.** `init_rssi − resp_rssi` was
  −2.91 dB at G1 and −3.27 dB at the gate, against −0.15 and −0.43 dB on the two
  Heltec-Heltec sweeps in the same capture. The Wio's `(TX − RX)` sits ~3.1 dB below the
  Heltec's, reproducing 2026-09-07's bench 3.37 dB over the air at two geometries 16 dB
  apart. Does not disturb D33; Register §3.3.
- **The two CRC errors are at G1, not at the gate.** G1 is 16 dB *stronger* and produced
  two, with `foreign=0`; the gate produced none. That is a bursty occupant on 915.0 MHz,
  which M20 already put an occupant on — not link margin. §14 stage 1 is now observed
  somewhere other than a desk, which is what §10.5 asked for.
- **The A/B separated the Wio's transmit term from its receive term** — a first here.
  TX ≈ 6 dB and RX ≈ 3 dB below the Heltec's, at one mount with one initiator.
  **Section below**, including what the number does and does not support.

**One thing to note for GateLink, not for this directory:** the field notes record a 24 in
tree trunk partially blocking the direct line from the gate controller to the house,
unrecorded before this run. GateLink mounts there.

## The A/B split the Wio's TX term from its RX term — read this before quoting it

**B1b's A/B is a board substitution at one position, and it is the first thing in this project
to separate the two terms.** The operator confirmed the same day that sweeps 2, 3 and 4 all
put the responder in the **same place** — resting on top of the gate controller enclosure,
antenna vertical — with the same initiator in the same room throughout, tethered and
untouched. **Only the responder board changed**, so path loss cancels.

| | sum/2 = (init+resp)/2 | diff = init − resp |
|---|---|---|
| Wio at G2, sweep 2 | −98.94 dBm | −3.27 dB |
| Heltec at G2, sweep 4 | −89.69 dBm | −0.43 dB |
| **difference** | **−9.25 dB** = the `(TX+RX)` term | **−2.84 dB** = the `(TX−RX)` term |

**TX_wio − TX_heltec ≈ −6.0 dB. RX_wio − RX_heltec ≈ −3.2 dB.**

**§7 is not overturned and must not be reported as overturned.** It ruled out **role
permutation** within a pair, which is still true: for any pair,
`P_ij − P_ji = (TX_i − RX_i) − (TX_j − RX_j)`. Substituting one **node** against a common
initiator at a fixed position is a different experiment. **Do not run more role permutations.**

**Carry the uncertainty.** Sweep 3 ran while the board was still being placed and reads
−91.79 against sweep 4's −89.69, so **placement at that mount is worth 2.10 dB** — well under
the 9.25 dB gap, but roughly ±1 dB on each split term. **The sum term rests on one pair of
sweeps**; the difference term is measured twice, at two geometries 16 dB apart, and matches
the bench. **Split approximate, difference firm.** Repeating the substitution on the same
mount is now a well-defined run — see *Worth doing*.

**The 2026-09-04 walk is not a control on this**, and the first analysis pass wrongly used it
as one. That walk read a **Heltec** at this spot at −98.25 dBm sum/2, 0.7 dB from this run's
**Wio** and 8.6 dB from its **Heltec**. The 8.6 dB is not a board difference — only two
Heltecs existed then, so both walks used the same pair, and `sum/2` is unchanged by which end
each sat at. **What was never pinned down is the initiator**, specified in that trace only as
"the office on the NW side." Indoor multipath at 915 MHz moves more than 8.6 dB over inches,
and this project has measured a desk rig wandering **24 dB at nominally identical placement**.
**Two uncontrolled numbers agreeing is not evidence.** Engineering log, 2026-09-09 (second
entry).

**Compliance is unaffected and the direction is safe.** §7.6 check 3 backed every measurement
out **below** calculated on both boards, so neither transmits above its setpoint, and a Wio
delivering ~6 dB less at the same commanded power sits further under the D33 ceiling.
**Check 2's pass on both boards is not in tension with this** — it is a step-size test, 5 dB
expected against 26 dB for a broken clamp, and is blind to a common-mode offset by design.

**Nothing about B1b's result changes.** The gate sweep was taken **with the Wio**, so the
0 % PER and every margin figure already carry the Wio's penalty. **Those are the deployed
numbers.** The corollary is new: **a Heltec at the gate would see ~9 dB more margin than
GateLink will** — an option if the SF7 tail is judged too thin, and GateLink's decision, not
this directory's.

## The responder swap — new 2026-09-09, and the trace shows it

**Reset the initiator when you change responders**, or the new board sweeps with no press.

- The responder owns `position_id` and boots it at **0** (`main.cpp:115`).
- The initiator starts a sweep whenever the position it hears differs from the one it swept
  (`main.cpp:1811`).
- Swap a responder in mid-capture and the initiator is holding the last position it swept.
  Zero is not that, so the fresh board sweeps immediately — wherever the operator is
  carrying it.

2026-09-09's capture runs `1, 2, 0, 1`: **two different locations share `position=1`**, and
there is a `position=0` the firmware is documented not to produce. Both are annotated in the
trace header.

**The previous file's rule "a trace with a position 0 predates the 2026-09-04 arming change"
is false and has been removed.** So has the same claim from the comment at `main.cpp:79`.
`--reset` on a new capture file fixes it; a firmware fix would require a press whenever a
position id moves backwards, and is not worth a reflash at the gate on its own.

## §7.6 is closed — 2026-09-07

**All four checks passed on a matched Heltec pair**, three tape-measured distances
(3.0 / 6.0 / 12.0 m) at 1.45 m AGL on grass. 72/72 test points, 0 % PER, zero error
counters, step 7 closing exactly at 192/192/192/192.

| Check | Result |
|---|---|
| 1. Power step | **PASS** — +5.0 to +5.5 dB against 5.0 expected |
| 2. **D33 clamp** | **PASS** — −4 dBm against a −4 dBm ceiling, all three positions |
| 3. Absolute EIRP | **PASS** — +1.9 to +5.8 dB, inside ±6 dB, all below calculated |
| 4. Path-loss slope | **PASS** — −15.8 to −16.3 dB/decade, **1.0–1.2 dB rms residual** |

**Check 2 was the one that mattered** — the D33 clamp reaching the PA over the air, which
nothing in this repository had ever verified. It passed on the Wio in §7's repeat too, so
**both board profiles are confirmed**. Decision Register §3.3 carries the register-side note.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) — the **2026-09-09** entry, then the four **2026-09-07** ones | what happened and why. The 09-09 entry: B1b's result, the A/B's confound, the responder swap, and the placeholder note again |
| 3 | [`data/README.md`](./data/README.md) — *"B1b: the gate closed, and the A/B did not measure what it looks like it measured"* | the numbers, the margin table, and why the A/B's 9 dB is siting |
| 4 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) **§2.1, §2.2, §3.3, §5.4** | D1's four bounds and its new SF constraint, D33's reopening, the asymmetry's field confirmation, and the ranked channel evidence |
| 5 | [`LRAN-M21-FCC-Grant-Findings`](../shared/LRAN-M21-FCC-Grant-Findings.md) + Protocol Spec **§18.2** | the regulatory frame. **Read before picking any number for D1** — `BW` and the rule section are one decision now |
| 6 | [`B1B-FIELD-CARD.md`](./B1B-FIELD-CARD.md) | **the procedure for the well bearing** — but **not its board table**, which is B1b's. Also the record of what B1b did and did not close. Its §4 and its `--note` template both carry 2026-09-09 corrections |
| 6a | [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md) | **read before any field session.** Start with *"Power down every board you are not measuring with"* and *"Erase the bench data first"*. Its commands carry **macOS port names** |
| 7 | [`data/README.md`](./data/README.md) | the two schemas, what each committed trace is *not*, the `pa_*` header fields, and **the 62.5 % band-coverage caveat** |
| 8 | [`EIRP-FIELD-CARD.md`](./EIRP-FIELD-CARD.md) + [`EIRP-SANITY-CHECK.md`](./EIRP-SANITY-CHECK.md) | **§7.6 is closed** — reference, not a job. The stands and log-clearing guidance still applies |
| 9 | [`LRAN-Range-Test-Firmware-Pass2-Tasks.md`](./LRAN-Range-Test-Firmware-Pass2-Tasks.md) and [`-Pass1-Tasks.md`](./LRAN-Range-Test-Firmware-Pass1-Tasks.md) | the two board profiles, and R1–R11 complete |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the two commands in *Git state* below |
| Spec | **`LRAN-Protocol-Specification` is v0.9**, `ver = 2`. Nothing on the wire has changed — no frame layout, no schema, no vector regenerates. **§18.2 is the authoritative Part 15 section**; §18.1 is annotated, not rewritten |
| Done | **Pass 1 R1–R11**, **Pass 2 X1–X10**, **M20**, **M21**, **§6 requirement 7**, **§7.6 incl. §7**, **B1b's gate bearing** |
| Firmware queue | **Empty.** No code is owed and no reflash is owed |

```bash
pio test -d lib/lran-protocol -e native      # host Unity suite
pio test -d firmware/range-test -e native    # host Unity suite
pio run  -d firmware/range-test -e heltec    # Heltec V3
pio run  -d firmware/range-test -e xiao      # XIAO ESP32S3 + Wio-SX1262 Kit
python3 tools/vectors/check.py
python tools/rangetest/test_capture.py       # PlatformIO's python
python3 tools/rangetest/test_survey_reintegrate.py
python3 tools/rangetest/test_eirp_check.py
python3 tools/rangetest/check_pa_table.py    # the paOptTable mirror vs. pinned RadioLib
python3 tools/checks/spec_citation_version.py # citations vs. the spec
```

**CI runs all of the above on every push and pull request** (`.github/workflows/ci.yml`).
Run them locally when you are about to spend bench time on the result; otherwise a green run
already answers it.

> **Counts are deliberately not written here or in root `CLAUDE.md`.** They were wrong more
> often than right. Run the commands.

## Which machine

**B1b ran from the Mac**, with the initiator tethered in the office. **The Kubuntu field
laptop comes out only when you need to plug into something in the field** — a board to flash
or capture from at the gate. It is a full host for that: clone verified against origin, all
PlatformIO packages installed, every check green, both firmware targets building.

| | macOS (M5) | Kubuntu field laptop |
|---|---|---|
| Heltec (CP2102) | `/dev/cu.usbserial-0001` | `/dev/ttyUSB0`, `/dev/ttyUSB1` |
| **XIAO (native USB)** | `/dev/cu.usbmodem1101` | `/dev/ttyACM0` — *not* a `ttyUSB` node, it has no bridge chip |
| Which is which | by USB location | **by plug order** — plug one at a time and note it |
| Permissions | none needed | `dialout` — confirmed present |

**Commands written in older documents carry macOS port names.** `FIELD-PROCEDURE.md`'s erase
commands and `EIRP-SANITY-CHECK.md`'s capture command both say `/dev/cu.usbserial-0001`. On
the Kubuntu laptop, substitute the `ttyUSB` node; the field card's table is the authority.

**Git auth on the field laptop is SSH**, `git@github.com:rjeffl/LoRa-RAN.git`, ed25519 key
with no passphrase. It was HTTPS, and because the repo is private and Plasma sets
`SSH_ASKPASS_REQUIRE=prefer`, every git network operation **hung silently** waiting on a GUI
dialog. If git ever hangs again with no output, that is the shape of it — and `curl` against
the same host separates transport from authentication in one command.

## Hardware state

**All three boards were reflashed 2026-09-06 from `main` at 3a9843d** — the build carrying
the PA record — and verified by reading the record back off each one. Nothing under
`firmware/`, `lib/` or `tools/` has changed since **except a comment in `main.cpp`**, so the
boards are current and no reflash is owed.

| Board | Env | State after 2026-09-09 | Notes |
|---|---|---|---|
| Heltec V3 — **Heltec dev board handheld case** | `heltec` | **Went to the gate for B1b's A/B.** Powered down after | The B1b card's **Heltec #1**. **Whether its stored survey campaign was erased before the trip is not recorded** — check before relying on it, below |
| Heltec V3 — **Meshtastic flat case** | `heltec` | **B1b's INITIATOR**, tethered in the office | **The target unit for the bridge node.** The B1b card's **Heltec #2**. Position log irrelevant — it was the initiator |
| XIAO ESP32S3 + Wio-SX1262 **Kit** | `xiao` | **B1b's walking RESPONDER.** Position log holds G1 and G2 | Log cleared before the run and dumped after; both committed. `/dev/cu.usbmodem1101` |

**Heltec #1's stored surveys: state unknown.** The field card's §4 required two erases before
it left the house, and nothing in the run records whether both were done. **The trace cannot
tell you** — Heltec #1 booted untethered as responder, so any survey dump went nowhere, and
the later resplog capture used `--role survey`'s counterpart and would not dump surveys
either. Boot it as `--role survey` and count the sites if you need to know. **It does not
matter for the data:** all seven sites at 130 bins each are committed in
`data/2026-09-05-survey-campaign-r11.csv` and `data/2026-09-05-survey-campaign.csv`. NVS on a
bench board is not a backup.

**The "seven stored sites" discriminator may therefore be gone.** **The two Heltecs are told
apart by their enclosures** — handheld case is #1, flat case is #2, the bridge's target unit.
The settings dump reads `heltec` for both.

**Port names are not identities.** Both CP2102 bridges report `SER=0001` and the nodes are
not stable across replug. **Read `board=` off the settings dump** — a wrong board selection
is silent, and writes the wrong pin map and antenna gain into a normal-looking CSV.

**The role is not persisted.** Every board re-asks at boot, so nothing above has to be undone
before a run.

> ### Power down every board you are not measuring with
>
> An idle ARMED initiator **beacons once a second** on the single fixed channel. A third
> powered board cost up to **60 % PER** on 2026-09-05 and looked exactly like poor link
> margin. Unplug spares, or park one in `SURVEY` (`--role survey`) — it listens and never
> transmits. **ARMED is not idle.** Full account in `FIELD-PROCEDURE.md`. **B1b honoured
> this**: the Wio was off for the A/B sweeps and Heltec #1 was off for the walk, so no sweep
> in that capture has a third board live in it.

## The NVS collision — and it fakes a fault

**Clear the responder's position log before every run.** This is not housekeeping.

- `PositionLog` **persists to NVS** (key `poslog`) on every PRG advance and **reloads at
  boot**. A firmware upload does not touch it.
- **`g_position_id` is not persisted** (`main.cpp:115`) — every run restarts at 0 and the
  first press makes it 1.
- `PositionLog::slot_for()` (`resp_log.cpp:61`) **matches on `position_id`** and returns the
  existing entry. Only if none matches does it open a new slot.

So last run's position 1 is **added to** this run's position 1: `probes_heard` and
`echoes_sent` accumulate, and the RSSI/SNR series mix two sessions. The cross-check against
the sweep trace then disagrees, and a disagreement there is **an instrumentation fault, not a
link result**. **The failure mode is a confident diagnosis of the wrong thing.**

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py --port /dev/cu.usbmodem1101 \
    --reset --role responder --key x --out /tmp/erase.csv --run-for 20
```

**Confirm `# position log cleared` comes back.** No line, no erase. Full procedure in
`FIELD-PROCEDURE.md`, *"Erase the bench data first"*; `--run-for`, never `--idle-timeout`.

**Do not clear an initiator that holds the survey campaign.** The campaign lives under
`surv*`/`survsite` in the same `lran-rt` namespace; nothing in the initiator path reads it
during a walk, and `z` would destroy it for nothing.

**All four of B1b's responder rows closed at exactly 192/192**, so nothing accumulated in
that run.

## What the well bearing is for, and what it is not

**It is not a Wio measurement.** The XIAO+Wio is GateLink's module, GateLink is at the gate,
and **WellLink's hardware is undecided** — its PRD is an explicit placeholder ("nothing here
is buildable yet", hardware requirements *to be developed*) and **D19**, mains versus
battery/solar, is open. Walking the well bearing with the Wio would measure a module with no
established connection to the node that will stand there, and would carry the Wio's ~9 dB
penalty into a reading whose point is the path.

**B1a already says what to walk it with: two Heltec boards.** The Heltec is also the reference
every other number in this directory is expressed in.

**What it actually gates is the bridge's antenna, and only that.** The 2026-09-05 finding is
the reason: **P2 and P6 are both ~40 m out and differ by 18.2 dB**, consistently across all 24
matched test points, because P2 leaves by the NW wall and P6's path crosses the house. The
bridge sits indoors on the NW side. **A bearing that crosses the structure starts ~18 dB down
before distance is considered**, and the well is not on the gate bearing. If it does not
close, the answer is an external or relocated bridge antenna — which is B1a's *"bridge antenna
type and position chosen and recorded"*, and is a decision best made before the antenna is
fixed rather than after.

**It does not gate D1**, whose four bounds come from M20, M21, W9 and B1b. **It does not block
bridge firmware** — B2 and B0 depend on a board in hand and the protocol library, neither of
which this touches.

**So the sequencing is:** worth one session before the bridge antenna is finalized; not worth
doing with the Wio; and not worth waiting on before firmware starts. Nothing about WellLink
itself can be settled by it until D19 and that node's hardware exist.

## D1 — a decision, against data that already exists

**Nothing external blocks it and no measurement is owed.** M20, M21, §7.6 and B1b's gate
bearing are all closed. **Read Protocol Spec §18.2 and Decision Register §2.1 and §2.2 before
picking any number** — there are four bounds now, not three.

- **Frequency — there is a ranked answer. Use 917.2–917.6 MHz.** The provisional 915.0 MHz is
  `weather-island`'s occupant peak, **and a small move off it is worse, not better**:
  915.8–916.4 carries the property-wide cluster peaking at −54 dBm. 917.2–917.6 is ~1.2 MHz
  clear of it on both scorings, floor at −116 dBm, nearest neighbour of any strength −104 dBm.
  Full table in Decision Register §5.4. Note 923.3–927.5 MHz is LoRaWAN US915 *downlink*, so
  Envelope A's uncommitted region is roughly **915.2–923.0 MHz**. **B1b ran on 915.0 and its
  two CRC errors are consistent with that occupant** — one more reason to move.
- **BW and the rule section are one decision.** BW125 forces **Envelope A** (§15.249,
  ≈−1.2 dBm EIRP, any frequency in 902–928); **Envelope B** forces BW500 and 903.0–914.2 MHz.
  **Envelope A is the plan of record** — both walks closed 0 % PER at its ceiling. If Envelope
  B is ever triggered the pick is **909.4 MHz**; half the US915 500 kHz grid is unusable here,
  914.2 included, where `gatelink-gate` sees −66 dBm. And a BW500 receiver gives up **6.0 dB**
  of floor to bandwidth before any occupant is considered.
- **SF — this is where B1b changed the picture, and it did not simplify it.** At the gate all
  three SFs closed at 0 % PER, so PER does not choose. The fade tail does: worst-probe SNR
  margin was **2.2 dB at SF7** against **14.8 dB at SF9**. Against that, W9's airtime table
  says SF7 keeps §12.3's `backoff_max_ms` defaults valid as written and SF9 needs it raised
  above 1107 ms. **The two constraints point opposite ways. Decide, and record which one you
  weighted.**
- **TX power.** The working point is **−4 dBm conducted with the fitted 3.0 dBi antenna**,
  which both walks closed 0 % PER on. **Do not derate to −9 dBm for conservatism** — it is the
  SX1262's hard floor and the 2026-09-04 walk lost 12.5–25 % at SF7 there. Record conducted
  power and antenna gain **separately**; the ceiling is EIRP and a combined figure cannot be
  audited.

**Do not close D1 from range data alone**, and **do not re-walk M20** — it is closed, and the
500 kHz re-integration was post-processing on the committed R11 trace. Regenerate the derived
file rather than editing it:

```bash
python3 tools/rangetest/survey_reintegrate.py \
    docs/rangetest/data/2026-09-05-survey-campaign-r11.csv \
    --out docs/rangetest/data/2026-09-06-m20-reintegration.csv
```

## The results to carry forward, from M20

**The occupant inventory is closed, off the R11 trace.** Read peaks from
`2026-09-05-survey-campaign-r11.csv` only.

- **A 915.8–916.4 MHz cluster at six of seven sites**, peaking at **−54 dBm at `bridge-house`
  on 916.0** — 62 dB over the floor and by a wide margin the loudest thing in the campaign.
  Property-wide. Found by the re-integration, not by the original inventory, which was built
  to ask whether one provisional channel was clear rather than to rank alternatives.
- **`weather-island` peaks −80 dBm at 915.0** against a −115 dBm median floor, reproducing
  within 1 dB across both campaigns. **The one confirmed occupant *on 915.0*** — and it is
  local to that site; every other site reads floor there. `propane-tank` sees −106 at 915.2,
  also reproducing.
- **Retracted: `irrigation-pump` at 915.2.** −77 dBm pre-R11, −112 (floor) under hold
  discipline — picked up walking in. **One in-channel occupant site, not two.**
- **`gatelink-gate` peaks −66 dBm at 914.0**, 1 MHz off channel and the strongest near-band
  neighbour any node site has — at the site GateLink will live at.
- **No carrier anywhere in 902–928.** Every occupant is bursty, which is what §12.1 assumes
  when it plans to surface contention as `cad_backoffs`.
- **The floor is uniform and receiver-thermal-limited** — −115.0 dBm median at all seven
  sites, the indoor one included.
- **The survey covers 62.5 % of the band, not all of it.** 125 kHz of receiver bandwidth every
  200 kHz. Floors and means scale soundly; **a narrowband occupant sitting in one of the
  75 kHz gaps is invisible at any level.** `data/README.md` has the arithmetic.

**The pre-R11 trace warned in its own header that the peak column was not attributable, and it
was right.** A caveat on a column is a claim to go back and test — and the same held for the
inventory's own framing, which is how the 916.0 cluster was finally seen.

## W9 passed, and left one thing for D1

Both runs passed over RF on the bench — §6.6.1's 222-byte maximum frame and §6.6.2's full
15-fragment set, 64 round trips, **zero faults at either end**. **No late fragments in either
direction across 512 frames**; §11.2's rule was chosen against a *hypothesised* RF echo and
there are none on this link. That is a **bench** negative at 1 m.

**The finding — §12.3's default backoff window is an SF7 assumption:**

| SF | 222-byte airtime (§15.1) | vs. `backoff_max_ms` 500 |
|---|---|---|
| **7** | **348 ms** | covers |
| 8 | 615 ms | **does not** |
| 9 | 1107 ms | **does not, by 2×** |

A window shorter than one frame's airtime cannot outlast the frame it backed off for. §12.3
permits transmitting after `cad_retries` regardless, so this is latency and `cad_backoffs`
rather than correctness — but `cad_backoffs` is the instrument §12.3 nominates to check
itself, and it would read high for a reason that is not congestion. **Raised, not patched, and
now in tension with B1b's SF7 fade tail.**

## Committed traces

| File | What it is |
|---|---|
| `2026-08-31-bench.csv` | Format proof, ~1 m bench link. **Not range data.** Also the trace the §7.6 tool was first exercised against — captured at **2.0 dBi** configured, where everything since carries 3.0 |
| `2026-09-04-walk-gatelink.csv` + `-resplog.csv` | **R10 walk, six positions.** 1152 probes, 2 lost downlink, 7 lost uplink, no dead test points. **Not a clear-field test** — the initiator was indoors at the bridge's target location. **Position 7 is not a location.** Caveats in the file's header. **Its P1 is the cross-check that unmasked B1b's A/B confound** |
| `2026-09-05-survey-campaign.csv` | All seven sites, 910 rows, pre-R11. **Superseded for peaks** — its `peak_dbm10` is not site-attributable. Floor and mean sound |
| `2026-09-05-survey-campaign-r11.csv` | **The M20 re-walk**, `hold_discipline=1` at every site. The trace the occupant inventory is built from. 68–74 passes, 130/130 bins, `dropped=0` |
| `2026-09-05-w9-bench.log` | **W9 / R9**, 64 round trips, zero faults either end. Console lines, not a CSV. **Not a link measurement** |
| `2026-09-05-bench-pass2-heltec.csv` | **Pass 2 regression check.** Heltec pair on pass 2 firmware, **192/192** — reproduces `2026-08-31-bench.csv` exactly. Third board parked in SURVEY, which is load-bearing |
| `2026-09-05-bench-pass2-xiao.csv` | **Pass 2, and NOT the B1b delta.** XIAO→Heltec, **192/192** — the first over-air proof the Wio's RF switch line works. Reads ~13 dB stronger RSSI, but **bench geometry is uncontrolled and dominates** |
| `2026-09-06-m20-reintegration.csv` | **DERIVED, not captured — the only file here that is not a measurement.** **Regenerate it, never hand-edit it** |
| `2026-09-07-eirp-sanity.csv` + `-resplog.csv` | **The §7.6 run that closed it.** Matched Heltec pair, three tape-measured distances. **All four checks PASS.** The first committed traces carrying the `pa_*` header fields |
| `2026-09-07-eirp-sanity-xiao.csv` + `-resplog.csv` | **§7's Wio repeat.** XIAO+Wio initiator. Checks 1, 2, 4 pass; the clamp holds on the Wio too. Check 3's WARNs are the module asymmetry, not geometry |
| `2026-09-07-eirp-sanity-swap.csv` + `-resplog.csv` | **The role swap — a negative result, kept deliberately.** It cannot separate TX from RX and neither can any such run. **Its absolute figures are not usable** (residuals 2.3–2.7 dB, check 3 failed at 12 m) |
| `2026-09-09-b1b-walk-gate.csv` | **B1b, and the trace that closes the gate bearing. Four sweeps in one file — read them in file order, not by `position`.** Sweeps 1–2 are B1b (G1, then the gate, 192/192 at 0 %); sweeps 3–4 are the Heltec A/B at the **same mount**, which splits the Wio's TX term from its RX term. **Two locations share `position=1`** and sweep 3 carries a `position=0`. Header carries the filled placeholders and every caveat |
| `2026-09-09-b1b-walk-gate-resplog-wio.csv` | The Wio's log, **sweeps 1 and 2 only**. Closes exactly at both |
| `2026-09-09-b1b-walk-gate-resplog-heltec.csv` | Heltec #1's log, **sweeps 3 and 4 only** — the A/B. **Its `position=1` is the gate, not G1** |
| `2026-09-09-b1b-field-notes.md` | **Not a trace.** The operator's field notes: decimal-degree fixes, AGL, elevation, obstructions, and why a third and fourth sweep exist. The only record of the G2 mount and the 24 in trunk. **Its same-day clarification is what makes the A/B readable** — one mount for all three gate sweeps, G2 is 2026-09-04's P1, and the deployed antenna lands within 6 in |

**Everything above the 2026-09-07 rows predates the PA record.** A trace with no `pa_*` lines
was captured by a board flashed before 2026-09-06; that is the only thing its absence means.

## Git state — ask git, do not read it here

> **Where `main` points, what merged last, which branches exist and whether a PR is open are
> deliberately not written in this file.** They were the single largest source of staleness in
> it: two of the five commits before 2026-09-09 exist *only* to correct three lines of
> hardcoded SHAs and PR numbers, and each correction needed its own branch and PR to land.
> **A written SHA is wrong the moment the branch carrying it merges, and it is wrong in the
> worst direction** — confidently, in a file whose whole value is being trustable cold.

Four commands, and they answer it better than prose can:

```bash
git fetch origin -p                       # prune deleted remote branches first
git log --oneline -1 origin/main          # where main actually is
gh pr list --state open                   # what is open, if anything
git log --branches --not --remotes --oneline   # local-only work; empty is good
```

**Run `git fetch` before trusting any of it.** Two machines push to this repository — the Mac
and the Kubuntu field laptop — so a local view can be behind without saying so.

### What a SHA in this file *does* mean

**Permanent history is citable; moving state is not.** `0f21c23` names a commit that will
always be that commit, so citing it is safe. "`main` is at `9fee445`" names where a pointer
happened to sit on one afternoon, so it is not.

So this is fine and stays:

- **Read B1b's two analysis commits in order — `0f21c23`, then `539068e`.** The second
  supersedes the first's reading of the A/B, and the first is kept as the record of how the
  wrong conclusion was reached. The engineering log's two 2026-09-09 entries mirror them.

### Two things that do not go stale, so they are written down

**A token without the `workflow` scope is refused when a commit touches
`.github/workflows/`**, with `refusing to allow an OAuth App to create or update workflow`.
The fix is `gh auth refresh -s workflow`, which is interactive. Nothing else in this
repository triggers it.

**The CI action pins are behind, and the annotation is not a failure.** GitHub forces
`actions/checkout@v4`, `actions/setup-python@v5` and `actions/cache@v4` onto Node.js 24 now
that Node 20 is deprecated on runners. **Every job passes.** Bumping the pins touches
`.github/workflows/` and so needs the scope above — worth its own change, not folded into
measurement work.

## First actions next session

1. **Ask git where you are** — the four commands in *Git state*. This file does not say, on
   purpose; `git fetch -p` first, because the field laptop pushes too.
2. Run the checks above — or read a green CI run instead of spending bench time on them.
3. **No firmware work is queued and no reflash is owed.** The one `main.cpp` change is a
   comment; the boards on the bench are current.
4. **No measurement is owed on the gate bearing.** B1b closed it.
5. **If it is D1, it does not start in this directory** — it is a decision against data that
   already exists, and B1b's SF constraint is the newest input.
6. **If it is the well bearing**, `B1B-FIELD-CARD.md` is the procedure but **not the board
   assignment** — walk it with a Heltec pair. Read its two 2026-09-09 corrections first, and
   **reset the initiator if you change responders.**
7. **Do not re-run the §7.6 role permutations.** B1b's A/B is a module figure and can be
   cited as one, with its ±1 dB and its one-pair-of-sweeps caveat attached.

## Worth doing, and nothing blocks on either

- **Repeat the A/B substitution, to tighten a number that already exists.** 2026-09-09 got
  TX ≈ −6.0 dB and RX ≈ −3.2 dB off **one** pair of sweeps at G2's mount. The repeat is cheap
  and well defined: same mount, same initiator untouched, **alternate Wio–Heltec–Wio** so drift
  shows in the data rather than being argued about, and photograph the mount. Three sweeps
  turn ±1 dB into something defensible and would settle the 0.7 dB coincidence with the
  2026-09-04 reading. **Not blocking anything** — the deployed margin figures already carry
  the Wio's penalty.
- **A `capture.py` guard against `--note` placeholders.** Refuse, or warn loudly, on a note
  containing `<...>`. Two runs in three have gone out with placeholders unedited — §7.6's
  `<H>m AGL on <stands>` and B1b's two position descriptions — and B1b also left a stale
  clause standing that the run falsified. Bold text under the command has now failed twice,
  which makes it a tooling question rather than a discipline one. Cheap, and it protects the
  geometry-dependent checks specifically.

## Behaviour that changed, and will make traces look different

**From 2026-09-09 — no code changed, but read this before parsing a multi-responder capture:**

- **The `position` column does not identify a sweep once more than one responder is used.**
  Split on file order. Section above.

**From 2026-09-08:**

- **The boot banner reads `v0.9`.** **Captures committed before 09-08 say `v0.8` and are
  correct** — they were produced by a firmware built against that citation. Do not re-stamp
  them. `ver` stays at `2` and v0.9 changed nothing on the wire, so old and new traces stay
  comparable; the string separates *when a build was made*, not *what it measured*.
- **CI exists**, and **`tools/checks/spec_citation_version.py`** runs in it.

**From 2026-09-06:**

- **Five `pa_*` `key=value` lines in the boot output**, folded into the trace header by
  `capture.py`. A trace without them was captured by a board flashed before 2026-09-06.
- **The conducted ceiling is −4 dBm** at the configured 3.0 dBi. **The code's arithmetic did
  not change** — it has always derived the ceiling from the configured gain — but a trace's
  ceiling is a function of the gain it was captured with, and `2026-08-31-bench.csv` was
  captured at 2.0.

**From Pass 2 (2026-09-05):**

- **Two build environments**, `heltec` and `xiao`, selected by `-DLRAN_BOARD_*`. One selection
  point, `kBoard`/`kBoardUi` in `board_config.h`.
- **The XIAO's display is flipped 180°** for the enclosure, and its role button is **GPIO 21**
  on the Wio, not GPIO 0.
- **`[env:xiao]` carries `-Wno-error=cpp`**, the only relaxation of `-Werror` in the repo.
  Reasoning is at the flag.

**From 2026-09-04:**

- **The initiator boots ARMED.** No sweep runs until the first PRG press — **for the first
  responder of a capture.** Not for a second one; section above.
- **Tap PRG = RESPONDER, hold ~1.5 s = SURVEY.** The survey is reachable untethered.
- **The survey's site cursor is persisted**; the role is not.
- **The responder saves each position as its sweep completes**, not on the next press.
- **`capture.py` drives the board** — `--reset`, `--role`, `--key`, `--key-after`,
  `--run-for`. Never run a serial console alongside it.

## Traps that cost real time here

The engineering log has the full account; this is the index.

- **A responder swapped in mid-capture sweeps with no press**, at position 0, wherever it is
  being carried. Reset the initiator. **New 2026-09-09**, and it cost a sweep.
- **Two uncontrolled numbers agreeing is not evidence.** B1b's A/B was a same-hour
  substitution at one mount, and it was talked out of its own result by a five-day-old reading
  whose indoor end was specified only as "the office on the NW side" — where multipath moves
  more than the 8.6 dB in question. **Weight the tighter experiment, and check what the looser
  one actually pinned down before using it as a control.** **New 2026-09-09**; it cost a wrong
  conclusion that stood for one commit.
- **Write down the mount, not just the position.** "0.8 m AGL on the back of a concrete
  column" and "on top of the gate controller enclosure" describe one spot and read as two. The
  A/B was nearly discarded over the wording. **New 2026-09-09.**
- **A stale responder position log merges into the new run** and fakes an instrumentation
  fault. Clear it, and confirm the line. Section above.
- **A `--note` template placeholder is a silent defect**, and so is a note clause the run then
  falsifies. Twice now: §7.6's `<H>m AGL on <stands>`, and B1b's position descriptions plus
  "Third Heltec POWERED DOWN" on a run that carried it to the gate. `capture.py` cannot tell a
  placeholder from a value and nothing downstream checks.
- **A third powered board corrupts a two-board measurement**, silently, by up to 60 % PER, and
  the symptom is indistinguishable from poor link margin. It was mistaken for a code
  regression across two full sweeps on 2026-09-05. **A ten-minute bisect against the previous
  firmware settles this class of question — reach for it before asserting a regression.**
- **A reflash session is a period during which every board transmits.** A flashed board boots
  ARMED, and esptool's hard reset *is* a boot. Park or unplug each board as it finishes.
- **Bench geometry moves RSSI further than anything you are trying to measure.** The Heltec
  reference moved **−24 → −42 dBm between two runs on placement alone**, and 2026-09-07's desk
  throwaway wandered 24 dB between two sweeps at *identical* placement. That is why §7.6 wants
  a tape measure, three distances and a slope rather than one number.
- **Opening a serial port presses PRG — on the Heltec.** GPIO 0 is also IO0, driven by the
  CP2102's DTR. Host tools must set `dtr = False` **before** opening. The XIAO has no bridge
  chip and its button is GPIO 21, so this trap is Heltec-only.
- **Flashing the XIAO from a firmware with a different USB stack fails once.** Bootloader entry
  swaps the USB device, esptool loses its handle, `Could not configure port`. The board *is* in
  the bootloader — on a **new** device node. Flash to that.
- **A tool that does not read the port while it waits loses everything the board says.** Cost
  19184 bytes of a survey dump, deterministically, and read as a firmware bug.
- **`capture.py` will not overwrite an existing `--out`** (needs `--force`).
- **Completion markers match as substrings anywhere in a `#` line.** Careless wording of a new
  firmware message truncates captures.
- **A boot line that is not `key=value` never reaches the trace.** `capture.py` collects
  `^[a-z][a-z0-9_]*=\S*$` into the header and counts everything else as `unparsed`. One space
  in one value and that line is silently absent.
- **`--idle-timeout` cannot end a run against a board that never goes quiet.** Use `--run-for`
  for setup commands.
- **Never ask RadioLib's `getPacketLength()` whether a packet arrived**, and never use
  `getRSSI()` without `false` for an ambient reading. Both hold stale values.
- **The responder must follow the initiator's retune**, or a clean 100 % PER looks real.
- **TCXO 1.8 V, `setDio2AsRfSwitch(true)`, and — on the Wio — a real `rf_sw` pin** all fail
  *silently*. The radio initialises, reports a successful transmit, and puts nothing on the
  air. `begin()` returning success proves none of them; only frames crossing does.
- **Both CP2102 bridges report `SER=0001`.** Trust the confirmation from the command that did
  the work, not a separate check afterwards.
- **A git hang is not a network failure.** Private repo plus Plasma's
  `SSH_ASKPASS_REQUIRE=prefer` sends the credential prompt to a GUI dialog and blocks forever;
  `GIT_TERMINAL_PROMPT=0` does not help, because it disables the *terminal* prompt and not
  askpass. `curl` against the same host separates transport from authentication.
- **Never transmit without an antenna connected.** It is also one of the failures the §7.6
  check exists to detect — detect it in the trace, not by damaging a PA.
- **The OLED needs hand-shading in direct sunlight.** Procedure, not a defect.
- **SF12 reads ~7 dB lower SNR than SF7 at the same RSSI**, on every trace back to
  2026-08-31. It is a property of the estimator, not of the link, and it means SF12 margin
  figures are understated. Do not chase it as a fault.

## Open, and not closable from this firmware alone

- **D1** — nothing external blocks it and no measurement is owed. It needs a decision made
  against the data above. **B1b's SF7 fade tail is the newest input and it pulls against W9's
  backoff finding.**
- **M6** — **the gate bearing is answered by B1b.** Still **not closed**: it asks for **both
  bearings** and the well bearing is unwalked. Arcsecond GPS at the house end still cannot
  support an RSSI-vs-distance curve, so it answers "does it work there", not "what is the path
  loss".
- **The Wio's TX/RX split** — **measured for the first time by B1b's A/B**: TX ≈ −6.0 dB and
  RX ≈ −3.2 dB against the Heltec, with ~±1 dB on each and the sum term resting on one pair of
  sweeps. **Not closed, because it wants one repeat** on the same mount; see *Worth doing*.
  §7's ruling stands for **role permutations** specifically: do not run another one.
- **D33** — **reopened 2026-09-06 by M21**, exactly as its own standing condition 1
  anticipated. The ceiling survives; the reasoning changed and the frame is §15.23 home-built.
  Decision Register §3.3, which now also carries B1b's field confirmation of the asymmetry.
- **W7** — the §15.1 airtime table regenerates once D1 fixes SF/BW/CR.
- **§2.3.1 sub-question (b)** — the sleep-current cost of holding `RF_SW` high. Untouched, and
  **B1b did not touch it either**. A meter on the carrier, not a walk.
- **`gatelink-expansion-board.md` §10 ring-out** — the header board's pads against the
  carrier's nets. **The Kit cannot close it.** It is the tracked check for Bridge Impl Plan
  §10.8.1's remaining premise.
- **A `capture.py` placeholder guard** — see *Worth doing*. Not blocking; two runs in three
  argue for it.

### Closed, and not to be reopened by habit

- **B1b's gate bearing** — **CLOSED 2026-09-09.** 192/192 at 0 % PER at the gate on the
  deployed pairing, at a spot within 6 in of where the node's antenna will sit. **The A/B in
  the same capture also split the Wio's TX term from its RX term** — a first, and it wants one
  repeat rather than a rerun of §7's permutations.
- **M20** — **CLOSED 2026-09-06.** Field work plus re-integration. Results in Decision Register
  §5.4; derived file `2026-09-06-m20-reintegration.csv`. **Do not re-walk it.**
- **M21** — **CLOSED 2026-09-06.** Both grants recorded in `LRAN-M21-FCC-Grant-Findings`.
- **W9** — **CLOSED in spec v0.8.** Left the SF7 backoff finding above for D1.
- **§7.6 EIRP sanity check** — **CLOSED 2026-09-07**, all four checks, plus §7's Wio repeat.
- **D31** — copyright holder. **Closed 2026-09-08: Robert J. Lee.**
- **Handoff §6 requirement 7** — **DONE 2026-09-06**, confirmed in a real trace 2026-09-07.

### Backlog items that are not range-test work

- **M22** — bridge LoRa PER with WiFi idle vs. saturated. Belongs to the bridge.
- **M23** — BLE RSSI to the BMS from the Stamp-S3A at its final mounting position, and
  LoRa-to-BLE isolation in the same session. Supersedes **M5**. Belongs to GateLink.
- **The 24 in trunk at the gate** — recorded by B1b's field notes, partially blocking the
  direct line from the gate controller to the house. **GateLink's siting question, not a
  range-test one.** No node document carries it yet.
