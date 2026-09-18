# Bridge Node — session handoff

**Written 2026-09-18, at the end of the session that analysed M25's ten-hour channel
capture.** Earlier the same day, a housekeeping pass committed the capture, split the
engineering log, moved the traps to [`traps.md`](./traps.md) and kept the 2026-09-17 session
brief under [`briefs/`](./briefs/). No firmware changed. This file replaces the previous one
wholesale.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**M25's capture is analysed, and it does not explain the bench losses.** The engineering
log's two 2026-09-18 entries have the numbers; the second corrects two figures in the first,
and `rssi_report.py` now reproduces every figure. In short:

- **917.4 MHz is not empty.** Above −110 dBm it was occupied 0.0912 % of the time over ten
  hours, by at least two sources. One is strictly periodic: bursts near −75 dBm every
  **130.69 s**. The other bursts near −93 dBm, sometimes for tens of seconds. Neither is the
  bridge, and neither is identified. Part of the weakest band, −110 to −101 dBm, is
  associated with the bridge's own transmissions, by a mechanism not yet established.
- **Nothing came within 34 dB of the bench's −37 dBm wanted signal**, and the simnode's media
  access never drops a frame on a busy channel. So the capture excludes an occupant loud
  enough to matter at one metre, in the hours it covered.
- **It did not cover the hours of the 2026-09-17 losses**, so the channel candidate is weaker
  and not closed.

**Two questions from it are the operator's, and neither blocks the bench:**

1. **Does this capture reopen D33?** Standing condition 3 requires that the survey find no
   co-channel occupant on 917.4 MHz. The capture found energy in its receive bandwidth, and
   it cannot say whether that energy is co-channel or a neighbour leaking in. Decision
   Register §3.4 and the M25 row are where the answer is recorded.
2. **Is the 130.69 s source the Davis Vantage Pro2?** Its hop cycle predicts 130.6875 s at
   transmitter ID 1, with one hop channel at 917.434 MHz and a 6.7 ms packet. Its transmitter
   sits at `weather-island`, on the line between the bridge and the gate.
   [`LRAN-Site-RF-Inventory`](../shared/LRAN-Site-RF-Inventory.md) §6 has the research. **Read
   the console's transmitter ID**: ID 1 confirms the match, and any other ID rules it out. At
   the gate, the engineering log estimates this source alone overlaps about 0.85 % of
   maximum-length SF9 uplink frames, at about 26 dB above the wanted signal.

**The site's 900 MHz equipment is now researched, from published sources.**
[`LRAN-Site-RF-Inventory`](../shared/LRAN-Site-RF-Inventory.md) gives Z-Wave (916.00 and
908.4 MHz), Insteon (915.0 MHz), YoLink (923.3 MHz) and the Davis (51 hop channels). It also
finds that M20's −54 dBm at 916.0 MHz in the house is on Z-Wave's channel, not YoLink's, which
Decision Register §5.4 will need to reflect. That part of **M26** is done; confirming model
numbers is still open.

**Run the interleaved sweep next, before BF-24.** The ten frame-log bursts of 2026-09-17
put a 250 ms gap at **2.50 %** and a 2000 ms gap at **1.90 %**. The same arms had read
5.6 % and 0 % that morning. Every loss fell in four of the ten runs whatever the spacing,
so the losses look clustered in **time**. **Every sweep on record ran one spacing to
completion before starting the next**, so a slow change in the environment is
indistinguishable from an effect of spacing. Alternating the two arms inside one session
separates them, at 45 s a burst. **Both simnodes are powered down from the M25 capture**,
so power up the one the sweep floods from and rebuild its identities first:

```bash
~/.platformio/penv/bin/python tools/simctl/rxlog.py --seconds 45 --json burst.json
~/.platformio/penv/bin/python tools/simctl/rxlog.py --read docs/bridge/data/bf27-framelog-session-2026-09-17.json
```

**The sweep goes before BF-24 because BF-24 decodes a fragmented `STATUS`**, which is the
traffic pattern that loses frames on this firmware. Decoding into it first means debugging
two problems at once. **If the knee is environmental**, the 1 s threshold and the
`backoff_max_ms` reasoning that leaned on it describe one quiet afternoon rather than a
property of this firmware.

