# Bridge Node — session handoff

**Written 2026-09-23 by the session that built BF-23's lever half on branch
`b4-bf23-levers`.** No board was touched, so the bench and its findings are as the
2026-09-21 session left them. This file replaces the previous one wholesale.

**What is durable: BF-23's lever half is built and host-tested, and it is not on air.**
Every bridge row of BF-32's table now reaches the code it configures, except
`simnode_diag_enable`, which is BF-26's. The bench is
live, and each board runs its own firmware from before this branch.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees
> with the documents below, they win — check the log's last entry against the date above
> before trusting anything here.

## The next job, in one place

**The next job needs the bench and the operator.** BF-23's lever half is host-tested only.
It is proven when the bridge board runs it and a set from Home Assistant moves a lever.
Reflash the bridge from this branch's committed tree. The engineering log's 2026-09-23
entry lists what to look for:

1. **`levers: gen N` on serial at boot**, carrying what NVS restored rather than the
   defaults. Set something first, then reboot.
2. **A `diag_interval_s` set changes the spacing of `lran/bridge/diag/state`.** That set
   is also V-B12's saturated-arm lever.
3. **A `poll_interval_s` set on a simnode** moves its next poll to the new interval,
   counted from its last one.
4. **`sched_task`'s high-water mark** after all three. It logs one on every configuration
   resolution. `sched_levers()` keeps its two `Levers` static, but its `printf` is new
   stack.

**When those pass**: mark BF-23 on air in Tasks §7 and Impl Plan §4.4.2, append the run to
the engineering log, and mark the PR ready. **V-B12's saturated arm is then runnable**.
Impl Plan §8.1.1 says how: interleave it with its idle control, never run it in blocks.

**If the bench is not available, BF-26 is next and needs no board.** Then BF-24.

- **BF-26**, `simnode_diag_enable`. Its row is in the table, and nothing reads it yet.
  **Consider carrying it on the lever board** (`levers.h`), because `sched_task` reads it
  and `mqtt_task` writes it. The struct leaves it out only because BF-26 was not yet
  built. The
  `TODO(BF-26)` gates are in `task_runtime.cpp`, and turning the switch on needs
  `g_availability.mark_known_pending()` and a discovery republish.
- **BF-24**, the decode and publication policy. Its `TODO(BF-24)` markers are in
  `task_runtime.cpp` and `task_runtime.h`.

### What BF-23's lever half decided

Impl Plan §4.4.2 has the full table. The short version:

- **A lock-free board, one writer.** `mqtt_task` reads its own store and publishes atomics
  under a generation counter. `sched_task` and `lora_task` each take a consistent copy and
  apply it themselves. `lora_task` waits on nothing.
- **Published after the NVS restore, and as soon as a set changes the store.** The second
  publish sits before `handle_config_set()` queues a node half's job. That function
  returns early once the job is queued.
- **`PollScheduler::retime()`**: a changed poll interval counts from the last poll.
- **`test_levers` checks each consumer's compile-time default against its row's
  default.**

### What the 2026-09-21 bench session established

**The bench loss rate is a function of frame spacing, and a second variable rides on it.**
Two interleaved sweeps, 1280 frames: **250 ms lost 3.91 %, 2000 ms lost 0.31 %**, and the
denser arm lost more in **all four** run-by-half cells. Reading either two-way split alone
gives a different answer, which is why `tools/simctl/sweep_analyze.py` prints the
cross-tab and the verdict reads it rather than the pooled arms.

**The second variable is unidentified.** The dense arm moved by a factor of four *within*
each session — up in run 1, down in run 2 — while the sparse arm held still. That is why a
single block-ordered sweep can land anywhere between 0 % and 8 %.

**Every frame-log cross-check was clean across 1253 receptions**: zero orphans, nothing
corrupt, nothing discarded at any §14 stage, and the bridge's own deafness accounts for at
most 1.15 of run 1's 17 losses. **The mechanism is still not known.**

**BF-32 works from Home Assistant to a node and back**, confirmed on air: a set applies and
persists, a clamp is reported rather than applied quietly, a set naming both halves draws
**one** `config/ack`, and a reboot restores what NVS held. Impl Plan §6.7 has the design;
the engineering log's 2026-09-21 entry has the bench session.

**Three defects the host tests could not find, all fixed.** Each is a shape worth
recognising again, and §6.7.5 names them: a simnode answering a solicited `CONFIG_ACK`
under its status `seq` rather than repeating the request's, a `sched_task` stack overflow
from a 1 KB job on a 3072-byte stack, and a set's ACK blanking the state rows it did not
name.

## Open, and not closable from here

