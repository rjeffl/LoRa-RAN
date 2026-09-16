# `range-test` — bench instrument, pass 1

**Subordinate to `/CLAUDE.md`.** Everything there applies. This file adds only what is
specific to this target.

**Primary document:** `docs/rangetest/LRAN-Range-Test-Firmware-Pass1-Tasks.md`.
**Binding protocol:** `docs/shared/LRAN-Protocol-Specification` **v0.12** (`ver = 2`).
**Record:** `docs/rangetest/engineering-log.md`.
**Prose:** root `## Writing` — use the `nbj-write-clearly` skill. It bites hardest here,
because most of this target's writing is dated campaign record: engineering-log entries,
`HANDOFF.md`, `FIELD-PROCEDURE.md` and the traces under `docs/rangetest/data/`. **Correct
one with a new dated entry or a marked-superseded note, never by rewriting it** — a
reading taken on 2026-09-05 keeps the words it was written with, including its hedges.

## This firmware never ships

It is **not** a node, **not** the seed of bridge firmware, and nothing in `src/` is a
template for one. It answers **D1**, and hosts **M6**, **M20** and **W9**.

Two consequences, both easy to violate by accident:

1. **Nothing migrates from here into node firmware.** The pieces are modular so *later
   bench work* can reuse them, and so pass 2 is a config addition rather than a refactor
   (R2). That reuse direction is outward from `lib/` and from this directory — never from
   this `main` into `firmware/bridge/` or `firmware/gatelink/`.
2. **`firmware/bridge/` and `firmware/simnode/` stay empty shells.** Do not fill them
   from here.

## The one specification rule this binary deliberately breaks

**§12.1 says "LoRa PHY parameters are not runtime-configurable."** This binary makes
frequency, SF, CR, TX power and payload size runtime-settable, because that is what a
sweep *is* (task guardrail 4).

It is confined to `phy_params.{h,cpp}` and the `TestPoint` struct. **Nothing that reads a
PHY parameter at runtime may migrate into node firmware** — on a fleet with no OTA, one
mismatched parameter is a walk to the gate with a laptop.

## No WiFi, no MQTT, no `secrets.h`, no Home Assistant

Task guardrail 2. If a task seems to need any of them, it is the wrong task.

This costs nothing on the protocol side: **`PING` carries no MAC** (§9.2 — every
authenticated type is bridge → node, and `PING` is not one), so W9 needs no key material.
`DecodeCtx::mac` stays `nullptr` here, which would be a serious defect in a real receiver
and is correct in this one.

**D34 does not apply.** `CommandGate` binds the first firmware that accepts a `COMMAND`
(simnode B0, GateLink M3). This one echoes unauthenticated `PING`.

## Board gotchas

- **RadioLib pinned exactly — `jgromes/RadioLib@7.7.1`**, no caret (**D32**, repo rule 9).
  Four firmwares share this driver. The OLED driver is pinned the same way: a bench
  instrument whose display library moves under it produces unexplained differences
  between one walk and the next.
- **Radio pins come from `BoardRadioConfig`, never `#define`.** Values are transcribed
  from the vendor variant `pins_arduino.h` at a recorded framework version — see the
  provenance block in `src/board_config.h`. They agree with Bridge Impl Plan §10.8.1,
  which described its Heltec column as *derived*; it is now *confirmed*.
- **GPIO 14 is `DIO1`, and the vendor header calls it `DIO0`.** That is the SX127x-era
  label on an SX1262 line. The number is right; the name is legacy. Do not "fix" it.
- **TCXO is 1.8 V and DIO2 drives the RF switch.** Both fail *silently* — the radio
  initialises, reports success, and transmits nothing. Both are in the board config and
  both are pinned by host tests.

## Pass 2 — two boards now, and the gotchas doubled

- **There are TWO environments: `heltec` (default) and `xiao`.** One selection point,
  `kBoard` / `kBoardUi` in `board_config.h`, chosen by `-DLRAN_BOARD_XIAO_WIO_KIT`. The
  preprocessor picks *which instance* and nothing else — it never reaches into the driver,
  the role logic or the UI.
- **A WRONG BOARD SELECTION IS QUIET.** The build still boots, still displays, and writes
  the wrong pin map and the wrong antenna gain into a CSV that looks entirely normal.
  **Read the board name off the R3 settings dump before trusting a trace.**
