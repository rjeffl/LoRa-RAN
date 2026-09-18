# Bridge Node — session handoff

**Written 2026-09-17, at the end of the session that timed the radio out of receive and
ruled the transmit path out as the cause of the bridge's frame loss.** It revises the
earlier 2026-09-17 file — written the same day, at the end of the session that ruled
`kIrqReadMs` out — rather than replacing it wholesale, because only the receive-knee
material changed.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**B4, starting with BF-23.** B3b is accepted and nothing is owed behind it. B4 is MQTT,
discovery and publication policy, and the whole milestone except one criterion is reachable
with **no node hardware** (**V-B11**): BF-23, BF-24, BF-25, with BF-26 behind
`/lib/lran-config/`.

**BF-23 has two halves and only one of them is reachable. An earlier draft of this file
said it "unblocks two things at once"; that is wrong.** Discovery generation is reachable
and specified. The other half — `TODO(BF-23)` on `g_diag_interval_s` (`task_runtime.cpp`),
the runtime lever that makes **V-B12's saturated arm possible at all** — is not, and the
reason predates the task:

- **`lran/<node>/config/set` has no payload.** Spec §16.2.1 leaves it undefined on purpose:
  *"a payload specified before its first caller is a guess carrying a version number."*
- **`/lib/lran-config/` does not exist and has no task.** See *Work no task owns*.

**This is the same gap that deferred BF-26 on 2026-09-14**, so it now blocks two tasks
rather than one. **Check it before planning around any `TODO(BF-23)` timing constant** —
the same caution this file already gives for `poll_interval_s`. Every other lever
(`reply_timeout_ms`, `missed_poll_threshold`, `error_min_interval_ms`, the media-access
config) sits behind it too.

**Choosing the route is an operator decision, not a default.** Three exist and they are not
equivalent: a general `config/set` with `/lib/lran-config/` behind it, a narrow
single-purpose topic for the diagnostic interval alone, or a serial-only lever like BF-26's
deferred fallback. The first commits a fleet-wide interface; the second freezes an
HA-visible token; the third leaves root rule 8 unmet.

**V-B12 is a B4 criterion and its idle arm is already measured.** Impl Plan §8.1 is the
record of the move; the engineering log's 2026-09-17 entries are the measurement.
**Its saturated arm stays blocked** until the lever above has a route.

**One thing must land before BF-24, and it is no longer the instrument — it is a
sweep.** BF-24 decodes a fragmented `STATUS`, which is the traffic pattern that loses
frames on this firmware, so decoding into it first means debugging two problems at once.

**BF-27's raw frame log is built and it answered its question.** Of nine frames lost across
ten bursts, the bridge's own deafness can account for **at most 0.52** — a ceiling, because
`rx_deaf_ms` is a total and never says where in a gap it fell. Four of those gaps contain a
bridge transmission and still cannot account for their losses. **All three cheap candidates
are now closed**, and the third is closed per frame rather than in aggregate.

**Run the interleaved sweep next, and run it before BF-24.** The same ten bursts put a
250 ms gap at **2.50 %** and a 2000 ms gap at **1.90 %** — the arms that read 5.6 % and 0 %
that morning. Every loss fell in four of the ten runs whatever the spacing, so the losses
look clustered in **time**. **Every sweep on record, including that one, ran one spacing to
completion before starting the next**, which leaves a slow change in the environment
indistinguishable from an effect of spacing. Alternating the two arms inside one session
separates them and costs 45 s a burst.

**What turns on it.** If the knee is environmental, the 1 s threshold and the
`backoff_max_ms` reasoning that leaned on it describe one quiet afternoon rather than a
property of this firmware. That is worth an hour before BF-24, not after.

```bash
~/.platformio/penv/bin/python tools/simctl/rxlog.py --seconds 45 --json burst.json
~/.platformio/penv/bin/python tools/simctl/rxlog.py --read docs/bridge/data/bf27-framelog-session-2026-09-17.json
```

**Do not re-derive the three candidates that are closed.** `rx_no_interrupt` read zero
across five more bursts and then zero again across 376 individually logged receptions;
`rx_deaf_ms` held at 639–659 ms while the PER moved from 6 % to 0 %; and no frame in any
logged session arrived corrupt or was discarded by the ladder. The engineering log's
2026-09-17 entries have all of it, with the numbers.

**None of this blocks BF-23, and BF-23 does not block it.**

```bash
git fetch origin -p
git switch main && git pull --ff-only
gh pr list --state open
```

**The catalogue runs in one command**, and every row it drives is green:

```bash
export LRAN_MQTT_HOST=$(sed -n 's/^#define MQTT_HOST[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
export LRAN_MQTT_USER=$(sed -n 's/^#define MQTT_USER[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
export LRAN_MQTT_PASSWORD=$(sed -n 's/^#define MQTT_PASSWORD[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
~/.platformio/penv/bin/python tools/simctl/simctl.py --port /dev/cu.usbmodem2101
```

**Command substitution, not `echo`** — the values land in the environment and nothing
prints. See *Bench credentials* below for why this is safe today and will not be.

**Set the bench up like this.** One simnode board is enough; a second is needed only for
`PING` between nodes. `simctl` enrols its own identity. For a hand-run session: `push f1`
for the GateLink role, or `fault <id> hdr_rsv` for any other. **A fault's `dst` defaults to
the bridge.** **A command is driven from the broker**: publish to
`lran/<node>/cmd/<action>/set`, read `lran/<node>/cmd/ack`.

## What the last two sessions established

**Every item below is in the engineering log's 2026-09-17 entries, with the numbers.**
Seven entries carry that date; **read the last one first** — it supersedes the earlier ones
on the receive path's mechanism, and on whether spacing is the variable at all, while
leaving their measurements standing.

- **B3b is accepted.** Its tasks were confirmed on air 2026-09-16; the milestone closed on
  2026-09-17 once its last criterion had a home.
- **V-B12 moved to B4 rather than gaining a `BF-*` number**, with the operator. Its
  saturated arm cannot run on this firmware and both things it needs — BF-23's lever and
  BF-26's bench diagnostics — are already B4 tasks.
- **M22's idle arm measured 5.6 % PER at a 250 ms gap falling to 0 % at 1100 ms**, over five
  spacings, and **the endpoints reproduced on an instrumented build the same day**. The
  curve only appeared because the gap was swept rather than left at the first spacing that
  gave zero.
- **That curve did not reproduce the same afternoon.** Ten bursts on the frame-log build
  put a 250 ms gap at 2.50 % and a 2000 ms gap at 1.90 %, with every loss falling in four of
  the ten runs whatever the spacing. **Both readings stand as taken**; what is now in doubt
  is whether spacing is the variable, because no sweep on record interleaved its arms.
- **Zero of those frames arrived corrupt**, in any run. Every loss is a frame the radio
  never delivered, which at one metre is not an RF story.
- **`kIrqReadMs` was the suspect and is ruled out.** `rx_no_interrupt` read zero across 190
  frames spanning both arms, so the interrupt path delivered every `RX_DONE` the radio
  raised. **The cause is not known** — see *Open, and not closable from here*.
- **The bridge's own media access does not explain it either.** Across eight `--gap 250`
  bursts, transmissions against frames lost run 3/5, 0/4, 7/1, 3/1, 6/3, 4/2, 3/1, 3/1.
- **The transmit path is ruled out too.** `rx_deaf_ms` — milliseconds outside receive, CAD
  and transmission together — held at **639–659 ms per 60 s window across five bursts**
  while the PER ran 6 %, 2 %, 2 %, 0 %, 0 %. The same deafness in the burst that lost three
  frames and the burst that lost none.
- **BF-27's frame log closed the transmit path per frame, not just in aggregate.** Of nine
  frames lost across those ten bursts, the bridge's own deafness can account for at most
  **0.52** — the sum of each gap's deaf share, which is that gap's ceiling. Four of the
  gaps contain a bridge transmission and still cannot account for their losses.
- **376 receptions were logged one by one and every one of them was clean.** Zero found by
  the timed read, so no interrupt was missed at any spacing; zero corrupt; zero discarded
  by the ladder; RSSI −39 to −36 dBm throughout.
- **`cad_free` closed a blind spot in every earlier run.** The control shows `cad_backoffs`
  0 against `cad_free` 3 per burst: on the old instrument that window read as a bridge that
  never contended for the channel, while the radio left receive three times for 643 ms.
- **`tools/simctl/per_measure.py` is the instrument for a PER figure**, with 30 host tests
  in CI and its arithmetic separated from its I/O; **`rxlog.py` is the instrument for
  *which* frames**, with 36 more on the same split. **`rx_wake.h`, `rx_deaf.h` and
  `frame_log.h` are the bridge-side half**, with 33 between them.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the 2026-09-17 entries first, **last one first**: M22's idle arm, the three mechanisms it rules out, and the sweep that did not reproduce |