**B4 is the milestone, and BF-23 does not wait on either measurement.** B4 is MQTT,
discovery and publication policy. Every B4 criterion but one is reachable with **no node
hardware** (**V-B11**): BF-23, BF-24, BF-25, with BF-26 behind `/lib/lran-config/`.

**BF-23's discovery half was built on 2026-09-17**, as `json_writer.h`, discovery
generation, 26 generated `/ha/` example payloads and `tools/checks/ha_examples.py`. Ask git
whether it has reached `main`:

```bash
git fetch origin -p
git log --oneline -1 origin/main -- tools/checks/ha_examples.py   # empty: not on main yet
```

**BF-23's other half is blocked on an operator decision.** `TODO(BF-23)` on
`g_diag_interval_s` (`task_runtime.cpp`) is the runtime lever that makes **V-B12's saturated
arm possible at all**, and it has no route:

- **`lran/<node>/config/set` has no payload.** Spec §16.2.1 leaves it undefined on purpose:
  *"a payload specified before its first caller is a guess carrying a version number."*
- **`/lib/lran-config/` does not exist and has no task.** See *Work no task owns*.

**This is the same gap that deferred BF-26 on 2026-09-14**, so it blocks two tasks. Every
other timing lever sits behind it too: `poll_interval_s`, `reply_timeout_ms`,
`missed_poll_threshold`, `error_min_interval_ms` and the media-access config. **Check it
before planning around any `TODO(BF-23)` timing constant.**

**Three routes exist, and they are not equivalent.** A general `config/set` with
`/lib/lran-config/` behind it commits a fleet-wide interface. A narrow single-purpose topic
for the diagnostic interval alone freezes an HA-visible token. A serial-only lever, like
BF-26's deferred fallback, leaves root rule 8 unmet. The 2026-09-17 brief gives a
recommendation among them; **the choice is the operator's.**

**The catalogue runs in one command**, and every row it drives is green:

```bash
export LRAN_MQTT_HOST=$(sed -n 's/^#define MQTT_HOST[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
export LRAN_MQTT_USER=$(sed -n 's/^#define MQTT_USER[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
export LRAN_MQTT_PASSWORD=$(sed -n 's/^#define MQTT_PASSWORD[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
~/.platformio/penv/bin/python tools/simctl/simctl.py --port /dev/cu.usbmodem2101
```

**Command substitution, not `echo`**, so the values land in the environment and nothing
prints. *Bench credentials* below says why this is safe today and will not be.

**Set the bench up like this.** One simnode board is enough; a second is needed only for
`PING` between nodes. `simctl` enrols its own identity. For a hand-run session, use
`push f1` for the GateLink role, or `fault <id> hdr_rsv` for any other. **A fault's `dst`
defaults to the bridge.** **Drive a command from the broker**: publish to
`lran/<node>/cmd/<action>/set`, and read `lran/<node>/cmd/ack`.

## What the 2026-09-17 sessions established

The engineering log's eight 2026-09-17 entries have every number below. **Read the last
entry first.** It supersedes the earlier ones on the receive path's mechanism, and on
whether spacing is the variable at all, and leaves their measurements standing.

- **B3b is accepted.** Its tasks were confirmed on air on 2026-09-16, and the milestone
  closed on 2026-09-17 once its last criterion had a home.
- **V-B12 moved to B4 rather than gaining a `BF-*` number**, with the operator. Its
  saturated arm cannot run on this firmware, and both things it needs, BF-23's lever and
  BF-26's bench diagnostics, are already B4 tasks. Impl Plan §8.1 records the move.
- **M22's idle arm measured five spacings that morning**: 5.6 % PER at 250 ms, 5.0 % at
  400, 1.0 % at 700, and 0 % at 1100 and at 2000. The endpoints reproduced on an
  instrumented build the same day.
- **That curve did not reproduce the same afternoon**, as *The next job* describes.
  **Both readings stand as taken.** What is in doubt is whether spacing is the variable,
  because no sweep on record interleaved its arms.