- **`g_config` is used from two tasks with no lock between them.** Found 2026-09-23, and
  it predates the lever half. `mqtt_task` calls `apply()`, `restore_defaults()`,
  `read_all()` and `state()`. `sched_task` calls `note_readback()`, `note_set_results()`
  and `state()` from `publish_config_resolution()`. Neither `ConfigStore` nor `Store`
  takes a lock. A set arriving while a node transaction resolves can read a half-written
  mirror or store. **It needs its own change**, either a lock around `g_config` or the
  resolution handed to `mqtt_task`, and the operator's view on which.
- **No row covers `ConfigPath`'s CONFIG_ACK wait.** `kConfigAckTimeoutDefaultMs`, 8000
  ms, is a timing constant with a setter that nothing calls. Root rule 8 says it should be
  runtime-configurable. Adding a row is a table change, and table names are permanent, so
  it is the operator's call.

- **The state mirror survives a node reboot and nothing invalidates it.** A simnode holds
  its overrides in RAM, so a reboot clears them while `config/state` goes on reporting the
  old values as current. A node with a store (GateLink, microSD, **D49**) keeps them, so
  the bridge cannot tell from the reboot alone — **the answer is a readback when a node's
  `ctx_id` changes**, which is a small addition that deserves its own thought. Not a
  regression: before BF-32 there was no mirror at all.
- **The bridge loses frames at one metre and the cause is not known.** Spacing is now a
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
- **The citation sweep**, 31 sites, still the only thing holding the spec header at v0.12.
  The operator's rule: once, when the configuration pass is finished. BF-32 is now done, so
  **this is close to ready**. The whole-document style passes are owed with it.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the **2026-09-23 entry** first, BF-23's lever half, then the **2026-09-21 entries** — BF-32's bench session and the interleaved sweep — then 2026-09-20 and 2026-09-19. Entries from 2026-09-10 to 2026-09-16 are in [`engineering-log-2026-09-10_2026-09-16.md`](./engineering-log-2026-09-10_2026-09-16.md) |
| 3 | [`traps.md`](./traps.md) | the section for the work you are about to do |
| 4 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§4.4.2** BF-23's lever half; **§6.7** BF-32's configuration path; **§8.1** V-B12 and **§8.1.1** what the interleaved sweep found; **§6.6.1** BF-27's frame log; **§10.5** the fault catalogue; **§4.4.1** BF-23's discovery |
| 5 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | §7 is B4, where the work goes next |
| 6 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and what breaks silently |
| 7 | [`firmware/simnode/CLAUDE.md`](../../firmware/simnode/CLAUDE.md) | what the simnode has and its traps |
| 8 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) | §9–§12, §14, §16; **§18.2, never §18.1 alone**. For configuration work: **§7.4**, **§7.4.1**, **§8.10–§8.12**, **§12.4**, **§16.7**. **Read the header block first** — the version is pinned at v0.12 on purpose and the block says why |
| 9 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) | **§3.4.1** D1 closed at 917.4 MHz and D33's condition 3 restated; **§3.6** D43–D57, the configuration set; **§5.4** M20's channel evidence |
| 10 | [`LRAN-D1-Parallel-Capture-Analysis`](../shared/LRAN-D1-Parallel-Capture-Analysis.md) | why 917.4 MHz won. [`LRAN-D1-Frequency-Change-Brief`](../shared/LRAN-D1-Frequency-Change-Brief.md) is **superseded** |
| 11 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**, **B0**, **B3a**, **B3b**. **M6**, **M19**–**M21**, **M24**, **M25**. **D1** and **D33**, register §3.4.1. **D34 amended**; **D35–D57**. **W4**, **W7**, **W9**, **W10**, **W12**. **V-B3**, **V-B9**, **V-B10**. **BF-2**–**BF-9**, **BF-15**–**BF-22**, **BF-27**'s frame log, BF-23's **discovery** half and its **lever half, host-tested**, **BF-32 entire**. `firmware/chan-capture/`, `lib/lran-link`'s `ChanMonitor`, `lib/lran-config/` |
| Not done | **B4**: BF-23's lever half **on air**, **BF-24**, **BF-25**, **BF-26**. **BF-33** unstarted. **BF-27's other three tools**. **V-B12**'s saturated arm, runnable once BF-23's lever is confirmed on air. **M22** open — spacing is now measured, the mechanism is not. **M26**. **BF-11a**, **BF-11b**. **The citation sweep**, 31 sites, and the style passes with it |
| Queue | **BF-23's bench confirmation**, which needs the board, then **V-B12's saturated arm**. Without the bench, **BF-26** and then **BF-24**, neither of which needs a board |

