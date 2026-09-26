# Bridge Node — session handoff

**Written 2026-09-26 by the session that did group 2's housekeeping.** BF-11a's leveled
log, BF-11b's watchdog, `mqtt_task`'s high-water mark and the simnode's `phy reset` fix
are host-tested (Impl Plan §5.2.2) and were confirmed on the bench the same day. Group 2
keeps its two larger items, one session each.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees
> with the documents below, they win — check the log's last entry against the date above
> before trusting anything here.

## Start here

**A session does one task group, and reads only what that task needs.** Open it with one
of these lines, then read this section and the sections the table names — not the whole
file:

**Queued: nothing.** The operator picks the next task from *Work before GateLink*. Group 1
has one new item, the lost `BOOT` event. Group 2's two remaining items and groups 3 and 4
remain.

**Nothing is owed on the bench.** The engineering log's second 2026-09-26 entry has the
four readings. `sched_task`'s PHY-change low of 1352 bytes is not re-measured; the next PHY
run should read it.

**The cleanup the task produced is part of the task**: stale comments and document lines
it made wrong, `TODO(<id>)` markers it closed, rows here it finished, merged branches and
worktrees. **Close the session** by committing, pushing and opening the PR. Merge it once
the operator accepts it. Then rewrite *Start here* and *The next job* for the next task.
Anything out of scope goes in one line under the right group below, not into the session.

## The next job, in one place

**B4b is accepted** (Impl Plan §8).

**B5 is accepted** (Impl Plan §8), against BF-36's simulated MPPT, as §8 allows. The real
MPPT's readback, which confirms BF-30's register scales, comes at B6.

**A node reboot owes a readback** (Impl Plan §6.7.7). It is read from `STATUS`, never
from a new `ctx_id`, because a roll makes one (spec §10.1).

**The air-timing fixes change when a command can go.** A gate `OPEN` now waits behind a
`CONFIG` in flight to any node, for its ACK and its readback. The engineering log's two
2026-09-25 *air-timing* entries have the rule and the bench run.

**B6 and B7 need GateLink**, and GateLink M6 gates B6.

**For any PHY run:** set `simnode_diag_enable` to 1, and set `deployed` to 1 on each bench
row you want in the fleet. Clear both afterwards. A change may start at any time now.

**The sandbox HA is drivable by API.** The session holds an admin token for it outside the
repository, and HA's `hassio.addon_restart` restarts the broker. The Supervisor REST proxy
refuses a long-lived token.

## Work before GateLink

Each group is one session unless its line says otherwise.

### 1. Bridge defects the bench can reach

These came from B4b's bench runs. None blocks GateLink. The three air-timing defects that
sat on the command path, and the state mirror's readback, were fixed on 2026-09-25.

- **The three restart edges were closed on 2026-09-26**, host-tested (Impl Plan §6.7.8).
  A bench run would confirm the owed PHY `config/ack`: reset the bridge within 150 ms of
  a commit, and watch `lran/bridge/config/ack` for the answer after the reboot.
- **The bridge's readback `POLL` can cost a node its `BOOT` event.** The bridge answers a
  `BOOT` status with a `POLL` about 350 ms later (Impl Plan §6.7.7), while the node's
  `BOOT` event is due. One boot in five lost the event on 2026-09-26. A collision is likely
  but unconfirmed: read the simnode's `radio` counters across a few `reboot` cycles first.
  Whether the bridge should wait before that `POLL` is the operator's call. The
  engineering log's *simnode's resets are real* entry has the timing.

### 2. BF-27's simulators, and the watchdog's reach

One session each. BF-11a, BF-11b, `mqtt_task`'s high-water mark and `phy reset` were done
on 2026-09-26, and so was the simnode's reset (spec §10.1, §10.7, §8.14).

- **BF-27's bridge-side simulators and packet loopback.** They gate nothing. What they
  would add is an unattended, time-varying source; WellLink data waits for schema `0x20`
  (Impl Plan §8.2).
- **The watchdog does not see a hung `lora_task`, `mqtt_task` or `app_task`** whose locks
  stay free (Impl Plan §5.2.2). Whether the feed should wait on their progress is the
  operator's call.

### 3. A B7 rehearsal on the bench

**B7's conditions can run against the simnodes now**: broker restarts, WiFi outages and a
node power cycle, with `deployed` set on the bench rows. It cannot accept B7, which
depends on B6. What it can find is stuck availability or a frame lost on reconnect while
the node is on a desk rather than at the gate.

### 4. HA, before GateLink deploys

