# `gatelink` — session handoff

**Written 2026-10-10 by the MPPT readback session**, which changed no code. It recorded
the MPPT's settings in [`mppt-config.md`](./mppt-config.md).

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

The tasks are in the order *The next job* gives. Tier 1 needs the WattCycle pack and the
MPPT on the bench, and Tier 3 needs neither.

| Tier | Task | Read |
|---|---|---|
| 1 | **GL5, `bms_task`** against the live pack | Plan §8.2's GL5 row and §5.2's interlock; `lib/bms-ble/`; [`bms-protocol.md`](./bms-protocol.md) |
| 1 | **M7**, pack current under charge and under load | Decision Register M7; plan §8.2's GL5 row |
| 1 | **`vedirect_task`'s CPU share** beside NimBLE, and **L3's captured block** | *Open*, below |
| 2 | **Bench soak through the real power chain**, 24 to 72 h | Plan §8.2's GL9 row for what the field soak will watch |
| 3 | **GL3, reset causes** | Spec §8.14 and §10.7; PRD R-3.5f–R-3.5k; plan §4.1's RST boot check |
| 3 | **GL3, state derivation** by injection | Plan §8.2's GL3 row |
| 3 | **The PHY trial** (spec §12.4.2), or a written decision to deploy without it | Plan §6.4; *Open*, below |
| — | **GL4's M14 log** (needs the MPPT back on the gate's PV) | Plan §8.2's GL4 row and §9.8; the engineering log's 2026-10-08 *Writes through the broker* entry for the history read |

**The cleanup the task produced is part of the task.** Close the session by committing,
pushing and opening the PR. Merge it once the operator accepts it, then rewrite this
section and *The next job*.

## The next job, in one place

**The operator is bringing the WattCycle pack to the bench beside the MPPT, and the gate is
down while it is here.** So the bench window does only the work that needs the pack or the
MPPT, then both go back. GateLink has no OTA, so every firmware item left after install
costs a walk to the gate. The tiers below finish the firmware before deploy without
keeping the gate down for work that doesn't need the pack.

**Tier 1 needs the pack and the MPPT. Do it first, in this order:**

1. **Build GL5's `bms_task` against the live pack.** Check its decode against
   `wattcycle-reader`, then measure the window and abort latency, which set
   `bms_window_max_ms`'s default and range. NimBLE's logging moves onto the leveled log
   here.
2. **Capture M7**, pack current under charge and under load. The charge leg needs a
   PV-side source: a current-limited lab supply at least about 5 V above the pack voltage.
   Without one, only the load leg runs.
3. **Measure `vedirect_task`'s CPU share with NimBLE running.**
4. **Capture L3's raw text block** while the MPPT is connected.

**Tier 2 uses the pack as realistic power.** Soak for 24 to 72 h through the real power
chain (MPPT, pack, StamPLC), with the radio, VE.Direct, BMS polls and SD writes all active.
It finds the faults that would otherwise cost walks to the gate. The INA226's node draw is a
first read on the power budget. It costs gate downtime, so it is the operator's call.

**Then the pack and the MPPT go back to the gate.** Once the MPPT is back on PV, its own
daily history (yield, Vmin) can be read in VictronConnect. A week of it could stand in for
M14's log, which would close GL4 without GateLink at the gate. Changing M14's method is the
operator's call, and plan §9.8 changes with it.

**Tier 3 runs on the StamPLC's 12 V supply**, with the simnode Heltec's BMS emulator in place
of the pack. It must finish before deploy:

- GL3's reset-cause slice, and its state derivation by injection.
- The PHY trial, or a written decision to deploy without it.
- The relay-at-watchdog, `AckResult` and `0xFF` questions under *Open*, and the SD
  library's `log_w`.
- Low-temperature inhibition through the BMS emulator.
- The 1050 interface harnesses, built on the bench to shorten the install visit.
- Last of all, the production build from the production `secrets.h`.

**At the gate, each needing its own visit:** GL2 (the 1050 must be powered, so the pack has
to be back), M23, GL6, then GL7. The bridge handoff carries the bridge and Home Assistant
work that has to be ready by the install visit.

The node's refusals of a bad MAC, a stale `seq` and an unauthenticated Set are host-tested
only, because the bridge always sends a valid MAC and resets `seq` itself.

## What the last session established

- **The MPPT's settings agree three ways**: VictronConnect, the raw HEX answers and BF-30's
  readback. [`mppt-config.md`](./mppt-config.md) has the table. Nothing changed since the
  2026-10-08 readback.
