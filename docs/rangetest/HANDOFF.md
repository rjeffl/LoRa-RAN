# Range test — session handoff

**Written 2026-09-05, at the end of the session that built and measured Pass 2.**

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) — the **2026-09-05** entries | what happened and why. The last three are Pass 2: the board profile, the bench, and the third radio that ate 60 % of the first measurement |
| 3 | [`LRAN-Range-Test-Firmware-Pass2-Tasks.md`](./LRAN-Range-Test-Firmware-Pass2-Tasks.md) | the second board profile — what it validates and, more importantly, what it does not |
| 4 | [`LRAN-Range-Test-Firmware-Pass1-Tasks.md`](./LRAN-Range-Test-Firmware-Pass1-Tasks.md) | R1–R11, complete. Its Pass 2 section is closed against what it predicted |
| 5 | [`FIELD-PROCEDURE.md`](./FIELD-PROCEDURE.md) | read before any campaign. **Start with "Power down every board you are not measuring with"** |
| 6 | [`data/README.md`](./data/README.md) | the two schemas, and what each committed trace is *not* |

## Where things stand

| | |
|---|---|
| Branch | `main` at **05ae426**, verified green **2026-09-05**. **`main` is the only branch again, local and remote** — `docs/handoff-final` and `r2-pass2-xiao` were merged and pruned |
| Merged today | **#28** (handoff close-out), **#29** (Pass 2 phase A — the board profile), **#30** (Pass 2 phase B — the bench, plus the document audit and three governing-doc rules) |
| Spec | **`LRAN-Protocol-Specification` is v0.8**, `ver = 2`. Pass 2 changed **nothing** on the wire — no frame layout, no schema, no vector regenerates |
| Done | **Pass 1 R1–R11 and the M20 re-walk.** **Pass 2 X1–X10:** a second board profile, built and bench-measured |
| **Next** | **D1** — still a decision, not a build, and still waiting only on **M21**. The one piece of *bench* work this directory still owes is **B1b**, the gate-bearing walk with the Wio |

```bash
pio test -d firmware/range-test -e native   # host Unity suite
pio run  -d firmware/range-test -e heltec   # Heltec V3
pio run  -d firmware/range-test -e xiao     # XIAO ESP32S3 + Wio-SX1262 Kit
pio test -d lib/lran-protocol -e native
python3 tools/vectors/check.py
python tools/rangetest/test_capture.py      # PlatformIO's python
```

All green at 05ae426. `pio` is at `~/.platformio/penv/bin/pio` and is **not on `PATH`**;
`capture.py` needs `~/.platformio/penv/bin/python`.

> **Counts are deliberately not written here or in root `CLAUDE.md`.** They were wrong more
> often than right. Run the commands.

## Hardware state

**Three boards, all flashed from `main` at the Pass 2 phase B build.**

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
| `2026-08-31-bench.csv` | Format proof, ~1 m bench link. **Not range data** |
| `2026-09-04-walk-gatelink.csv` | **R10 walk, six positions.** 1152 probes, 2 lost downlink, 7 lost uplink, no dead test points. **Not a clear-field test** — the initiator was indoors at the bridge's target location. **Position 7 is not a location.** Caveats in the file's header |
| `2026-09-04-walk-gatelink-resplog.csv` | The responder's log for that walk. Closes against the sweep trace at all six positions |
| `2026-09-05-survey-campaign.csv` | All seven sites, 910 rows, pre-R11. **Superseded for peaks** — its `peak_dbm10` is not site-attributable. Floor and mean sound |
| `2026-09-05-survey-campaign-r11.csv` | **The M20 re-walk**, `hold_discipline=1` at every site. The trace the occupant inventory is built from. 68–74 passes, 130/130 bins, `dropped=0` |
| `2026-09-05-w9-bench.log` | **W9 / R9**, 64 round trips, zero faults either end. Console lines, not a CSV. **Not a link measurement** |
| `2026-09-05-bench-pass2-heltec.csv` | **Pass 2 regression check.** Heltec pair on pass 2 firmware, **192/192** — reproduces `2026-08-31-bench.csv` exactly. Third board parked in SURVEY, which is load-bearing |
| `2026-09-05-bench-pass2-xiao.csv` | **Pass 2, and NOT the B1b delta.** XIAO→Heltec, **192/192** — the first over-air proof the Wio's RF switch line works. Reads ~13 dB stronger RSSI, but **bench geometry is uncontrolled and dominates**: the Heltec reference itself moved −24 → −42 dBm between two runs on placement alone |

### The results to carry forward

**The occupant inventory is closed, off the R11 trace.** Read peaks from
`2026-09-05-survey-campaign-r11.csv` only.

