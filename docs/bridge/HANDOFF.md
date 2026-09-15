# Bridge Node — session handoff

**Written 2026-09-14, at the end of the session that brought BF-16 up on the bridge board,
built BF-15, built simnode B0's first slice and proved it on air, and built BF-7 and BF-8.** It replaces the
2026-09-13 file wholesale; that file's content is carried over where it is still true.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**Read this file on the B0 branch, `b0-simnode-bringup`, which is stacked on B3's
`b3-protocol-registry`.** Both pull requests stay drafts until the operator accepts each
milestone, so `main`'s copy of this file predates all of it, and B3's predates B0. **B0
cannot merge before B3**: it moves `media_access` into `lib/lran-link/`, out of code B3
adds.

```bash
git fetch origin -p
git switch b0-simnode-bringup && git pull --ff-only
gh pr list --state open
```

**B0's remaining task is BF-6**, `ROLE_GATELINK`. It unblocks the five command-path faults
(`ack_suppress`, `ack_dup`, `event_replay`, `cmd_replay`, `cmd_stale_seq`) that `fault`
refuses today, naming BF-6. After it, B0 is complete bar two pieces of bench work: flashing
the XIAO profile, and seeing BF-9's OLED page on the handheld Heltec.

**BF-9 is built, and the handheld Heltec runs it** (engineering log, 2026-09-14, BF-9 and
its correction). Its panel answers at boot. **Both profiles drive a panel**: the XIAO's is
on the Seeeduino expansion board.
Its command path calls `CommandGate::check()` before dispatch and sends `COMMAND_ACK` only
after `record()`; on `InFlight` it sends nothing (spec §9.4 v0.11). Bridge Impl Plan §10.9
records what exists; `firmware/simnode/CLAUDE.md` lists the traps.

**When the broker is reachable again, run V-B9 (Impl Plan §6.5.2) before anything else on
the bench.** It is owed since BF-16 changed `ota_policy.cpp`, and **it cannot run without
the broker**: the verdict marks an image valid only with `mqtt_connected`, so a good image
rolls back and the test records a broker outage as a firmware failure.

**Desk work that needs neither the broker nor a second board:** BF-17, the poll scheduler
(Sonnet, per the Tasks document).

## What the last session established

**BF-9, 2026-09-14, later the same day.** Impl Plan §10.9.1 has the page layout.

- **The OLED page is text first**: `oled_page.{h,cpp}`, 14 host tests that drive the real
  injector and node. `ui.cpp` draws it with the bridge's ThingPulse pin. Both simnode
  images build. **On the handheld Heltec the operator confirmed the page by eye**:
  identities, `off`, inverted fault bars, the countdown and self-clear, and the `~` cut.
  Row 0's frame format is not confirmed; it needs a second transmitting board. **Not supported:**
  that a 1 Hz redraw leaves the radio's counters unchanged, which is unmeasured, or
  anything on the XIAO, which has not been flashed.
- **The XIAO's panel is on its Seeeduino expansion board.** BF-9 first claimed the XIAO had
  no panel. The operator corrected it, and `profiles.h` now carries the range test's values.
- **The page caught one budget miss**: an impossible RSSI overran row 0. An RSSI outside
  −199…99 dBm now shows as `?`.

**Simnode B0, first slice, 2026-09-14.** The engineering log's third 2026-09-14 entry has
the transcript; Impl Plan §10.9 the choices.

- **Two Heltecs running `simnode-heltec` complete every PING round trip on D1's PHY**: 8
  bytes in 520 ms, the 222-byte frame in 2289 ms, and the 15-fragment set in both
  directions in about 8.5 s. Counters reconcile on both boards; no TX error, timeout or
  forced transmission. **Not supported:** anything about range (about 1 m apart), four
  identities on one board on air, or the XIAO profile, which builds and was not flashed.
