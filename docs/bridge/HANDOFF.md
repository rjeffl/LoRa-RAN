# Bridge Node — session handoff

**Written 2026-09-23 by the session that opened V-B12's saturated arm and deferred it.**
The documented lever cannot saturate WiFi, and the bench needs a network the operator can
load. [`briefs/2026-09-23-vb12-bench-network-brief.md`](./briefs/2026-09-23-vb12-bench-network-brief.md)
has both findings and the setup options. BF-34 is confirmed on air and merged, and Impl
Plan §6.2.2 records what it decided.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees
> with the documents below, they win — check the log's last entry against the date above
> before trusting anything here.

## Start here

**A session does one task group, and reads only what that task needs.** Open it with one
of these lines, then read this section and the sections the table names — not the whole
file:

```text
Continue from docs/bridge/HANDOFF.md: BF-26, the bench publication gate.
Continue from docs/bridge/HANDOFF.md: BF-24, the publication policy.
Continue from docs/bridge/HANDOFF.md: V-B12's saturated arm, on the bench. <setup>
```

**V-B12's saturated arm is deferred until the operator has set up the bench network.**
When resuming it, replace `<setup>` with the answers to the brief's *Decisions to make
first*: the Mac's internet path, the broker host and its IoT address, whether the bridge's
`secrets.h` already points at the IoT network, and the blaster's control and rate cap if
they are decided. For example:
`Setup: Mac dual-homed (wired main, WiFi IoT); Mosquitto on the Mac at 10.0.20.5; bridge
not yet reflashed; blaster control undecided.`

| Task | Read |
|---|---|
| **BF-26** (no board) | *The next job*; Impl Plan **§4.2a** and **§4.4.2**; Firmware Tasks' BF-26 row; spec **§16.6** |
| **BF-24** (no board) | *The next job*; Impl Plan **§6.3**; Bridge PRD **R-5.2b**; Firmware Tasks' BF-24 row |
| **V-B12's saturated arm** (bench, **deferred**) | *The next job*; the [bench network brief](./briefs/2026-09-23-vb12-bench-network-brief.md); *Hardware state*; [`traps.md`](./traps.md) §*Measuring frame loss*, §*Bench boards and serial ports* and §*Bench credentials*; Impl Plan **§8.1** and **§8.1.1** |

**The cleanup the task produced is part of the task**: stale comments and document lines
it made wrong, `TODO(<id>)` markers it closed, rows here it finished, merged branches and
worktrees. **Close the session** by committing, pushing and opening the PR. Merge it once
the operator accepts it. Then rewrite *Start here* and *The next job* for the next task.
Anything out of scope goes in one line under *Open*, not into the session.

## The next job, in one place

**One observation from the bench run is open.** In one of the six rolls, the bridge sent
its heard-first `POLL` to f1 210 ms before the roll. The first roll attempt went unanswered
and no poll reply was heard. The retry succeeded 2.7 s later. The cause is not shown,
because the simnode logs neither a `POLL` nor its reply. The engineering log's *BF-34 on
air* entry has the detail and two candidate changes. Neither is needed to close BF-34.

**Next: BF-26, then BF-24.** Neither needs a board.

- **BF-26**, `simnode_diag_enable`. Its row is in the table, and nothing reads it yet.
  **Consider carrying it on the lever board** (`levers.h`): `sched_task` reads it and
  `mqtt_task` writes it. The `TODO(BF-26)` gates are in `task_runtime.cpp`. Turning the
  switch on needs `g_availability.mark_known_pending()` and a discovery republish.
- **BF-24**, the decode and publication policy. Its `TODO(BF-24)` markers are in
  `task_runtime.cpp` and `task_runtime.h`.

**Deferred: V-B12's saturated arm.** It waits on two things, and the
[bench network brief](./briefs/2026-09-23-vb12-bench-network-brief.md) has both.

- **A WiFi load.** `diag_interval_s` has a 10 s floor, so it adds three small documents
  every 10 s, and that load is not the flood M22 asks for. A bench-only UDP blaster, rate
  capped and counting what it sent, is the option chosen for planning. Its control and
  build form are not decided. **Impl Plan §8.1 still names `diag_interval_s`**, and its
  correction goes in the same commit as the blaster.
- **A network to load.** The operator prefers to move the bridge, the Mac and a new broker
  to the IoT network, whose 2.4 GHz radio carries only IoT traffic. The Mac must keep
  internet access for the Claude app.

