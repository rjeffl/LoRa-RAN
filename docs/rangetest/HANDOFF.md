# Range test — session handoff

**Written 2026-09-07, at the end of the session that ran and closed the §7.6 EIRP sanity
check.** It replaces the 2026-09-06 file wholesale.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**B1b — the gate-bearing walk with the Wio.** §7.6 closed on 2026-09-07 and **M6's
precondition is met**, so nothing in this directory blocks the 500 ft leg any more. The
2026-09-05 desk runs are explicitly not B1b.

`FIELD-PROCEDURE.md` comes first for any field session. The three rules that decide whether
a run is worth anything have not changed:

1. **Power the third board OFF.** Not in a backpack, not in `SURVEY` — off.
2. **Clear the responder's position log**, and see `# position log cleared` come back.
   A stale log fakes an instrumentation fault; section below.
3. **Antennas connected before power**, every time.

**D1** is a decision, not a measurement, and does not start in this directory. Read Protocol
Spec §18.2 and Decision Register §2.1 before picking any number.

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
nothing in this repository had ever verified. It also passed on the Wio in §7's repeat, so
**both board profiles are confirmed**. Decision Register §3.3 carries the register-side note.

**§7's Wio repeat found a module asymmetry**: the Wio-SX1262's `(TX − RX)` sits **3.37 dB
below** the Heltec's, reproduced across two runs. **Reciprocal RSSI cannot say whether that
is a weak PA or an optimistic RSSI**, and no further permutation will — for any pair,
`P_ij − P_ji = (TX_i − RX_i) − (TX_j − RX_j)`. Separation needs an absolute reference this
project does not have. **Do not run a third permutation expecting a different answer.**
Neither reading disturbs the ceiling; `data/README.md` has the arithmetic.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md) | **read before any field session — B1b is next.** Start with *"Power down every board you are not measuring with"* and *"Erase the bench data first"* |
| 2b | [`EIRP-FIELD-CARD.md`](./EIRP-FIELD-CARD.md) + [`EIRP-SANITY-CHECK.md`](./EIRP-SANITY-CHECK.md) | **§7.6 is closed** — these are now reference, not the next job. The card's stands and log-clearing guidance still applies to any run |
| 3 | [`engineering-log.md`](./engineering-log.md) — the **four 2026-09-07** entries, then the five **2026-09-06** ones | what happened and why. The 09-07 set: the field laptop and the NVS collision; the §7.6 run; the Wio repeat; and the role swap that could not do what it was run for |
| 4 | [`data/README.md`](./data/README.md) — *"The three 2026-09-07 EIRP traces are one measurement"* | why the matched pair is what makes the other two readable |
| 5 | [`LRAN-M21-FCC-Grant-Findings`](../shared/LRAN-M21-FCC-Grant-Findings.md) + Protocol Spec **§18.2** | the regulatory frame. **Read before picking any number for D1** — `BW` and the rule section are one decision now |
| 6 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) **§2.1, §3.3, §5.4** | D1's four bounds, D33's reopening, and the ranked channel evidence |
| 7 | [`data/README.md`](./data/README.md) | the two schemas, what each committed trace is *not*, the `pa_*` header fields, and **the 62.5 % band-coverage caveat** |
| 8 | [`LRAN-Range-Test-Firmware-Pass2-Tasks.md`](./LRAN-Range-Test-Firmware-Pass2-Tasks.md) | the second board profile — what it validates and, more importantly, what it does not |
| 9 | [`LRAN-Range-Test-Firmware-Pass1-Tasks.md`](./LRAN-Range-Test-Firmware-Pass1-Tasks.md) | R1–R11, complete. Its Pass 2 section is closed against what it predicted |

## Where things stand

