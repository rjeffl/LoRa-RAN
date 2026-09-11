# LRAN range test firmware — pass 1 tasks (two Heltec V3 boards)

**For:** Claude Code, working in a new `firmware/range-test/`
**Binding specification:** [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) **v0.11**
**Decision status:** [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md)
**Depends on:** `/lib/lran-protocol/` — **satisfied.** P1–P7 are complete against v0.6
(107 host tests, 110 on target, 72 W4 vectors, zero divergence); W9 runs against it
**Closes / advances:** **D1** (bounded by D33 — see register §2.1), **M6**, **M20**,
**W9**. Driver is **RadioLib** per **D32**, version pinned in `platformio.ini`
**Hardware, pass 1:** two Heltec WiFi LoRa 32 V3 boards, matched antennas
**Pass 2, later:** the XIAO ESP32S3 + Wio-SX1262 configuration. Not in scope here, but
task R2 exists to make pass 2 a config addition rather than a refactor

---

## 0. Read this first

This firmware answers **D1** — channel, SF, BW, CR and TX power — which is the last
thing blocking node firmware. It also hosts **W9**, because it already needs two boards,
an antenna and a link, and building a second bench tool for that would be waste.

### Read these, and only these sections

| Document | Sections |
|---|---|
| `LRAN-Protocol-Specification` v0.9 | §12 (radio config, injected pin map, CAD/backoff — **including §12.3's v0.8 backoff measurement and v0.9's survey result**), §15 (airtime), §6.6 (`PING`, `PATTERN_FILL`, `frag_chunk`), §18 (**W5 and W9 closed**, W7). **§18.2 is the authoritative Part 15 section as of v0.9**; §18.1 is annotated rather than rewritten and must not be read on its own |
| Decision register | **D32** (RadioLib), **D33** (fixed channel at 15.249), **D1** as amended (§2.1). **D34 does not apply here** — `CommandGate` binds firmware that accepts a `COMMAND`, and this one echoes unauthenticated `PING` |
| [`LRAN-Bridge_Node-Implementation-Plan`](../bridge/LRAN-Bridge_Node-Implementation-Plan.md) v0.7 | Repo layout and board-count guidance |
| `gatelink-expansion-board.md` rev 0.3 | The Wio-SX1262 net assignment — for R2's second board config, not for pass 1 wiring |
| `/lib/lran-protocol/` plan + engineering log | The API this consumes; the P7 entry for what the target build already does |

The GateLink and Bridge **PRDs** are not needed. This is bench work against the radio and
the protocol, not against node requirements.

Leverage wattcycle-reader code elements as needed for access to heltec OLED, etc.
### Guardrails

1. **This firmware never ships and is not the seed of bridge firmware.** Its own
   PlatformIO project, its own directory, and a `CLAUDE.md` in it saying so. Bridge and
   simnode remain empty shells; do not fill them from here. That said, coding should be done in a modular fashion so that applicable portions could be leveraged, referenced, or reused for subsequent firmware tasks without reinventing. 
2. **No WiFi, no MQTT, no `secrets.h`, no Home Assistant.** If a task seems to need any
   of them, it is the wrong task.
3. **TX power is clamped at the D33 ceiling in code**, not by operator discipline. The
   sweep starts at the bottom of the SX1262's range and climbs only on failure. A
   working point chosen at an unusable power is a result you throw away.
4. **This binary deliberately violates §12.1's "PHY parameters are not
   runtime-configurable" rule.** That is the whole point of a sweep, and it is confined
   here. Nothing that reads a PHY parameter at runtime may migrate into node firmware.
5. Pin RadioLib to an exact version (D32). Four firmwares will share this driver.
6. Standing: no changes to `/lib/lran-protocol/`. If the range test needs something the
   library does not expose, report it rather than reaching in.
7. Stop and ask questions if directions are ambiguous, cause conflicts, or do not anticipate any requirements to complete the tasks. Suggest changes to plan where appropriate.

### Branch plan

| Branch | Tasks | Gate |
|---|---|---|
| `range/skeleton` | R1–R3 | Both boards flash, radio inits, link established at one test point |
| `range/sweep` | R4–R7 | A full automated sweep runs and emits CSV |
| `range/survey` | R8 | Ambient scan produces a trace at both locations |
| `range/w9` | R9 | 222-byte and fragmented `PING` pass over RF |

R10 is fieldwork, not a branch.

---

## Branch 1 — `range/skeleton`

### R1 — project and role model

Create `firmware/range-test/` as a standalone PlatformIO project with its own
`platformio.ini` and `CLAUDE.md`.