The boards run `98b4b04`. Before the first arm, check with `git diff --stat 98b4b04 HEAD --
firmware lib` whether a reflash is needed; the blaster and the IoT `secrets.h` both need
one. Impl Plan §8.1.1 says how to run the arms: interleave them with the idle control, and
never run them in blocks. **The roll adds one exchange per heard identity after each bridge
boot**, so let every identity roll before a sweep's first arm.

## Open, and not closable from here

- **The state mirror survives a node reboot and nothing invalidates it.** A simnode holds
  its overrides in RAM, so a reboot clears them while `config/state` goes on reporting the
  old values as current. A node with a store (GateLink, microSD, **D49**) keeps them, so
  the bridge cannot tell from the reboot alone — **the answer is a readback when a node's
  `ctx_id` changes**, which deserves its own thought. **It must not fire on a roll**, which
  changes the `ctx_id` and keeps the configuration (spec §10.6 node step 2). Not a
  regression: before BF-32 there was no mirror at all.
- **The bridge loses frames at one metre and the cause is not known.** Spacing is a
  measured variable rather than a suspect; the mechanism is not. The three candidates
  inside the bridge stay ruled out from 2026-09-17, and M25 found nothing on the channel
  loud enough to matter. **A rate measured at one metre is still not evidence about 87 m**,
  and it is optimistic in the wrong direction.
- **`rssi_report.py`'s periodicity verdict is not to be trusted on a long capture.** It
  called the Davis "not periodic" on the day that confirmed its clock to half a second.
  Left unfixed by operator direction; fix it before any future capture, because that
  verdict is what the documents cite when they attribute an occupant.
- **Nothing a node sends the bridge carries a MAC the bridge verifies.** §9.2 makes every
  authenticated type bridge → node, so `rx_rejected_seq` and `rx_dup_command` stay at zero
  by construction.
- **The `radio_ok` half of the OTA verdict is untested on hardware.**
- **M26** — the §3.1 equipment inventory. No link's data rate is confirmed. It no longer
  gates D1 or D33; what still needs it is Decision Register §5.4's attribution of the
  915.8–916.4 MHz cluster.
- **W15** (`CONFIG_ACK` has no override flag, so `config/state`'s `source` is inferred) and
  **W16** (nothing says when a node sends `CONFIG_CHANGE`) — both GateLink's.
- **The whole-document style passes** are owed, on a branch of their own.
- **Nothing checks that `vectors_data.h` matches the W4 JSON.** D57's two vectors went
  unembedded for three days and hid a codec defect (protocol-lib engineering log,
  2026-09-23). Add a step to `ci.yml`'s `checks` job that fails when
  `lib/lran-protocol/test/test_vectors/vectors_data.h` differs from what
  `python3 tools/vectors/embed.py` produces from the committed JSON. Prefer a `--check`
  mode that renders to memory and compares, over writing the file, because
  `run_ci_local.py` runs the checks job on a developer's tree. The same check would help
  for the JSON against `generate.py`. Cite W4 and spec §13.2, and give it a branch of its
  own.

## Reference documents

**Read only the rows your task names in *Start here*.** Rows 2 and 3 are the ones most
worth a targeted read: the log's latest entry for the task, and one section of the traps.

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the **five 2026-09-23 entries** first, last one first: **BF-34 on air**, then **BF-34 built** and its bench steps, then the configuration lock with the `seq` gap it closes, then BF-23's lever half and its bench run. Then the **2026-09-21 entries** — BF-32's bench session and the interleaved sweep — then 2026-09-20 and 2026-09-19. Entries from 2026-09-10 to 2026-09-16 are in [`engineering-log-2026-09-10_2026-09-16.md`](./engineering-log-2026-09-10_2026-09-16.md) |
| 3 | [`traps.md`](./traps.md) | the section for the work you are about to do |
| 4 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§6.2.2** BF-34's context roll; **§4.4.2** BF-23's lever half; **§6.7** BF-32's configuration path and **§6.7.6** its lock; **§8.1** V-B12 and **§8.1.1** what the interleaved sweep found; **§6.6.1** BF-27's frame log; **§10.5** the fault catalogue; **§4.4.1** BF-23's discovery |
| 5 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | §7 is B4, where the work goes next |
| 6 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and what breaks silently |
| 7 | [`firmware/simnode/CLAUDE.md`](../../firmware/simnode/CLAUDE.md) | what the simnode has and its traps |
| 8 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) | §9–§12, §14, §16; **§18.2, never §18.1 alone**. For configuration work: **§7.4**, **§7.4.1**, **§8.10–§8.12**, **§12.4**, **§16.7**. **§10.6** is D58's context roll, which BF-34 built |
| 9 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) | **§3.7** D58's outcome and **§2.3** its reasoning; **§3.4.1** D1 closed at 917.4 MHz and D33's condition 3 restated; **§3.6** D43–D57, the configuration set; **§5.4** M20's channel evidence |
| 10 | [`LRAN-D1-Parallel-Capture-Analysis`](../shared/LRAN-D1-Parallel-Capture-Analysis.md) | why 917.4 MHz won. [`LRAN-D1-Frequency-Change-Brief`](../shared/LRAN-D1-Frequency-Change-Brief.md) is **superseded** |
| 11 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**, **B0**, **B3a**, **B3b**. **M6**, **M19**–**M21**, **M24**, **M25**. **D1** and **D33**, register §3.4.1. **D34 amended**; **D35–D58**. **Protocol Spec v0.13** and its citation sweep, merged. **W4**, **W7**, **W9**, **W10**, **W12**. **V-B3**, **V-B9**, **V-B10**. **BF-2**–**BF-9**, **BF-15**–**BF-22**, **BF-27**'s frame log, **BF-23** both halves, the lever half **confirmed on air**, **BF-32 entire**. **BF-34 confirmed on air** and merged. `firmware/chan-capture/`, `lib/lran-link`'s `ChanMonitor`, `lib/lran-config/` |
| Not done | **B4**: **BF-24**, **BF-25**, **BF-26**. **BF-33** unstarted. **BF-27's other three tools**. **V-B12**'s saturated arm, deferred on its WiFi load and bench network. **M22** open — spacing is now measured, the mechanism is not. **M26**. **BF-11a**, **BF-11b**. The whole-document style passes |
| Queue | **BF-26**, then **BF-24**, neither of which needs a board. **V-B12's saturated arm** once the bench network is set up |

