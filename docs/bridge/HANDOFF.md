# Bridge Node — session handoff

**Written 2026-09-17, at the end of the session that accepted B3b and moved V-B12 to B4.**
It replaces the 2026-09-16 file wholesale; its content is carried over where it is still
true.

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

**Start with BF-23 rather than picking freely, because it unblocks two things at once.**
Besides discovery generation it owns `TODO(BF-23)` on `g_diag_interval_s`
(`task_runtime.cpp`) — the runtime lever that makes **V-B12's saturated arm possible at
all**. Until it exists the bridge's WiFi transmits one diagnostic document a minute and no
load can be put on it.

**V-B12 is now a B4 criterion, and its idle arm is already measured.** Impl Plan §8.1 is the
record of the move; the engineering log's 2026-09-17 entries are the measurement.

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

## What the last session established

**Every item below is in the engineering log's 2026-09-17 entries, with the numbers.**

- **B3b is accepted.** Its tasks were confirmed on air 2026-09-16; the milestone closed on
  2026-09-17 once its last criterion had a home.
- **V-B12 moved to B4 rather than gaining a `BF-*` number**, with the operator. Its
  saturated arm cannot run on this firmware and both things it needs — BF-23's lever and
  BF-26's bench diagnostics — are already B4 tasks.
- **M22's idle arm measured 5.6 % PER over 250 frames at one metre** with a 250 ms gap, and
  **0 % over 40 frames at 2000 ms**. The losses are a function of inter-frame spacing.
- **Zero of those 250 frames arrived corrupt.** Every loss is a frame the radio never
  delivered, which at one metre is not an RF story.
- **The bridge's own media access does not explain it.** Burst 2 lost 4 frames of 50 with
  zero transmissions and zero CAD backoffs, and the wide-gap control lost nothing while
  transmitting. **Receive turnaround is consistent with the data and not proved.**
- **`tools/simctl/per_measure.py` is the instrument**, with 18 host tests in CI and its
  arithmetic separated from its I/O.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the 2026-09-17 entries first: M22's idle arm, what it rules out, and what it does not |
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
| Not done | **B4**: BF-23–BF-25, BF-26 deferred. **V-B12**, now a B4 criterion with its idle arm measured. **M22** open. **BF-11a**, **BF-11b** |
| Queue | BF-23 first, for the reason in *The next job*. Nothing waits on a document |

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
python3 tools/checks/simctl_catalogue.py        # simctl's rows vs. fault.cpp
python3 tools/vectors/check.py                  # W4 vectors, self-check
```

**All of the above passed on 2026-09-17**: 435 Unity cases across the five native suites,
and every check above.

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
| Heltec WiFi LoRa 32 V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `bridge` / `heltec`, **USB-flashed 2026-09-16 from `24f7993`, a clean tree** (BF-22). Confirmed on air 2026-09-17: `lran/bridge/version` reads `0.1.0`, `git 24f7993`, `slot app0`. Only documentation has landed since, so it is `main` in behaviour | NVS: nothing this node depends on yet | On USB to the macOS build machine, last seen as `/dev/cu.usbserial-0001`. **Polls, receives and publishes**: WiFi, broker and radio all up |
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `simnode` / `simnode-heltec`, last flashed 2026-09-16 with `Node::on_error` — **BEHIND: it has neither `ctx_reject` nor the completed `set_displaced` (BF-21)**. Reflash before using it for the catalogue. MAC `44:1b:f6:fa:bc:2c` | Nothing persists; identities reset on every boot | On USB, last seen as `/dev/cu.usbserial-4`. Boots with `f0` `ROLE_RANGE` and `f2` `ROLE_HEALTH`. **Both were disabled by hand on 2026-09-17 and that is gone after any reboot** |
| XIAO ESP32S3 + **Wio-SX1262 Kit** (p-5982, B2B) | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `simnode` / `simnode-xiao-wio`, **reflashed 2026-09-16 for BF-21**, so it HAS `ctx_reject` and the completed `set_displaced`. MAC `68:ee:8f:4b:85:f4`. Native USB, so it enumerates as `/dev/cu.usbmodem*` | B1b position log, dumped and committed | On USB. Boots with `f1` `ROLE_GATELINK` alone. **`f3` `ROLE_FAULT` was added by hand for M22 and is gone after any reboot** |

**A USB flash puts the bridge board back in a known state.** Flash from a committed tree: a
`-dirty` git field on the banner means the running image matches no commit.

**A wrong board selection is silent.** It writes the wrong pin map into a normal-looking
artifact. **Tell the two Heltecs apart by enclosure** — both CP2102 bridges report
`SER=0001`, and the port name is not stable across replug. The XIAO is unambiguous: it is the
only `usbmodem` port.

**No stored state on any of these boards is the only copy.** Every survey site and every B1b
position is committed under `docs/rangetest/data/`, and every M22 run under
`docs/bridge/data/`.

**Its antenna stays on it.** The bridge uses the range test's 3.0 dBi 19 cm stick (Bridge
PRD **R-4.3a.1**); the gain is a term in D1's EIRP arithmetic, and `radio_config.h` asserts
the sum at compile time.

## Behaviour that changed, and will make older artifacts read differently

- **V-B12 is a B4 criterion since 2026-09-17**, not a B3b one. Text saying B3b is blocked on
  it, or that no task owns it, is correct for before that.
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

- **The bridge loses frames offered back to back, at one metre, on a clean bench.** 5.6 % at
  a 250 ms gap and **0 % at 2000 ms**, none of them corrupt. **A bench capture that spaces
  frames tightly will show losses that are not the bug you are chasing** — space them, or
  expect a non-zero floor. Engineering log, 2026-09-17, two entries.
- **`cad_backoffs` counts a *busy* CAD only.** A CAD that returns free still takes the radio
  out of receive and increments nothing. A zero in that column is not evidence the radio
  stayed in receive.
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

- **The bridge drops frames offered back to back, and the receive path is why.** Measured
  2026-09-17: **5.6 % at a 250 ms gap, 0 % at 2000 ms**, same board, same image, one
  variable. Not RF — none arrived corrupt. Not the bridge's own transmissions or CAD —
  burst 2 had neither and still lost 4 of 50, and the control lost nothing while
  transmitting. **Receive turnaround is consistent with the data and not proved**; what
  would prove it is a capture showing where the second frame goes, and that is not built.
  **It matters beyond M22**: a fragmented `STATUS` is exactly this pattern, and nothing in
  the protocol stops a node sending one.
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