- **Spec §12.3 media access, `RadioPins` and `kPhy` now live in `lib/lran-link/`**, shared
  by the bridge and the simnode. The bridge, flashed back from `cab05e8`, boots on it with
  the radio up.
- **Schema `0xF0` has no `status_reason`**, so the simnode marks it synthetic with
  `health_flags` bit 0 (Impl Plan §10.1).
- **The bridge does not answer PING**, which spec §17.3 requires of every node build. No
  task gives it to the bridge.

**BF-16 on the board, 2026-09-14.** The engineering log's first 2026-09-14 entry has the
banner lines verbatim.

- **The SX1262 comes up on D1's PHY** with Impl Plan §10.8.1's Heltec pin map, and no
  `radio down` line appeared in 40 s. That is evidence the TCXO and RF-switch settings are
  right. **It is not evidence that a frame goes out or comes in, or that DIO1 wakes
  `lora_task`.**
- **`lora_task` used about 1.7–1.9 KB of its 8192-byte stack at bring-up**: 6248 and 6496
  bytes free on two boots. Taken before any frame arrived, so it is a floor, not a working
  load.
- **MQTT connect attempts back off and nothing reboots** with the broker unreachable, and
  `lora_task` kept running. Observed once.

**BF-15, 2026-09-14.** The engineering log's BF-15 entry and Impl Plan §4.2.1 have the full
account.

- **`registry.{h,cpp}` is the registry**: `kNodeTable` with six rows, HKDF keys at load,
  `is_bench`, and the learned fields. `registry_runtime.{h,cpp}` adds mbedTLS and a mutex.
  **Every derived key matches its W4 vector** (`test_registry`).
- **What a node is** is written once, before `start_tasks()`, and read lock-free by
  `lora_task`. **What the bridge learns** is written under a mutex, today only by
  `app_task`: `ctx_id` (reset `cmd_seq` to 1 on a new one), `last_seen`, RSSI, SNR,
  `proto_ver`.
- **`RxLadder` refuses a source the registry does not know**, after stage 9 and before
  stage 10, counted as `unregistered_src`. **Spec §14 has no stage for it** — raised for
  v0.12, not patched.
- **On the board**, the `Registry:` banner line lists all six rows and the radio still comes
  up. **Not supported:** that a key verifies a frame on air.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the 2026-09-14 entries: the bring-up, BF-15 and the spec gap, B0's first slice, BF-7 |
| 3 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§10** is the simnode, **§10.9** what B0 has built, **§10.5.2** BF-7's primitive and the fault-to-operation map; **§4.2.1** is BF-15; **§8** owns B0's and B3's criteria; **§6.5.2** is V-B9, owed |
| 4 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | §4 is B0's tasks, BF-6 to BF-9 left; §6 is B3's order, BF-15 to BF-22 |
| 5 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and what breaks silently |
| 6 | [`firmware/simnode/CLAUDE.md`](../../firmware/simnode/CLAUDE.md) | what the simnode has, what it does not, and its traps |
| 7 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) §3.2.1 | D34's amendment, which the simnode's command path follows |
| 8 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) §9, §10, §11, §12, §14 | keys, context, reassembly, radio, the discard ladder. **§18.2, never §18.1 alone** |
| 9 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**. **M6**, **M19**, **M20**, **M21**. **D1**, **D33**; **D34 amended**. **BF-15** and **BF-16** built; BF-16's radio up on the board. **BF-2**, **BF-3**, **BF-5** and BF-4's core built; PING echo on air. **BF-7**, **BF-8** and **BF-9** built, host-tested |
| Not done | **B0**: BF-6, the rest of BF-4, BF-9 on a panel. **B3**, every criterion. **V-B9's re-run.** **BF-11a**, **BF-11b** |
| Queue | **BF-6** → flash the XIAO simnode → B0 accepted → BF-17 to BF-22. **V-B9** as soon as the broker is reachable |

