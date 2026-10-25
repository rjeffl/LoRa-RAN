# GateLink Node — engineering log

**Dated record, appended to and never rewritten.** Measurements, surprises and the
things that cost an hour. Where this disagrees with a document, the document is the
current statement and this is what was true on the day. Implementation Plan §5.3 names
this file.

**Earlier entries are in a file beside this one**, moved there unedited. A document citing
an engineering-log entry by date means the file holding that date:

- 2026-10-01 to 2026-10-02, moved 2026-10-15:
  [`engineering-log-2026-10-01_2026-10-02.md`](./engineering-log-2026-10-01_2026-10-02.md)

[`docs/README.md`](../README.md#conventions) says when the log is split next.

---

## 2026-10-05 — GL0 starts: a bring-up image, and a StamPLC the Mac cannot see

**The carrier is built.** The operator reports the 5 V and 3.3 V rails clean, the netlist
buzzed out on the board, and the Wio and the level translator seated, with the antenna
connected. The module is the header board, "Wio-SX1262 for XIAO" (p-6379), not the Kit.
Of expansion board §10's remaining items: R3 and R4 are fitted, and C10 is rated 50 V.
D2 is not fitted. The P6KE18A's maker is unknown, but its marking gives a 25.2 V maximum
clamp at an 18.9 V breakdown, which is under the AP63357's 32 V input limit. VE.Direct is
not wired, so measurement M4 and §11 step 5 wait until the MPPT comes to the bench.

**No radio test ran.** `gatelink-bringup` builds and is ready to flash. It walks §11
steps 1–4 as console commands (`firmware/gatelink/src/bringup.cpp`).

**RadioLib's `begin()` undoes the DIO1 pull-down.** `SX126x::modSetup()` in 7.7.1 sets
the IRQ pin to plain `INPUT`, which on the ESP32 clears a pull-down set before it.
Expansion board §7.1.1 and plan §4.1 both said to set `INPUT_PULLDOWN` before
`radio.begin()`. That alone leaves an open DIO1 conductor floating, so the bring-up
image sets the pull-down again after `begin()`, and both documents now say so. The
simnode's `radio.cpp` never sets a pull-down, and the Kit's DIO1 is a board trace.

**`SD.begin()` would start the shared bus on the wrong pins.** M5StamPLC's
`sd_card_init()` passes the global `SPI` to `SD.begin()`, which starts an idle `SPIClass`
on the board definition's default pins. On `esp32-s3-devkitc-1` those are G11–G13: BUSY,
the LCD's chip select and the internal I²C's SDA. The bring-up image calls
`SPI.begin(7, 9, 8)` before `M5StamPLC.begin()`. M5StamPLC leaves the SD card off by
default, which is why L6 never met this. M5GFX drives the panel in 3-wire SPI on the
same host, and its `endTransaction()` restores full-duplex for Arduino's SPI users.
Step 4 tests whether that is enough.

**With the 12 V supply on, the StamPLC's USB never enumerates.** No `/dev/cu.usbmodem*`
appears, and `ioreg -p IOUSB` lists no device at all. The schematics account for it,
though M5Stack's documentation does not mention it. The StampS3's USB-C VBUS reaches its
5 V rail through a 1 A PPTC with no diode (`Sch_StampS3_v0.3.3`), and the StamPLC feeds
that pin from `SYS_5V` through FU3 (`K141_sch_StamPLC_V10_CPU`). The board therefore
holds about 5 V on VBUS before the cable is plugged in. A USB-C host applies VBUS only
after it sees 0 V there, so the Mac never attaches. That last step is an inference from
the Type-C specification, not a measurement. **Falsified by:** a USB 2.0 hub with USB-A
ports between the Mac and the StamPLC, which has no 0 V check. If the port still does
not appear through the hub, the cause is something else. The carrier cannot be the
cause: the StampS3's USB is on G19 and G20, which reach neither the bus header nor the
Grove ports. GL0 waits for that hub.

## 2026-10-06 — GL0: the radio answers on the carrier, and R4 does not hold reset

**Through a USB 2.0 hub, the StamPLC enumerates with the 12 V supply on.** It appeared
as `/dev/cu.usbmodem1101`. That is the outcome the 2026-10-05 entry's falsification
check predicted, so the VBUS backfeed explanation stands. The bring-up image flashed
from `1e5048a`, and the banner reads `0.1.0 (1e5048a)`.

**§11 steps 2 and 3 pass.** Console output, from `tools/bench/bench.py`:

```text
reset: BUSY in reset high; after release low in 1619 us
begin: RadioLib status 0 - radio up
begin: 917400000 Hz, SF9, BW 125 kHz, CR 4/5, -4 dBm conducted, 3.0 dBi antenna
radio: version "SX1261 V2D 2D02" (status 0), sync word 0x1424 (status 0, expect 0x1424)
tx: #1 time on air 164864 us; DIO1 edge SEEN at 171385 us; IRQ 0x0001, TX_DONE set
```

The DIO1 edge arrived on the first transmit, so the PORT.A white conductor carries the
IRQ. The version string reads "SX1261" on an SX1262 as well; the part answered over SPI,
and the read-back sync word matches.

**R4 does not hold the radio in reset.** Right after boot, with G2 set to `INPUT`,
`pins` reads NRESET high and BUSY low:

```text
pins: NSS G41 high (expect high, R3)
pins: NRESET G2 high (expect low, R4)
pins: BUSY G11 low (expect high while in reset)
pins: DIO1 G1 low (expect low, pull-down)
```

A low BUSY means the radio really was out of reset, so this is not a threshold reading
near mid-rail. Something on the RST net pulls up harder than R4's 10 kΩ pulls down.
The candidates are a pull-up on the StamPLC's PORT.A or one on the Wio header board;
neither is confirmed. Firmware drives RST from `begin()` on, so bring-up is not blocked,
but expansion board §7.1's fail-loud reset at boot does not hold as built. **Falsified
by:** the RST voltage to GND at idle after a boot. A reading near 0 V means the `pins`
reading is wrong; otherwise the opposing pull-up is 10 kΩ × (3.3 − V)/V.

**Not run:** `txloop 50` with the 3.3 V rail on a meter, `sd` and `bus 60`. The board
moved to the workbench. The operator is weighing dropping the microSD card, because it
is a liability in an enclosure without climate control. That would leave NVS as the only
nonvolatile store.

## 2026-10-06 — The RST pull-up is on the Wio, and R4 cannot win against it

**With the board powered and G2 not driven, the RST net reads 2.3 V.** The operator
measured it on the carrier. It still reads 2.3 V with the PORT.A cable unplugged, so the
pull-up is not on the StamPLC. Only R4 and the Wio remain on the net, so the Wio pulls
RST up through about 10 kΩ × (3.3 − 2.3) / 2.3 ≈ 4.3 kΩ. Whether the resistor is on
Seeed's header board or inside the module is not known. Finding out means taking the
carrier out of the enclosure to pull the Wio, and that was not done. R4 could not be
measured in circuit with the Wio seated.

**R4 stays fitted at 10 kΩ.** Winning against 4.3 kΩ cleanly needs about 470 Ω, which
draws 7 mA whenever G2 drives high. Instead, the GateLink driver checks RST at boot: it
drives RST low and requires BUSY to rise, then releases RST and requires BUSY to fall.
Expansion board §7.1 and plan §4.1 now say so.

**SD testing is deferred**, along with the decision on whether to keep the card. Dropping
it would move persistence to NVS, which changes Protocol Spec §8.11's meaning of
`APPLIED_NOT_PERSISTED` for GateLink (D49). It would also leave the M14 baseline log and
D29's seasonal temperature log without a local store.

## 2026-10-06 — The 3.3 V rail holds through transmit at −4 dBm

**The 3.3 V rail read 3.32–3.33 V through `txloop 50` and `txloop 200`.** The operator
may have seen one brief dip to 3.31 V, possibly from poor probe contact. GL0 requires
≥3.2 V. All 250 transmits completed with a DIO1 edge and `TX_DONE`, at Envelope A's
−4 dBm conducted.

The meter was a DMM, which averages, so it cannot show the dip at each PA turn-on.
Each loop is about 172 ms, of which 165 ms is time on air, so the reading is close to
the rail voltage during transmit. A scope on the rail at the start of a transmit would
show the turn-on dip. The operator notes that the Wio is the only switching load on the
rail, and reads the result as the 100 µF capacitor doing its job.

**`bus` runs without a card now.** It required `sd` first. With the card's future
undecided, it runs the LCD and radio tasks alone when no card is mounted.

## 2026-10-06 — The LCD and the radio share the bus cleanly, without a card

**`bus 60` ran with no microSD card mounted**, on image `1d9f746`:

```text
bus: 60 s; LCD 4000 frames; SD off 0 ok 0 bad; radio 7989 ok 0 bad; tx after ok
```

The operator watched the panel throughout. The text stayed legible with no corruption.
The panel flashes because the test alternates navy and black fills at about 67 frames a
second, which is intended. This passes §11 step 4 for the LCD and the radio only. The
microSD leg waits on the decision whether to keep the card.

## 2026-10-06 — §11 step 4 passes with the microSD card, once the test stops starving IDLE0

**The 128 GB card would not mount until it was formatted on the board.** `SD.begin()`
failed with FatFs `(13) There is no valid FAT volume`. That error comes after the card
has initialised and its first sectors have been read, so the wiring was good. The core
builds FatFs with `FF_FS_EXFAT 0` (`ffconf.h`), and a card over 32 GB ships as exFAT.
`sd format` passes `format_if_empty` to `SD.begin()`. FatFs made a FAT32 volume of
121,942 MB in 61 s over SPI at 4 MHz, with the operator's permission to erase the card.

**The first two runs with the card tripped the task watchdog on IDLE0**, at 5 s and at
9 s. Each time `bus_lcd` was running on CPU 0, busy-waiting in M5GFX's
`Bus_SPI::writeDataRepeat()`. The suspected cause, an LCD clock that an SD transaction
left at 4 MHz, is ruled out: the longest LCD hold stayed at 14.3–14.6 ms throughout. Every
driver busy-waits on its own transfer, and the lock passes straight from one task to
the next, so the bus never idles. Three unpinned tasks at priority 2 kept IDLE0 off
CPU 0. No transfer failed in either run. The bus tasks now run on core 1 at priority 1.

**With that change, all three legs pass:**

| Run | LCD | SD | Radio | Transmit after |
|---|---|---|---|---|
| `bus 60` | 1,592 frames, max hold 14.6 ms | 1,588 ok, 0 bad, max 59.2 ms | 1,596 reads, 0 bad, max 0.23 ms | ok |
| `bus 5` | 135 frames | 133 ok, 0 bad | 134 reads, 0 bad | ok |
| `bus 20` | 532 frames, max 14.6 ms | 530 ok, 0 bad, max 51.1 ms | 531 reads, 0 bad, max 0.23 ms | ok |

The operator watched the panel through `bus 20` and saw no corruption.

**An SD write holds the bus for up to 59 ms**, and a radio operation waits behind it.
That bears on plan §5.2's ACK-timing question at GL3, if the card stays.

**The console holds back the last line of a long command.** After the 60 s and 20 s
runs, the summary line appeared only when the next command produced output. The 5 s
run's summary arrived at once. The cause is not established; it looks like the USB CDC
transmit buffer not being flushed. Send `stat` to flush it.

## 2026-10-06 — GateLink keeps the microSD card; the Wio's 10 kΩ does not explain 2.3 V

**The operator has decided to keep the microSD card.** Nothing in the plan or the
specification changes, since both already assume it. The bring-up `bus` test still runs
without a card, for a bench with none fitted.

**The Wio's schematic shows a 10 kΩ pull-up on RST.** Against R4's 10 kΩ, that alone
would hold RST at 1.65 V, not the 2.3 V measured. The measured 2.3 V implies about
4.3 kΩ of pull-up in total, so something else supplies about 7.7 kΩ in parallel. Two
candidates fit, and neither is checked: a pull-up inside the SX1262 on NRESET, or an R4
that is not 10 kΩ. An R4 of about 23 kΩ against the Wio's 10 kΩ alone would also give
2.3 V. **Falsified by:** R4 measured out of circuit, or RST measured on the carrier with
the Wio pulled. Neither changes the conclusion that R4 cannot hold reset. Expansion board §7.1
now cites the Wio's 10 kΩ.

## 2026-10-06 — GL1 starts: the relay expander does not reset with the ESP32

**A reset in the middle of a relay pulse lengthens it.** The four relays are P0_0–P0_3 of
the AW9523B at 0x59 on the internal I²C bus (M5StamPLC 1.2.0, `io_expander_b_init()`).
That chip sits outside the ESP32's reset domain, so a watchdog reset or a panic leaves its
output latch as it was. The relay stays energized through the boot ROM, `setup()`'s 200 ms
banner delay and `M5.begin()`, until `io_expander_b_init()` makes every pin an input.
PRD R-3.5j asks that no relay energize from reset; this is a relay that never
de-energized. Found by reading the installed library, not on the bench.

**The library's own sequence would also close a relay briefly.** For each relay pin,
`io_expander_b_init()` calls `pinMode(OUTPUT)` before `digitalWrite(false)`. With a 1 in
the latch, the relay is driven between those two I²C transactions.

**`board_relays_off_early()` is now the first call in `setup()`**, in the node image and
the bring-up image. It writes 0 to the output latch over a raw I²C write before anything
else runs. That ends the stretched pulse at the first instruction `setup()` executes, and
leaves a 0 in the latch for the library's sequence. It does not cover the boot ROM and
the core's start-up, before `setup()`. **Falsified by:** the GL1 scope check, a relay
output observed through a watchdog reset fired during a pulse.

**The library names the INA226's current the "IO socket output current"**
(`getIoSocketOutputCurrent()`, `M5StamPLC.h`). That names neither of the two readings
plan §3.4 weighs, the bank's current or the node's. M12 is still open.

## 2026-10-06 — GPIO 3 holds the IO expanders in reset after a chip reset

**The first bench run of GL1's image found the boot-time relay-off write unacknowledged**
after the upload's reset (`rst:0x15`, `USB_UART_CHIP_RESET`) and after a power-on with
USB unplugged. It was acknowledged after a software reset and after four
interrupt-watchdog resets (`rst:0xc`, `RTC_SW_CPU_RST`), three of them during a 2 s pulse
on K4. Clocking SCL to free the bus did not change it, and neither did the core's `Wire`
in place of M5Unified's `In_I2C`.

**A bus scan at the start of `setup()` found the cause.** After a chip reset, only 0x32
(RX8130), 0x40 (INA226) and 0x48 (LM75) answer. With GPIO 3 driven high, 0x43 (expander
A, a PI4IOE5V6408 by M5Unified's board table) and 0x59 (the relay expander) answer too,
and the write is acknowledged at once. GPIO 3 is the LCD's reset (`STAMPLC_PIN_LCD_RST`),
and M5GFX drives it inside `M5.begin()`. A CPU reset leaves it high, which is why those
resets behaved.

**What this means for PRD R-3.5j:**

- After a power-on, brownout or USB reset, the relay expander sits in hardware reset until
  GPIO 3 rises. Its outputs are then whatever its reset state gives, so the boot ROM is
  covered by hardware if that state leaves the relays off.
- After a CPU reset, the expander keeps its latch, and `board_relays_off_early()` clears
  it at the start of `setup()`. The boot ROM and the core's start-up remain uncovered.
- M5GFX pulses GPIO 3 again inside `M5.begin()`, which resets the expander to its
  defaults before `io_expander_b_init()` turns each relay pin to an output and then writes
  it low. The latch's reset default therefore decides whether a relay closes briefly
  there. The datasheet's default was not checked. **Falsified by:** the scope through a
  power cycle, which is GL1's R-3.5j check.

`board_relays_off_early()` now drives GPIO 3 high before the write. After the upload's
chip reset and after an interrupt-watchdog reset during a 2 s K4 pulse, the console
reported `boot relay-off ok` and `relays 0x00`.

**Pulse widths timed from the expander writes:** 499.9 ms at the default 500 ms and
99.9 ms at 100 ms. A pulse sent 0.2 s after K1's trailing edge was refused, as
`relay_min_spacing_ms` requires. The scope measurement is still to come.

**A 12 V power cycle with USB attached resets nothing.** USB VBUS keeps the StamPLC
running, and the operator saw the carrier's 3.3 V LED stay lit. The carrier draws only
from Bus pin 1, so something reaches that pin from USB, or the supply was not fully off.
Not investigated.

**The INA226 read 12,144 mV and 0 mA** with the StamPLC and carrier running and USB
attached. That fits the library's "IO socket output current", but the board may have
been drawing from USB, so it does not settle M12.

**`beep` printed `ledc_get_duty(745): LEDC is not initialized`** on its first use.
Whether the buzzer sounded was not recorded.

## 2026-10-06 — The buzzer sounds

**The operator heard a clearly audible beep during the session** but wasn't watching the
bench. The only `beep` sent was the one that logged `LEDC is not initialized`, and no
button was pressed, so that command most likely made it. The message looks like a log
line from the first `tone()` call, not a failure. Not yet confirmed by a `beep` sent
while someone is listening.

## 2026-10-06 — The node image shares the SPI bus cleanly from its own tasks

**GL1's concurrency criterion passes on the node image** (plan §8.2). `bus <s>` drives
the LCD from `ui_task`, the microSD from `log_task` and the radio from `lora_task`, each
at its plan §5.2 priority and unpinned, all under the `SpiLock`. Each device checks its
own transfer. The radio reads its sync word back every 5 ms, and the microSD takes one
append on each 20 ms `log_task` pass.

| Run | LCD frames, longest call | microSD ok / bad, longest call | Radio ok / bad, longest call |
|---|---|---|---|
| 60 s | 600, 25.0 ms | 2100 / 0, 23.8 ms | 7790 / 0, 15.5 ms |
| 300 s | 3000, 36.2 ms | 9007 / 0, 55.9 ms | 35764 / 0, 49.1 ms |

Neither run reset the chip or tripped the task watchdog, and `io_task` kept its 100 ms
period through both: its count in the 30 s `alive:` lines rose by 300 each time. A longest call includes the wait for
the lock, so the radio's 49 ms is mostly a microSD write ahead of it. The bring-up
image's IDLE0 starvation did not appear, because each task here sleeps between
operations rather than taking the lock straight back.

`radio.cpp` brings the SX1262 up in `lora_task` at boot, with RadioLib status 0.

## 2026-10-06 — M12: the INA226 sees neither the node's current nor Bus pin 1's

**The INA226 reads VIN, but its shunt carries neither the node's own supply nor the
carrier's draw through Bus pin 1.** The operator put a DVM in series with the 12 V feed
to VIN, with USB unplugged, and read the panel's INA226 line alongside it:

| Condition | DVM, 12 V feed | INA226 |
|---|---|---|
| StamPLC and carrier | 43.6 mA | 11,870 mV, 0 mA |
| Carrier unplugged | 41.2 mA | — |
| Carrier, plus 2.2 kΩ from Bus pin 1 to GND | 49.2 mA | 0 mA |

The carrier draws 2.4 mA, and the resistor adds 5.6 mA, close to the 5.5 mA that 12 V
across 2.2 kΩ predicts. The INA226 did not move off 0 mA for either. The library's
calibration (10 mΩ, 2 A full scale) resolves about 61 µA, and the part's offset is about
1 mA at most, so a 5.6 mA load through its shunt could not read 0. The panel showed whole
milliamps at the time.

M5StamPLC 1.2.0 calls the reading the current of "the right side io socket." The StamPLC
has one expansion socket, and the carrier is in it, so that name points at some output
on the socket other than Bus pin 1. Which one was not established.

**What this settles.** Plan §9.8, V-11 and PRD R-4.4b read the INA226 as the node's own
supply current, and expansion board §7.7 as the bank's current. Both are wrong. The bus
voltage, which reads VIN, is still usable as the bank's voltage behind the inline fuse.
The node's consumption needs another source, and choosing it is a requirement change, not
a fix made here.

**With USB attached, the 12 V feed carries 3.1 mA.** USB supplies almost all of the
StamPLC's load even with VIN at 12 V. That is why the first INA226 reading, 0 mA with
USB attached, settled nothing, and it fits the 12 V power cycle that reset nothing.

## 2026-10-06 — The inputs debounce against a bench switch

**`io_task` debounces a bench switch as plan §8.2 asks.** The switch fed 12 V+ to IN8,
with `EXCOM_COM` on 12 V−, which is the high-level wiring plan §3.2 gives IN1–IN4. At
lran-config's defaults, `input_poll_ms` 100 and `input_debounce_samples` 2, the debounced
value followed each raw change one poll later.

`in` now counts bit changes before and after the debouncer. Ten taps, as short as the
operator could make them, gave **20 raw edges and 16 debounced**. The poll sampled every
tap, and the two that lasted a single sample were rejected. A bounce shorter than
`input_poll_ms` falls between samples and never reaches the debouncer at all.

**IN8 is bit 7 and IN1 is bit 0**, matching M5StamPLC 1.2.0's `_in_pin_list`. The switch
was first wired to IN8 while meant for IN1. With the board mounted upside down, its
terminal labels read the other way round.

## 2026-10-07 — The StamPLC schematic answers M12, and R-4.4b drops node current

**The schematic settles what M12 measured.** VIN reaches the StamPLC's system rail
through a power MOSFET and a Zener, which protect against reverse voltage and transients,
and nothing on that path passes through the INA226's shunt. The shunt sits on the 5 V
output rail alone. GateLink does not use that rail, so the reading is 0 mA in this
application, which is what M12 saw.

**Reading the schematic first would have saved the bench time.** Five minutes with it
answers M12. The bench session took over half an hour and ended in the same place. A
measurement that asks what a host's own sensor sees starts from that host's schematic.

**The operator amended PRD R-4.4b (v0.17).** The node publishes VIN from the INA226 and
`node_ma` as its unavailable sentinel. No other current sensor is planned: node current
was a nice-to-have, never a requirement. V-11 now compares the BMS's overnight ΔSOC, which
is the whole bank's consumption, gate operator included, against the budget.

## 2026-10-07 — GL1's relay checks on a logic analyzer: pulses, power cycles and a watchdog reset pass

**The method.** A HiLetgo USB logic analyzer ran PulseView on the operator's Linux laptop,
sampling at 1 MHz. Each relay's COM went to analyzer GND, and its NO went to a channel with
10 kΩ to a pull-up supply, so a channel reads low while its contact is closed. The board ran
`main`'s image (`3beb863`). **The analyzer sees the contact, not the coil.** A coil
glitch shorter than the relay's operate time does not show, and every result below has
that limit.

**The pull-ups need their own supply for anything that powers the board down.** They were
first fed from the Wio's 3.3 V, so every channel would have read low the moment the board
lost power. A 5 V wall adapter replaced it for the power cycles.

**Pulse widths, contact to contact, at the default 500 ms:**

| Relay | Analyzer | Firmware (expander writes) |
|---|---|---|
| K1 | 499.3 ms | 499.9 ms |
| K2 | 499.3 ms | 499.9 ms |
| K3 | 499.3 ms | 499.8 ms |
| K4 | 498.2 ms | 499.9 ms |

All four are inside 500 ms ±10 ms with operate and release times included. One
single-sample low showed on K3 when K4 operated. The operator judged it noise from the
breadboard connections. A 1 µs low is far shorter than a contact can close, so it is not
K3's contact; coupling from K4's switching is likely but not shown.

**Five power cycles with USB unplugged: no channel went low.** The board came back with
`relays 0x00; boot relay-off ok`. This answers the 2026-10-06 entry's open question: the
relay expander's reset default, through M5GFX's GPIO 3 pulse inside `M5.begin()`, closes
no contact.

**A watchdog reset mid-pulse: no relay energized from the reset, but K4 held through it.**
`relay 4 2000`, then `hang` 0.57 s later. The interrupt watchdog reset the board
(`Reset: interrupt_watchdog`), and K4's contact stayed closed for 1262.46 ms. By the
console's timestamps that puts its release at about the moment `setup()` ran, so
`board_relays_off_early()` released it, not the reset: a CPU reset leaves the expander's
latch alone. No channel went low after K4's pulse. R-3.5j is met as worded, but the same
mechanism lengthens a pulse. A hang 100 ms into a 500 ms pulse would hold the relay for
the watchdog timeout plus the reboot, and a hang in `io_task` itself would hold it until
the watchdog fired. The panic output also printed `Re-entered core dump! Exception happened
during core dump!` before rebooting.

**The brownout leg was not run.** The bench supply adjusts from 5.25 V to 17 V, and at
5.25 V the StamPLC's MP4560 still regulates: no reset and no glitch. No supply on hand
reaches the board's reset point. The operator closed GL1 with the leg open. A VIN sag that
deep means the LiFePO4 BMS failed to cut the battery's output at low charge. A
catastrophic battery or BMS failure is beyond the scope of any failsafe operation.
**Falsified by:** a VIN sag through the reset point on the analyzer, if the brownout leg is
ever run. The 2026-10-06 entry's premise, that a brownout puts the relay expander into
hardware reset as a power-on does, is unmeasured.

**`bench.py run` exits when a port disappears and a command is then written to it.** USB
was unplugged for the power cycles. The read side logged `reopening` once a second, and
the next `send` raised `PortNotOpenError` at `bench.py:259`, which stopped the harness.

## 2026-10-07 — GL3's node image on the air: BOOT, the context roll, polls and PING pass

**The first node image's frames never left: every CAD hung the radio.** `lora_task` queued
the `BOOT` status and event at boot, and the console showed status `seq` 3 with no frame
transmitted. The new `radio` line gave `cad err 2`, `begin fails 2` and `last error
startReceive -707`. A CAD started cleanly, `CAD_DONE` never arrived within 500 ms, and
every command after it failed. Each failure dropped the frame, so the outbox emptied with
nothing on the air.

**The bring-up image's new `cad` step placed it outside the firmware.** With no tasks and no
panel traffic, `begin` succeeded and read back the version and sync word, and then a CAD
let BUSY fall at 5.6 ms with no DIO1 edge and IRQ `0x0000`. A `tx` straight after a fresh
`begin` failed the same way, though GL0 recorded 250 clean transmits on 2026-10-06.
`startChannelScan` afterwards returned `WRONG_MODEM`: the chip read back packet type GFSK,
its power-on default, so the radio was resetting when its RF chain powered up. The
operator found a power problem at the expansion board. With it fixed, `tx`, three CADs
from standby and from receive, and `txloop 5` all completed, each CAD with `CAD_DONE` at
20–25 ms.

**-707 is `SPI_CMD_FAILED`, not `SPI_CMD_TIMEOUT`**, which is -705. `radio.h` and
`bringup.cpp` said otherwise from GL1, and are corrected.

**On the air with the bridge, no broker.** The bridge restarted when the harness opened its
port, at 0.0 s below:

| t (s) | Bridge | Reads as |
|---|---|---|
| 1.44 | `rx peer=0x01 type=4 schema=16 seq=1`, `config: 01 rebooted` | The `BOOT` `STATUS` (spec 10.7) |
| 1.85 | `rx type=5 schema=17 seq=2`, `gatelink online` | The `BOOT` event |
| 2.05–2.86 | `tx type=1`, `rx type=2`, `roll: 01 rolled to ctx 0xe9f955c7 after 1 attempt(s)` | `ROLL_CONTEXT` and its ACK (spec 10.6) |
| 23.78–24.80 | `tx type=3`, `poll: 01 answered in 966 ms` | A scheduled `POLL`, answered with schema `0x10` |

GateLink's `lran` line read the same `ctx_id`, `0xe9f955c7`, and no refusal. The bridge's
type 8 frame at 18.92 s went unanswered, as expected: GateLink does not take `CONFIG` yet.

**PING from the simnode, identity `0xF0`, all echoed:** 32 bytes in 901 ms; 202 bytes
`pattern` in one 222-byte frame, 2460 ms; 202 bytes `pattern` in five 48-byte fragments,
five back, 4130 ms. RSSI −59 dBm, SNR 11.0 dB. This is GL0's ping and loopback item.

**Not run: every check that needs a `COMMAND`.** The broker at 192.168.2.52 was down, and
the bridge takes commands from MQTT alone. The ACK after the pulse, dedup of a retry and
the context resync wait for it.

**`Reset: unknown` after the harness opens the StamPLC's port.** The USB-serial-JTAG reset
is not among `main.cpp`'s cases, and `reset_cause()` reports it as `UNKNOWN` in the `BOOT`
event. GL3's reset-cause slice should name it.

## 2026-10-08 — M4 by DVM: the MPPT's TX has a weak high side; its low side is unmeasured

**The MPPT is the gate's 75/15, brought to the bench** and powered from a bench supply
with no PV connected. The gate runs from its battery meanwhile. The operator saved the
charger's settings from VictronConnect beforehand.

**The operator measured J4 pin 3, the MPPT's TX, with a DVM, not a scope:**

| Load on pin 3 | Reading |
|---|---|
| None | about 3.2 V |
| 10 kΩ to GND | 1.1 V |

The line idles high between 1 Hz text blocks, so both readings are the high level. They
put the high side's source impedance near 19 kΩ, from 2.1 V dropped at 0.11 mA. That is
weaker than plan §4.2.2's 10–11 kΩ, which came from Victron's 22 kΩ load figure. The
unloaded 3.2 V agrees with expansion board §7.4's 3.25 V.

**D25 is still open.** It turns on the low side, which a DVM cannot see on a 1 Hz stream.
A strong low with this weak high is the case plan §4.2.2 calls moot: the BSS138 works. A
low as weak as this high would sit near 3.3 V against the converter's 10 kΩ pull-up to
5 V, and no data would arrive. The bring-up image's `ved raw` tests the carrier's own
path instead, which is the question D25 exists to answer. Checksummed blocks through
J4 settle it for this carrier; nothing, with the cable metered, is D25's failure case.

## 2026-10-08 — VE.Direct works both ways on the carrier; the BSS138 passes the MPPT's lows

**The first build of the harness crossed TX and RX.** J4 is a 2×2 header and socket, not
the JST of expansion board §2, and it was wired to §6's pin numbers through the factory
VE.Direct cable, which is a crossover. The MPPT's TX landed on HV3, our TX channel. `ved
raw` saw no bytes. `ved edges`, which samples G4 and G5 as inputs with the UART released,
counted 1623 falling edges in 3 s on G5 and none on G4. The operator swapped the crimps of
pins 2 and 3, and §6 now names J4's pins for the MPPT pin each reaches.

**D25 is answered for this unit: the BSS138 stays.** Over the crossed cable, the MPPT's
lows passed channel 3 against its 10 kΩ pull-up, and over the corrected one they pass
channel 4 and R2. Both reached the ESP32 as clean lows, the shortest 45 µs against a 52 µs
bit. The weak 19 kΩ high side of the earlier entry pairs with a strong low side, which is
the case plan §4.2.2 calls moot. M4 was not run as specified, with a scope on a 10 kΩ
load; this is the carrier's own path doing the job instead.

**Text.** One block every 1000 ms, 19 fields, every block read continuously passed its
checksum, and `decode_mppt()` left none unparsed. The block carries `OR` and `H23`, which
neither osh-labs nor spec 7.2.2 defines. PID `0xA075`, FW `175`, battery 13.36 V at
−40 mA, `CS` 0 and `OR` `0x00000001` with no PV. One block, byte for byte, is now
`kCaptured` in `lib/vedirect`'s text tests.

**HEX round-trip.** Ping answered `0x4175`, AppVersion the same, and ProductId `0xA075`,
which agrees with the text block's PID. Each answered in 8–13 ms. Gets of BF-30's ten
registers and of `0x0201` answered in 14–15 ms, one in 111 ms with a text block in the
way:

| Register | Raw | Reads as |
|---|---|---|
| `0xEDF7` absorption | 1420 | 14.20 V |
| `0xEDF6` float | 1350 | 13.50 V |
| `0xEDF4` equalisation | 0 | 0.00 V |
| `0xEDFD` auto equalisation | 0 | off |
| `0xEDF2` temperature compensation | 0 | off |
| `0xEDF1` battery type | `0xFF` | user defined |
| `0xEDF0` maximum charge current | 150 | 15.0 A |
| `0xEDFB` absorption time limit | 200 | 2.00 h |
| `0xEDEA` system voltage | 12 | 12 V |
| `0xEDE0` low-temperature cut-off | 500 | 5.00 °C |
| `0x0201` device state | 0 | off |

The simnode's simulated MPPT holds 1420 in `0xEDF4`; this unit reads 0.

**The first HEX contact sets off an Async burst.** The scan's first Get, of `0x0100`,
drew about 25 unsolicited `:A` frames and no reply inside 1000 ms. The same Get answered
`0xA075` on the next try, and the second scan answered 12 of 12. The node's VE.Direct
task should expect the burst and retry a timed-out Get. Async frames also follow single
requests; the console discarded each as unmatched without losing its transaction.

**Three blocks failed their checksum, and the console caused them.** Nothing reads
Serial1 between console commands, so its 1 KB buffer overflows and loses bytes. Every
failure followed such a gap.

## 2026-10-08 — The readback matches VictronConnect, and M4 closes in circuit

**The operator compared the scan with the VictronConnect iOS app** on the Battery settings
page, and every value the app shows matches. The app showed no equalisation voltage, so
`0xEDF4`'s 0 has no second reading.

**M4 and D25 closed, by the operator's decision**, on the in-circuit result above rather
than a scope reading. The BSS138 stays in both directions. Decision Register v0.29.

## 2026-10-08 — `vedirect_task` on the carrier: readback over the air, and staleness

**Image `cb92ff6`, then `177b8e3`, on the carrier at the gate's MPPT, with the bridge on
the air and no broker.** The bridge's BF-30 readback starts once it hears the node, so it
ran without MQTT and exercised the whole `HEX_REQ` path.

**All ten BF-30 registers came back through the node, each on its first attempt.** Every
`HEX_RSP` carried status 0 and the MPPT's reply verbatim. The values are the scan's
above: `0xEDF7` 1420, `0xEDF6` 1350, `0xEDF4` 0, `0xEDFD` 0, `0xEDF2` 0, `0xEDF1` `0xFF`,
`0xEDF0` 150, `0xEDFB` 200, `0xEDEA` 12 and `0xEDE0` 500. The bridge paced them over
about 54 s.

**The first Get met the Async burst and still answered.** The node counted 8 Async frames
before the reply, and no retry was needed. So the retry at half of `hex_timeout_ms` has
not yet run on the board; the native suite is all that covers it. After the readback the
MPPT kept sending Async frames: the next boot counted 97 in about 150 s with no request
sent.

**Staleness, PRD R-3.3f.** With the console sampling `ved` every 2 s, the operator pulled
the VE.Direct cable for about 15 s. The last block arrived at 122.8 s. Bit 1 was clear at
126.9 s and set at 129.1 s, 6.2 s after it, against `vedirect_stale_s` of 5. The values
held at the last block's while the bit was set. The bit cleared by 143.3 s, after the
cable went back in. No block failed its checksum across the pull: 108 good, 0 bad.

**The USB console loses bytes from the middle of a line.** The 190-byte `ved` line lost
about 60 bytes, so `177b8e3` split it in two. The shorter lines then lost bytes too, one
in about ten, so line length is not the cause. The loss is in the console, not in
`vedirect_task`: the counters it prints agree with each other.

**`vedirect_task` passes about 240 times a second**, against the 50 its 20 ms wait alone
would give. Serial1's receive callback wakes it for each burst of bytes. Nothing has been
starved yet.

## 2026-10-08 — Writes through the broker, and 30 days of MPPT history

**Bridge, simnode Heltec and the StamPLC on the bench, with the sandbox broker; the MPPT on
its bench supply with no PV.** Traces: [`data/gl4-write-gates-2026-10-08.log`](data/gl4-write-gates-2026-10-08.log)
and [`data/m14-mppt-history-2026-10-08.log`](data/m14-mppt-history-2026-10-08.log).

**The StamPLC was silent until it was replugged.** For 135 s after the bridge booted,
`lran/gatelink/availability` stayed `offline`, and macOS gave the StamPLC no
`/dev/cu.usbmodem*`. After a replug it booted `177b8e3`, rolled its context and came
online. What it was doing before the replug is not known; its console was unreachable.

**The bridge refused a Set while disarmed, and built no frame for it.** A Set of `0xEDF0`
to 14.0 A (`:8F0ED008C00E4`) drew `refused_disarmed` on `hex/response` and a retained
`hex/audit` entry with `authorization` `disarmed`. The bridge's frame log shows no
transmission between the request and the refusal. PRD R-3.5b's second gate passes on the
bench.

**Armed, the same Set reached the MPPT and read back.** `write_enable/set ON` armed it,
and `write_enable/state` followed. The MPPT echoed the Set with flags 0. A Get of
`0xEDF0` returned `0x008C`, and the bridge's own readback after the write published
`charge_max_current_a` 14.0. A second Set restored 15.0 A, `OFF` disarmed it, and the
readback showed 15.0 again. Each write's audit entry carries `authorization` `armed` and
the MPPT's reply.

**A history Get timed out at the node, though the MPPT answered it.** The first Get of
`0x1050`, today's history record, came back `status` timeout. The node's counters showed
one retry and two unmatched replies. `VedLink::answers()` matched a Get through
`vedirect::reg_reply()`, which accepts at most 4 value bytes, and a history record carries
34. `cbacd3f` matches on the echoed register alone. On that image every Get from `0x104F`
to `0x106E` answered on its first attempt. The first history Get was also the first time
the Get retry ran on the board.

**The history gives M14 a starting point, not its result.** M14 asks for a week logged
at the gate before install, and the MPPT is off the gate's PV, so it stays open. The
records are the MPPT's own, kept while it was on the gate:

| | Days 1–30 |
|---|---|
| Daily yield | 0.03–0.14 kWh, most days 0.08–0.10 |
| Daily Vbat min | 12.44–13.19 V |
| Daily Vbat max | 13.64–14.23 V |
| Daily Pmax | 19–60 W |
| Daily Vpv max | 19.13–21.79 V |
| Errors | none recorded |

Day 0 is the bench: no yield and a Vpv max of 0.01 V. Day 1 has 312 minutes of bulk and
no absorption, so it may be the partial day the MPPT came off the gate. The record does
not say. The totals record's yield, 467, matches the text block's `H19` of 4.67 kWh. The
layout used is Victron's history record: yield and consumption in 0.01 kWh, voltages in
0.01 V, Imax in 0.1 A, and bulk, absorption and float in minutes. The rest of the
totals record is not decoded.

## 2026-10-08 — GL3's command path on the air: ACK after the pulse, the in-flight retry, the dedup hit and both resyncs

**Bridge and StamPLC on the bench, with the sandbox broker; no simnode.** Every command
went in on `lran/gatelink/cmd/<action>/set`. Trace:
[`data/gl3-command-path-2026-10-08.log`](data/gl3-command-path-2026-10-08.log). Times
below are seconds from the start of each run.

**Every command on the air acked in one attempt.** Run 1, image `4fac560`:

| Command | `cmd/ack` | At GateLink |
|---|---|---|
| `nop` | `acked`, result 0 | `NOP ACCEPTED` |
| `open` | `acked`, result 0 | K3 for 499.8 ms, then the ACK |
| `hold_open` | `acked`, result 0 | `HOLD_OPEN ACCEPTED`; the K1 line lost its middle on the console |
| `release_hold` | `acked`, result 0 | K2 for 499.8 ms |
| `close 0` | `acked`, result 0 | K2 for 499.8 ms |
| `close 1` | `acked`, result 0 | K2, then K4 1.03 s later; the ACK after K4's trailing edge |
| `request_status` | `acked`, result 0 | A `STATUS` followed the ACK |
| `close 5` | `acked`, **result 5** | `REJECTED_ARG`, nothing dispatched |

The `lran` line after them read `exec 8, pulsed 5`: five sequences handed to `io_task`, and
the three commands that pulse nothing did not. `close 1` arrived at 175.13 and its ACK left
at about 176.6, inside the bridge's 3 s timeout, as plan §5.2's decision of 2026-10-07
predicted for the defaults.

**A retry inside the execution window goes unanswered** (spec §9.4). With the bridge's
`command_ack_timeout_ms` at 500, `close 1` went out at 211.95 and its retry, same `seq`
10, at 212.76, while K2 was still timing. GateLink logged `retry in flight, not answered
(spec 9.4)`, K4 followed, and the one ACK reached the bridge at 213.78. `cmd/ack` read
`acked`, two attempts. The `lran` line went from `dup 0, exec 9` to `dup 1, exec 10`:
one execution for two transmissions. The timeout went back to 3000 after the check.

**The real ACK always beat the next retry, so `DUPLICATE_CACHED` needed a fault.**
`75726f1` adds `lran ack drop`, which withholds one fresh ACK through lran-node's
`fresh_ack()` hook. In run 2, `open` at `seq` 1 pulsed K3 and its ACK was withheld. The
bridge's retry at 25.83 drew `dedup hit, DUPLICATE_CACHED (ACCEPTED), not executed`, and
`cmd/ack` read `acked`, two attempts, result 7, detail 0. `exec 1, pulsed 1`: no second
pulse. That is root rule 2 on GateLink's own relays.

**Both resyncs behave as spec §10.3 and D70 say.** `4fac560` adds `lran ctx new`, a new
`ctx_id` that GateLink does not announce. A reboot cannot stand in for it, because the
bridge hears the `BOOT` frames and adopts the new context from them.

- After `ctx new`, `request_status` drew `REJECTED_CTX (frame ctx 0xee1103fa, own
  0x66f5e7c8)`. The bridge adopted the context and retried once at `seq` 1, which was
  accepted. `cmd/ack` read `acked`, attempts 1, because a resync restarts the attempt
  count, as the bridge log of 2026-09-16 found.
- After a second `ctx new`, `open` drew `REJECTED_CTX` and the bridge published
  `unconfirmed`, result 3, with no retry. Nothing pulsed. A `nop` that followed went at
  `seq` 1 in the adopted context and acked.

**Not reached on the air: `REJECTED_MAC` and `REJECTED_SEQ`.** The bridge always sends a
valid MAC, and it resets `seq` with every roll and resync, so neither occurs without a
fault. Both stay host-tested in `lib/lran-node` and `lib/lran-protocol`. Every frame here
passed GateLink's MAC check, and `refused 2` counts the two `REJECTED_CTX` replies alone.

**The console loses the middle of a line under load.** `GL q 1: OPEN ACCEPTED` and
`GL (expander writes)` each lost about 25 bytes. The log queue dropped nothing (`log 0`),
so the loss is past the queue, in the USB CDC path the 2026-10-02 and 2026-10-08 entries
already found. It cost one pulse width, K1's, which this run has as dispatched but not
measured.

## 2026-10-08 — GL3's task watchdog fires on a stalled `app_task` at the configured 10 s

**`app_task` is the only subscriber, and the watchdog caught it.** `8fae69e` arms the task
watchdog in `start_tasks()` from `watchdog_timeout_s`'s default, and `app_task` feeds it
once a 100 ms pass (Impl Plan §5.2). On the StamPLC at `8fae69e`, `wdt` read
`timeout 10 s`. `wdt stall` parks `app_task` unfed at 9.56 s; the watchdog fired at 19.54 s
and named `app (CPU 0/1)` alone. The next boot's banner read `Reset: task_watchdog`.
`esp_reset_reason()` reports the watchdog rather than the panic it aborts through, so spec
§8.14's reset cause reads `Watchdog`.

**Nothing else tripped it.** After that reboot the board ran 110 s idle, with `vedirect_task`
reading the MPPT and `lora_task` listening, and no `task_wdt` line appeared. The GL1 bus
test was not run against it, and BLE does not run until GL5, which is where the BLE window
first sits beside the watchdog.

**Not built here: applying a `SET`.** GateLink has no `Store` yet, so the boot value is the
row's default. `apply_watchdog_timeout()` is the call the CONFIG slice makes; ESP-IDF 4.4's
`esp_task_wdt_init()` reconfigures a running watchdog, as the bridge found on 2026-09-26.

## 2026-10-08 — GL3's CONFIG on the air, with the card and without it

**V-10 passes on the bench, both legs** (Impl Plan §8.3, PRD R-4.2c). The StamPLC ran
`ff2c092`, the bench bridge ran the same commit, and every request went through
`lran/gatelink/config/set`.

| Step | GateLink | `config/ack` |
|---|---|---|
| `SET relay_pulse_ms 700`, card in | 1 write, 0 failed | `persisted`, `ok` 700 |
| Restart, `GET_ALL` | `1 restored` | `persisted`; `config/state` shows 700, `source` `override` |
| Card pulled, `SET relay_pulse_ms 800` | write failed, `DIRTY` | `applied_not_persisted`, `ok` 800 |
| Restart with no card | `NOT MOUNTED - defaults` | — |
| `SET relay_pulse_ms 800, watchdog_timeout_s 20` | `wdt: timeout 20 s` at once | `applied_not_persisted`, both `ok` |
| Card back, `GET_ALL` | `card back, config.json rewritten` | `applied_not_persisted`, as found |
| `GET_ALL` again | `overrides persisted` | `persisted` |
| Restart | `2 restored`, `wdt: timeout 20 s` from boot | — |
| `RESTORE_DEFAULTS` | `wdt: timeout 10 s`, file emptied | two `CONFIG_ACK` frames on the bridge; harness stopped before the MQTT line |

`node/state` followed: `config_persisted` and `sd_ok` were both `false` with the card out
and both `true` once it was back (spec §7.2.8 bits 0 and 1). Before this slice, bit 0 was
always set and bit 1 always clear.

**The first `SET` deadlocked `lora_task`.** `radio_service()` held the SpiLock while it
called `on_frame()`, so the engine ran inside the lock, and the card write took the same
non-recursive mutex. `lora`'s pass count stopped at 1275 and `ui_task` stalled behind the
LCD's take. The bridge reported every later `CONFIG` as outcome unknown. `91579e3` hands the
frame over after the lock is released.

**A remount with no card holds the SPI bus for about 1 s, and a failed write for about
1.8 s.** The SD library retries `GO_IDLE_STATE` and prints each retry through the core's
`log_w`, from `lora_task`. The first build retried every 30 s on a timer, which would stall
the radio at a gate with no card and was a fixed timing constant besides. `ff2c092` retries
once after a `CONFIG` or a readback finds the card unusable, so only configuration traffic
pays. The bridge's 8 s `config_ack_timeout_ms` covers the 2.5 s the slowest `SET` took.

**The bench bridge at `28dc204` answered `watchdog_timeout_s` `UNKNOWN_PARAM`.** It predates
the row, and its `CONFIG` came back outcome unknown as well. That second result was the
deadlock, not the bridge. Reflashed at `ff2c092` with the operator's agreement, it carries
the row.

**Still not reached: the PHY group.** It answers `READ_ONLY` because spec §12.4.2 is not
built on GateLink, and `config.json` never holds a PHY row.

## 2026-10-10 — The MPPT's LiFePO4 settings read back as configured

**Bridge, simnode Heltec and the StamPLC on the bench at `ff2c092`, with the sandbox
broker; the MPPT on its bench supply with no PV, the WattCycle pack beside it.** Trace:
[`data/mppt-readback-2026-10-10.log`](data/mppt-readback-2026-10-10.log).

**The bridge's boot readback matches the operator's VictronConnect page on every setting
the app shows.** The harness opening the bridge's port reset it, and its boot pass sent
ten HEX Gets through GateLink. Each answered on its first attempt, about 2 s apart.
Decoding the raw answers on GateLink's console by hand gives BF-30's published values.
[`mppt-config.md`](mppt-config.md) has the table and starts R-6.1b's record.

**Nothing changed since 2026-10-08.** Every value equals that day's readback after the
15 A restore. So the operator's LiFePO4 setup predates GL4's write test, and the readback
on 2026-10-08 was already reading it.

**`0xEDF4` and `0xEDF2` read 0 again**, so their scales are still not exercised. The
LiFePO4 settings disable both, and no readback of this configuration will exercise them.

**`0xEDF1` reads `0xFF` while VictronConnect names the preset *Smart Lithium (LiFePo4)*.**
The preset name is not in the register.

**The pack's absorption specification contradicts itself**, as `mppt-config.md` records:
"14.2V – 14.6V (14.6V ± 0.2V)". 14.20 V meets the first range and not the second. Which
one the vendor means is the operator's to settle.

## 2026-10-10 — Absorption stays at 14.20 V

**The operator settled the pack's absorption figure.** WattCycle gives 14.6 V, with
14.2–14.6 V the acceptable range. The MPPT stays at 14.20 V, because the operator expects
the lower absorption voltage to increase cell life. [`mppt-config.md`](mppt-config.md)
records the choice.

## 2026-10-10 — GL5: `bms_task` reads the pack, and the window is measured

**The StamPLC and the MPPT both on the WattCycle pack, the PV panel indoors, the sandbox
bridge running.** Trace of the final image:
[`data/bms-windows-2026-10-10.log`](data/bms-windows-2026-10-10.log). `bms now [abort_ms]`
on the console runs a window at once and, with `abort_ms`, asks it to end that far in, which
stands in for a reply queued at that moment.

**The decode agrees with `wattcycle-reader`'s for this pack.** Four cells at 3330–3333 mV,
four temperatures at 18.7–22.1 °C, 13320 mV, 0 mA with the raw discharge flag, SOC 96 %,
95.9 of 100.0 Ah, 3 cycles, SOH 100.0 %, link RSSI −66 to −67 dBm. A reading at rest settles
nothing about the current's sign, so M7 is still open.

**The first image read the pack, then panicked in the FreeRTOS timer task**, PC 0 in
`prvProcessReceivedCommands`: a timer whose callback was gone. With NimBLE's debug log on,
the window read cleanly and showed why it usually did not. Straight after the FFF1
subscription the pack asks to update the connection parameters, and our disconnect lands
while that update is pending. The debug log's own delay was enough to avoid it. Two changes
cleared it, each tested apart:

- **The client refuses the update and opens at the parameters the pack asks for**: 15 ms,
  latency 0, 4 s supervision. Panics fell from every window to 1 in 22 without aborts.
- **NimBLE's callouts run on `esp_timer`** (`CONFIG_BT_NIMBLE_USE_ESP_TIMER=1`). With
  FreeRTOS timers, a series of 26 windows with bench aborts panicked 3 times, early in a
  window and once on the first window after a reset. On `esp_timer` the same series and a
  second one of 26 ran clean. Why a FreeRTOS timer outlives its callout here is not
  established. The 24–72 h soak is the check that would show the cure is not complete.

**Discovering the FFF0 characteristics in one pass cut the window by more than half.**
Asked for one by one, NimBLE discovered each separately: 1.4 s of a 1.9 s connect. After:

| | Before | After, 15 reads |
|---|---|---|
| Window, interlock to release | 2652–3653 ms | 992–2237 ms, median 1217 |
| Connect, with discovery | 1905–2495 ms | 625–1145 ms |
| Scan to the pack's name | 41–461 ms | 11–701 ms |
| Abort latency, worst | 1352 ms | 617 ms |

**The abort latency is the connect.** Every other step checks for its end every 10 ms;
NimBLE's connect is one call that cannot be interrupted. An abort asked during it waits it
out, up to about 1.1 s. A `COMMAND_ACK` queued at the worst moment therefore leaves about
1.9 s of the bridge's 3 s ACK timeout for media access and airtime.

**`bms_window_max_ms` is in the table at `0x1042`: default 5000 ms, range 2000–30000.**
The default is more than twice the slowest window seen. The floor sits above every window
measured, and a cap set too low costs only BMS reads: a reply the bridge waits on still
ends a window early. 30 s is NimBLE's own connect timeout. No overrun was counted in any
series.

**The interlock ran against real traffic.** In two earlier series a reply the bridge was
waiting on was queued during a window: lora_task counted `asked 1`, the window ended as
`aborted`, and the reply went out after it. A frame that is not awaited waited instead
(`tx waits`).

**The heap returns to the same figure after every window**, 231376 B on the final image,
and the lowest free heap since boot stays near 182 KB. The controller cycle leaks nothing
measurable over 26 windows.

**NimBLE's C++ log is off.** At `CORE_DEBUG_LEVEL` 2 it printed `E NimBLEClient:` lines
from its host task straight to the USB port, around `log_task`. `bms_task`'s own line names
the step a failed window ended in.

## 2026-10-10 — M7: the pack current's sign is bit 15, and its unit is 0.1 A

**`lib/bms-ble` read the pack current at a tenth of its size, and on charge with the wrong
sign.** The bench had the WattCycle pack on the MPPT's battery terminals, the StamPLC on
the same terminals, and a load on the MPPT's load output. Under load, `bms data` read
−380 mA while the WattCycle app read −4.00 A and the MPPT 4.05 A out of the battery.
`aiobmsble` 0.27's TDT decoder scales the field in 0.1 A and takes the sign from bit 15.
`bms data` now prints the raw field (`853ba8f`), and the raw values settled it:

| Pack state | Raw | Old decode | New decode | WattCycle app |
|---|---|---|---|---|
| At rest (`bms-protocol` §9) | `0x4000` | 0 mA | 0 mA | 0.0 A |
| Load on the MPPT's load output | `0xC028` | −400 mA | −4000 mA | −4.00 A |
| LiFePO4 charger on the pack, load off | `0x4012` | −180 mA | +1800 mA | +1.8 A |

Bit 14 was set in all three states, so it is not the direction; what it means is still
open (`bms-protocol` §10). The decode now takes the sign from bit 15 and the magnitude in
0.1 A, and the host test holds both raw values. M7 is closed in the register. W6 stays
open in the spec, which still calls `pack_ma`'s convention pending.

**The load was 4 A.** The operator turned it down from 11 A to keep the load board cool,
and the MPPT reported `load 4000 mA` throughout.

**The MPPT read the battery 0.9–1.5 V below the pack.** At 4 A the MPPT reported 11.74
and later 12.31 V while the BMS read 13.21–13.24 V. The operator measured 12.3 V at the
MPPT's terminals and 13.18 V at the pack's, and found one section of the harness warm.
That is about 0.22 Ω, or 3.5 W at 4 A. At the controller's 15 A it would drop about 3.3 V
and dissipate about 50 W, and the MPPT would regulate on a voltage well below the pack's.

**The first window after a boot ended `aborted` at about 45 ms, twice,** with `abort
asked` and nothing yet connected. The window after it read normally. It is consistent
with the boot's LoRa traffic asking for the radio, but that was not checked.

## 2026-10-10 — `vedirect_task` takes under 1 % of a core, with or without NimBLE

**The StamPLC on the bench with the MPPT on VE.Direct and the WattCycle pack in BLE range;
no load, no charger, no PV.** Image `3f29da5`. Trace:
[`data/ved-cpu-ble-2026-10-10.log`](data/ved-cpu-ble-2026-10-10.log).

The Arduino core's FreeRTOS is built without run-time stats, so `3f29da5` times each
`vedirect_task` pass itself, from wake to done, and keeps passes with `bms_task`'s BLE
window open apart from the rest. `ved cpu` prints both. The share is an upper bound: a pass
that `io_task` or `lora_task` preempts counts their time too.

| BLE window | Time | Passes a second | Busy | Longest pass | Longest gap between wakes |
|---|---|---|---|---|---|
| Shut, no window in the interval | 9.7 s | 228 | 0.84 % | 121 µs | 20.001 ms |
| Open, across 18 windows | 22.3 s | 196 | 0.80 % | 716 µs | 20.531 ms |

Three more shut readings, of 6–23 s each, gave 0.81–0.95 % and a longest pass of 683–696 µs.

**NimBLE does not starve `vedirect_task`.** The longest gap between wakes with a window open
was 20.5 ms, against the 20 ms wait, so no pass waited more than about half a millisecond for
a CPU. The text parser saw 135 good blocks and no bad checksum, overflow or interrupted
block across the run. Passes fall from about 230 to 196 a second with a window open. Why
was not looked at; it does not change the share.

**Twenty `bms now` commands gave 18 windows: 17 read the pack, and one ended `connect`.**
Windows ran 933–1772 ms.
Those over 1.6 s spent about 1.13 s connecting, against about 0.63 s in the others.

**Some `bms now` results printed about 13 s after `window now`**, though the window itself
reported about 1.2 s. Whether the window started late or its line printed late was not
established.

## 2026-10-10 — L3: the MPPT holds a HEX answer until its text block ends

**Same bench, bring-up image `7c97fe4`.** Its banner reads `-dirty`: the engineering log
and the trace above were not yet committed. Trace:
[`data/l3-ved-raw-hex-2026-10-10.log`](data/l3-ved-raw-hex-2026-10-10.log).

L3 asked for a raw text block with a HEX exchange inside it. `7c97fe4` adds a register to
the bring-up image's `ved raw`, which then sends a Get of it every 50 ms through the
capture. `ved raw 5 EDF7` sent 100 Gets in 5 s and received 2537 bytes: five text blocks,
95 answers of `:7F7ED008C05D9` (absorption, 14.20 V) and 15 asynchronous `:A` frames.

**No HEX frame landed inside a text block.** A Get went out every 50 ms whether or not a
block was being sent, and each block takes about 100 ms at 19200 baud. Even so, every
answer came after the block's checksum byte, the first with no `\r\n` before it. The MPPT
then sent the answers it had queued, back to back, before the next block. So this 75/15,
FW 175, does not interleave, and a capture of a HEX frame mid-block cannot be provoked on
it. `test_hex_inside_a_block_resumes_it` stays synthetic, as a defence against other
firmware.

One block and the first two answers after it are now `kCapturedHex` in
`lib/vedirect/test/test_text/`. `kMppt7515` stays: its nonzero PV, charge-state and yield
values exercise `decode_mppt()` where the bench captures read zero.

`interrupted` against `bad_checksum` on one run: `vedirect_task`'s 2026-10-10 run above saw
0 of each across 135 blocks, with no Gets in flight.

## 2026-10-10 — GL3's reset causes on the air, and the StamPLC's inputs read nothing

**The StamPLC on its 12 V supply, the bridge on `/dev/cu.usbserial-0001`, bench.py with
`--mqtt`.** Images `2537cb9`, then `7dbf50a` with a `panic` command, then `99cc6a6`. Trace:
[`data/gl3-reset-causes-2026-10-10.log`](data/gl3-reset-causes-2026-10-10.log).

**Every reset cause the bench can produce reached Home Assistant with the right `detail`.**
Each boot printed `boot: <cause>, ctx_id …` and the bridge published
`lran/gatelink/event/boot`:

| Cause | Produced by | `detail` |
|---|---|---|
| `POWER_ON` | USB and VIN removed together | 1 |
| `REBOOT_COMMAND` | `165` on `lran/gatelink/cmd/reboot/set` | 2 |
| `SOFTWARE` | console `restart` | 3 |
| `WATCHDOG` | `wdt stall` (task watchdog) and `hang` (interrupt watchdog) | 4 |
| `PANIC` | console `panic`, new: `abort()` | 5 |
| `EXTERNAL` | an upload, or the host opening the USB serial port | 7 |

The `REBOOT` came back `acked`, one attempt, 0.4 s before the node restarted (R-3.5f).
**31 `BOOT` events carried 31 different `ctx_id`s** (R-3.5h). The boot's word is now drawn
in `start_tasks()` with `bootloader_random_enable()` on, before BLE or any task starts.

**A USB reset reported `UNKNOWN` until this branch.** ESP-IDF 4.4 has no `ESP_RST_USB`, and
`esp_reset_reason()` answers `ESP_RST_UNKNOWN` for a reset through the USB serial/JTAG
peripheral. The ROM's `rtc_get_reset_reason(0)` reads `USB_UART_CHIP_RESET` or
`USB_JTAG_CHIP_RESET`, and the node maps both to `EXTERNAL`. `main` (`c362d31`) flashed for
the input check below printed `Reset: unknown` on the same reset where this branch prints
`Reset: usb`.

**On the bench a power-on can read `EXTERNAL`.** When the host reopens the serial port as
the board comes up, the open resets the chip again over USB, and the second boot is the one
the console shows. The first boot's `power_on` event still reaches the broker.

**R-3.5k's RST check passes on every boot.** RST low reads BUSY high, and BUSY falls
1618–1622 µs after release, the 1.6 ms the bring-up image's `reset` measured on
2026-10-06. RadioLib's `begin()` pulses RST too; the check is what proves the line arrives.

**The `BOOT` event's `input_bits` is always 0.** `announce_boot()` builds it before
`io_task`'s first read lands. The alarms re-sent at boot wait for that read.

### R-3.5i not verified: the StamPLC's inputs read 0 with 13.4 V on them

The operator fed 12 V to IN5 with `EXCOM_COM` on 12 V−. One `in` at 783 s read raw `0x10`.
From the next `restart` on, every read was `0x00`: the boot's first read, and every live
read after it.

What was ruled out, in order:

- **Wiring.** A DVM read 13.36 V at the IN5 and IN6 screw terminals against `EXCOM_COM`,
  ferrules crimped, polarity right. IN5 alone drew 4.1 mA in series at 12 V.
- **A floating input supply.** Tying the input supply's negative to VIN's changed nothing.
  IN8 fed from the VIN supply through the bench switch, the setup that worked on
  2026-10-06, read 0 as well.
- **The AW9523.** K1–K4 clicked on `relay`, with 0 I²C failures. Its registers, read through
  `io_task`: input `00 0f`, output `00 0f`, config `f0 ff`, interrupt `ff ff`, GCR `10`. The
  input pins are inputs and read low; the unused P1_0–P1_3 read high, so the register is
  live.
- **This branch's firmware.** `main` (`c362d31`) read IN8 as `0x00` the same way, after a
  power-on.

**The StamPLC IO board schematic (V1.0) does not match the board.** It draws each input as
`EXCON_COM` → 6.2 kΩ → EL3H4 LED → 6.2 kΩ → `EXCON_INx`, and the output as an emitter
follower from `SYS_3.3V` through 1 kΩ to `SYS_INx`, 5.1 kΩ to ground. Unpowered, every
input reads 6.2 kΩ to `EXCOM_COM` in both directions, the never-wired ones included. An
ohmmeter cannot light an LED, so a symmetric 6.2 kΩ means the measured path does not pass
through one. And the schematic's 12.4 kΩ would allow 1 mA at 12 V, where 4.1 mA was
measured.

**Not established: why the optocouplers' outputs never rise.** One hypothesis was the
schematic's margin: about 1 mA of LED current needs a CTR near 50 % to pull `SYS_INx` past
the AW9523's threshold, against the EL3H4's 20–300 %. The 4.1 mA reading contradicts it.
The next checks are an input fed from 24 V (within the board's 5–36 V), and the board's
revision against the schematic's.

**Falsified by:** any input reading 1 while energized. The firmware is then cleared, and
R-3.5i's boot check can be run as written.

## 2026-10-11 — The StamPLC's inputs: port 0 reads, port 1 does not

**The StamPLC on its 12 V supply, inputs fed from the bench supply or two 12 V SLAs in
series, bench.py on the console.** The image already on the board, then `a890775` with an `aw` register
dump. Board temperature 21.5–27.6 °C on the LM75. Each row is five `in` reads a second apart.

**IN1–IN4 read high, and IN5–IN6 read 0, at every voltage tried.**

| Energized | Drive | `EXCOM_COM` | raw |
|---|---|---|---|
| IN5, IN6 | 15.11 V | connected | `0x00` |
| IN5, IN6 | 25.25 V | connected | `0x00` |
| IN1 | 25.25 V | connected | `0x01` |
| IN1–IN4 | 25.25 V | connected | `0x0F` |
| IN1–IN4 | 13.4 V | **open** | `0x00` |
| IN1–IN4 | 13.4 V | connected | `0x0F` |
| IN1–IN6 | 13.4 V | connected | `0x0F` |

In the last row the edge counters did not move, so IN5 and IN6 never rose at all. IN1–IN4
are the AW9523's P0_4–P0_7 and IN5–IN8 its P1_4–P1_7 (IO schematic V1.0). Every input tested
on 2026-10-10 was on port 1.

**The expander's configuration is right for port 1.** `aw` with IN1–IN6 at 13.4 V:

```text
aw: input f0 0f output 00 0f dir f0 ff int ff ff id 23 gcr 10 ledmode ff ff
```

P1_4–P1_7 are inputs (`dir`, 1 = input) in GPIO mode (`ledmode`, 1 = GPIO), so no
constant-current sink holds them low, and the ID is the AW9523's `0x23`. P1_4 and P1_5 read
0 with their inputs energized. **So port 1's fault is in hardware:** the four opto outputs
PC5–PC8, their `SYS_IN5`–`SYS_IN8` traces, or the expander's P1_4–P1_7. The case is sealed,
and telling these apart needs it opened: `SYS_IN5` near 3 V with IN5 energized puts the
fault in the expander, and near 0 V puts it before it.

**Correction to 2026-10-10: the IO schematic does match the board.** R14 (6.2 kΩ) sits
directly across each input's terminals, beside R22 and the LED. That entry read the input
as one series path. R14 explains the symmetric 6.2 kΩ an ohmmeter reads, and 13.4 V draws
2.2 mA through R14 and about 2.0 mA through the LED, the 4.1 mA measured. The operator's
analysis, [`StamPLC-el3h4-input-analysis.md`](StamPLC-el3h4-input-analysis.md), has the
arithmetic.

**IN1–IN4 hold high down to 5.24 V, the bench supply's floor.** The operator lowered
the supply from 13.4 V in 0.5 V steps of about 5 s each, with the supply feeding only
IN1–IN4. The edge counters stayed at 4/4 from the reflash on, so no input fell even for one
poll. At 5.24 V the LED carries about 0.68 mA. The dropout lies below that and was not
found. The analysis puts a minimum-CTR (20 %) EL3H4 near 2.0 V at `SYS_INx` with 2.0 mA of
LED current, below the threshold, so this unit's parts sit well above the minimum. That is
one unit's margin, not the design's.

**A stiff 5 V source reads; a 5 V pull-up is another matter.** Each input draws about
1.5 mA at 5.24 V, 0.85 mA of it through R14. Not tested: a pulled-up line, which sags under
that load.

**What it costs GateLink:** plan §3.2 puts the FIRE terminal on IN5 and the alarm output
on IN6, the two inputs R-3.5i re-sends at boot. IN1–IN4 are taken by the four dry contacts.
R-3.5i can be run on IN1–IN4 to prove the firmware, but the deploy needs port 1 working or
the two voltage-sense signals moved.

**Falsified by:** IN5 or IN6 reading 1 while energized with `EXCOM_COM` connected, on this
board.

## 2026-10-12 — GateLink deploys on four inputs, and IN3 and IN4 go through a 2N7000

**The operator decided to deploy the bench StamPLC, whose IN5–IN8 read nothing** (2026-10-11
entry). FIRE and the alarm output are no longer monitored, which leaves four signals for the
four inputs that read. Decision Register D77 records it; PRD v0.19 and plan v0.36 carry it.

**The 1050's SAFETY and EXIT lines idle at about 4.85 V, in standby too** (operator's
meter). So the 1050 keeps its pull-ups up while it sleeps, and the lines are not stiff
enough to drive an input directly (2026-10-11: about 1.5 mA at 5 V). The current each line
sources into a short, which gives the pull-up's value, is not measured.

**Each line drives a 2N7000, which sinks its input from a local 12 V on `EXCOM_COM`.**
4.85 V of gate drive is above the 4.5 V at which the part's R<sub>DS(on)</sub> is specified.
A 10 kΩ in series and 1 MΩ gate to source load the line by about 5 µA. The stage inverts:
an idle line energizes the input. `EXCOM_COM` is shared, so IN1 and IN2 go sinking too, and
the harnesses fitted on 2026-10-10 need rewiring. Plan §3.2 has the circuit.

**The firmware turns opto levels into asserted inputs before the debounce.**
`inputs_from_levels()` (`gate_io.h`) flips IN3 and IN4 and masks IN5–IN8 to 0. The FIRE and
hard-shutdown path stays in `lora_task`, unreachable. `in`'s `raw` still shows the opto's
levels; `debounced` is now asserted inputs. On the bench, with nothing on IN3 and IN4,
`debounced` reads `0x0C`: both asserted, as a broken wire would read.

**Falsified by:** a 2N7000 stage on the bench, its gate on a 4.85 V source through 10 kΩ,
leaving its input reading 0 at `raw`.

## 2026-10-13 — The 1050's lines measured, K3 and K4 move to the Radio inputs, and GL3 derives the gate

**The pack and the MPPT are back at the gate** (operator). Tier 2's soak waits for the
2N7000 board and the rewired harnesses, which change the production node mechanically.

**The 1050's 5 V inputs source 2.95 mA into a short and rest at about 4.75 V**, with the
1050 active and in standby (operator's meter, at the gate). That is a source of about
1.6 kΩ. GateLink reads SAFETY and EXIT. The 2N7000 stage's 10 kΩ and 1 MΩ cost such a line
under 10 mV, so plan §3.2's margin question closes. The 4.85 V of 2026-10-12 and today's
4.75 V differ by 0.1 V; both clear the 4.5 V at which the 2N7000's R<sub>DS(on)</sub> is
specified.

**K3 and K4 move to Radio Open (39) and Radio Close (40)** (operator). They do what Guard
Station Open and Close do, they were free, and the keypad keeps the Guard Station inputs to
itself. Plan v0.37 §3.1 records it. §9.2's wake test did not include them. Its model says
a command input wakes the board, so they should; GL2 checks it.

**`app_task` now derives the gate block.** Until today STATUS sent `gate_state` UNKNOWN.

- `gate_state.cpp` maps IN1 and IN2 to spec §8.3, and holds 1/0 for `hold_confirm_ms`
  before declaring OPEN_HELD. A hold is LRAN if a K1 pulse claimed it, KEYPAD_FIRE after a
  FIRE edge, UNKNOWN if it was there at boot, MANUAL otherwise. A K1 claim lapses when the
  gate closes, when K2 pulses, or when nothing changes within `cause_window_ms`, so a K1
  that moved nothing cannot claim a later manual hold.
- `detect.cpp` classifies EXIT-then-SAFETY as EXIT and SAFETY-then-EXIT as ENTRY. The
  first edge sends VEHICLE_DETECTED, or VEHICLE_WHILE_HELD_OPEN under a hold, at once and
  with direction UNDETERMINED. The follow-up, with the same `event_id`, carries the result.
  `detect_sequence_window_ms` and `detect_sequence_idle_ms` resolve partial traversals.
- **`held_open_alert_repeat_s` is read as a rate limit.** PRD R-3.2.3e names it without
  saying what it repeats, and spec §7.2.5 bit 4, "suppressed by the re-alert interval",
  reads as a limit. At its default, 0, every detection under a hold alerts.
- `inject` (PRD R-5.4b) overrides inputs below the debounce. Named scripts cover the
  30 s EXIT-to-SAFETY gap (`exit`) and both partial traversals (`wand-back`, `loop-stop`).

The three host suites pass, 34 cases among the firmware's 128.

**Falsified by:** a bench `inject exit` that does not end in a follow-up with direction
EXIT, or a K1 `HOLD_OPEN` followed by `inject hold` whose hold reads anything but LRAN.

## 2026-10-13 — GL3's derivation on the bench: every case passes on the air

**Flashed with `d19188a`'s code and run against the bridge and the
sandbox broker** with `tools/bench/bench.py`. The inputs were wired as they will be: COM at
+12 V, an input pulled to GND to energize it, IN3 and IN4 on GND for idle. `in` read `raw
0x0C debounced 0x00` and `gate` read CLOSED. Twenty detection events reached MQTT, none
dropped (`pushes dropped 0`, I²C failures 0).

| Case | How | Result |
|---|---|---|
| EXIT with a 30 s gap | `inject exit` | First event at the EXIT edge; the follow-up, same `event_id`, `direction: exit` 30 s later |
| ENTRY | `inject entry` | `direction: entry` |
| Simultaneous | `inject both` | UNDETERMINED at once |
| Partial at the wand | `inject wand-back` | UNDETERMINED 10 s after EXIT cleared (`detect_sequence_idle_ms`) |
| Partial on the loops | `inject loop-stop` | UNDETERMINED at 60 s (`detect_sequence_window_ms`) |
| LRAN hold | `cmd/hold_open/set` through the bridge, then `inject` | K1 pulsed; `hold_source: lran`, cause `lran_command`; SAFETY's first edge sent `vehicle_while_held_open` |
| MANUAL hold | `inject` alone | `hold_source: manual`, cause `manual_hold`; EXIT's first edge alerted |
| KEYPAD_FIRE hold | `inject +F` first; IN5 is not wired (D77) | `hold_source: keypad_fire`; alerted |
| UNKNOWN hold | IN1 on GND, then `restart` | `open_held`, `hold_source: unknown` 2 s after boot; lifting IN4 alerted |
| Real traversal | Operator lifted IN4, then IN3 | `direction: exit` |

The partial traversals left `last_traversal` unchanged. A sensor already asserted at boot
sent nothing, as designed: IN4 was off GND across the `restart`, and only its next edge
alerted.

**`held_open_alert_repeat_s` is a rate limit, and 0 alerts once per vehicle** (operator).
Raising it would filter a double alert from a vehicle towing a trailer, which is rare.

**Event to MQTT took 0.4 s to 6 s.** Most arrived within 2 s. The 6 s one, a follow-up,
was queued before the gate's next changes, so it did not wait behind their STATUS frames.
A BLE window is one candidate; the cause is not established. None was lost.

## 2026-10-13 — Spec §12.4.2's PHY trial on GateLink: commit and both reverts pass on the air

**GateLink now takes a fleet PHY change.** It runs `lib/lran-node`'s `PhyTrial`, as the
simnode does. The committed group is in NVS (`firmware/gatelink/src/nvs_blob.cpp`), not on
the microSD (operator, same date). If the group were on the card, a card that failed after
a commit would boot the node on D1's defaults while the bridge stayed on the new settings.
Plan §6.4 records the choice.

**Flashed with `82ccd83`** over USB and run against the bridge (`ff2c092`) and the sandbox
broker. The trace is [`gl-phy-trial-2026-10-13.log`](./data/gl-phy-trial-2026-10-13.log),
with the bridge's per-second `CHAN` lines removed. Times are seconds from its start.
GateLink was the only node in the fleet: `simnode_diag_enable` was 0, and every simnode
row read `deployed` 0.

| Check | What was done | What happened |
|---|---|---|
| Commit | `{"set":{"freq_hz":917000000}}` on `lran/bridge/config/set` at 20.78 | GateLink accepted the `SET` at 21.28 and retuned at 22.09, after its `CONFIG_ACK` left. The bridge retuned at 22.81 and committed at 26.69. `config/ack` read `ok`, `persisted`. The bridge's confirming `GET` reached GateLink at 27.16, and `phy` then read 917.0 MHz committed, `trials 1 committed 1` |
| The commit outlives a reset | `reset GL` at about 41 | GateLink booted on 917.0 MHz. The bridge received its `BOOT` status and event, then polled it as before |
| Revert after a reboot | 917.0 → 917.4 MHz at 159.68, and `reset GL` 0.13 s after GateLink logged `board trial` at 161.43 | GateLink booted on 917.0 and logged the revert as `a reboot ended the trial`. The bridge polled it on 917.4 MHz unanswered, and reverted at 273.86, 112.6 s after GateLink's `CONFIG_ACK`: `phy_trial_s` 120 less one 8000 ms `config_ack_timeout_ms` (spec §12.4.1 step 8). `config/ack` read `reverted`, and `lran/bridge/event/phy_reverted` `not_heard`. The bridge's next `POLL` drew `EVENT PHY_REVERTED detail 0x0002` at 275.01, which reached `lran/gatelink/event/phy_reverted` with `detail` 2 |
| Revert at the window | 917.0 → 917.4 MHz at 289.71. `hold BRG` after the bridge logged `every node accepted - retuning` at 292.96, and `release BRG` at 436.83 | GateLink retuned at 292.11 and logged `the window expired` at 412.02, 119.9 s later. It was back on 917.0 MHz at 412.22. The bridge booted on 917.0 and published `phy_reverted` `restart`. Its first `POLL` to GateLink drew `PHY_REVERTED detail 0x0001` at 601.65, which reached MQTT with `detail` 1 |
| Back to the bench's group | 917.0 → 917.4 MHz at about 615 | `config/ack` `ok`, `persisted`, at 620.62. GateLink and the bridge both hold 917.4 MHz committed, which is D1's group |

**After its restart the bridge sent nothing for 163 s**, until `inject both` made GateLink
transmit at 600. GateLink's `deployed` reads 0, so the bridge does not poll it from boot,
and an idle GateLink sends nothing of its own. The bridge handoff records the same for the
simnode rows. The `PHY_REVERTED` therefore waited for GateLink's next push. With
`deployed` 1 the bridge would poll from boot; that path was not run here.

**The boot after a USB reset lost the start of two console lines** at 162.45: `D to the
committed group - a reboot ended the trial` and `ll in 1621 us`. The earlier reset at 41
printed whole. Each line reached `log_task`'s queue whole, since the revert was reported
correctly, so the loss is on the USB CDC side. The cause is not established; the handoff's
*Open* already carries lost console bytes from 2026-10-08.

**Not measured: what the commit's NVS write costs `io_task`.** A flash write stalls the
cache on both cores. No relay command ran during a trial, and the bridge sends no other authenticated
frame during one (`phy_change.h`'s `blocks_traffic()`). The bench soak's relay timing
is the check that would show a cost.

## 2026-10-14 — Low-temperature charge inhibition passes through the BMS emulator

**GL5's BMS path for PRD R-6.4a works on the air.** GateLink `d0eebee` and the simnode
`a49a90a` were flashed over USB and run against the bench bridge and the sandbox broker.
The trace is [`gl5-charge-inhibit-2026-10-14.log`](./data/gl5-charge-inhibit-2026-10-14.log),
with the bridge's `CHAN` lines and its rxlog diagnostic removed. Times are seconds from its
start.

| Check | What was done | What happened |
|---|---|---|
| Onset | `SIM bms on 49A1`, `SIM bms temp -5`, then `GL bms now` at 19.44 | The read took 422 ms. GateLink logged `charge: INHIBITED, by bms` at 20.01. The bridge published `node/state` with reason `charge_inhibited` at 20.90 and `event/charge_inhibited`, event_id 2, at 22.56 |
| Clear | `SIM bms temp off`, then `GL bms now` at 32.27 | GateLink logged `charge: no longer inhibited, by bms` at 32.83. The bridge received its STATUS at 34.59 and republished `battery/state` with the cells at 22 °C. No event, as designed |
| Onset again | `SIM bms temp -5`, then `GL bms now` at 38.08 | A STATUS and event_id 3 at 40.26 and 40.56 |

**The pack's own inhibit flag is still unread.** `0x8D` is not decoded (bms-protocol §10),
so the BMS path uses the coldest sensor at or below `charge_inhibit_temp_c` with the pack
not charging. `battery/state.charge_inhibited` comes from spec §7.2.7 bit 3, the BMS's own
flag, and so read `false` throughout. The derived indication is §7.2.6 bit 3.

**The clear is invisible in MQTT on this bench.** The bridge publishes §7.2.6 bit 3 only
inside `solar/state`, and with no MPPT on the UART that topic reads `available: false` with
`charge_inhibited: null`. The bridge did not republish `node/state` for the clearing
STATUS, whose reason matched the onset's; it appears to publish that topic only when it
changes, which was not checked in its code. The onset showed only through its reason and
its event. With
VE.Direct down, a BMS-path inhibit therefore never reaches HA as a state, which is the case
R-6.4a exists for. The handoff carries it for the bridge.

**The VE.Direct path (R-6.4b) is host-tested only.** The MPPT is at the gate, so nothing
fed GateLink's UART. `test_charge_inhibit` covers the confirm time, each part of the
condition, the rising-voltage restart, the stale-BMS handover and the clear.

**The bench bridge lacks the three new rows** (`0x1043`–`0x1045`): its `config/state`
listed every other GateLink row. The bridge compiles lran-config's table into its image,
so it learns them when it is rebuilt.

## 2026-10-14 — The charge-inhibition thresholds become constants: the bridge cannot carry the rows

**Any new GateLink row overflows the bridge, so the three thresholds are constants for now**
(operator, 2026-10-14). `charge_inhibit_temp_c`, `charge_inhibit_band_ma` and
`charge_inhibit_rise_mv` were rows `0x1043`–`0x1045` in the run above. They are now
`kChargeInhibitTempC`, `kChargeInhibitBandMa` and `kChargeInhibitRiseMv` in
`charge_inhibit.h`, at the same values. That breaches PRD R-5.3a until the rows return,
and changing one means a USB reflash at the gate.

**The bridge's `test_config` found it.** It builds GateLink's worst-case `config/state`,
every value at its widest, and the document must fit `kMaxPayloadLen`, 2048 bytes:

| GateLink rows on the topic | `config/state` | `get_all` answer |
|---|---|---|
| Three new rows, 36 in all | 2200 bytes | 2045 bytes |
| The temperature row alone, 34 in all | 2068 bytes | 1925 bytes |

**Raising the cap to 2560 does not link.** The bridge's `dram0_0_seg` overflowed by
10,816 bytes. The publish queue's 32 slots, the event queue's 8 and about seven static
`PublishMessage`s each grow by 512 bytes, about 24 KB in all. The operator chose constants
over cutting the publish queue to 24 slots. The rows can return once the bridge no longer
sizes every queue slot for `config/state`, for example by building that document in
`mqtt_task` from one static buffer, as discovery is built.

## 2026-10-15 — The charge-inhibition thresholds are parameters again

**Rows `0x1043`–`0x1045` are back in `lran-config`, and PRD R-5.3a is met again.**
`charge_inhibit_temp_c`, `charge_inhibit_band_ma` and `charge_inhibit_rise_mv` keep the
ranges and defaults they had on 2026-10-14, so the constants' values carry over unchanged.
`app_task` reads them live each pass, and the console's `chg` line prints the live values.

**The bridge carries them because `config/state` has its own cap now** (bridge engineering
log, 2026-10-15). The 2200-byte worst case from the 2026-10-14 entry fits
`kMaxConfigPayloadLen`, 3072. The bridge's native suite builds that document from the
restored table, and all 531 tests pass. GateLink's block is 24 rows and its readback 233
bytes, still two `CONFIG_ACK` messages (Library Plan v0.28 §4).

Both images link: GateLink at 98,320 bytes of static RAM, the bridge's `heltec` at
253,140, unchanged by the table.

**On the air, each row sets and persists.** Both boards were flashed over USB from this
branch, and each row was set on `lran/gatelink/config/set` through `tools/bench/bench.py`:

| Row | Set to | `config/ack` |
|---|---|---|
| `charge_inhibit_temp_c` | −5 | `ok`, `persisted` |
| `charge_inhibit_band_ma` | 250 | `ok`, `persisted` |
| `charge_inhibit_rise_mv` | 35 | `ok`, `persisted` |

GateLink's console then read `temp <= -5 C, band 250 mA, rise 35 mV`, so `app_task` takes
the new values without a reboot. The bridge's next `config/state` was 1957 bytes, over the
old 1024-byte cap, and carried all three with `source` `override`. The bridge's connect
banner read `heap free 58536, lowest 53440`.

**Not checked:** the three `number` entities in Home Assistant, because the harness logs
only `lran/#`. `tools/checks/ha_examples.py` covers their discovery payloads.

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
