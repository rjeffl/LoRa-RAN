# `gatelink` — session handoff

**Written 2026-10-24 by the new-hardware bringup session.** It benched the replacement
StamPLC, the new MOSFET input board, the new SD card and the changed VE.Direct connector.
It also found why IN5–IN8 never read: P1's direction register must be written before those
pins sample. `io_task` now writes it, and all six inputs read.

**Two follow-up sessions the same day** found that the original StamPLC's IN5–IN8 read
too, and then settled D77 as **D83: GateLink deploys on six inputs.** IN5 and IN6 are read,
FIRE and `HARD_SHUTDOWN` go out on their edges, and R-3.5i passed on the air.

**Updated 2026-10-25 by the bench-soak session.** The first soak ran 24 h through the
real power chain, and GateLink did not reset (engineering log, 2026-10-25, *Bench soak,
24 h*). A follow-up session fixed `bench.py`'s keepalive, put wall-clock times in its log
and marked retained messages, and GateLink now writes each BMS window's outcome to the
card's `/bms.log`, flashed at `1049387` from the Mac.

**Updated 2026-10-26 by the soak's second-run session.** The second run ran 19.2 h from the
Kubuntu laptop, and GateLink did not reset again (engineering log, 2026-10-26, *Bench soak,
second run*). The BMS missed five windows, and two of them took the battery unavailable at
the bridge. **`/bms.log`, which says why, is still on the card, unread.** The Kubuntu
laptop cannot read the card, and the operator will not reflash from it.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win. GateLink's is [`engineering-log.md`](./engineering-log.md).

## Start here

**A session does one task group, and reads only what that task needs.** Open it with one
of these lines, then read this section and the sections the table names:

```text
Continue from docs/gatelink/HANDOFF.md: task L<n>.
```

**The input board is built and fitted, and the original StamPLC is back in service**
(2026-10-24). The pack and the MPPT are on the bench with it, so VE.Direct, the BMS and the
radio all ran together through the real power chain. Everything below runs on the StamPLC's
12 V bench supply. **The soak no longer waits on hardware**, and GateLink
deploys on six inputs (D83).

| Tier | Task | Read |
|---|---|---|
| 3 | **Spec v0.18's GateLink side**: answer `ACTUATOR_FAULT` for a failed relay write (D78), name the path in `CHARGE_INHIBITED`'s `detail` (D81), and send schema `0x13` with `ah_drawn_dah` (D82) | spec §7.2.10, §8.2, §8.9, §7.2.8; PRD R-6.4c; plan §5.2 |
| 3 | **The relay at a watchdog reset** — a written proposal; and the SD library's `log_w` | *Open*, below |
| 2 | **Read the card's `/bms.log`, from the Mac.** Both soaks passed without a reset, and the second run's five BMS misses are on the card, unread. Either take the card out and read it in the Mac, or add a console command that prints the file and flash it from the Mac. Then explain the misses in a new engineering-log entry | Engineering log, 2026-10-26, *Bench soak, second run*; 2026-10-25, *`/bms.log`*; *Reading `/bms.log`*, below |
| — | **GL4's M14**: the MPPT is on the bench now, so its PV history is reachable. The operator's call | Plan §8.2's GL4 row and §9.8 |

### The soak's setup

The operator chose this on 2026-10-24: the gate is already down, the MPPT runs from a
30 W panel, and the Mac cannot stay attached.

- **Keep USB off the StamPLC during the soak.** With 12 V on, USB carries all but 3.1 mA of
  the board's load, so a soak with USB attached does not test the power chain. Flash it
  over USB, then unplug. A cable with VBUS cut, through the USB 2.0 hub, would keep the
  console without powering the board; that is the only way to catch a panic's backtrace.
- **The `BOOT` event names the reset cause** (`reset_cause()` in `task_runtime.cpp`), so
  an unplanned panic, watchdog or brownout reset shows in the MQTT log without a console.
- **The bridge goes on its own USB supply, and the simnode is unplugged**, so GateLink
  is the only node on the air.
