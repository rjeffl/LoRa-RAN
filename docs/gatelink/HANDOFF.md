# `gatelink` — session handoff

**Written 2026-10-05, at the end of the session that started GL0.**
It replaces the file the session that made the document amendments wrote.

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

| Task | Read |
|---|---|
| **GL1** (bench, the analyzer half) | *The next job*, below; plan §8.2's GL1 row; PRD R-3.5j. The 2026-10-06 engineering-log entry *GPIO 3 holds the IO expanders in reset* says which reset windows the code does not cover. `firmware/gatelink/CLAUDE.md` lists the console commands |

**The cleanup the task produced is part of the task.** Close the session by committing,
pushing and opening the PR. Merge it once the operator accepts it, then rewrite this
section and *The next job*.

## The next job, in one place

**GL1's two relay checks, on a logic analyzer.** Steps 3–5 of the last list passed and
merged; the analyzer half takes a new branch from `main`. No scope is on the bench. A
HiLetgo USB logic analyzer runs PulseView on the operator's Linux laptop, because
PulseView has no Apple Silicon build. The operator is wiring the relay outputs to a
header, since the screw terminals give the probes no clean connection.

**The wiring for each relay.** COM to analyzer GND, NO to a channel with 10 kΩ to the
analyzer's 3.3 V, so the channel reads low while the contact is closed. Sample at 1 MHz
or faster and capture continuously through each reset. The analyzer sees the contact,
not the coil: a coil glitch shorter than the relay's operate time does not show. Record
that as the method's limit, and propose that R-3.5j's *Verified by* accept it.

1. Each relay through a `relay <k>` pulse: 500 ms ±10 ms, operate and release times
   included.
2. Every relay output through a power cycle with USB unplugged, a `hang` after
   `relay 4 2000`, and a brownout (PRD R-3.5j). USB VBUS keeps the board running, so
   unplug it for the power cycle. **The brownout needs a source**: a supply that can sag
   VIN below the board's reset point. None has been named.

Then close GL1: merge its PR once the operator accepts it.

## What the last session established

- **The node's own tasks share the SPI bus cleanly.** `bus 300` ran the LCD from
  `ui_task`, the microSD from `log_task` and the radio from `lora_task`: 0 bad transfers
  in about 48,000, no reset, and `io_task` on its period throughout.
- **M12: the INA226 reads VIN, and its shunt carries neither the node's current nor Bus
  pin 1's.** A 5.6 mA load on Bus pin 1 left it at 0 mA. PRD R-4.4b, plan §9.8 and V-11
  still rely on it (*Open*).
- **The inputs debounce against a bench switch.** Ten taps gave 20 raw edges and 16
  debounced. IN8 is bit 7, and its terminal is easy to read as IN1's with the board
  upside down.
- **With USB attached, the 12 V feed carries 3.1 mA**, against 43.6 mA without.

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
| Done | Plan v0.29. L1, L2, L3, L4, L5, L6, L7, the split readback, the document amendments. `wattcycle-reader` M0–M8 (its own milestones) |
| In progress | GL0: done except ping and loopback with the bridge, which wait for GL3. GL1: all but the two relay checks on the analyzer |
| Not started | GL2–GL9 |
| Queue | The rest of §8.1, in any order |

