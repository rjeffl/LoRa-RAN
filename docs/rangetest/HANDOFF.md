# Range test — session handoff

**Written 2026-09-06, at the end of the session that closed M21 and M20 and set up the
§7.6 EIRP check.** It replaces the 2026-09-05 file wholesale.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) — the **2026-09-06** entries | what happened and why. The last three are M21's closure, M20's re-integration, and the §7.6 check. Before them, the 2026-09-05 Pass 2 entries |
| 3 | [`EIRP-SANITY-CHECK.md`](./EIRP-SANITY-CHECK.md) | the §7.6 procedure. **M6's precondition**, needs no firmware change, and is the next bench job |
| 4 | [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md) | read before any campaign. **Start with "Power down every board you are not measuring with"** |
| 5 | [`LRAN-M21-FCC-Grant-Findings`](../shared/LRAN-M21-FCC-Grant-Findings.md) + Protocol Spec **§18.2** | the regulatory frame. **Read before picking any number for D1** — `BW` and the rule section are one decision now |
| 6 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) **§2.1, §3.3, §5.4** | D1's four bounds, D33's reopening, and the ranked channel evidence |
| 7 | [`LRAN-Range-Test-Firmware-Pass2-Tasks.md`](./LRAN-Range-Test-Firmware-Pass2-Tasks.md) | the second board profile — what it validates and, more importantly, what it does not |
| 8 | [`LRAN-Range-Test-Firmware-Pass1-Tasks.md`](./LRAN-Range-Test-Firmware-Pass1-Tasks.md) | R1–R11, complete. Its Pass 2 section is closed against what it predicted |
| 9 | [`data/README.md`](./data/README.md) | the two schemas, what each committed trace is *not*, and **the 62.5 % band-coverage caveat** |

## Where things stand

| | |
|---|---|
| Branch | **`main` at 8653d38**, verified green **2026-09-06**. **`main` is the only branch, local and remote** — `m21-closure` was merged and pruned |
| Merged 2026-09-06 | **#31** (the previous handoff rewrite), **#32** (M21 closure, M20 re-integration, the §7.6 setup) |
| Spec | **`LRAN-Protocol-Specification` is v0.8**, `ver = 2`. Nothing on the wire changed — no frame layout, no schema, no vector regenerates. **§18.2 is the authoritative Part 15 section**; §18.1 is annotated, not rewritten |
| Done | **Pass 1 R1–R11**, **Pass 2 X1–X10**, **M20 (closed)**, **M21 (closed)** |
| **Next** | **The §7.6 EIRP sanity check** — a bench measurement, procedure written, tool written and host-tested, **run not performed**. It gates **M6**, so it comes before B1b. After it: **B1b**, the gate-bearing walk with the Wio. **D1** is a decision and does not start in this directory |

```bash
pio test -d lib/lran-protocol -e native      # host Unity suite
pio test -d firmware/range-test -e native    # host Unity suite
pio run  -d firmware/range-test -e heltec    # Heltec V3
pio run  -d firmware/range-test -e xiao      # XIAO ESP32S3 + Wio-SX1262 Kit
python3 tools/vectors/check.py
python tools/rangetest/test_capture.py       # PlatformIO's python
python3 tools/rangetest/test_survey_reintegrate.py
python3 tools/rangetest/test_eirp_check.py
```

All green at 8653d38. `pio` is at `~/.platformio/penv/bin/pio` and is **not on `PATH`**;
`capture.py` needs `~/.platformio/penv/bin/python`.

> **Counts are deliberately not written here or in root `CLAUDE.md`.** They were wrong more
> often than right. Run the commands.

## Hardware state

**Three boards, all flashed from `main` at the Pass 2 phase B build.** No firmware change
has landed since; the flashed image is current.

| Board | Env | Port | Notes |
|---|---|---|---|
| Heltec V3 ×2 | `heltec` | `/dev/cu.usbserial-*` | Both report `SER=0001`; told apart only by USB location |
| XIAO ESP32S3 + Wio-SX1262 **Kit** | `xiao` | `/dev/cu.usbmodem*` | On a Seeeduino XIAO Expansion Board. **Meshtastic has been overwritten** |