| 3 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§8** owns the milestones; **§8.1** is V-B12's move; **§10.5** is the fault catalogue; **§7.2.1** BF-21's `simctl`; **§6.2.1** BF-18; **§6.1.1–§6.1.2** BF-17 and BF-20; **§4.3.2** BF-19; **§10** the simnode |
| 4 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | §7 is B4, where the work goes next; §6's B3b tasks are all built |
| 5 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and what breaks silently |
| 6 | [`firmware/simnode/CLAUDE.md`](../../firmware/simnode/CLAUDE.md) | what the simnode has, what it does not, and its traps |
| 7 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) §9–§12, §14 | keys, context, reassembly, radio, the discard ladder. **§18.2, never §18.1 alone** |
| 8 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**, **B0**, **B3a**, **B3b**. **M6**, **M19**, **M20**, **M21**. **D1**, **D33**; **D34 amended**. **V-B3**, **V-B9**, **V-B10**, **W9**. **BF-2**–**BF-9**, **BF-15**–**BF-22** |
| Not done | **B4**: BF-23–BF-25, BF-26 deferred. **BF-27's other three tools** — the log is built, the rest of §6.6 is not. **V-B12**, now a B4 criterion with its idle arm measured. **M22** open. **BF-11a**, **BF-11b** |
| Queue | BF-23 first, for the reason in *The next job*. **The interleaved spacing sweep is what remains of the knee**, it does not block BF-23, and it must land before BF-24. Nothing waits on a document |

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
python3 tools/simctl/test_per_measure.py        # M22 PER arithmetic and its guards, no board (30)
python3 tools/simctl/test_rxlog_analyze.py      # BF-27 frame-log arithmetic and its guards (36)
python3 tools/checks/simctl_catalogue.py        # simctl's rows vs. fault.cpp
python3 tools/vectors/check.py                  # W4 vectors, self-check
```

**All of the above passed on 2026-09-17**: **471** Unity cases across the five native suites
— 127 protocol, 7 link, 16 sim, 109 simnode, 212 bridge — and every check above.

**`pio` is a shell alias on the macOS build machine.** A script that does not source the
user's profile must call `~/.platformio/penv/bin/pio` by path, or every step fails as
`command not found` while looking like a build failure. **The same applies to `python3`** —
`pyserial` and `paho-mqtt` live in PlatformIO's environment, so a bench tool runs under
`~/.platformio/penv/bin/python`, not the system interpreter.

**CI runs all of these** on every pull request, and builds both V-B9 bad images.

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
(**P8**, with D34's amendment and spec v0.11), `28ffd82` (the poll-to-answer instrument) and
`65508bf` (the 2026-09-16 catalogue and W9 record).

**Merging a stack: never pass `--delete-branch`.** Deleting a base branch **closes** the PR
stacked on it rather than retargeting it. Merge each PR without it, retarget the next to
`main` while it is still open, then delete branches by hand. **This worked as written on
2026-09-16** for #59 → #60 → #61.

**A push touching `.github/workflows/` needs workflow token scope.** Refused once, on
2026-09-08; accepted since. Try the push; if it is refused, the operator refreshes auth.

## Bench credentials — the broker is a sandbox, and that changes what is safe

**The broker at the address in `secrets.h` is a disposable sandbox Home Assistant install
with its own Mosquitto.** Its credentials are **not** the production ones, so they may be
put into the environment directly rather than prompted for. **That changes at the production
cutover**, after which a password must not reach argv, a log or a committed file.

**`simctl` and `per_measure` read `LRAN_MQTT_HOST`, `LRAN_MQTT_USER` and
`LRAN_MQTT_PASSWORD` from the environment and never take them as arguments**, which is
right either way — an argument reaches argv, and argv reaches the process table and the
shell history. Source them out of `secrets.h` without echoing them; `secrets.h` is
gitignored and stays where it is.

**The OTA password is read from `LRAN_OTA_PASSWORD`** in the shell that runs the upload.

## Hardware state

**This table names the devices in this subproject's terms**; the range-test handoff owns
them in its own roles, and its rows do not transfer here.

| Device | Called here | Told apart by | Firmware / env | Stored state | Current state |
|---|---|---|---|---|---|
| Heltec WiFi LoRa 32 V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `bridge` / `heltec`, **USB-flashed 2026-09-17 from `1375c3f`, a clean tree** — `rx_wake.h`'s two receive counters, `rx_deaf.h`'s two, and `frame_log.h`'s per-frame record on `lran/bridge/diag/rxlog/state` (BF-27). Confirmed on air the same day: `lran/bridge/version` reads `0.1.0`, `git 1375c3f`, `slot app0`. It replaced the `d2212c9` image flashed earlier that day. **The frame log did not change what it measures**: the 2000 ms control read 0/40 twice with it running, as it did without | NVS: nothing this node depends on yet | On USB to the macOS build machine, last seen as `/dev/cu.usbserial-0001`. **Polls, receives and publishes**: WiFi, broker and radio all up |
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `simnode` / `simnode-heltec`, last flashed 2026-09-16 with `Node::on_error` — **BEHIND: it has neither `ctx_reject` nor the completed `set_displaced` (BF-21)**. Reflash before using it for the catalogue. MAC `44:1b:f6:fa:bc:2c` | Nothing persists; identities reset on every boot | On USB. **The port name moves across replug** — it was `/dev/cu.usbserial-4` and was `/dev/cu.usbserial-3` on 2026-09-17. Boots with `f0` `ROLE_RANGE` and `f2` `ROLE_HEALTH`. **Both were disabled by hand on 2026-09-17 and that is gone after any reboot** |
| XIAO ESP32S3 + **Wio-SX1262 Kit** (p-5982, B2B) | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `simnode` / `simnode-xiao-wio`, **reflashed 2026-09-16 for BF-21**, so it HAS `ctx_reject` and the completed `set_displaced`. MAC `68:ee:8f:4b:85:f4`. Native USB, so it enumerates as `/dev/cu.usbmodem*` | B1b position log, dumped and committed | On USB as `/dev/cu.usbmodem2101`. Boots with `f1` `ROLE_GATELINK` alone. **`f3` `ROLE_FAULT` was added by hand for M22 and is gone after any reboot** — **and it was gone on 2026-09-17 afternoon**, so this board had rebooted since the morning runs. It was rebuilt the same way (`id add f3 ROLE_FAULT`, `disable f1`), with a fresh `ctx`. **Check `id list` before believing a run**, rather than assuming the bench survived |

**A USB flash puts the bridge board back in a known state.** Flash from a committed tree: a
`-dirty` git field on the banner means the running image matches no commit.

**A wrong board selection is silent.** It writes the wrong pin map into a normal-looking
artifact. **Tell the two Heltecs apart by enclosure** — both CP2102 bridges report
`SER=0001`, and the port name is not stable across replug. The XIAO is unambiguous: it is the
only `usbmodem` port.

**To tell the two Heltec ports apart without opening an enclosure, read the boot banner.**
Opening the port reboots the board, which prints it; the bridge's first line is `LRAN Bridge
Node - node 0x00`. Open with `dtr` and `rts` low before `open()`, as `simctl`'s `Console`
does, or the open presses PRG. **Confirmed on 2026-09-17**, when the bridge answered on
`/dev/cu.usbserial-0001` and the simnode Heltec on `/dev/cu.usbserial-3`.

**No stored state on any of these boards is the only copy.** Every survey site and every B1b
position is committed under `docs/rangetest/data/`, and every M22 run under
`docs/bridge/data/`.

**Its antenna stays on it.** The bridge uses the range test's 3.0 dBi 19 cm stick (Bridge
PRD **R-4.3a.1**); the gain is a term in D1's EIRP arithmetic, and `radio_config.h` asserts
the sum at compile time.

## Behaviour that changed, and will make older artifacts read differently

- **V-B12 is a B4 criterion since 2026-09-17**, not a B3b one. Text saying B3b is blocked on
  it, or that no task owns it, is correct for before that.
- **`lran/bridge/diag/radio/state` carries `rx_no_interrupt` and `rx_wake_empty` since
  2026-09-17** (`rx_wake.h`), and **`cad_free` and `rx_deaf_ms` since later the same day**
  (`rx_deaf.h`). Text saying no counter sits between `RX_DONE` and the ladder is correct for
  before the first pair; text treating `cad_backoffs` as the bridge's media-access instrument
  is correct for before the second. **All four are bridge-local and deliberately not spec
  §14.1 counters**, so they are absent from `lran/bridge/diag/state` and from schema `0xF0`.
- **`enter_mode()` is the one place `lora_link.cpp` changes `g_mode` since 2026-09-17.** A
  transition that assigns it directly now escapes the deaf-time accounting.
- **`lran/bridge/diag/rxlog/state` exists since 2026-09-17** (BF-27), carrying one record
  per frame in and out. **It is the one bridge topic that is not retained on a `/state`
  leaf**, against spec §16.2's table, and the deviation is raised rather than settled —
  Impl Plan §6.6.1 says why. Text saying the bridge publishes only aggregate counters is
  correct for before it.
- **`log_task` drains that ring and is no longer idle since 2026-09-17.** It was a `vTaskDelay`
  loop with a `TODO(BF-11a)` in it from BF-11 until then. **The leveled log still has no
  queue**; only the frame log uses the task.
- **The bridge accepts protocol version N *and* N−1 since BF-22**, and addresses each node
  in the version that node announced. Text saying it accepts only N — or that `bad_ver`
  moves `rx_bad_ver` twice — is correct for before it. `lran/<node>/diag/state` gained
  `unsupported_ver`.
- **The bridge subscribes to `lran/+/cmd/+/set` and publishes `lran/<node>/cmd/ack` and
  `lran/bridge/diag/cmd/state` since BF-18.** Text saying the bridge only publishes, or
  that `subscribe()` has no caller, is correct for before it.
- **`set_displaced` moves one counter since BF-21**, not two. A capture from before it also
  shows `rx_reassembly_timeout`.
- **The bridge prints `poll: <node> answered in N ms (window N ms)` since `28ffd82`.** Text
  saying no poll timing is observable is correct for before it.
- **The XIAO simnode boots as `0xF1 ROLE_GATELINK` since BF-6**, not `ROLE_RANGE`.
- **`media_access` and the PHY constants moved to `lib/lran-link/` on the B0 branch.**
- **The handheld Heltec is a simnode from 2026-09-14.** Older text calls it the range test's
  board and says it never runs a simnode build.
- **The ladder refuses unregistered sources since BF-15.** It was counted as
  `unregistered_src` until **BF-15a renamed it `rx_unknown_src`** and moved it inside
  `rx_dropped`; the old name exists nowhere now.
- **`RxMessage` carries a decoded header and complete payload since BF-16**, not raw bytes.
- **`TaskSpec::stack_words` is `stack_bytes` since BF-16**; only `lora` changed, 4096 → 8192.
- **The OTA verdict requires `radio_ok` since BF-16.**
- **`LoRaBridge` is retired in favour of `Bridge Node`, 2026-09-10** (**D17** amended).
  `lran-bridge` remains the firmware target and **"LoRa Bridge" remains the HA device name**.
- **The PHY parameters are stated rather than deferred, 2026-09-10.** A document citing
  Protocol Spec v0.9 or earlier reads §12.1 as "per D1" and `backoff_max_ms` as 500.

## Traps that cost real time here

- **The bridge loses frames on a clean bench at one metre, and the spacing that protects a
  measurement is not known.** The morning of 2026-09-17 measured 5.6 % at 250 ms falling to
  0 % at 1100 ms, twice. **The afternoon did not reproduce it**: ten bursts put 250 ms at
  2.50 % and 2000 ms at 1.90 %, including a 2000 ms control that lost 8 % and a 250 ms arm
  that lost nothing. **So do not treat "space them above 1 s" as a safe rule** — it was the
  rule this file carried until those runs, and one 2000 ms control has since broken it.
  **A bench measurement that counts frames needs a control arm in the same session**,
  whatever the spacing. **The cause is not known**; `kIrqReadMs`, the bridge's media access
  and its transmit path are all ruled out, the last per frame. Engineering log, 2026-09-17,
  seven entries — read the last one first.
- **`cad_backoffs` counts a *busy* CAD only.** A CAD that returns free still takes the radio
  out of receive and increments nothing. A zero in that column is not evidence the radio
  stayed in receive. **Read `cad_free` and `rx_deaf_ms` beside it since 2026-09-17**; the
  gap is closed, and every run recorded before that date carries it.
- **`rx_deaf_ms` is read against the window it was differenced over, not on its own.**
  `per_measure` times each window and prints the fraction. The two spans are not perfectly
  aligned — the tool times the `rx` readings while the radio document is whichever arrived
  most recently — so read a fraction far below the PER as ruling the transmit path out,
  never as a figure to quote to two decimals.
- **A gap in the frame log's `seq` is not a lost frame until the type is checked.** W11: a
  `PING` responder echoes the initiator's `seq`, so a node's `PING` answers carry numbers
  from the *bridge's* sequence space. `rxlog_analyze.py` keys streams on `(peer, type)` for
  this reason. Reading the topic by hand without doing the same invents losses.
- **A frame-log record can go missing two ways and only one of them is the bridge's.** The
  ring overwrites when `log_task` falls behind, and that is reported as `lost` inside the
  payload; a record that left the bridge and never reached a subscriber is QoS 0, a broker
  restart, or a tool that started late. **`rxlog.py` reports them separately and they must
  not be added** — the second has nothing to do with the receive path.
- **A zero in `rx_no_interrupt` is a result, not an absence.** It means every `RX_DONE` the
  radio raised arrived with its own DIO1 edge. **What it cannot see**: a frame whose
  `RX_DONE` was cleared by a neighbouring `readData()` before any pass looked — the first
  frame's edge is real there, so the pass reads as an ordinary packet and `rx_wake_empty`
  stays zero too. Do not read a zero pair as "the receive path is clean".
- **A bench node's `lran/<node>/diag/state` is not published at all**, so `unsupported_ver`,
  `proto_ver` and the per-node link are invisible at the broker for `f0`-`f3`. Spec §16.6
  gates them on `simnode_diag_enable`, which **BF-26** has not built. Read the bridge's
  serial instead.
- **A disabled identity re-enables itself on the next boot**, and opening a simnode's serial
  port reboots it. **Quiet both boards in the same session that runs the measurement**, and
  hold the ports open.
- **An identity in `ROLE_FAULT` answers no `POLL`** (`node.cpp`), which is exactly what a PER
  measurement wants and exactly what makes it go `offline` after three missed polls. Use
  `ROLE_HEALTH` for an identity that must answer.
- **A command takes 4-9 s from the MQTT publish to the node**, not the ~1 s the radio alone
  suggests: `sched_task`'s 1 s tick, the TX queue behind the poll scheduler, and media
  access each add to it. Three of BF-18's bench attempts were lost to assuming ~2 s.
- **Opening the simnode's port changes its `ctx_id`**, so the bridge's learned context goes
  stale on every reconnect. Useful for reaching the resync deliberately; announce with
  `push f1` afterwards for everything else.
- **A background serial capture piped into `tail` writes an empty file**, because the pipe
  buffers until the process exits. Redirect to a file instead, and run Python with `-u`.
- **Opening *either* board's serial port reboots it, the XIAO included.** Hold one port open
  for a whole run rather than reconnecting per command.
- **A reboot of the bridge board zeroes every counter.** Read the counters, then leave that
  port alone for the rest of the run. `per_measure` refuses a window this happened in.
- **Bench cross-traffic moves the bridge's counters.** W9's pings are addressed to another
  node and the bridge still hears them: `rx_not_addressed` and `rx_dropped` both climbed by 8.
  Difference a counter only across a window carrying nothing else.
- **A `PING`'s `n` is payload and caps at 202.** `ping f0 222` answers `ERR ping f0: n above
  202`. The 222 in B3a's criterion is the frame: `kMaxFrame` 222 − `kHdrLen` 16 − `kCrcLen` 2
  leaves 204, and the ping header takes two.
- **`flood` sends one frame unless you give it a count.** `fault f1 flood 50 gap 0` is the row
  §10.5 describes; `fault f1 flood` is one frame and proves nothing.
- **An entry whose correct result is "nothing happens" needs a second reading.** A silent pass
  and a frame that never arrived look identical at the broker. Check `rx_frames` moved.
- **A wrong broker address reads as `Error: Bad file descriptor` from `mosquitto_sub`**, not
  as a connect failure. The bridge's banner prints `MQTT broker:`; believe it over a shell
  variable.
- **`mosquitto_sub` block-buffers into a pipe**, so `| tee` shows an empty file for minutes
  while it is working. Subscribe with a client that line-buffers.
- **A bench identity is polled only after the bridge has heard it.** `push` works for
  `ROLE_GATELINK`; `fault <id> hdr_rsv` announces any role and moves no counter.
- **Two simnode boards boot with the same identities** (`f0`, `f2`), and opening either serial
  port resets its board to them. Reconfigure one in the same session that runs the test.
- **`AUTH_FAIL` (reason 202) on the bridge's first WiFi attempt at every boot** is expected as
  of 2026-09-15, five boots out of five; a later attempt connects. Unexplained, harmless so
  far, and invisible except in the serial log.
- **V-B9 needs the broker.** The verdict requires `mqtt_connected`, so with the broker down a
  good image rolls back and reads as a firmware failure.
- **V-B9's bad images print the same version as the good image.** Read `Slot:`, the V-B9
  banner and the `WiFi SSID:` line to tell them apart.
- **A bench counter check must be read at the broker**, not from the console: the bridge has
  no console, `diag/state` publishes every 60 s, and a reflash resets every counter to zero.
- **`lib_extra_dirs = ..` in a library's own test project loses `unity.h`.** Use
  `lib_deps = symlink://../<dep>`, as `lib/lran-link/platformio.ini` does.