- **`kXiaoWioKit` is the B2B product (p-5982), not the header board (p-6379).** They are
  not pin-compatible outside the three SPI nets. The name carries "Kit" for that reason;
  a constant called `kXiaoWio` would be the wrong map half the time and look right both.
- **The Wio needs BOTH RF-switch mechanisms** — `dio2_as_rf_switch` *and* a real `rf_sw`
  pin. The Heltec needs only the first, which is why `rf_sw` was dead code for all of
  pass 1. This is the *third* silent failure on this board, not the second.
- **This board does not validate GateLink's carrier.** It validates the module, the driver
  and the config seam. See Pass 2 Tasks §2.2 before quoting a result from it as a GateLink
  result.
- **The XIAO's USB is the ESP32 itself.** A reset tears the port down and the host must
  re-enumerate. The serial role selector and `capture.py`'s boot window have less margin
  here than on the Heltec's CP2102; the button selector does not. On macOS it enumerates
  as `/dev/cu.usbmodem*`, not `/dev/cu.usbserial*`.
- **Every board profile is checked for pin collisions** by `has_pin_conflict()`, a
  `static_assert` per profile plus host tests. Not theory: the header-board product would
  have put NSS and RF_SW straight on top of the expansion board's I2C bus.
- **The OLED needs hand-shading in direct sunlight** (confirmed outdoors, 2026-08-31).
  Contrast is already maxed; this is a panel limit, not a layout one. Consequence for
  edits: a hand-shaded glance is brief, so RSSI stays the largest element and the
  display does not grow a fourth line.
- **OLED sits behind Vext (active LOW).** Enable Vext, pulse the OLED reset, *then* I2C.
  A dark panel is usually Vext, not the driver. The sequence is lifted from
  `/wattcycle-reader/src/BmsDisplay.cpp`, where it is verified on this board.
- **The PRG button is the BOOT strapping pin.** See below.
- **A host opening the serial port presses PRG.** GPIO 0 is also IO0, and IO0 is driven by
  the USB bridge's DTR — `serial.Serial(port, ...)` asserts DTR as it opens, so opening the
  port holds the button down. In survey mode that is store-and-advance, so every tethered
  session stored a bogus run and stepped the campaign cursor; it presents as *"the erase
  does not stick"*. **Any host tool touching these boards must set `dtr = False` before
  opening** (construct the port unopened — setting it afterwards is too late). `prg_edge()`
  is the second line of defence: a press must hold the line low for 50 ms, so a pulse on
  close cannot register. Confirmed on hardware 2026-09-04: tap still advances the position,
  hold still selects SURVEY.
- **Never ask `getPacketLength()` whether a packet arrived.** It holds the length of the
  *last* packet and is not cleared by reading, so a poll built on it re-reports one
  buffered frame forever. Gate on the DIO1 interrupt. This cost a bench run to find and
  would have silently zeroed R4's PER — see the engineering log, 2026-08-31. **Any
  firmware here that polls RadioLib has the same trap available to it.**
- **Both CP2102 bridges report `SER=0001`.** The boards are not distinguishable by USB
  serial number, only by enumerated device node, which is not stable across replug. Do
  not write a port name into anything durable; the OLED badge is the reliable identifier.

## Five modes, and the survey never transmits

`SURVEY` (R8 / M20) is the third mode on this binary, selected with `v` in the boot window.
It scans 902.0-927.8 MHz in 200 kHz steps and **listens only** - there is deliberately no
path from the survey loop to `transmit()` or `set_power()`, and `loop()` returns before the
frame handling runs. Keep it that way.

- **`getRSSI(false)`, never `getRSSI()`.** The default reads the packet-status register,
  which holds the *last frame's* RSSI and is not cleared - the same trap `poll()` fell into
  with `getPacketLength()`. On an empty bin the survey would report a stale reading from a
  bin scanned minutes ago. **Third occurrence of this register family's bug in this
  firmware.**
- **The settle time after each retune is a measurement, not a delay.** The SX1262's RSSI
  climbs while its AGC settles; sampling through it drags every bin's mean down by the same
  amount, which looks exactly like a clean, quiet band.
