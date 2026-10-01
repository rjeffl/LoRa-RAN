# `gatelink` — session handoff

**Written 2026-10-01, at the end of the session that built L3.** It replaces the file the
session that ran L1's bench checks wrote.

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
| **L2** — move `bms_ble` to `lib/bms-ble/`, write `bms-protocol.md` | Plan §4.3, §8.1. `wattcycle-reader/CLAUDE.md` and `README.md`, then `wattcycle-reader/docs/wattcycle-reader-poc_3.md` |
| **L4** — GateLink's parameter block | Plan §4.4, §6.4, §8.1. `lib/lran-config/include/lran/config/table.h`. PRD §5.3. `doc-findings` finding 2 first |
| **L5** — node key provisioning | Plan §6.8. Spec §9.1. `secrets.h.example`. `tools/vectors/` for the HKDF |
| **L6** — `firmware/gatelink/` skeleton | Plan §5.1, §5.3, §7.5. `wattcycle-reader/platformio.ini`'s `m5stack_stamplc` env, the simnode's and the bridge's `platformio.ini` |
| **Document amendments** | `doc-findings.md`, findings 3–6, 8 and 9. Each names where the correct statement lives |

**The cleanup the task produced is part of the task.** Close the session by committing,
pushing and opening the PR. Merge it once the operator accepts it, then rewrite this
section and *The next job*.

## The next job, in one place

**L3 is done once its PR merges**, with one criterion left open: its test block is
synthesized, not captured from the MPPT. The first capture at GL4 replaces it. L1 merged
on 2026-10-01. The operator picks the next task from the table above. L4 and L5 are
independent of each other, and either suits a short session.

## What the last session established

- **`lib/vedirect/` parses text blocks.** `include/vedirect/text.h` holds `TextParser`,
  which takes one UART byte at a time, and `decode_mppt()`. 12 tests join the HEX suite's
  12. The bridge's `heltec` and both simnode targets compile it under `-Werror`.
- **The parser has met no real MPPT output.** Its test block is assembled from osh-labs'
  sample, spec §7.2.2 and labels osh-labs names. `OR` and `H23` are absent from it.
- **A HEX line inside a text block resumes the block**, as osh-labs' specification says
  and its code does not. The `interrupted` counter is the check that would show it wrong.
  The engineering log's L3 entry has the reasoning, and
  `lib/vedirect/osh-labs-deviations.md` lists every departure from upstream.
- **osh-labs is not proven on a 75/15**, by its own specification. Plan v0.19 says so.

## Decisions taken 2026-10-01, by the operator

| Decision | Recorded in |
|---|---|
| The carrier is `gatelink-expansion-board` rev 0.3; the plan defers to it | Plan header, §2–§3 |
| Extract `lib/lran-node/` from the simnode; GateLink and the simnode both consume it | Plan §5.4, L1 |
| A firmware-local board layer, not `/lib/lran-platform/` | Plan §5.3. **Contradicts System PRD §3.5's SHALL** — `doc-findings` 9 |
| Move `bms_ble` to `lib/bms-ble/` under repo conventions | Plan §4.3, L2 |
| GateLink milestones are `GL0`–`GL9`, renamed from `M0`–`M9` | Plan §8.2. Dated records still say "GateLink M*n*" and mean `GL`*n* |

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
| Done | Plan v0.19. L1. `wattcycle-reader` M0–M8 (its own milestones) |
| In progress | L3: built and host-tested, PR awaiting acceptance |
| Not started | L2, L4–L6. GL0–GL9. `firmware/gatelink/` does not exist |
| Queue | The rest of §8.1, in any order |

```bash
pio test -d firmware/simnode -e native        # L1's regression suite; must stay green
pio test -d lib/lran-node -e native           # the node engine (L1)
pio test -d lib/lran-protocol -e native       # codec, CommandGate, schemas
pio test -d lib/lran-config -e native         # parameter table and Store (L4)
pio test -d lib/vedirect -e native            # HEX codec and text parser (L3)
cd wattcycle-reader && pio test -e native     # 21 TDT protocol tests (L2 moves them)
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
| M5Stack StamPLC (K141) | **the StamPLC** | DIN case with screw terminals and a colour LCD; nothing else in the fleet looks like it | Last recorded running `wattcycle-reader -e m5stack_stamplc` (its README, M7a–M8) | Nothing GateLink depends on | Location not recorded. **No carrier fitted** |
| XIAO ESP32S3 + Wio-SX1262 **Kit** (p-5982) | **the XIAO Kit** | XIAO with a B2B-connected module; the only board with that stack | `firmware/simnode -e simnode-xiao-wio`. The bridge's handoff owns it as the target-radio simnode | A committed PHY group in NVS (the bridge handoff's *Hardware state*) | Borrowed from the bridge bench. Proves the Wio's radio configuration, **not** the carrier's wiring |
| Wio-SX1262 for XIAO **header board** (p-6379) | **the carrier's module** | 2.54 mm headers, no XIAO attached | — | — | **Not recorded in the repo as in hand.** Expansion board §10 says the board that arrived was the Kit. Ask the operator |
| Carrier (expansion board rev 0.3) | **the carrier** | Perfboard on a right-angle 2×8 header | — | — | **Not built.** Expansion board §10's checks are all unticked |
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
- **The panel is landscape 240×135**, not the 135×240 its name suggests.

## Open, and not closable from here

- **L3's captured block.** Capture a raw text block, with a HEX exchange inside it if
  one can be provoked, at GL4, and replace `kMppt7515` in `lib/vedirect/test/test_text/`.
  Check `interrupted` against `bad_checksum` on the same run.
- **Reporting osh-labs deviations upstream is owed**, by operator decision 2026-10-01; the
  timing is the operator's. [`lib/vedirect/osh-labs-deviations.md`](../../lib/vedirect/osh-labs-deviations.md)
  lists them. Re-check each row against upstream's latest commit first, report rows 1, 4
  and 5 and defects D1 and D2, and put each issue link in the row's *Reported* column.
- **§5.2's two questions**: what the `COMMAND_ACK` waits for, and the bound on a BLE window.
  Due before GL3.
- **`doc-findings` 2–6, 8 and 9**: PRD R-4.3b, R-4.3d and R-4.3f, D26, VE.Direct's 5 V vs
  3.25 V, the INA226's two readings, System PRD §3.5. Each needs the operator or a
  measurement.
- **Measurements** M1–M4, M8–M16 and M23, and **M7 / W6** (`pack_ma` sign). The register
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