- **A simnode PING to `00` reports no echo.** The bridge does not answer PING yet.
- **An `RxLadder` with no `PeerKeys` refuses every frame**, as `unregistered_src`.
- **The bridge's ERROR reply and the sender's next frame deafen each other.** Half duplex:
  the bridge cannot receive while it answers, and the sender cannot hear the answer while it
  transmits. A multi-frame §10.5 row loses a frame and its reply to this, and it is not a
  defect at either end. **`gap` spaces injections, not the frames inside one injection.**
- **The simnode logs a received ERROR's `err_code` since 2026-09-16.** Before that it routed
  `MsgType::Error` to `default: ++unhandled`.
- **`registry_begin()` must run before `start_tasks()`**, and **`lora_task` must never call
  `registry_runtime`**, which waits on a mutex.
- **The library's platform crypto is not in its build.** `platform/esp32/` and
  `platform/native/` are added by each firmware's `build_src_filter`.
- **The codec returns `Ok` for an authenticated frame it had no key to check.** Test
  `mac_verified`, never the status alone.
- **ESP-IDF stack depth is bytes.** Size a stack from `uxTaskGetStackHighWaterMark`, and read
  it as a range: two boots differed by 248 bytes.
- **RadioLib's `scanChannel()` has no timeout and `transmit()` busy-waits.** Start the
  operation and read the IRQ register against a deadline, as `lora_link` does.