| | |
|---|---|
| Branch | see **Git state** below — `main` is **not** the only branch any more |
| Merged through | **#36**. Everything since is on `docs/eirp-field-laptop`, **PR not yet open** |
| Spec | **`LRAN-Protocol-Specification` is v0.8**, `ver = 2`. Nothing on the wire has changed — no frame layout, no schema, no vector regenerates. **§18.2 is the authoritative Part 15 section**; §18.1 is annotated, not rewritten |
| Done | **Pass 1 R1–R11**, **Pass 2 X1–X10**, **M20**, **M21**, **§6 requirement 7**, **§7.6 incl. §7** |
| Firmware queue | **Empty.** This directory owes **one** measurement — B1b — and no code |

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
```

**All nine green on the field laptop 2026-09-07**, including both firmware builds. `pio` is
at `~/.platformio/penv/bin/pio` and is **not on `PATH`**; `capture.py` needs
`~/.platformio/penv/bin/python`.

> **Counts are deliberately not written here or in root `CLAUDE.md`.** They were wrong more
> often than right. Run the commands.

## The field laptop

**Work has moved to the Kubuntu field laptop.** It is now a full host — clone verified
against origin, all PlatformIO packages installed, every check green, **both firmware
targets build**, so a reflash in the field is possible if it comes to that.

| | macOS (M5) | **Kubuntu field laptop** |
|---|---|---|
| Heltec (CP2102) | `/dev/cu.usbserial-0001` | **`/dev/ttyUSB0`, `/dev/ttyUSB1`** |
| **XIAO (native USB)** | `/dev/cu.usbmodem1101` | **`/dev/ttyACM0`** — *not* a `ttyUSB` node, it has no bridge chip |
| Which is which | by USB location | **by plug order** — plug one at a time and note it |
| Permissions | none needed | `dialout` — **confirmed present** |

**Commands written in older documents carry macOS port names.** `FIELD-PROCEDURE.md`'s
erase commands and `EIRP-SANITY-CHECK.md`'s capture command both say
`/dev/cu.usbserial-0001`. Substitute the `ttyUSB` node. The field card's table is the
authority.

**Git auth is SSH now**, `git@github.com:rjeffl/LoRa-RAN.git`, ed25519 key with no
passphrase. It was HTTPS, and because the repo is private and Plasma sets
`SSH_ASKPASS_REQUIRE=prefer`, every git network operation **hung silently** waiting on a GUI
dialog. If git ever hangs again with no output, that is the shape of it — and `curl` against
the same host separates transport from authentication in one command.

## Hardware state

**All three boards were reflashed 2026-09-06 from `main` at 3a9843d** — the build carrying
the PA record — and **verified by reading the record back off each one**. Nothing under
`firmware/`, `lib/` or `tools/` has changed since, so **the boards are current**.

| Board | Env | State | Notes |
|---|---|---|---|
| Heltec V3 | `heltec` | **POWERED DOWN** | **Holds the stored survey campaign** — all seven sites. Not used on 2026-09-07 and kept off throughout, which is why the EIRP runs are clean |
| Heltec V3 | `heltec` | `SURVEY` | No stored sites. **Position log cleared 2026-09-07** after its logs were captured. Was both responder and initiator across the three EIRP runs |
| XIAO ESP32S3 + Wio-SX1262 **Kit** | `xiao` | `SURVEY` | **Position log cleared 2026-09-07** after capture. Was initiator then responder. `/dev/ttyACM0`, not `ttyUSB` |

**Both logs were dumped and committed before clearing.** Nothing was lost — the six
2026-09-07 traces are the record, and NVS on a bench board is not a backup.

**Port names are not identities.** Both CP2102 bridges report `SER=0001` and the nodes are
not stable across replug. **Read `board=` off the settings dump** — a wrong board selection
is silent, and writes the wrong pin map and antenna gain into a normal-looking CSV.

**The role is not persisted.** Every board re-asks at boot, so nothing above has to be
undone before a run.

**The stored campaign is committed** as `data/2026-09-05-survey-campaign-r11.csv`, and
**that is the copy that matters**: NVS on one bench board is not a backup.

> ### Power down every board you are not measuring with
>
> An idle ARMED initiator **beacons once a second** on the single fixed channel. A third
> powered board cost up to **60 % PER** on 2026-09-05 and looked exactly like poor link
> margin. Unplug spares, or park one in `SURVEY` (`--role survey`) — it listens and never
> transmits. **ARMED is not idle.** Full account in `FIELD-PROCEDURE.md`.

## The NVS collision — new 2026-09-07, and it fakes a fault

**Clear the responder's position log before every run.** This is not housekeeping.

- `PositionLog` **persists to NVS** (key `poslog`) on every PRG advance and **reloads at
  boot**. A firmware upload does not touch it.
- **`g_position_id` is not persisted** (`main.cpp:115`) — every run restarts at position 1.
- `PositionLog::slot_for()` (`resp_log.cpp:61`) **matches on `position_id`** and returns the
  existing entry. Only if none matches does it open a new slot.

So last run's position 1 is **added to** this run's position 1: `probes_heard` and
`echoes_sent` accumulate, the RSSI/SNR series mix two sessions. §7.6 step 7's cross-check
then disagrees, and that document calls disagreement *"an instrumentation fault, not a link
result."* **The failure mode is a confident diagnosis of the wrong thing.**

```bash
~/.platformio/penv/bin/python tools/rangetest/capture.py --port /dev/ttyUSB0 \
    --reset --role responder --key x --out /tmp/erase.csv --run-for 20