- **The pack charges through M7's warm harness section.** At 30 W that is about 2.3 A, or about
  1.2 W in the 0.22 Ω. The fix is still owed before a larger panel.
- **Log with `bench.py --dir <soak dir> run --mqtt`** (`--dir` goes before the
  subcommand). Its lines lead with wall-clock time, and `<retained>` marks a message the
  broker replayed. Add a `--port` only for a board whose console is attached without
  powering it. The run needs the bench `secrets.h` in that checkout, which the operator
  copies.
- **Start the logger outside any Claude Code session**, with `nohup`, `systemd-run --user`
  or a terminal that stays open. The first run was a session's background task, and it was
  killed when that session ended, 4.3 h before the 24 h mark.
- **Read the card's `/bms.log` after the soak**, one line per BLE window, for why each
  missed poll ended. The `bms` line counts lines written and failed.
- **Watch for:** any `BOOT` after the first, and a steady STATUS rate. Watch for BMS
  misses, with `/bms.log` naming how each ended. VE.Direct should never go stale; note the
  overnight ΔSOC.
- **Build from a clean tree**, so the banner does not read `-dirty`. OTA uploads are the
  operator's. Quit VictronConnect before the USB upload.

### Reading `/bms.log`

The file has one line per BLE window since the card was last cleared, and `uptime_s` is
its only clock, because the RTC reads 2049. The second run's boot was at 19:14:49 EDT on
2026-10-25, so its misses are the lines near `uptime_s` 2,410, 14,770, 27,430, 40,390 and
66,310. Any line whose `end` is not `read` is a miss, and its timings say where the window
stopped. **Do it from the Mac**, which holds the toolchain GateLink was built with:

- **Card out:** power the StamPLC down, take the card out, and read it in the Mac. Nothing
  changes on the board.
- **Over USB:** a console command that prints `/bms.log` under the SPI lock. That is a
  firmware change and a flash, which resets GateLink. It is worth having at the gate,
  where the card is behind the enclosure.

Record what the lines show as a new dated entry in the engineering log, and rewrite this
file.

**The cleanup the task produced is part of the task.** Close the session by committing,
pushing and opening the PR. Merge it once the operator accepts it, then rewrite this
section and *The next job*.

## The next job, in one place

**GL3 is done, R-3.5i included** (engineering log, 2026-10-24, *D83*). State derivation, hold sources,
direction, partial traversals and the held-open alert passed on the air (engineering log,
2026-10-13). GL6 confirms them against real gate cycles, after GL2.

**Low-temperature inhibition is built, and its BMS path passed on the air** through the
emulator's `bms temp` (engineering log, 2026-10-14). Its VE.Direct path, R-6.4b, is
host-tested only and waits for the MPPT. Its three thresholds are rows `0x1043`–`0x1045`
again, settable from HA, and passed on the air (engineering log, 2026-10-15). The bridge
shows the indication while VE.Direct is down since the same date (bridge engineering log).

**Tier 3 finishes the firmware before deploy**, because every firmware item left after
install costs a walk to the gate:

- Spec v0.18's GateLink side (D78, D81, D82), the relay-at-watchdog question under
  *Open*, and the SD library's `log_w`.
- The 2N7000 stages for IN3 and IN4 and the IN1 and IN2 harnesses rewired sinking, when the
  board arrives (plan §3.2).
- Last of all, the production build from the production `secrets.h`.

**Tier 2's soak has run twice, for 24 h and then 19.2 h**, through the MPPT, the pack and
the StamPLC, with the radio, VE.Direct, BMS polls and SD writes all active. GateLink reset
in neither run, so GL5's timer-task panic did not recur in about 43 h; its cause is still
not established. What remains is the BMS misses, and `/bms.log` should explain them.

**Fix the warm section of the MPPT's battery harness before the MPPT charges through it**
(engineering log, 2026-10-10, *M7*: about 0.22 Ω, about 50 W at 15 A). The operator's.

**At the gate, each needing its own visit:** GL2 (the 1050 powered; confirm K3 and K4 wake
it on Radio Open and Close), M23, GL6, then GL7. The bridge handoff carries the bridge and
Home Assistant work that has to be ready by the install visit.