- **`weather-island` peaks −80 dBm at 915.0** against a −115 dBm median floor, reproducing
  within 1 dB across both campaigns. **The one confirmed in-channel occupant.**
  `propane-tank` sees −106 at 915.2, also reproducing.
- **Retracted: `irrigation-pump` at 915.2.** −77 dBm pre-R11, −112 (floor) under hold
  discipline — picked up walking in. **One in-channel occupant site, not two.**
- **`gatelink-gate` peaks −66 dBm at 914.0**, 1 MHz off channel and the strongest near-band
  neighbour any node site has — at the site GateLink will live at.
- **No carrier anywhere in 902–928.** Every occupant is bursty, which is what §12.1 assumes
  when it plans to surface contention as `cad_backoffs`.
- **The floor is uniform and receiver-thermal-limited** — −115.0 dBm median at all seven
  sites, the indoor one included.

**The pre-R11 trace warned in its own header that the peak column was not attributable, and
it was right.** A caveat on a column is a claim to go back and test.

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

## D1 is the next work, and it is a decision

- **Channel.** `weather-island` has a confirmed in-channel occupant at 915.0;
  `gatelink-gate` has the strongest near-band neighbour at 914.0 (−66 dBm). Both bursty.
  Moving off 915.0 is available — the survey covers 902.0–927.8 in 200 kHz bins and the
  other five sites are quiet.
- **SF.** The backoff table above is the constraint the survey did not supply. SF7 keeps
  §12.3's defaults valid as written; SF8+ needs `backoff_max_ms` raised above full-frame
  airtime.
- **TX power.** Needs **M21**, the modules' FCC grant conditions — **and M21 is now two
  modules**, the Heltec's SX1262 and the Seeed Wio-SX1262. Separate grants, both open.

**Do not close D1 from range data alone.** The frequency needed M20 and has it; the power
needs M21 and does not.

## First actions next session

1. `git checkout main && git pull --ff-only`, then run the checks above. Everything through
   **#30** is merged and `main` is at **05ae426**. No branches to reconcile.
2. **No firmware work is queued.** Pass 1 and Pass 2 are both complete and merged.
3. **Decide which of the two open threads you are on**, because they are not the same job:
   - **D1** — a decision against the data above. Read the W9 backoff table before picking
     an SF and the occupant inventory before picking a channel. **Blocked on M21**, which
     is paperwork and which nothing in this repo advances.
   - **B1b** — the gate-bearing walk with the Wio. This is the only bench work this
     directory still owes, and it is a walk, not a build. The desk runs are explicitly not
     it.
4. **If it is B1b:** re-read `FIELD-PROCEDURE.md` first, and note the third board. Two
   boards make a measurement; a spare still powered in a backpack is in the experiment.

**If the next session is D1, it does not start in this directory.** The decision is
recorded in `LRAN-Decision-Register`, the constraints live in the protocol specification
(§12.1, §12.3, §15.1), and this firmware's job — supplying the measurements — is finished
except for B1b.

## Behaviour that changed, and will make traces look different

**From Pass 2 (2026-09-05):**

- **Two build environments now**, `heltec` and `xiao`, selected by `-DLRAN_BOARD_*`. One
  selection point, `kBoard`/`kBoardUi` in `board_config.h`.
- **The boot banner now reads `pass 2` and `v0.8`.** It said `pass 1, branch 1 (R1-R3)` and
  `v0.7` until this session — every capture before then carries the wrong string.
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
- **The OLED needs hand-shading in direct sunlight.** Procedure, not a defect.

## Open, and not closable from this firmware alone

- **D1** — M20 closed. Waits only on **M21**.
- **M21** — **now two modules**: the Heltec's SX1262 and the Seeed Wio-SX1262. Separate FCC
  grant conditions, both open. Paperwork, not bench work.
- **B1b** — the gate-bearing walk with the Wio. **Owed by this directory.** The 2026-09-05
  desk runs are not it.
- **M6** — has data (six positions, all closing with margin) but is **not closed**:
  arcsecond GPS cannot support an RSSI-vs-distance curve. It answers "does it work there",
  not "what is the path loss".
- **W7** — the §15.1 airtime table regenerates once D1 fixes SF/BW/CR.
- **W9** — **CLOSED in spec v0.8.** Left the SF7 backoff finding above for D1.
- **§2.3.1 sub-question (b)** — the sleep-current cost of holding `RF_SW` high. Untouched;
  belongs to B1b.
- **`gatelink-expansion-board.md` §10 ring-out** — the header board's pads against the
  carrier's nets. **The Kit cannot close it.** It is the tracked check for Bridge Impl Plan
  §10.8.1's remaining premise.
- **D31** — copyright holder. Every file carries the `<holder>` placeholder.