```

**Confirm `# position log cleared` comes back.** No line, no erase. Full procedure in
`FIELD-PROCEDURE.md`, *"Erase the bench data first"*; `--run-for`, never `--idle-timeout`.

**Do not clear the initiator.** The survey campaign lives under `surv*`/`survsite` in the
same `lran-rt` namespace; nothing in the initiator path reads it during an EIRP run, and
`z` would destroy the seven-site campaign for nothing.

## What 2026-09-07 established

**Three sweeps and three responder logs**, all committed, plus one throwaway that was
deliberately **not** committed — a fixed-desk rig check run before the real thing, which
proved the laptop drives the board and is not a measurement.

Firsts, all confirmed on hardware:

- **`capture.py` folds all five `pa_*` lines into the trace header.** Previously asserted
  only by tests on both sides. **The three 2026-09-07 traces are the first committed ones
  carrying them.**
- **The boot record reads `kPaOptTable[0]`** — `pa_duty_cycle=2 pa_hp_max=2 pa_val=-5`, the
  −9 dBm point, confirming 2026-09-06's correction rather than the claim it replaced.
- **The D33 clamp works over the air, on both board profiles.**
- **Sentinels survive board to tool** — a lost point emitted `65535` / `−32768` and
  `eirp_check.py` excluded it from the means rather than averaging it in.
- **Step 7's cross-check closes exactly** in all three runs, 192/192/192/192.

**Two process lessons, worth more than the numbers:**

- **A `--note` template placeholder is a silent defect.** The §7.6 capture ran with
  `<H>m AGL on <stands>` unedited, and checks 3 and 4 are exactly the geometry-dependent
  ones. `capture.py` cannot know `<H>` is not a value and nothing downstream checks. The
  geometry was filled in the same day and **the note says it was**.
- **Step 7 had no runnable command** in either short document — caught by the operator
  reading ahead, not by any check. Both now carry the invocation, and the mechanism they
  described was wrong: no `d` is needed, the responder dumps at boot from NVS
  (`main.cpp:1073`).

## What Pass 2 did, and what it did not

**Did.** Added the XIAO ESP32S3 + Wio-SX1262 **Kit** as a second board profile behind R2's
injected-config seam. Display pins, panel reset, Vext and the role button moved into
`BoardUiConfig`. The driver learned the Wio's discrete RF switch line — a `rf_sw` field R2
defined that *nothing had ever read*, because the Heltec carries `kPinNone`. Both boards
measured at **192/192, 0 % PER**, the Heltec run reproducing the pass-1 bench reference
exactly.

**Did not.** It added a board, not a measurement of the link. **Nothing in Pass 2 advances
D1**, and the two bench traces are not range data — they say so in their own headers.