**Read `board=` off the R3 settings dump to know which board you are talking to.** It is
the only reliable identifier, and a wrong board selection is otherwise silent — the build
still boots, still displays, and writes the wrong pin map and antenna gain into a
normal-looking CSV.

> ### Power down every board you are not measuring with
>
> An idle ARMED initiator **beacons once a second** on the single fixed channel. A third
> powered board cost up to **60 % PER** on 2026-09-05 and looked exactly like poor link
> margin. Unplug spares, or park one in `SURVEY` (`--role survey`) — it listens and never
> transmits. **ARMED is not idle.** Full account in `FIELD-PROCEDURE.md`.

## What 2026-09-06 changed

**M21 closed, and D33 reopened with its ceiling intact.** Both modules' grants are recorded
in `LRAN-M21-FCC-Grant-Findings`. Neither is §15.249 — both are §15.247 DTS + DSS — and the
grants **do not transfer**, so the project's operative frame is **§15.23 home-built**.
**No node may be represented as FCC certified anywhere**: not a README, a LICENSE header, an
enclosure label or HA device metadata.

**Three things the committed traces changed in the findings as received**, which is the
reason to keep raising discrepancies rather than adopting a document wholesale:

- **The working point is −4 dBm conducted, not −9.** −9 dBm is the SX1262's hard floor
  (`kSx1262MinDbm`), not a conservative setting, and the 2026-09-04 walk measured
  **12.5–25 % PER at SF7 there** where −4 dBm was clean at all six positions. `phy_params.cpp`
  now carries the −4 dBm arithmetic and a note saying **not** to credit feedline loss.
- **M20's field work was already done.** Its M21 amendment turned out to be arithmetic on a
  committed trace, not a second campaign. **Do not re-walk M20.**
- **The SX1262 has no low-power PA**, so the obligation became logging the applied
  `paOptTable` entry — see the owed firmware change below.

**M20 closed, and the re-integration found the loudest signal in the campaign.** Details in
the D1 section.

**The §7.6 check was set up without a firmware change**, and running the new tool against
the oldest committed trace (`2026-08-31-bench.csv`, a format proof) already passes two of
its three checks: the power step tracks (+5.8 / +5.6 dB against 6.0 expected) and the D33
clamp ran over the air. The absolute figure reads 11 dB low, which is desk geometry, and is
exactly why the procedure insists on a tape measure and three distances.

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
Spec §18.2 and Decision Register §2.1 before picking any number** — there are now four
bounds, not three.

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

## First actions next session

1. `git checkout main && git pull --ff-only`, then run the checks above. Everything through
   **#32** is merged and `main` is at **8653d38**. No branches to reconcile.
2. **No firmware work is queued except one small item** — see "the owed firmware change"
   below. Pass 1 and Pass 2 are both complete and merged.
3. **Decide which thread you are on, and they are ordered:**
   - **The §7.6 EIRP sanity check** — a short-range bench measurement, procedure in
     [`EIRP-SANITY-CHECK.md`](./EIRP-SANITY-CHECK.md). **No firmware change needed**, and it
     is M6's stated precondition, so **it comes first if the bench is available**. Heltec
     pair, three tape-measured distances, then the Wio repeat, which is cheap once the site
     is set up and is **not** B1b.
   - **B1b** — the gate-bearing walk with the Wio. The only field work this directory still
     owes. A walk, not a build; the 2026-09-05 desk runs are explicitly not it.
   - **D1** — a decision against the data above. **It does not start in this directory:**
     it is recorded in `LRAN-Decision-Register` and its constraints live in the protocol
     specification (§12.1, §12.3, §15.1, §18.2).
4. **If it is a bench or field session:** re-read `FIELD-PROCEDURE.md` first, and note the
   third board. Two boards make a measurement; a spare still powered in a backpack is in
   the experiment.

### The owed firmware change