- **Three mechanisms inside the bridge are ruled out.** `rx_no_interrupt` read zero across
  190 frames spanning both arms, which rules out `kIrqReadMs`. `rx_deaf_ms` held at
  **639–659 ms per 60 s window across five bursts** whose PER ran 6 %, 2 %, 2 %, 0 %, 0 %,
  which rules out the transmit path in aggregate. Across eight `--gap 250` bursts, bridge
  transmissions against frames lost ran 3/5, 0/4, 7/1, 3/1, 6/3, 4/2, 3/1, 3/1, so the
  bridge's media access does not predict the losses either.
- **BF-27's frame log ruled out the transmit path per frame.** Of nine frames lost across
  ten bursts, the bridge's own deafness can account for **at most 0.52**. That figure is a
  ceiling, because `rx_deaf_ms` is a total and never says where in a gap it fell. Four of
  the gaps contain a bridge transmission and still cannot account for their losses.
- **376 receptions were logged one by one, and every one was clean.** Each was announced by
  its own interrupt, none arrived corrupt, and the ladder discarded none. RSSI ran −39 to
  −36 dBm throughout, and the RX queue never went deeper than 1. **Every loss is a frame
  the radio never delivered**, which at one metre is not an RF story. **The cause is not
  known.**
- **`cad_free` closed a blind spot in every earlier run.** The control shows `cad_backoffs`
  0 against `cad_free` 3 per burst. On the old instrument, that window read as a bridge that
  never contended for the channel, while the radio left receive three times for 643 ms.
- **Two of the three radio families on the property cannot move `cad_backoffs` at any
  signal level.** A LoRa CAD detects a LoRa preamble at the configured SF, so Z-Wave and
  Insteon FSK are invisible to it. Decision Register §3.4 names CAD as the instrument
  keeping the channel under observation for **D33 standing condition 3**. **M25** builds one
  that can see them, and **M26** completes the §3.1 inventory it would be read against.
  **D33's status is unchanged**, and neither item reopens it.
- **The instruments are built and tested on the host.** `tools/simctl/per_measure.py` gives
  a PER figure and `rxlog.py` says *which* frames were lost, each with its arithmetic
  separated from its I/O. `rx_wake.h`, `rx_deaf.h`, `frame_log.h` and `chan_monitor.h` are
  the bridge-side half.

**Do not re-derive the closed candidates.** The engineering log's 2026-09-17 entries have
all of it, with the numbers.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the 2026-09-17 entries, **last one first**. Entries from 2026-09-10 to 2026-09-16 are in [`engineering-log-2026-09-10_2026-09-16.md`](./engineering-log-2026-09-10_2026-09-16.md); read them only when a document cites one |
| 3 | [`traps.md`](./traps.md) | the section for the work you are about to do. Bench work needs *Measuring frame loss* and *Bench boards and serial ports* at least |
| 4 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§8** owns the milestones; **§8.1** is V-B12's move; **§6.6.1** BF-27's frame log; **§10.5** the fault catalogue; **§7.2.1** BF-21's `simctl`; **§6.2.1** BF-18; **§6.1.1–§6.1.2** BF-17 and BF-20; **§4.3.2** BF-19; **§10** the simnode |
| 5 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | §7 is B4, where the work goes next; §6's B3b tasks are all built |
| 6 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and what breaks silently |
| 7 | [`firmware/simnode/CLAUDE.md`](../../firmware/simnode/CLAUDE.md) | what the simnode has, what it does not, and its traps |
| 8 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) §9–§12, §14, §16 | keys, context, reassembly, radio, the discard ladder, MQTT. **§18.2, never §18.1 alone** |
| 9 | [`LRAN-Site-RF-Inventory`](../shared/LRAN-Site-RF-Inventory.md) | the property's Z-Wave, Insteon, YoLink and Davis radios, and what M20 saw at each frequency |
| 10 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) | **§3.1** the site's 900 MHz equipment; **§3.4** D33's standing conditions and the note against its own instrument; **§5.4** M20's channel evidence; **M25**, **M26** |
| 11 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere |

[`briefs/2026-09-17-session-brief.md`](./briefs/2026-09-17-session-brief.md) is optional. It
is a dated reading of the documents above and adds recommendations, not facts.

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**, **B0**, **B3a**, **B3b**. **M6**, **M19**, **M20**, **M21**. **D1**, **D33**; **D34 amended**. **V-B3**, **V-B9**, **V-B10**, **W9**. **BF-2**–**BF-9**, **BF-15**–**BF-22**. BF-27's frame log |
| Not done | **M25** — measured and analysed on 2026-09-18; whether it reopens D33 is the operator's call. **M26** — researched from published sources on 2026-09-18; device model numbers not yet confirmed. **B4**: BF-23's lever half, BF-24, BF-25, BF-26 deferred. **BF-27's other three tools**. **V-B12**, a B4 criterion with its idle arm measured. **M22** open. **BF-11a**, **BF-11b** |
| Queue | The interleaved sweep, then BF-24. BF-23's discovery half does not wait on it. The D33 question waits on the operator. Nothing waits on a document |

```bash
pio test -d lib/lran-protocol -e native         # library host suite
pio test -d lib/lran-link -e native             # spec 12.3 media access, both firmwares
pio test -d lib/lran-sim -e native              # BF-7's FramePatch, against the W4 negatives
pio test -d firmware/bridge -e native           # bridge host suites
pio test -d firmware/simnode -e native          # simnode host suites
pio run  -d firmware/bridge -e heltec           # bridge target - NEEDS secrets.h
pio run  -d firmware/simnode -e simnode-heltec  # simnode target - NEEDS secrets.h (key only)
pio run  -d firmware/simnode -e simnode-xiao-wio
python3 tools/checks/lora_task_never_blocks.py  # lora_task blocks on nothing
python3 tools/checks/no_mbedtls_hkdf.py         # HKDF built from HMAC, spec 9.1
python3 tools/checks/bridge_partitions.py       # A/B table; add --firmware/--elf after a build
python3 tools/checks/spec_citation_version.py   # binding citations vs. the spec header
python3 tools/simctl/test_simctl.py             # simctl's verdict logic, no board
python3 tools/simctl/test_per_measure.py        # M22 PER arithmetic and its guards, no board
python3 tools/simctl/test_rxlog_analyze.py      # BF-27 frame-log arithmetic and its guards
python3 tools/simctl/test_rssi_analyze.py       # M25 channel-capture arithmetic and its guards
python3 tools/checks/simctl_catalogue.py        # simctl's rows vs. fault.cpp
python3 tools/vectors/check.py                  # W4 vectors, self-check
```

**CI runs all of these on every pull request**, and builds both V-B9 bad images. Run them
locally when you are about to spend bench time on the result. **Call `pio` and the bench
tools' Python by path from a script**; [`traps.md`](./traps.md#bench-boards-and-serial-ports)
says why.

## Git state — ask git, do not read it here

> **Where `main` points, what merged last, which branches exist and whether a PR is open
> are deliberately not written in this file.** A written SHA is wrong the moment the branch
> carrying it merges, and it is wrong in the worst direction — confidently, in a file whose
> whole value is being trustable cold.

```bash
git fetch origin -p                             # prune deleted remote branches first
git log --oneline -1 origin/main                # where main actually is
gh pr list --state open                         # what is open, if anything
git log --branches --not --remotes --oneline    # local-only work; empty is good
git branch -vv | grep ': gone]'                 # local branches whose remote was deleted
```

**Run `git fetch` before trusting any of it.** Two machines push to this repository.

