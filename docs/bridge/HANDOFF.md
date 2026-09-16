# Bridge Node — session handoff

**Written 2026-09-16, at the end of the session that built BF-18 and put the command
path on air.** It replaces the earlier 2026-09-16 file wholesale; that file's content is
carried over where it is still true.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**B3b. BF-21, then BF-22.**

**BF-18 is done and confirmed on air** (2026-09-16). A command from Home Assistant
reached a simnode and executed; a suppressed ACK was retried with the **same `seq`** and
answered `DUPLICATE_CACHED` *not executed*; spec §10.3's resync adopted the node's
context and retried once. **`ResyncFailed` is the one piece still host-only** — the
window between the node's rejection and the bridge's retry is under a second, and three
attempts at racing `ctx f1 new` against it all missed.

- **BF-21** — the §10.5 catalogue as a committed `simctl` script. **Read §10.5's
  multi-frame note before designing it**: a row that emits more than one frame cannot
  confirm its own ERROR from the same board. **BF-21 now decides two things**: whether
  `fault.cpp` should complete `set_displaced`'s displacing set, and **whether the simnode
  gains a `ctx_reject` fault** so §10.3 step 3 can be forced rather than raced. An armed
  behaviour is what every other catalogue row already is.
- **BF-22** — version tolerance. `bad_ver` cannot pass §10.5 until this lands.

```bash
git fetch origin -p
git switch main && git pull --ff-only
gh pr list --state open
```

**Set the bench up like this.** One simnode board is enough; a second is needed only for
`PING` between nodes. A bench identity is polled at the bridge only once it has spoken:
`push f1` for the GateLink role, or `fault <id> hdr_rsv` for any other. **A fault's `dst`
defaults to the bridge.** **A command is driven from the broker**, not the console:
publish to `lran/<node>/cmd/<action>/set` and read `lran/<node>/cmd/ack`.

## What the last session established

**Every item below is in the engineering log's 2026-09-16 entries, with the numbers.**

- **The bridge sends authenticated frames now.** BF-18 is the first; every authenticated
  type is bridge → node (spec §9.2), so before today only the rejection path had been
  proved on air.
- **Root rule 2 held on air.** The simnode logged `dedup hit, DUPLICATE_CACHED
  (ACCEPTED), not executed` against a retry carrying the same `seq`. At the gate the
  difference is a second relay pulse.
- **Spec §6.3's cached result travels in `detail`**, proved with a non-zero one:
  `close` with `arg 5` cached `REJECTED_ARG`, and the dedup hit replayed `detail 5`.
- **BF-18 built the MQTT receive path**, which no task owned. The gap the Firmware Tasks
  changelog raised at v0.19 is half closed; **`/lib/lran-config/` is still unowned**.
- **`lran/bridge/diag/cmd/state` exists** because the bench needed it: a resync and a
  command that never resynced publish the same `cmd/ack`.