- **Receive routes only `RX_DONE` to DIO1.** `HEADER_VALID` and `HEADER_ERR` never wake the
  task; `lora_link` reads them on a 1 s poll and before a CAD.
- **Never ask `getPacketLength()` whether a packet arrived.** Gate on `RX_DONE`.
- **RadioLib's `SPIClass` `Module` constructor allocates on the heap.** Construct an
  `ArduinoHal` in static storage.
- **The sandbox broker refuses anonymous clients** (`CONNACK 5`).
- **A retained message read at subscribe time is not evidence of this boot.** Compare the
  `version` payload, or wait for a live publication. `diag/state` is retained.
- **An OTA upload can fail 1 s in with `Receive Failed`** while espota's progress bar climbs.
  Retry once before debugging.
- **Opening the serial port can press PRG.** GPIO 0 is on the CP2102's DTR. Construct the port
  unopened and set `dtr = False` before opening.
- **`pio test -e esp32s3` overwrites whatever the board was running.**
- **A C++ `verifyRollbackLater()` links cleanly and does nothing.** It must be `extern "C"`.
- **`MQTT_MAX_PACKET_SIZE` defaults to 256 bytes in PubSubClient.** Set it ≥ 1024.
- **HA's entity registry remembers every `unique_id`, and a retained discovery config survives
  a reflash.** Develop against the dev HA VM and dev broker until **B6**.