The node's refusals of a bad MAC, a stale `seq` and an unauthenticated Set are host-tested
only, because the bridge always sends a valid MAC and resets `seq` itself.

## What the last session established

- **GateLink ran the second soak, 19.2 h, without a reset** (engineering log, 2026-10-26).
  `uptime_s` places its boot at 19:14:49 on 2026-10-25. All 1,150 polls were answered,
  `missed_polls` stayed 0, and VE.Direct never went stale. ΔSOC overnight was 0.
- **The BMS missed five windows, one at a time, about 3.5 h apart**, with one 7.2 h gap.
  Two misses took `age_s` to 601 and 602, past the bridge's `bms_stale_s` 600, so the
  bridge published the battery unavailable until the next read.
- **`/bms.log` is unread.** The console cannot print it, the Kubuntu laptop cannot read
  the card, and the operator will not reflash from that laptop.
- **`bench.py` ran 19.2 h under `nohup` with one broker connection.** No broker outage
  happened, so the keepalive fix is still tested against a fake broker only.
- **Five events at 10:50 on 2026-10-26, `event_id` 9001 to 9005, were the operator's test
  publishes**, on MQTT alone. They are not GateLink's.

## Decisions taken 2026-10-01, by the operator

| Decision | Recorded in |
|---|---|
| The carrier is `gatelink-expansion-board` rev 0.3; the plan defers to it | Plan header, §2–§3 |
| Extract `lib/lran-node/` from the simnode; GateLink and the simnode both consume it | Plan §5.4, L1 |
| A firmware-local board layer, not `/lib/lran-platform/` | Plan §5.3. **Contradicts System PRD §3.5's SHALL** — `doc-findings` 9 |
| Move `bms_ble` to `lib/bms-ble/` under repo conventions | Plan §4.3, L2. Built 2026-10-02 |
| GateLink milestones are `GL0`–`GL9`, renamed from `M0`–`M9` | Plan §8.2. Dated records still say "GateLink M*n*" and mean `GL`*n* |
| L4 declares four unnamed rows: `relay_min_spacing_ms`, `vedirect_stale_s`, `inject_spacing_ms`, `buzzer_enable` | Plan §6.4, the table |
| R-4.3i's gain and envelope rows stay out of L4; finding 2 is settled by renaming | PRD v0.14 changelog |
| The bridge's `kMaxPayloadLen` rises to 2048 | Bridge engineering log, 2026-10-01 |

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`LRAN-GateLink_Node-Implementation-Plan`](./LRAN-GateLink_Node-Implementation-Plan.md) §5 and §8 | the architecture and the task list. Read §2–§4 only for a hardware or interface task |
| 3 | [`doc-findings`](./doc-findings.md) | the PRD and register defects still open; a task that touches one fixes it |
| 4 | [`gatelink-expansion-board`](./gatelink-expansion-board.md) | the carrier. Needed from GL0 on, and for anything that names a pin |
| 5 | [`LRAN-GateLink_Node-PRD`](./LRAN-GateLink_Node-PRD.md) | requirements by identifier, when a task cites one |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Plan v0.41. The PHY trial, on the air 2026-10-13. L1, L2, L3, L4, L5, L6, L7, the split readback, the document amendments. GL0. `wattcycle-reader` M0–M8 (its own milestones) |
| In progress | GL1: done; PRD v0.18 drops R-3.5j's brownout leg. GL3: BOOT, the roll, polls, PING and the command path pass on the air, dedup and resync included, the task watchdog arms and fires, CONFIG passes with the card and without it, and every reset cause the bench can produce passes against spec §10.7, and state derivation, direction and the held-open alert pass by injection on the air. R-3.5i passes on the air (2026-10-24). GL4: every criterion passes on the bench except M14, now possible with the MPPT back on the gate's PV. GL5: `bms_task` reads the pack, the window and abort latency are measured, `bms_window_max_ms` is set, M7 is closed, `vedirect_task`'s CPU share beside NimBLE is measured, and low-temperature inhibition passes on the air by the BMS path; M23, the VE.Direct path on the air and `0x8D` remain |
| Not started | GL2, GL6–GL9 |
| Queue | The rest of §8.1, in any order |

