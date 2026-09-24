# Bridge Node — session handoff

**Written 2026-09-24 by the session that built BF-33 slice 3, the simnode's half of a PHY
change, on the host.** It found that **no PHY change can start** while the bridge watches
GateLink and WellLink, both provisioned and neither deployed, so slice 4's bench run waits on
a decision. The engineering log's slice 3 entry has the evidence.

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
Continue from docs/bridge/HANDOFF.md: B4b, the PHY fleet decision.
Continue from docs/bridge/HANDOFF.md: the vectors_data.h check.
```

| Task | Read |
|---|---|
| **B4b, the PHY fleet decision**, then slice 4 | *The next job*; the engineering log's three *BF-33* entries of 2026-09-24, last first; spec **§12.4.1** step 2 and **§16.5**; `firmware/bridge/src/node_availability.cpp`'s `watched`; `sched_phy_start()` and the refusal in `task_runtime.cpp` |
| **The `vectors_data.h` check** (no board) | *Open*'s last item; spec **§13.2**; `tools/vectors/embed.py`; `ci.yml`'s `checks` job |

**The cleanup the task produced is part of the task**: stale comments and document lines
it made wrong, `TODO(<id>)` markers it closed, rows here it finished, merged branches and
worktrees. **Close the session** by committing, pushing and opening the PR. Merge it once
the operator accepts it. Then rewrite *Start here* and *The next job* for the next task.
Anything out of scope goes in one line under *Open*, not into the session.

## The next job, in one place

**BF-33 is four slices**, split on 2026-09-24:

1. **`lran-config`** — **done**. `Access::Phy`, the bridge's rows, `Store`'s trial copy.
2. **The bridge's fleet machine, spec §12.4.1 and §16.7.5** — **built, flashed, not run.**
   Its boot and its refusal are confirmed on the board.
3. **The simnode's half, spec §12.4.2** — **built on the host, flashed, not run.** One NVS
   blob per board, a retune once every member identity accepts, and every role but
   `ROLE_FAULT` answering the PHY group. A `get_all` on air returned the six rows.
4. **The bench run** against B4b's row in Impl Plan §8: a reboot mid-trial, confirmation
   by a received frame, the fleet moving together, and D33's clamp. **Blocked.**

**Why slice 4 is blocked.** The fleet is every node the bridge watches, and
`AvailabilityWatchdog` watches a production row from boot. 0x01 and 0x02 are therefore
in the fleet and offline, and §12.4.1 step 2 answers every change `phy_fleet_incomplete`.
In production that lasts until WellLink is deployed. **Settle first how a provisioned node
that has never been deployed counts toward the fleet.** It is a spec and registry question,
and the operator's. Three candidates were put on 2026-09-24:

- a registry mark for a node not yet deployed, which leaves the fleet and the polls;
- a fleet of nodes heard this boot, which departs from §12.4.1's "a node the bridge
  polls";
- a bench-only build flag that drops 0x01 and 0x02, which unblocks slice 4 and settles
  nothing for production.

**For the run itself:** set `simnode_diag_enable` to 1. Then make each simnode transmit
once, because the bridge watches a bench node only after hearing it: `push f1` works, and
f0 and f2 answer only polls. **Disable any enabled identity the bridge will not watch**, or
its board never retunes. Set the flag back to 0 afterwards.

**Still owed a board:** `mqtt_task`'s high-water mark, which nothing prints, and
`sched_task`'s during a PHY change. It read 2312 of 5120 bytes free after an ordinary
`CONFIG`.

**The sandbox HA is drivable by API.** The session holds an admin token for it outside the
repository, and HA's `hassio.addon_restart` restarts the broker. The Supervisor REST proxy
refuses a long-lived token. **pyserial's open did not reset the bridge on 2026-09-24**;
toggle RTS to capture its banner.

## Open, and not closable from here

- **A provisioned, undeployed node blocks every PHY change.** See *The next job*.
- **Four spec questions from BF-33 slice 2**, in the engineering log's entry of that name:
  an empty fleet refused `phy_fleet_incomplete`; the bridge's own PHY rows `READ_ONLY`
  without a usable store; no §16.7.5 reason for a failed commit write; and §12.4 step 1's
  backoff "derived from the resulting airtime", which the bridge does not do, because
  `backoff_max_ms` is a lever. Raise all four at the next revision.
- **A `restore_defaults` on the bridge's topic during a PHY trial clears the restart
  marker**, because `Store::restore_defaults()` clears the namespace and rewrites the
  committed group. A restart during that trial is then not reported. Rare.

- **D60 is accepted and the specification's text is owed.** `RESTORE_DEFAULTS` keeps the
  committed PHY group, and a PHY trial's `CONFIG_ACK` carries `APPLIED_NOT_PERSISTED`.
  Spec §8.10 and §12.4.2 say neither yet. Write both at the next revision; Decision
  Register §3.9 has the reasoning.

- **BF-34's heard-first `POLL` went to f1 210 ms before one roll in six**, and that roll's
  first attempt went unanswered. The engineering log's *BF-34 on air* entry has two
  candidate changes. Neither is needed to close BF-34.
- **The bridge's boot banner still says "No discovery yet."** That BF-15 line has been
  wrong since BF-23 built discovery. It is a one-line firmware fix, left for the next flash.
- **`mppt_charge_state` and `mppt_error` show raw VE.Direct codes in HA**, such as `3`, not
  names. Whether discovery maps them to names is BF-24's question, and GateLink's data
  will need it.
- **`node/state` republishes on every frame**, because `uptime_s` is in it and changes
  every poll; `node/health/state` is the same. Publish-on-change never withholds either.
  Whether uptime belongs in the change hash is the operator's call. Impl Plan §6.6.2.
- **A dummy event reaches a node's real event topics** whenever that node has not been
  heard this boot, marked `synthetic: true`. Automations that send email or SMS must filter
  on it before GateLink deploys.
- **Spec §7.3's deduplication triple withholds every follow-up**, because a follow-up
  reuses its first edge's `event_id`. BF-25 adds the follow-up bit to the key, by operator
  decision. Reword §7.3 at the next revision. Impl Plan §6.3.2.
- **PubSubClient 2.8 publishes at QoS 0 only**, and spec §16.3 requires QoS 1 for events.
  Events are tagged QoS 1, and a failed publish holds an event for retry. The fix is D5's
  fallback, espMqttClient, on a branch of its own. Impl Plan §6.3.2.
- **An event raised while the broker is down is lost once the publish queue fills.** State
  fills the 32 slots and the queue refuses the newest message. Impl Plan §5.2.1's per-class
  queue policy is where this belongs.
- **Spec §16.2 names neither `lran/<node>/node/health/state` nor
  `lran/bridge/diag/publish/state`.** Both use §16.1's optional item, as `diag/radio` does.
  Raise them at the next revision.
- **A simnode's `config/state`, `config/ack` and `cmd/ack` are published whatever
  `simnode_diag_enable` says.** Spec §16.6 says bench data goes *"exclusively"* to
  `diag/state` and `availability`, and §16.7 gives every node a `config/*` topic. Whether
  the configuration and command answers are bench data is the specification's question.
  Raise it rather than gating them locally, because gating them would make a bench set
  unanswerable while the flag is clear. Impl Plan §4.2a.1.
- **The IoT network's nearest access point is still pinned to channel 6** from V-B12.
  Whether it stays pinned is the operator's call.
- **A flag set `applied_not_persisted` leaves a stale `online` after a reboot.** The
  bridge comes back with the flag clear, and nothing withdraws the `online` it published.
  Rare, since a normal set persists. Impl Plan §4.2a.1.
- **The state mirror survives a node reboot and nothing invalidates it.** A simnode holds
  its overrides in RAM, so a reboot clears them while `config/state` goes on reporting the
  old values as current. A node with a store (GateLink, microSD, **D49**) keeps them, so
  the bridge cannot tell from the reboot alone — **the answer is a readback when a node's
  `ctx_id` changes**, which deserves its own thought. **It must not fire on a roll**, which
  changes the `ctx_id` and keeps the configuration (spec §10.6 node step 2). Not a
  regression: before BF-32 there was no mirror at all.
- **M22 closed on a bench at one metre, where every frame arrived near −22 dBm.** It rules
  out a gross coexistence failure, not desense near sensitivity. The check that would
  reopen it is R-4.4b's PER at the gate, rising with the bridge's WiFi traffic, once
  GateLink is deployed.
- **Two header citations of the Library Plan are stale, and no check covers them.**
  Firmware Tasks says v0.12 and the Impl Plan says v0.14, against v0.19.
  `spec_citation_version.py` reads only the protocol specification's citations. Read each
  document against the Library Plan's changes, then bump. Extending the check to every
  ``**<Role>:** [`LRAN-…`](…) vX.Y`` header line is the lasting fix, on a branch of its own.
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
- **W17**, a node that misses every confirming frame of a PHY change (spec §12.4.4), has no
  remedy. It closes after GateLink deploys, by operator decision (D59).
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
| 2 | [`engineering-log.md`](./engineering-log.md) | the **2026-09-24 entries**, the three **BF-33** entries last first, then **V-B4 passes**, **B4's acceptance tally** and then **V-B8 in Home Assistant**, first. Then the **twelve 2026-09-23 entries**, last one first: **BF-27's dummy publish**, **BF-25 built**, then **BF-24 built**, then **BF-26 on air**, then **V-B12 measured**, then **V-B12's blaster**, then its deferral, then **BF-34 on air**, then **BF-34 built** and its bench steps, then the configuration lock with the `seq` gap it closes, then BF-23's lever half and its bench run. Then the **2026-09-21 entries** — BF-32's bench session and the interleaved sweep — then 2026-09-20 and 2026-09-19. Entries from 2026-09-10 to 2026-09-16 are in [`engineering-log-2026-09-10_2026-09-16.md`](./engineering-log-2026-09-10_2026-09-16.md) |
| 3 | [`traps.md`](./traps.md) | the section for the work you are about to do |
| 4 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§8.2** B4's tally; **§6.6.2** BF-27's dummy publish; **§6.3.2** BF-25's events; **§6.3.1** BF-24's publication policy; **§4.2a.1** BF-26's bench gate; **§6.2.2** BF-34's context roll; **§4.4.2** BF-23's lever half; **§6.7** BF-32's configuration path and **§6.7.6** its lock; **§8.1** V-B12, **§8.1.1** what the interleaved sweep found and **§8.1.3** V-B12's result; **§6.6.1** BF-27's frame log; **§10.5** the fault catalogue; **§4.4.1** BF-23's discovery |
| 5 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | §8 is B4b, where the work goes next |
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
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**, **B0**, **B3a**, **B3b**. **M6**, **M19**–**M22**, **M24**, **M25**. **D1** and **D33**, register §3.4.1. **D34 amended**; **D35–D58**. **Protocol Spec v0.13** and its citation sweep, merged. **W4**, **W7**, **W9**, **W10**, **W12**. **V-B3**, **V-B9**, **V-B10**, **V-B12**. **BF-2**–**BF-9**, **BF-15**–**BF-22**, **BF-27**'s frame log, **BF-23** both halves, the lever half **confirmed on air**, **BF-32 entire**. **BF-34 confirmed on air** and merged. **BF-24** and **BF-25** built, host-tested and shown at the broker and in HA. **B4 accepted** 2026-09-24, Impl Plan §8.2. **BF-27's dummy publish** built and on air. **BF-26 confirmed on air**, and the bench restored after V-B12. `firmware/chan-capture/`, `lib/lran-link`'s `ChanMonitor`, `lib/lran-config/` |
| Not done | **B4b** (**BF-33**): slices 1 to 3 built, 2 and 3 flashed and not run; slice 4 blocked on the fleet decision. **BF-35**, HA controls for the configuration table, unstarted. **BF-27's** bridge-side simulators and packet loopback. **M26**. **BF-11a**, **BF-11b**. The whole-document style passes |
| Queue | **B4b**, the PHY fleet decision, then BF-33 slice 4 |

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

**The bridge runs BF-27's dummy publish image, `bee046b-dirty`**, flashed on 2026-09-23
before that work was committed. Its banner matches no commit, so reflash it from `main`
before a run that cites the image. It reaches the house broker at <sandbox-broker-ip>.
**Opening its USB serial port resets it**, even with DTR and RTS held low, so hold the port
in one process for a whole console session. **`simnode_diag_enable` is off and persisted
in its NVS.** The broker retains 48 simnode discovery configs from the enabled run, so Home
Assistant carries four simnode devices whose entities read unavailable. The XIAO runs
`98b4b04`; the Heltec simnode was not touched. All three were on USB when this session
ended. **This table names the devices in this subproject's terms**; the range-test handoff
owns them in its own roles.

| Device | Called here | Told apart by | Firmware | Current state |
|---|---|---|---|---|
| Heltec V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `firmware/bridge -e heltec`. MAC `44:1b:f6:f9:70:14` | **At its production position in the office, NW wall, desk height.** On USB as `/dev/cu.usbserial-0001`. NVS holds the configuration store — clear a bench value with `{"op":"restore_defaults"}` on its `config/set`, not by reflashing |
| Heltec V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `firmware/simnode -e simnode-heltec`. MAC `44:1b:f6:fa:bc:2c` | In the office, about 1.5 m from the bridge board. On USB as `/dev/cu.usbserial-4` on 2026-09-23. Its port name moves across replug |
| XIAO ESP32S3 + **Wio-SX1262 Kit** | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `firmware/simnode -e simnode-xiao-wio`. MAC `68:ee:8f:4b:85:f4` | On USB as **`/dev/cu.usbmodem2101`**. The number moves across replugs, and it is the only `usbmodem` port. Holds `f1` in `ROLE_GATELINK` and `f3` in `ROLE_FAULT` in NVS |

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