```bash
pio test -d lib/lran-protocol -e native         # library host suite
pio test -d lib/lran-link -e native             # spec 12.3 media access
pio test -d lib/lran-sim -e native              # BF-7's FramePatch vs. the W4 negatives
pio test -d lib/lran-config -e native           # BF-32's table and store
pio test -d firmware/bridge -e native           # bridge host suites, test_context_roll among them
pio test -d firmware/simnode -e native          # simnode host suites
pio run  -d firmware/bridge -e heltec           # bridge target - NEEDS secrets.h
pio run  -d firmware/simnode -e simnode-heltec  # NEEDS secrets.h (key only)
pio run  -d firmware/simnode -e simnode-xiao-wio
python3 tools/checks/lora_task_never_blocks.py  # lora_task blocks on nothing
python3 tools/checks/no_mbedtls_hkdf.py         # HKDF built from HMAC, spec 9.1
python3 tools/checks/bridge_partitions.py       # A/B table
python3 tools/checks/spec_citation_version.py   # binding citations vs. the spec header
python3 tools/checks/ha_examples.py             # ha/discovery/ vs. the firmware
python3 tools/checks/simctl_catalogue.py        # simctl's rows vs. fault.cpp
python3 tools/simctl/test_sweep_analyze.py      # the interleaved sweep's arithmetic
python3 tools/simctl/test_rxlog_analyze.py      # BF-27's frame-log arithmetic
python3 tools/vectors/check.py                  # W4 vectors, self-check
```