```bash
pio test -d lib/lran-protocol -e native         # library host suite
pio test -d lib/lran-link -e native             # spec 12.3 media access
pio test -d lib/lran-sim -e native              # BF-7's FramePatch vs. the W4 negatives
pio test -d lib/lran-config -e native           # BF-32's table and store
pio test -d firmware/bridge -e native           # bridge host suites
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

**Two branches revising one document will collide on its version number, silently**, and
git will auto-merge the version line because both sides typed the same text. **Before
revising a shared document, read the other branch's copy** — `git show
origin/<branch>:<path>` — and take the next version. This bit twice: the Decision Register
on 2026-09-20, and the Bridge Implementation Plan on 2026-09-21, where one branch took
v0.40 and the other therefore took v0.41. Put new reasoning in a **`.N` subsection** under
the section that owns it, the §3.2.1 precedent, rather than the next free number.

**Merging a stack: never pass `--delete-branch`.** Deleting a base branch **closes** the PR
stacked on it rather than retargeting it. Merge each PR without it, retarget the next to
`main` while it is still open, then delete branches by hand. **Check `gh pr view <n> --json
baseRefName` after every stack merge.** Expect the child to conflict once retargeted, and
**merge `main` in rather than rebasing** — a rebase rewrites the commits the next PR is
built on.

**Deleting a remote branch is the operator's command, not the agent's.** Do the local half
— `git worktree remove`, then `git branch -d` — record the tip SHAs, and hand over one
`git push origin --delete <names…>` line.

**A push touching `.github/workflows/` needs workflow token scope.** It was refused once,
on 2026-09-08, and accepted since. Try the push; if it is refused, the operator refreshes
auth.

**Permanent history is citable; moving state is not.** `4250e00` (six document defects),
`ebdcf0d` (the `LoRaBridge` retirement), `8253085` (**P8**, D34's amendment, spec v0.11),
`76e6d11` (M25's capture), `530a137` (D1 day 1's captures), `47b8c87` (D1 accepted, D33's
condition 3 restated) and `8fba937` (`/lib/lran-config/`).

## Bench credentials — the broker is a sandbox, and that changes what is safe

**The broker at the address in `secrets.h` is a disposable sandbox Home Assistant install
with its own Mosquitto.** Its credentials are **not** the production ones, so they may be
put into the environment directly rather than prompted for. **That changes at the
production cutover**, after which a password must not reach argv, a log or a committed
file.

**`simctl`, `per_measure`, `rxlog` and `sweep_interleave` read `LRAN_MQTT_HOST`,
`LRAN_MQTT_USER` and `LRAN_MQTT_PASSWORD` from the environment and never take them as
arguments.** Source them out of `secrets.h` with command substitution so nothing prints:

```bash
export LRAN_MQTT_HOST=$(sed -n 's/^#define MQTT_HOST[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
export LRAN_MQTT_USER=$(sed -n 's/^#define MQTT_USER[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
export LRAN_MQTT_PASSWORD=$(sed -n 's/^#define MQTT_PASSWORD[[:space:]]*"\(.*\)".*/\1/p' secrets.h)
```

**The OTA password is read from `LRAN_OTA_PASSWORD`** in the shell that runs the upload.

## Hardware state

**All three boards were reflashed from their own projects on 2026-09-21** and none is
running `chan-capture` any more. **This table names the devices in this subproject's
terms**; the range-test handoff owns them in its own roles.

| Device | Called here | Told apart by | Firmware | Current state |
|---|---|---|---|---|
| Heltec V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `firmware/bridge -e heltec`. MAC `44:1b:f6:f9:70:14` | **At its production position in the office, NW wall, desk height.** On USB as `/dev/cu.usbserial-0001`. NVS holds the configuration store — clear a bench value with `{"op":"restore_defaults"}` on its `config/set`, not by reflashing |
| Heltec V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `firmware/simnode -e simnode-heltec`. MAC `44:1b:f6:fa:bc:2c` | In the office, about 1.5 m from the bridge board. On USB as `/dev/cu.usbserial-3`. Its port name moves across replug |
| XIAO ESP32S3 + **Wio-SX1262 Kit** | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `firmware/simnode -e simnode-xiao-wio`. MAC `68:ee:8f:4b:85:f4` | On USB as **`/dev/cu.usbmodem1101`** — it was `2101` before a replug, and it is the only `usbmodem` port. Holds `f1` in `ROLE_GATELINK` and `f3` in `ROLE_FAULT` in NVS |

**The link ran −52 to −48 dBm on 2026-09-21**, about 12 dB weaker than the 2026-09-17
sessions, because the bridge board moved to its production position for the D1 capture and
has not moved back. SNR held at +10 to +12 dB. **Absolute loss rates are not comparable
across those sessions**; a comparison inside one sweep is.

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
  high-water mark on every configuration resolution. Read that number before adding
  anything to its tick. BF-23 added `sched_levers()` to that tick, and no board has run
  it yet.
- **Interleave the arms of any frame-counting sweep**, and give every one a control arm in
  the same session. `tools/simctl/sweep_interleave.py` does both.
- **HA's entity registry remembers every `unique_id`**, and a retained discovery config
  survives a reflash. Develop discovery against the dev HA VM and dev broker until **B6**.