```bash
pio test -d firmware/gatelink -e native       # task table, boot page, pulse, sequencer, GateLinkApp, VedLink, BMS block, config store, gate state, detection, injection
python3 tools/checks/io_task_never_blocks.py  # R-5.2a (L6)
pio test -d firmware/simnode -e native        # L1's regression suite; must stay green
pio test -d lib/lran-node -e native           # the node engine (L1), split readback
pio test -d lib/lran-protocol -e native       # codec, CommandGate, schemas
pio test -d lib/lran-config -e native         # parameter table and Store (L4)
python3 tools/checks/config_doc.py            # gatelink-config.md against the table (L4)
python3 tools/provision/node_key.py --self-test   # node key derivation (L5)
pio test -d lib/vedirect -e native            # HEX codec, text parser, the captured blocks (L3, GL4)
pio test -d lib/bms-ble -e native            # 22 TDT protocol tests (L2, L7)
python3 tools/checks/run_ci_local.py          # CI's checks job
```

## Git state — ask git, do not read it here

```bash
git fetch origin -p                            # prune deleted remote branches first
git log --oneline -1 origin/main                # where main actually is
gh pr list --state open                         # what is open, if anything
git log --branches --not --remotes --oneline    # local-only work; empty is good
```

**Run `git fetch` before trusting any of it.** Two machines push to this repository.

## Hardware state

| Device | Called here | Told apart by | Firmware / env | Stored state | Current state |
|---|---|---|---|---|---|
| M5Stack StamPLC (K141) | **the StamPLC** | DIN case with screw terminals and a colour LCD; nothing else in the fleet looks like it | **The original unit, MAC `50:78:7d:cd:c9:94`**, on `firmware/gatelink -e gatelink` at `1049387`, flashed over USB from the Mac on 2026-10-25. The replacement, MAC `ac:a7:04:03:52:0c`, ran the new-hardware bench work at `aaf87e8`. The node key is derived from the master and node ID `0x01`, not the MAC, so one image serves either board | A microSD card the board mounts, with `/config.json` holding 3 overrides that boot restores. Those three are `charge_inhibit_temp_c` 0, `charge_inhibit_band_ma` 100 and `charge_inhibit_rise_mv` 20, the default values, each still marked `source: override` (engineering log, 2026-10-24). The 2026-10-15 test values are gone | On the operator's workbench with the carrier fitted and a 12 V supply on VIN. IN1–IN4 wired sinking: COM at +12 V, an input to GND to energize it, IN3 and IN4 on GND for idle. **IN1–IN6 read** since the direction-register fix (engineering log, 2026-10-24); the firmware reads all six (D83). At `/dev/cu.usbmodem101` on 2026-10-24. The expansion board's power fault was fixed that day |
| XIAO ESP32S3 + Wio-SX1262 **Kit** (p-5982) | **the XIAO Kit** | XIAO with a B2B-connected module; the only board with that stack | `firmware/simnode -e simnode-xiao-wio`. The bridge's handoff owns it as the target-radio simnode | A committed PHY group in NVS (the bridge handoff's *Hardware state*) | Borrowed from the bridge bench. `/dev/cu.usbmodem2101` on 2026-10-02, flashed with the split-readback simnode image, `f1` in `ROLE_GATELINK`. Proves the Wio's radio configuration, **not** the carrier's wiring |
| Wio-SX1262 for XIAO **header board** (p-6379) | **the carrier's module** | 2.54 mm headers, no XIAO attached | — | — | **In hand and seated in the carrier** (operator, 2026-10-05) |
| Carrier (expansion board rev 0.3) | **the carrier** | Perfboard on a right-angle 2×8 header | — | — | **Built.** Rails clean and netlist buzzed out, by operator report on 2026-10-05; antenna connected. J4 built as a 2×2 header, and the VE.Direct harness to the gate's MPPT fitted and corrected on 2026-10-08 |
| Heltec WiFi LoRa 32 V3 | **the simnode Heltec** | OLED on the board, USB-UART bridge (`/dev/cu.usbserial-*`) | `firmware/simnode -e simnode-heltec`, with the L7 BMS emulator and `bms temp`, flashed 2026-10-14 at `a49a90a` | The bridge handoff's *Hardware state* | `/dev/cu.usbserial-4` on 2026-10-02. The bridge handoff owns the board. The emulator is off at boot; `bms on 49A1` starts it for `wattcycle-reader` |
| MPPT 75/15 (PID `0xA075`, FW 175), WattCycle pack, Nice/Apollo 1050 | the installation | At the gate, ~87 m | Charger settings saved from VictronConnect, 2026-10-08; as configured, [`mppt-config.md`](./mppt-config.md) | The 1050's programming, to be recorded in `docs/gatelink/1050-config.md` at GL2 | **All back at the gate** (operator, 2026-10-13). Not yet rewired or reprogrammed (GL2) |