**Handoff §6 requirement 7 — log the applied `paOptTable` entry and the `optimize` flag at
boot.** Small, and the only firmware work this thread still owes. Until it exists **a trace
does not record which PA configuration produced its numbers**, which is a gap in the §15.23
good-engineering-practice record rather than a defect in the link. It is not a precondition
for the §7.6 check — that check reads the ceiling out of the row — but it belongs in the
same neighbourhood and is worth doing while the reasoning is fresh.

## Behaviour that changed, and will make traces look different

**From 2026-09-06:**

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

- **A third powered board corrupts a two-board measurement**, silently, by up to 60 % PER,
  and the symptom is indistinguishable from poor link margin. It was mistaken for a code
  regression across two full sweeps on 2026-09-05. **A ten-minute bisect against the
  previous firmware settles this class of question — reach for it before asserting a
  regression, not after.**
- **Bench geometry moves RSSI further than anything you are trying to measure.** The Heltec
  reference moved **−24 → −42 dBm between two runs on placement alone**. That is 18 dB
  against the ±6 dB an absolute EIRP figure is trying to resolve, and it is why §7.6 wants
  a tape measure, three distances and a slope rather than one number.
- **Opening a serial port presses PRG — on the Heltec.** GPIO 0 is also IO0, driven by the
  CP2102's DTR. Host tools must set `dtr = False` **before** opening. The XIAO has no
  bridge chip and its button is GPIO 21, so this trap is Heltec-only.
- **Flashing the XIAO from a firmware with a different USB stack fails once.** Bootloader
  entry swaps the USB device, esptool loses its handle, `Could not configure port`. The
  board *is* in the bootloader — on a **new** `/dev/cu.usbmodem*`. Flash to that. It does
  not recur once this firmware is installed.
- **A tool that does not read the port while it waits loses everything the board says.**
  Cost 19184 bytes of a survey dump, deterministically, and read as a firmware bug.
- **`capture.py` will not overwrite an existing `--out`** (needs `--force`).
- **Completion markers match as substrings anywhere in a `#` line.** Careless wording of a
  new firmware message truncates captures.
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
- **Never transmit without an antenna connected.** It is also one of the failures the §7.6
  check exists to detect — detect it in the trace, not by damaging a PA.
- **The OLED needs hand-shading in direct sunlight.** Procedure, not a defect.

## Open, and not closable from this firmware alone

- **The §7.6 EIRP sanity check** — **owed, and it gates M6.** Procedure and tool committed
  2026-09-06; the run has not been performed. **The next job in this directory.**
- **B1b** — the gate-bearing walk with the Wio. **Owed by this directory.** The 2026-09-05
  desk runs are not it.
- **Handoff §6 requirement 7** — log the applied `paOptTable` entry and the `optimize` flag
  at boot. **The one firmware change this thread still owes.**
- **D1** — nothing external blocks it. It needs a decision made against the data above, plus
  B1b if the 500 ft leg is wanted first.
- **M6** — has data (six positions to 106 m, all closing with margin) but is **not closed**:
  the last ~46 m is unwalked (that is B1b), and arcsecond GPS cannot support an
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
- **D31** — copyright holder. Every file carries the `<holder>` placeholder.

### Closed this session, and not to be reopened by habit

- **M20** — **CLOSED 2026-09-06.** Field work plus re-integration. Results in Decision
  Register §5.4; derived file `2026-09-06-m20-reintegration.csv`. **Do not re-walk it.**
- **M21** — **CLOSED 2026-09-06.** Both grants recorded in `LRAN-M21-FCC-Grant-Findings`.
- **W9** — **CLOSED in spec v0.8.** Left the SF7 backoff finding above for D1.

### New backlog items, neither of which is range-test work

- **M22** — bridge LoRa PER with WiFi idle vs. saturated. Belongs to the bridge.
- **M23** — BLE RSSI to the BMS from the Stamp-S3A at its final mounting position, and
  LoRa-to-BLE isolation in the same session. Supersedes **M5**. Belongs to GateLink, and it
  does **not** gate M6 or B1b.