- **The scan is HELD between sites (R11).** Two PRG presses per site: one on arrival to
  start the dwell, one when it is done to store and advance. Before R11 the scan never
  stopped, so the walk to each site was folded into that site's run and a burst heard in
  transit became a permanent occupant of a peak hold. **No press pattern avoids that** -
  pressing on arrival only moves the contamination to the site just left - so it needs the
  phase. `SurveyCampaign` in `survey.h` owns the cursor and the phase and **not** NVS: a
  store fails by short write, and a cursor that advanced over an unwritten site is a site
  silently lost, so the caller reports the outcome back through `note_stored()`.
  Boot and power cycle come up **Held**.
- **`hold_discipline` lives in the NVS blob (v2), not in the dump code.** A reader asks how
  the data was COLLECTED, and the firmware reading NVS is not the firmware that collected
  it - printing a constant made a re-dump of the pre-R11 campaign claim a discipline it
  never had, caught on hardware within an hour of flashing. **v1 blobs are still read** and
  report `hold_discipline=0`: rejecting them to add one bit would have destroyed the only
  copy of the campaign that motivated the bit. Blob is 1584 bytes now, not 1580.
- **The site cursor is persisted, the role is not.** R1 governs the ROLE (a power cycle
  re-asks); campaign PROGRESS is different and must survive, because this board has no
  battery and every move between laptop and power bank is a power cycle. Without it the
  cursor restarted at 0 and the only way forward was to press PRG past the finished
  sites - which STORES an empty run over each one on the way.
- **`SURVEY` is reachable by holding PRG**, not only by serial `v`. Serial-only was a
  field-blocking bug: an unplugged board came back as `INITIATOR`, the mode that
  transmits. See `src/role.h`.
- **Seven named sites, one stored run each** (`bridge-house`, `gatelink-gate`,
  `weather-island`, `welllink-well`, `irrigation-pump`, `hopyard-lower`, `propane-tank`).
  Seven blobs of 1580 bytes in a 20 kB NVS partition - the fit is asserted by a host test,
  not assumed. A short write is reported and does **not** advance the site.
- **A campaign dump prints ONE header for all seven sites.** A header reprinted per site is
  indistinguishable, to anything reading the port, from a board reboot - `capture.py` read
  it exactly that way and stopped after the first site.
- **Occupancy detection is probabilistic and the absence of a peak proves nothing.** One
  radio sees each bin ~1/130 of the time. The floor and mean are solid; a quiet bin is not
  a proven empty one.

## W9 is the only thing here that links `/lib/lran-protocol/`

**R9, `src/w9.{h,cpp}`.** Everything before it deliberately did not: the sweep measures
the **radio link** with its own raw frame (`bench_frame.h`), W9 measures the **protocol**
with the real codec and real `PING` frames. Two runs, both over RF:

1. **§6.6.1** — a 202-byte echo, which is a frame of exactly **222 bytes**, `LRAN_MAX_FRAME`.
2. **§6.6.2** — the same echo with `frag_chunk = 14`, the full **15-fragment** set, and the
   only mechanism in the protocol that exercises reassembly over the air.

- **Selected by serial `w` / `x` in the boot window, not by PRG.** The survey needed a
  button because the walking board is untethered by definition; W9 is a bench run with
  both boards reachable from a console, and its output is a per-fragment fault report that
  only means anything on one. A fourth and fifth PRG gesture would put the protocol bench
  one mistimed thumb away from the walk, on a board whose default role transmits.
- **`frag_chunk` appears nowhere on the wire** (§6.6.2), so the responder cannot be told
  which one the initiator used — it **infers** it from the largest fragment in the received
  set. §11.1 makes that sound: every fragment but the last carries the same length. The
  shape must be read **before** the reassembler completes, because the fragments are gone
  afterwards.
- **The responder echoes the REASSEMBLED bytes, never a regenerated pattern.** Rebuilding
  the echo from `n` would pass the run no matter what the link did to the bytes on the way
  in, which is the one way to make this test worthless.
- **`PATTERN_FILL` is not optional here** (§6.6.3). The CRC says a frame is corrupt; the
  pattern says *which byte*, and that offset is the only thing separating a marginal RF
  path from a reassembly or buffer-indexing bug. Both are live the first time §6.6.2 runs.