**A wrong module selection is silent.** A pin table taken from the Kit produces a carrier
that looks configured and never answers. The Kit's control lines are GPIO 38–42; the header
board's are its D-pads (expansion board §6.1).

## Behaviour that changed, and will make older artifacts read differently

- **Milestone names.** Before plan v0.18, GateLink milestones were `M0`–`M9`, which the
  specification, the Decision Register and dated briefs still use. `GL`*n* is the old `M`*n*.
  A bare `M`*n* in a GateLink document written from v0.18 on is a measurement.
- **The carrier.** Plan revisions before v0.18 describe an LDO on `EXT_5V`, radio reset on
  G14, and VE.Direct on PORT.A. They were correct for the design of their time and do not
  describe rev 0.3. Any harness or header made to them is wrong.

## Traps that cost real time here

- **A StamPLC on its 12 V supply never appears on a USB-C host port.** It backfeeds 5 V
  onto VBUS. A USB 2.0 hub with USB-A ports works (engineering log, 2026-10-05 and
  2026-10-06).
- **A microSD card over 32 GB will not mount.** The core builds FatFs without exFAT, so
  `SD.begin()` reports `(13) There is no valid FAT volume`. The bring-up image's
  `sd format` makes a FAT32 volume, and only on a card without one.
- **The console holds back the last line of a long command.** It appears with the next
  command's output; send `stat` to flush it. Don't read a missing summary as a hang.
- **Busy-waiting SPI drivers starve IDLE0.** Three tasks sharing the bus lock at priority
  2 on CPU 0 tripped the task watchdog in under 10 s. GL1's board layer meets this too.
- **RadioLib's `begin()` clears DIO1's pull-down.** Set it again after `begin()`.
- **`Serial` is silent on the StamPLC without `-DARDUINO_USB_CDC_ON_BOOT=1`.** Boot ROM lines
  and NimBLE logs still appear, so it looks like it works.
- **That flag makes RadioLib 7.7.1 emit a `#warning`**, which `-Werror` turns into a failed
  build. Use `-Wno-error=cpp` in that one env, as `simnode-xiao-wio` does.
- **The published M5StamPLC package drifts from its GitHub `main`.** Read the installed
  headers under `.pio/libdeps/`, not GitHub.
- **The StamPLC and the XIAO both enumerate as `/dev/cu.usbmodem*`.** The bridge handoff
  identifies the XIAO as "the only `usbmodem` port"; with the StamPLC plugged in, that
  stops being true. The boot banner does not print the MAC; `esptool.py --port <port>
  read_mac` does, and resets the chip.
- **Bus 14 is labelled `CS` and carries BUSY**; NSS is on Bus 16. Expansion board §6.
- **An open DIO1 conductor fails late**, as transmits that never complete, not at
  `radio.begin()`. Plan §4.1 and expansion board §7.1.1.
- **The panel is landscape 240×135**, not the 135×240 its name suggests. GateLink turns it
  180° and insets each line 6 pixels; text at x = 0 is under the bezel.
- **A power cycle on the bench can boot twice.** If `bench.py` reopens the serial port as
  the board comes up, the open resets the chip over USB, and the console shows the second
  boot as `Reset: usb`. A true power cycle removes USB and VIN together.