- **`mppt_charge_state` and `mppt_error` show raw VE.Direct codes in HA**, such as `3`, not
  names. Whether discovery maps them to names is BF-24's question, and GateLink's data
  will need it. The dummy publish can show either answer.
- **`charge/state` does not follow a setting changed behind the bridge**, with
  VictronConnect for example. A readback pass runs only on first hearing and after a write
  the bridge sent, so the next boot corrects it. Whether a periodic pass is worth the
  airtime is the operator's call. Found on the bench 2026-09-25.
- **A `BOOT` event's `reset_cause` reaches HA as a number** in `detail` (spec §8.14).
  Whether the bridge names it, as the event topics name nothing else today, is the
  operator's call.
- **`node/state` republishes on every frame**, because `uptime_s` is in it and changes
  every poll; `node/health/state` is the same. Publish-on-change never withholds either.
  Whether uptime belongs in the change hash is the operator's call. Impl Plan §6.6.2.

### 5. Documents and tools

- **The whole-document style passes** are owed, on a branch of their own.
- **`ha/README.md` says `kMaxPayloadLen` is 768 bytes.** It has been 1536 since BF-33, and
  the sentence's argument about discovery key lengths needs rechecking, not only the number.
- **`rssi_report.py`'s periodicity verdict is not to be trusted on a long capture.** It
  called the Davis "not periodic" on the day that confirmed its clock to half a second.
  Left unfixed by operator direction. Fix it before any future capture, because that
  verdict is what the documents cite when they attribute an occupant.

## Waits on GateLink or the operator

- **B6 and B7**, and **BF-30**'s readback against the real MPPT, which confirms the
  register scales and whether `0xEDEA` carries the system voltage setting.
- **Spec v0.16's four B5 readings**, raised in the engineering log's *B5's spec readings*
  entry. The code builds the operator's reading of each until then.
- **Gate 1 on air.** No bench tool sends a write-class `HEX_REQ` with a bad MAC, so the
  node's refusal is host-tested only.
- **A QoS 1 retransmission across a reconnect has not been seen on the bench.** It comes
  from reading espMqttClient's source. B7's broker-restart criterion is where it would show,
  with an event in flight at the moment the connection drops.
- **The readback mirror's `CLAMPED` fix is host-tested only.** No simnode row clamps; the
  first node-held row that can is in GateLink's table.
- **Set `deployed` on GateLink when it goes into the field**, on
  `lran/gatelink/config/set`. Until then the bridge polls it only once heard.
- **GateLink's configuration block, `0x1000`–`0x1FFF`,** waits for the GateLink milestone
  (Library Plan §4).
- **W17**, a node that misses every confirming frame of a PHY change (spec §12.4.4), has no
  remedy. It closes after GateLink deploys, by operator decision (D59).
- **M22 closed on a bench at one metre, where every frame arrived near −22 dBm.** It rules
  out a gross coexistence failure, not desense near sensitivity. The check that would
  reopen it is R-4.4b's PER at the gate, rising with the bridge's WiFi traffic, once
  GateLink is deployed.
- **The bridge loses frames at one metre and the cause is not known.** Spacing is a
  measured variable rather than a suspect; the mechanism is not. The three candidates
  inside the bridge stay ruled out from 2026-09-17, and M25 found nothing on the channel
  loud enough to matter. **A rate measured at one metre is still not evidence about 87 m**,
  and it is optimistic in the wrong direction.
- **The `radio_ok` half of the OTA verdict is untested on hardware.**
- **Nothing a node sends the bridge carries a MAC the bridge verifies.** §9.2 makes every
  authenticated type bridge → node, so `rx_rejected_seq` and `rx_dup_command` stay at zero
  by construction. This is a property, not a task.
- **M26** — the §3.1 equipment inventory, the operator's. No link's data rate is
  confirmed. It no longer gates D1 or D33; what still needs it is Decision Register §5.4's
  attribution of the 915.8–916.4 MHz cluster.
- **The IoT network's nearest access point is still pinned to channel 6** from V-B12.
  Whether it stays pinned is the operator's call.

## Reference documents