**And it does not validate GateLink's carrier.** Seeed sells two Wio-SX1262 products that
share only the three SPI nets. The board in hand is the **Kit** (p-5982, B2B, GPIO 38–42);
GateLink's is the **header board** (p-6379, 2.54 mm pads). Bridge Impl Plan §2.3.1
finding 2 is closed **negatively**:

> The Kit validates the SX1262, the module's RF performance, RadioLib on a second board,
> and the injected-config seam. It does **not** validate the carrier's net list.
> *XIAO validates the module; only the carrier validates the carrier.*

The header board's pin map lives in **`gatelink-expansion-board.md` §6.1**, which owns it.
Do not copy it into a range-test board profile — it is a different product.

## Committed traces

| File | What it is |
|---|---|
| `2026-08-31-bench.csv` | Format proof, ~1 m bench link. **Not range data.** Also the trace the §7.6 tool was first exercised against — captured at **2.0 dBi** configured, where everything since carries 3.0 |
| `2026-09-04-walk-gatelink.csv` | **R10 walk, six positions.** 1152 probes, 2 lost downlink, 7 lost uplink, no dead test points. **Not a clear-field test** — the initiator was indoors at the bridge's target location. **Position 7 is not a location.** Caveats in the file's header |
| `2026-09-04-walk-gatelink-resplog.csv` | The responder's log for that walk. Closes against the sweep trace at all six positions |
| `2026-09-05-survey-campaign.csv` | All seven sites, 910 rows, pre-R11. **Superseded for peaks** — its `peak_dbm10` is not site-attributable. Floor and mean sound |
| `2026-09-05-survey-campaign-r11.csv` | **The M20 re-walk**, `hold_discipline=1` at every site. The trace the occupant inventory is built from. 68–74 passes, 130/130 bins, `dropped=0` |
| `2026-09-05-w9-bench.log` | **W9 / R9**, 64 round trips, zero faults either end. Console lines, not a CSV. **Not a link measurement** |
| `2026-09-05-bench-pass2-heltec.csv` | **Pass 2 regression check.** Heltec pair on pass 2 firmware, **192/192** — reproduces `2026-08-31-bench.csv` exactly. Third board parked in SURVEY, which is load-bearing |
| `2026-09-05-bench-pass2-xiao.csv` | **Pass 2, and NOT the B1b delta.** XIAO→Heltec, **192/192** — the first over-air proof the Wio's RF switch line works. Reads ~13 dB stronger RSSI, but **bench geometry is uncontrolled and dominates**: the Heltec reference itself moved −24 → −42 dBm between two runs on placement alone |
| `2026-09-06-m20-reintegration.csv` | **DERIVED, not captured — the only file here that is not a measurement.** The R11 trace re-integrated over 500 kHz on the US915 grid, plus per-125 kHz bins across 915.2–923.0. **Regenerate it, never hand-edit it** |

| `2026-09-07-eirp-sanity.csv` + `-resplog.csv` | **The §7.6 run that closed it.** Matched Heltec pair, three tape-measured distances. **All four checks PASS.** The first committed traces carrying the `pa_*` header fields |
| `2026-09-07-eirp-sanity-xiao.csv` + `-resplog.csv` | **§7's Wio repeat.** XIAO+Wio initiator. Checks 1, 2, 4 pass; the clamp holds on the Wio too. Check 3's WARNs are the module asymmetry, not geometry |
| `2026-09-07-eirp-sanity-swap.csv` + `-resplog.csv` | **The role swap — a negative result, kept deliberately.** It cannot separate TX from RX and neither can any such run. Useful as an independent repeat; **its absolute figures are not usable** (residuals 2.3–2.7 dB, check 3 failed at 12 m) |

**Everything above the 2026-09-07 rows predates the PA record.** A trace with no `pa_*`
lines was captured by a board flashed before 2026-09-06; that is the only thing its absence
means. **The three 2026-09-07 traces are the first committed ones carrying them.**

### The results to carry forward