- **The Heltec V3's TCXO runs at 1.8 V; the OLED sits behind Vext; the vendor header's `DIO0`
  on GPIO 14 is the SX1262's DIO1.** All three fail without an error.

## Open, and not closable from here

- **The bridge drops frames spaced closer than about 1 s, and nothing on the bridge can yet
  see why.** Measured 2026-09-17 across five spacings — **5.6 % at 250 ms, 5.0 % at 400,
  1.0 % at 700, 0 % at 1100 and at 2000** — and the endpoints reproduced the same day on an
  instrumented build. **`kIrqReadMs` was the suspect and is ruled out**: `rx_no_interrupt`
  read zero across 190 frames spanning both arms, so the interrupt path delivered every
  `RX_DONE` the radio raised and the timed read never recovered anything.
  **The transmit path is ruled out as well**, later the same day: `rx_deaf_ms` held at
  **639–659 ms per 60 s window across five bursts** whose PER ran 6 %, 2 %, 2 %, 0 %, 0 % —
  the same deafness in the burst that lost three frames and in the burst that lost none.
  Bridge transmissions had already failed to predict losses burst by burst: across eight
  `--gap 250` bursts, transmissions against frames lost run 3/5, 0/4, 7/1, 3/1, 6/3, 4/2,
  3/1, 3/1. Nothing is corrupt, nothing is discarded, and the RX queue never went deeper
  than 1.
  **BF-27's frame log closed the transmit path a third time, per frame**: across ten bursts
  and nine lost frames, the bridge's own deafness can account for at most **0.52** of them,
  and 376 logged receptions were all announced by their own interrupt, all uncorrupted and
  all accepted by the ladder.
  **What the same ten bursts also did was fail to reproduce the spacing curve** — 2.50 % at
  250 ms against 1.90 % at 2000 ms, with the losses falling in four runs of the ten whatever
  the spacing. **So the open question has moved.** It is no longer only "why does a
  closely-spaced frame go missing"; it is **whether spacing is the variable at all**, and no
  sweep on record can answer that, because every one of them ran its arms in blocks.
  **The interleaved sweep is the next step and it needs no new code.** **It matters beyond
  M22**: a fragmented `STATUS` is exactly this pattern, and **BF-24's decode work meets it
  first**.
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
  MQTT receive path it was paired with is built** — BF-18 did it, so a `config/set`
  subscriber now has a transport seam and an inbound queue to reuse.