**Read only the rows your task names in *Start here*.** Rows 2 and 3 are the ones most
worth a targeted read: the log's latest entry for the task, and one section of the traps.

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the **2026-09-26 simnode's resets** entry, then the **2026-09-25 mirror readback** entry, then the two **2026-09-25 air-timing** entries, then the **2026-09-25 events at QoS 1** entry, then the **2026-09-25 Spec v0.15's code on air** entry, then the **code spec v0.15 owed** entry, then the **2026-09-25 pre-GateLink survey** and **BF-35** entries, then the **2026-09-24 entries**, the four **BF-33** entries and **D61**, last first, then **V-B4 passes**, **B4's acceptance tally** and then **V-B8 in Home Assistant**, first. Then the **twelve 2026-09-23 entries**, last one first: **BF-27's dummy publish**, **BF-25 built**, then **BF-24 built**, then **BF-26 on air**, then **V-B12 measured**, then **V-B12's blaster**, then its deferral, then **BF-34 on air**, then **BF-34 built** and its bench steps, then the configuration lock with the `seq` gap it closes, then BF-23's lever half and its bench run. Then the **2026-09-21 entries** — BF-32's bench session and the interleaved sweep — then 2026-09-20 and 2026-09-19. Entries from 2026-09-10 to 2026-09-16 are in [`engineering-log-2026-09-10_2026-09-16.md`](./engineering-log-2026-09-10_2026-09-16.md) |
| 3 | [`traps.md`](./traps.md) | the section for the work you are about to do |
| 4 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§6.7.7** the readback a node reboot owes; **§4.3.3** BF-37's transport and BF-38's event queue; **§6.7.2a** spec v0.15's code; **§4.4.3** BF-35's controls; **§8.2** B4's tally; **§6.6.2** BF-27's dummy publish; **§6.3.2** BF-25's events; **§6.3.1** BF-24's publication policy; **§4.2a.1** BF-26's bench gate; **§6.2.2** BF-34's context roll; **§4.4.2** BF-23's lever half; **§6.7** BF-32's configuration path and **§6.7.6** its lock; **§8.1** V-B12, **§8.1.1** what the interleaved sweep found and **§8.1.3** V-B12's result; **§6.6.1** BF-27's frame log; **§10.5** the fault catalogue; **§4.4.1** BF-23's discovery |
| 5 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | **§9** is B5–B7, where the work goes next |
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
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**, **B0**, **B3a**, **B3b**. **M6**, **M19**–**M22**, **M24**, **M25**. **D1** and **D33**, register §3.4.1. **D34 amended**; **D35–D58**. **Protocol Spec v0.13** and its citation sweep, merged. **W4**, **W7**, **W9**, **W10**, **W12**. **V-B3**, **V-B9**, **V-B10**, **V-B12**. **BF-2**–**BF-9**, **BF-15**–**BF-22**, **BF-27**'s frame log, **BF-23** both halves, the lever half **confirmed on air**, **BF-32 entire**. **BF-34 confirmed on air** and merged. **BF-24** and **BF-25** built, host-tested and shown at the broker and in HA. **B4 accepted** 2026-09-24, Impl Plan §8.2. **B4b accepted** 2026-09-24, BF-33 entire. **The poll clash**, fixed and shown on air 2026-09-24. **BF-35**, built and shown in the sandbox HA 2026-09-25. **The `vectors_data.h` check**, in CI 2026-09-25. **`spec_citation_version.py` reads role header lines**, and **the six stale citations it found are reconciled**, 2026-09-25. **BF-27's dummy publish** built and on air. **Spec v0.15's code confirmed on air** 2026-09-25, with the `not_applied` and `CLAMPED` fixes and the simnode's `CONFIG_CHANGE`. **BF-26 confirmed on air**, and the bench restored after V-B12. **BF-37** and **BF-38**, built and shown at the broker 2026-09-25, with the HA synthetic filter. **BF-36** and **BF-28**–**BF-30** built, **V-B6** passed on the bench, and **B5 accepted** 2026-09-25. **Group 1's three air-timing defects**, fixed and run on the bench 2026-09-25. **The state mirror's readback after a node reboot**, the same day. **Group 1's three restart edges**, host-tested 2026-09-26. **BF-11a**, **BF-11b**, `mqtt_task`'s high-water mark and `phy reset`'s overrides, host-tested 2026-09-26. **The simnode's real reset**, run on the bench 2026-09-26. `firmware/chan-capture/`, `lib/lran-link`'s `ChanMonitor`, `lib/lran-config/` |
| Not done | **B6**, **B7**. **BF-27's** bridge-side simulators and packet loopback. **The simnode's spec v0.16 reset obligations**. **M26**. The whole-document style passes |
| Queue | Empty; the operator picks from *Work before GateLink* |

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
python3 tools/checks/spec_citation_version.py   # versioned citations vs. each target's header
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