```bash
pio test -d firmware/gatelink -e native       # task table and boot page (L6)
python3 tools/checks/io_task_never_blocks.py  # R-5.2a (L6)
pio test -d firmware/simnode -e native        # L1's regression suite; must stay green
pio test -d lib/lran-node -e native           # the node engine (L1), split readback
pio test -d lib/lran-protocol -e native       # codec, CommandGate, schemas
pio test -d lib/lran-config -e native         # parameter table and Store (L4)
python3 tools/checks/config_doc.py            # gatelink-config.md against the table (L4)
python3 tools/provision/node_key.py --self-test   # node key derivation (L5)
pio test -d lib/vedirect -e native            # HEX codec and text parser (L3)
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
| M5Stack StamPLC (K141) | **the StamPLC** | DIN case with screw terminals and a colour LCD; nothing else in the fleet looks like it | `firmware/gatelink -e gatelink`, the node image with GL1's console, flashed 2026-10-06 at `3061caa`. MAC `50:78:7d:cd:c9:94` | A 128 GB microSD card, formatted FAT32 on the board, holding the bus tests' `/gl0bus.bin` and `/gl1bus.txt` | On the operator's workbench with the carrier fitted and a 12 V supply on VIN, a DVM in series with it and a bench switch on IN8. Reached through a USB 2.0 hub, at `/dev/cu.usbmodem11301` on 2026-10-06 |
| XIAO ESP32S3 + Wio-SX1262 **Kit** (p-5982) | **the XIAO Kit** | XIAO with a B2B-connected module; the only board with that stack | `firmware/simnode -e simnode-xiao-wio`. The bridge's handoff owns it as the target-radio simnode | A committed PHY group in NVS (the bridge handoff's *Hardware state*) | Borrowed from the bridge bench. `/dev/cu.usbmodem2101` on 2026-10-02, flashed with the split-readback simnode image, `f1` in `ROLE_GATELINK`. Proves the Wio's radio configuration, **not** the carrier's wiring |
| Wio-SX1262 for XIAO **header board** (p-6379) | **the carrier's module** | 2.54 mm headers, no XIAO attached | — | — | **In hand and seated in the carrier** (operator, 2026-10-05) |
| Carrier (expansion board rev 0.3) | **the carrier** | Perfboard on a right-angle 2×8 header | — | — | **Built.** Rails clean and netlist buzzed out, by operator report on 2026-10-05; antenna connected; VE.Direct cable not fitted |
| Heltec WiFi LoRa 32 V3 | **the simnode Heltec** | OLED on the board, USB-UART bridge (`/dev/cu.usbserial-*`) | `firmware/simnode -e simnode-heltec`, with the L7 BMS emulator | The bridge handoff's *Hardware state* | `/dev/cu.usbserial-4` on 2026-10-02. The bridge handoff owns the board. The emulator is off at boot; `bms on 49A1` starts it for `wattcycle-reader` |
| MPPT 75/15, WattCycle pack, Nice/Apollo 1050 | the installation | At the gate, ~87 m | — | The 1050's programming, to be recorded in `docs/gatelink/1050-config.md` at GL2 | Installed and running the gate today. **Not yet rewired or reprogrammed (GL2)** |

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

- **PRD R-4.4b, plan §9.8 and V-11 need another source for the node's current**, since
  M12 found the INA226 cannot give it. Options: drop node current and use the BMS or MPPT
  figures, which measure the bank; add a current sensor on VIN; or find which socket
  output the shunt carries. The operator's decision, before GL9.
- **The carrier's 3.3 V LED stayed lit with the 12 V off and USB attached.** The carrier
  draws only from Bus pin 1, so something reaches that pin from USB, unless the supply was
  not fully off. With 12 V on, USB carries all but 3.1 mA of the board's load. Find the
  path, or confirm the supply was off.
- **`beep` logs `LEDC is not initialized` on first use**, though the buzzer was heard. Send
  one `beep` while listening to tie the two together.

- **The bridge handoff's *Hardware state* row for the simnode Heltec** still names
  `/dev/cu.usbserial-3` and an image without the emulator. It is the bridge's file to
  rewrite.
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
- **The bridge answers nothing to a `config/set` of 512 bytes or more**
  (`kMaxInboundPayloadLen`). The refusal is counted, but no `config/ack` is published.
  A set of GateLink's 23 non-PHY rows is about 660 bytes. Spec §16.7.3 expects an answer.
  The bridge's to fix or document.
- **The bridge handoff's *Hardware state* row for the XIAO Kit** names
  `/dev/cu.usbmodem1101`. On 2026-10-02 it was `/dev/cu.usbmodem2101`, running this
  branch's simnode image.
- **PRD R-4.3i's `antenna_gain_dbi` and envelope rows**, M21 handoff items 1–3. They are
  fleet-wide, so they go in node-common and the bridge's block, not GateLink's. Kept out
  of L4 by operator decision, 2026-10-01. Until then `tx_power_dbm`'s maximum is the
  ceiling.
- **GateLink's 19 ranges are proposals.** None is measured. The operator reviews them
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
- **§5.2's two questions**: what the `COMMAND_ACK` waits for, and the bound on a BLE window.
  Due before GL3. An SD write holds the bus lock for up to 59 ms, and the radio waits
  behind it (engineering log, 2026-10-06).
- **The RST boot check** in plan §4.1 is owed by GateLink's radio driver, at GL3.
- **About 7.7 kΩ of RST pull-up is unexplained**, beyond the Wio's 10 kΩ. R4 measured out
  of circuit, or RST measured with the Wio pulled, would settle it. It changes nothing
  while the boot check stands.
- **Expansion board §11 step 5** and measurement M4 wait for the MPPT on the bench; step 6
  waits for the gate.
- **`doc-findings` 6 and 8**: VE.Direct's 5 V against 3.25 V, which M4 settles, and the
  INA226's two readings. M12 settled 8's facts; the R-4.4b item above is what remains.
- **Measurements** M1–M4, M8–M11, M13, M14, M16 and M23, and **M7 / W6** (`pack_ma` sign). The register
  holds their status.
- **The bridge's B6 and B7** wait on GL6. **BF-30**'s register scales are confirmed at GL4.
- **W17** stays open until after GateLink deploys, by operator decision (D59).

### Closed, and not to be reopened by habit

- **D30** — no co-processor. A direct SX1262 on the carrier is the plan of record, with
  three written triggers for revisiting it (plan §6.7).
- **The carrier design's authority** — the expansion board, operator decision 2026-10-01.
- **HEX is in scope for the first release.** PRD R-3.3e sets its write-authentication
  rule, and the bridge's HEX path is built and accepted at B5. Expansion board §12's open question is answered.
- **D24** — two manual UNLOCK paths, the handheld remote and the panel pushbutton.