**CI runs all of these on every pull request.** Run them locally when you are about to
spend bench time on the result. **Call `pio` and the bench tools' Python by path from a
script**; [`traps.md`](./traps.md#bench-boards-and-serial-ports) says why.

## Git state — ask git, do not read it here

> **Where `main` points, what merged last, which branches exist and whether a PR is open
> are deliberately not written in this file.** A written SHA is wrong the moment the
> branch carrying it merges, and it is wrong in the worst direction — confidently, in a
> file whose whole value is being trustable cold.

```bash
git fetch origin -p                             # prune deleted remote branches first
git log --oneline -1 origin/main                # where main actually is
gh pr list --state open                         # what is open, if anything
git log --branches --not --remotes --oneline    # local-only work; empty is good
git branch -vv | grep ': gone]'                 # local branches whose remote was deleted
```

**Run `git fetch` before trusting any of it.** Two machines push to this repository.
[`traps.md`](./traps.md#git-branches-and-merging) has the merge and push gotchas; read
them before closing a session.

## Hardware state

**All three boards run `98b4b04`**, flashed from a clean tree on 2026-09-23 for BF-34's
bench run. None of them is running `chan-capture`. All three were on USB when this session
ended. The XIAO's port had moved to `/dev/cu.usbmodem2101`. **This table names the devices
in this subproject's terms**; the range-test handoff owns them in its own roles.

| Device | Called here | Told apart by | Firmware | Current state |
|---|---|---|---|---|
| Heltec V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `firmware/bridge -e heltec`. MAC `44:1b:f6:f9:70:14` | **At its production position in the office, NW wall, desk height.** On USB as `/dev/cu.usbserial-0001`. NVS holds the configuration store — clear a bench value with `{"op":"restore_defaults"}` on its `config/set`, not by reflashing |
| Heltec V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `firmware/simnode -e simnode-heltec`. MAC `44:1b:f6:fa:bc:2c` | In the office, about 1.5 m from the bridge board. On USB as `/dev/cu.usbserial-3`. Its port name moves across replug |
| XIAO ESP32S3 + **Wio-SX1262 Kit** | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `firmware/simnode -e simnode-xiao-wio`. MAC `68:ee:8f:4b:85:f4` | On USB as **`/dev/cu.usbmodem2101`**. The number moves across replugs, and it is the only `usbmodem` port. Holds `f1` in `ROLE_GATELINK` and `f3` in `ROLE_FAULT` in NVS |

**The link ran −52 to −48 dBm on 2026-09-21**, about 12 dB weaker than the 2026-09-17
sessions, because the bridge board moved to its production position for the D1 capture and
has not moved back. SNR held at +10 to +12 dB. **Absolute loss rates are not comparable
across those sessions**; a comparison inside one sweep is.

On 2026-09-23 the bridge heard f1, on the XIAO, at −27 to −26 dBm and +11 dB SNR. That
day's figure is not comparable to the 2026-09-21 range until someone confirms which board
the 2026-09-21 range came from.

**A wrong board selection is silent.** It writes the wrong pin map into a normal-looking
artifact. **Tell the two Heltecs apart by enclosure**: both CP2102 bridges report
`SER=0001` and the port name is not stable across replug. **Or read the MAC from the boot
banner** — every one of these firmwares prints it.

**Flash from a committed tree.** A `-dirty` git field on the banner means the running image
matches no commit.

**Its antenna stays on the bridge board.** The range test's 3.0 dBi 19 cm stick (Bridge PRD
**R-4.3a.1**). The gain is a term in D1's EIRP arithmetic and `radio_config.h` asserts the
sum at compile time.

## Traps that cost real time here

[`traps.md`](./traps.md) has the full set. These are the ones the next job meets first:

- **Opening any of these boards' serial ports reboots it, the XIAO included**, which resets
  a simnode's identities and its `ctx_id`. **Hold both ports open for a whole run** — a
  disabled identity re-enables on the next boot.
- **A bench identity is polled only after the bridge has heard it.** `push f1` announces a
  `ROLE_GATELINK` identity; `fault <id> hdr_rsv` announces any role and moves no counter.
- **A command takes 4–9 s from the MQTT publish to the node**, not ~1 s. A configuration
  set is slower still: it waits on the poll scheduler and the media access behind it.
- **`sched_task` is the deepest task in this firmware since BF-32**, and it logs its
  high-water mark on every configuration resolution: 1976–2324 bytes free of 3072 on
  2026-09-23. Read that number before adding anything to its tick.
- **After a bridge boot, a bench identity refuses commands and `CONFIG` until the bridge
  hears it** (BF-34). `push f1` starts its roll, and `roll: f1 rolled to ctx …` on the
  bridge's serial log says it finished. A board still running pre-BF-34 firmware draws
  `DUPLICATE_CACHED` instead, which is the defect the roll closes.
- **Interleave the arms of any frame-counting sweep**, and give every one a control arm in
  the same session. `tools/simctl/sweep_interleave.py` does both.
- **HA's entity registry remembers every `unique_id`**, and a retained discovery config
  survives a reflash. Develop discovery against the dev HA VM and dev broker until **B6**.
