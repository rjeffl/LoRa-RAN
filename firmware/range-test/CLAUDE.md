# `range-test` — bench instrument, pass 1

**Subordinate to `/CLAUDE.md`.** Everything there applies. This file adds only what is
specific to this target.

**Primary document:** `docs/rangetest/LRAN-Range-Test-Firmware-Pass1-Tasks.md`.
**Binding protocol:** `docs/shared/LRAN-Protocol-Specification` **v0.7** (`ver = 2`).
**Record:** `docs/rangetest/engineering-log.md`.

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

## Three modes, and the survey never transmits

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

**M21 is open** — the modules' own FCC grant conditions. Until it closes, the 2.0 dBi in
`main.cpp` is a nameplate figure, not an audited one.

## Build and test

```bash
pio test -d firmware/range-test -e native      # host: the D33 clamp and the R3 dump
pio run  -d firmware/range-test -e heltec      # target build
pio run  -d firmware/range-test -e heltec -t upload
```

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
| `range/w9` | R9 | 222-byte and fragmented `PING` pass over RF |

R10 is fieldwork, not a branch.

**Field procedure:** [`docs/rangetest/FIELD-PROCEDURE.md`](../../docs/rangetest/FIELD-PROCEDURE.md)
- setup, the position cycle, and the survey campaign.

**Do not close D1 from range data alone.** The frequency needs R8's survey (**M20**); the
power needs the grant conditions (**M21**).