```bash
pio test -d lib/lran-protocol -e native         # library host suite
pio test -d firmware/bridge -e native           # bridge host suites, test_registry and test_lora among them
pio run  -d firmware/bridge -e heltec           # bridge target - NEEDS secrets.h
pio test -d lib/lran-link -e native             # spec 12.3 media access, both firmwares
pio test -d lib/lran-sim -e native              # BF-7's FramePatch, against the W4 negatives
pio test -d firmware/simnode -e native          # simnode host suites, test_fault (BF-8) among them
pio run  -d firmware/simnode -e simnode-heltec  # simnode target - NEEDS secrets.h (key only)
pio run  -d firmware/simnode -e simnode-xiao-wio
python3 tools/checks/lora_task_never_blocks.py  # lora_task blocks on nothing; reads the LoRa files and registry.cpp
python3 tools/checks/no_mbedtls_hkdf.py         # HKDF built from HMAC, spec 9.1
python3 tools/checks/bridge_partitions.py       # A/B table; add --firmware/--elf after a build
python3 tools/checks/spec_citation_version.py   # binding citations vs. the spec header
python3 tools/vectors/check.py                  # W4 vectors, self-check
```

**CI runs all of these** on every pull request, and builds both V-B9 bad images. Run them
locally before spending bench time on the result.

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