- **The echo timeout is sized from airtime, not guessed.** The fragmented run puts 15
  frames on the air in each direction; a timeout that fitted run 1 scores every run-2 PING
  lost.
- **Guardrail 6 holds.** Nothing reaches into `/lib/lran-protocol/` — it is consumed
  through its public headers exactly as node firmware will. R9 is the first time that API
  is driven by something that is not its own test suite, which is a second thing the run
  is worth.

## The walking operator can only see the responder's display

R5 has the operator press PRG, stand still for a ~7 minute sweep, then move on - from
several hundred feet away, where the initiator's console and OLED are invisible. So
**`BenchKind::ArmedBeacon` exists purely as their go signal.**

It used to be a `WarmupProbe`, which is also what the initiator sends five times *during* a
sweep at each configuration change - so the responder could not say "done" without saying
it five times too early. Echoed like any probe, counted by neither end, and distinguishable.

The responder shows an inverted `DONE` bar. **Measured 2026-09-03: a sweep takes 424 s and
the go signal reaches the responder 17.2 s after it ends** - the responder is cycling from
SF12 back round to the beacon's configuration. That lag is expected, documented in
[`FIELD-PROCEDURE.md`](../../docs/rangetest/FIELD-PROCEDURE.md), and not a defect.

Anything that changes when a sweep ends, or what the responder displays, has to keep this
signal unambiguous. It is the only thing standing between the operator and a timed guess.

## Role selection deviates from R1, deliberately

R1 says the role is selected by "holding the PRG button at boot". **On this board that
cannot work as written.** PRG is GPIO 0, the ESP32-S3 BOOT strapping pin; held low
*through reset* it puts the chip in the ROM serial downloader, so the application never
runs and never reads the button.

Implemented instead as a **3-second selection window after the application starts** —
OLED countdown, press PRG for `RESPONDER`, no press for `INITIATOR`. Every property R1
asked for survives: one binary, two roles, no persistence, role on the OLED, no laptop at
the walking end. Reasoning is in `src/role.h`.

`INITIATOR` is the no-press default because it is the tethered end: if the default is ever
wrong, the operator is sitting at the laptop that shows it.

**A serial selector sits alongside PRG.** Sending `i` or `r` during the same window picks
the role directly — needed because R2's gate is worked with both boards tethered to one
machine, where a thumb cannot reach two buttons in two 3-second windows. Additive, still
inside the window, still not persisted.

**Both selectors are confirmed on hardware** (2026-08-31): reset then PRG inside the
window gives `RESPONDER`, no press gives `INITIATOR`, and the serial characters do the
same. GPIO 0 is confirmed behaviourally rather than off the schematic — the stronger of
the two checks, since what matters is that the button reaches that GPIO.

## TX power is clamped in code, not by discipline

Task guardrail 3 and **D33**. `clamp_conducted()` in `phy_params.cpp` is the only path to
an output-power value.

- **Conducted power and antenna gain are recorded separately** (D33 standing condition 1).
  The ceiling is EIRP; a combined figure cannot be audited. They travel together in
  `PowerPoint` so they cannot drift apart between the clamp and the CSV.
- **The clamp rounds down, never to nearest.** Rounding a half-dB up is a transmission
  above the ceiling. Integer tenths of a dB throughout — no floats.
- **`BelowRadioFloor` is a refusal, not a floor.** With enough antenna gain the EIRP
  ceiling drops below the SX1262's −9 dBm minimum; the firmware says so and does not
  transmit.
- **The sweep starts at the bottom and climbs only on failure.** A working point chosen at
  an unusable power is a result you throw away.

**M21 closed 2026-09-06** — both modules' grants are recorded in
`docs/shared/LRAN-M21-FCC-Grant-Findings.md`. Neither is §15.249; the frame is **§15.23
home-built** and **no node may be represented as FCC certified anywhere**. The D33 ceiling
survives, and the working point is **−4 dBm conducted at the fitted 3.0 dBi antenna**.

**The antenna gain is a build flag, not a value in `main.cpp`** — `-DLRAN_ANTENNA_GAIN_DBI10`,
set explicitly in both environments, so an antenna swap is one line visible in the diff.
(This paragraph said "the 2.0 dBi in `main.cpp`" until 2026-09-06; it was wrong on both the
figure and the location.)