**One binary, two roles, selected by holding the PRG button at boot.** Role shows on the
OLED at startup and is not persisted — a power cycle re-asks.

- **INITIATOR** — the fixed end. Tethered to the field laptop, drives the sweep, owns
  the CSV. Sits at the house or the gate and does not move. Echo working status and live link quality on the OLED.
- **RESPONDER** — the walking end. Untethered, battery or power bank, echoes probes and
  shows live link quality on the OLED.

**Why this split rather than the reverse:** the initiator has to be scriptable and has to
log, which means it needs the laptop; the walking unit only needs to echo and display.
Making the *walking* unit the responder is what allows one person to run the test.

**Do not hardcode `upload_port` or `monitor_port`.** The Kubuntu field machine (old intel mac) and the macos build machine (M5 mac) enumerate the CP2102 differently. Though the initial plan is to use the M5 mac tethered to the initiator and to use resonder untethered, we may want the option of connecting the field mac to the respondor if the need arises.

### R2 — SX1262 bring-up via RadioLib, with the pin map injected

Per §12.2 the pin map, TCXO voltage and RF-switch mode are supplied by configuration at
construction, never compiled in. Pass 1 has one board type, which makes the seam feel
pointless — build it anyway. It is the reason pass 2 is a config addition instead of a
rewrite, and four firmwares are going to depend on it.

**Do:** define a `BoardRadioConfig` struct — SPI pins, NSS, DIO1, RST, BUSY, TCXO
voltage, RF-switch mode — and one named instance for the Heltec V3. Leave a second,
clearly-marked slot for the Wio-SX1262 carrier, populated from
`gatelink-expansion-board.md` in pass 2.

**Take the Heltec V3 pin numbers from the board schematic or the vendor board
definition, not from memory or from a forum post.** Two settings on this board fail
*silently* — the radio initialises, reports success, and transmits nothing:

- The TCXO reference voltage. The V3 uses a TCXO, not a crystal, and RadioLib must be
  told its voltage or the oscillator never starts.
- DIO2 as RF switch. The V3 switches its RF path from DIO2; without
  `setDio2AsRfSwitch(true)` the PA is never connected to the antenna.

Get these into the board config on the first commit. Debugging "the radio says it sent
it and nothing arrives" from scratch costs an afternoon.

**Acceptance:** both boards initialise, and a single hardcoded packet crosses the bench
with plausible RSSI at 1 m.

### R3 — §12.1 fixed settings

Explicit header, CRC enabled, sync word `0x1424`, BW 125 kHz as the starting point.
These come from §12.1 and are not swept.

Swept parameters — frequency, SF, CR, TX power, payload size — live in the test-point
structure from R4.

**Acceptance:** a settings dump on the serial console at boot, so a CSV can be correlated
with the configuration that produced it.

---

## Branch 2 — `range/sweep`

### R4 — the test point, and what the sweep measures

A test point is `(frequency, SF, CR, TX power, payload size)`. For each, the initiator
sends N probes and records per-probe RSSI, SNR and whether an echo returned.

**The primary metric is round-trip PER**, deliberately. A command that gets no
`COMMAND_ACK` has failed regardless of which direction dropped it, so round-trip is what
the system actually cares about. It conflates the two directions; the responder's own
local log (R6) disambiguates them after the fact.

**Use raw RadioLib frames for the sweep, not LRAN `PING`.** The sweep needs a bench
header carrying a position ID, a test-point index and the responder's measured RSSI —
none of which fit `PING`, whose responder echoes the payload unchanged per §6.6. The
sweep measures the *radio link*; W9 (R9) measures the *protocol*. Keeping them separate
avoids inventing a schema for a bench tool and keeps §6.6 clean.

**Power clamp:** the TX power table starts at the SX1262 minimum and rises to the D33
ceiling and no further. Record **conducted power and antenna gain as separate columns** —
the 15.249 limit is EIRP, and a log that records only "power = X" cannot be audited
later.

### R5 — position marking from the walking end

The responder's PRG button marks a new position: press increments a position ID, which
rides in the echo and lands in the initiator's CSV.

This is what makes single-operator testing work. You walk out, stop, press once, wait for
the sweep to cycle, walk on. The log labels itself and nobody has to correlate timestamps
with a paper notebook afterwards.

**Acceptance:** position ID appears correctly in the CSV, increments once per press, and
survives the walk out and back.

### R6 — display and logging

**Responder:** OLED shows live RX RSSI, SNR, current position ID and echo count, in text
large enough to read outdoors at arm's length in sunlight. It also keeps a local running
summary of what *it* received, which is the reverse-direction data R4 gave up.