**Permanent history is citable; moving state is not.** For the documents: `4250e00` (six
defects, including the duplicate `V-B2` and the B0-on-P8 gate), `ebdcf0d` (the
`LoRaBridge` retirement and the D33 bench-power reconciliation) and `8253085` (**P8**, with
D34's amendment and spec v0.11). **B2 is merged history**: `git log --oneline --merges -5
origin/main` finds the merge. **BF-15, BF-16 and B0's commits are not cited by SHA** until
they merge; the one exception below is the firmware provenance a board's banner prints.

**Merging B0 and B3: B3 first.** Merge B3's pull request without `--delete-branch`, retarget
B0's to `main` while it is still open, then delete B3's branch by hand.

**A push touching `.github/workflows/` needs workflow token scope.** Refused once, on
2026-09-08; accepted since. Try the push; if it is refused, the operator refreshes auth.

**Merging a stack: never pass `--delete-branch`.** Deleting a base branch **closes** the PR
stacked on it rather than retargeting it. Merge each PR without it, retarget the next to
`main` while it is still open, then delete branches by hand.

## Hardware state

**This table names the devices in this subproject's terms**; the range-test handoff owns
them in its own roles, and its rows do not transfer here.

| Device | Called here | Told apart by | Firmware / env | Stored state | Current state |
|---|---|---|---|---|---|
| Heltec WiFi LoRa 32 V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `bridge` / `heltec`, **USB-flashed from `cab05e8` on the B0 branch, a clean tree**: banner `Version: 0.1.0 (cab05e8)`, `Slot: app0`, `Image state: not_pending`, `Registry:` with six rows. It ran `simnode-heltec` as `f1` for the B0 on-air check in between | NVS: nothing this node depends on yet | On USB to the macOS build machine, last seen as `/dev/cu.usbserial-0001`. **Receives on 917.4 MHz; transmits nothing.** Broker unreachable at the last boot |
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `simnode` / `simnode-heltec`, **USB-flashed from `c3ef4ca` (BF-9)**, MAC `44:1b:f6:fa:bc:2c`. Boots as `f0 ROLE_RANGE` and `f2 ROLE_HEALTH`, with `OLED: up`. **It ran an old range-test image (spec v0.8 banner) until 2026-09-14** | Nothing persists; identities reset on every boot. Any range-test NVS from before is not the only copy of anything | On USB to the macOS build machine, last seen as `/dev/cu.usbserial-3` |
| XIAO ESP32S3 + **Wio-SX1262 Kit** (p-5982, B2B) | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `range-test` / `xiao`. **`simnode-xiao-wio` builds and has never been flashed** | B1b position log, dumped and committed | Powered down. **Not with the operator offsite as of 2026-09-14**, so its first simnode flash waits for the bench |

**The bridge board still transmits nothing on its own.** `lora_task` sends only what the TX
queue holds, and nothing queues a frame until BF-17 or BF-18.

**A USB flash puts the bridge board back in a known state.** `pio run -t upload -e heltec`
writes the bootloader, the table, `boot_app0.bin` (which resets `otadata` to `app0`) and
the image. Flash from a committed tree: a `-dirty` git field on the banner means the running
image matches no commit.

**A wrong board selection is silent.** It writes the wrong pin map into a normal-looking
artifact. **Tell the two Heltecs apart by enclosure** — both CP2102 bridges report
`SER=0001`, and the port name is not stable across replug.

**No stored state on any of these boards is the only copy.** Every survey site and every
B1b position is committed under `docs/rangetest/data/`.

**`secrets.h` on the macOS build machine holds a real master key**: the 2026-09-14 boot
printed no placeholder warning. It is gitignored and uncommitted; never read it into a
command, a log or a commit.

**Its antenna stays on it.** The bridge uses the range test's 3.0 dBi 19 cm stick (Bridge
PRD **R-4.3a.1**); the gain is a term in D1's EIRP arithmetic, and `radio_config.h`
asserts the sum at compile time.

## Behaviour that changed, and will make older artifacts read differently

- **`media_access` and the PHY constants moved to `lib/lran-link/` on the B0 branch.**
  BF-16-era text placing `media_access.{h,cpp}` in `firmware/bridge/src/`, or its seven tests
  in `test_lora`, is correct for when it was written.
- **The handheld Heltec is a simnode from 2026-09-14.** Older text calls it the range test's
  board and says it never runs a simnode build.
- **The ladder refuses unregistered sources since BF-15.** Before it, a frame from any
  `src` could be delivered and take a reassembly slot. BF-16's text describing slots for
  any peer is correct for when it was written.
- **The boot banner gains a `Registry:` line since BF-15**, and its last line names BF-15.
- **`RxMessage` carries a decoded header and complete payload since BF-16**, not raw frame
  bytes. BF-11-era text describing `app_task` as the decoder is correct for when it was
  written.
- **`TaskSpec::stack_words` is `stack_bytes` since BF-16**, and Impl Plan §5.2.1's column
  reads bytes from v0.22. Only `lora` changed, 4096 → 8192.
- **The OTA verdict requires `radio_ok` since BF-16.** V-B9's 2026-09-13 pass tested the
  verdict before that change.
- **Impl Plan §5.3 no longer places `registry.cpp` in `sched_task`**, from v0.23.
- **V-B12 moved from B2 to B3 in Impl Plan v0.21.** An older revision lists it under B2;
  that listing was a defect, not a different plan.
- **`LoRaBridge` is retired in favour of `Bridge Node`, 2026-09-10** (**D17** amended).
  `lran-bridge` remains the firmware target and **"LoRa Bridge" remains the HA device
  name**.
- **The PHY parameters are stated rather than deferred, 2026-09-10.** A document revision
  citing Protocol Spec v0.9 or earlier reads §12.1 as "per D1" and `backoff_max_ms` as 500.

## Traps that cost real time here

- **Opening a Heltec's serial port reboots it, even with DTR and RTS held low.** Arm faults
  after the banner, in the same connection you check them from.
- **Two simnode Heltecs boot with the same identities** (`f0`, `f2`), and opening either
  serial port resets its board to them. Reconfigure one in the same session that runs the
  test; `firmware/simnode/CLAUDE.md` has the rest.
- **`lib_extra_dirs = ..` in a library's own test project loses `unity.h`.** Use
  `lib_deps = symlink://../<dep>`, as `lib/lran-link/platformio.ini` does.
- **A simnode PING to `00` reports no echo.** The bridge does not answer PING yet.
- **V-B9 needs the broker.** The verdict requires `mqtt_connected`, so with the broker down
  a good image rolls back and reads as a firmware failure.
- **An `RxLadder` with no `PeerKeys` refuses every frame**, as `unregistered_src`. A new
  ladder test that expects delivery must register its sources (`test_lora`'s `AnySource`).
- **`unregistered_src` is not a §14.1 counter** and is outside `rx_dropped`. Do not rename
  it to an `rx_` name before spec v0.12 decides.
- **`registry_begin()` must run before `start_tasks()`**, and **`lora_task` must never call
  `registry_runtime`**, which waits on a mutex. The never-block check reads `registry.cpp`
  but deliberately not `registry_runtime.cpp`.
- **The library's platform crypto is not in its build.** `platform/esp32/` and
  `platform/native/` are added by each firmware's `build_src_filter`; a new firmware that
  forgets gets an undefined `MbedtlsKdf` at link.
- **The codec returns `Ok` for an authenticated frame it had no key to check.** Test
  `mac_verified`, never the status alone. `RxLadder` does; anything else decoding frames
  must too.
- **ESP-IDF stack depth is bytes.** Upstream FreeRTOS documentation says words, and BF-11
  followed it. Size a stack from `uxTaskGetStackHighWaterMark`, and read it as a range: two
  boots differed by 248 bytes.
- **RadioLib's `scanChannel()` has no timeout and `transmit()` busy-waits.** Start the
  operation and read the IRQ register against a deadline, as `lora_link` does.
- **Receive routes only `RX_DONE` to DIO1.** `HEADER_VALID` and `HEADER_ERR` are in the
  register but never wake the task; `lora_link` reads them on a 1 s poll and before a CAD.
- **Never ask `getPacketLength()` whether a packet arrived.** It holds the last length and
  is not cleared. Gate on `RX_DONE`.
- **RadioLib's `SPIClass` `Module` constructor allocates on the heap.** Construct an
  `ArduinoHal` in static storage and use the constructor that takes one.
- **The broker address in `secrets.h` was once a Tailscale address.** The build machine
  reaches it over its tunnel; the bridge cannot. Use the broker's LAN address.
- **`4WAY_HANDSHAKE_TIMEOUT` on every WiFi attempt is a wrong passphrase**, not a range
  problem.
- **The serial log is silent on WiFi and MQTT state.** Only the OLED and the broker show it;
  an unreachable broker shows in the log only as `WiFiClient` connect timeouts.
- **The sandbox broker refuses anonymous clients** (`CONNACK 5`). The operator runs the
  subscription; a prompt keeps the password out of history. Set `BROKER` and `MQTT_USER`
  first:
  `read -rs 'P?MQTT password: ' && echo && mosquitto_sub -h "$BROKER" -u "$MQTT_USER" -P "$P" -t 'lran/bridge/#' -v -F '%I %t %p'; unset P`
- **An OTA upload can fail 1 s in with `Receive Failed`** while espota's progress bar keeps
  climbing — that is the Mac's send buffer, not the bridge. Retry once before debugging.
- **V-B9's bad images print the same version as step 2's good image.** Read `Slot:`, the
  V-B9 banner and the `WiFi SSID:` line to tell them apart.
- **Opening the serial port can press PRG.** GPIO 0 is on the CP2102's DTR. Construct the
  port unopened and set `dtr = False` before opening.
- **`pio test -e esp32s3` overwrites whatever the board was running.** Identify the board
  before the upload.
- **A C++ `verifyRollbackLater()` links cleanly and does nothing.** It must be
  `extern "C"`. `bridge_partitions.py --elf` is the check; V-B9 is the proof.
- **`MQTT_MAX_PACKET_SIZE` defaults to 256 bytes in PubSubClient.** Set it ≥ 1024.
- **HA's entity registry remembers every `unique_id`, and a retained discovery config
  survives a reflash.** Develop against the dev HA VM and dev broker until **B6**.
- **The Heltec V3's TCXO runs at 1.8 V; the OLED sits behind Vext; the vendor header's
  `DIO0` on GPIO 14 is the SX1262's DIO1.** All three fail without an error.

## Open, and not closable from here

- **B0's criteria** — "console accepts every command" and "faults arm, fire, self-disarm,
  OLED". BF-6 remains.
- **Frames to and from the bridge on air** — DIO1 waking `lora_task`, a key verifying,
  frames both ways. The simnode can now transmit; the bridge still logs nothing per frame
  and answers nothing until BF-17, BF-18 or a PING responder.
- **Spec §17.3: the bridge answers no PING.** RF loopback is "required of every node build"
  and no `BF-*` task assigns it to the bridge.
- **The XIAO simnode profile on hardware** — builds, never flashed.
- **V-B9 re-run** — owed since BF-16 changed the verdict; needs the broker.
- **Spec gap for v0.12: a frame from an unregistered source** — §14 has no stage for it.
  The bridge counts `unregistered_src` meanwhile (engineering log, 2026-09-14).
- **Spec gap for v0.12: §16.2's `lran/bridge/version` payload.**
- **Spec §12.1's node-address filtering** — RadioLib 7.7.1 has no SX126x setter, and the
  reading that the part filters only in GFSK is unverified against the datasheet. Needs a
  specification decision before a duty-cycled node relies on it (§17.1).
- **Decoding per schema has no task** — Impl Plan §5.3's `decode/`; `app_task`'s `TODO`
  gives it to BF-24.
- **Six task stacks unmeasured** — every size but `lora` is still BF-11's figure.
- **The OTA first-data timeout** — one failure in two uploads on 2026-09-13. If it recurs,
  decide between `ArduinoOTA.setTimeout()` and `WiFi.setSleep(false)`; the second changes
  what M22 measures.
- **R-5.3d on hardware** — an OTA upload deferred during a LoRa transaction.
- **A serial log line per network state change** — proposed, not assigned to a task.
- **BF-11a** (log queue drain) and **BF-11b** (hardware watchdog from `sched_task`).
- **What GateLink's `COMMAND_ACK` waits for** — pulse complete, or gate confirmed. GateLink
  **M3** needs the answer.
- **M22 / V-B12** — bridge LoRa PER with WiFi idle versus saturated, under B3.
- **GateLink M0's LDO margin** — sized for Envelope B's 19.6 dBm, tested only at −4 dBm.
- **The range-test firmware still transmits on the provisional 915.0 MHz**, which is why it
  could not stand in for simnode B0.

### Closed, and not to be reopened by habit

- **B2 — accepted and merged, 2026-09-13.** Bench record in the engineering log.
- **D1 and D33 — CLOSED 2026-09-10.** 917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm
  conducted, Envelope A. Reopening needs a new fact — a `cad_backoffs` reading the channel
  does not explain, or an Envelope B trigger — not a re-reading of B1b.
- **M19 — DONE 2026-09-10.** Do not regenerate §15.1's table expecting different numbers.
- **M6 — CLOSED 2026-09-09.** Gate **~87 m**, well **~100 m**. No well walk is owed.
- **M20 — CLOSED 2026-09-05.** Re-walking to rank channels would be an expensive, invisible
  mistake; the tool does it from the committed trace.
- **M21 — CLOSED 2026-09-06.** §15.23 home-built.
- **W4, W5, W9, W12 — closed.** §18.1 is annotated, not rewritten, and must not be read
  alone.
- **D31 — closed 2026-09-08.** Copyright holder is Robert J. Lee.
- **D32 — closed.** RadioLib, pinned. Run `tools/rangetest/check_pa_table.py` after any
  version bump, and re-read `lora_link.cpp`, which reads the SX126x IRQ register directly.