- **R-6.1b is met** for battery type, equalisation, temperature compensation and float.
  Absorption, 14.20 V, is the low end of the pack's 14.2–14.6 V, kept for cell life.
- **`0xEDF1` reads `0xFF`** while VictronConnect names the preset *Smart Lithium
  (LiFePo4)*.

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
| Done | Plan v0.33. L1, L2, L3, L4, L5, L6, L7, the split readback, the document amendments. GL0. `wattcycle-reader` M0–M8 (its own milestones) |
| In progress | GL1: done except R-3.5j's brownout leg, left open by the operator. GL3: BOOT, the roll, polls, PING and the command path pass on the air, dedup and resync included, the task watchdog arms and fires, and CONFIG passes with the card and without it. State derivation and the reset-cause slice remain. GL4: every criterion passes on the bench except M14, which waits for the MPPT to go back on the gate's PV |
| Not started | GL2, GL5–GL9 |
| Queue | The rest of §8.1, in any order |

```bash
pio test -d firmware/gatelink -e native       # task table, boot page, pulse, sequencer, GateLinkApp, VedLink, config store
python3 tools/checks/io_task_never_blocks.py  # R-5.2a (L6)
pio test -d firmware/simnode -e native        # L1's regression suite; must stay green
pio test -d lib/lran-node -e native           # the node engine (L1), split readback
pio test -d lib/lran-protocol -e native       # codec, CommandGate, schemas
pio test -d lib/lran-config -e native         # parameter table and Store (L4)
python3 tools/checks/config_doc.py            # gatelink-config.md against the table (L4)
python3 tools/provision/node_key.py --self-test   # node key derivation (L5)
pio test -d lib/vedirect -e native            # HEX codec, text parser, the captured block (L3, GL4)
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
| M5Stack StamPLC (K141) | **the StamPLC** | DIN case with screw terminals and a colour LCD; nothing else in the fleet looks like it | `firmware/gatelink -e gatelink`, the node image with `vedirect_task`, flashed 2026-10-08 at `cbacd3f`. MAC `50:78:7d:cd:c9:94` | A 128 GB microSD card, formatted FAT32 on the board, holding the bus tests' `/gl0bus.bin` and `/gl1bus.txt` | On the operator's workbench with the carrier fitted and a 12 V supply on VIN, a DVM in series with it and a bench switch on IN8. Reached through a USB 2.0 hub, at `/dev/cu.usbmodem11301` on 2026-10-08, after a replug. The expansion board's power fault was fixed that day |
| XIAO ESP32S3 + Wio-SX1262 **Kit** (p-5982) | **the XIAO Kit** | XIAO with a B2B-connected module; the only board with that stack | `firmware/simnode -e simnode-xiao-wio`. The bridge's handoff owns it as the target-radio simnode | A committed PHY group in NVS (the bridge handoff's *Hardware state*) | Borrowed from the bridge bench. `/dev/cu.usbmodem2101` on 2026-10-02, flashed with the split-readback simnode image, `f1` in `ROLE_GATELINK`. Proves the Wio's radio configuration, **not** the carrier's wiring |
| Wio-SX1262 for XIAO **header board** (p-6379) | **the carrier's module** | 2.54 mm headers, no XIAO attached | — | — | **In hand and seated in the carrier** (operator, 2026-10-05) |
| Carrier (expansion board rev 0.3) | **the carrier** | Perfboard on a right-angle 2×8 header | — | — | **Built.** Rails clean and netlist buzzed out, by operator report on 2026-10-05; antenna connected. J4 built as a 2×2 header, and the VE.Direct harness to the gate's MPPT fitted and corrected on 2026-10-08 |
| Heltec WiFi LoRa 32 V3 | **the simnode Heltec** | OLED on the board, USB-UART bridge (`/dev/cu.usbserial-*`) | `firmware/simnode -e simnode-heltec`, with the L7 BMS emulator | The bridge handoff's *Hardware state* | `/dev/cu.usbserial-4` on 2026-10-02. The bridge handoff owns the board. The emulator is off at boot; `bms on 49A1` starts it for `wattcycle-reader` |
| MPPT 75/15 (PID `0xA075`, FW 175), WattCycle pack, Nice/Apollo 1050 | the installation | At the gate, ~87 m | Charger settings saved from VictronConnect, 2026-10-08; as configured, [`mppt-config.md`](./mppt-config.md) | The 1050's programming, to be recorded in `docs/gatelink/1050-config.md` at GL2 | **The MPPT is on the operator's bench, on a bench supply with no PV**, since 2026-10-08; the gate runs on its battery alone meanwhile. The rest is installed, not yet rewired or reprogrammed (GL2) |

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
  stops being true. Read the MAC from the boot banner.
- **Bus 14 is labelled `CS` and carries BUSY**; NSS is on Bus 16. Expansion board §6.
- **An open DIO1 conductor fails late**, as transmits that never complete, not at
  `radio.begin()`. Plan §4.1 and expansion board §7.1.1.
- **The panel is landscape 240×135**, not the 135×240 its name suggests. GateLink turns it
  180° and insets each line 6 pixels; text at x = 0 is under the bezel.
- **A StamPLC that macOS lists but gives no `/dev/cu.usbmodem*`** needs a replug. The
  USB device shows `!registered, !matched` in `ioreg -p IOUSB` until it does.

## Open, and not closable from here

- **R-3.5j's brownout leg is unverified.** The bench supply stops at 5.25 V, where the
  MP4560 still regulates. The operator left it open on 2026-10-07: a VIN sag that deep
  means the LiFePO4 BMS failed, which is beyond any failsafe's scope. PRD §3.5's
  preamble still names a brownout as a reset GateLink must survive. Revise R-3.5j, or run
  the leg with a supply that reaches the reset point.
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
- **`bench.py run` exits when it writes to a port that has disappeared**
  (`PortNotOpenError` at `bench.py:259`); the read side reopens, the write side does not.
- **Protocol Specification §7.2.4 calls `node_ma` the "INA226 supply current"**, which
  the INA226 cannot give (PRD R-4.4b, v0.17). The field stays and carries its sentinel; the
  note waits for the next specification revision.
- **The carrier's 3.3 V LED stayed lit with the 12 V off and USB attached.** The carrier
  draws only from Bus pin 1, so something reaches that pin from USB, unless the supply was
  not fully off. With 12 V on, USB carries all but 3.1 mA of the board's load. Find the
  path, or confirm the supply was off.
- **Spec §7.2.2 names no sentinel for its `uint8` code fields** (`charge_state`, `mppt_err`,
  `mppt_tracker`). GateLink sends `0xFF` for one missing from a good block
  (`ved_link.h`, `kCodeNotAvailable`). A non-zero `mppt_err` triggers a push, so the
  specification should say what `0xFF` means.
- **GateLink's USB console loses bytes from the middle of lines**, about one line in ten
  (engineering log, 2026-10-08). Splitting the `ved` line did not stop it.
- **`vedirect_task` wakes about 240 times a second**, once per burst of received bytes.
  Measure its CPU share before GL5 puts NimBLE beside it.
- **The PHY group answers `READ_ONLY` on GateLink**, because spec §12.4.2's trial is not
  built here (plan §6.4). A fleet PHY change cannot include GateLink until it is.
- **The SD library prints its retries to `Serial` from `lora_task`**, through the core's
  `log_w` at `CORE_DEBUG_LEVEL=2`. That breaks *Only `log_task` writes to `Serial`*
  (`firmware/gatelink/CLAUDE.md`), and only configuration traffic with a bad card reaches it.
- **`boot_count` and `state.json` are not built** (plan §6.4's file table). STATUS sends
  `boot_count` 0.
- **`beep` logs `LEDC is not initialized` on first use**, though the buzzer was heard. Send
  one `beep` while listening to tie the two together.

- **`wattcycle-reader`'s `loop()` ticks the reassembler with a clock read before its
  blocking write.** The library tolerates it now. GateLink's `bms_task` should read its
  clock after the write.

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
- **`nimble_transport.cpp` logs through `Serial`**, as the PoC did. GateLink's leveled log
  (GL1) should carry those lines before `bms_task` uses the file, at GL5.
- **System PRD §9.1's layout is stale** beyond the `bms-ble` and `lran-platform` lines: it
  lists `gatelink-config.md` and `THIRD_PARTY_NOTICES.md` as not yet written, and the
  bridge and GateLink firmware as planned. A System PRD style revision, not GateLink's.
- **The bridge and simnode `CLAUDE.md` files cite old document versions**: PRD v0.17,
  Impl Plan v0.75 and Tasks v0.61, where the documents are at v0.18, v0.82 and v0.69.
  Check the sections and `BF-*` numbers each file cites before moving the citations, as the
  root `CLAUDE.md`'s *Check the version* rule asks. The bridge's to reconcile.
- **Five `CLAUDE.md` passages describe what the file used to say**: range-test's 2.0 dBi
  note, simnode's Kit-variant warning and its `CONFIG_ACK` note, wattcycle-reader's
  `lran-prd-v0_5` §5.7 citation, and the bridge's note on B0's P6-only gating. The
  2026-10-03 prompt audit proposed removing them. That is a style pass, and it waits for
  the operator to ask for one.
- **No split readback has been seen on air.** The bridge names GateLink's rows only for
  `0x01` (`node_block()`), and a simnode cannot take that ID, so `simnode1`'s readback
  stops at 68 bytes. Showing the split needs the bridge to name GateLink's block for a
  `ROLE_GATELINK` simnode, which changes the simnode's discovery in HA, or needs GateLink
  itself on air. The bridge's call.
- **PRD R-4.3i's `antenna_gain_dbi` and envelope rows**, M21 handoff items 1–3. They are
  fleet-wide, so they go in node-common and the bridge's block, not GateLink's. Kept out
  of L4 by operator decision, 2026-10-01. Until then `tx_power_dbm`'s maximum is the
  ceiling.
- **GateLink's 20 ranges are proposals.** None is measured. The operator reviews them
  before HA first publishes the names, which are permanent.
- **The bridge's 2048-byte payload has not run on a board.** The bridge handoff's §7 owns
  the reading.
- **L3's captured block.** Capture a raw text block, with a HEX exchange inside it if
  one can be provoked, at GL4, and replace `kMppt7515` in `lib/vedirect/test/test_text/`.
  Check `interrupted` against `bad_checksum` on the same run.
- **Reporting osh-labs deviations upstream is owed**, by operator decision 2026-10-01; the
  timing is the operator's. [`lib/vedirect/osh-labs-deviations.md`](../../lib/vedirect/osh-labs-deviations.md)
  lists them. Re-check each row against upstream's latest commit first, report rows 1, 4
  and 5 and defects D1 and D2, and put each issue link in the row's *Reported* column.
- **Spec §8.2 has no `AckResult` for a relay pulse the expander did not carry out.** It
  holds that a pulse "either happens or the node is not running", but an I²C write can
  fail. GateLink counts the failure and answers `ACCEPTED` (plan §5.2). A spec question:
  a new result, or a meaning for `detail` under `ACCEPTED`.
- **An SD write holds the SPI bus lock for up to 59 ms**, and the radio waits behind it
  (engineering log, 2026-10-06). GL3's radio driver inherits that wait.
- **The RST boot check** in plan §4.1 is owed by GateLink's radio driver, at GL3's
  reset-cause slice.
- **`Reset: unknown` after a USB-serial-JTAG reset.** `main.cpp` and `reset_cause()` have no
  case for it, so the `BOOT` event reports `UNKNOWN`. GL3's reset-cause slice.
- **About 7.7 kΩ of RST pull-up is unexplained**, beyond the Wio's 10 kΩ. R4 measured out
  of circuit, or RST measured with the Wio pulled, would settle it. It changes nothing
  while the boot check stands.
- **Expansion board §11 step 6** waits for the gate.
- **`0xEDF4` reads 0 on the gate's MPPT**, and the simnode's simulated MPPT holds 1420.
  VictronConnect does not show the setting. A simnode change, if anyone wants it.
- **The StamPLC was silent on the air for 135 s before a replug** (engineering log,
  2026-10-08). Nothing explains it yet; watch for a repeat.
- **Measurements** M1–M3, M8–M11, M13, M14, M16 and M23, and **M7 / W6** (`pack_ma` sign). The register
  holds their status.
- **The bridge's B6 and B7** wait on GL6. **BF-30**'s scales agree with VictronConnect for every register the app shows; `0xEDF4` and `0xEDF2` read 0, so their scales are not exercised. The LiFePO4 settings disable both, so no readback of this configuration will exercise them.
- **W17** stays open until after GateLink deploys, by operator decision (D59).

### Closed, and not to be reopened by habit

- **D30** — no co-processor. A direct SX1262 on the carrier is the plan of record, with
  three written triggers for revisiting it (plan §6.7).
- **The carrier design's authority** — the expansion board, operator decision 2026-10-01.
- **HEX is in scope for the first release.** PRD R-3.3e sets its write-authentication
  rule, and the bridge's HEX path is built and accepted at B5. Expansion board §12's open question is answered.
- **D24** — two manual UNLOCK paths, the handheld remote and the panel pushbutton.