- **A StamPLC that macOS lists but gives no `/dev/cu.usbmodem*`** needs a replug. The
  USB device shows `!registered, !matched` in `ioreg -p IOUSB` until it does.

## Open, and not closable from here

- **Why the direction write is lost is not established.** The same write in `board_begin()`
  did not hold. M5GFX pulses GPIO 3 inside `M5.begin()`, and M5StamPLC's
  `io_expander_b_init()` calls `configureDirection(0x0)`, which sets every pin to output
  despite its `// all inputs!` comment. The write is a fix, not an explanation.
- **IN5's and IN6's polarities are the operator's description, not a measurement.**
  `kInputsActiveLow` `0x1C` has IN5 invert and IN6 not, because the 1050's alarm output
  drives 12 V on an alarm. M3 measures both at the gate before they are connected, and
  whether the alarm output needs a pull-down to hold the 2N7000 off.
- **The console splices lines under load.** Two `alarm:` lines arrived cut short and
  joined to the next `gate:` line (`FIgate: CLOSED`), as a boot line did earlier
  (`BUSY fell itored`). Whether `log_task` or `bench.py` drops the bytes is not
  established.
- **The RTC reads `2049-12-01`** and reports itself set (engineering log, 2026-10-24).
  Nothing on the wire reads it yet.
- **The pack's real low-temperature cut-off is not measured**, so `charge_inhibit_temp_c`
  defaults to PRD §6.4's "approximately 0 °C". A `0x8C` and `0x8D` capture on a cold
  morning at the gate would set it and start mapping `0x8D`'s bits.
- **The BMS misses one window about every 3.5 to 3.7 h** in both soaks (engineering log,
  2026-10-25 and 2026-10-26). In the second run, two single misses went past the bridge's
  `bms_stale_s` of 600 and marked the battery unavailable. The cause is not established.
  `/bms.log` on the card names how each missed window ended; see *Start here*.
- **GateLink's console cannot print a file from the card.** Reading `/bms.log` at the gate
  means opening the enclosure for the card.
- **`charge_fet` and `discharge_fet` read false and `alarms` 65535 throughout the soak**,
  and `pack_ma` read 0. Whether those are the pack's real states or a decoding gap is not
  established.
- **The bridge's `rx_no_interrupt` and `rx_wake_empty` rose from 0 to 4 in the first
  soak**, and stayed 0 through the second. Not looked into.
- **The BMS path has no hysteresis.** A pack sitting at the threshold could send a STATUS
  and an event on every poll, every 300 s at most.

- **PRD R-3.2.3e does not say `held_open_alert_repeat_s` is a rate limit.** The operator
  confirmed it is (2026-10-13), and `detect.h` says so. The next PRD revision states it.
- **Event to MQTT took up to 6 s on the bench**, most under 2 s (engineering log,
  2026-10-13). Not lost, and the cause is not established.
- **A `ctx_id` drawn after boot may not come from a true entropy source.** `ROLL_CONTEXT`
  and `lran ctx new` call `esp_random()`, which is true random only while the RF subsystem
  runs, and BLE is down between windows. Spec §10.1 asks for true entropy at every draw.
- **The `BOOT` event's `input_bits` is always 0**, because it is built before `io_task`'s
  first read. Harmless while the BOOT event carries no input state; a spec reader may
  expect otherwise.
- **R-3.5j's *Verified by* says "on a scope"**; GL1 used a logic analyzer on the contacts,
  which cannot see a coil glitch shorter than the operate time. Proposed: accept the
  analyzer, with that limit stated. The operator's call, then a PRD and plan §8.2 edit.
- **A watchdog reset holds an energized relay until `setup()`** (engineering log,
  2026-10-07). A hang 100 ms into a 500 ms pulse would close the relay for about 1 s. Any
  fix is a design question, since releasing the relay at the panic would mean I²C from
  the panic handler.
- **The interrupt-watchdog panic printed `Re-entered core dump!`** before rebooting. The
  reset still happened. Unexamined.
