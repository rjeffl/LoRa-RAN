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