**Initiator:** CSV to serial — one row per test point per position, with position ID,
frequency, SF, CR, conducted power, antenna gain, payload size, probes sent, echoes
received, PER, and mean/min/max RSSI and SNR. OLED echos similar data as responder to provide visual feedback that devices are communicating.

The responder has no SD card. If retaining its local log matters, a small NVS ring of
per-position summaries dumped over serial on reconnect is enough — do not build anything
larger.

### R7 — CSV lands in the repo

Sweep output goes to a versioned directory, not to a scratch file. These traces are the
evidence for D1 and the input to W7's airtime regeneration, and a channel choice that
cannot be justified from a committed measurement will get reopened.

---

## Branch 3 — `range/survey`

### R8 — ambient RSSI sweep

Required by §12.1 before D1 may fix a frequency. Add it as a third mode on the initiator
binary.

- Scan **902–928 MHz in 200 kHz steps** — 130 bins. The step matches the LoRaWAN channel
  grid, so a LoRaWAN-shaped occupancy pattern is recognisable on sight, which settles a
  question about the site's existing equipment that no datasheet has answered.
- Record **peak and mean RSSI per bin**, over repeated passes lasting several minutes,
  not one fast sweep. The site's known occupants are temperature sensors on a slow
  reporting cadence; a sweep spending a second per bin will see nothing and conclude the
  band is empty.
- Run at **both** the bridge location and the most distant node location, and keep both
  traces. §12.1 requires both because they will not look alike, and it is the node's
  floor that sets its margin.
- CSV out, same directory as R7.

**Acceptance:** two committed traces, and a chosen frequency justified against them.

---

## Branch 4 — `range/w9`

### R9 — the W9 bench runs

Now against the real codec, using `/lib/lran-protocol/` and real `PING` frames.

- **222-byte `PING`** (§6.6.1) — the maximum frame over real RF. §15.1 puts it over a
  second of channel occupancy at SF9; check that against the CAD/backoff window while the
  boards are out, since §12.3's defaults were chosen against an empty channel.
- **Fragmented `PING`** (§6.6.2) — `frag_chunk = 14` on a 202-byte echo gives the full
  15-fragment set. `PATTERN_FILL` localises any reassembly fault to a byte offset.
- **Late fragments, if the link produces them.** v0.5's §11.2 late-fragment rule was
  chosen against a hypothesised RF echo. Whether echoes actually occur on this link is
  worth knowing before the rule is relied on at the gate — and a clean "we saw none" is
  a useful result too.

**Acceptance:** both runs pass over RF, and the engineering log gets an entry. If either
fails, that is a protocol finding and goes through the same reporting route as the P6
findings did.

---

## R10 — fieldwork notes (not code)

- **Matched antennas, recorded.** Same antennas at both ends, same height, both noted in
  the log with their gain. Use the antenna supplied with the module — D33's conditions
  turn on antenna gain, and a swapped antenna invalidates the power figure.
- **Height matters more than you expect** at 915 MHz over 150 m of ground. Record it
  **per end and per position**, not as one figure for the walk — received power scales
  with the *product* of the two antenna heights. Two runs at different heights are worth
  more than one careful run at an unrecorded one.
- **Record indoor/outdoor and wall penetrations at each end.** Added 2026-09-05 after the
  first walk omitted it. The initiator sat indoors at the bridge's target location, so
  every reading in that trace bundles at least one framed wall — and two positions at the
  same 40 m range differ by **18.2 dB** depending on which face of the house the path
  leaves by. A note carrying bearing to a degree and height to a centimetre, which does
  not say the radio was indoors, describes the wrong experiment.
- **Say which question the walk is answering.** A deployment measurement puts the radios
  where the nodes will actually live, walls included, and answers M6. A propagation
  measurement needs clear paths, distances to ~1 m and per-position heights. **The first
  walk was the former**, and its numbers cannot be extrapolated to another node location
  by distance alone.
- **Shade the OLED with a hand.** Confirmed outdoors 2026-08-31: the display does not
  power through direct sunlight, and is comfortably readable with minimal shading. This
  is procedure, not a defect — a 128×64 monochrome OLED at maximum contrast is
  outmatched by direct sun and no layout change fixes it. The measurement never depends
  on it: the CSV over serial is the record, and the responder's NVS log covers the
  untethered end.
- **Watch the neighbours.** A full-size `PING` at SF9 holds the channel for over a second
  and R9 repeats it. If a YoLink sensor stops reporting during a run, that is worth
  knowing before concluding the link is fine.
- **Do not close D1 from this data alone.** The frequency needs R8's survey; the power
  needs the module's FCC grant conditions per D33.