- **The receive path's losses** — still unassigned, and **three hypotheses shorter**. **Do
  this before BF-24**, which decodes a fragmented `STATUS` into exactly this traffic
  pattern. All three instrument steps are **built**, all bridge-local rather than spec
  §14.1, and each came back clean: `rx_wake.h` (`rx_no_interrupt`, `rx_wake_empty`) ruled
  out `kIrqReadMs`, `rx_deaf.h` (`cad_free`, `rx_deaf_ms`) ruled out the transmit path in
  aggregate, and `frame_log.h` (BF-27) ruled it out again per frame — a ceiling of 0.52
  frames against nine lost.
  **What remains is not another instrument. It is an interleaved sweep**, and it exists
  because the same ten bursts put a 250 ms gap at 2.50 % against a 2000 ms gap at 1.90 %.
  **Every sweep on record ran its arms in blocks**, so a slow environmental change and an
  effect of spacing are not separated anywhere. Alternate the two arms inside one session,
  45 s a burst, `rxlog.py` collecting.
  **A poll-free burst is not available** and does not need to be: the flooding node enrols
  itself on its first frame (`scheduler.cpp`), so it is polled regardless of what else is
  quiet — and the poll load is now measured rather than avoided.
- **The Implementation Plan cites PRD v0.6; the PRD is at v0.12** — found 2026-09-17 while
  editing that header, and deliberately not bumped. **Reconcile §§2.1, 2.2, 3.2, 7.1 and 8
  against the PRD's v0.7–v0.12 changelog first, then correct the citation**; a bare number
  bump is the failure the citation rule exists to expose. **The more useful half is why
  nothing caught it**: `tools/checks/spec_citation_version.py` covers protocol-spec citations
  only, so no check reads a PRD-to-plan or plan-to-tasks citation. Extending it will likely
  surface other stale ones — report those rather than fixing everything on one branch.
