# Bridge Node — engineering log

**Dated record, appended to and never rewritten.** Measurements, surprises and the
things that cost an hour. Where this disagrees with a document, the document is the
current statement and this is what was true on the day. Impl Plan §5.3 names this file.

**Earlier entries are in two files beside this one**, moved there unedited. A document
citing an engineering-log entry by date means the file holding that date:

- 2026-09-10 to 2026-09-16, moved 2026-09-18:
  [`engineering-log-2026-09-10_2026-09-16.md`](./engineering-log-2026-09-10_2026-09-16.md)
- 2026-09-17 to 2026-09-23, moved 2026-09-26:
  [`engineering-log-2026-09-17_2026-09-23.md`](./engineering-log-2026-09-17_2026-09-23.md)

[`docs/README.md`](../README.md#conventions) says when the log is split next.

---

## 2026-09-24 — V-B8 in Home Assistant: each event fired once through two restarts and two refreshes

**V-B8 passes on the sandbox.** Three synthetic events each fired a Home Assistant
automation once. None fired again across two HA restarts, a reload of the MQTT integration
and a broker restart that made the bridge republish discovery. The bridge ran the image
from BF-27's run, `bee046b-dirty`, with no reflash. HA was 2026.9.3, driven through its REST
API with a long-lived token.

**The instrument was an automation triggered on `lran/gatelink/event/#`**, in `queued` mode,
writing one logbook row per message with the topic, `event_id`, `ctx_id`, `follow_up` and
`synthetic`. A second row for one event would be the V-B8 failure. The automation was
deleted after the run.

| Step | HA automation fires, total |
|---|---|
| `dummy event gatelink vehicle_while_held_open` | 1 |
| HA restart (`homeassistant.restart`) | 1 |
| MQTT integration reload (`config_entries` reload) | 1 |
| Mosquitto add-on restart; the bridge reconnected and republished its discovery set | 1 |
| The same event as a follow-up, then `fire_asserted` | 3, one row each |
| A second HA restart | 3 |

The last two rows are the positive control: the automation still fired after the refreshes,
so the absence of a replay is not a dead subscription. A retained-only subscription to
`lran/#` afterwards found no `event` topic.

**The broker restart republished discovery.** `on_mqtt_connected()` resets
`g_discovery_cursor` on every connect (`task_runtime.cpp`). The bridge does not subscribe
to `homeassistant/status`. HA's own restart needed nothing from it, because every
discovery config is retained.

**§6.3's rules read the same in HA as at the broker.** A dummy `STATUS` populated all
GateLink entities, and `binary_sensor.gatelink_synthetic_data` read `on`. After
`mppt_flags=2` and `bms_age_s=900`, 20 solar and battery entities went `unavailable`. They
stayed that way through both HA restarts, because the `available: false` documents are
retained. `bms_reading_age` stayed available and read 900, as did `bms_link_rssi`, which is
the reading that says why the rest are stale. Temperatures show in °F: this HA uses the
imperial unit system and converts the declared °C.

**The hand-set `online` was overwritten, and Impl Plan §6.6.2 said it would not be.**
Opening the serial port reset the bridge at 10:04, so the watchdog started over.
`lran/gatelink/availability` was set to `online` by hand at 10:04:38. At 10:06:16, after three missed polls,
the watchdog published its first `offline` transition over it, and HA showed all 53
GateLink entities as `unavailable` when read after the first restart. §6.6.2's rule holds only once
that first transition has happened. Set `online` more than three poll intervals after the
bridge boots, or set it again. The broker restart overwrote it a second time, as §6.6.2
says. §6.6.2 now says both.

**Opening the USB serial port resets the bridge**, even with DTR and RTS held low before
`open()` on macOS. One process held the port for the whole run, fed through a FIFO.

**What this leaves.** V-B8 ran with synthetic events on a bridge at its desk. The transport
is still PubSubClient at QoS 0; the handoff's *Open* keeps that item. B4's §6.3 criterion
has now been shown in HA as well as at the broker.

---

## 2026-09-24 — B4's acceptance tally: the discovery set read at the broker and in HA

**Every retained discovery config names its own node's availability, and HA holds one
device per node.** That read is the only new evidence behind Impl Plan §8.2's tally. The
rest comes from the 2026-09-23 and 2026-09-24 entries above. No board was touched, and the
bridge was still running BF-27's image.

**The read.** A paho subscriber held `homeassistant/#` and `lran/+/availability` at the
sandbox broker for 4 s and kept retained messages only. HA's `/api/template` counted MQTT
entities per device.

| Device | Configs at the broker | Entities in HA |
|---|---|---|
| LoRa Bridge | 6 | 6 |
| GateLink | 53 | 53 |
| WellLink | 8 | 8 |
| Simnode 0 to 3 | 12 each | 12 each |

All 115 configs list `lran/<node>/availability` for the node their `~` names. Of
GateLink's, 33 list only that, 10 add `battery/state` and 10 add `solar/state`, each of
those 20 with `avty_mode: all`. Retained availability read `online` for the bridge and
`offline` for GateLink and WellLink, and no simnode had one.

**The read cannot show V-B4's republish.** Mosquitto persists retained messages across a
restart, so a config present after one says nothing about the bridge. A subscriber held
through the restart would tell them apart: the broker's copies reach it with the retain
flag set, and the bridge's republished ones with it clear.

---

## 2026-09-24 — V-B4 passes: a broker restart brought every production config back from the bridge

**V-B4 passes.** After a Mosquitto restart, the bridge republished all 67 of its production
discovery configs within 5 s of the broker coming back, and the broker's persisted copies
arrived beside them. The bridge ran BF-27's image with no reflash, and nobody opened its
serial port.

**The instrument told the two sources apart by the retain flag.** A paho subscriber on
`homeassistant/#` and `lran/+/availability` reconnected at 1 s intervals and resubscribed on
every connect. The broker delivers its stored copies to a new subscription with the retain
flag set, and forwards a live publish to an existing subscription with it clear. The
subscriber had to resubscribe before the bridge reconnected, or the bridge's configs would
have arrived as stored copies. It did, by 3 s.

**The run**, in seconds from the subscriber's first connect. The restart went through HA's
`hassio.addon_restart` at 10:27:06.

| Time (s) | Event |
|---|---|
| 5.0 | Restart requested |
| 5.1 | The subscriber disconnected |
| 9.2 | The subscriber resubscribed, and 115 configs and three availability topics arrived with retain set |
| 12.3 | The bridge's `lran/bridge/availability online` arrived with retain clear |
| 12.3 to 13.6 | GateLink's 53 configs, retain clear, then both nodes' `offline` |
| 13.6 to 13.8 | WellLink's 8 configs, retain clear |
| 13.8 to 13.9 | The bridge's 6 configs, retain clear |

**The 48 simnode configs arrived with retain set only**, which is the negative control.
`simnode_diag_enable` is off, persisted, since BF-26's run, and the bridge publishes no bench
discovery while it is off (Impl Plan §4.2a). So the flag separates the configs the bridge
sent from the ones the broker kept.

**One more message arrived with retain clear, at 20.4 s**, on a topic under
`homeassistant/` that held no config. The script did not record its name. It is not in the
retained set afterwards, which still holds 115 configs. Home Assistant's MQTT integration
publishes `online` to `homeassistant/status` when it reconnects, and that is the likely
source.

**Afterwards**, HA listed all 115 entities, and 109 read `unavailable`: GateLink's 53 and
WellLink's 8, whose nodes are `offline`, and the 48 simnode entities. The bridge's 6 were
available.

## 2026-09-24 — B4b opened: spec §12.4 leaves the fleet open, and D59 is proposed

**No code was written for BF-33.** The handoff asked for spec §12.4 to be read before the
code, and the read found the mechanism undefined for more than one node. The operator
directed a spec revision first. Spec v0.14's §12.4.1 to §12.4.4 and Decision Register §2.4
(D59) hold the draft.

**One gap would have reverted every node on a working link.** §12.4 step 4 confirms a
change only with an authenticated frame, and §9.2 leaves `POLL` unauthenticated. A bridge
that did nothing but poll on the new settings would see every node revert at
`phy_trial_s`.

**The first fix proposed in this session stranded a node, and it was caught while the text
was being written.** The proposal had the bridge send each node a confirming `CONFIG` `GET`
as soon as it retuned. A node commits on that frame, so a near node could commit while a
far node never heard the new settings. The bridge would then revert, and the near node
would be left on settings nobody used. The draft now splits the step. The bridge first
hears every node on the new settings by `POLL`, which commits nothing, and only then
commits itself and confirms each node. The same order closes a bridge restart mid-trial,
because D58's roll is an authenticated frame sent on the settings the bridge committed.

**One case stays open as W17.** A node heard in the first half that then misses every
confirming `GET` reverts after the bridge has committed.

**Found in passing**: spec §12.4 cited a §12.1a that has never existed, corrected in the
draft. The simnode's `apply_config` still says a large `GET_ALL` has no specified split,
which D57 overtook; that line is under the handoff's *Open*.

## 2026-09-24 — D59 accepted, and the specification moves to v0.14

**The operator accepted D59 as drafted**, and put W17 after GateLink's deployment. The
specification's header moved to v0.14 the same day. The citation sweep moved 26 binding
citations, each document reconciled with v0.14 first. The System PRD's version column had
fallen behind in six rows unrelated to v0.14, and the sweep brought them level.

**W4 gained one vector**, `event_phy_reverted`, for 78 in all. The 77 vectors committed
before it kept their bytes, compared field by field against a copy taken before
regeneration. The codec's `EventType` gained `PhyReverted`, and that forced two switches
to name it: the bridge's `event_type_name()`, which `-Wswitch` would otherwise fail, and
the simnode's console table. The native suites pass: 135 in `lran-protocol`, 17 in the
bridge's `test_events` with one new case, and 115 in the simnode.

## 2026-09-24 — BF-33 split in four, and its library half built

**BF-33 did not fit one session, so the operator split it.** The documents alone took about
half of a 200k-token budget. The four slices are these: `lran-config`'s table and store;
the bridge's §12.4.1 fleet machine with the retune in `lora_task`; the simnode's §12.4.2
half; and the bench run B4b's row asks for. This session built the first slice. No firmware
behaves differently yet, because no `Store` enables the trial. Library Plan §4 v0.19 records
the API.

**Adding the bridge's six PHY rows broke `test_config`.** With 21 global rows, the bridge's
`get_all` answer counts to about 1216 bytes, and `lran/bridge/config/state` to about 1134,
against a `kMaxPayloadLen` of 1024. Both figures come from an offline count at each row's
longest value, not from a board. The operator chose to raise `kMaxPayloadLen` to 1536.
`MQTT_MAX_PACKET_SIZE` was already 2048. The publish queue's 32 slots grow by about 16 KB,
and the `heltec` build reads 195,032 bytes of RAM, 59.5 %. Two stack buffers were sized by
`kMaxPayloadLen`, and both changed. `publish_cmd_ack()` runs on `sched_task` and now uses
128 bytes. The version document built on each broker connect is now static, so `mqtt_task`
does not hold two payloads on its stack at once. **Read both tasks' high-water marks at the
next flash**, because nothing on a board has checked this change.

**Two questions go to the specification.** Neither blocks the next slice.

- **`RESTORE_DEFAULTS` keeps the committed PHY group.** The spec's §8.10 and D52 say it
  clears every override. Applied to the PHY group, it would retune one node to D1's
  defaults while the fleet stayed where it was, which §12.4 exists to prevent. The store
  keeps the group, and the next revision should say so.
- **A node's `CONFIG_ACK` for a PHY trial reads `APPLIED_NOT_PERSISTED`.** That is the
  truth about the trial values, and §12.4.1 step 4 checks only the values. §12.4.2 step 2
  says `APPLIED_NOT_PERSISTED` "does not extend to the PHY group", which was written about
  a node with no store. The two readings should be reconciled in the text.

The native suites pass: 25 in `lran-config` with nine new cases, 387 in the bridge and 115
in the simnode. The `heltec` and `simnode-xiao-wio` targets build, and `run_ci_local.py`
passes.

## 2026-09-24 — BF-33 slice 2: the bridge's fleet machine, host-built

**The bridge's half of spec §12.4.1 is built and host-tested, and it has not run on a
board.** `phy_change.{h,cpp}` is the state machine, with no Arduino dependency, like
`config_path.h`. `sched_task` drives it, and `lora_task` applies the retune. The native
suite is 406 cases, 19 of them new, and the `heltec` build reads 197,264 bytes of RAM,
60.2 %. No change can complete on air yet. Every simnode answers a PHY `SET` `READ_ONLY`
until slice 3 builds §12.4.2, so a change today ends `not_accepted` at the first node.

**Four things the reading found, each of which shaped the code:**

- **The boot restore went through `Store::apply()`**, which now opens a PHY trial. A
  stored group would have come back as a trial at every boot. `nvs_restore()` now calls
  `ConfigStore::restore()`, which calls `Store::restore()`.
- **`ConfigPath` cannot carry the fan-out.** It runs one transaction and publishes a
  per-node `config/ack` when that transaction resolves. §16.7.5 needs one answer on the
  bridge's topic when the whole change ends. `PhyChange` claims its own `CONFIG_ACK`s
  first in `config_on_ack()`, as the roll does in `cmd_on_ack()`.
- **`lora_link` set the PHY once, in `lora_start()`.** `lora_request_phy()` hands new
  settings to `lora_task` under `g_diag_mux`. `lora_task` applies them through
  `radio_begin()` on the first pass that finds the radio receiving, nothing of ours
  arriving and nothing queued to send.
- **The table and `PhyConfig` count in different units.** Bandwidth is whole kHz in the
  table and tenths in `PhyConfig`. `phy_config_from()` converts it and applies D33's EIRP
  check again, and a host test holds the default group equal to `kPhy`.

**Choices a reviewer should check against the specification:**

- **Any authenticated frame confirms a node**, so from the first `CONFIG` to the last
  confirming `GET` no command, roll or other `CONFIG` is admitted
  (`PhyChange::blocks_traffic()`). Polls still go out.
- **An empty fleet is refused `phy_fleet_incomplete`.** §12.4.1 step 2 names only an
  offline node. A bridge that moved alone would strand every node it has not heard.
- **The bridge's PHY rows stay `READ_ONLY` without a usable NVS store**, by the
  reasoning of §12.4.2 step 2, which the specification states for nodes only.
- **A failed commit write has no §16.7.5 reason.** The change reverts, `config/ack`
  reads `reverted`, and no `phy_reverted` event is published.
- **The trial marker lives in the PHY blob**, so the commit that writes the new group
  clears it in the same NVS write. A boot that finds it set publishes
  `phy_reverted` with `reason` `restart` once.
- **After an abandon the machine stays busy until the last node's window has closed**,
  so a second change cannot reach a node still counting down the first. A node that
  never answered step 4 is read back with `GET_ALL` at that point, which is step 4's
  deferred readback.

## 2026-09-24 — BF-33 slice 3: the simnode takes a PHY change, and no change can start

**The simnode's half of spec §12.4.2 is built and host-tested, and none of it has moved a
radio.** `phy_trial.{h,cpp}` holds the board's PHY group in `lran-config`'s `Store`, which
tells the radio when to retune and runs the trial window. `nvs_blob.cpp` keeps the group in
NVS. The native suite has 128 cases, 13 of them new. Both simnode targets and the bridge
were flashed. The bench then showed that **the bridge refuses every PHY change**, so
nothing reached a simnode on air. The last section below explains why.

**The simnode did not do what slice 2's entry says it did.** That entry says every simnode
answers a PHY `SET` `READ_ONLY`. In fact only `ROLE_GATELINK` answered `CONFIG`, and its
generic RAM store took a PHY id as an ordinary parameter. It answered `OK` and
`APPLIED_NOT_PERSISTED` and never retuned. The bridge would have gone on to step 5 and
retuned alone, then reverted when step 6 heard nobody.

**The operator decided three questions before the build:**

- **The simnode persists the PHY group in NVS**, one blob per board, in the bridge's
  layout. `PhyBlob` moved from the bridge into `lran-config` (`phy_blob.h`), so both
  firmwares write one format. `phy reset` erases the blob and retunes to D1's group, so a
  stranded board recovers without a reflash. Nothing else on a simnode persists.
- **A board retunes once every member identity has accepted the same group.** Up to four
  identities share one SX1262, and the bridge sends each its own `SET` on the old settings.
  A board that retuned after its first identity's ACK would not hear the next `SET`. A
  member is any enabled identity whose role is not `ROLE_FAULT`. So an enabled identity the
  bridge does not watch keeps the board on its old settings, and the change ends
  `not_heard`. Any authenticated frame to any identity confirms the whole board.
- **`ROLE_RANGE` and `ROLE_HEALTH` answer `CONFIG`** for the PHY group, and answer any other
  row `UNKNOWN_PARAM`. `ROLE_FAULT` still answers nothing.

**Choices a reviewer should check against the specification:**

- **A `SET` naming PHY rows during a trial answers `READ_ONLY`.** §12.4.2 does not say what
  a node does with a second group before the first is confirmed. The bridge never sends one
  (`blocks_traffic()`), and stacking one trial on another would leave nothing coherent to
  revert to.
- **A group some members accepted, but the board never retuned to, is dropped after
  `phy_trial_s`.** The radio never moved, so there is no `PHY_REVERTED` to send.
- **Confirmation is a `CONFIG` or `COMMAND` whose gate verdict is `Execute`, or a roll.** A
  roll skips the gate, and §12.4.1 counts the roll after a bridge restart as confirmation.
  A `DUPLICATE_CACHED` answer does not confirm.
- **`PHY_REVERTED` goes out with the next frame from the bridge, of any type**, before that
  frame's own answer, and from every `ROLE_GATELINK` identity. The other roles have no
  event schema and report nothing, as step 8 allows.
- **§12.3's backoff window is not recomputed on a retune**, as on the bridge: `backoff_max_ms`
  is a lever. This is the fourth spec question from slice 2's entry.
- **A generic-store `GET_ALL` now carries six more rows**, so a `ROLE_GATELINK` identity holding
  more than 16 `u32` overrides cuts its answer: the six rows cost 42 of 196 bytes. The cut is logged. §7.4.1's split is not
  built here.

**What slice 2 owed a board, read on this flash:**

- **The bridge's radio comes up on 917.4 MHz** from `g_boot_phy`: `LoRa: radio up -
  917400000 Hz, SF9, BW 125.0 kHz, CR 4/5, -4 dBm conducted`. `lora_task` has 6428 bytes
  free.
- **`sched_task` has 2312 bytes free**, of 5120, after a `get_all` to simnode1 completed.
  No PHY change ran, so `sched_phy()`'s own depth is not in that figure.
- **`mqtt_task`'s high-water mark is not read.** Nothing prints it.
- **The refusal is right on the broker.** `{"set":{"freq_hz":917000000}}` on
  `lran/bridge/config/set` answered `{"op":"set","persist":"not_applied","error":"phy_fleet_incomplete"}`.

**The simnode's `CONFIG` path works on air.** A `get_all` on `lran/simnode1/config/set`
drew six results from f1, `persisted`, and `config/state` filled in the PHY rows.

**No PHY change can start, on this bench or in production, until every provisioned
production node answers.** The fleet is every node the bridge watches.
`AvailabilityWatchdog` watches a production row always and a bench row once heard. So
0x01 and 0x02 are in the fleet from boot, both offline, and §12.4.1 step 2 refuses with
`phy_fleet_incomplete`. That holds until WellLink is deployed, which is not planned soon. The
refusal follows the text: the bridge polls both nodes. **Whether a provisioned node that
has never been deployed counts toward the fleet is a spec and registry question.** The
operator closed slice 3 host-only, and slice 4 cannot start without an answer.
`simnode_diag_enable` was set to 1 for the check and back to 0 afterwards.

## 2026-09-24 — D61: a node joins the PHY fleet once deployed or heard, and slice 4 is unblocked

**The operator chose a registry mark over the other two candidates**, and D61 closed the
same day as a bridge per-node lever, `deployed` (`0x0081`, default 0). Decision Register
§2.5 has the proposal and §3.10 the answers. The specification's §16.5 and §16.6 are to
say it at the next revision, with D60's two cases.

**What changed on the bridge.** The poll scheduler and the availability watchdog no longer
enrol a production row at construction. `sched_levers()` enrols a row the tick its
`deployed` lever reads 1, through `PollScheduler::enrol()`. Every other row, production or
bench, is enrolled on its first frame, as a bench row always was. The watchdog takes the
lever in place of `NodeInfo::is_bench`. Both latch: clearing the lever takes effect at the
next restart, so a node cannot leave §12.4.1's fleet mid-boot while a change might still
move it.

**Two choices a reviewer should check:**

- **The lever applies to bench rows too.** D61 names no exception, and a bench identity with
  `deployed` set is polled from boot. That is harmless and sometimes useful.
- **`deployed` is a `bool`, not the `u8` §2.5 first drafted.** `simnode_diag_enable` set the
  table's convention for a 0-or-1 row. The register's item 1 was corrected on the branch
  before merge.

**What it costs on a bench.** With every lever at 0 and no simnode transmitting, the fleet
is empty and a PHY change is still refused `phy_fleet_incomplete`, as slice 2 chose. Make
each simnode transmit once, or set its `deployed` lever.

**Not run on a board.** The bridge's native suite is 409 cases, all passing, and `heltec`
builds. Nothing was flashed.

## 2026-09-24 — BF-33 slice 4 on air: B4b's criteria hold, and a poll can clash with a PHY `CONFIG`

**Every criterion in B4b's row held on the bench.** The bridge was flashed with D61
(`2066fc9`), and both simnodes ran `0a0d6c9`, slice 3's build. Three identities made the
fleet: f0 and f2 on the Heltec, and f1 on the XIAO. Each change moved `freq_hz` between
917.4 and 917.0 MHz. Every `CONFIG` carried all six PHY rows. `phy_trial_s`,
`config_ack_timeout_ms` and `poll_reply_timeout_ms` kept their defaults of 120 s, 8 s and
10 s. The bench was set up this way:

- `simnode_diag_enable` was 1, and `deployed` was 1 on `simnode0` to `simnode2`. Both were
  set back to 0 afterwards.
- **D61 held on air.** After the flash, the bridge sent no boot poll to 0x01 or 0x02. Each
  `deployed` set enrolled its node within a second. After each later bridge reboot, the
  bridge polled all three bench nodes at once.

| Run | What was done | What happened |
|---|---|---|
| D33's clamp | `{"tx_power_dbm":10}` | The set was answered `clamped` at −4 dBm, the current value, so no trial opened and no `CONFIG` went out |
| The fleet moves | 917.4 → 917.0, then back | Each run passed §12.4.1 steps 3 to 7: the fan-out took 3 s, the bridge heard all three nodes 5 s after it retuned, and then it committed. Each board committed on its first `GET`. A Heltec reset after the second run's commit came back on the committed group |
| A node reboots in its trial | The Heltec was reset as soon as it retuned | The Heltec came back on 917.4, `REVERTED (reboot during trial)`. The bridge polled f0 and f2 on 917.0 and heard neither, so it committed nothing. It reverted 96 s after the first `CONFIG_ACK`, which is step 8's deadline for three nodes, and published `phy_reverted` `not_heard`. f1 answered polls on 917.0 and still reverted when its 120 s window closed, because a `POLL` confirms nothing. It then sent `PHY_REVERTED` detail `0x0001` in the first frame after the revert |
| The bridge reboots before its commit | The bridge was reset 150 ms after it retuned | The bridge came back on 917.4 and published `phy_reverted` `restart`. All three nodes reverted when their windows closed, and f1 sent `PHY_REVERTED` |
| The bridge reboots after its commit | The bridge was reset 150 ms after its commit, before any step 7 `GET` | The bridge came back on 917.0. Its §10.6 roll reached each node inside its window, and both boards committed 917.0 with no `GET` sent |
| Back to Envelope A | 917.0 → 917.4 | The change committed on the bridge and on both boards |

**A node's `PHY_REVERTED` from a bench identity never reaches the broker.** Spec §16.6
withholds bench data from `event/` topics, and the bridge counted every such frame in
`bench_withheld`. The bridge's own `phy_reverted` did publish, and §12.4.2 step 8 names it
as what reaches Home Assistant. GateLink's event will be the first to publish.

**A poll and a PHY `CONFIG` went to f2 211 ms apart, and the change was abandoned.** In
one tick, `sched_polls()` sent f2 its `POLL`, and then `sched_phy()` sent f2 its `SET`.
The bridge received no answer to either frame, and f2 logged no `CONFIG`. After
`config_ack_timeout_ms`, the change was abandoned `not_accepted`, which is §12.4.1 step 4.
f0 and f1 had already accepted. f1's board retuned alone and reverted at 120 s. The Heltec
never retuned, because f2 had not accepted. The bridge read f2 back with `GET_ALL` once
`phy_trial_s` had passed. The abandon worked as specified. The cause is that nothing in
`sched_task` stops a second frame from going to a node whose poll answer is still due. The
*BF-34 on air* entry saw the same 210 ms gap before a roll. By operator decision, the fix
goes on a branch of its own. The later runs here were started at 36 s past the minute,
between poll cycles.

**Three smaller findings:**

- **The `config/ack` for a committed change can be lost.** The reset 150 ms after the
  commit beat `mqtt_task` to the broker, so that set got no answer. The `config/state`
  published after the reboot showed the new value.
- **`sched_task` had 1352 of 5120 bytes free** when the deferred readback completed. That
  figure includes two completed PHY changes and one abandoned change, against 2312 after an
  ordinary `CONFIG`. `lora_task`'s lowest figure was 6168 bytes. `mqtt_task`'s is still
  unread, because nothing prints it.
- **The bridge's `event_id` for `phy_reverted` restarts at 1 after a reboot**, so ids 1 and
  2 were followed by id 1. Spec §7.3's deduplication covers node events, not the bridge's
  topic, so this breaks no rule. An automation keyed on `event_id` alone would miss the
  repeat.

## 2026-09-24 — The poll clash fixed: one exchange on the air at a time, and the bench shows the clash before and none after

**On the bench, the old image sent five frames while a `POLL`'s answer was still due, and
the fixed image sent none.** The fix is `air_turn.h` (`6a8b76d`). An outstanding scheduled
`POLL` holds a command, a roll, a `CONFIG` and the start of a PHY change. In turn, no
scheduled `POLL` starts while one of those waits for its answer, while a PHY change blocks
traffic, or while a command or configuration job waits in its queue. The operator chose this
over the narrower fix, which held other frames behind a poll but let polls run on. Under the
narrower fix, a missed poll could hold a step-7 `GET` for 10 s, more than step 8's 8 s
reserve for that node. A `POLL` could also still follow a `COMMAND` whose ACK was due. The
full trace is [`data/poll-clash-bench-2026-09-24.log`](./data/poll-clash-bench-2026-09-24.log).

**The bench.** The control arm ran on the image the bridge already held, `2066fc9`. The fix
arm ran on `30c295f`, flashed from a clean tree; its firmware is `6a8b76d`'s. The fleet was
f0 and f2 on the Heltec, and f1 on the XIAO. `poll_interval_s` was 10 on `simnode0` to
`simnode2`, which put a `POLL` on the air about every 3 s. The harness published each fix-arm
change 40–50 ms after the bridge's frame log showed a `POLL` going out. Each change then
started only once that `POLL` was answered, 0.4–2.1 s later.

**How a clash was counted.** The bridge's serial frame log (BF-27) records every frame it
sends and receives, on its own millisecond clock. A `POLL` to a node counts as open from its
`tx` record until the next `rx` from that node, or for 10 s. Every other `tx` inside that
interval is a frame sent while an answer was due.

| | Control, `2066fc9` | Fix, `30c295f` |
|---|---|---|
| `POLL`s sent | 95 | 77 |
| Command, roll or `CONFIG` sent while a `POLL` was open | **5**: three rolls and two step-7 `GET`s, each 229 ms after a `POLL` to another node | **0** |
| A scheduled `POLL` beside a step-6 `POLL` to one node | once, to f0, 229 ms apart | 0 |
| Rolls after the boot | f0 took 1 attempt; f2 and f1 took 2, each retried 3003 ms later, after a timeout | f0 and f2 took 1 attempt; f1 took 2 (see below) |
| PHY changes committed | 1 of 1 | **5 of 5**, each started beside a `POLL` |
| `CONFIG` frames sent | 7 for one change: f2's `GET` went unanswered and was sent again 7.4 s later | 30 for five changes, six each, none sent again |
| From the set to the commit | 14.3 s | 12.8–20.5 s |

**Five `OPEN`s went to f1 under 10 s polling on the fixed image.** Each was acknowledged at
the first attempt, 1.7–2.9 s after the publish on `lran/simnode1/cmd/open/set`, measured at the
broker. None waited out a missed poll, because no poll was missed.

**Two frames can still go out while an answer is due, and neither is this fix's to close:**

- **A PHY change's step-6 `POLL`s go 229 ms apart to different nodes**, in two of the five
  changes. They belong to the change itself, which `air_turn.h` leaves alone. Both changes
  committed.
- **f1's roll after the fix arm's reflash took two attempts with no `POLL` open.** The
  bridge heard f1's `ACCEPTED` at 14815 ms and sent the retry 52 ms later, 527 ms after the
  first attempt went on air. f1 answered it `REJECTED_CTX`, which completed the roll (spec
  §10.6 step 4). A 3000 ms window ending at 14867 ms would have opened at about 11867 ms. That
  fits a window opened when `sched_task` queued the frame, and a frame that then waited about
  2.5 s for media access. The trace does not show when the frame was queued, so this is not
  shown.

**Opening the three ports reset all three boards**, `rst:0x1 (POWERON)` on both Heltecs and
`USB_UART_CHIP_RESET` on the XIAO. The handoff said that pyserial reset none of them earlier
the same day. The first attempt published its setup while the bridge was booting, so only
`simnode2`'s set arrived and a one-node change ran. That attempt was stopped. The run above
waited for the bridge's `availability` before its setup.

**The control arm's return change was refused `phy_change_in_progress`**, because the
harness sent it 15 s after the commit, while the step-7 `GET`s were still running. That is
the harness's timing, not a defect. The fix arm's first change therefore went from 917.0 MHz
back to 917.4 MHz.

**The bench was left as it was found.** The fleet is committed on 917.4 MHz.
`simnode_diag_enable` is 0. `deployed` is 0 and `poll_interval_s` is 60 on `simnode0` to
`simnode2`, both as overrides. `lora_task` read 6344 bytes free at its lowest. No
configuration resolution ran on a node's own topic, so `sched_task` printed no high-water
figure.

## 2026-09-25 — BF-35: the configuration table's controls, in the sandbox HA

**Home Assistant registered every configuration entity the bridge published, and a write
from HA came back through the bridge's `config/state`.** The bridge was flashed with
the BF-35 change, before its commit, over USB on `/dev/cu.usbserial-0001`. esptool read MAC
`44:1b:f6:f9:70:14`, the bridge board's. The image uses 60.2 % of RAM and 28.1 % of flash.
The host suite passed 423 of 423.

**Before anything was published, the operator chose the names and the layout**: the table
name as the `object_id`, the bridge's PHY rows as box-mode controls, each node's PHY rows as
sensors, and the bridge's per-node rows on the bridge's availability. Impl Plan §4.4.3 has
the reasons.

**What HA registered, read over its REST API about 20 s after the boot:**

| Device | Entities | State |
|---|---|---|
| LoRa Bridge | 19 `number`, 1 `select`, 1 `switch` (`simnode_diag_enable`) | Every value the table's default. `tx_power_dbm` shows `-4.0`, because HA renders a number as a float |
| GateLink, WellLink | `poll_interval_s` 60, `deployed` off | Available, on the bridge's availability |
| GateLink, WellLink | 4 node-common `number`s, 6 PHY `sensor`s | `unavailable`, because neither node is online. R-3.3d, as intended |

**Three writes went through HA's services and came back through the bridge**, and each was
restored afterwards. `number.lora_bridge_diag_interval_s` went to 120 and back to 60.
`switch.lora_bridge_simnode_diag_enable` went on and back off.
`number.gatelink_poll_interval_s` went to 90 and back to 60. Every read came from the state
HA took from `config/state`, since an MQTT `number` or `switch` with a state topic is not
optimistic. No PHY row was written, because a write there starts a change across the fleet.
`test_discovery` covers the `select`'s template instead, by parsing the rendered command
through the bridge's own parser.

**What this left behind.** The bridge runs the BF-35 image, and every lever holds the value
it held before. The sandbox registry now has 45 new entities, all under `lran_bridge_`,
`lran_gatelink_` and `lran_welllink_`. The bench nodes got none.

**One gap this run exposed, not closed.** The `select` limits HA to 125, 250 and 500 kHz,
but `lran/bridge/config/set` still takes any `bandwidth_khz` from 125 to 500, 300 among them.
The table has a range, and the SX1262 has discrete points. Recorded under the handoff's
*Open*.

## 2026-09-25 — `spec_citation_version.py` reads role header lines, and finds six stale citations

**The check now reads every versioned `**<Role>:**` header line** that links an `LRAN-`
document, and compares the version after the link with that document's own **Version:**
header. Before this, it read citations of the protocol specification only. The number of
citations it checks went from 26 to 32, and every one of the 26 is still checked.

**The handoff expected two stale citations and the check found six.** Firmware Tasks
cites the Bridge PRD at v0.14 (now v0.15), the Impl Plan at v0.54 (now v0.58) and the
Library Plan at v0.12 (now v0.19). The Impl Plan cites the Bridge PRD at v0.14 (now v0.15)
and the Library Plan at v0.14 (now v0.19). The GateLink Impl Plan cites the GateLink PRD at
v0.9 (now v0.10). Each needs its document read against the intervening revisions before
the number moves, so none was bumped here.

**`KNOWN_STALE` holds the six, so `main` stays green.** An entry is keyed by citing file
and cited document, and holds the version cited. The check fails on a stale citation not
in the table, and on an entry whose citation has moved. The second failure is the one
that stops the table from outliving the debt. Both failures were exercised by editing a
citation and running the check.

## 2026-09-25 — The six stale role header citations reconciled

**Only Firmware Tasks needed changes to its body.** Each citing document was read against
the cited document's changelog since the version it cited. `KNOWN_STALE` is now empty.

| Citing document | Cited document, from → to | What the reading found |
|---|---|---|
| Firmware Tasks | Bridge PRD v0.14 → v0.15 | Nothing. v0.15 is D59 with no requirement change, and BF-33's row already cites D59 |
| Firmware Tasks | Impl Plan v0.54 → v0.59 | §8 did not say B4b was accepted (v0.56), and nothing named D61's `deployed` lever (v0.55) or the poll-clash fix (v0.57). §8 now does. v0.58's BF-35 was already in its row |
| Firmware Tasks | Library Plan v0.12 → v0.19 | BF-33's row still said the PHY rows answer `READ_ONLY` until it lands. Library Plan v0.19 built BF-33's library half, so the row now says it is built. v0.13–v0.18 were already in BF-32's, BF-34's and BF-24's rows |
| Impl Plan | Bridge PRD v0.14 → v0.15 | Nothing. The plan took D59 in its own v0.54 |
| Impl Plan | Library Plan v0.14 → v0.19 | Nothing. §6.2.2, §6.3.1 and B4b's row already describe what BF-34, BF-24 and BF-33 built |
| GateLink Impl Plan | GateLink PRD v0.9 → v0.10 | Nothing. The same D59 sweep wrote both, and §6.4 has the PRD's three points. **W17** asks nothing of the build |

**Four of the six were number-only drift.** A document took a revision's content and
never moved the header number. The check catches the number and cannot tell which kind of
drift it has found, so each one still needs reading.

## 2026-09-25 — The pre-GateLink survey: B5 has no simulator, and two event gaps had no task

**The survey sorted the handoff's open list by one question: can the bridge board, the two
simnodes and the sandbox HA close it before GateLink exists?** Most items can. The handoff
now keeps them in *Work before GateLink*, and keeps the rest in *Waits on GateLink or the
operator*.

**B5 cannot run on the bench yet.** Impl Plan §2.1 lists every milestone from B2 to B5 as
reachable on two boards, and §8 lets B5 use *"a real MPPT reachable via GateLink or a
simulator"*. The simnode has no such simulator. §10.2's `ROLE_GATELINK` answers `POLL`,
`COMMAND` and `CONFIG`, and nothing in `firmware/simnode/` handles `HEX_REQ`. Firmware
Tasks v0.46 adds **BF-36** for the responder. §10.2 is corrected in BF-36's commit, when
the claim becomes true, not here.

**Two event-delivery gaps had no task row.** The QoS 0 transport (Impl Plan §6.3.2) and
the queue that refuses a new event while the broker is down (§5.2.1) were open in the
handoff since BF-25. §5.2.1 gave the per-class refinement to BF-24 and BF-25, and neither
built it. They are now **BF-37** and **BF-38**. Both matter before GateLink deploys,
because its events drive email and SMS.

**The specification's owed text is in one list for the first time.** Items waiting on the
next revision were spread across the handoff, the Decision Register (§3.9, §3.10) and the
Library Plan (§4). The handoff's *Specification v0.15* table collects ten, and marks which
need only text and which need an operator decision first.

**Two stale lines surfaced and were fixed.** Firmware Tasks' BF-13 row said V-B9 had not
run, and BF-14's said the status page had not been seen. Both happened at B2's bench
session on 2026-09-13, in this log's earlier file. Two more are listed rather than fixed:
`firmware/bridge/CLAUDE.md` cites three documents at old versions in prose that
`spec_citation_version.py` does not read, and System PRD §12 gives the register's range as
D1–D58.

## 2026-09-25 — Spec v0.15 accepted, and the citations moved with the header

**The operator accepted spec v0.15 on 2026-09-25**, with D62–D69 as the register records
them. Bumping the header alone failed `spec_citation_version.py` at 26 citations, so the
bump waited for the sweep and merged with it. Each citing document was read against v0.15
before its citation moved.

**Two documents disagreed with v0.15, not only with its number.**

- **GateLink PRD R-5.3e** said a restore-defaults *"SHALL clear all overrides"*. D60 keeps
  the committed PHY group, so the requirement now says so, and names D68's `OVERRIDE` bit
  as the marking it asks for.
- **Bridge Impl Plan §4.2a, §4.4.3, §6.3.2 and §6.6.1** each carried a question for the
  specification. v0.15 answers all four, so each now records the answer, and §6.6.1 the
  rename D66 owes.

**The two stale citations the pre-GateLink survey entry listed are fixed**: `firmware/bridge/CLAUDE.md`
now cites the Bridge PRD, Impl Plan and Firmware Tasks at their current versions, and
System PRD §12 gives the register's range as D1–D69.

**The W4 vectors were regenerated, and every vector kept its bytes.** Only each file's
`spec` field changed. No vector exercises D68's `OVERRIDE` bit or D64's `INVALID_VALUE`
yet; both come with the `lran-protocol` change, which the handoff lists with the rest of
the code v0.15 owes.

## 2026-09-25 — The code spec v0.15 owed, built on host

**Every code line the handoff's group 1 listed is built, and none of it has run on air.**
The libraries, the simnode and the bridge pass their native suites (137, 29, 130 and 428
cases), and the `heltec` target builds. Library Plan v0.21 and Impl Plan v0.61 (§6.7.2a)
say what changed.

**A round trip does not witness the `OVERRIDE` bit.** A codec that read `status` whole
would decode `0x80` as an unknown `ParamStatus` and write the same byte back, so
`test_vectors`' new schema round trip passes it. The two new vectors are therefore also
checked by name, field by field. The round trip catches the other defect: a codec that
drops bit 7.

**After the first committed PHY change, every PHY row reads `override`.**
`commit_phy_trial()` writes the whole group, rows the set did not name included, so each
becomes a held override. D68 says an override equal to its default is still an override,
so the marking is honest. It will surprise an operator who changed only `spreading_factor`
and then sees `freq_hz` marked. Recorded rather than changed: the fix belongs to the store's
design, not to this marking.

**Two gaps found while wiring D69, both out of scope:**

- **No simnode sends `CONFIG_CHANGE` on its own.** Spec §8.7 has a node send it after a PHY
  revert. The bridge path can be exercised today only by setting the reason by hand.
- **The readback mirror skips a `CLAMPED` result**, though it carries the effective value
  (spec §7.4). `note_set_results()` records `OK` alone, so a clamped set leaves
  `config/state` showing the value from before the set. This predates v0.15.

## 2026-09-25 — Spec v0.15's code on air: four checks pass, and the bench found a wrong `persist`

**All four bench checks the handoff listed pass, after one bridge fix.** The bridge on
`/dev/cu.usbserial-0001` (MAC `44:1b:f6:f9:70:14`) first ran `5176d1a`. The simnode Heltec
on `/dev/cu.usbserial-3` (MAC `44:1b:f6:fa:bc:2c`) holds f0 `ROLE_RANGE` and f2
`ROLE_HEALTH`. The XIAO on `/dev/cu.usbmodem2101` (MAC `68:ee:8f:4b:85:f4`) holds f1
`ROLE_GATELINK`. Every board was identified by MAC before it was flashed. A paho client
recorded `lran/#` with UTC timestamps; the times below come from it.

**D68 — `source` follows the node's bit, not the value.** On the bridge's topic,
`cmd_retries` set to 3, its default, reads `override`. On f1, the PHY rows first read
`override` at Envelope A's values, because the XIAO's NVS held a committed group from an
earlier change. After `phy reset` and a reboot, the same values read `default` (13:34:06).
**The simnode's `phy reset` does not clear the marking until a reboot.**
`PhyTrial::reset_to_defaults()` writes each default through `Store::restore()`, which
holds it as an override, so a readback between the reset and the next boot still reads
`override`.

**D64 — a bandwidth of 300 is refused, and the ack's `persist` was wrong.** On the bridge's
topic the entry read `invalid_value` with value 125, but `persist` read `persisted`
(13:34:16). Spec §8.11 says `NOT_APPLIED` when every entry was refused. The unchanged-group
path in `handle_config_set()` set `persisted` for every PHY-only set, on the reasoning that
the group in force is the committed one. That holds for a row equal to the group, and not
for a refused one. `phy_unchanged_persist()` now decides it, host-tested, and the reflashed
bridge answered `not_applied` (13:36:52). With `phy_trial_s` 121 in the same set, the
bandwidth read `invalid_value` and the change committed with `persist` `persisted`
(13:37:12).

**On a node's topic the answer is `read_only`, and that is correct.** Spec §16.7.1 answers
any PHY row named on `lran/<node>/config/set` `read_only` with the last value read back,
so a bandwidth of 300 there never reaches D64's check. The handoff expected
`invalid_value` on both topics; the expectation was wrong, not the code.

**D67 — `phy_reverted` carries `boot`.** The first flash booted as 1 and the reflash as 2.
An SF 10 change, with the XIAO rebooted 1.5 s after the set and before it accepted, gave
`{"boot":2,"event_id":1,"reason":"not_accepted","node":"simnode1"}` (13:37:37).

**D66 — the frame log is on its new leaf.** `lran/bridge/diag/rxlog/log` carried 82
publications by 13:38, none retained, and `rxlog/state` carried none.
`tools/simctl/rxlog.py --seconds 90` read 12 records, two `STATUS` per bench node, with no
losses.

**D69 ran end to end once the simnode sent `CONFIG_CHANGE`.** The simnode now owes one
after its own PHY revert and carries it in the next poll's answer (spec §8.7). An SF 10 set
at 13:43:06 put the XIAO into its trial, and a reboot at 13:43:08 reverted it with detail
`0x0002`. The bridge abandoned the change at 13:44:44 (`reason` `not_heard`, `event_id` 2).
f1's next poll answer at 13:44:56 carried `PHY_REVERTED` and a `STATUS`. The bridge's next
poll to f1, at 13:45:06, drew a `CONFIG_ACK` (schema `0x12`), and `simnode1/config/state`
was republished at 13:45:08 with no `config/set` and no `config/ack`. The frame log does
not record `status_reason`, so the `CONFIG_CHANGE` itself is inferred: nothing else in that
window starts a readback.

**The `CLAMPED` mirror fix is host-tested only.** `note_set_results()` now mirrors `OK`,
`CLAMPED` and `INVALID_VALUE`, the three results spec §8.12 says carry the effective value.
No simnode node-held row clamps on the bench: f1's readback carries its PHY rows and the
bridge's per-node rows alone, so the fix waits for GateLink's table to reach the air.

**Opening either simnode's port appeared to reboot the board**, although `con.py` set DTR
and RTS low before `open()`, as `simctl.py` does. The boot banner followed each open, and
f1's `ctx_id` changed each time. Every reboot is a new context and a context roll at the
bridge, so a bench step that opens a console mid-scenario is also a reboot.

Suites: bridge 430 of 430, simnode 130 of 130; the `heltec` target builds. The run left
`simnode_diag_enable` and each bench row's `deployed` cleared, and `phy_trial_s` back at
120.

## 2026-09-25 — events at QoS 1 on espMqttClient, from a queue of their own (BF-37, BF-38)

**The bridge publishes events at QoS 1 now, and state can no longer take an event's queue
slot.** BF-37 replaced PubSubClient 2.8 with espMqttClient 1.7.3, D5's designated fallback.
BF-38 gave events a queue of their own. Impl Plan §4.3.3 records the choices. The bridge
board ran `e08f4b1`, flashed from a clean tree.

**What the bench showed**, on the sandbox broker:

| Check | Result |
|---|---|
| A dummy `vehicle_while_held_open`, read by a subscriber at QoS 1 | Arrived at QoS 1, not retained. The broker delivers at the lower of the two QoS values, so the bridge published at QoS 1 |
| The Mosquitto add-on restarted; three dummy `STATUS` frames and a `fire_asserted` with its follow-up raised during the outage | Both events arrived within 0.1 s of the bridge's `online`, each once |
| `lran/bridge/diag/radio/state` afterwards | `q_event_high_water` 2 and `q_event_dropped` 0, beside the six older queues |
| Heap, printed by the bridge on the reconnect | 72,580 bytes free, 60,188 at the lowest |

**The run did not force a QoS 1 retransmission.** An event the broker has not acknowledged
when the connection drops is sent again after the CONNACK. That behaviour comes from reading
espMqttClient's `_clearQueue()` and `_onConnack()`, and no bench run has exercised it. The
events in the outage row waited in the bridge's own event queue, which BF-38 built, and
never reached the library before the reconnect.

**Static RAM rose about 42 KB**, from 197,328 bytes on `main` to 239,260. The event queue
takes 13 KB and the library's packet pool about 25 KB. There is no heap figure from before
the change to compare with, because nothing printed one.

**Three things in the library cost the most reading:**

- **By default, espMqttClient allocates each outgoing packet on the heap.** Root rule 3
  forbids that. `EMC_USE_MEMPOOL` switches it to a static pool, and nothing in its README
  says so. With a pool, the library's `publish()` queues a packet and writes it in `loop()`,
  so the drain now waits for `pending()` to fall below eight.
- **An inbound payload arrives in pieces cut at the library's read buffer**, each with its
  offset and the total. A `config/set` cut in two would have been parsed as two broken
  ones. `InboundAssembler` rebuilds it, and `test_net` covers the cut points.
- **Its `library.json` lists AsyncTCP, which is LGPL-3.0, as a dependency on every ESP32
  build.** PlatformIO compiles it, and the linker map shows no AsyncTCP member in the image.
  `THIRD_PARTY_NOTICES.md` has the `grep` that would show otherwise.

**The synthetic filter for Home Assistant** is `ha/automations/lran_event_notify.yaml`. It
was loaded into the sandbox HA as committed. A dummy event from the bridge reached the
broker and did not trigger it. A hand-published event marked `synthetic: false` did, which
left one persistent notification in the sandbox. The automation was deleted after the run.

Suites: bridge 436 of 436; the `heltec` target builds.

## 2026-09-25 — B5's HEX code against osh-labs/VE.Direct_mppt_arduino: one register moved

B5 built `lib/vedirect/`, `charge_readback` and `sim_mppt` from Victron's "BlueSolar HEX
protocol" PDF. `osh-labs/VE.Direct_mppt_arduino` is now the VE.Direct reference of record
(GateLink Impl Plan §4.2.4), so the code was compared against the library's
`src/VeDirectHexProtocol.{h,cpp}` and `src/VeDirectRegisters.h` at its `main`. That
register file says it was itself verified against the same PDF, Rev 18.

**Framing agrees throughout.** The command and response nibbles, the Get/Set reply flags
(`0x01`, `0x02`, `0x04`), the `0x55` checksum, little-endian register and value, and
uppercase output all match.

**One register disagreed, and it moved.** The bridge read "System voltage setting" at
`0xEDEF`, and `sim_mppt` held it there. The library does not name `0xEDEF`; its
`SYSTEM_VOLTAGE` is `0xEDEA`, un8, volts. Both now use `0xEDEA`. The HA `object_id`,
`charge_system_voltage_v`, is unchanged. Which register carries the configured setting on
the MPPT 75/15 is still unobserved; B6's readback against the real MPPT confirms it.

**The other nine charge registers match** in ID, width, sign and scale: `0xEDF7`, `0xEDF6`,
`0xEDF4` at 0.01 V; `0xEDFD` and `0xEDF1`, un8; `0xEDF2`, sn16 at 0.01 mV/K; `0xEDF0` at
0.1 A; `0xEDFB` at 0.01 h. So do `sim_mppt`'s `0x0201` device state and `0xEDDA` error
code.

**Two gaps where the library is silent**, so the PDF still stands:

- `0xEDE0`, battery low-temperature level, sn16 at 0.01 °C. The library has no such
  register.
- **Lowercase hex.** The library's receive parser accepts it; `lib/vedirect` refuses it.
  This difference is kept on purpose: the library reads only MPPT output, while
  `lib/vedirect` also checks requests typed into Home Assistant, and Victron requires
  uppercase.

`HexRsp` has no `Async` (`0xA`) member. The bridge never sees an unsolicited frame, but
GateLink's UART will, and the library's async queue is the model for it there.

Suites: `lib/vedirect` 12 of 12, simnode 140 of 140, bridge 466 of 466.

## 2026-09-25 — B5's spec readings: four points where the code chose, raised for v0.16

BF-36 and BF-28 to BF-30 met four places where spec v0.15 is silent or reads two ways. The
operator chose each reading on 2026-09-25, and the code builds it. Each is raised for
spec v0.16 here, one line each. Until v0.16 settles them, the code is a reading of the
specification, not a statement of it.

- **(a) A refused write-class `HEX_REQ`.** §8.13 names `HEX_RSP(REJECTED_UNAUTHENTICATED)`
  for a request with no valid MAC, and §9.4 step 3 names `COMMAND_ACK(REJECTED_MAC)` for
  every authenticated frame. The simnode answers a bad MAC with the `HEX_RSP`, and a
  context, deduplication or `seq` failure with the `COMMAND_ACK` §9.4 names. The bridge
  claims either answer. Raise: §7.6 should say which frame answers at each step.
- **(b) `HEX_RSP`'s `seq`.** §9.2's table correlates a `HEX_RSP` to its request by `seq`,
  and §7.6 does not say the node repeats it. The simnode repeats the request's `seq`, and
  the bridge matches on the node and that `seq`. Raise: §7.6 should state it.
- **(c) A retained `write_enable/set`.** §16.2 marks `write_enable/{state,set}` retained
  together. A retained `ON` on `set` would re-arm writes on every broker reconnect, which
  defeats gate 2. The bridge ignores a retained `set` and publishes an empty retained
  message to clear it. Raise: §16.2 should mark `set` not retained.
- **(d) Two VE.Direct topics the spec does not list.** `vedirect/charge/state`, the
  readback R-3.5d asks for, is not in §16.2; it follows §16.1's grammar with `charge` as
  the item. §16.6's list of a bench node's answers names `config/ack`, `config/state` and
  `cmd/ack`, and not `hex/response`, `hex/audit` or `write_enable/state`. The bridge
  publishes those three for a bench node whatever `simnode_diag_enable` says, for D65's
  reason, and gates the bench readback on the flag. Raise: §16.2 and §16.6 should list
  them.

Impl Plan §6.4.1 records the code, and none of it has been on air.

## 2026-09-25 — V-B6 on the bench: the three gates, the expiry and the audit, against f1's simulated MPPT

**V-B6 passed on the bench.** The bridge board and the XIAO were flashed from `7e7b92a`, a
clean tree, and f1 ran `ROLE_GATELINK` with BF-36's simulated MPPT. A script held both
serial ports open for the whole run and published to `lran/simnode1/vedirect/...` on the
sandbox broker. `simnode_diag_enable` was 1 for the run, so the bench readback published,
and 0 afterwards.

| Step | Result |
|---|---|
| Get `0xEDF7`, `:7F7ED006A` | `answered`, `ok`, `:7F7ED008C05D9`, 14.20 V. `seq` 11, in the read space |
| Set `0xEDF7` to 14.00 V while disarmed | `refused_disarmed`, no frame on air; `hex/audit` with `authorization` `disarmed` |
| `ON` on `write_enable/set` | `write_enable/state` `ON` 0.8 s later |
| The same Set while armed | `answered`, `ok`; `hex/audit` with `authorization` `armed`. The XIAO logged the write under command `seq` 2 |
| Get `0xEDF7` again | `:7F7ED007805ED`, 14.00 V. The readback pass after the write had already published it on `charge/state` |
| `mppt f1 timeout 1`, then a Get | `answered` with `status` `timeout` and a `null` response |
| `hello` on `hex/request` | `malformed`, `seq` `null`, nothing transmitted |
| Arm, then wait | `write arm expired` and `write_enable/state` `OFF` **301.0 s** after the arm. The Set that followed was `refused_disarmed` |
| A fresh subscriber | Received the retained `hex/audit` (the last refusal), `write_enable/state` `OFF` and `charge/state` |

**The first readback pass ran as the bridge first heard f1**, after the roll, one register
every 2 s, and all ten answered. The XIAO counted 43 frames in and 44 out, with no drop and
no rejection.

**A retained `write_enable/set` is refused on a reconnect, and only then.** The first
attempt published a retained `ON` while the bridge was subscribed, and the bridge armed. The
test was wrong, not the code: a broker delivers a message to a subscriber already
connected with the retain flag clear (MQTT 3.1.1 §3.3.1.3), so that `ON` is
indistinguishable from an operator's. The case the rule guards is a reconnect. With that
`ON` still retained, a reset of the bridge logged `retained write_enable/set ignored and
cleared`, published an empty retained message on `write_enable/set`, and stayed disarmed.
A fresh subscriber then found no retained `set`.

**A refused write takes a command `seq`.** The two `refused_disarmed` Sets took `seq` 1 and
4, and neither reached the air. The node accepts any `seq` above its high-water mark (spec
§9.4), so the gap costs nothing. It does mean `seq` on `hex/audit` is not a count of writes
the node saw.

**`charge/state` does not follow a change made behind the bridge's back.** After `mppt f1
reset` put 14.20 V back, `charge/state` still read 14.00 V, because a pass runs only on
first hearing and after a write the bridge sent. On GateLink the same happens when a
setting is changed with VictronConnect. The next bridge boot corrects it.

Not covered: gate 1 on air. No bench tool sends a write-class `HEX_REQ` with a bad MAC;
`test_gatelink` and `test_hex_proxy` cover it on the host.

## 2026-09-25 — The air-timing defects: exchanges exclude each other, windows open on air, step-6 POLLs take turns

**Three defects from B4b's bench runs are fixed and host-tested, and none is yet shown on
air.** All three sat in `sched_task`'s decision about when a frame may go or when its answer
is late. Each is one commit on `b-defects-air-timing`.

**A command and a `CONFIG` could be in flight to one node together** (`30d3ae1`). Every
pair of exchanges excluded each other except two: `sched_config()` did not ask whether a
command or a roll was busy, and `sched_commands()` and `sched_roll()` did not ask about the
`CONFIG`. Each sender carried its own list, so the gap was one missing term in three places.
`exchange_may_start()` in `air_turn.h` now answers for every exchange, and each sender asks it
alone plus its own extra condition. A gate command can now wait behind a `CONFIG`, for its ACK
timeout and then its readback, where before it would have gone beside it.

**A reply window opened at the queue, not on air** (`2b55681`). This is the *poll clash
fixed* entry's f1 roll, retried 527 ms after it went on air. The code confirms the
mechanism that entry could only infer: every path called `on_sent(now_ms)` straight after
`send_tx()`, and `lora_task` can hold a frame through several CAD rounds of up to
`backoff_max_ms` each. The trace still does not show when that frame was queued, so the
2.5 s wait remains an inference from the arithmetic. The command, roll, `CONFIG`, HEX and
PHY-change paths now queue with a ticket. `lora_task` records when each ticketed frame
leaves it, whether sent, timed out, refused by the radio or dropped with the radio down.
`sched_task` holds that path's `next()` until then, and `on_aired()` moves the window's
start. **A HEX write was the sharpest case**: it is never retried, so a window closed
early reported `unknown` for a write that happened.

**The scheduled poll keeps its window at the queue.** `scheduler.h` says its answer time
includes the queue and media access on purpose, B3a records it against
`poll_reply_timeout_ms` (Impl Plan §6.1.1), and 10 s is sized for that wait. Moving it would
change a measure that other entries cite. Polls a `CONFIG` readback or a PHY change sends
belong to their path, and they do wait for the air.

**The hold has a 10 s backstop.** If `lora_task` never reported a frame, the path would
hold for good, and on the command path that means a gate that stops answering. After 10 s the
window opens from then, and the serial log prints `air: <path> frame not reported`. Nothing
should print that line; if it prints, look for an exit from `lora_task`'s transmit path that
does not call `note_tx_done()`.

**A PHY change's step-6 `POLL`s go one at a time** (`3c53e7f`). Each node's `POLL` waited
only for that node's previous one, so two went 229 ms apart to different nodes. Now a
`POLL` whose answer is still due holds the next, to any node, for one
`config_ack_timeout_ms`. **The first version of this fix starved a node**: after a silent
node's timeout, the loop picked the first unheard node again, which was the silent one.
`test_a_silent_node_holds_the_next_poll_for_one_timeout` caught it, and the nodes now take
turns from the one after the last polled. Worst case, with every node silent, each node is
polled once per `nfleet × config_ack_timeout_ms` rather than once per timeout. Step 8's
deadline is unchanged.

**What would show these on air**, on a bench run with the frame log (BF-27):

- No `CONFIG` `tx` record while a `COMMAND` or `ROLL_CONTEXT` to any node awaits its answer,
  and the reverse.
- No roll or command retry within `cmd_ack_timeout_ms` of the previous attempt's `tx`
  record. The `tx` record is written when `start_transmit()` starts, so the window, which
  now opens at `TX_DONE`, closes at least that long after it.
- Every step-6 `POLL` `tx` record follows the previous one's answer, or comes at least
  `config_ack_timeout_ms` after it.

Native suites: 475 cases pass. `heltec` builds.

## 2026-09-25 — The air-timing fixes on air: no two exchanges overlapped, and every one took one attempt

**The bridge ran `6db6771` for a 5-minute bench run, and none of the three defects
appeared.** The fleet was f0 and f2 on the Heltec and f1 on the XIAO, with `deployed` set and
`poll_interval_s` 10. The trace is
[`data/air-timing-bench-2026-09-25.log`](./data/air-timing-bench-2026-09-25.log).

| Check | Result |
|---|---|
| Rolls after the boot | f0, f1 and f2, **1 attempt each** |
| A `CONFIG` and an `OPEN` to f1 published 50 ms apart, four times, both orders | **Serial every time.** The `COMMAND` went, its ACK came back, and only then the `CONFIG`, 0.2–1.7 s later |
| Five more `OPEN`s to f1 | All acknowledged at the first attempt, 1.5–2.7 s from the publish to `cmd/ack` |
| PHY change to 917.0 MHz and back | **Both committed**, in 11.6 s and 13.8 s |
| Step-6 `POLL`s | Six. After each change's first, each went 0.2–2.1 s after the previous node's answer |
| A frame sent while another path's answer was due | **0 of 111** |
| `air:` backstop lines | 0 |

**How an overlap was counted.** A `tx` in the bridge's frame log opens its peer's answer
until the next `rx` from that peer, or until that path's own window has passed: 10 s for a
`POLL`, 3.5 s for a `COMMAND`, 8 s for a `CONFIG` and 3 s for a `HEX_REQ`, each plus 300 ms
of airtime. **A flat 10 s cap for every type, the poll-clash entry's rule, flags five
frames.** All five follow a BF-30 `HEX_REQ` to f0 or f2 by 5.0–9.2 s. Neither identity has
an MPPT behind it, so neither answers, and the HEX path's 3 s window had closed.

**Media access was busy, so the window fix had something to act on.** The bridge counted 44
CAD backoffs and 2 forced transmissions over 105 frames. No exchange was retried. **The
2026-09-24 roll retry was not reproduced**, so this run shows the fix does no harm under
that load, not that it closes that case. The host test
`test_the_window_counts_from_when_the_frame_aired` in `test_context_roll` replays it.

`sched_task` read 1932 bytes free at its lowest, on a `CONFIG` resolution; `lora_task` read
6416. **The bench was restored**: `deployed` 0 and `poll_interval_s` 60 on `simnode0` to
`simnode2`, and `simnode_diag_enable` 0, all acknowledged. The fleet is on 917.4 MHz. f1 holds
`dedup_cache_depth` 8, its default, in RAM until its next reboot.

## 2026-09-25 — The mirror readback: a node reboot is read from its STATUS, and config/state follows it

**`config/state` no longer reports overrides a node reboot cleared.** The bridge now reads a
reboot from a node's `STATUS` and asks for a readback, through the pending bit D69's
`CONFIG_CHANGE` already sets. Impl Plan §6.7.7 records the design.

**The handoff proposed a readback when a node's `ctx_id` changes. Spec §10.1 rules that
out.** Since v0.13 a new context "no longer means the node rebooted", because a roll makes
one, and the same section names `boot_count` and `uptime_s` as the fields that report a
reboot. Keyed on the context, the readback would fire after every bridge restart, once per
roll. It would also race the roll: a node whose `ACCEPTED` is lost is learned at its next
frame, before the roll resolves on `REJECTED_CTX`. `RebootWatch` in `config_path.h` reads
three signs instead: `status_reason` `BOOT`, a change in a non-zero `boot_count`, and an
`uptime_s` below the last reading plus the time since. A roll moves none of them.

**On the bench**, the bridge ran `3534e0b`. f1 was on the XIAO, running `7e7b92a`. Each
case first set `dedup_cache_depth` 16 on `lran/simnode1/config/set`, and `config/state`
showed it as an override within 3 s. The trace is
[`data/mirror-readback-bench-2026-09-25.log`](./data/mirror-readback-bench-2026-09-25.log).

| Case | Sign that caught it | `config: f1 rebooted` | `config/state` back to `null`, `default` |
|---|---|---|---|
| `REBOOT` on `lran/simnode1/cmd/reboot/set`, payload `165` | `BOOT`, in the simnode's first `STATUS` | On that `STATUS` | 8 s after the publish |
| Board reset, by reopening the XIAO's port, then `push f1` | **`uptime_s` alone.** The push carried `DEBUG_SYNTHETIC`, and a real boot reports `boot_count` 0 | On the push | 4 s after the push |

**The board reset abandoned a BF-30 charge readback mid-pass**, at `0xEDFB`, because the
reset landed on a `HEX_REQ` in flight. That is the pass's own timeout doing its job; the
next first hearing starts a new pass.

**The node's readback marks `freq_hz` and its PHY neighbours `override`.** That is the
simnode's `phy reset` defect, already under group 2 of the handoff, and not new.

`sched_task` read 1900 bytes free at its lowest, on a `HEX_REQ`. The previous entry's run
read 1932. **The bench was restored:** `simnode_diag_enable` 0, acknowledged. f1's override
went with its reboot. Native suites: 483 cases pass. `heltec` builds.

## 2026-09-25 — A node reset between a command and its ACK made §10.3's resync a second execution

**The question was what the spec provides to reboot a node.** `REBOOT` (`0x7F`, guard
`0xA5`) was the whole answer, and §8.1's table row was all the spec said about it. Tracing
a lost ACK through it found the defect. The rebooted node answers the bridge's retry with
`REJECTED_CTX`. §10.3 step 2 adopted the new context and resent the `REBOOT` with `seq` 1,
and the node accepted it, because its dedup cache and `rx_high_water` went with the reset.
**One lost ACK, two reboots.**

**The same path runs without any directed reboot.** A watchdog, a panic or a brownout
between an `OPEN` and its ACK draws the same answer, and the resync became a second relay
pulse. The bridge cannot tell that answer from a node that reset before the request
arrived. `command.cpp` and `hex_proxy.cpp` both resynced, and `hex_proxy.cpp`'s comment
argued the retry could not write twice. That holds only for a node that has not reset.

**The operator chose D70–D72 the same day**, and spec v0.16's new §10.7 lists every reset
case. An actuation command, a `REBOOT` or a VE.Direct Restart that draws `REJECTED_CTX`
now ends `unconfirmed`, and the bridge adopts the context without a retry. `resync_may_retry()`
reads spec §8.1's split by range. `test_command` gains three cases and `test_hex_proxy`
one; the three resync cases that used `OPEN` now use `REQUEST_STATUS`. Native suites:
487 bridge cases, 137 library and 140 simnode, all pass. `heltec` and the simnode build.

**Two more gaps turned up in the trace.** §10.1 said only "random" for `ctx_id`, and a
node without WiFi or Bluetooth gets a pseudo-random `esp_random()`. A repeated `ctx_id`
would have let the retried `REBOOT` pass §9.4 step 2 and loop. It would also have reopened
replay, and the bridge would have withheld the new boot's events as repeats. And an
`EVENT` queued at a reset is lost, because events have no ACK. v0.16 requires true
entropy, and it has a node send `FIRE_ASSERTED` and `HARD_SHUTDOWN` again at boot.

**Not run on the bench.** The 2026-09-25 mirror-readback run showed `REBOOT` with its ACK
delivered: `ACCEPTED`, a new `ctx_id`, and an authenticated `CONFIG` accepted afterwards.
The simnode's reboot is simulated, and no run has lost the ACK. The handoff holds both.

## 2026-09-25 — D70 on the bench: a reset between a command and its ACK ends `unconfirmed`, with one execution

**The bridge no longer resyncs an actuation or a `REBOOT` that drew `REJECTED_CTX`.** It
publishes `unconfirmed`, adopts the node's new context, and sends nothing more. The bridge
ran `99d5be4`, flashed over USB for this run. f1 was on the XIAO, whose build still names
spec v0.15 in its banner; nothing it does on this path changed in v0.16. The trace is
[`data/d70-bench-2026-09-25.log`](./data/d70-bench-2026-09-25.log).

**The handoff's recipe could not test `OPEN`.** An `OPEN` whose ACK is suppressed, with no
reset, is answered from the dedup cache and ends `acked`, which is correct. The actuation
case needs a reset between the execution and the retry. `ctx f1 new` on the console
supplies it: it calls the same `new_context()` as the simulated `REBOOT`, and it went in
10 ms after the simnode logged `OPEN ACCEPTED`, well inside `command_ack_timeout_ms`
3000. Case C is the recipe as written, kept as the control.

Each case armed `ack f1 suppress 1` first.

| Case | Simnode | `cmd/ack` | Executions, actuations after |
|---|---|---|---|
| A: `REBOOT`, payload `165` | `REBOOT ACCEPTED`, `rebooted`, then `REJECTED_CTX` on the retry | `unconfirmed`, attempts 2, result 3, 5.7 s after the publish | 1, 0 |
| B: `OPEN`, then `ctx f1 new` | `OPEN ACCEPTED`, then `REJECTED_CTX` on the retry | `unconfirmed`, attempts 2, result 3, 8.4 s after the publish | 2, 1 |
| C: `OPEN`, no reset | `OPEN ACCEPTED`, then `DUPLICATE_CACHED (ACCEPTED), not executed` | `acked`, attempts 2, result 7 | 3, 2 |

**One reboot and one actuation per command, and no third frame.** In A and B the retry
carried the old `ctx_id` and the same `seq`, and the bridge's next command reached
the new context at `seq` 1 with no roll. `diag/cmd/state` read `cmd_unconfirmed` 2 and
`cmd_resyncs` 0 after B.

**This is still a simulated reset.** The simnode's `REBOOT` and `ctx new` clear what a
reset clears without an `esp_restart()`. Group 2 of the handoff holds that gap. The bench
was restored: `ack f1 normal`, and `simnode_diag_enable` was 0 throughout.

## 2026-09-26 — The three restart edges: each now survives the restart in NVS, host-tested

Group 1's last item. Impl Plan §6.7.8 has the design. This entry records what reading the
code found.

**The trial marker's loss had a second cost.** `Store::restore_defaults()` called
`clear_all()`, which on the bridge is `Preferences::clear()` on the whole `cfg`
namespace, and then `save_group()`, which writes the blob with the marker clear. Between
the two writes, flash held no PHY group at all. A reset in that moment would bring the
bridge up on the table's defaults, off a fleet that had moved. `clear_all()` now removes
the scope's table keys one at a time and never touches the blob. `Store::restore_defaults()`
no longer rewrites the group, and lran-config's `Persist` contract says so.

**The owed `config/ack` clears only after it has left the bridge.** Clearing the record when
`sched_task` queued the answer would not have fixed the 2026-09-24 case, because that
answer was queued and then lost in the queue. `drain_publish_queue()` clears the record
once the publish queue is empty and the transport's `pending()` is 0. A refused
publication sets the answer back to owed, so a disconnect at the wrong moment can publish
it twice.

**The bench `online` needed a record, not a rule.** The first BF-26 build published
`offline` at every boot with the flag clear, which spec §16.6 forbids (the *BF-26 on air*
entry). With every state `Unknown` at boot, the bridge has no other way to tell a topic
holding a retained `online` from one never published. The mask is written on the tick a
bench row's topic changes between `online` and `offline`, not on every publication.

**Verified:** lran-config 29 and bridge 490 host tests, simnode 140, the `heltec` and
`simnode-heltec` builds, and `run_ci_local.py`. No bench run: each edge needs a reset
timed within a fraction of a second, or a flag set while NVS refuses the write.

## 2026-09-26 — Group 2's housekeeping: the leveled log, the watchdog and `phy reset`, host-tested

Four of group 2's five items, scoped with the operator at the start: BF-11a, BF-11b,
`mqtt_task`'s high-water mark and the simnode's `phy reset`. The simnode's spec v0.16
reset obligations and BF-27's bridge-side simulators became groups of their own in the
handoff. Impl Plan §5.2.2 has the design.

**The first line length cut the longest line.** `LogMessage` held 160 bytes, the frame
log's line length. The test that formats the `levers:` line with every field at its widest
failed: that line reaches 182 characters. It is 192 now. A cut line ends in `...`, but a cut
`levers:` line hides the value a bench run set out to confirm.

**The watchdog timeout is compile-time, against root rule 8's letter.** Impl Plan §5.2.2
argues it. The TWDT was already running under Arduino-ESP32 at 5 s, watching only core
0's idle task. `esp_task_wdt_init()` reconfigures it to 10 s rather than failing, which the
IDF 4.4 header states.

**`phy reset` had to leave the table's defaults unmarked, and no call on `Store` could do
that.** `restore()` holds any value it is given as an override, a default included, and
`restore_defaults()` deliberately keeps the PHY group (D52). lran-config gains
`Store::forget_phy_group()`, documented as a bench tool that nothing on the air reaches.

**Verified:** bridge 497, lran-config 30 and simnode 140 host tests; the `heltec`,
`simnode-heltec` and `simnode-xiao-wio` builds; `run_ci_local.py`. **Nothing was flashed.**
Owed on the bench: the `Reset:` banner line, one `mqtt: stack high-water` line after
connect, `sched_task`'s high-water mark on the next configuration resolution against
2026-09-24's 1352 bytes, and `phy reset` on a simnode followed by a readback with no PHY
row marked as an override.

## 2026-09-26 — Group 2's housekeeping on the bench: all four owed lines read

The bridge ran `4ef3f2c` on `/dev/cu.usbserial-0001` (MAC `44:1b:f6:f9:70:14`), flashed
over USB. Both simnodes ran the same commit: the Heltec on `/dev/cu.usbserial-4` (MAC
`44:1b:f6:fa:bc:2c`) holds f0 and f2, and the XIAO on `/dev/cu.usbmodem2101` (MAC
`68:ee:8f:4b:85:f4`) holds f1. The trace is
[`data/housekeeping-bench-2026-09-26.log`](./data/housekeeping-bench-2026-09-26.log).

- **The `Reset:` banner line reads `Reset: power_on`** after an RTS reset. The ROM line
  above it reads `rst:0x1 (POWERON)`, so the two agree: the CP2102 pulls EN, and the S3
  reports that as a power-on reset.
- **`mqtt_task`'s high-water mark fell three times**: 4616 bytes free of 6144 before the
  broker connected, 2568 after it, and 2124 once the `get_all` readbacks had run. That
  leaves 2124 bytes as the lowest reading so far, not a settled figure.
- **`sched_task` had 2296 bytes free** after f1's `get_all` resolved. The comparable
  2026-09-24 figure is 2312, after an ordinary `CONFIG`, so `log_printf()` costs about 16
  bytes on this path rather than the 130 expected. The 1352 bytes of 2026-09-24 came after
  two PHY changes and an abandoned one. No PHY change ran here, so that low is not
  re-measured.
- **`phy reset` leaves no PHY row marked `override`.** After `phy reset` on the XIAO, a
  `get_all` on `lran/simnode1/config/set` drew six PHY results, and `config/state` showed
  `freq_hz` to `phy_trial_s` as `default`. `poll_interval_s` 60 and `deployed` 0 stayed
  overrides, as the previous bench left them.

**The first `get_all` was refused with `context_roll_pending`.** The reflash gave f1 a new
`ctx_id`, and the bridge had not heard it. `push f1` rolled the context in one attempt, and
the retry succeeded. That is spec §10.1 working as written, not a defect.

No watchdog reset appeared on any board during the run. The bench was left as it was
found: `simnode_diag_enable` 0, and no row's `deployed` changed.

## 2026-09-26 — The simnode's resets are real: five boots on the bench, one BOOT event lost on air

**The simnode now meets spec v0.16's reset obligations** (§10.1, §10.7, §8.14). An accepted
`REBOOT` resets the board through `esp_restart()` once its ACK is on the air. Every boot
sends a `BOOT` status and then a `BOOT` event with the reset cause. `ctx_id` is drawn with
the bootloader's entropy source on, and `boot_count` comes from NVS. Impl Plan §10.9.2 has
the design, and `firmware/simnode/CLAUDE.md` has the three things to keep.

The run used the XIAO on `/dev/cu.usbmodem2101`, flashed from this branch, with the
bridge on `a85734f`. `simnode_diag_enable` and f1's `deployed` were set for the run and
cleared after it. The trace is
[`data/simnode-reset-bench-2026-09-26.log`](./data/simnode-reset-bench-2026-09-26.log).

| Reset | Cause the node reported | `boot_count` | `ctx_id` | BOOT status and event in the bridge's `rxlog` |
|---|---|---|---|---|
| Port opened (`rst:0x15`) | `EXTERNAL` | 7 | `0x34f3b1d8` | Both |
| `REBOOT` on `lran/simnode1/cmd/reboot/set`, `165` | `REBOOT_COMMAND` | 8 | `0x2ac1e583` | Both. `cmd/ack` read `acked`, one attempt |
| `reboot` | `SOFTWARE` | 9 | `0x2c60dc3d` | **Status only** |
| `reboot panic` | `PANIC` | 10 | `0x31e23bc1` | Both |
| `reboot f1 WATCHDOG`, simulated | `WATCHDOG` | 11 | `0x3e0ddc84` | Both |

**The ACK went out before the reset.** The node logged `REBOOT ACCEPTED`, and 160 ms later
`REBOOT: ACK on the air, restarting`. The ROM line after it read `rst:0xc
(RTC_SW_CPU_RST)`.

**No `ctx_id` repeated** across the ten boots of both runs. All five in this table start
`0x2` or `0x3`, so a further sample checked the spread: twelve `reboot` cycles and twenty
`ctx f1 new` draws. Their first hex digits ran from `0` to `f`, with no repeat. Spec §10.1's falsifying check
also names a watchdog reset and a power cycle. Neither ran: the simnode has no command that
starves a watchdog, and a power cycle needs hands on the cable.

**The first run reported the port-open reset as `UNKNOWN`.** ESP-IDF 4.4 has no
`ESP_RST_USB`, and returns `ESP_RST_UNKNOWN` for the S3's `USB_UART_CHIP_RESET`. `main.cpp`
now reads the ROM reason in that case, and the second run's table shows the fix.

**The `SOFTWARE` boot's event never reached the bridge.** The node queued it as `seq` 2,
after the status at `seq` 1. The bridge's `rxlog` shows the status at 1216531 ms and then
its own readback `POLL` at 1216884 ms (Impl Plan §6.7.7). In the other four boots the event
arrived about 290 ms after the status, so it was due while the bridge transmitted. The
node heard the `POLL` and answered it at `seq` 3 and 4. That points to a collision. Nothing
confirms it yet, because the node's `radio` counters were not read during the run. Spec
§10.7 already counts a lost event as lost, because events have no ACK. What is new is that
the bridge's own reaction to a `BOOT` status can cause the loss, one boot in five here.

No BOOT event reached MQTT, and none should: spec §16.6 withholds a bench node's events.
The bridge's `event_frames` counter and its `rxlog` are the evidence here. Spec §10.7's
active alarms (D72) never arise on a simnode, because its inputs boot at their defaults.

## 2026-09-26 — The lost BOOT event: the bridge's own CAD destroyed it, twice over

**The event was not lost to a collision.** The bridge's CAD took its radio out of receive
while the event was arriving. The *simnode's resets are real* entry above found the loss,
at one boot in five, and named a collision as likely. A CAD opens two windows in which it
destroys an arriving frame. Closing both took two commits, `5ba4d8c` and `71e9173`. After
the second, 60 boots lost no event.

### What the first trace already showed

The node had sent the event: it answered the readback `POLL` at `seq` 3 and 4, so `seq` 2
went out. The bridge was deaf for 46 ms before that `POLL`, and for 21–25 ms before every
other `POLL` in the trace. One CAD costs 21–25 ms, so 46 ms is two, and one of them found
the channel busy. The minute around it confirms that: `cad_backoffs` rose by 1 and
`cad_deferred` by 0. A busy CAD that was not deferred ran while the event was on the air,
and the radio had not yet flagged a valid header.

### The first window: preamble to header

Both radio drivers deferred a CAD while `HEADER_VALID` was set, and not before it.
RadioLib 7.7.1's default receive flags leave `PREAMBLE_DETECTED` out. So a CAD between a
preamble and its header destroyed the frame. At SF9 that window is about 88 ms: 8 preamble
symbols, 4.25 of sync and 8 of header, at 4.096 ms each. `5ba4d8c` enables the flag in
both drivers and moves the rule into `lib/lran-link`'s `RxArrival`.

### The second window: a burst's next frame

**The preamble guard alone still lost one event in 40.** In that boot (`boot_count` 57),
the `POLL` went at +975 ms, 48 ms of deafness behind it, and the busy CAD was again not
deferred. So that CAD started before the event's preamble could be detected at all. The
node sends its event 39 ms after its status ends: a 247 ms `EVENT` (spec §15.1) arrives
286 ms after the status, in every boot here. A CAD in the event's first symbols destroys
it, and no receive flag is up that early.

`71e9173` starts no CAD for 176 ms after any reception ends. That is twice the
preamble-to-header time, derived from the PHY in use. The holdoff is neither a CAD nor a
busy one, so it spends no retry and moves no counter. The same commit fixes a flaw found
in review. The flags are sticky, so a real preamble behind a stale one set no new bit, and
the driver restarted receive and ran its CAD in the same pass, over the real frame. It now
leaves the CAD to the next pass.

### The runs

Each run reset the XIAO with `reboot` every 12 s, or every 30 s in the first run. It read
the node's `radio` and `stats f1` after each boot and logged the bridge's `rxlog` and
`diag/radio/state` from the broker. `simnode_diag_enable` and f1's `deployed` were set for
each run and cleared after it.

| Run | Bridge | Boots | Events heard | Bridge `cad_deferred` | Bridge `cad_backoffs` | Trace |
|---|---|---:|---:|---:|---:|---|
| Baseline | `a85734f` | 12 | 12 | not read per run | not read per run | [`a-baseline`](./data/boot-event-loss-bench-2026-09-26-a-baseline.log) |
| Preamble guard | `5ba4d8c` | 40 | **39** | 0 → 14 | 0 → 19 | [`b-preamble`](./data/boot-event-loss-bench-2026-09-26-b-preamble.log) |
| Guard and holdoff | `71e9173-dirty` | 60 | **60** | 0 → 25 | 0 → 36 | [`c-holdoff`](./data/boot-event-loss-bench-2026-09-26-c-holdoff.log) |

The third image reads `-dirty` because this branch's document edits were uncommitted when
it was built. Its code is `71e9173`'s. The XIAO ran the same commit in each run, and its
banner carries no git field.

**The node rarely backed off, and never deferred.** Across the 112 boots, its
`cad_deferred` read 0 each time and its `cad_backoffs` read 0 in 108. The other four read 1
in the second run, and 1, 2 and 2 in the third. It sent 4 frames in 96 boots
and 5 in 16. **`tx_forced` stayed 0** on both boards.

**The holdoff moved the earliest readback `POLL`, not the typical one.** Measured from
the `BOOT` status, the earliest went at 322 ms in the second run and at 488 ms in the
third. The median moved from 753 to 718 ms, because `sched_task`'s 1 s tick sets most of
the delay. Each of the third run's 25 deferrals is a CAD that found a frame arriving and
let it land.

### What this does not show

**Zero losses in 60 is not proof of zero.** Before either fix, one event was lost in 17
boots: one in the five of the first trace and none in the baseline's 12. A residual stays, because CSMA
cannot close it: a frame that starts in the same few milliseconds as a CAD. The holdoff
covers only a burst whose second frame starts within 176 ms of the first.

**No counter tells a preamble deferral from a header deferral.** `cad_deferred` counts
both. The figures above cannot say which window each deferral closed.