- **The GateLink console loses the middle of a line under load** (engineering log,
  2026-10-08, *GL3's command path on the air*). `log 0` shows the queue dropped nothing,
  so the loss is in the USB CDC path. It hid one pulse width on the bench.
- **The console's `sd` hangs the node when no card is fitted** (replacement StamPLC, no
  card and no carrier, 2026-10-23, at `969cdf7`). Twice, `sd` printed nothing, and the
  console then stayed silent, with no `alive:` line for over 30 s and no watchdog reset. A
  USB reset recovered it. Boot's own mount fails without hanging. The cause is not
  established; a second `SD.begin()` after a failed one is the first suspect.
- ~~**`bench.py run` exits when it writes to a port that has disappeared.**~~ **Closed
  2026-10-24:** the write side now logs `<TAG not written: …>` and carries on, and
  `reader()` reopens the port. `test_bench.py` covers it.
- **The carrier's 3.3 V LED stayed lit with the 12 V off and USB attached.** The carrier
  draws only from Bus pin 1, so something reaches that pin from USB, unless the supply was
  not fully off. With 12 V on, USB carries all but 3.1 mA of the board's load. Find the
  path, or confirm the supply was off.
- **GateLink's USB console loses bytes from the middle of lines**, about one line in ten
  (engineering log, 2026-10-08). Splitting the `ved` line did not stop it.
- **Some `bms now` results printed about 13 s after `window now`**, though each window
  reported about 1.2 s (engineering log, 2026-10-10). Whether the window started late or
  its line printed late is not established.
- **Decision Register D49 says `APPLIED_NOT_PERSISTED` means no usable store, "microSD on
  GateLink"**. GateLink's PHY group is now in NVS (plan §6.4), so D49's wording may need an
  amendment. The register's owner should decide.
- **The SD library prints its retries to `Serial` from `lora_task`**, through the core's
  `log_w` at `CORE_DEBUG_LEVEL=2`. That breaks *Only `log_task` writes to `Serial`*
  (`firmware/gatelink/CLAUDE.md`), and only configuration traffic with a bad card reaches it.