- **The run produced no discards at all** — every §14.1 counter zero across 33 frames.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the 2026-09-16 entry first: the catalogue table, W9, and the traps this run cost time on |
| 3 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§8** owns B3b's criteria; **§10.5** is the fault catalogue; **§6.2.1** BF-18; **§6.1.1–§6.1.2** BF-17 and BF-20; **§4.3.2** BF-19; **§10** the simnode |
| 4 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | §6 is B3b's tasks — BF-21 and BF-22 remain; BF-26 (§7) deferred |
| 5 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and what breaks silently |
| 6 | [`firmware/simnode/CLAUDE.md`](../../firmware/simnode/CLAUDE.md) | what the simnode has, what it does not, and its traps |
| 7 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) §9–§12, §14 | keys, context, reassembly, radio, the discard ladder. **§18.2, never §18.1 alone** |
| 8 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**, **B0**, **B3a**. **M6**, **M19**, **M20**, **M21**. **D1**, **D33**; **D34 amended**. **V-B3**, **V-B9**, **W9**. **BF-2**–**BF-9**, **BF-15**, **BF-15a**, **BF-16**, **BF-17**, **BF-18**, **BF-19**, **BF-19a**, **BF-20** |
| Not done | **B3b**: BF-21, BF-22. **BF-26** deferred. **BF-11a**, **BF-11b** |
| Queue | BF-21 → BF-22. Nothing waits on a document |

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
python3 tools/vectors/check.py                  # W4 vectors, self-check
```

**`pio` is a shell alias on the macOS build machine.** A script that does not source the
user's profile must call `~/.platformio/penv/bin/pio` by path, or every step fails as
`command not found` while looking like a build failure.

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

## Hardware state

**This table names the devices in this subproject's terms**; the range-test handoff owns
them in its own roles, and its rows do not transfer here.

| Device | Called here | Told apart by | Firmware / env | Stored state | Current state |
|---|---|---|---|---|---|
| Heltec WiFi LoRa 32 V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `bridge` / `heltec`, **USB-flashed 2026-09-16 from `5f8e3f7`, a clean tree**: banner `Version: 0.1.0`, `Slot: app0`, `Registry:` with six rows | NVS: nothing this node depends on yet | On USB to the macOS build machine, last seen as `/dev/cu.usbserial-0001`. **Polls, receives and publishes**: WiFi, broker and radio all up |
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `simnode` / `simnode-heltec`, reflashed 2026-09-16 with `Node::on_error`, MAC `44:1b:f6:fa:bc:2c` | Nothing persists; identities reset on every boot | On USB, last seen as `/dev/cu.usbserial-4`. Boots with `f0` `ROLE_RANGE` and `f2` `ROLE_HEALTH` |
| XIAO ESP32S3 + **Wio-SX1262 Kit** (p-5982, B2B) | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `simnode` / `simnode-xiao-wio`, **reflashed 2026-09-16 for BF-18** - it had been left on a pre-v0.12 image, banner `v0.11`. MAC `68:ee:8f:4b:85:f4`. Native USB, so it enumerates as `/dev/cu.usbmodem*` | B1b position log, dumped and committed | On USB. Boots with `f1` `ROLE_GATELINK` alone; `f3` `ROLE_RANGE` was added by hand for W9 and is gone after any reboot |

**A USB flash puts the bridge board back in a known state.** Flash from a committed tree: a
`-dirty` git field on the banner means the running image matches no commit.

**A wrong board selection is silent.** It writes the wrong pin map into a normal-looking
artifact. **Tell the two Heltecs apart by enclosure** — both CP2102 bridges report
`SER=0001`, and the port name is not stable across replug. The XIAO is unambiguous: it is the
only `usbmodem` port.

**No stored state on any of these boards is the only copy.** Every survey site and every B1b
position is committed under `docs/rangetest/data/`.

**`secrets.h` on the macOS build machine holds a real master key and the broker's LAN
address.** It is gitignored and uncommitted; never read it into a command, a log or a commit.
**The broker is a disposable sandbox Home Assistant install** with its own Mosquitto, so its
credentials are not the production ones — that changes at the production cutover, and a
password must not reach argv, a log or a committed file after it.
**The OTA password is read from `LRAN_OTA_PASSWORD`** in the shell that runs the upload —
prompt for it with `read -rs` rather than putting it in a file or a history line.

**Its antenna stays on it.** The bridge uses the range test's 3.0 dBi 19 cm stick (Bridge
PRD **R-4.3a.1**); the gain is a term in D1's EIRP arithmetic, and `radio_config.h` asserts
the sum at compile time.

## Behaviour that changed, and will make older artifacts read differently

- **The bridge subscribes to `lran/+/cmd/+/set` and publishes `lran/<node>/cmd/ack` and
  `lran/bridge/diag/cmd/state` since BF-18.** Text saying the bridge only publishes, or
  that `subscribe()` has no caller, is correct for before it.
- **The bridge prints `poll: <node> answered in N ms (window N ms)` since `28ffd82`.** Text
  saying no poll timing is observable is correct for before it.
- **The XIAO simnode boots as `0xF1 ROLE_GATELINK` since BF-6**, not `ROLE_RANGE`.
- **`media_access` and the PHY constants moved to `lib/lran-link/` on the B0 branch.**
- **The handheld Heltec is a simnode from 2026-09-14.** Older text calls it the range test's
  board and says it never runs a simnode build.
- **The ladder refuses unregistered sources since BF-15**, counted as `unregistered_src`.
- **`RxMessage` carries a decoded header and complete payload since BF-16**, not raw bytes.
- **`TaskSpec::stack_words` is `stack_bytes` since BF-16**; only `lora` changed, 4096 → 8192.
- **The OTA verdict requires `radio_ok` since BF-16.**
- **`LoRaBridge` is retired in favour of `Bridge Node`, 2026-09-10** (**D17** amended).
  `lran-bridge` remains the firmware target and **"LoRa Bridge" remains the HA device name**.
- **The PHY parameters are stated rather than deferred, 2026-09-10.** A document citing
  Protocol Spec v0.9 or earlier reads §12.1 as "per D1" and `backoff_max_ms` as 500.

## Traps that cost real time here

- **A command takes 4-9 s from the MQTT publish to the node**, not the ~1 s the radio alone
  suggests: `sched_task`'s 1 s tick, the TX queue behind the poll scheduler, and media
  access each add to it. Three of BF-18's bench attempts were lost to assuming ~2 s.
- **Opening the simnode's port changes its `ctx_id`**, so the bridge's learned context goes
  stale on every reconnect. Useful for reaching the resync deliberately; announce with
  `push f1` afterwards for everything else.
- **A background serial capture piped into `tail` writes an empty file**, because the pipe
  buffers until the process exits. Redirect to a file instead.
- **Opening *either* board's serial port reboots it, the XIAO included.** The 2026-09-15 file
  said the XIAO's native USB does not; it does — its banner printed and its identities reset
  to `f1` alone. Hold one port open for a whole run rather than reconnecting per command.
- **A reboot of the bridge board zeroes every counter.** Read the counters, then leave that
  port alone for the rest of the run.
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
- **`ROLE_FAULT` answers no `POLL`** (`node.cpp`). An identity in that role, once enrolled,
  always goes `offline` after three missed polls. Use `ROLE_HEALTH` for an identity that must
  answer, and expect the offline line when arming faults on a polled one.
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
- **`rx_unknown_src` replaced `unregistered_src` in BF-15a**, and it is inside `rx_dropped`
  now. Both are on the bench as of 2026-09-16. A counter document captured before that flash
  carries the old key and the old sum; compare `lran/bridge/version` before trusting either.
- **The bridge's ERROR reply and the sender's next frame deafen each other.** Half duplex:
  the bridge cannot receive while it answers, and the sender cannot hear the answer while it
  transmits. A multi-frame §10.5 row loses a frame and its reply to this, and it is not a
  defect at either end. **`gap` spaces injections, not the frames inside one injection.**
- **The simnode logs a received ERROR's `err_code` since 2026-09-16.** Before that it routed
  `MsgType::Error` to `default: ++unhandled`, so an older bench log records that an ERROR
  arrived and nothing about what it said.
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

- **`ResyncFailed` is host-tested only.** Spec §10.3 step 3 stops a command after a second
  `REJECTED_CTX`; the bench could not force one, because the window between the node's
  rejection and the bridge's retry is under a second. **BF-21 decides the simnode
  `ctx_reject` fault** that would make it deterministic.
- **Nothing a node sends the bridge carries a MAC the bridge verifies.** §9.2 makes every
  authenticated type bridge → node, so the bridge's own `rx_rejected_seq` and
  `rx_dup_command` stay at zero by construction. BF-18 proved the *sending* half.
- **The `radio_ok` half of the OTA verdict is untested on hardware.** V-B9's images fail by
  network or by panic; no image with a dead radio exists.
- **`poll_reply_timeout_ms` = 10 000 has bench evidence** (522–1686 ms at 1 m, no contention)
  but no measurement under contention or at range. M22 / V-B12 is where that lands.
- **`set_displaced`'s second counter** — §10.5 records the behaviour; **BF-21** decides
  whether `fault.cpp` should complete the displacing set instead.

#### Spec v0.12 — landed 2026-09-16, and what it left open

**All nine questions are answered.** Eight became **D35–D42** in the Decision Register;
the ninth was a fact, verified against the datasheet as **M24**. `LRAN-Spec-v0.12-Brief`
holds the reasoning and is superseded. **`ver` stays `2` and no vector regenerates** —
`generate.py` re-run against v0.12 reproduced the committed files byte for byte.

**What it opened rather than closed:**

- **W14** — §17.1's duty-cycling design assumed a silicon address filter that LoRa does
  not have, so its power model is unquantified. Owed before WellLink is built on that
  profile, moot if **D19** makes WellLink mains-powered.
- **W10** is now a counting question: `CONFIG` and `CONFIG_ACK` are single-frame, so a
  configuration larger than one frame is several messages with no atomicity across them.
  Count GateLink's real parameters against 24 entries before `/lib/lran-config/` is
  designed.
- **BF-15a and BF-19a are done and on air**, 2026-09-16 — `rx_unknown_src` is a registry
  row inside `rx_dropped`, and the bridge answers §14's `ERROR`s under §14.2's two bounds.
  The §10.5 catalogue tested both: every one of the eight §14 stages that names an `ERROR`
  produced one at the simnode, and `errors_suppressed` reached 2.
  **§14.2's bound 1 is what v0.12 left untested on air** — see *The next job* above.

#### Work no task owns

- **`/lib/lran-config/`** — System PRD §9.4 describes it; BF-26 and BF-23 need it. **The
  MQTT receive path it was paired with is built** — BF-18 did it, so a `config/set`
  subscriber now has a transport seam and an inbound queue to reuse.
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
- **M22 / V-B12** — bridge LoRa PER with WiFi idle versus saturated, under B3b.
- **GateLink M0's LDO margin** — sized for Envelope B's 19.6 dBm, tested only at −4 dBm.
- **The range-test firmware still transmits on the provisional 915.0 MHz.**

### Closed, and not to be reopened by habit

- **BF-18 — built and on air 2026-09-16**, with `ResyncFailed` noted above as host-only.
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