**The bridge board runs `3534e0b`, the mirror readback, flashed on 2026-09-25** from a
tree identical to it. **The XIAO runs `7e7b92a`**, B5's code, and was reset in that run. **The Heltec simnode runs `75a3ed0`**, spec v0.15's code, flashed the same day.
The broker retains `simnode1`'s `vedirect/charge/state`, `hex/audit` and
`write_enable/state` from V-B6, and no `write_enable/set`. The bridge holds `cmd_retries` 3, its default, as an
override from D68's check; `restore_defaults` clears it. All three were on USB when this session ended: the bridge as
`/dev/cu.usbserial-0001`, the Heltec simnode as `/dev/cu.usbserial-3` and the XIAO as
`/dev/cu.usbmodem2101`. **The fleet is on 917.4 MHz**, and both simnode boards hold it as
their committed group in NVS, with `phy_trial_s` 120. **`simnode_diag_enable` is 0**, and `deployed` reads 0 and
`poll_interval_s` 60 as overrides on `simnode0` to `simnode2`, so the bridge polls no node
from boot. It reaches
the sandbox broker. The broker still retains the simnode discovery configs from an earlier
enabled run, so Home Assistant shows simnode devices whose entities read unavailable. **This table names the devices in this subproject's terms**; the
range-test handoff owns them in its own roles.

| Device | Called here | Told apart by | Firmware | Current state |
|---|---|---|---|---|
| Heltec V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `firmware/bridge -e heltec`. MAC `44:1b:f6:f9:70:14` | **At its production position in the office, NW wall, desk height.** On USB as `/dev/cu.usbserial-0001`. NVS holds the configuration store — clear a bench value with `{"op":"restore_defaults"}` on its `config/set`, not by reflashing |
| Heltec V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `firmware/simnode -e simnode-heltec`. MAC `44:1b:f6:fa:bc:2c` | In the office, about 1.5 m from the bridge board. On USB as `/dev/cu.usbserial-3` on 2026-09-25. Holds `f0` in `ROLE_RANGE` and `f2` in `ROLE_HEALTH`. Its port name moves across replug |
| XIAO ESP32S3 + **Wio-SX1262 Kit** | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `firmware/simnode -e simnode-xiao-wio`. MAC `68:ee:8f:4b:85:f4` | On USB as **`/dev/cu.usbmodem2101`**. The number moves across replugs, and it is the only `usbmodem` port. Held `f1` in `ROLE_GATELINK` alone on 2026-09-25; `f3` in `ROLE_FAULT` was not present. Add it with `id add f3 ROLE_FAULT` for a fault run |

**The link ran −52 to −48 dBm on 2026-09-21**, about 12 dB weaker than the 2026-09-17
sessions, because the bridge board moved to its production position for the D1 capture and
has not moved back. SNR held at +10 to +12 dB. **Absolute loss rates are not comparable
across those sessions**; a comparison inside one sweep is.

On 2026-09-23 the bridge heard f1, on the XIAO, at −27 to −26 dBm and +11 dB SNR. That
day's figure is not comparable to the 2026-09-21 range until someone confirms which board
the 2026-09-21 range came from. During V-B12's sweeps the bridge heard f3, on the XIAO, at a median
−22 dBm and +11 dB SNR.

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

- **Opening a serial port from pyserial reset all three boards** in the poll-clash run on
  2026-09-24, although an earlier session that day saw no reset. Wait for the bridge's
  `availability` before publishing anything. A reset clears a simnode's identities and its
  `ctx_id`, so **hold every port open for a whole run**; a disabled identity re-enables on
  the next boot.
- **A bench identity is polled only after the bridge has heard it.** `push f1` announces a
  `ROLE_GATELINK` identity; `fault <id> hdr_rsv` announces any role and moves no counter.
- **A command takes 4–9 s from the MQTT publish to the node**, not ~1 s. Five `OPEN`s on
  2026-09-24 took 1.7–2.9 s to `cmd/ack` at 10 s polling. A configuration
  set is slower still: it waits on the poll scheduler and the media access behind it.
- **`sched_task` is the deepest task in this firmware since BF-32**, and it logs its
  high-water mark on every configuration resolution: 1352 bytes free of 5120 on
  2026-09-24, after three PHY changes. Read that number before adding anything to its tick.
- **After a bridge boot, a bench identity refuses commands and `CONFIG` until the bridge
  hears it** (BF-34). `push f1` starts its roll, and `roll: f1 rolled to ctx …` on the
  bridge's serial log says it finished. A board still running pre-BF-34 firmware draws
  `DUPLICATE_CACHED` instead, which is the defect the roll closes.
- **Interleave the arms of any frame-counting sweep**, and give every one a control arm in
  the same session. `tools/simctl/sweep_interleave.py` does both.
- **HA's entity registry remembers every `unique_id`**, and a retained discovery config
  survives a reflash. Develop discovery against the dev HA VM and dev broker until **B6**.