- **`boot_count` and `state.json` are not built** (plan §6.4's file table). STATUS sends
  `boot_count` 0.
- ~~**`beep` logs `LEDC is not initialized` on first use.**~~ **Closed 2026-10-24:** the
  operator heard the buzzer while `beep` logged the error, so the error is cosmetic.

- **`wattcycle-reader`'s `loop()` ticks the reassembler with a clock read before its
  blocking write.** The library tolerates it now. GateLink's `bms_task` should read its
  clock after the write.

- ~~**`bench.py` loses its MQTT keepalive in a broker outage.**~~ **Closed 2026-10-25:**
  `_ping` skips a missing socket and logs any error but `OSError`. Tested against a fake
  broker only; the restarted soak's first broker outage is the real test.

- **The bench `secrets.h` is temporary.** It holds bench WiFi, the sandbox broker's
  credentials and a bench master key, and the operator creates a production `secrets.h`
  before the final production builds. The GateLink node key derived from the bench master
  appeared in a session transcript on 2026-10-02. That is acceptable only because the
  bench master is replaced: **production GateLink and bridge images must be built from the
  production `secrets.h`, with the node key derived again from its master.**
- **A leaked node key costs the whole fleet.** Spec §9.1 derives each key from the master
  and the node ID alone, so replacing GateLink's key means a new master and a reflash of
  every node. A per-node key generation would contain it. That is a protocol question,
  not a GateLink one.
- **The pack is matched by its advertised name, `XDZN_001_49A1`, compiled in**
  (`board_profile.h`). A replacement pack means a USB reflash at the gate. A parameter
  cannot hold a name; whether to match any `XDZN_001_` instead is the operator's call.
- **Spec §7.2.3 names no sentinel for `cell_temp_c`.** GateLink sends `INT8_MIN` for a
  sensor the pack did not report, under root rule 6. The specification should say so.
- **`bms_alarms` and `bms_flags` bits 1–5 are sent unavailable**, because `0x8D` is not
  decoded (`bms-protocol.md` §9). R-3.4a and R-3.4c need it, and the decode needs a
  capture during a protection event.
- **A `COMMAND_ACK` queued at the worst moment of a window keeps about 1.9 s of the
  bridge's 3 s ACK timeout** for media access and airtime: the connect can hold the abort
  for 1.1 s. A late ACK costs one retry and a `DUPLICATE_CACHED`, not a second pulse.
- ~~**The scheduled poll at `bms_poll_s` was not watched on the bench.**~~ **Closed
  2026-10-25:** the first soak watched it for 24 h, and it misses about once every 3.7 h
  (above).
- **Five `CLAUDE.md` passages describe what the file used to say**: range-test's 2.0 dBi
  note, simnode's Kit-variant warning and its `CONFIG_ACK` note, wattcycle-reader's
  `lran-prd-v0_5` §5.7 citation, and the bridge's note on B0's P6-only gating. The
  2026-10-03 prompt audit proposed removing them. That is a style pass, and it waits for
  the operator to ask for one.
- ~~**No split readback has been seen on air.**~~ **Closed 2026-10-24:** GateLink itself
  answered one `get_all` with 34 results in two `CONFIG_ACK` frames (engineering log,
  2026-10-24).
- **PRD R-4.3i's `antenna_gain_dbi` and envelope rows**, M21 handoff items 1–3. They are
  fleet-wide, so they go in node-common and the bridge's block, not GateLink's. Kept out
  of L4 by operator decision, 2026-10-01. Until then `tx_power_dbm`'s maximum is the
  ceiling.
- **GateLink's 20 ranges are proposals.** None is measured. The operator reviews them
  before HA first publishes the names, which are permanent.
- **The bridge's 2048-byte payload has not run on a board.** The bridge handoff's §7 owns
  the reading.
- **Reporting osh-labs deviations upstream is owed**, by operator decision 2026-10-01; the
  timing is the operator's. [`lib/vedirect/osh-labs-deviations.md`](../../lib/vedirect/osh-labs-deviations.md)
  lists them. Re-check each row against upstream's latest commit first, report rows 1, 4
  and 5 and defects D1 and D2, and put each issue link in the row's *Reported* column.
- **An SD write holds the SPI bus lock for up to 59 ms**, and the radio waits behind it
  (engineering log, 2026-10-06). GL3's radio driver inherits that wait.
- **About 7.7 kΩ of RST pull-up is unexplained**, beyond the Wio's 10 kΩ. R4 measured out
  of circuit, or RST measured with the Wio pulled, would settle it. It changes nothing
  while the boot check stands.
- **Expansion board §11 step 6** waits for the gate.
- **`0xEDF4` reads 0 on the gate's MPPT**, and the simnode's simulated MPPT holds 1420.
  VictronConnect does not show the setting. A simnode change, if anyone wants it.
- **The StamPLC was silent on the air for 135 s before a replug** (engineering log,
  2026-10-08). Nothing explains it yet; watch for a repeat.
- **Measurements** M1–M3, M8–M11, M13, M14, M16 and M23. The register holds their status.
- **The first BMS window after a boot ended `aborted` at about 45 ms, twice** (engineering
  log, 2026-10-10, *M7*). Probably the boot's LoRa traffic; not checked.
- **The bridge's B6 and B7** wait on GL6. **BF-30**'s scales agree with VictronConnect for every register the app shows; `0xEDF4` and `0xEDF2` read 0, so their scales are not exercised. The LiFePO4 settings disable both, so no readback of this configuration will exercise them.
- **W17** stays open until after GateLink deploys, by operator decision (D59).

### Closed, and not to be reopened by habit

- **D30** — no co-processor. A direct SX1262 on the carrier is the plan of record, with
  three written triggers for revisiting it (plan §6.7).
- **The carrier design's authority** — the expansion board, operator decision 2026-10-01.
- **HEX is in scope for the first release.** PRD R-3.3e sets its write-authentication
  rule, and the bridge's HEX path is built and accepted at B5. Expansion board §12's open question is answered.
- **D24** — two manual UNLOCK paths, the handheld remote and the panel pushbutton.