---

### R11 — the survey hold state

**Added 2026-09-05, after the M20 campaign was run and found to be measuring the walk.**

Before R11 the survey scan never stopped. Storing a site reset the accumulator and
resumed sampling immediately, so everything the radio heard while the operator walked to
the next site was folded into that site's run — and because `peak_dbm10` is a **peak
hold**, one burst heard in transit was attributed permanently to a site the operator was
only walking towards.

**No press pattern avoids this.** Pressing on arrival rather than on departure only moves
the contamination to the site just left; the accumulator is running either way. It needs
a phase in which the radio is not accumulating.

- **`SurveyCampaign` owns the cursor and the phase** (`Held` / `Running`), in `survey.h`,
  Arduino-free and host-tested like everything else there. It does **not** own NVS: a
  store can fail by short write, and a cursor that advanced over a site that was not
  written is a site silently lost, so the caller performs the store and reports the
  outcome back through `note_stored()`.
- **Two presses per site.** Arrive, press to start the dwell; when it is done, press to
  store and advance, which returns to `Held`. The walk happens in `Held`.
- **The accumulator is cleared when the dwell STARTS**, not when the previous site was
  stored, so a long hold accumulates nothing.
- **Boot and power cycle come up `Held`.** The operator is not standing at the site when
  the board boots, and a power cycle happens between sites with the board in a bag.
- **The OLED shows an inverted `HELD` bar**, for the same reason `show_sweep_done` has
  one: the difference between "walking, not measuring" and "measuring" has to survive a
  glance at a hand-shaded panel in sunlight.
- **`hold_discipline` in the NVS blob, reported in the per-site preamble.** A reader
  cannot tell a clean run from a transit-contaminated one from the numbers, so the run
  carries it. **In the blob, not the dump path** — the firmware reading NVS is not the
  firmware that collected the data, and a constant in the dump path made a re-dump of the
  pre-R11 campaign claim a discipline it never had. Blob version 2; **v1 blobs are still
  read** and report `0`, because rejecting them to add one bit would have destroyed the
  only copy of the campaign that motivated it.

**Acceptance:** a campaign walked with the hold state produces a trace whose peaks are
site-attributable, and `docs/rangetest/data/README.md` drops the caveat for traces
carrying `hold_discipline=1`. Host tests cover every transition, including the failed
store and the last site.

**Also in this task, because the same afternoon proved it was missing:** host tests for
`capture.py` (`tools/rangetest/test_capture.py`). The defect that destroyed a third of
the 2026-09-05 campaign was in the tool, and **no test in the repo covered the tool at
all** — every test covered the firmware. The regression under test is blunt: the boot
window must read the port.

---

## Pass 2 — XIAO configuration — **DONE 2026-09-05**

Built and measured. See
[`LRAN-Range-Test-Firmware-Pass2-Tasks.md`](./LRAN-Range-Test-Firmware-Pass2-Tasks.md);
this section is kept for what it predicted.

It said the only thing pass 1 owed pass 2 was R2's seam — *"a second `BoardRadioConfig`
populated from `gatelink-expansion-board.md`, and nothing else changing. If pass 2 turns
out to need more than that, R2 was built wrong, and it is worth saying so in the log."*

**Pass 2 needed more, and R2 was not built wrong.** The excess was in three places, none
of them the radio pin map: a `rf_sw` field R2 defined that nothing ever read, the display
and button pins (R2 scoped a *radio* seam correctly — one board cannot reveal a *board*
seam), and the antenna as a D33 clamp input. Said out loud, as instructed, in the
engineering log for that date.

**The prediction below was exactly right**, and was the first thing pass 2 had to fix:

> the Wio-SX1262 carrier drives its RF switch from a dedicated `LORA_RFSW` GPIO rather
> than from DIO2, which is exactly the divergence §12.2 anticipates and the first real
> test of the seam.

It also needs both mechanisms, not one instead of the other. Confirmed over the air:
192 probes out, 192 echoes back.

**One thing it got wrong**, worth recording because it propagated: the second config was
to be *"populated from `gatelink-expansion-board.md`"*. That document describes the
**header board** (p-6379), and the board that arrived is the **Kit** (p-5982), whose
control lines cross a B2B connector on entirely different GPIO. Copying the carrier's map
would have produced a board that looked configured and did not work.

---

## Record

Findings, measurements and surprises go in
[`/docs/rangetest/engineering-log.md`](./engineering-log.md) as they happen, dated. The
sweep CSV (R7) and the survey output (R8) are committed alongside it — a range figure
with no record of antenna height, bearing and TX power is not a result anyone can reuse.