**The occupant inventory is closed, off the R11 trace.** Read peaks from
`2026-09-05-survey-campaign-r11.csv` only.

- **A 915.8–916.4 MHz cluster at six of seven sites**, peaking at **−54 dBm at
  `bridge-house` on 916.0** — 62 dB over the floor and by a wide margin the loudest thing
  in the campaign. Property-wide. Found by the re-integration, not by the original
  inventory, which was built to ask whether one provisional channel was clear rather than
  to rank alternatives.
- **`weather-island` peaks −80 dBm at 915.0** against a −115 dBm median floor, reproducing
  within 1 dB across both campaigns. **The one confirmed occupant *on 915.0*** — and it is
  local to that site; every other site reads floor there. `propane-tank` sees −106 at
  915.2, also reproducing.
- **Retracted: `irrigation-pump` at 915.2.** −77 dBm pre-R11, −112 (floor) under hold
  discipline — picked up walking in. **One in-channel occupant site, not two.**
- **`gatelink-gate` peaks −66 dBm at 914.0**, 1 MHz off channel and the strongest near-band
  neighbour any node site has — at the site GateLink will live at.
- **No carrier anywhere in 902–928.** Every occupant is bursty, which is what §12.1 assumes
  when it plans to surface contention as `cad_backoffs`.
- **The floor is uniform and receiver-thermal-limited** — −115.0 dBm median at all seven
  sites, the indoor one included.
- **The survey covers 62.5 % of the band, not all of it.** 125 kHz of receiver bandwidth
  every 200 kHz. Floors and means scale soundly; **a narrowband occupant sitting in one of
  the 75 kHz gaps is invisible at any level.** Absence of a peak was already weak evidence
  and this makes it weaker. `data/README.md` has the arithmetic.

**The pre-R11 trace warned in its own header that the peak column was not attributable, and
it was right.** A caveat on a column is a claim to go back and test — and the same held for
the inventory's own framing, which is how the 916.0 cluster was finally seen.

### W9 passed, and left one thing for D1

Both runs passed over RF on the bench — §6.6.1's 222-byte maximum frame and §6.6.2's full
15-fragment set, 64 round trips, **zero faults at either end**. **No late fragments in
either direction across 512 frames**; §11.2's rule was chosen against a *hypothesised* RF
echo and there are none on this link. That is a **bench** negative at 1 m and says nothing
about the 500 ft path.

**The finding — §12.3's default backoff window is an SF7 assumption:**

| SF | 222-byte airtime (§15.1) | vs. `backoff_max_ms` 500 |
|---|---|---|
| **7** | **348 ms** | covers |
| 8 | 615 ms | **does not** |
| 9 | 1107 ms | **does not, by 2×** |

A window shorter than one frame's airtime cannot outlast the frame it backed off for. §12.3
permits transmitting after `cad_retries` regardless, so this is latency and `cad_backoffs`
rather than correctness — but `cad_backoffs` is the instrument §12.3 nominates to check
itself, and it would read high for a reason that is not congestion. **Raised, not patched.**

## D1 — a decision, against data that already exists

**Nothing external blocks it.** M20 and M21 are both closed. It needs a decision made
against the material below, plus B1b if the 500 ft leg is wanted first. **Read Protocol
Spec §18.2 and Decision Register §2.1 before picking any number** — there are four bounds
now, not three.

- **Frequency — there is a ranked answer. Use 917.2–917.6 MHz.** The provisional 915.0 MHz
  is `weather-island`'s occupant peak, **and a small move off it is worse, not better**:
  915.8–916.4 carries the property-wide cluster peaking at −54 dBm. 917.2–917.6 is ~1.2 MHz
  clear of it on both scorings, floor at −116 dBm, nearest neighbour of any strength −104
  dBm. Full table in Decision Register §5.4. Note 923.3–927.5 MHz is LoRaWAN US915
  *downlink*, so Envelope A's uncommitted region is roughly **915.2–923.0 MHz**.