- **R-3.2c is unmet and no task names it.** The PRD says *"WiFi RSSI SHALL be published as
  a diagnostic."* `wifi_rssi_dbm()` exists (`wifi_link.cpp`) and is read into the OLED
  status page and **nowhere else** — `task_runtime.cpp` puts it on the screen and no MQTT
  topic carries it. Verified 2026-09-17 by reading every caller. **It belongs in BF-24's
  table**, which is the task that owns what the bridge publishes; it is recorded here
  because BF-24's row does not mention it and nothing else would surface it.
- **A PING responder on the bridge** — spec §17.3 requires RF loopback of every node build.
- **Decoding per schema has no task** — Impl Plan §5.3's `decode/`; `app_task`'s `TODO` gives
  it to BF-24. Nothing a simnode sends is decoded or published today.
- **Six task stacks unmeasured** — every size but `lora` is still BF-11's figure.
- **The first-attempt `AUTH_FAIL` at every boot** — reproducible, unexplained, unassigned.
- **A serial log line per network state change** — proposed, not assigned to a task.
- **The OTA first-data timeout** — one failure in two uploads on 2026-09-13; it did not recur
  on 2026-09-15's three uploads.
- **R-5.3d on hardware** — an OTA upload deferred during a LoRa transaction.
- **BF-11a** (log queue drain) and **BF-11b** (hardware watchdog from `sched_task`).
- **What GateLink's `COMMAND_ACK` waits for** — pulse complete, or gate confirmed. GateLink
  **M3** needs the answer.
- **GateLink M0's LDO margin** — sized for Envelope B's 19.6 dBm, tested only at −4 dBm.
- **The range-test firmware still transmits on the provisional 915.0 MHz.**

### Closed, and not to be reopened by habit

- **B3b — accepted 2026-09-17.** Its tasks were on air the day before; the milestone closed
  once V-B12 had a home. Do not reopen whether V-B12 should have had a `BF-*` number.
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