**Permanent history is citable; moving state is not.** `4250e00` (six document defects),
`ebdcf0d` (the `LoRaBridge` retirement and the D33 bench-power reconciliation), `8253085`
(**P8**, with D34's amendment and spec v0.11), `28ffd82` (the poll-to-answer instrument),
`65508bf` (the 2026-09-16 catalogue and W9 record) and `76e6d11` (M25's capture).

**Merging a stack: never pass `--delete-branch`.** Deleting a base branch **closes** the PR
stacked on it rather than retargeting it. Merge each PR without it, retarget the next to
`main` while it is still open, then delete branches by hand. **This worked as written on
2026-09-16** for #59 → #60 → #61.

**A push touching `.github/workflows/` needs workflow token scope.** It was refused once, on
2026-09-08, and accepted since. Try the push; if it is refused, the operator refreshes auth.

## Bench credentials — the broker is a sandbox, and that changes what is safe

**The broker at the address in `secrets.h` is a disposable sandbox Home Assistant install
with its own Mosquitto.** Its credentials are **not** the production ones, so they may be
put into the environment directly rather than prompted for. **That changes at the production
cutover**, after which a password must not reach argv, a log or a committed file.

**`simctl` and `per_measure` read `LRAN_MQTT_HOST`, `LRAN_MQTT_USER` and
`LRAN_MQTT_PASSWORD` from the environment and never take them as arguments.** That is right
either way: an argument reaches argv, and argv reaches the process table and the shell
history. Source them out of `secrets.h` without echoing them; `secrets.h` is gitignored and
stays where it is.

**The OTA password is read from `LRAN_OTA_PASSWORD`** in the shell that runs the upload.

## Hardware state

**This table names the devices in this subproject's terms.** The range-test handoff owns
them in its own roles, and its rows do not transfer here.

| Device | Called here | Told apart by | Firmware / env | Stored state | Current state |
|---|---|---|---|---|---|
| Heltec WiFi LoRa 32 V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `bridge` / `heltec`, **USB-flashed 2026-09-17 from `ecc2e6f`, a clean tree**. It carries `rx_wake.h`'s two receive counters, `rx_deaf.h`'s two, `frame_log.h`'s per-frame record on `lran/bridge/diag/rxlog/state` (BF-27) and `chan_monitor.h`'s RSSI sampler on serial (M25). **The capture file's `CHAN-BOOT` line names the running image** (`CHAN-BOOT,ecc2e6f,917400000,...`); check it rather than trusting this row. **The frame log did not change what it measures**: the 2000 ms control read 0/40 twice with it running, as it did without | NVS: nothing this node depends on yet | On USB to the macOS build machine, last seen as `/dev/cu.usbserial-0001`. It polls, receives and publishes, with WiFi, broker and radio all up. **The M25 capture that held this port closed at 2026-09-18T13:43:22Z.** A second opener fails or steals bytes, so check for a running `rssi_capture.py` before touching the port |
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `simnode` / `simnode-heltec`, last flashed 2026-09-16 with `Node::on_error`. **BEHIND: it has neither `ctx_reject` nor the completed `set_displaced` (BF-21).** Reflash before using it for the catalogue. MAC `44:1b:f6:fa:bc:2c` | Nothing persists; identities reset on every boot | **Powered down on 2026-09-17 for M25's capture**; whether it has been powered up since is not recorded. **Its port name moves across replug**: it has been `/dev/cu.usbserial-4` and `/dev/cu.usbserial-3`. Boots with `f0` `ROLE_RANGE` and `f2` `ROLE_HEALTH` |
| XIAO ESP32S3 + **Wio-SX1262 Kit** (p-5982, B2B) | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `simnode` / `simnode-xiao-wio`, **reflashed 2026-09-16 for BF-21**, so it HAS `ctx_reject` and the completed `set_displaced`. MAC `68:ee:8f:4b:85:f4`. Native USB, so it enumerates as `/dev/cu.usbmodem*` | B1b position log, dumped and committed | **Powered down on 2026-09-17 for M25's capture**; whether it has been powered up since is not recorded. Last seen as `/dev/cu.usbmodem2101`. Boots with `f1` `ROLE_GATELINK` alone. **The M22 setup (`id add f3 ROLE_FAULT`, `disable f1`) is gone after any reboot**, so rebuild it and **check `id list` before believing a run** |

**A USB flash puts the bridge board back in a known state.** Flash from a committed tree: a
`-dirty` git field on the banner means the running image matches no commit.

**A wrong board selection is silent.** It writes the wrong pin map into a normal-looking
artifact. **Tell the two Heltecs apart by enclosure**: both CP2102 bridges report
`SER=0001`, and the port name is not stable across replug. The XIAO is unambiguous, because
it is the only `usbmodem` port.

**To tell the two Heltec ports apart without opening an enclosure, read the boot banner.**
Opening the port reboots the board, which prints it; the bridge's first line is `LRAN Bridge
Node - node 0x00`. Open with `dtr` and `rts` low before `open()`, as `simctl`'s `Console`
does, or the open presses PRG. **Confirmed on 2026-09-17**, when the bridge answered on
`/dev/cu.usbserial-0001` and the simnode Heltec on `/dev/cu.usbserial-3`.

**No stored state on any of these boards is the only copy.** Every survey site and every B1b
position is committed under `docs/rangetest/data/`, and every M22 run and the M25 capture
under `docs/bridge/data/`.

**Its antenna stays on the bridge board.** The bridge uses the range test's 3.0 dBi 19 cm
stick (Bridge PRD **R-4.3a.1**). The gain is a term in D1's EIRP arithmetic, and
`radio_config.h` asserts the sum at compile time.

## Behaviour that changed, and will make older artifacts read differently

**An older artifact is correct for when it was made.** Changes before 2026-09-16 are in
[`traps.md`](./traps.md#behaviour-that-changed-before-2026-09-16).

- **V-B12 is a B4 criterion since 2026-09-17**, not a B3b one. Text saying B3b is blocked on
  it, or that no task owns it, is correct for before that.
- **`lran/bridge/diag/radio/state` carries `rx_no_interrupt` and `rx_wake_empty` since
  2026-09-17** (`rx_wake.h`), and **`cad_free` and `rx_deaf_ms` since later the same day**
  (`rx_deaf.h`). Text saying no counter sits between `RX_DONE` and the ladder is correct for
  before the first pair. Text treating `cad_backoffs` as the bridge's media-access
  instrument is correct for before the second. **All four are bridge-local and deliberately
  not spec §14.1 counters**, so they are absent from `lran/bridge/diag/state` and from
  schema `0xF0`.
- **`enter_mode()` is the one place `lora_link.cpp` changes `g_mode` since 2026-09-17.** A
  transition that assigns it directly escapes the deaf-time accounting.
- **`lran/bridge/diag/rxlog/state` exists since 2026-09-17** (BF-27), carrying one record
  per frame in and out. **It is the one bridge topic that is not retained on a `/state`
  leaf**, against spec §16.2's table, and the deviation is raised rather than settled. Impl
  Plan §6.6.1 says why. Text saying the bridge publishes only aggregate counters is correct
  for before it.
- **The bridge writes `CHAN` and `CHANSUM` lines to serial since 2026-09-17** (M25,
  `chan_monitor.h`). A tool reading that port for anything else has to tolerate them.
  **They are not on MQTT and are not meant to be**: a bucket a second for ten hours is a
  capture file, not a diagnostic topic.
- **`log_task` drains the frame-log ring and is no longer idle since 2026-09-17.** It was a
  `vTaskDelay` loop with a `TODO(BF-11a)` in it from BF-11 until then. **The leveled log
  still has no queue**; only the frame log uses the task.
- **The bridge accepts protocol version N *and* N−1 since BF-22**, and addresses each node
  in the version that node announced. Text saying it accepts only N, or that `bad_ver`
  moves `rx_bad_ver` twice, is correct for before it. `lran/<node>/diag/state` gained
  `unsupported_ver`.
- **The bridge subscribes to `lran/+/cmd/+/set` and publishes `lran/<node>/cmd/ack` and
  `lran/bridge/diag/cmd/state` since BF-18.** Text saying the bridge only publishes, or
  that `subscribe()` has no caller, is correct for before it.
- **`set_displaced` moves one counter since BF-21**, not two. A capture from before it also
  shows `rx_reassembly_timeout`.

## Traps that cost real time here

[`traps.md`](./traps.md) has the full set, grouped by the work they bite. These are the ones
the next job meets first:

- **Do not treat "space frames above 1 s" as a safe rule**, and give every frame-counting
  measurement a control arm in the same session.
  [Measuring frame loss](./traps.md#measuring-frame-loss)
- **A capture with a simnode powered up is not a channel measurement**, and a `CHAN` line is
  not the denominator. [Measuring the channel](./traps.md#measuring-the-channel-m25)
- **Opening either board's serial port reboots it**, which resets its identities and its
  `ctx_id`. [Bench boards](./traps.md#bench-boards-and-serial-ports)
- **A reboot of the bridge board zeroes every counter.**
  [Measuring frame loss](./traps.md#measuring-frame-loss)
- **A command takes 4-9 s from the MQTT publish to the node**, not ~1 s.
  [Commands](./traps.md#commands-the-broker-and-mqtt)
- **HA's entity registry remembers every `unique_id`**, and a retained discovery config
  survives a reflash. Develop discovery against the dev HA VM and dev broker until **B6**.
  [Commands](./traps.md#commands-the-broker-and-mqtt)

## Open, and not closable from here

- **The bridge loses frames at one metre, and the cause is not known.** The open question
  is whether spacing is the variable at all. M25's capture found nothing loud enough to
  matter at one metre in the ten hours it covered, which were not the hours of the losses.
  The interleaved sweep in *The next job* is the measurement that can answer it. **The margin inverts between
  here and the gate.** At one metre the wanted signal is −37 dBm and the loudest neighbour
  −54 dBm. At the gate the wanted signal is about −100 dBm, the neighbours −80 to
  −89 dBm, and M25's periodic source about −75 dBm. **A loss rate measured at one metre is not evidence about 87 m**, and it is
  optimistic in the wrong direction. **BF-24's decode work meets the same traffic pattern
  first.**
- **Nothing a node sends the bridge carries a MAC the bridge verifies.** §9.2 makes every
  authenticated type bridge → node, so the bridge's own `rx_rejected_seq` and
  `rx_dup_command` stay at zero by construction. BF-18 proved the *sending* half.
- **The `radio_ok` half of the OTA verdict is untested on hardware.** V-B9's images fail by
  network or by panic; no image with a dead radio exists.
- **`poll_reply_timeout_ms` = 10 000 has bench evidence** (522–1686 ms at 1 m, no contention)
  but no measurement under contention or at range.

#### Spec v0.12 — landed 2026-09-16, and what it left open

**All nine questions are answered.** Eight became **D35–D42** in the Decision Register;
the ninth was a fact, verified against the datasheet as **M24**. `LRAN-Spec-v0.12-Brief`
holds the reasoning and is superseded. **`ver` stays `2` and no vector regenerates.**

- **W14** — §17.1's duty-cycling design assumed a silicon address filter that LoRa does
  not have, so its power model is unquantified. Owed before WellLink is built on that
  profile, moot if **D19** makes WellLink mains-powered.
- **W10** is now a counting question: `CONFIG` and `CONFIG_ACK` are single-frame, so a
  configuration larger than one frame is several messages with no atomicity across them.
  Count GateLink's real parameters against 24 entries before `/lib/lran-config/` is
  designed.
- **§14.2's bound 1 is what v0.12 left untested on air** — no unregistered source has been
  produced on the bench.

#### Work no task owns

- **`/lib/lran-config/`** — System PRD §9.4 describes it; BF-26 and BF-23 need it. **The
  MQTT receive path it was paired with is built**: BF-18 did it, so a `config/set`
  subscriber has a transport seam and an inbound queue to reuse.
- **The receive path's losses** — unassigned, and **three hypotheses shorter**. All three
  instrument steps are **built**, all bridge-local rather than spec §14.1, and each came back
  clean. **A fourth candidate is not in this firmware at all**: the channel. **M25**
  measured it on 2026-09-18 and made it weaker without closing it. **M26** completes the
  equipment inventory; neither is a `BF-*` task.
  **A poll-free burst is not available** and does not need to be. The flooding node enrols
  itself on its first frame (`scheduler.cpp`), so it is polled regardless of what else is
  quiet, and the poll load is measured rather than avoided.
- **Spec §16.2's retention rule against a streaming diagnostic.** The frame log publishes
  unretained where §16.2's table says a `/state` leaf is retained, and Impl Plan §6.6.1
  raises the deviation. No spec amendment has been drafted.
- **The Implementation Plan cites PRD v0.6; the PRD is at v0.12.** This was found on
  2026-09-17 and deliberately not bumped. **Reconcile §§2.1, 2.2, 3.2, 7.1 and 8 against the
  PRD's v0.7–v0.12 changelog first, then correct the citation**; a bare number bump is the
  failure the citation rule exists to expose. **Nothing caught it** because
  `tools/checks/spec_citation_version.py` covers protocol-spec citations only, and no check
  reads a PRD-to-plan or plan-to-tasks citation. Extending it will likely surface other
  stale ones; report those rather than fixing everything on one branch.
- **No link has ever been measured at 917.4 MHz.** Every walk, every bench trace and B1b's
  gate run were captured at **915.0 MHz**, the range test's provisional frequency, which
  Decision Register §3.4 already flags as `weather-island`'s own peak. The operating channel
  has a survey behind it and **no link measurement at all**. Found on 2026-09-17 by reading
  every `freq_hz` column under `docs/rangetest/data/`.
- **R-3.2c is unmet and no task names it.** The PRD says *"WiFi RSSI SHALL be published as
  a diagnostic."* `wifi_rssi_dbm()` exists (`wifi_link.cpp`) and is read into the OLED
  status page and **nowhere else**: `task_runtime.cpp` puts it on the screen, and no MQTT
  topic carries it. Verified on 2026-09-17 by reading every caller. **It belongs in BF-24's
  table**, which is the task that owns what the bridge publishes. It is recorded here
  because BF-24's row does not mention it and nothing else would surface it.
- **A PING responder on the bridge** — spec §17.3 requires RF loopback of every node build.
- **Decoding per schema has no task** — Impl Plan §5.3's `decode/`; `app_task`'s `TODO`
  gives it to BF-24. Nothing a simnode sends is decoded or published today.
- **Six task stacks unmeasured** — every size but `lora` is still BF-11's figure.
- **The first-attempt `AUTH_FAIL` at every boot** — reproducible, unexplained, unassigned.
- **A serial log line per network state change** — proposed, not assigned to a task.
- **The OTA first-data timeout** — one failure in two uploads on 2026-09-13; it did not
  recur on 2026-09-15's three uploads.
- **R-5.3d on hardware** — an OTA upload deferred during a LoRa transaction.
- **BF-11a** (log queue drain) and **BF-11b** (hardware watchdog from `sched_task`).
- **What GateLink's `COMMAND_ACK` waits for** — pulse complete, or gate confirmed. GateLink
  **M3** needs the answer.
- **GateLink M0's LDO margin** — sized for Envelope B's 19.6 dBm, tested only at −4 dBm.
- **The range-test firmware still transmits on the provisional 915.0 MHz.**

### Closed, and not to be reopened by habit

- **B3b — accepted 2026-09-17.** Its tasks were on air the day before; the milestone closed
  once V-B12 had a home. Do not reopen whether V-B12 should have had a `BF-*` number.
- **`kIrqReadMs`, the bridge's media access and its transmit path — ruled out 2026-09-17**
  as the cause of the receive losses, the last per frame. Engineering log, 2026-09-17.
- **BF-22 — built and on air 2026-09-16.** V-B10 met. Do not widen the accepted
  version range past N−1 for convenience; §13.2 is why.
- **BF-21 — built and run 2026-09-16.** Both its decisions are made; do not reopen
  `set_displaced`'s second counter or ask again whether `ctx_reject` should exist.
- **BF-18 — built and on air 2026-09-16**, `ResyncFailed` included, via `ctx_reject`.
- **B3a — accepted 2026-09-16.** The catalogue, W9 and the clause-by-clause record are in the
  engineering log and in #61.
- **W9 — closed 2026-09-16** on the bench, board to board.
- **B0 — accepted 2026-09-15.** Bench record in the engineering log and in #60.
- **V-B9 — re-run and passed 2026-09-15**, with the `radio_ok` gap noted above.
- **V-B3 — passed 2026-09-15**, both directions.
- **B2 — accepted and merged, 2026-09-13.**
- **D1 and D33 — CLOSED 2026-09-10.** 917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted,
  Envelope A. Reopening needs a new fact, not a re-reading of B1b.
- **M19 — DONE 2026-09-10.** Do not regenerate §15.1's table expecting different numbers.
- **M6 — CLOSED 2026-09-09.** Gate **~87 m**, well **~100 m**. No well walk is owed.
- **M20 — CLOSED 2026-09-05.** The tool ranks channels from the committed trace.
- **M21 — CLOSED 2026-09-06.** §15.23 home-built.
- **W4, W5, W12 — closed.** §18.1 is annotated, not rewritten, and must not be read alone.
- **D31 — closed 2026-09-08.** Copyright holder is Robert J. Lee.
- **D32 — closed.** RadioLib, pinned. Run `tools/rangetest/check_pa_table.py` after any
  version bump.
