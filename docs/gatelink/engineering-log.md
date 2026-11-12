# GateLink Node — engineering log

**Dated record, appended to and never rewritten.** Measurements, surprises and the
things that cost an hour. Where this disagrees with a document, the document is the
current statement and this is what was true on the day. Implementation Plan §5.3 names
this file.

**Earlier entries are in files beside this one**, moved there unedited. A document citing
an engineering-log entry by date means the file holding that date:

- 2026-10-01 to 2026-10-02, moved 2026-10-15:
  [`engineering-log-2026-10-01_2026-10-02.md`](./engineering-log-2026-10-01_2026-10-02.md)
- 2026-10-05 to 2026-10-15, moved 2026-11-01:
  [`engineering-log-2026-10-05_2026-10-15.md`](./engineering-log-2026-10-05_2026-10-15.md)

[`docs/README.md`](../README.md#conventions) says when the log is split next.

---

## 2026-10-24 — The replacement StamPLC's IN5–IN8 do not read either

**A second StamPLC fails port 1 the same way, so D77's premise is no longer one bad
unit.** The 2026-10-11 entry set the falsifying observation as "IN5 or IN6 reading 1 while
energized with `EXCOM_COM` connected, **on this board**". That test now has a second board,
and the reading did not change.

The two units are physically different: the replacement reads MAC `ac:a7:04:03:52:0c`
against `50:78:7d:cd:c9:94` on the unit D77 was decided on.

| State | `aw` input registers | Reads |
|---|---|---|
| Nothing driven | `00 0f` | IN1–IN8 de-energized |
| IN3–IN6 driven together | `c0 0f` | IN3, IN4 energized; IN5–IN8 still 0 |

Port 0 bits 6 and 7 are IN3 and IN4, so the new input board's two stages on those channels
work. `M5StamPLC.cpp`'s `_in_pin_list = {4, 5, 6, 7, 12, 13, 14, 15}` maps IN5–IN8 to port
1's upper nibble. That nibble stayed 0 while the same board drove all four channels from
the same supply and the same common.

**The drive is proven good by the channels that do read.** The operator measured 13.6 V at
each MOSFET gate and 0 V from drain to ground, with `EXCON_COM` at +12 V, so each
energized input sees the full 12 V across the opto. IN3 and IN4 register that; IN5 and
IN6, driven identically on the same board, do not.

**The expander configuration is right, and there is no second input path.** `dir f0 ff`
makes P0_4–P0_7 and all of port 1 inputs, `ledmode ff ff` puts both ports in GPIO mode,
and the ID reads the AW9523's `0x23`. Expander A at `0x43` drives only the status light
and the backlight (`io_expander_a_init()`), so no input reaches the MCU through it.

**The register read is not an addressing artifact.** `board_read_inputs()` takes the two
input registers in one 2-byte read from `0x00`, so `Input_Port1` at `0x01` arrives only if
the part auto-increments. It does: the bytes differ, `00` then `0f`, where a part that did
not would have returned `00 00` idle and `c0 c0` energized. Port 1's `0x0f` is coherent on
its own terms. P1_0–P1_3 are unconnected here and float high, and P1_4–P1_7 sit on the
opto emitters with R30 pulling them down, so a working register reads `0x0f` with those
four optos off. The expander at `0x59` also answers with the AW9523's ID `0x23` and holds
the relay directions this firmware drives, and one AW9523 carries all 16 lines the board
needs, so the inputs have no second expander to sit on.

**One hypothesis is left, and neither board has tested it:** whether IN5–IN8 share
`EXCON_COM` with IN1–IN4. The 2026-10-11 test opened that terminal and dropped IN1–IN4
together, which establishes the common for those four only.
[`StamPLC-input-conditioning`](./StamPLC-input-conditioning.md) calls it "almost certainly
shared", an assumption rather than a measurement. A separate, unwired common for IN5–IN8
would leave those optos with no return path and read 0 at any drive voltage, which is what
both boards show.

**The operator's readings do not yet prove current reaches the opto.** 13.6 V at the gate
and 0 V from drain to ground say the MOSFET is on. An open conductor between that drain
and the `EXCOM_IN5` terminal gives the same two readings, because a MOSFET on with no load
also sits at 0 V. Measuring at the StamPLC's own terminal separates the two: about 12 V
from `EXCOM_IN5` to `EXCON_COM` means current is flowing through the opto, and roughly 0 V
across them means the terminal is still sitting at the common and nothing is driving it.

**Falsified by:** about 6.2 kΩ between `EXCOM_IN5` and `EXCON_COM`, R14 across the input
terminals as [`StamPLC-el3h4-input-analysis`](./StamPLC-el3h4-input-analysis.md) §1
measures it on IN1. An open circuit there says the common is elsewhere, and a wire
recovers the inputs. About 6.2 kΩ with the register still reading 0 puts the fault in the
opto outputs PC5–PC8, their `SYS_IN5`–`SYS_IN8` traces, or the expander's P1_4–P1_7, on
two units.

**M5's own documentation weakens the hypothesis without closing it.** The StamPLC page
lists one common terminal, `EXCOM_COM`, for all eight inputs, and names the input expander
`AW9523B (0x59)`, the address this firmware reads. It carries no pinout row pairing each
input with a pin, and it never states that the eight inputs share the one common.

**The IO schematic could not be read here.** `K141_sch_StamPLC_V10_IO.pdf` carries no
usable text layer, and the host has no `pdftotext` or `pdftoppm`.

**No published report matches the symptom.** A search of M5Stack's documentation, its
community forum and the `m5stack/M5StamPLC` issue tracker found nothing describing IN5–IN8
reading 0 while IN1–IN4 read. The two issues that touch the inputs both describe every
input failing: issue 15 is about AW9523 interrupts and was answered with an example, and
issue 17, "Cannot read inputs !!!", was resolved by a jumper between `-` and `COM`. That
resolution does not apply here, because IN1–IN4 read through the same common. The absence
of a report is weak evidence either way, since few builds use more than four inputs.

### When IN5 and IN6 return, IN6 does not invert

The operator states the 1050's alarm output is a 12 V output that goes active on an alarm
condition, so IN6 reads asserted when the alarm is active and its stage does not invert.
Whether that output has a pull-down is not known, and one may have to be supplied.
`kInputsActiveLow` therefore cannot widen to `0x3C`: IN3, IN4 and IN5 invert and IN6 does
not, on the signals as described.

**The edge-triggered events are not built.** `FIRE_ASSERTED` and `HARD_SHUTDOWN` are sent
only by the boot re-send of R-3.5i (`task_runtime.cpp`, `send_boot_alarm`). PRD G-9 and
G-10 need a path that sends them on an edge, which no masking hid because it was never
written. `gate_state.cpp` already consumes both bits for the movement cause, the hold
source and FAULT.

### What the new hardware did pass

Everything below ran on the firmware at `969cdf7`, flashed before this session.

- **VE.Direct works through the changed J4 connector, in both directions.** 62 text blocks
  with 0 bad, 0 unsynced and 0 unparsed; 10 HEX transactions sent and answered with 0
  retries and 0 timeouts; 129 async frames, 0 bad. `batt 13340 mV -10 mA, pv 1534 cV 0 W`,
  `cs 3 err 0 mppt 2`. The MPPT's battery voltage and the INA226's both read 13340 mV.
- **The pack reads.** One window in 1622 ms (`init 42 scan 171 conn 1137 hs 91 ans 169
  down 12`). Cells 3342, 3344, 3345, 3344 mV, delta 3; temps 18.6, 20.9, 16.8, 17.1 °C;
  pack 13370 mV 0 mA; SOC 99 %; 99.9 of 100.0 Ah; 4 cycles; SOH 100.0 %; RSSI −62. The
  interlock logged 0 tx waits, 0 aborts and 0 overruns.
- **All four relays pulse.** K1–K4 each held 299.9 ms against a 300 ms request, with 0 I²C
  failures and the boot relay-off check passing.
- **The radio is clean.** RST check passed with BUSY falling in 1620 µs; rx 13, tx 18, 0
  errors, 0 timeouts, 0 forced, 0 CAD errors.
- **The card mounts and the store persists.** 3 rows restored at boot, 0 refused, 0
  unknown, 0 failed writes, 0 failed mounts.
- **The command path runs end to end.** `lran/gatelink/cmd/open/set` returned
  `{"outcome":"acked","seq":2,"attempts":1,"result":0,"detail":0}` and pulsed K3 for
  499.9 ms.
- **The buzzer sounds.** The operator heard `beep`, which still logs `LEDC is not
  initialized` first. That error is cosmetic.

### The split readback has now been seen on air

**One `get_all` returned 34 results in two `CONFIG_ACK` frames**, which the handoff had
recorded as never observed. The bridge logged `FRAME #41 tx` for the request, then `FRAME
#42 rx` and `#43 rx` for the replies, both `type=11 schema=18`. GateLink logged `config 01
<- 00 seq 1: op 3, 0 entries, 34 result(s)`. The outcome
was 1. Both replies arrived at RSSI −39 and SNR 11.

### Two smaller things

**The charge-inhibition rows are back at their default values.** The readback shows
`charge_inhibit_temp_c` 0, `charge_inhibit_band_ma` 100 and `charge_inhibit_rise_mv` 20,
which are the defaults, each still carrying `source: override`. The 2026-10-15 test values
of −5, 250 and 35 are gone. The three rows are the 3 that boot restores.

**The RTC reads `2049-12-01 01:18:15`.** It reports itself set. Nothing read from it is on
the wire yet.

### The shared common is measured, and everything outside the StamPLC is eliminated

**All eight inputs read about 6.1 kΩ to `EXCON_COM`** (operator, 2026-10-24, on the
original unit). That is R14 across each input's terminals, so the eight inputs do share
the one common and IN5–IN8's input networks are intact. The separate-common hypothesis
above is falsified, by the test this entry named for it.

The operator also reports the input board bench-tested on all four channels before it was
installed, and the IN5 and IN6 connections buzzed good after it. The direct drive of 2026-10-11 had
already run at 13.4 V, 15.11 V and 25.25 V. Nothing outside the StamPLC remains: the board
works, the harness is continuous, the common is shared and R14 is in place.

**Port 1 is not dead as a port.** P1_0–P1_3 are unconnected on this board and read `0x0f`,
which is their real floating state, so the register path and port 1 both work. Only
P1_4–P1_7 are in question.

**The next test needs no meter and does not need the case opened.** Driving P1_4–P1_7 as
outputs and reading the input register back separates the two remaining candidates. A
readback that follows the drive confines the fault to the opto output stages PC5–PC8. A
readback that stays 0 puts it at the expander's own P1_4–P1_7. The expander pin reaches
the opto emitter through R38, 1 kΩ, with R30's 5.1 kΩ to ground, so driving 3.3 V costs
about 0.54 mA.

### Superseded the same day: IN5 and IN6 read, and the fault was in their connections

**This subsection overturns the conclusion above.** Everything earlier in this entry
stands as measured, but "a second StamPLC's IN5–IN8 do not read" is wrong for this unit.
IN5 and IN6 read, and they track the bench supply.

The operator buzzed the IN5 and IN6 connections, reporting both good, and the firmware was
reflashed with the hard reset that upload ends on. The next `aw` read `c0 3f`, where every
earlier sample this session had read `c0 0f`. Port 1 bits 4 and 5 are IN5 and IN6.

A toggle of all four input-board channels then tracked cleanly, 26 samples at 3 s:

| Samples | `aw` input registers | State |
|---|---|---|
| 11 | `c0 3f` | IN3–IN6 energized |
| 8 | `00 0f` | all four off |
| 7 | `c0 3f` | IN3–IN6 energized again |

**The expander was never at fault, and the new `aw drive` command proves it separately.**
Driving P1_4–P1_7 as outputs and reading the input register back gave `ff` with those bits
`f0` when driven 1, and `0f` with those bits `00` when driven 0, three times running. The
pins, their traces to R38 and the register path all work.

**What changed is not established, and that matters more than it looks.** Buzzing a
connection does not energize it, so either a terminal was loose and reseating it during the
check fixed it, or a conductor was not landed where the buzz suggested. The reading moved
across a reflash and a hard reset as well, so a boot-time cause cannot be ruled out from
here. **A node 87 m away with no OTA should not deploy on a connection that started
working during a continuity check:** re-land both conductors deliberately and confirm the
toggle again before the soak.

**The original unit's 2026-10-11 result is not overturned by this.** That board was driven
directly at 13.4 V, 15.11 V and 25.25 V through different wiring, and its reading is its
own record. This entry says only that the unit now in service reads all six inputs.

### What restoring IN5 and IN6 now needs

D77 chose to deploy on four inputs because the then-deploying StamPLC read four. The unit
in service reads six, so that premise is gone and the decision needs the operator. The work
behind it, none of it done:

- **The firmware mask.** `kInputsWired` goes `0x0F` to `0x3F`. `kInputsActiveLow` goes
  `0x0C` to `0x1C`, not `0x3C`: IN3, IN4 and IN5 invert and IN6 does not, because the
  1050's alarm output drives 12 V on an alarm.
- **G-9 and G-10 have no edge-triggered path.** `FIRE_ASSERTED` and `HARD_SHUTDOWN` are
  sent only by R-3.5i's boot re-send. `gate_state.cpp` already consumes both bits for the
  movement cause, the hold source and FAULT.
- **R-3.5l and D80's `node_flags` bit 7** describe inputs nothing senses, which stops being
  true.
- **M3 applies again**, and D72 with it.
- **The bridge's withdrawn entities.** *FIRE input* and *Hard-shutdown latch* come back,
  which the bridge handoff owns.
- **Plan §3.2, PRD G-9, G-10, §3.2.5, R-3.5i, R-3.5l and the §7 entity table** all describe
  the four-input deploy.
- **Whether the 1050's alarm output carries a pull-down** is unknown, and one may be needed.

### The cause: P1_4–P1_7 do not sample until P1's direction register is written

**Both subsections above are superseded, and D77 rests on a firmware defect rather than
dead silicon.** The operator asked whether the port had to be toggled from output to input
mode before it would read. It does, and that is the whole of it.

**The register already reads back correct while the pins are inert.** `dir f0 ff` says
P1_4–P1_7 are inputs in every dump this session, in the states where IN5–IN8 read and in
the states where they do not. That is why every configuration check made here looked right
and proved nothing.

A write to the register is what the pins need, and `aw redir` writes the value the register
already holds, with no output phase:

```text
aw: input c0 0f                                     IN5, IN6 dead after a fresh boot
aw redir: P1_4-P1_7 set input again, input reg 3f, those bits 30 - IN5-IN8 read
aw: input c0 3f                                     and they stay read
```

`aw drive 1` does it too, through its restore, which is why the window after the earlier
reflash worked: that boot happened to leave the register written, and the reading held
until a power cycle took it away. **The operator's re-landing of the IN5 and IN6
conductors was not the cause of anything**, and neither was a marginal EL3H4 at 12 V: the
behaviour is digital, all four channels at once, and it switches on one I2C write.

**The fix is one write, and where it goes matters.** In `board_begin()`, straight after
`M5StamPLC.begin()`, it did not hold: the next boot read `raw 0x0c` again, so something
between that call and the first pass leaves the register's value intact while the pins stay
inert. From `io_task`'s first pass it holds. With that write in place, a fresh boot reads:

```text
boot: first input read raw 0x3c debounced 0x00
aw: input c0 3f
in: raw 0x3C debounced 0x00 edges 4/0 relays 0x00 i2c failures 0
```

`raw 0x3c` is IN3, IN4, IN5 and IN6 energized at the first read, against `raw 0x0c` before
the fix. The write costs no I2C failures. `debounced 0x00` is still right, because
`inputs_from_levels()` inverts IN3 and IN4 and masks IN5–IN8 under D77, which has not
changed.

**What this does not establish.** The original unit was never tested after a direction
write, so whether its IN5–IN8 also read is unknown and worth one bench hour if that board
is still to hand. The mechanism behind the lost write is not established either: M5GFX
pulses GPIO 3 inside `M5.begin()` and M5StamPLC's `io_expander_b_init()` calls
`configureDirection(0x0)`, which sets every pin to output despite its `// all inputs!`
comment, before `pinMode` puts the input pins back. Which of those leaves the pins inert
while the register reads correct is not pinned down. The write is a fix, not an explanation.

**IN5 and IN6 are still masked in firmware.** `kInputsWired` is unchanged, so `input_bits`
and the `in` line still report four inputs and only `aw` shows the other two. Restoring
them is the work the subsection above lists, and D77 is the operator's to settle first.

## 2026-10-24 — The original StamPLC's IN5–IN8 read: that unit was never faulty

**The original StamPLC, MAC `50:78:7d:cd:c9:94` from `esptool read_mac`, reads all six
inputs once flashed with the direction-register fix.** It was flashed with `firmware/gatelink
-e gatelink` at `6ae730d`, which carries `aaf87e8`. It ran on its 12 V supply, with IN3–IN6
energized and `EXCOM_COM` connected, and `bench.py` on the console. This answers the first
item in the subsection above headed *What this does not establish*.

| Boot | IN5, IN6 | Boot line | `in` | `aw` input |
|---|---|---|---|---|
| After the flash | energized | `raw 0x3c` | `raw 0x3C`, edges 4/0 | — |
| `restart` | energized | `raw 0x3c` | `raw 0x3C`, edges 4/0 | `c0 3f` |
| Same boot | drive removed | — | `raw 0x0C`, edges 6/0 | `c0 0f` |
| Same boot | drive restored | — | `raw 0x3C`, edges 8/0 | `c0 3f` |
| After removing 12 V and USB | energized | not captured | `raw 0x3C`, edges 4/0 | `c0 3f` |

No row used `aw redir` or `aw drive`, so `io_task`'s direction write is the only one made.
IN5 and IN6 fall when their drive is removed and rise when it returns, so they follow the
input rather than sticking high. `i2c failures` stayed 0 throughout.

**The power-cycle row booted twice.** The harness crashed on the cold boot (the write-side
`PortNotOpenError`, *Open* in the handoff), so the power-on boot printed to a closed port.
Restarting the harness reset the chip over USB, and that second boot reads `Reset: usb`.
Every boot passes through M5StamPLC's `configureDirection(0x0)`, which the same day's fix
follows. A software-reset boot therefore tests the fix as hard as a power-on boot does,
though the power-on boot's own line was not seen.

**So the 2026-10-11 entry's fault in hardware was this firmware defect**, on this unit as
on the replacement. Both StamPLCs read IN1–IN6, and D77's premise, a StamPLC whose IN5–IN8
do not read, holds for neither board. Settling D77 stays the operator's.

**Falsified by:** this unit reading `aw` input `c0 0f` with IN5 and IN6 energized, after a
boot of an image carrying `aaf87e8`, without an `aw redir`.

## 2026-10-24 — D83: IN5 and IN6 read, and FIRE and HARD_SHUTDOWN go out on the air

**The operator settled D77 as six inputs (D83).** `firmware/gatelink -e gatelink` at
`2afa0d7` flashed to the original StamPLC. The bridge was on its bench port, and `bench.py`
logged both consoles and `lran/#`. IN3–IN6 were energized from the input board.

**R-3.5i passes on the air for the first time.** IN6 energized at boot is an asserted
alarm, so boot read `raw 0x3c debounced 0x20`. GateLink then sent the alarm again after
`BOOT`, and the bridge published it after `event/boot`:

```text
GL   boot: HARD_SHUTDOWN present at boot, sent again
MQTT lran/gatelink/event/boot          {"event_id":1, ... "gate_state":"fault","input_bits":32,"detail":7,"reset_cause":"external" ...}
MQTT lran/gatelink/event/hard_shutdown {"event_id":2, ... "gate_state":"fault","input_bits":32 ...}
```

**Every edge sends what `AlarmWatch` promises.** The operator removed and restored IN5's
drive, then IN6's, about 5 s apart:

| Edge | Console | STATUS reason | Event |
|---|---|---|---|
| IN5 drive removed | `inputs 0x30 - FIRE ASSERTED` | `fire` | `fire_asserted`, id 3 |
| IN5 drive restored | `inputs 0x20 - FIRE clear` | `fire` | none |
| IN6 drive removed | `inputs 0x00` | `hard_shutdown`, `shutdown_latch` false | none |
| IN6 drive restored | `inputs 0x20` | `hard_shutdown`, `shutdown_latch` true | `hard_shutdown`, ids 4 and 5 |

Each STATUS reached MQTT before its event, with `fire_inputs_sensed: true` throughout, so
the bridge shows the three FIRE and alarm entities without any change of its own.
`node_flags` bit 5 followed IN6 as `shutdown_latch`.

**IN6's two events were the restore's contact bounce**, by the operator's account: the
conductor was re-landed by hand. The debounced inputs read `0x20`, `0x00` and `0x20` within
610 ms, longer than the 200 ms that `input_debounce_samples` 2 at `input_poll_ms` 100
spans. The 1050's alarm output drives the input electronically, so this says nothing about
the installed signal.

**What this does not establish.** The polarities in `kInputsActiveLow` (`0x1C`) are the
bench's: IN5 inverts like IN3 and IN4, and IN6 does not, on the operator's description of
the 1050's alarm output. M3 measures both at the gate before they are connected.

**Falsified by:** a rising edge on IN5 or IN6 after boot with no event on
`lran/gatelink/event/fire_asserted` or `event/hard_shutdown`.

## 2026-10-25 — Bench soak, 24 h: GateLink did not reset, and `bench.py` lost its keepalive, then its host

**The soak ran on branch `gl-bench-soak`**, with the setup in the handoff's *The soak's
setup*. `bench.py --dir ~/lran-soak/2026-10-24 run --mqtt` created `bench.log` at 18:30:13
EDT on 2026-10-24. The kernel logged the StamPLC's USB disconnect at 18:32:05, log time
about 112 s, so the power-chain soak dates from then and reached 24 h at 18:32:05 on
2026-10-25. The log's mark at 529.88 s was written at 18:39:03, after the unplug it
records. Log times below are seconds from 18:30:13.

**GateLink did not reset in 24 h.** `uptime_s` in `lran/gatelink/node/state` rose from 71
to 70,931 by the log's last line, staying 95 s ahead of the log clock, so it booted at
about 18:28:38 on 2026-10-24. A live capture at 18:38:50 on 2026-10-25 read 87,011, the same
boot to the second, and `event_frames` stayed at 2, both from the boot. The one apparent
reset in the log, 64,151 to 63,011 at 65,502 s, was a stale retained `node/state` that the
broker restored after its reboot; the backlog that followed it read 64,271 onwards.

**The log covers 19.6 h of the 24, to 70,861 s (14:11:14).** The last 4.3 h are covered
by the bridge's cumulative counters and GateLink's uptime, read in the live capture:

| Watched for | Result |
|---|---|
| `BOOT` after the first | none: `uptime_s` continuous, `event_frames` 2 throughout |
| STATUS rate | `status_frames` 1,186 at 70,861 s, 1,452 at 18:38; one per `poll_interval_s` 60 through the unlogged hours too. `missed_polls` 0 in every `diag/state` |
| Link | RSSI −42 to −59 dBm, SNR 11 dB. `rx_no_interrupt` and `rx_wake_empty` went 0 → 1 near 47,584 s and to 4 by 18:38; not cleared up |
| VE.Direct | never unavailable in 418 `solar/state` documents, nor in the live capture |
| BMS | reachable throughout the log; one missed poll about every 3.7 h, below. The unlogged hours are not covered |
| Overnight ΔSOC | 0: SOC 99 % throughout, pack 13,340 → 13,310 mV over 24 h, `pack_ma` 0 throughout |

**The BMS misses one poll about every 3.7 h, and each miss reaches the bridge's stale
limit.** `age_s` climbed past `bms_poll_s` 300 to 600 four times, peaking at 13,776, 27,036,
53,856 and 66,817 s, and once to 301 at 39,998 s. A fresh reading followed each time. 600 is
the bridge's `bms_stale_s`, so on each of those four the battery came within one publish of
being marked unavailable. Why every 3.7 h is not established. The BLE side's own
counters were not logged.

**`pack_ma` read 0 for the whole soak**, with the StamPLC on the pack, so the board's draw
is presumably below the BMS's current resolution. `charge_fet` and `discharge_fet` read
false and `alarms` 65535 throughout. Whether those are this pack's real states or a
decoding gap is not established. The MPPT's `load_on` went false at 50,736 s (08:35 EDT),
and read true again in the live capture at dusk. It charged in `bulk` from 63,156 s, at
1 W and up to 70 mA, and was `off` again by 18:38.

**The broker went down once in the log, from 64,056 to 65,502 s** (12:17 to 12:41 EDT).
The operator reports more than one reboot; the log shows only that outage. The bridge
buffered publishes through it and flushed on reconnect. `q_publish_high_water` reached 32,
and `q_publish_dropped` went 0 → 233 at 65,521 s, all of it that backlog.

**Since the broker returned, it has closed `bench.py`'s connection every 96 s**: 55 times
by 70,783 s. The cause is in `Mqtt._ping`, which catches only `OSError`. While the broker
was down, `self.sock` was `None`, so `self.sock.sendall` raised `AttributeError` and the
ping thread died; the run's own output kept the traceback. With no PINGREQ, the broker drops
each new connection at 1.5 × the 60 s keepalive. Each cycle loses about 5 s of `lran/#`, or
about 5 %. A reset in one of those gaps would still show as an `uptime_s` drop in the next
`node/state`.

**The log stopped at 14:11 because its host went away, not the bench.** `bench.py` ran as a
background task of a Claude Code session, and that task's output ends `[killed]` at
14:11:19, when the session ended. Nothing in the system journal marks 14:11; the laptop's
Wi-Fi dropped later, from 14:38 to 15:49, and did not affect the log.

**What the restart needs** (the plan: fix, then restart for 12 to 18 h):

- `_ping` must survive a missing socket, so a broker outage cannot end the keepalive.
- `bench.py` must run outside any Claude Code session, under `nohup`, `systemd-run --user`
  or a terminal that stays open.
- `bench.log` should carry wall-clock time, or a wall-clock mark at start. Every time in
  this entry was converted from the log's relative seconds, and the first conversion went
  wrong by 7 min.
- `bench.py` should mark a retained message as retained, so a stale retained value restored
  by the broker cannot read as a reset.
- The BMS poll outcome per window, if GateLink publishes it, so the 3.7 h miss has a cause.

**Falsified by:** an `uptime_s` in `lran/gatelink/node/state` that does not place GateLink's
boot at about 18:28:38 on 2026-10-24, read before its next intentional reset.

## 2026-10-25 — Input board v1.1: R9 holds Q4's gate low when the 1050's alarm output is off

**The 1050's alarm output does not pull down when off.** With the 1050 unpowered, the
operator measured about 3.2 MΩ from its alarm output to ground. The operator takes that to
mean a high-side P-channel FET stage, likely able to source about 500 mA at 12 V, that goes
open when off. On the v1.0 input board, that output reaches Q4's gate only through R4 (1K),
and nothing else connects to the gate. With the alarm off, Q4's gate therefore floats.

**v1.1 adds R9, 10K from Q4's gate to `Gnd`.** The schematic and netlist carry it, and the
operator is fitting it to the existing hand-built perfboard. The values follow from the
2N7000's datasheet limits:

| Condition | Q4 gate | 2N7000 limit |
|---|---|---|
| Alarm on, 12 V | 10.9 V, from R4 and R9 as a divider | V<sub>GS</sub> ±20 V |
| Alarm on, 14.4 V on a charging battery | 13.1 V | V<sub>GS</sub> ±20 V |
| Alarm off | under 0.8 V while the output leaks under 80 µA | V<sub>GS(th)</sub> 0.8 V minimum |

The alarm output sources about 1.1 mA into R4 and R9 when on. A P-FET switched off
typically leaks in the nA to low-µA range, so the off-state margin is wide. A larger R9
would cut that margin and weaken the node against noise on the field wire, for current
that does not need saving.

**The other three input stages, Q1–Q3, get no pulldown.** By the operator's description,
the 1050's other inputs to the board are 5 V lines pulled up inside the 1050 and driven
low when asserted, so the 1050 sets both of their states.

**No gate clamp.** A zener from gate to source would guard against an inductive load's
turn-off kickback on the alarm line. The alarm output has never been used, and GateLink
will be its only load, so the board leaves the clamp out.

**The 2026-10-24 bench run predates R9.** Q4's gate floated each time IN6's drive was
removed in that run, and nothing recorded what discharged it.

**Not yet measured:** J3 pin 2 to ground with the 1050 powered, its alarm off and R9
fitted. Expect under 0.1 V.

**Falsified by:** Q4's gate reading 0.8 V or more to ground with R9 fitted, the 1050
powered and its alarm output off.

## 2026-10-25 — `bench.py` before the soak's restart: the keepalive, wall-clock times, retained messages

Three of the five items in *What the restart needs*, above, are now in `tools/bench/bench.py`,
with tests in `test_bench.py` that need no board:

- **`Mqtt._ping` skips the PINGREQ while `self.sock` is `None`**, so a broker outage no
  longer ends the keepalive. Any error other than `OSError` is logged and the thread keeps
  pinging. `test_keepalive_survives_a_missing_socket` starts the thread with no socket,
  which killed the old one, and checks that it pings once a socket appears.
- **Every log line now leads with local wall-clock time and its UTC offset**, ahead of the
  relative seconds: `2026-10-25T18:30:13-0400    112.04 MQTT …`. The offset keeps a run
  across the 1 November DST change readable. `wait --tag` reads the tag from the third
  field now. Logs written before this change have no wall-clock field, so `wait --tag`
  does not match them.
- **A PUBLISH the broker replays from its store is logged with `<retained>`** before the
  topic, from the retain flag of the received packet. MQTT 3.1.1 §3.3.1.3 has the broker
  set that flag on a stored message sent to a new subscription, so the first soak's stale
  `node/state`, restored after the broker's reboot, should have carried it.

Running `bench.py` outside a Claude Code session is an operating step, not a code change.
`tools/bench/README.md` now gives the `nohup` line. Whether GateLink publishes each BMS
poll's outcome is still the operator's call, and the restart does not wait on it.

**Not yet run against the broker.** The fixes are tested against a fake broker only. The
restart's first broker outage, or a broker restart forced at its start, is the test.

**Falsified by:** the restarted soak's log showing the broker closing `bench.py`'s
connection at 96 s intervals after a broker outage.

## 2026-10-25 — `/bms.log`: each BMS window's outcome on the card, for the restarted soak

**The operator chose the card over the air** for the restart. GateLink already counted how
each BLE window ended (`BmsEnd`), but only the console's `bms` line showed it, and the soak
runs with USB unplugged. On the air it would need a new schema ID or `event_type`, a spec
revision. The card needs neither.

**`bms_task` appends one line per window to `/bms.log`**, read or not, after it releases
the interlock:

```text
uptime_s=87011 end=not_found ms=3070 init=40 scan=3000 conn=0 hs=0 ans=0 down=25 abort=0 frames=287 crc=1 bad_term=0 undecoded=0
```

`end` is `bms_end_name()`'s word. The timings are `BmsTiming`'s, and the last four counts
are the notification path's, cumulative since boot. A miss in the first soak would have
read as one line whose `end` is not `read`. The RTC still reads 2049, so `uptime_s` is the
only clock; a drop in it is a reboot.

The append takes the SPI lock for up to 59 ms, as a `config.json` write does. With no card
mounted it fails at once and counts; it never remounts, because a remount with no card
holds the bus for about 1 s. The `bms` console line gains the written and failed counts
and `bms_task`'s free stack, since FATFS now runs on that 6 KB stack. The line buffer is
static.

`format_bms_log_line()` is in `bms_link.cpp`, and the native suite tests it; `BmsTiming`
and `BmsRxCounters` moved to `bms_link.h` with it. The target builds. **Not yet run on the
board**: nobody has checked the file's lines or the stack headroom on the StamPLC.

**Falsified by:** a `/bms.log` with fewer lines since a boot than the `bms` line's `ends`
add up to, while its failed count reads 0.

## 2026-10-25 — `/bms.log` on the board: one line written, 3252 bytes of stack free

GateLink was flashed over USB from a clean tree at `1049387`, and the banner read
`Version: 0.1.0 (1049387)`. With the 12 V on and USB attached, `cfg` read the card
mounted, and `bms now` ran one window: `read in 1052 ms`. The `bms` line then read
`/bms.log 1 failed 0; stack free 3252`, so the line reached the card, and `bms_task` kept
3252 of its 6144 bytes free through a full BLE window and the append. The file's text was
not read back; the operator can read it with the card in a laptop after the soak.

**Falsified by:** a `bms` line whose failed count rises while `cfg` reads the card
mounted.

## 2026-10-26 — Bench soak, second run, 19.2 h: GateLink did not reset, and the BMS missed five windows

**The run was set up by the handoff's *Starting the restart*,** on the Kubuntu laptop, on
branch `gl-bench-soak`, with GateLink at `1049387`. `bench.py --dir
~/lran-soak/2026-10-25-run2 run --port BRG=/dev/ttyUSB0 --mqtt` started at 19:10:27 EDT on
2026-10-25 under `nohup`, with the bridge's console on the laptop and the StamPLC's USB
unplugged. GateLink's `BOOT` arrived at 19:14:51 with `reset_cause` `power_on`. The logger
was stopped with `send quit` at 14:25:14 on 2026-10-26, 69,287 s in. It ran to the end,
outside any Claude Code session.

**GateLink did not reset in 19.2 h.** `uptime_s` in `lran/gatelink/node/state` rose
without a drop to 69,010 at 14:24:59, which places its boot at 19:14:49. The bridge's
`event_frames` stayed at 2, the `BOOT` and the first `FIRE_ASSERTED` it raised.

| Watched for | Result |
|---|---|
| `BOOT` after the first | none: `uptime_s` continuous, `event_frames` 2 throughout |
| STATUS rate | 1,153 `status_frames` and 1,150 polls, all answered, one per `poll_interval_s` 60. `missed_polls` 0 in every `diag/state` |
| Link | RSSI −43 to −53 dBm, SNR 11 dB. `rx_no_interrupt`, `rx_wake_empty` and `tx_errors` 0 throughout; `q_publish_dropped` 0 |
| Broker | one connection for the whole run, with no outage, so the keepalive fix ran 19 h but was never tested by an outage |
| VE.Direct | never unavailable in 395 `solar/state` documents after the boot's first |
| BMS | five missed windows, each one window long, below. Two of them reached the bridge's stale limit |
| Overnight ΔSOC | 0: SOC 99 % throughout, pack 13,300 to 13,310 mV, `pack_ma` 0 throughout |

**The BMS missed five windows, one at a time, at about 3.5 h spacing.** `age_s` climbed
past `bms_poll_s` 300 and peaked at 600 at 19:54:59, 602 at 23:20:59, 601 at 02:51:59, 600
at 06:27:59 and 600 at 13:39:59. A fresh read followed each time. The gaps are 3 h 26 min,
3 h 31 min and 3 h 36 min, then 7 h 12 min, so a miss expected near 10:03 did not happen.
The first run's spacing was about 3.7 h (entry of 2026-10-25, *Bench soak, 24 h*). **This
time two of the misses crossed the line**: at 602 and 601 s, past the bridge's
`bms_stale_s` 600, the bridge published `battery/state` with `"available": false` until the
next read. One miss is enough to mark the battery unavailable, depending on where the
bridge's publish falls against the read.

**`/bms.log` was not read.** It names how each of those five windows ended, which is what
this run was restarted to find. The console has no command that prints the file. The card
cannot be read from the Kubuntu laptop, and the operator declined a reflash from it,
because it carries a different toolchain from the Mac's. The file stays on the card until
it is read.

**Five events at 10:50 to 10:51 were the operator's, not GateLink's.**
`vehicle_while_held_open`, `fire_asserted`, `hard_shutdown` and `bms_alarm`, with
`event_id` 9001 to 9005, `ctx_id` 4294967295 and `uptime_s` 1 to 12, arrived on MQTT
alone. The bridge's `events` count did not move and no radio frame matches them. The
operator published them as a test. Their `uptime_s` reads as resets to a script that
does not filter on topic.

**The MPPT's `load_on` went false at 08:37:59, and it charged in `bulk` from 11:45:00**, at
1 W and up to 70 mA, as in the first run. `charge_fet` and `discharge_fet` read false and
`alarms` 65535 again throughout.

**Falsified by:** a `/bms.log` whose lines at `uptime_s` near 2,410, 14,770, 27,430,
40,390 and 66,310, give or take one `bms_poll_s`, all read `end=read`. The stale ages
would then come from the path after the window, not from the BLE side.

## 2026-10-26 — `/bms.log` read: all five misses were windows the poll reply aborted

**The operator took the card out and the Mac read it.** `/bms.log` held 239 lines. The
first three, at `uptime_s` 14 to 617, come from a boot before the soak's. `uptime_s` then
drops to 302 and climbs without a drop to 71,134, past the logger's stop: the board ran on
until the card came out. Of the soak boot's 236 windows, 231 read `end=read` and five
read `end=aborted`. None is `not_found`, a connect failure or a timeout:

```text
uptime_s=2110 end=aborted ms=1112 init=4 scan=441 conn=650 hs=0 ans=0 down=17 abort=1 frames=6 crc=0 bad_term=0 undecoded=0
uptime_s=14470 end=aborted ms=2628 init=4 scan=821 conn=1650 hs=121 ans=0 down=32 abort=1 frames=46 crc=0 bad_term=0 undecoded=0
uptime_s=27130 end=aborted ms=1142 init=4 scan=281 conn=630 hs=91 ans=129 down=7 abort=1 frames=87 crc=0 bad_term=0 undecoded=0
uptime_s=40090 end=aborted ms=677 init=4 scan=21 conn=635 hs=0 ans=0 down=17 abort=1 frames=129 crc=0 bad_term=0 undecoded=0
uptime_s=66010 end=aborted ms=247 init=4 scan=241 conn=0 hs=0 ans=0 down=2 abort=1 frames=214 crc=0 bad_term=0 undecoded=0
```

These lines are the five misses the previous entry predicted, each one `bms_poll_s` before the
`uptime_s` it estimated from the stale-age peaks. That entry's falsifier did not fire: the
misses are on the BLE side.

**`aborted` means `lora_task` asked for the radio back** (`TxGate::WaitAbort`,
`bms_link.cpp`), and it asks only for a frame the bridge awaits. Every abort's line reads
`uptime_s mod 60` = 10, so the frame is the poll's STATUS reply. The bridge polls every
60 s, and the polls land at a fixed phase of GateLink's uptime.

**The window walks into that phase about every 3.5 h, because `bms_task` waits
`bms_poll_s` after a window ends rather than from when it began.** Its period is 300 s plus
the window, and successive lines step 301 s (142 times), 302 s (89) or 303 s (4). The mean
window is 1,394 ms, so the window's phase against the 60 s poll cycle advances about 1.4 s
per window. It crosses the poll's phase once in about 43 windows, about 3.6 h, and the
logged spacing is 12,360 to 12,960 s. The pass expected near 10:03 did not abort: its
window logged `uptime_s` 53,350, phase 10, and read in 1,292 ms. At one-second resolution
the line cannot show whether the poll fell just after that window or the window stepped
over it.

**A miss costs a whole period because R-3.4d retries at the next poll.** `age_s` then
reaches about 600, the bridge's `bms_stale_s`. Whether a miss marks the battery unavailable
depends on where the bridge's publish falls against the next read; two of five did.

**Proposed, the operator's call, Tier 3 since it is a reflash:**

- After `end=aborted`, run the next window a few seconds after the reply goes out instead
  of `bms_poll_s` later. The abort keeps the radio's priority, and a miss costs seconds
  rather than a period.
- Or raise the bridge's `bms_stale_s` above twice `bms_poll_s`, so one miss of any kind
  never marks the battery unavailable. A bridge row, with no walk to the gate.

Scheduling from the window's start (`vTaskDelayUntil`) is not a fix. The phase would then
move only with the two crystals' drift, so a collision, once reached, could repeat for
hours.

**Falsified by:** with the abort retry built, a soak whose `/bms.log` shows an `aborted`
line followed by a window more than one `bms_poll_s` later.

## 2026-10-26 — `bms_retry_s`: an aborted BMS window is retried once, seconds later

**The operator chose the firmware fix** over raising the bridge's `bms_stale_s`. After a
window ends `aborted`, `bms_task` waits `bms_retry_s` instead of `bms_poll_s`, then runs
the window again. A retry aborted in turn waits the full period, so a burst of awaited
replies cannot keep BLE cycling against the radio. Other ends keep R-3.4d's next-poll retry.

**`bms_retry_s` is row `0x1046`**: u16, default 5 s, range 2 to 60. A timing value is a
parameter on a node that cannot be reflashed without a walk (root rule 8). The floor leaves
the aborting reply its backoff, at most `backoff_max_ms` 1500, and its airtime before the
retry takes the interlock. GateLink's block is 25 rows and 172 bytes; a readback is 240
bytes, still two `CONFIG_ACK` messages. The HA discovery file and the maintenance
dashboard's Power card carry it.

`bms_retry_next()` in `bms_link.cpp` decides it, and `test_bms_link` tests it. The target
builds. **Not yet run on the board**, and the bench bridge needs a reflash before HA can
set the row.

**Falsified by:** a soak whose `/bms.log` shows an `aborted` line that is not itself a
retry, followed by a window more than `bms_retry_s` and one window later.

## 2026-10-26 — `bms_retry_s` on the board: each forced abort retried; one abort panicked in NimBLE

**GateLink `a1de65d`, flashed over USB from the Mac**, through a USB 2.0 hub with the 12 V
supply on. Console only, no bridge or broker. Trace:
[`data/bms-retry-2026-10-26.log`](data/bms-retry-2026-10-26.log).

**Every forced abort was followed by one retry.** Fifteen `bms now <abort_ms>` commands,
from 100 to 1000 ms, gave 14 aborted windows; the fifteenth read the pack before its abort
point. The `bms` line's counts settle it where the line timing cannot. Read 8 s after an
abort, they showed `read` and `aborted` rising together: 12 and 12, then 13 and 13, then
14 and 14. The window that read first left `aborted` at 14 and ran no retry. Where both
lines printed promptly, the retry's `read` line came 5.9 to 6.9 s after the abort's, which
is `bms_retry_s` 5 plus the retry's window. One abort's line and its retry's printed in the
same instant, 7 s after the command, which is the late printing the entry of 2026-10-10
noted.

**Not exercised:** a retry that a reply aborts in turn, which should wait the full period,
and an abort raised by a real poll. Both need the bridge.

**The first forced abort panicked GateLink.** `bms now 300`, 0.29 s in: `InstrFetchProhibited`,
PC 0, and the backtrace's one resolvable frame is `NimBLEDevice::host_task`
(`NimBLEDevice.cpp:837`), the line after `nimble_port_run()` returns. So NimBLE's host task
ran past `nimble_port_freertos_deinit()` instead of being deleted, during the teardown
the abort starts. On 2026-10-10, two series of 26 windows with bench aborts ran clean on
`esp_timer`, and the two soaks' five real aborts did not reset. One panic in 15 here is too few to rate, and
the retry adds a teardown for every abort. Its cause is not established.

**The card did not mount.** The operator fitted a new card, formatted FAT32 on the Mac with
the old card's other files copied over, and `f_mount` returned 13, "no valid FAT volume",
at every boot. `/bms.log` counted 29 failed appends and no lines. A GUID partition map
would give that, since ESP-IDF's FATFS reads an MBR; not checked.

**Falsified by:** a forced abort whose `bms` counts, read 8 s later, show `aborted` risen
and `read` not.

## 2026-10-26 — The new card was HFS+; reformatted FAT32, it mounts and takes the retry's line

**The 4 GB card's partition map was MBR, but its volume was Journaled HFS+**, which
`diskutil info` showed on the Mac. ESP-IDF's FATFS reads only FAT, so `f_mount` returned 13.
The guess at a GUID map in the previous entry was wrong. The operator reformatted it
MS-DOS (FAT) on MBR and copied `config.json` back.

**After a `restart`, the card mounted and restored 3 rows.** The boot before it had not
tried a mount, so `cfg` read `NOT MOUNTED` with 0 mounts failed. One `bms now 400` then
aborted at 19.14 s, and its retry read the pack at 25.24 s, `bms_retry_s` 5 plus a 1,082 ms
window. The `bms` line read `ends read 1 aborted 1` and `/bms.log 2 failed 0`: both windows
wrote their line. Trace:
[`data/bms-retry-card-2026-10-26.log`](data/bms-retry-card-2026-10-26.log).

## 2026-10-26 — The MPPT's history after two weeks on the gate: ten full days at 50 W, and M14 closes

**Bridge and StamPLC on the bench with the sandbox broker; the MPPT on the pack with no
PV.** The MPPT went back to the gate after the 2026-10-08 read and stayed on its 50 W panel
until the operator pulled it for the bench soak. Its history therefore holds the gate days
that the 2026-10-08 read lacked. GateLink ran `a1de65d`. After the bridge powered up, the
operator had to restart GateLink before it answered; the cause was not looked at. Trace:
[`data/m14-mppt-history-2026-10-26.log`](data/m14-mppt-history-2026-10-26.log).

**Every Get from `0x104F` to `0x106E` answered `ok` over the air, with no timeout.** The
totals record's yield read 581, matching the solar document's `yield_total_kwh` of 5.81.
It read 467 on 2026-10-08, so the MPPT harvested 1.14 kWh in between.

**The records overlap the 2026-10-08 read at day sequence 58.** Its day 1, 312 minutes of
bulk and no absorption, is day 15 here, and days 16–30 repeat its days 2–16. The MPPT
starts a record when PV returns after a dark spell, not at midnight. The 18 calendar days
since 2026-10-08 made 15 records, so a record's age in days does not give its date.

| Days | Where | Yield kWh | Load kWh | Vbat min | Vbat max | Absorption, float min | Pmax W |
|---|---|---|---|---|---|---|---|
| 0–2 | Bench | 0.00 | 0.00 | 13.26–13.28 | 13.28–13.35 | 0, 0 | 0–1 |
| 3 | Gate or bench, not known | 0.04 | 0.03 | 13.13 | 14.20 | 0, 0 | 41 |
| 4–13 | Gate, 50 W | 0.08–0.10 | 0.04–0.05 | 12.48–13.19 | 14.21–14.23 | 120–124, 51–120 | 42–53 |
| 14 | 2026-10-08, bench then gate | 0.20 | 0.06 | 12.98 | 14.21 | 120, 0 | 43 |

**On each of the ten gate days the charger reached absorption and then float**, and no
record carries an error. The battery was full every day, so the yield shows what the battery
could take, not what the panel can give. Load-output consumption was 0.04–0.05 kWh a day.
Days 1 and 2 reached a Vpv maximum of 19.03 V and 19.34 V, but no more than 1 W.

**M14 closes on these records.** It asked for a week of MPPT yield and battery Vmin before
install, and days 4–13 are ten consecutive days on the gate's own panel. The ten records
are the MPPT's daily summaries, not a log at GateLink's poll rate.

**Falsified by:** a gate-day record without absorption once the gate is back in service,
in the same season.

## 2026-10-26 — The NimBLE abort panic: a queued timer event zeroed at de-init; 100 aborts clean after the fix

**The panic of the forced abort was the host task running `ble_hs_timer`'s event after
the event was zeroed.** The register dump in
[`data/bms-retry-2026-10-26.log`](data/bms-retry-2026-10-26.log) resolves against the
`a1de65d` image. A8, `0x40375439`, is the return into `nimble_port_run()` after
`ble_npl_event_run()`, and A2, the event passed, is `0x3fcad48c`. That is offset 8 in
`ble_hs_timer`, the static callout in `ble_hs.c`, which is where its embedded event sits.
PC 0 is that event's `fn`, which was null.

**NimBLE zeroes that callout while its event can still be queued.** `ble_hs_timer_reset()`
stops and de-inits `ble_hs_timer` once the host is not enabled, and
`npl_freertos_callout_deinit()` clears the whole callout, event included, without taking
it off the queue. `ble_hs_stop()`, inside `NimBLEDevice::deinit()`, sets the host to not
enabled. A scan stop runs `ble_gap_master_reset_state()`, which calls
`ble_hs_timer_resched()` and queues the event at once. An abort reaches `deinit()`
microseconds after the scan stop, so the event can still be waiting when the callout is
zeroed. A window that reads goes on to connect, so the host task runs the event long
before teardown. NimBLE-Arduino 1.4.3 has that `ble_hs_timer_reset()`, and so does
esp-nimble's `nimble-1.5.0-idf` branch.

**`5fd1509` drains the host queue before each de-init.** `bms_window()` waits 10 ms, puts
a marker event on NimBLE's default queue, and waits up to 100 ms for the host task to run
it. The queue is FIFO, so once the marker runs, every event queued before it has run. The
`bms` line reports the longest wait and the timeouts. A rescheduling that comes after the
marker and before `ble_hs_stop()` is not covered. Nothing in a stopped window should
cause one, but that is reasoning, not a measurement.

**100 forced aborts on `5fd1509` ran without a panic.** Bridge and broker on the bench,
the pack in reach, and abort points spread from 102 to 997 ms. Trace:
[`data/bms-abort-drain-2026-10-26.log`](data/bms-abort-drain-2026-10-26.log).

| | Count |
|---|---|
| Windows aborted | 100 |
| Aborted in the scan, the case that panicked | 25 |
| Retries that read the pack | 98 |
| Retries that failed to connect | 1 |
| Panics or resets | 0 |
| `/bms.log` lines written, failed | 199, 0 |
| Longest drain wait, timeouts | under 1 ms, 0 |

At the earlier rate of one panic in 15 aborts, 100 clean aborts would come about one time
in a thousand. The 2026-10-10 timer-task panics on FreeRTOS timers fit the same zeroed
event, but that was not checked.

**Falsified by:** a panic in `nimble_port_run()` on `5fd1509` or later, or a `bms` line
with drain timeouts above 0.

## 2026-10-27 — The final soak, 12 h: no reset, and every aborted BMS window read on its retry

**The run used the handoff's *The soak's setup*, with the MPPT and the pack back on the
bench, logged from the Mac.** `bench.py --dir ~/lran-soak/2026-10-26-soak3 run --mqtt`
started at 17:14:05 EDT on 2026-10-26 and stopped on `send quit` at 05:14:05 on
2026-10-27, 43,200 s in. No console was attached. GateLink's `BOOT` came before the logger
started: the first `node/state`, a retained one, read `uptime_s` 130, and the next placed
the boot at about 17:11:22. So the MQTT log has no `reset_cause`, and no version topic names
the image. The image carries `a1de65d`'s retry, because the card shows it running. That it
carries `5fd1509`'s drain was not checked.

**GateLink did not reset.** `uptime_s` rose without a drop from 130 to 43,330. The card's
`/bms.log` carries the same boot further: its `uptime_s` climbs without a drop to 71,790,
about 13:08 on 2026-10-27, after the logger stopped. The bridge's `event_frames` stayed at
1, the `BOOT`.

| Watched for | Result |
|---|---|
| `BOOT` after the first | none: `uptime_s` continuous, `event_frames` 1 throughout |
| Polls | `missed_polls` 0 throughout; the bridge's `rx_frames` reached 739 with every receive-error counter at 0 |
| BMS | seven aborted windows on the card, each read on its retry 6 or 7 s later. `battery/state` never went unavailable |
| Battery `age_s` | 301 at most, against 600 to 602 at each miss in the second run |
| VE.Direct | `solar/state` available throughout; the night run left the MPPT `off`, PV at 10 mV |
| Overnight ΔSOC | 0: SOC 99 % throughout, `batt_mv` 13,260 to 13,280 |
| Safety, Exit and Fire | inactive throughout, as wired: all 47 `gate/state` messages read `in_safety`, `in_exit` and `in_fire` false and `input_bits` 0 |
| Drain timeouts | lost: the console was not attached, and the counters live in RAM, cleared when the StamPLC was powered down |

**The Safety, Exit and Fire inputs were tied to their inactive state for this run**, with
the input FETs on (operator, 2026-10-27), so their indicators should have stayed inactive
throughout. They did. Every `gate/state` message read them false, and `detect/state`'s
`safety` and `exit` read false too. GateLink sent no event frame after its `BOOT`, and a
Fire edge would have sent `FIRE_ASSERTED` (D83).

**`/bms.log` answers the question the soak was for.** The operator powered the StamPLC
down, took the card out, and the Mac read it. The file holds 454 lines from several boots. The soak's boot starts at
line 210, at `uptime_s` 302, and runs 245 lines to 71,790. Of those windows, 237 read
`end=read`, seven `end=aborted` and one `end=connect`. Each `aborted` line is followed by
an `end=read` 6 s later, 7 s once:

| Aborted at `uptime_s` | Read at | Gap since the previous abort |
|---|---|---|
| 2,110 | 2,116 | — |
| 13,270 | 13,276 | 3.1 h |
| 25,030 | 25,036 | 3.3 h |
| 36,490 | 36,496 | 3.2 h |
| 47,050 | 47,057 | 2.9 h |
| 57,910 | 57,916 | 3.0 h |
| 68,770 | 68,776 | 3.0 h |

Every abort's `uptime_s mod 60` is 10, the poll's STATUS reply, as in the second run (entry
of 2026-10-26, *`/bms.log` read*). The first four fall inside the logger's span, at the
four `age_s` peaks of 300 and 301 at 17:46, 20:52, 00:08 and 03:19. Each was followed a
minute later by an `age_s` near 53. `bms_retry_s` does what it was built for: an abort now
costs about 6 s of age, not a whole `bms_poll_s`. The windows still walk into the poll's
phase, so the aborts keep coming. Only their cost changed.

**The one `end=connect` is the last line, at 71,790**, about two minutes before the card was
read, after the logger had stopped and before the power-down. It is the only connect failure in the soak's boot.
The retry applies only to an aborted window, so nothing followed it. It was not looked into.
Trace: [`data/bms-log-soak3-2026-10-27.log`](data/bms-log-soak3-2026-10-27.log), lines
210 to 454 of the card's file.

**Reading the card means powering GateLink down, so the RAM counters must be read first**
(operator, 2026-10-27). With the StamPLC installed in GateLink's enclosure, taking the SD
card out and putting it back is difficult. Caution requires powering the unit down to reach
the card. That power-down is what lost this soak's drain timeouts. An unattended soak whose
firmware counters matter has two ways to keep them: write them to the card, or attach USB
and read them before the card comes out.

**Falsified by:** an `aborted` line in a later `/bms.log` with no `end=read` within
`bms_retry_s` plus one window, or a `battery/state` `age_s` past about 310 with the pack
in reach.

## 2026-10-27 — R-6.4b's PV power test cannot hold: a pack that takes nothing leaves PPV at 0 W

**R-6.4b as written could never infer an inhibition, so PRD v0.23 tests PV voltage
instead** (operator, 2026-10-27). GateLink `3fa9dfe`, with D81, ran on the bench with the
gate's MPPT 75/15, the WattCycle pack on its battery terminals, a 30 W panel behind a
window on its PV input, and the bench bridge and sandbox broker. The pack was full, at SOC
99 % and `pack_ma` 0, so it took no charge, which is how the MPPT sees a pack whose BMS
refuses charge. Trace:
[`gl-r64b-ppv-2026-10-27.log`](./data/gl-r64b-ppv-2026-10-27.log), GateLink's console and
the `solar/state` and `battery/state` documents.

| Seconds | `charge_state` | PV | `pv_w` | Battery |
|---|---|---|---|---|
| 0–176 | bulk | 15.26–17.06 V | 1 W | 13.28 V, 30–40 mA |
| 234 on | bulk | 15.47–20.35 V | 0 W | 13.28 V, 0–10 mA |

**The MPPT stayed in bulk with the sun up and reported 0 W.** A charger whose battery takes
nothing harvests nothing, so `pv_w > 0` fails in exactly the case R-6.4b is for. Here
GateLink's VE.Direct condition held for 234 s on the 1 W readings, then failed and
restarted its clock short of `charge_inhibit_confirm_s`. A load on the MPPT's load output
would probably keep PPV above 0; `load_ma` read 0 throughout.

**PV voltage shows the sun whatever the battery takes**, so R-6.4b now asks for PV at
least 1 V above battery voltage. The 1 V is the floor Victron's SmartSolar manuals give for
charging once it has started; the 75/15's own figure has not been checked. Here PV sat 2 V
or more above the battery throughout.

**The BMS read at 302 s took over the decision**, as designed: a fresh read outranks
VE.Direct for three polls. The pack read 19–22 °C, so nothing was inhibited.

**Falsified by:** a `solar/state` in bulk or absorption with `batt_ma` near 0 and
`pv_mv` below `batt_mv` plus 1000 while the sun is on the panel.

## 2026-10-27 — D81 on the air: `detail` 2 from VE.Direct with the real MPPT, `detail` 1 from the BMS emulator

**Both deciding paths name themselves on the air** (PRD R-6.4e, spec §8.9). GateLink
`eac974c`, with D81 and PRD v0.23's PV voltage test, ran on the bench with the MPPT, the
pack and the window panel of the entry above, the simnode's BMS emulator, the bench bridge
and the sandbox broker. Trace:
[`gl-d81-detail-2026-10-27.log`](./data/gl-d81-detail-2026-10-27.log). Times are seconds
from the harness's start, which was about 5 s after GateLink booted.

| Check | What was done | What happened |
|---|---|---|
| VE.Direct onset (R-6.4b) | Nothing. The full pack took 0–40 mA in bulk, with PV at 15.5–19.8 V and `pv_w` 0. `bms_poll_s` was set to 3600 at 22 s, so the decision would stay with VE.Direct | At 302.44 `solar/state` read `charge_inhibited: true` and `node/state` reason `charge_inhibited`. At 304.56 `event/charge_inhibited`, event_id 2, carried `detail` 2, `inhibit_path` `vedirect`, `uptime_s` 300. GateLink logged `charge: INHIBITED, by vedirect` |
| Real pack's read | The BMS window already due at 300 s ran at 307.76 | The pack, at 19.4–22.7 °C, took the decision and cleared it: `charge: no longer inhibited, by bms` |
| BMS onset (R-6.4a) | `SIM bms on 49A1`, `SIM bms temp -5`, then `GL bms now` five times | The first window reached the real pack. The second reached the emulator, and event_id 3 at 377.54 carried `detail` 1, `inhibit_path` `bms`. The third reached the emulator, and the fourth the real pack, which cleared it at 390.91. The fifth reached the emulator, and event_id 4 at 394.03 carried `detail` 1 again |

**The VE.Direct inference won its race against the BMS read by about 7 s.** Setting
`bms_poll_s` to 3600 did not move the window already scheduled for 300 s. A rerun that
needs VE.Direct to hold the decision should set the row before boot, or hide the pack's
BLE.

**The real pack and the emulator advertise the same name, `XDZN_001_49A1`, and GateLink
takes whichever its scan meets first.** Read time told them apart: the emulator answered in
372–402 ms, the pack in 1512–1692 ms. The simnode's `bms status` counted 3 connects, which
matches the three emulator reads.

**The bench was put back**: `SIM bms temp off`, `SIM bms off`, and `bms_poll_s` set to
300, which `config/ack` read as `persisted`.

**One console line in the trace is two lines interleaved**, at 316.08, where a `charge:`
line and a `chg` reply arrived together. It is left as captured.

**Not shown: the inference against a pack whose BMS refuses charge.** The full pack stands
in for one, and the MPPT sees the same zero current. A BMS that opens its charge FET may
also let the terminal voltage rise, which would restart the clock until it settles. The
first cold sunny morning at the gate is that test.

**Falsified by:** a `CHARGE_INHIBITED` event whose `detail` disagrees with the `charge:
INHIBITED, by …` line GateLink logged for the same onset.

## 2026-10-27 — `pack_ma`, the FET flags and `alarms` are not decoding gaps

**Question:** the three soaks read `pack_ma` 0, `charge_fet` and `discharge_fet` false, and
`alarms` 65535 throughout. Are those the pack's real states, or a gap in the decode? The pack
was still on the bench, so this was the last chance to read it without a walk to the gate.

**`alarms` and the FET flags are not decoded at all, by design.** `fill_bms_block()` writes
`bms_alarms` as `UINT16_MAX` and never sets `bms_flags` bits 1–5, because `0x8D` is not
decoded (bms-protocol §9, `bms_link.h`). The bridge then publishes the sentinel as the number
65535 and the unset bits as `false` (`publish.cpp`, the battery block). So `alarms` 65535
means "unavailable", and `charge_fet: false` means "not known", not "off". Spec §7.2.7 gives
bits 1–5 no way to say "unknown", and that gap is the specification's to close, not the
bridge's. It is under *Open* in the handoff.

**`pack_ma` 0 is a correct decode of a current below the field's resolution.** A live read
from the real pack, at 14:39 EDT with the MPPT in bulk and `batt_ma` 20–30:

```text
bms: cells 4: 3327 3328 3328 3328 mV (delta 1); temps 4: 206 225 198 198 (0.1 C); pack 13310 mV 0 mA (charge flag, raw 0x4000); SOC 99%; 989/1000 (0.1 Ah); cycles 4; SOH 1000; rssi -71
```

Raw `0x4000` is bms-protocol §6's at-rest pattern: bit 14 set, magnitude 0. The field steps
in 0.1 A. The StamPLC and carrier draw 43.6 mA (2026-10-06), and the MPPT put 20–30 mA
in, so the pack's net current was well under one step. The decoder has not changed since M7's
fix (`5f0e564`, 2026-10-10), which matched −4.0 A and +1.8 A against the WattCycle app on
this pack. The read took 1393 ms, the real pack's time, not the emulator's.

**This matters for R-6.4c.** If D82's `ah_drawn_dah` integrates `pack_ma`, GateLink's own
standing draw of about 44 mA never registers, and a night of it reads 0 Ah. About 1 Ah
a day is lost below the step. R-6.4c still has to choose the current it integrates, and
the BMS field cannot carry a load this small.

**Opening the StamPLC's console reset it.** `bench.py run --port GL=/dev/cu.usbmodem101`
opens the port with DTR and RTS low, and the board still restarted:
`rst:0x15 (USB_UART_CHIP_RESET)`, `Reset: usb`. The RAM counters went with it, so the drain
timeouts were again not read. They held only what had accumulated since the D81 session's
flash, not a soak. The handoff's plan, to read the `bms` line over USB before the card
comes out, needs the console already open before the soak starts. Attaching afterwards
clears what it came to read. Whether the reset comes from macOS's open or from pyserial's
line setup is not established.

**The first window after that reset ended `no_answer`** (scan 510 ms, connect 639 ms,
handshake 90 ms, 3729 ms with no `0x8C` reply). The second ended `connect` at 111 ms, and the
third read. This joins the 2026-10-10 entry's first-after-boot `aborted` windows; not
looked into.

**Falsified by:** a raw current field with nonzero magnitude bits that decodes to 0 mA, or a
`pack_ma` of 0 while the WattCycle app shows 0.1 A or more.

## 2026-10-28 — The silence after a bridge restart: GateLink's `deployed` lever was 0, so the bridge never polled it

**Bridge and StamPLC on the bench, both consoles on USB, with the sandbox broker.** The
bridge ran `760e9a3` and GateLink `eac974c`. Trace:
[`data/bridge-reboot-silence-2026-10-28.log`](data/bridge-reboot-silence-2026-10-28.log).

**The silence reproduced on the first bridge reset.** For the 2 min that followed, the
bridge sent no frame and heard none. Its `diag/state` read `rx_frames` 0, and
`diag/cmd/state` read `roll_sent` 0. GateLink's console printed only `alive:` lines.
GateLink was not waiting on a roll it had missed, because the bridge never sent one.

**The cause is GateLink's `deployed` lever on the bridge, at its default of 0.** The
retained `lran/gatelink/config/state` read `"deployed":{"value":0,"source":"default"}`.
Under D61 (spec §16.5), the bridge polls a node from boot only when that node's `deployed`
row is set. It enrols any other node when that node's first frame arrives. The §10.6 roll
is sent under the node's `ctx_id`, and the boot `POLL` is what fetches it. With the lever
at 0 the bridge sent no `POLL`, so it waited for GateLink to transmit first. GateLink's
`lora_task` sends unprompted only for a gate, hold or detection change, and none came in
the 2 min observed. A GateLink restart sends a boot `STATUS`, which enrols it, and the roll
follows. That is why the operator's restart ended the silence on 2026-10-26 and
2026-10-28.

**Setting the lever ended it.** `{"set":{"deployed":true}}` on `lran/gatelink/config/set`
was answered `persisted`. The bridge polled 0x01 within 0.3 s, and the roll completed 3 s
later. After a second bridge reset, the boot `POLL` went out 634 ms after boot, and the
roll completed about 3 s after that.

**The bench bridge keeps `deployed` = 1 for GateLink.** Bridge plan §6.6.2 keeps it at 0
on a bench bridge because the dummy GateLink never answers. The real GateLink is on the
bench now, so the lever belongs at 1. With it set, the bridge marks GateLink offline three
missed polls after GateLink loses power.

**The 2026-10-08 silence is only partly explained.** The bridge there had the same default,
so its radio silence fits. The StamPLC also had no `/dev/cu.usbmodem*` until the replug,
and this cause does not explain that.

**Falsified by:** a bridge restart with `deployed` = 1 for 0x01 that is followed by no
`POLL` to 0x01, or a silence after a restart in which the bridge's log shows that `POLL`
and a completed roll.

## 2026-10-28 — R-6.4c counts the BMS's remaining capacity, because neither current sees GateLink's draw

**The operator chose both of R-6.4c's terms (2026-10-28).** Charging is active while the MPPT
is in bulk, absorption or float with VE.Direct battery current above 0. `ah_drawn_dah` is the
fall in the BMS's remaining capacity since then, not an integrated current (PRD v0.24).

**Neither current GateLink reads carries its own 44 mA.** `pack_ma` steps in 0.1 A, so the
draw reads 0 (2026-10-27, *`pack_ma`, the FET flags and `alarms`*). VE.Direct's `I` is the
charger's own output, and GateLink takes its supply straight from the pack (PRD R-4.4a), not
from the load output, so `I` never includes it. At night both integrate to about 0 Ah.

**The choice rests on an unmeasured premise:** that the BMS's coulomb counter registers a
draw its current field shows as 0. The remaining capacity also steps in 0.1 Ah (`989/1000`
in the 2026-10-27 read), and the soaks did not log it overnight.

**Falsified by:** BMS remaining capacity unchanged across a night with GateLink's load alone,
where about 0.5 Ah over 12 h is expected.

## 2026-11-01 — The firmware freeze on the bench: `ACTUATOR_FAULT`, schema `0x13`, a fault log, and a console that does not reset

**On the air, with the sandbox bridge and no MPPT or pack.** The StamPLC ran `gatelink-sandbox`.

| Check | Result |
|---|---|
| F1, `relayfault` then `cmd/open/set` | `cmd/ack` `result` 19 (`ACTUATOR_FAULT`), `detail` 0, one attempt |
| F1, the next OPEN | `result` 0 |
| F2, a STATUS | `schema` 19 (`0x13`) in the bridge's frame log |
| F4, `lran ctx new`, then a bridge reset | `ctx` renewed, then a roll `ACCEPTED` with a new value |
| Fault log | `boot cause=5` after `panic`, `actuator_fault` after the F1 check |
| Fault log rotation | With the cap set to 300 B for one image, `fault.txt` moved to `fault.old.txt` |

**Then with the simnode Heltec's BMS emulator**, `bms on` with no suffix, so it advertised
`XDZN_001_BC2D`:

| Check | Result |
|---|---|
| F8, a name other than `XDZN_001_49A1` | Read, and the fault log read `bms_name XDZN_001_BC2D` |
| F7, `bms temp -1` | `INHIBITED`, and one `CHARGE_INHIBITED` with `detail` 1 |
| F7, `bms temp 1`, inside the band | Stayed inhibited, with no second event |
| F7, `bms temp 2`, past the band | Cleared |
| F9, `/bms.log` after a read | `drain_to=0 drain_max=0` |

**Not run on the air:** `MPPT_ERROR` and a real `ah_drawn_dah` count, because both need the
MPPT. The host suites cover their logic. `ah_drawn_dah` has only been sent as `UINT16_MAX`,
and R-6.4c's premise is still unmeasured.

**The stack table and the static stack must agree, and no test says so.** Raising
`log_task`'s row to 6144 alone gave `Tasks: 6 of 7 started` and no console. `start_tasks()`
skips a row whose stack disagrees. The banner line is the only sign.

**Opening the console resets the StamPLC through pyserial, not through the open.** This
settles the question left open on 2026-10-27, on macOS. BOOT events over MQTT counted the
resets across three opens of `/dev/cu.usbmodem101`:

| Open | Boots |
|---|---|
| `os.open()` alone | 0 |
| `os.open()`, then DTR and RTS cleared by ioctl | 0 |
| pyserial with `dtr` and `rts` false before `open()`, as `bench.py run` does | 1 |

`bench.py attach` opens the port bare and sets raw mode with `tcsetattr`, and it read the
`bms` line with no BOOT. **So a visit can read the RAM counters, on macOS.** Linux is not
tested, and the field laptop runs Kubuntu. **Falsified by:** a BOOT event during `attach`.

**A panic's backtrace is readable over USB after the fact.** The framework's `sdkconfig`
writes an ELF core dump to flash, and the `coredump` partition is at `0x7F0000`. After
`panic`, `esptool read_flash 0x7F0000 0x10000` returned it. The ELF starts 20 bytes in, and
`xtensa-esp32s3-elf-gdb` with the image's `firmware.elf` showed `console_command()` at
`task_runtime.cpp:2088`. **The core dump is useless without the `firmware.elf` of the image
that wrote it**, so keep the production build's. `esptool` resets the board. Whether a
watchdog panic also writes one, given 2026-10-07's `Re-entered core dump!`, was not tried.

**The console loses bytes when it is fed fast, and the CRC catches it.** `file cat` of a
180 KB `/bms.log`, with `attach` as the reader:

| Writes | Lost | CRC |
|---|---|---|
| 512 B a tick, 100 ms TX timeout | 7,583 B, about 4 % | fails |
| 512 B a tick, 2 s TX timeout | 69 to 430 B, about 0.1 % | fails |
| 128 B when the ring has room, no pause | about 25,000 B, about 15 % | fails |
| 64 B per 4 ms, 2 s TX timeout | 0 in five dumps | matches |

That last rate is about 11 KB/s, so 180 KB takes 17 s. Why feeding HWCDC faster loses more
is not established. This may be the same loss as 2026-10-08's, unchecked.

**`/config.json` holds `{}`, not 3 overrides.** The handoff's *Hardware state* was out of
date; `file cat` showed 3 bytes. Nothing here wrote it before the `bms_poll_s` test.

### F5: the relay at a watchdog reset, a proposal for the operator

**The problem is wider than the brief's "about 1 s".** A reset clears a held relay only at
`setup()`'s `board_relays_off_early()`, because a CPU reset leaves the expander's latch
alone (2026-10-07). Three cases hold a relay closed past its pulse:

| Case | Held for | Today |
|---|---|---|
| A panic during a pulse | Until `setup()`, about 1 s | Measured 2026-10-07 |
| `io_task` hangs during a pulse | Without limit. The task watchdog watches `app_task` alone, so nothing fires while `app_task` runs | Read in the source, not run |
| The trailing edge's expander write fails | Until the next relay write, because `pulser.update()` fires once and nothing retries | Read in the source, not run |

**Proposal, in order of value:**

1. **Retry the trailing edge.** `io_task` rewrites the mask each pass until the write lands.
   A few lines, no wait, and it covers the case with no limit that needs no other fault.
2. **Let `app_task` stop feeding the watchdog when `io_task`'s pass count stops moving.** A
   hung `io_task` then resets the board. A few lines. `watchdog_timeout_s` sets the delay,
   10 s by default.
3. **Drop the relays from the panic path by driving GPIO 3 low**, which resets both
   expanders with no I²C. It would wrap `esp_panic_handler` at link time, since this core
   offers no hook. It needs one bench run on the analyzer, and the wrap is new risk in the
   path that reports every other fault. It shortens only the 1 s case, so **it is the one
   to leave out** unless a 1 s closure matters at the 1050.

**The operator chose 1 and 2 and left out 3 (2026-11-01). Both pass on the bench:**

- **Retry.** `relayfault off 3` failed three trailing-edge writes in an OPEN. `cmd/ack` read
  `result` 19, the retry count read 3, and the next OPEN read `result` 0 with the count
  unchanged. The count shows the writes, not the contact: no analyzer was on the relay.
- **`io_task` stall.** `relay 1 2000` then `wdt iostall`: the fault log read `io_task_stall`,
  and a `BOOT` with `reset_cause` `watchdog` came 16 s after the command, against about 12 s
  from the two timeouts. The relay's release at `setup()` is 2026-10-07's result, not
  measured again.

## 2026-11-01 — R-6.4c's overnight check: the baseline, and what the night has to answer

**The baseline is 98.9 of 100.0 Ah, read at 16:10:50 EST.** The console's `bms data`:

```text
bms: cells 4: 3338 3339 3341 3339 mV (delta 3); temps 4: 148 172 140 141 (0.1 C); pack 13350 mV 0 mA (charge flag, raw 0x4000); SOC 99%; 989/1000 (0.1 Ah); cycles 4; SOH 1000; rssi -64
```

PRD R-6.4c's premise is that this figure falls under GateLink's standing draw, which `pack_ma`
cannot show. It expects about 0.5 Ah over 12 h, and an unchanged figure falsifies it.

**Only the console can answer this tonight.** MQTT's `capacity_ah` read 100.0 at 99 % SOC,
because `bms_link.cpp` fills `bms_capacity_dah` from the nominal capacity. Remaining capacity
reaches the wire only as the fall in `ah_drawn`, and that field stays null until a BMS read
happens while charging is active. This boot had none, since the MPPT read `off` with PV at
10 mV.

**USB was on the StamPLC when the baseline was read**, and the handoff records that with 12 V
on, USB carries all but 3.1 mA of the board's load. The night's clock therefore starts when USB comes off,
not at 16:10. The operator will unplug it and start the MQTT logger on returning to the bench,
and move the bridge from the laptop's USB to its own supply at the same visit. The bridge's reboot
does not touch the BMS's count.

**The rest of the bench, at the same time:** GateLink online on the sandbox broker, RSSI about
−60 dBm. The MPPT read 13330 mV and −10 mA, and the BMS 13350 mV. Those 20 mV are the two meters'
offset at almost no current, and B1's harness measurement has to subtract them. The operator will
connect the panel in the morning, after the read.

## 2026-11-02 — R-6.4c's morning read: the BMS counted an eighth of the draw

**The BMS's remaining capacity fell 0.1 Ah across about 19.3 h on GateLink's load alone.**
PRD R-6.4c expected about 0.8 Ah at its 0.04 Ah an hour. USB came off at about 17:30 on
2026-11-01, with VIN on the pack alone, by operator report. The console read at 12:46 EST:

```text
bms: cells 4: 3329 3330 3330 3330 mV (delta 1); temps 4: 217 242 214 212 (0.1 C); pack 13310 mV 0 mA (charge flag, raw 0x4000); SOC 99%; 988/1000 (0.1 Ah); cycles 4; SOH 1000; rssi -63
```

**The falsifier as written was not met, because 989 did not stay 989.** One 0.1 Ah step still
places the fall somewhere under 0.2 Ah, and the step could have come before USB was off. Why
the BMS under-counts is not established. `pack_ma` read 0 all night, so a current deadband
in the BMS would fit, but nothing here tests one. The 44 mA draw was not measured again this
night.

**The operator chose to keep `ah_drawn` as a lower bound** (PRD v0.28). The wire field and
the firmware do not change.

**GateLink did not reset.** `~/lran-soak-r64c/bench.log` has no `BOOT` from 17:30:34 to the
read, one `boot_time`, and an uptime rising without a drop. Plugging USB into the hub for the
read did not reset the board either. `missed_polls` stayed 0 and GateLink never went offline,
at −66 to −50 dBm.

**`/bms.log` for the same stretch holds 235 windows: 230 read and 5 aborted**, about 3 h
apart. No two windows sit over 320 s apart, which fits `bms_retry_s` recovering each one.
All five show `drain_to=0`, as does the last line, and `crc`, `bad_term` and `undecoded` sum
to 0. `age_s` never passed 301. VE.Direct stayed available, with the charger `off` and PV at
10 mV.

**The logger's two gaps were the Mac sleeping**, by operator report: 17:35:54 to 17:46:57 and
17:47:02 to 17:51:08, each ending in a broker connection reset. The bridge's `tx_frames` did
not fall across them.

**The HA session's activity is all in the log** (HA engineering log, 2026-11-01): events 9101
to 9112 from 20:47:29, dry-run on at 20:49:05, the three actions at 20:50 with `result` 16,
dry-run off at 20:50:43 and Request configuration at 20:53:17. The four Hold ON commands at
19:17:44 to 19:17:49, each acked with `result` 0 and dry-run off, were the operator pressing the
button to see its state. The gate state stayed `closed`.

**Two things the log shows and nothing here explains:**

- **`config/state` was never published live.** The log holds it only as retained replays at
  each connect, with `deployed` and `poll_interval_s` set and the rest null. Request
  configuration at 20:53 was acked with `result` 0, and no document followed. That bears on
  H12's check of GateLink's `unknown` config numbers.
- **`load_on` went false at 06:53 with the panel disconnected**, and `load_ma` read 0
  throughout. `mppt-config.md` does not mention a load schedule. That bears on H12's check of
  *Gate Pier Lights*.

**The panel went on at about 12:53 and did not start the charger.** The console read PV at
13.23 V, 0 W, charger `off`, no error, against a battery at 13.29 V. Disconnected, the 30 W panel
metered 15.27 V, under heavy overcast, by operator report. Why the MPPT's terminals read about
2 V less than the disconnected panel was not looked into. **B1 and the first real `ah_drawn`
have no result**, and both wait for more light.

## 2026-11-02 — IN4's 2N7000 was bad, and a small vehicle leaving logged a false entry

**IN4 not asserting was a bad 2N7000.** It showed 200 kΩ across drain and source, by operator's
meter, and IN4 has asserted since the operator replaced it. The 4.6 V in the HA log, with the wire
off (same date), has not recurred as a problem. What damaged the part is not known.

**IN3 to IN6 then read correctly from real inputs**, from the boot at 16:06:52: `input_bits` 8 for
IN4, `fire_asserted` at 24 with IN4 still held, and `hard_shutdown` at 40, which took the gate to
`fault` until IN6 cleared.

**A small vehicle leaving logged a false entry, as the HA handoff predicted from `detect.cpp`.**
IN4, IN3, IN3 again, at 16:12:40: event 8 resolved `exit`, and the second IN3 pulse at 16:12:52
started event 9, which resolved `undetermined`. The small vehicle entering had no such problem,
because the repeat pulse comes before the crossing resolves (event 10, `entry`).

**The operator chose the firmware fix, R-3.2.3f (PRD v0.29).** After a traversal, `Settling` waits
for both sensors to stay clear for `detect_sequence_idle_ms`. Two of the five new `test_detect`
cases fail without the change. Flashed over USB from `2e50f5e`, booted at 16:16:55. The same
sequence at 16:20:54 logged event 2, `exit`, with no second detection.

**A held gate alerted from real inputs.** IN1 asserted with IN2 clear read `open_held`, `manual`,
at 16:22:21. IN4 then IN3 sent `vehicle_while_held_open`, then its `exit` follow-up at 16:23:29.

## 2026-11-02 — What stands before B3: the operator's answers

**Reviewed from `828c86a` with the operator, and no GateLink firmware changes.** The `gatelink`
image builds there, carrying the production node key and neither the master nor a sandbox key.

| Item | Operator's answer |
|---|---|
| B1, the harness | Closed as fixed, with no measurement. The first harness was an inexpensive ready-made one, never meant to be permanent, and it ran the gate with the pack, the MPPT and the 50 W panel for over a month. The new one is a full DIN splice block, with wire sized for the loads and integrated breakers and fuses |
| The first real `ah_drawn` | No bench wait. Its first non-sentinel values come at the gate, and a bug means a USB reflash there |
| B2, the simnodes | Already off, and they stay off |
| M3's polarity | Already measured at the gate. IN5 signals as SAFETY and EXIT do. IN6 is a 12 V MOSFET output with no pull-down: it floats when not driven and drives 12 V when driven. A 10K pull-down on the 2N7000's gate compensates. No voltages were given |
| IN6's off state with the 10K | Not measured yet, at GL2 |
| GateLink's parameter rows | The 2026-10-15 approval stands, and the rows added since get no separate review |
| `boot_count` | Stays unbuilt. HA counts `BOOT` events instead |
| R-6.4c's unavailable case | The firmware's reading stands: a passing cloud does not blank the value (PRD v0.30) |
| The SD card | Clear the bench files at B3, keeping `/config.json` |
| The master key's three backup drives | All encrypted |

**M3's answer matches `kInputsActiveLow`'s `0x1C`**, which inverts IN3, IN4 and IN5 and leaves IN6.

## 2026-11-02 — B3: production images on both boards, and the link authenticates

**The bridge and GateLink run production images of `cb1be00`, on the production broker.** The
operator approved the flash, and both went over USB within 36 s, from 18:12:00. USB also meant the
bridge's sandbox `OTA_PASSWORD` never came into it. Both banners read `0.1.0 (cb1be00)`, with no
`-dirty`. Each image was built from clean, after `-t clean`, because the first `gatelink` build took
7 s and was incremental.

**The images were checked before the flash, by comparing values and printing none:**

| Image | Holds | Does not hold |
|---|---|---|
| `gatelink` | The production node key, which matches `node_key.py --node 0x01` from the production master | The master, the sandbox node key |
| `heltec` | The production master, `MQTT_HOST` and `OTA_PASSWORD`, the last in plain text | The sandbox master, `MQTT_HOST` and `OTA_PASSWORD` |

**The link authenticated on the first boot.** GateLink accepted the bridge's `ROLL_CONTEXT` and
answered a `HEX_REQ`, with `refused 0` and `rx_rejected_mac` 0, at −51 dBm and SNR 11 dB. VE.Direct
reported at once, and `bms now` returned SOC 99 % at 13.31 V. `ah_drawn` read `null`, because no read
has set its reference while charging. All of this is about a minute of running each time, not a soak.

**The operator found production HA correct.** This session has no tool for production HA, and
`bench.py` watches `lran/#` alone, so it saw neither the discovery configs nor the entities.

**Config, read from `config/state`, needed no clearing.** `simnode_diag_enable` is 0 by default.
GateLink's `deployed` is 1 by override. The bridge's other overrides are the six PHY rows at
Envelope A's values and the `simnode0` to `simnode2` rows, whose values equal the defaults. GateLink
lists the six PHY rows as overrides too, at the same values.

**The card, after the operator cleared it:** `config.json` holding `{}`, `log/`, `.Spotlight-V100/`,
`.Trashes/` and `.fseventsd/`. The operator could not delete the last three from macOS, which writes
them on mount, and they need Linux. The listing gives a directory's size as 0, so it does not say
what they or `log/` hold. After the power cycle GateLink reported `sd_ok` and `config_persisted`
true, its link came back, and the bridge read it as `deployed` 1.

**The images are outside `.pio`, in `~/lran-release/cb1be00/`, mode 600.** Their SHA-256 begin:

| File | SHA-256 |
|---|---|
| `gatelink/firmware.elf` | `2fbc8e4a5610ab19` |
| `gatelink/firmware.bin` | `8e581752b7ac144d` |
| `bridge/firmware.elf` | `741b1f886d397e7e` |
| `bridge/firmware.bin` | `6ee6e3cbe3e80193` |

The bridge's files hold the production master and passwords, so they need the care `secrets.h` gets.

**Not tested:** an OTA upload with the production password, because nothing has needed one. A core
dump read against the kept ELF, because nothing has produced one.


## 2026-11-03 — The B3 soak: no reset in about 16 h, and the charger never started

**GateLink ran the production image of `cb1be00` overnight and did not reset.** Its `boot_time` stayed
`2026-11-02T23:26:00Z` in every `node/state` from 19:09 to 08:04, and `uptime_s` never fell. Production
HA's `sensor.gatelink_last_boot` still read the same value at 10:26, about 16 h after that boot. The
bridge ran two OTA uploads during the night, `6d47e6f` at 21:03 and `69fe850` at 21:05. Its counters
below start from the second.

| Item | Result, 19:09 to 08:04 unless stated |
|---|---|
| Link | RSSI −88 to −82 dBm, SNR 9 to 12 dB, `rx_rejected_mac` 0 |
| `missed_polls` | 1 at 05:34, in 777 `diag/state` reports. HA shows one more at 08:13. Each cleared on the next report |
| `rx_crc_err` | 2, at about 00:36 and 05:33 |
| Commands | 6 sent, 6 acked, 0 retries. The gate and hold commands returned `result` 16 under dry-run |
| Battery | `available` in all 777 reports, `age_s` at most 300, SOC 99 %, `pack_mv` 13300 to 13310 |
| VE.Direct | `available` in all 137 `solar/state` reports |
| Bridge radio | `rx_no_interrupt`, `rx_wake_empty` and every queue's dropped count at 0 |
| `enclosure_temp_c` | 29.0 to 31.8 |
| Events | `event_frames` 0. The gate stayed closed, with the inputs tied inactive |

**The charger did not start, so this soak does not exercise charging.** `charge_state` read `off` and
`pv_w` read 0 throughout. PV read 10 mV until 05:54, 290 mV at 06:37 and 13.15 V at 07:52. It then stayed between
13.17 and 13.28 V until at least 10:26, against a pack at about 13.30 V. The morning read of 2026-11-02
saw the same thing: 13.23 V at the MPPT, and 15.27 V from the disconnected panel. Why the terminals sit
near the pack's voltage is not established, and light may be enough to explain why the charger stays
off. So `pack_ma` under charge, `ah_drawn` and the MPPT's charge states have not run on this image.
`ah_drawn` was `null` in all 777 reports.

**The logger stopped hearing the broker at 08:04:21 and did not notice.** Mosquitto logged `Client
lran-bench-49398c6b [192.168.2.109:49572] disconnected: exceeded timeout` at 08:06:08. At 10:18 the Mac
still listed that socket as `ESTABLISHED`, with the process alive and `recv()` blocked. `bench.py` read
with no timeout, so it could wait there indefinitely. Why the broker stopped hearing the keepalive, and
why later pings did not surface an error, is not established. The ping thread was alive at 10:18.
`bench.py` now treats 90 s without a packet as a lost link and reconnects. `test_bench.py` covers that
against a fake broker only.

**`load_on` went false at 07:35**, with PV at 11.96 V. It had read true since the log began. On
2026-11-02 it went false at 06:53 with no panel connected, so the two mornings do not share a simple
PV threshold.

**Dry-run does not survive a reset.** `dry_run_` initializes to `false` in `gatelink_app.h`, and the
command handler only sets it in RAM. This bears on GL7 and R-9.2b, because relays wired with dry-run on
become live after any reset of GateLink.

**Not read or not known:** the card's `/bms.log` and `/log/fault.txt`, because this review had no
console. Whether USB stayed attached overnight is not recorded here. If it did, this soak did not test
the power chain, because USB carries all but 3.1 mA of the board's load.

## 2026-11-03 — An input mask goes in before deployment, after a review of what else the next images need

**The operator decided to add a way to disable IN3 to IN6 before deployment**, so that a repeat of the
input board's failures, such as IN4's bad 2N7000 (2026-11-02), costs those features and not GateLink.
This session changed no firmware. A later session builds the mask, and the decision still needs a
Decision Register entry.

**Before building new bridge and GateLink images, a session reviews H10's findings and the other
documents** for firmware changes that should go in the same images (operator). One revision then
carries everything, which avoids a second reflash at the gate.

**What a failed input does today**, from reading the code. None of this was run:

| Input | Failure | Effect |
|---|---|---|
| IN3, IN4 | Stage or wire open | Reads asserted, because the stage inverts (`kInputsActiveLow`). After one detection, `Settling` waits for both sensors to clear, so detection stops |
| IN5 | Stage or wire open | Reads asserted: `FIRE_ASSERTED` on the edge, and again at every boot |
| IN5 | Intermittent | `FIRE_ASSERTED` on every rising edge, with no rate limit. HA sends each as a critical push |
| IN6 | Stage or wire open | Reads clear, so a hard shutdown goes unreported |
| IN6 | Stuck on | `observe()` returns `Fault` ahead of IN1 and IN2, so the bridge's cover mapping has no open or closed state |

**Commands do not read the inputs.** `GateLinkApp::execute()` does not check them, so the relays work
through all of these failures. Pulling the input board without another change makes IN3, IN4 and IN5
read asserted.

**What exists without new firmware, and what each leaves open:**

- **HA automations off** (`lran_1_fire`, `lran_1_hard_shutdown`). They can be turned off remotely, but
  that stops the pushes only. A stuck IN6 still forces `Fault`, and a chattering input still uses airtime.
- **`inject -S,-E,-F,-A` on the console.** It needs USB at the gate, and a reset clears it. HA shows
  the inputs as clear, not as unsensed. It has not been tried for this purpose.
- **A jumper bypass.** Disconnect the failed stages, tie `EXCOM_IN3` to `EXCOM_IN5` to 0 V, and leave
  `EXCOM_IN6` open. The bypass survives a reset, and HA again shows clear. Nobody has fitted it, and whether
  `EXCOM_COM`'s 12 V reaches the StamPLC through the input board was not traced. That terminal serves
  all eight inputs, so the bypass must keep it fed.

**The proposal, which the building session may change:** an `lran-config` parameter in GateLink's
block, holding a bit mask over IN3 to IN6 that cannot name IN1 or IN2. `io_task` applies it where it
applies injection, below the debounce. Masking IN6 would then clear a false `Fault`, and masking IN5 would stop repeated
FIRE pushes. Persistence follows the card, so without a working card a reset clears the mask.

**Questions the proposal leaves open:**

- `node_flags` bit 7 covers IN5 and IN6 together (spec §7.2.8, D80), so masking either one makes both
  unavailable through the bridge. Per-input masking would need a change to that meaning.
- IN3 and IN4 have no "not sensed" indication on the wire, so a masked sensor reads clear unless the
  spec gains one.
- Unmasking an input that is still asserted is a rising edge, so it would send a new alert.
- R-3.5l says bit 7 is not set under D83, and it needs revising.

**A mask cannot protect the 1050.** Each stage reaches the 1050's SAFETY, EXIT or FIRE line through
10 kΩ. A 2N7000 failed gate-to-source would pull a line from 4.75 V to about 4.1 V, against the 1.6 kΩ
source measured on 2026-10-13. That is an estimate, and the 1050's threshold is not known. If the gate
itself misbehaves, those field wires come off.

**Falsified by:** a bench run that masks each input while it is forced asserted and chattering, and
then sees an alert, `Fault`, or a detection from it. The mask's tests should include that run.

## 2026-11-03 — The input board's netlist differs from the plan, and two 2N7000s are suspect

**[`Gatelink-Input-Board.net`](./Gatelink-Input-Board.net), rev 1.1, is now in the repository**, as the
operator's reference copy of the KiCad export.

**The board does not match Impl Plan §3.2**, and the operator confirms the 1K series resistors and the
drain-source shunts as built:

| Item | Plan §3.2 | Netlist |
|---|---|---|
| Gate series resistor | 10 kΩ | 1K (R1 to R4) |
| 1 MΩ | Gate to source, holding the gate off if the wire breaks | Drain to ground (R5 to R8) |
| Gate pulldown | On every stage | Q4 only (R9, 10K) |
| Gate zener | Optional margin | None |

**That changes three statements in this log and the plan:**

- A broken SAFETY, EXIT or FIRE field wire leaves the gate floating, so the input does not reliably
  read asserted. The plan's claim and the first table in today's *An input mask goes in* entry hold
  for an open drain wire, not for an open field wire.
- Today's estimate of 4.1 V on a 1050 line used the plan's 10 kΩ. Through the fitted 1K, a gate failed
  to source would pull the line to about 1.8 V. That is also an estimate, against the same 1.6 kΩ source,
  and the 1050's threshold is still unknown.
- `EXCOM_COM` has no net on this board, which answers the question that entry left open. The bypass
  does not touch it, and IN1 and IN2 do not pass through the board.

**The operator's measurements.** The replaced IN4 part, Q2, read 200 kΩ drain to source on a meter. With
0 V on the gate through its 1K, its drain sat at about 4.6 V. Q1 still measures 1 MΩ, the value of R5, and
"only pulls the drain down to about 1.4 V". The bench drives each input by moving a lead between +12 V
and ground by hand.

**What the numbers say.** These are calculations from R14 and R22 on the StamPLC, not measurements:

- A good stage rests at about 12 V with its gate low and near 0 V with its gate high: about 20 mV at the
  4.1 mA the plan gives.
- Q2's 4.6 V means about 2.2 mA, or about 2.1 kΩ. The meter's 200 kΩ is about a hundred times that, so
  the leak was not a plain resistance, and contamination alone is a poor fit.
- 2.2 mA through a drain-to-gate leak would put about 2.2 V on the gate across its 1K, near a 2N7000's
  typical threshold. The part would then turn itself on. That is a hypothesis about gate oxide damage,
  and no one has measured the removed part.
- If Q1's gate was high, 1.4 V at about 3.2 mA means about 440 Ω. That is far above the 5.3 Ω maximum
  the plan cites at 4.5 V, unless the gate pin was not actually high. If its gate was low, the part is nearly
  shorted. Which state the reading was taken in is not recorded.

**The cause is not established.** The stage's dissipation is at most about 50 mW, so heat from normal
operation is not a candidate. The leading candidate is electrostatic discharge or another gate overvoltage,
because Q1 to Q3 have no gate clamp and no discharge path. Q1 and Q2 carried the hand-driven crossing runs
of H12, while Q3 and Q4 were driven less. That is a correlation, and it is not proof.

**Falsified by:** a removed Q2 with no drain-to-gate leak, or a Q1 whose gate pin is low or leaking
when the reading repeats. Any untouched parts from the same batch that show low-voltage leakage would point
to the parts instead.

## 2026-11-03 — The B3 soak closes at 19.5 h with no reset, and GateLink deploys on the repaired v1.1 input board

**GateLink did not reset from its boot at 18:26 on 2026-11-02 until the soak closed at about 14:00 on
2026-11-03.** `uptime_s` read 70287 at 13:57, about 19.5 h, and `boot_time` stayed `2026-11-02T23:26:00Z`
in all 608 logged `node/state` reports. The logger lost 08:04 to 10:35, as this morning's entry records.
The figures from the restarted logger add to that entry, and they do not replace it:

| Item | Result, 10:35 to 13:59 |
|---|---|
| Link | RSSI −91 to −82 dBm, SNR 10 to 12 dB. `rx_rejected_mac` 0 |
| `missed_polls` | 0 in 203 `diag/state` reports |
| `rx_crc_err` | 3, one more than this morning's 2, in 1035 `rx_frames` |
| Commands | Still 6 sent and 6 acked, with 0 retries |
| Battery | `available` in all 203 reports, `age_s` at most 301, SOC 99 %, `pack_mv` 13300. `pack_ma` read −100 in the 5 reports from 10:56 to 11:00 and 0 otherwise |
| VE.Direct | `available` in all 82 `solar/state` reports |
| Bridge radio | `rx_no_interrupt`, `rx_wake_empty` and every queue's dropped count at 0 |
| `enclosure_temp_c` | 28.8 to 29.4 |
| Events | `event_frames` 0. The gate stayed closed |

**The charger stayed off through midday, so this soak still does not exercise charging.** PV read 13.23 to
13.30 V with `pv_w` 0, `charge_state` `off` and `yield_today_kwh` 0.00. `ah_drawn` was `null` in all 980
`battery/state` reports. `load_on` stayed false from 07:35. Why the terminals sit near the pack's voltage is
still not established.

**The new `bench.py` read timeout did not run against the real broker.** The restarted logger connected
once at 10:35 and never needed to reconnect.

**Not read:** the card's `/bms.log` and `/log/fault.txt`, and the RAM counters, because no console was
attached. The Mac listed no USB serial device at the close. That says nothing about whether USB stayed
attached overnight, so that question from this morning's entry stays open. The log is in `~/lran-soak-b3`
on the Mac.

**The operator set a new plan of record**, because the rev 1.2 input board's recommended changes are more
extensive than a hand-built perf board will survive:

1. Swap Q1 on the existing v1.1 board, and put the board back in service.
2. Make the GateLink and bridge firmware changes that bypass or disable IN3 and IN4.
3. Run a brief bench test and a soak with the new firmware and the repaired board.
4. Deploy with the repaired v1.1 board.
5. In parallel, design v1.2, review it and send it out for PCBs, which ends the hand-built perf board.
6. Pick a later date to build v1.2 and swap the input board as required.

**The gate goes back into service now, and the bridge and GateLink go offline until they are ready for
the firmware updates and testing** (operator).

**What this leaves open:**

- This morning's mask decision covered IN3 to IN6, and the plan names IN3 and IN4. Which inputs the
  firmware covers is for the review to put to the operator.
- The rev 1.2 recommendations the operator reviewed are not in this repository.
- Neither the plan of record nor the mask has a Decision Register entry yet.

## 2026-11-03 — The operator's answers: the failures came after installation, and the board never met the 1050

**The stages have never been connected to the 1050, and all of them tested good when the board was
built** (operator). So whatever damaged Q1 and Q2 happened after installation. The bench's hand-moved 12 V
lead is the only drive this log records on those stages. That does not establish a cause.

**The operator named IN3 and IN4 because they are Q1 and Q2, the parts showing failures.** No problem has
ever been observed on IN5. The drives differ like this (operator):

- **IN3, IN4 and IN5** share one type of drive. Each is a 1050 input pulled high internally to 5 V, reaching
  4.6 to 4.7 V, and driven low by an external device. The 2026-10-13 measurement read 4.75 V.
- **IN6** is different: a FET drives the output to 12 V, with no apparent pulldown when off.

**Which inputs the firmware must be able to disable stays open**, as D90. It must cover IN3 and IN4.

**The MPPT and the pack leave the bench for the gate**, because the gate does not run without them
(operator). The next bench soak needs them back, or a stand-in.

**The rev 1.2 recommendations are now in
[`LRAN-Input-Board-v1.2-Brief`](./LRAN-Input-Board-v1.2-Brief.md)**, taken from the session of 2026-11-03.
**D89 to D91 and M27 are in the Decision Register.**

## 2026-11-03 — IN5 and IN6 have no inhibit today, so D90 covers all four board inputs

**The operator recalled being told that IN5 and IN6 already have an inhibit, and the firmware does not have
one.** Three related things exist, from reading the code:

- `node_flags` bit 7 is in the spec, and the bridge marks the FIRE and hard-shutdown entities unavailable
  while it is set. GateLink never sets it (D83, R-3.5l).
- `kInputsWired` in `gate_io.h` is a compile-time constant, so changing it takes a reflash.
- The console's `inject` command needs USB, and a reset clears it.

The earlier session described the bridge's side as ready and still recommended the mask. **By the operator's
standing instruction for this case, D90 now covers IN3, IN4, IN5 and IN6**, every input that runs through the
input board. This replaces the previous entry's open question.

**The operator can replace the series gate resistors without a full rebuild**, and does not want to damage
the 1050. These estimates assume the 1.6 kΩ source measured on 2026-10-13 and about 3.1 kΩ into the StamPLC
input, from the 4.1 mA at 13.4 V in Impl Plan §3.2. No one has measured either failure:

| Failure on Q1 to Q3, at 12 V | 1K, as built | 10 kΩ |
|---|---|---|
| Gate failed to source: the 1050 line falls to | about 1.8 V | about 4.1 V |
| Drain failed to gate: the 1050 line rises to | about 6.8 V | about 5.5 V |
| Drain failed to gate: current into the line | about 1.3 mA | about 0.5 mA |

The 1050's threshold and its input ratings are not known, so neither column is shown to be safe. A
drain-to-gate leak is also the hypothesis for Q2 in *The input board's netlist*, and it is unproven.

**A v1.2 board starts from scratch, with no rework of the existing board** (operator). A hand-built perfboard
is acceptable at first build, and it is very fragile under rework.

## 2026-11-03 — R1 to R3 change to 10 kΩ on the v1.1 board

**The operator chose to change R1 to R3 from 1K to 10 kΩ at the Q1 swap**, on the estimates in *IN5 and IN6
have no inhibit today*. R4 stays 1K, because IN6's drive is a 12 V FET and not a 5 V line. **The board then
differs from `Gatelink-Input-Board.net`, rev 1.1**, which stays the record of the board as first built. Each
stage gets a recheck at about 5 V after the change, because the bench has only driven these stages from a
12 V lead.

**Falsified by:** a stage that does not switch cleanly from about 4.6 V through its 10 kΩ.

## 2026-11-03 — M27's in-circuit test: Q1's gate is damaged, and Q3's leaks too

**The operator measured all four stages in circuit, driving each input to about 13.2 V and then 0 V.** Q1
fails with gate leakage in both states. Q3, which no reading had questioned, leaks through its gate about
24,000 times the brief's 10 nA limit. Q2, the replacement fitted on 2026-11-02, is the control.

| FET | Condition | Input | Gate to source | Drain to source |
|---|---|---|---|---|
| Q1 | Original | 13.2 V | 9.51 V | 8 mV |
| | | 0 V | 403 mV | 11.43 V |
| Q2 | Replaced | 13.2 V | 13.21 V | 5 mV |
| | | 0 V | 0 | 13.12 V |
| Q3 | Original | 13.2 V | 12.96 V | 6 mV |
| | | 0 V | 0 | 13.13 V |
| Q4 | Original | 13.2 V | 12.02 V | 5 mV |
| | | 0 V | 0 | 13.12 V |

**These figures are calculations from that table.** No one measured across the series resistors as M27's
test 1 asks. They assume R1 and R3 are still the as-built 1K, and that each input was at 13.2 V when its gate
was read. A meter across R3 at 13.2 V should read about 0.24 V, which would check Q3's figure directly.

| FET | Reading | Implied |
|---|---|---|
| Q1, input high | 3.69 V lost across R1 | About 3.7 mA into the gate, or about 2.6 kΩ from gate to source and drain |
| Q1, input low | 403 mV across R1, with the drain at 11.43 V | About 0.4 mA from drain to gate, or roughly 20 to 27 kΩ. The drain's extra 1.7 V drop is consistent with that current |
| Q2 | No drop at the gate | No leakage this reading can resolve |
| Q3, input high | About 0.24 V lost across R3 | About 0.24 mA into the gate, or about 52 kΩ |
| Q3, input low | Gate at 0, drain at 13.13 V | No drain-to-gate leak this reading can resolve |
| Q4 | 12.02 V against 12.0 V from R4 and R9 as a divider | No leak near Q3's size. R9 hides anything much smaller |

**Every drain switched in both states**, with 5 to 8 mV on and more than 11 V off. The channels look sound, and
the faults are at the gate. The 1.4 V drain reading in *The input board's netlist* did not repeat. Its gate
state was not recorded, so this does not explain it.

**This answers that entry's falsifier and the v1.2 brief's.** Q1's gate pin is leaking, so the gate-damage
hypothesis stands for Q1, though leakage on the board itself is not excluded. An M27 result pointing away from
the gate would have lowered the value of the brief's changes 1 and 2, and this one does not. The removed parts
can separate the two: a part that stops leaking out of circuit puts the leak on the board.

**Q4 is the only original part without a measurable leak, and it is the only stage with a gate pulldown.** Q1
and Q2 carried H12's hand-driven runs, and Q3 was driven less, which matches how badly each part reads. Both
patterns fit a gate with no discharge path, damaged during handling. They come from four parts on one board,
and Q4's drive also differs, so they are not proof. M27's batch tests are still the check for a bad batch.

**Q3 still switches, and it is unlikely to stop on this alone.** Through R3's new 10 kΩ, a 52 kΩ leak would
leave its gate at about 3.9 V from the 1050's 4.6 V, above a 2N7000's 3 V maximum threshold. The worry is
what comes next: if the leak grows as Q2's and Q1's may have, the part fails at the gate, on a line that
reaches the 1050.

**M27 still owes** a drain-to-gate and gate-to-source reading on the removed Q2, and the leakage and threshold
tests on the removed parts and unused parts from the batch.

**Falsified by:** a meter across R3 that reads millivolts at 13.2 V, which would mean the Q3 reading came from
something other than gate current.

## 2026-11-03 — The firmware revision review: what the next images could carry

**The review proposes few additions to D90's mask, because every added change widens the soak that D89 calls
brief.** The list below is for the operator to decide. No firmware changed. Status comes from reading the
documents and the source, and `git diff cb1be00 HEAD` shows only the dry-run switch's discovery change in
`firmware/`, `lib/` and `ha/discovery/`.

**GateLink's image, which costs a walk to the gate if an item misses it:**

| # | Item | Recommend | Why |
|---|---|---|---|
| G1 | **D90's mask**, as an `lran-config` row over IN3 to IN6, applied below the debounce | Build (decided) | The questions below settle its shape |
| G2 | **Record `0x8D`'s raw reply on the card when it changes.** No firmware path requests `0x8D` today, so `bms_alarms` stays `UINT16_MAX`, and R-3.4a and R-3.4c wait on a capture during a protection event | Build, if the extra request fits the BMS window on the bench | The cold mornings that could produce the capture start now, and otherwise someone must be at the gate with a tool when one happens. It only records. Decoding it takes a later reflash, and it adds traffic to a path that has soaked clean |
| G3 | **`antenna_gain_dbi` and the envelope rows** (PRD R-4.3i) | Defer | Fleet-wide and with no function at the gate. Kept out by the operator on 2026-10-01 |
| G4 | **The freeze's later list**: `boot_count`, `BOOT`'s `input_bits`, `sd` with no card, `bms_task`'s clock, the RTC | Defer | None affects deployment. `boot_count` stays out by the operator's decision of 2026-11-02 |
| G5 | **A rate limit on repeated `FIRE_ASSERTED`** | Defer | The mask stops a chattering IN5 once someone sets it. Until then, HA sends each edge as a critical push |
| G6 | **`BMS_ALARM`** (F3's open half) | Defer | With `bms_alarms` unavailable there is nothing to pass through. G2 is the step before it |
| G7 | **Relay dry-run surviving a reset** | Leave as is | A reset that clears dry-run is a trap at GL7, and the handoff's check covers it. A persisted dry-run could leave the gate silently dead |

**The bridge's image, which can still update by OTA once it is deployed:**

| # | Item | Recommend | Why |
|---|---|---|---|
| B1 | **The mask's discovery row and `config/state`**, which come from the shared table | Build (follows G1) | The name is permanent on production, so the operator approves it first |
| B2 | **A poll right after `SET_RELAY_DRY_RUN` and `SET_BMS_POLLING` are acknowledged** | Build | The bridge handoff says to take it with the next bridge change, and this is that change |
| B3 | **The Request configuration button left GateLink's node rows null** (bridge log, 2026-10-28; freeze R3) | Investigate on the bench before GateLink's image is frozen | If the fault is on the node's side, the fix needs GateLink's image. `cmd/ack` and the frame log were never captured |
| B4 | The publish queue's share with rxlog, and retained `lran/bridge/version` | Defer | OTA can reach both later |

**For G1, this review proposes leaving `node_flags` bit 7 unset and letting HA read the mask.** The row is already in `config/state`, so HA can show "not sensed" per input with no change to any
frame. Masking IN5 then does not take IN6 down with it, as bit 7 would (spec §7.2.8, D80). R-3.5l's statement that
bit 7 is not set still holds. A spec reading under which a masked input may not read clear in
`input_bits` would falsify this and need a revision. If one is needed, §7.2.10's two wording fixes from the handoff's *Open*
go with it.

**The other D90 questions, with what the review would accept:**

- **Persistence follows the card.** A dead card plus a reset clears the mask, so a failed stage returns until
  someone sets it again. Accept this, and put it in the operator's notes for the gate.
- **Unmasking an asserted input is a rising edge**, and it sends a new alert. Accept this, because an input still
  asserted at unmask is worth an alert.
- **The default is 0**, every input live, so a new image with an empty `config.json` behaves as `cb1be00` does.

**Which checks to repeat on the new images:**

| Check | Repeat | Why |
|---|---|---|
| Clean build from a committed tree, image contents compared, banners, first-boot link | Yes | Each is B3's check of a new production image |
| VE.Direct and BMS read, `config/state` overrides, card contents after a power cycle | Yes | The new row changes the table both images read |
| The mask's run: each input masked while forced asserted and chattering | New | The entry *An input mask goes in* names it as the falsifier |
| Each stage from about 5 V, with toggle switches | New | D89, and the falsifier in *R1 to R3 change to 10 kΩ* |
| OPEN and CLOSE in dry-run, and one relay pulse | Yes | The mask changes `io_task`, which drives the relays |
| F1 to F9's bench runs | Only those whose code changes | Otherwise they would only repeat the 2026-11-01 runs on the same code |
| B3's production HA walk | Only the new entities | H10 covers the rest, and the bridge's image changes little |
| A 12 h soak with the real MPPT and pack, or a stand-in | Yes | D89 calls for a brief one, and 12 h matches 2026-10-27's. Read `/bms.log` and `/log/fault.txt` after it |
| Charging on the new image | Yes, if there is sun | The B3 soak never saw charging, and the charger's silence is unexplained |

**The review found two stale lines, both for the handoffs' *Open*.** The HA handoff says GateLink sends no
`MPPT_ERROR`, and `mppt_error_watch.cpp` sends it. GateLink's handoff still calls its ranges proposals, and the
bridge handoff records approval on 2026-10-15. The operator should confirm those ranges now, because changing one
later takes both images.

## 2026-11-03 — The operator accepts the review, and Q3 comes out with Q1

**The operator accepted every recommendation in the review** and the Q3 swap in *M27's in-circuit test*. So the
next images carry G1 and B1, the mask and its discovery row, along with B2, the poll after the two commands. G2 goes
in if the extra request fits the BMS window on the bench, and B3 gets its bench investigation before GateLink's
image is frozen. The other items are deferred or left as they are.

**D90's mask takes the review's shape:** a config row with bit 7 left unset, HA reading the row from `config/state`,
persistence on the card, and a default of 0.

**Two things are still the operator's.** This acceptance did not name the mask row, and that name is permanent on
production. It also did not confirm GateLink's parameter ranges, so that question stays open.

## 2026-11-04 — Input board v2.0: the renamed PCB's schematic passes review

**The operator renamed the PCB from v1.2 to v2.0, because it is a completely different board.** Earlier entries keep
"v1.2", and their links to the old brief name now live at
[`LRAN-Input-Board-v2.0-Brief`](./LRAN-Input-Board-v2.0-Brief.md).

**[`gatelink-input-board-v2.0.net`](./gatelink-input-board-v2.0.net), dated 2026-11-04T17:05, passed review.** Its
nets match the plan: Safety to Q1 to IN3, Exit to Q2 to IN4, Fire to Q3 to IN5, and Alarm to Q4 to IN6. J1 to J5 match
v1.1 pin for pin. Nothing has been built, so these are calculations and readings of the netlist.

**The first draft had 1M from Q4's gate to source, and the review changed it to 10K.** The alarm output does not pull
down when off (2026-10-25), so 1M let about 0.8 µA put 0.8 V on the gate and could send a false `HARD_SHUTDOWN`. 10K
restores the 80 µA margin. The operator wanted R4 to match the other stages, and 10K / 10K allows that, with 6.0 V
on the gate at 12 V and 7.2 V at 14.4 V. The brief's 4.7K adds nothing the stage needs.

**The draft's four drain 1 MΩ resistors came out.** The first draft kept them from v1.1, and they have no job there.
R5 to R7 hold Q1 to Q3's gates off.

**J1's two pins carry different returns** (operator). One takes the 1050's GND, which references the sources. The
other goes to the common terminal of all the StamPLC's relays, which references the relays that pull the 1050's inputs
low. The 1050's 12 V positive goes to `EXCON_COM`. The second pin is a wire routing choice that saves another run to the 1050, and electrically
both are the 1050's common ground. The review had assumed the second pin was the local 12 V negative, and that was
wrong. The operator's note named Q1 and Q2's sources, and the netlist ties all four to `Gnd`.

**The operator's other answers:** lifting a field wire is acceptable for taking a failed stage out, and it does not
replace D90's firmware mask. No sockets, and mounting holes go on the PCB, which is not designed yet. C1 to C4 are
50 V ceramics with 2.54 mm lead spacing, and the footprint in the netlist is a 2.50 mm pitch. That is to check during
layout, not a fault found.

**Falsified by:** Q4's gate reading 0.8 V or more with the 1050 powered and its alarm off, or a Q1 to Q3 stage that does
not switch cleanly from about 4.6 V.

## 2026-11-08 — The v2.0 PCB review: the input board is close, and the expansion board's U1 does not fit

**Both v2.0 layouts pass KiCad 10.0.7's DRC with schematic parity, and the review found work before either goes to
JLCPCB.** It read both projects in `KiCad/GateLink/` with `kicad-cli`: ERC, DRC, a fresh netlist export, the track,
via and drill sizes, and 3D renders of each side. Nothing has been built, so every finding here is a reading of the
files or a calculation.

**The expansion board's U1 footprint is a TO-220, and the OKI-78SR is deeper.** KiCad's own
`Converter_DCDC_Murata_OKI-78SR_Vertical` puts the module's body 5.4 mm above the pin row and 3.4 mm below it. At
U1's position, that body covers C7's pad 1 at (113.23, 95.40) and overlaps C6's body. This blocks assembly, and DRC
cannot see it, because the TO-220 courtyard is smaller than the part.

**Three of the brief's corrections are not in the schematic yet.** J7.1 is still on the net labelled `/G5`, and
U2's value is still `LM7803_TO220` on a 78xx symbol. Corrections 4 and 5 are in: R4 is DIO1's 10 kΩ pull-down, C6 is
22 µF on +5V and C7 is 10 µF on +12V. The brief's open questions on C1, C2, C3, C5, F1 and D2 are settled in the
layout.

**J1.2 is a second GND terminal, and that invites a short.** The VE.Direct cable carries the MPPT's +5 V on its
pin 4, which rev 0.3 §6 leaves unconnected. With a spare GND terminal and no silkscreen labels, that wire lands
on GND easily.

**Two plug-in modules can go in rotated 180°.** The Wio's two 7-pin rows accept it rotated, which puts its GND on
RF_SW and its 3V3 on NSS. The level shifter has no outline or pin-1 mark, which correction 7 asks for.

**U2's nearest output capacitor is C4, about 15 mm away.** The review recommends a 1 to 4.7 µF ceramic at U2's pin 3.

**J12's outer row sits about 3.2 mm from the board edge, against about 5.7 mm on v1.0.** The pin nets and the
footprint's orientation match v1.0's J8, which the operator rang out on 2026-10-05. Whether the right-angle header
still clears the edge and mates the same way is not checked.

**The input board's circuit matches its brief on all four stages.** Its remaining items are small: the Gerbers in
`Gerbers/` predate the last save of the PCB by four minutes, the terminal names are printed on the bottom side only,
and the schematic has one dangling wire stub. C1 to C4's 2.50 mm footprint against the part's 2.54 mm leads makes no
practical difference, which closes the brief's open check.

**The input board's connectors were consolidated after the 2026-11-04 netlist.** J1 is now a 6-way terminal, Gnd,
Gnd, Safety, Exit, Fire and Alarm, and J2 a 4-way terminal, IN3 to IN6. The gate nets are unchanged.
[`gatelink-input-board-v2.0.net`](./gatelink-input-board-v2.0.net) is now the export reviewed today.

**Both boards print silkscreen text with 0.1 mm lines**, below JLCPCB's 0.15 mm minimum. Every copper and drill size
is within JLCPCB's standard two-layer process.

The briefs list each finding and its fix:
[`LRAN-Expansion-Board-v2.0-Brief`](./LRAN-Expansion-Board-v2.0-Brief.md) and
[`LRAN-Input-Board-v2.0-Brief`](./LRAN-Input-Board-v2.0-Brief.md), each under *PCB review, 2026-11-08*.

**Falsified by:** the OKI-78SR datasheet's outline placing the body clear of C6 and C7 at U1's position, or a
first-board J12 that mates differently from v1.0's.

## 2026-11-08 — The v2.0 PCB re-review: the circuits are ready, and the silkscreen is not

**Both v2.0 boards pass ERC with no errors or warnings, and DRC with no violations and schematic parity.** The
operator revised both projects after the morning's review. This pass read the files saved at 15:25 and 15:28 with
`kicad-cli`: ERC, DRC with and without a zone refill, a fresh netlist export, pad positions, silkscreen widths and 3D
renders. Nothing has been built, so every finding here is a reading of the files.

**The expansion board's schematic carries all seven corrections, and U1 now fits.** U1 uses the OKI-78SR footprint,
and its courtyard clears C6, C7 and D2. C8 adds 4.7 µF at U2's output, a GND pour is on F.Cu, and the test points
take a 1.0 mm drill. J1.1 is now a no-connect terminal for the VE.Direct cable's +5 V wire.
[`gatelink-expansion-board-v2.0.net`](./gatelink-expansion-board-v2.0.net) is now the 15:14 export.

**The operator assembles both boards by hand** (operator, 2026-11-08). The level shifter and the Wio have no stock
footprint, and the level shifter's module does not mark its pin 1, so silkscreen labels carry each module's
orientation.

**The level shifter's labels match the netlist pin for pin. The Wio's do not mark it safely.** A "D1" label sits
beside J9.1, which is the Wio's 5V pin, while the Wio's D1 is J11.2. J1.1 has no label to say the +5 V wire parks
there.

**The input board's top labels for J2 read IN1 to IN4, and its nets are IN3 to IN6.** The bottom labels are right.
An installer working from the top would land the IN3 and IN4 wires on the IN5 and IN6 terminals.

**The saved files still show a 1.65 mm stackup on both boards, and 0.1 mm silkscreen lines.** On the input board
every text item uses them. On the expansion board six text items and both module outlines do. The operator reported
both as corrected, so the edits may sit in KiCad unsaved. The input board's Gerbers are still from 2026-11-05, and the
expansion board has none.

The briefs list each item under *PCB re-review, 2026-11-08*:
[`LRAN-Expansion-Board-v2.0-Brief`](./LRAN-Expansion-Board-v2.0-Brief.md) P13 to P16, and
[`LRAN-Input-Board-v2.0-Brief`](./LRAN-Input-Board-v2.0-Brief.md) I6 and I7.

**Falsified by:** a silkscreen Gerber, generated for the order, that shows J2 labelled IN3 to IN6 and every line at
0.15 mm or more. For the Wio finding, a seated Wio whose 5V pin is not beside the "D1" label.

## 2026-11-08 — The v2.0 rerun: both layouts are ready for Gerbers, after one label

**The operator's revisions close every silkscreen and stackup item, and the rerun found one new label error.** The
checks ran on the expansion board's PCB saved at 16:42 and the input board's saved at 16:27. Both pass ERC with no
errors or warnings, and DRC with schematic parity. Silkscreen lines are 0.15 mm on both boards, and each stackup
totals 1.58 mm. J2 now reads IN3 to IN6.

**The operator closed three expansion-board items.** J1.1 reads NC, and the installed VE.Direct cable has its +5 V
wire removed. J12's 3.2 mm from the edge matches the hand-built perfboard exactly, and the v1.0 KiCad layout, drawn
after that board was built, does not. The Wio's labels for the pins in use are enough for hand assembly.

**The new Wio label beside J11.6 reads RS_SW, and the net is `/RF_SW`.** On the input board, DRC warns that the SFTY
label touches J1's outline, which is cosmetic. Both boards still need Gerbers, and U2's pin order still needs its
datasheet check. The briefs carry these under *Rerun at 16:42* and *Rerun at 16:27*.

**Falsified by:** a silkscreen Gerber, generated for the order, that differs from these labels or widths.

## 2026-11-08 — The v2.0 final check: both boards need only their Gerbers

**Both v2.0 layouts pass ERC with no errors or warnings, and DRC with no violations and schematic parity.** The
checks ran on the expansion board's PCB saved at 16:54 and the input board's saved at 16:56. The Wio label beside
J11.6 now reads RF_SW, and the input board's SFTY label clears J1's outline. Both stackups total 1.6 mm, and both
netlists are unchanged.

**The operator confirmed U2's pin order against the MCP1825S datasheet** (operator, 2026-11-08). That closes
correction 2's last check.

**Gerbers are the only item left before ordering.** The input board's are from 2026-11-05, and the expansion board
has none. The briefs record this under *Final check at 16:54* and *Final check at 16:56*.

**Falsified by:** Gerbers generated for the order that show a label or width different from these files.

## 2026-11-08 — The v2.0 Gerbers match their boards, and both are ready to order

**Both boards' Gerbers and drill files match a fresh `kicad-cli` export of the reviewed PCB, apart from timestamps.**
The operator generated them at 17:01 and 17:02. Each set has seven Gerber layers, PTH and NPTH Excellon files and a
job file, and each `*.110826.zip` holds the same ten files byte for byte. Since the final check, the expansion
board's PCB changed only in its plot settings, and the input board's did not change.

**The two boards use different origins, and each is consistent with itself.** The expansion board plots at KiCad's
absolute origin. The input board's Gerbers and drill files both use the auxiliary origin at its lower-left corner.
Both job files state a 1.6 mm board.

**Upload the two `*.110826.zip` files.** The input board's folder still holds `input-board-v2.0-gbr.110626.zip`,
from 2026-11-05.

**Falsified by:** a fabricated board whose holes do not line up with its copper, or JLCPCB's Gerber viewer showing a
layer different from the 3D renders in this review.

## 2026-11-11 — D92's images: the mask, the panel and two card logs, built and run on the bench

**The operator decided a temporary deployment with no input board (D92)**, so field data, the low-temperature
charge inhibit above all, starts before the v2.0 boards arrive. This session built D90's mask and the operator's
additions, flashed both production images at `155a054` from a clean tree, and started a soak. The operator named
the rows on 2026-11-11: `input_mask`, `display_mode`, and `ved_log_s` for the VE.Direct card log.

**The B3 soak's card, read before the flash.** `/log/fault.txt` shows 72,001 s with no reset, a power-on boot,
then events at 337 to 390 s with `inputs` 0x20, 0x10, 0x18 and 0x1c, and another power-on boot. Those are IN6,
IN5, then IN3 to IN5 reading asserted, the pattern a missing or unpowered input board gives. `/bms.log` holds 241
reads, 6 aborted windows and 7 cap ends, and the cap ends all follow the last boot, with the pack at the gate.

**What the bench showed, harness unpowered and the MPPT and pack at the gate:**

| Check | Result |
|---|---|
| Banners and first-boot link | Both `0.1.0 (ba13ffd)`, then GateLink at `155a054`. `config/state` has no null row 10.8 s after boot |
| Boot with `input_mask` 0 | `boot: FIRE_ASSERTED present at boot, sent again`, and `event/fire_asserted` reached the **production** broker at 14:01:23 |
| `input_mask` 15 | `config/ack` says `persisted`. Debounced inputs 0x00, against raw 0x00 |
| Software reset | `cfg: 1 restored`, first input read 0x00, and no FIRE at boot |
| The mask's falsifier | `inject` set IN3 to IN6, released them, then set IN5 and IN6 (injected 0x3c, value 0x30). Debounced inputs stayed 0x00, with no alarm, event or `Fault` |
| OPEN and CLOSE in dry-run | `DRY_RUN` (result 16) each; `relay 1` measured 499.9 ms |
| `display_mode` 2, 1 and 0 | The operator saw each behave as R-4.7e says. A press lit the panel without a beep, and a second press blanked it |
| Status lines | The operator saw `VE none`, `BMS 0/0 none`, the RX line and the INA226 line |
| `/ved.log` | A line every 60 s, with sentinels while the MPPT is away |

**One fix came from the bench.** Before the first VE.Direct block, `/ved.log`'s `age_ms` printed the uptime, which
reads as a real age. It now prints `UINT32_MAX` (`155a054`).

**Not covered here.** G2's `0x8D` request has not met the pack, so whether it fits the BLE window is unmeasured.
`/bms.log`'s new `alm=` field is that window's cost. A true power cycle of the mask, the backlight's current (M12),
and IN1 and IN2 with the harness powered are still owed. `EXCOM_COM` was not metered (D92).

**Falsified by:** a `/bms.log` line at the gate whose `end=cap` follows `ans=` and a nonzero `alm=`, which would say
the `0x8D` request pushed a window past `bms_window_max_ms`.

## 2026-11-11 — The power-chain soak starts, and the mask survives a true power cycle

**The operator started the overnight power-chain soak** with the MPPT and the pack on the bench, no PV, USB off the
StamPLC, and the bridge on its own supply. GateLink powered up at 14:41 EST after a full power-down, USB and 12 V
both off, while the operator wired the production power harness. That boot is the mask's true power cycle.

**The harness logs MQTT alone.** The bridge's USB port is not on this Mac, and the operator approved an MQTT-only
run on this laptop, detached from any session so it runs unattended. The log is
`~/lran-soak/2026-11-11-power/bench.log`, started at 14:43:26 EST:

```bash
D=~/lran-soak/2026-11-11-power; mkdir -p "$D"
nohup ~/.platformio/penv/bin/python tools/bench/bench.py --dir "$D" run --mqtt > "$D/run.out" 2>&1 &
```

**The mask held across the power cycle, on MQTT and HA evidence.** HA's `binary_sensor.gatelink_fire_input` went
from `unavailable` to `off` at 14:40:49 EST and was not `on` afterwards. The bridge's config mirror reads
`input_mask` 15, source `override`. The earlier boot with `input_mask` 0 put `event/fire_asserted` on the
production broker, so a lost mask would have shown. The console's `cfg: 1 restored` line was not read, because no
console was attached.

**The soak's starting state, 14:43 to 14:47 EST:**

| Topic | State |
|---|---|
| Link | Both available. RSSI −53 dBm, SNR 11 dB, `missed_polls` 0, `deployed` 1 |
| `node/state` | `uptime_s` 119, `boot_time` 19:41:00Z, `node_mv` 13604, `boot_count` null |
| `solar/state` | Available. `batt_mv` 13600, `batt_ma` −10, `pv_mv` 30, charger `off`, no error, `charge_inhibited` false |
| `battery/state` | First window read at +200 s: SOC 98 %, `pack_mv` 13630, `pack_ma` 0, cells 3402 to 3410 mV at 17 to 21 °C, BLE RSSI −62 dBm |
| `gate/state` | `closed`, `input_bits` 0 |

**What the MQTT-only log cannot show.** It has no `BOOT` line, so a reset reads as `uptime_s` falling or a new
`boot_time`. `boot_count` is null and cannot count them. The card's `/ved.log`, `/bms.log`, `/bms8d.log` and
`/log/fault.txt` cover the rest once USB goes back in.

**Falsified by:** a `node/state` in the log whose `boot_time` is later than 19:41:00Z, or `/log/fault.txt` showing a
boot during the soak.

## 2026-11-12 — The power-chain soak closes: no reset, and two BMS windows missed outright

**GateLink ran 21.4 h through the power chain without a reset**, from its 14:41 EST boot on 2026-11-11 to
12:05 EST on 2026-11-12. Every one of the log's 924 `node/state` messages carries `boot_time` 19:41:00Z, and
`uptime_s` never fell. HA's `sensor.gatelink_last_boot` still read 19:41:00Z at 12:10 EST, after the log ended.

**`bench.py` lost the broker at 12:05 EST, and GateLink did not.** It timed out and then logged
`[Errno 65] No route to host` every 5 s, although the Mac could ping 192.168.2.52 at 12:10. HA kept receiving
GateLink's states, with `missed_polls` 0. The session stopped `bench.py` at 12:10 EST.

**Against the pass criteria in the handoff:**

| Criterion | Result |
|---|---|
| No reset after the first boot | Met. One `boot_time` throughout |
| No missed polls | Met. `missed_polls` 0 in all 1,281 `gatelink/diag/state` messages; `last_seen_s` at most 60; RSSI −60 to −43 dBm |
| VE.Direct never stale | Met. `solar/state` `available` in all 126 messages, error `no_error` |
| BMS windows reading, an aborted one on its retry | **Not met.** Two windows produced no reading, below |
| No input events | Met. No `event/` topic, `gate/state` `closed` with `input_bits` 0 throughout, and HA's `binary_sensor.gatelink_fire_input` never `on` |

**Two BMS windows produced no reading.** Reading times, taken as each message's time less its `age_s`, are 300 s
apart except at two gaps of 600 s and 609 s: the windows due about 00:49 and 05:30 EST. At 05:35:47 `age_s` reached
601 and `battery/state` went `available: false` for one minute, until the next reading at about 05:35:54. That
reading came about 6 s after the window was due, which is consistent with an aborted window read on its retry. The
MQTT log cannot say whether either missed window aborted or ran its retry; `/bms.log` on the card can. The
2026-10-27 soak had no such miss: all seven aborts read on their retry.

**Charging was not tested.** The panel was not connected, `pv_mv` stayed at or below 30 and the charger stayed
`off`, so these images have still not charged. The pack held SOC 98 % and `pack_ma` 0; `batt_mv` fell from 13600
to 13290 and `node_mv` from 13604 to 13291 over the run. Cells read 17 to 21 °C.

**Still owed before GateLink leaves the bench:** the card's `/bms.log`, `/bms8d.log`, `/ved.log` and
`/log/fault.txt`, read with USB in, which may explain the two missed windows; and IN1 and IN2 with the gate's power
harness powered.

**Falsified by:** a `/log/fault.txt` boot between 19:41:00Z on 2026-11-11 and the card read, or `/bms.log` entries
showing the 00:49 and 05:30 windows read.

**Operator notes, 2026-11-12.** The broker loss at 12:05 EST may have been the operator's: they tried to close the
Claude app before moving the Mac, and `[Errno 65] No route to host` fits a machine leaving its network. The operator
has no explanation for the two missed BMS windows; `/bms.log` is still the record that could give one. Charging
waits until GateLink is at the gate.

## 2026-11-12 — The card read: both missed BMS windows failed to connect, and a failed connect gets no retry

**The operator plugged USB in at the bench, and `bench.py attach` read the card with no reset.** The `alive` counters
kept climbing across the reads, and `/log/fault.txt`'s last boot is the soak's power-on (`cause=1`, `git=155a054`),
with no boot after it. Every hourly `counts` line on that boot reads `i2c=0 cmd=0 rel=0 push=0 drain=0 drop=0 fail=0`.
The files and the RAM `bms` line are in `~/lran-soak/2026-11-11-power/card/`.

**`/bms.log` explains both misses.** On the soak's boot it holds 270 windows: 263 `end=read`, 5 `end=aborted` and 2
`end=connect`. Every aborted window read on its retry 6 or 7 s later. The two `end=connect` windows are the misses:

| `uptime_s` | EST | Line |
|---|---|---|
| 36509 | 00:49:29 | `end=connect ms=957 scan=331 conn=611 hs=0`. Next window 302 s later, read |
| 53399 | 05:30:59 | `end=connect ms=1002 scan=391 conn=596 hs=0`. Next window 300 s later aborted at `scan=31`, and its retry read 7 s after that |

**A failed connect waits the full `bms_poll_s`, as the firmware is written.** `bms_retry_next()` returns true only for
`BmsEnd::Aborted` (`firmware/gatelink/src/bms_link.cpp:126`), so `BmsEnd::ConnectFailed` is not retried. At 05:30:59
the connect failed, and the next window aborted, so the reading came about 610 s after the last one. That carried
`age_s` past the bridge's `bms_stale_s`, and the battery went unavailable for a minute.

**G2's `0x8D` request cost each window 190 ms at the median**, `alm=` 0 to 215 ms over the 270 windows, and no
window ended `cap`. The longest window took 3072 ms, with a 2149 ms connect. This answers the 2026-11-11 falsifier
on the bench, not yet at the gate. `/bms8d.log` holds one record, at `uptime_s=302`.

**`/ved.log` agrees with MQTT.** 1,338 rows, one a minute, `age_ms` at most 999 after the first, `err=0`, `cs=0` (off)
and `inhibited=0` throughout, PV at most 3 cV.

**Falsified by:** an `end=connect` window followed by a window under `bms_retry_s` later, which would mean failed
connects are retried after all.

## 2026-11-12 — A failed BMS connect is retried once, by operator decision

**The operator decided to retry a failed connect before deployment**, after the card read showed both of the
soak's missed windows ended `end=connect`. `bms_retry_next()` now returns true for `BmsEnd::ConnectFailed` as well as
`BmsEnd::Aborted`, so such a window is tried again `bms_retry_s` later, default 5 s. A retry that ends either way
still waits the full `bms_poll_s`. The other failures, `not_found`, `handshake`, `subscribe`, `request`,
`no_answer` and `init`, are unchanged; none appeared in the soak.

`test_a_failed_connect_is_retried_once` covers it, and `bms_retry_s`'s description in `lran-config`'s table now names
both cases.

**Falsified by:** an `end=connect` line in `/bms.log` on the new image whose next window is not about `bms_retry_s`
later.

**Flashed 2026-11-12 at 13:05 EST**, the production image of `572717b` over USB from a clean tree. `/log/fault.txt`
records `boot cause=7 ver=0.1.0 git=572717b`, and the console read `cfg: card mounted; 2 restored, 0 refused`. HA
shows `sensor.lran_gatelink_inputs_not_sensed` as IN3, IN4, IN5, IN6 and FIRE `off`, so `input_mask` 15 survived the
reflash. The first BMS window, at about 13:10 EST, read in 1587 ms.

## 2026-11-12 — The temporary deployment's plan

**The operator set out the deployment's plan before the merge.**

- **IN1 and IN2 cannot be tested on the bench.** The gate's wiring harness was installed at the gate on 2026-11-11,
  so they are checked once the gate runs with the full system. A new mating harness for the bench is built before
  the v2.0 work.
- **The deployment is temporary, about two weeks.** Its goal is to test basic gate control and monitoring with the
  1050 and to collect meaningful field data.
- **When the v2.0 input and expansion boards are ready**, GateLink is pulled for the retrofit and retested on the
  bench before it is redeployed.
- **After this session** the bridge is powered down and moved to its final operating location, and the pack, the
  MPPT and GateLink are powered down and disconnected for transport. GateLink is next online at the gate.
- **HA logs the MQTT traffic**, and a separate logger on an always-on machine, the HA host Mac, is an option if
  monitoring MQTT on its own has value.

**The operator asked whether to run a live soak at the gate, and the session proposed one**: the first days at the
gate, on the bench soak's pass criteria plus charging, IN1 and IN2 and the gate commands, since none of the three
could run on the bench. That is not yet decided.

## 2026-11-12 — At the gate: IN1 and IN2 are wired to NC, and the 1050's OUT1 is disabled

**GateLink booted at the gate at 14:38 EST** (`last_boot` 19:38 UTC) on `572717b`, with `input_mask` 15. Production
HA's history from then shows `sensor.gatelink_gate` only ever `open_countdown`, `open_held` or, once, `closed` (15:23:34
to 15:24:22), never `moving` or `closed` at rest. It has read `open_countdown` since 15:24, while the gate stands open.

**The operator's at-gate checks:**

- **IN2 follows the gate; IN1 does not.** `EXCOM_COM` reaches the StamPLC without the input board, so D92's falsifier
  is not met.
- **The 1050 has lost part of its programming, and OUT1's function is disabled.** One of the board's programming
  buttons no longer works, so OUT1 cannot be reprogrammed to OPEN in place.
- **IN1 and IN2 are wired to the relays' NC contacts**, relay common to GND and `EXCOM_COM` at +12 V, so each input
  is energized while its relay is idle. The operator wired NC for an inverted sense.
- **Gate commands were sent from HA** (`hold_source` read `lran` at 14:53:30 and 15:30:49), with
  `switch.gatelink_relay_dry_run` off.
- **The first BMS windows' `/bms.log` lines are not read yet.**

**The NC wiring inverts IN1 and IN2 against the firmware and the plan.** Implementation Plan §3.2 says *Use NO*, with
IN1 and IN2 asserted when energized, and `kInputsActiveLow` (`gate_io.h`) inverts only IN3 to IN5. On NC, a closed,
idle gate reads 1/1, `open_countdown`, and a held-open gate reads 0/1, `moving`. With OUT1 dead, IN1 reads 1
throughout, so GateLink reports IN2's inverse as `open_countdown` (gate idle, open or closed) and `open_held` (moving or
counting down). The history fits: each `open_held` lasted 16 to 57 s, the length of a movement, and the gate open now
with no countdown reads `open_countdown`. **HA's gate state, cover and hold alerts are wrong while both conditions
hold**, and fixing either alone does not make them right.

**The operator decided to rewire IN1 and IN2 to the relays' NO contacts**, as the plan specifies, rather than invert
them in firmware. Common stays on GND and `EXCOM_COM` on +12 V. OUT1 still needs its function restored on the 1050
before IN1 means anything: on NO with OUT1 dead, IN1 reads 0, and a held-open gate reads `closed`.

**HA changes, by operator decision:** `automation.lran_1_fire_asserted` and `automation.lran_1_gatelink_hard_shutdown`
turned off at 17:41 EST through production's ha-mcp (D92's pre-deployment step 1). The production master's drives are
not yet confirmed encrypted.

**Falsified by:** IN1 and IN2 reading 0/0 with the gate closed and idle once the inputs are on NO, or a meter showing
OUT1's NC contact open with the gate closed.