- **BW and the rule section are one decision.** BW125 forces **Envelope A** (§15.249,
  ≈−1.2 dBm EIRP, any frequency in 902–928); **Envelope B** forces BW500 and 903.0–914.2
  MHz. **Envelope A is the plan of record** — the walk closed 0 % PER at its ceiling. If
  Envelope B is ever triggered the pick is **909.4 MHz**; half the US915 500 kHz grid is
  unusable here, 914.2 included, where `gatelink-gate` sees −66 dBm. And a BW500 receiver
  gives up **6.0 dB** of floor to bandwidth before any occupant is considered.
- **SF.** The W9 backoff table above is the constraint the survey did not supply. SF7 keeps
  §12.3's defaults valid as written; SF8+ needs `backoff_max_ms` raised above full-frame
  airtime.
- **TX power.** The working point is **−4 dBm conducted with the fitted 3.0 dBi antenna**,
  which is what the 2026-09-04 walk ran at and closed 0 % PER on at all six positions.
  **Do not derate to −9 dBm for conservatism** — it is the SX1262's hard floor and the same
  walk lost 12.5–25 % at SF7 there. Record conducted power and antenna gain **separately**;
  the ceiling is EIRP and a combined figure cannot be audited.

**Do not close D1 from range data alone**, and **do not re-walk M20** — it is closed, and
the 500 kHz re-integration was post-processing on the committed R11 trace. Regenerate the
derived file rather than editing it:

```bash
python3 tools/rangetest/survey_reintegrate.py \
    docs/rangetest/data/2026-09-05-survey-campaign-r11.csv \
    --out docs/rangetest/data/2026-09-06-m20-reintegration.csv
```

## Git state — read before pushing

As of the end of 2026-09-07:

- **`origin/main` is at `f199542`** (through PR #36).
- **`origin/docs/eirp-field-laptop`** carries everything since: the field-card commits
  fast-forwarded in, the field-laptop and NVS-collision documentation, the step 7 fix, and
  the six EIRP traces. **This is the branch to open the PR from** — it contains
  `docs/eirp-field-card`'s two commits, so merging it closes both.
- **`origin/docs/eirp-field-card` exists and has no PR.** Superseded by the branch above.
- **Local `main` is behind the working branch** and 2 ahead of `origin/main`; it was
  fast-forwarded onto the field-card commits before the working branch was cut.

**Everything is pushed. Nothing is local-only.** Verify with:

```bash
git status --porcelain          # empty
git log --branches --not --remotes --oneline   # empty
```

## First actions next session

1. **Open the PR** for `docs/eirp-field-laptop` if it is still open, and write the
   description. See *Git state* above.
2. `git pull --ff-only`, then run the checks above. **Auth is SSH** — if git hangs with no
   output, read *The field laptop* before assuming the network is down.
3. **No firmware work is queued and no reflash is owed.** The boards are current.
4. **B1b is the next measurement.** `FIELD-PROCEDURE.md` first, and **clear the responder's
   position log** before the run.
5. **If it is D1, it does not start in this directory** — it is a decision against data that
   already exists.
6. **Do not re-run the §7.6 permutations.** It is closed, and the TX/RX split is not
   reachable with the instruments here.

## Behaviour that changed, and will make traces look different

**From 2026-09-06:**

- **Five new `key=value` lines in the boot output** — `pa_optimize`, `pa_duty_cycle`,
  `pa_hp_max`, `pa_val`, `pa_table` — and `capture.py` puts them in the trace header
  automatically. **Confirmed on hardware 2026-09-07.** A trace without them was captured by
  a board flashed before 2026-09-06.
- **The conducted ceiling is −4 dBm** at the configured 3.0 dBi, where the comment in
  `phy_params.cpp` previously described −3 dBm at 2.0 dBi. **The code's arithmetic did not
  change** — it has always derived the ceiling from the configured gain — but a trace's
  ceiling is a function of the gain it was captured with, and `2026-08-31-bench.csv` was
  captured at 2.0.

**From Pass 2 (2026-09-05):**

- **Two build environments now**, `heltec` and `xiao`, selected by `-DLRAN_BOARD_*`. One
  selection point, `kBoard`/`kBoardUi` in `board_config.h`.
- **The boot banner now reads `pass 2` and `v0.8`.** It said `pass 1, branch 1 (R1-R3)` and
  `v0.7` until then — every capture before that carries the wrong string.
- **The XIAO's display is flipped 180°** for the enclosure, and its role button is
  **GPIO 21** on the Wio, not GPIO 0.
- **`[env:xiao]` carries `-Wno-error=cpp`**, the only relaxation of `-Werror` in the repo.
  Reasoning is at the flag.

**From 2026-09-04:**

- **The initiator boots ARMED.** No sweep runs until the first PRG press, so **positions
  start at 1, not 0.** A trace with a position 0 predates this.
- **Tap PRG = RESPONDER, hold ~1.5 s = SURVEY.** The survey is reachable untethered.
- **The survey's site cursor is persisted**; the role is not.
- **The responder saves each position as its sweep completes**, not on the next press.
- **`capture.py` drives the board** — `--reset`, `--role`, `--key`, `--key-after`,
  `--run-for`. Never run a serial console alongside it.

## Traps that cost real time here

The engineering log has the full account; this is the index.

- **A stale responder position log merges into the new run** and fakes an instrumentation
  fault. Clear it, and confirm the line. Section above.
- **A third powered board corrupts a two-board measurement**, silently, by up to 60 % PER,
  and the symptom is indistinguishable from poor link margin. It was mistaken for a code
  regression across two full sweeps on 2026-09-05. **A ten-minute bisect against the
  previous firmware settles this class of question — reach for it before asserting a
  regression, not after.**
- **A reflash session is a period during which every board transmits.** A flashed board
  boots ARMED, and esptool's hard reset *is* a boot — so a board comes off the programmer
  beaconing once a second. Observed 2026-09-06 with two other boards on the bench, which is
  the 60 % PER configuration above. Park or unplug each board as it finishes.
- **Bench geometry moves RSSI further than anything you are trying to measure.** The Heltec
  reference moved **−24 → −42 dBm between two runs on placement alone**, and 2026-09-07's
  desk throwaway wandered 24 dB between two sweeps at *identical* placement. That is 18–24
  dB against the ±6 dB an absolute EIRP figure is trying to resolve, and it is why §7.6
  wants a tape measure, three distances and a slope rather than one number.
- **Opening a serial port presses PRG — on the Heltec.** GPIO 0 is also IO0, driven by the
  CP2102's DTR. Host tools must set `dtr = False` **before** opening. The XIAO has no
  bridge chip and its button is GPIO 21, so this trap is Heltec-only.
- **Flashing the XIAO from a firmware with a different USB stack fails once.** Bootloader
  entry swaps the USB device, esptool loses its handle, `Could not configure port`. The
  board *is* in the bootloader — on a **new** device node. Flash to that. It did **not**
  recur on the 2026-09-06 reflash, as pass 2 predicted it would not.
- **A tool that does not read the port while it waits loses everything the board says.**
  Cost 19184 bytes of a survey dump, deterministically, and read as a firmware bug.
- **`capture.py` will not overwrite an existing `--out`** (needs `--force`).
- **Completion markers match as substrings anywhere in a `#` line.** Careless wording of a
  new firmware message truncates captures.
- **A boot line that is not `key=value` never reaches the trace.** `capture.py` collects
  `^[a-z][a-z0-9_]*=\S*$` into the header and counts everything else as `unparsed`. One
  space in one value and that line is silently absent — which is why the PA record's format
  is asserted by tests on both sides rather than eyeballed.
- **`--idle-timeout` cannot end a run against a board that never goes quiet.** Use
  `--run-for` for setup commands.
- **Never ask RadioLib's `getPacketLength()` whether a packet arrived**, and never use
  `getRSSI()` without `false` for an ambient reading. Both hold stale values.
- **The responder must follow the initiator's retune**, or a clean 100 % PER looks real.
- **TCXO 1.8 V, `setDio2AsRfSwitch(true)`, and — on the Wio — a real `rf_sw` pin** all fail
  *silently*. The radio initialises, reports a successful transmit, and puts nothing on the
  air. `begin()` returning success proves none of them; only frames crossing does.
- **Both CP2102 bridges report `SER=0001`.** Trust the confirmation from the command that
  did the work, not a separate check afterwards.
- **A git hang is not a network failure.** Private repo plus Plasma's
  `SSH_ASKPASS_REQUIRE=prefer` sends the credential prompt to a GUI dialog and blocks
  forever; `GIT_TERMINAL_PROMPT=0` does not help, because it disables the *terminal* prompt
  and not askpass. `curl` against the same host separates transport from authentication.
- **Never transmit without an antenna connected.** It is also one of the failures the §7.6
  check exists to detect — detect it in the trace, not by damaging a PA.
- **The OLED needs hand-shading in direct sunlight.** Procedure, not a defect.

## Open, and not closable from this firmware alone

- **B1b** — the gate-bearing walk with the Wio. **The only measurement this directory still
  owes.** The 2026-09-05 desk runs are not it, and neither are the 2026-09-07 EIRP runs.
- **D1** — nothing external blocks it. It needs a decision made against the data above, plus
  B1b if the 500 ft leg is wanted first.
- **M6** — **precondition met 2026-09-07**, so nothing external blocks it now. Still **not
  closed**: the last ~46 m is unwalked (that is B1b), and arcsecond GPS cannot support an
  RSSI-vs-distance curve. It answers "does it work there", not "what is the path loss".
- **D33** — **reopened 2026-09-06 by M21**, exactly as its own standing condition 1
  anticipated. The ceiling survives; the reasoning changed and the frame is §15.23
  home-built. Decision Register §3.3.
- **W7** — the §15.1 airtime table regenerates once D1 fixes SF/BW/CR.
- **§2.3.1 sub-question (b)** — the sleep-current cost of holding `RF_SW` high. Untouched;
  belongs to B1b.
- **`gatelink-expansion-board.md` §10 ring-out** — the header board's pads against the
  carrier's nets. **The Kit cannot close it.** It is the tracked check for Bridge Impl Plan
  §10.8.1's remaining premise.
- ~~**D31** — copyright holder. Every file carries the `<holder>` placeholder.~~
  **Closed 2026-09-08: Robert J. Lee.** `LICENSE` written at the repo root, placeholder
  replaced repo-wide. Superseding note, not a rewrite — the line above describes the state
  this handoff was written in.

### Closed, and not to be reopened by habit

- **M20** — **CLOSED 2026-09-06.** Field work plus re-integration. Results in Decision
  Register §5.4; derived file `2026-09-06-m20-reintegration.csv`. **Do not re-walk it.**
- **M21** — **CLOSED 2026-09-06.** Both grants recorded in `LRAN-M21-FCC-Grant-Findings`.
- **W9** — **CLOSED in spec v0.8.** Left the SF7 backoff finding above for D1.
- **§7.6 EIRP sanity check** — **CLOSED 2026-09-07**, all four checks, plus §7's Wio
  repeat. **M6's precondition is met.** Decision Register §3.3 and the M6 row carry it.
- **The Wio TX/RX split** — **not closable here, by construction.** Reciprocal RSSI gives
  only `(TX − RX)` per node. **Do not run another permutation.**
- **Handoff §6 requirement 7** — **DONE 2026-09-06**, and **confirmed in a real trace
  2026-09-07.** The PA record plus `check_pa_table.py` as the check on the mirror it needs.

### Backlog items that are not range-test work

- **M22** — bridge LoRa PER with WiFi idle vs. saturated. Belongs to the bridge.
- **M23** — BLE RSSI to the BMS from the Stamp-S3A at its final mounting position, and
  LoRa-to-BLE isolation in the same session. Supersedes **M5**. Belongs to GateLink, and it
  does **not** gate M6 or B1b.