**The PA configuration is on the record too** (handoff §6 requirement 7, 2026-09-06). The
boot output carries `pa_optimize`, `pa_duty_cycle`, `pa_hp_max`, `pa_val` and `pa_table`,
and `capture.py` folds them into the trace header.

- **`setOutputPower` is called with TWO arguments**, passing `kPaOptimize` explicitly. It is
  `true`, which is what RadioLib's one-argument overload already did, so nothing on the air
  changed — but a flag that alters emitted power is not left to a library default.
- **`pa_config.cpp` MIRRORS RadioLib's `paOptTable`, and a mirror can drift.** It has to:
  the table is file-static in `SX1262.cpp`, and the SX1262's PA config is written by a
  command, not to a readable register. **`python3 tools/rangetest/check_pa_table.py` is the
  check on that premise** — run it after any RadioLib version change. It fails loudly when
  the pinned source is absent rather than skipping.
- **`begin()` re-asserts the power after RadioLib's `begin()` sets it**, because RadioLib
  hardcodes `optimize = true` internally. Today they agree; the day they do not, the boot
  record would otherwise describe a configuration the radio was not in.

## Build and test

```bash
pio test -d firmware/range-test -e native      # host: the D33 clamp and the R3 dump
pio run  -d firmware/range-test -e heltec      # target build
pio run  -d firmware/range-test -e heltec -t upload
~/.platformio/penv/bin/python tools/rangetest/test_capture.py   # the capture tool
python3 tools/rangetest/check_pa_table.py      # the PA mirror vs. pinned RadioLib
```

**`capture.py` has host tests now, and it needs them.** The defect that destroyed a third
of the 2026-09-05 campaign was in the tool, not the firmware, and nothing in the repo
tested the tool at all. PlatformIO's python, not a bare `python3`.

**Do not hardcode `upload_port` / `monitor_port`** (R1). The Kubuntu field machine and the
macOS build machine enumerate the CP2102 differently, and either board may end up on
either machine. Override on the command line; on macOS always `/dev/cu.*`, never
`/dev/tty.*`.

**The clamp is host-tested on purpose.** It is the one thing here that is wrong in a way
that puts illegal power on the air, and it is pure arithmetic. A bench with a spectrum
analyser is the wrong place to discover a rounding bug.

## Branches

| Branch | Tasks | Gate |
|---|---|---|
| `range/skeleton` | R1–R3 | Both boards flash, radio inits, link at one test point |
| `range/sweep` | R4–R7 | A full automated sweep runs and emits CSV |
| `range/survey` | R8 | Ambient scan produces a trace at both locations |
| `range/w9` | R9 | 222-byte and fragmented `PING` pass over RF — **passed 2026-09-05** |
| `range/r11-survey-hold` | R11 | A campaign walked with the hold state; peaks site-attributable |

R10 is fieldwork, not a branch.

**Field procedure:** [`docs/rangetest/FIELD-PROCEDURE.md`](../../docs/rangetest/FIELD-PROCEDURE.md)
- setup, the position cycle, and the survey campaign.

**Pass 1 is complete** as of 2026-09-05: R1–R11 built, M20 captured and analysed, W9
passed on the bench. There is no build work queued here.

**D1 closed 2026-09-10, and D33 closed with it:** 917.4 MHz, SF9, BW 125 kHz, CR 4/5,
−4 dBm conducted with the fitted 3.0 dBi antenna, under §15.249 Envelope A. Protocol Spec
§12.1 states them; Decision Register §3.4 records why. The W9 backoff finding this file used
to hold open is answered — **§12.3's `backoff_max_ms` default is now 1500**, above SF9's
1107 ms full-frame airtime.

**This firmware still transmits on the provisional 915.0 MHz** (`kProvisionalFreqHz`, R8's
survey starting point), which is `weather-island`'s own peak at −80 dBm. **It is deliberately
not the D1 channel**: this is a bench instrument, nothing here is deployed, and no task is
blocked on changing it. But **a re-run on the old channel produces data that will be
distrusted later**, so move it before capturing anything meant to be compared against a
deployed link — and expect the change to reach `test_phy_params`, `test_csv` and
`test_survey`, which pin 915000000 by value.
