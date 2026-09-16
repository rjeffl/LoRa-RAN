# Bridge Node — session handoff

**Written 2026-09-15, at the end of the bench session that re-ran V-B9, flashed the XIAO,
had B0 accepted, and put B3a on air.** It replaces the 2026-09-14 file wholesale; that
file's content is carried over where it is still true.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**Finish B3a's discard-counter pass, take the operator's acceptance, and merge the three-PR
stack.** Everything else B3a needs is done and recorded (engineering log, 2026-09-15).

**Read this file on `b3-poll-scheduler`, the top of the stack.** Every lower branch's copy is
older.

```bash
git fetch origin -p
git switch b3-poll-scheduler && git pull --ff-only
gh pr list --state open
for n in $(gh pr list --state open --json number -q '.[].number'); do gh pr checks "$n"; done
```

### Do these in order

1. **The §10.5 discard catalogue, by hand** (Impl Plan §10.5, the `fault list` output). One
   entry at a time from a simnode, reading `lran/bridge/diag/state` at the broker after each:
   **exactly the named counter moves**, and `rx_dropped` moves only for the counters §14.1
   marks yes. Four are done — `bad_crc`, `wrong_dst` (as `rx_not_addressed`), `hdr_rsv`
   accepted, and `frag_*` untouched — so the rest of the list is the work. Stage 1 is not
   injectable. **A fault's `dst` defaults to the bridge.**
2. **B3a acceptance by the operator**, against Impl Plan §8's B3a row.
3. **Merge bottom-up, in the same sitting.** Update each PR's criteria section first and mark
   it ready.
   ```bash
   gh pr ready 59 && gh pr merge 59 --merge          # never --delete-branch
   gh pr edit 60 --base main && gh pr ready 60 && gh pr merge 60 --merge
   gh pr edit 61 --base main && gh pr ready 61 && gh pr merge 61 --merge
   git push origin --delete b3-protocol-registry b0-simnode-bringup b3-poll-scheduler
   ```
   Retarget each PR **before** deleting the branch under it, or GitHub closes it.
4. **Then B3b, from `main`.** A spec v0.12 revision is B3b's real gate: BF-18, BF-19a and
   BF-26 each wait on it (*Spec v0.12*, below). BF-21's `simctl` scripts and BF-22's version
   tolerance need no spec change and can start on a new branch meanwhile.

**Set the bench up like this.** The XIAO carries `f0` `ROLE_RANGE`, `f1` `ROLE_GATELINK`,
`f2` and `f3` `ROLE_HEALTH`; the handheld Heltec carries nothing, so its identities do not
collide. A bench identity is enrolled at the bridge only once it has spoken: `push f1` for
the GateLink role, or `fault <id> hdr_rsv` for any other, which the bridge accepts and counts
nowhere.

## What the last session established

**Every item below is in the engineering log's 2026-09-15 entry, with the numbers.**

- **V-B9 passed all four steps**, on the stack's tip. **Its one gap:** the no-network image's
  radio came up, so the run proves the network half of the verdict, not the `radio_ok` half
  BF-16 added.
- **B0 was accepted by the operator.** The XIAO ran a simnode image for the first time, four
  identities loaded on it at once, and every console command was typed on a board. #60's
  criteria section is the clause-by-clause record.
- **B3a is on air.** Four simnode identities from one board polled and `online` at once;
  V-B3 passed both ways; retained `offline` for both production nodes seen at the broker;
  `bad_crc`, `wrong_dst` and an accepted `hdr_rsv` read back from `lran/bridge/diag/state`.
- **Poll-to-answer times are measured**: 522–606 ms for a schema `0xF0` answer and 793–1013 ms
  for `ROLE_GATELINK`'s `0xFE`, against a 10 000 ms window, on a quiet bench at about 1 m.
- **B3a needed an instrument, and it was added** (`28ffd82`): `PollScheduler::on_heard()`
  returns the poll-to-answer time and `sched_on_heard()` prints it. One new host test.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the 2026-09-15 entry first: V-B9, B0, B3a, the poll instrument, `ROLE_FAULT` and POLL |
| 3 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§8** owns B3a's and B3b's criteria; **§10.5** is the fault catalogue; **§6.1.1–§6.1.2** BF-17 and BF-20; **§4.3.2** BF-19; **§10** the simnode |
| 4 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | §6 is B3a's and B3b's tasks, with BF-19a new and BF-26 (§7) deferred |
| 5 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and what breaks silently |
| 6 | [`firmware/simnode/CLAUDE.md`](../../firmware/simnode/CLAUDE.md) | what the simnode has, what it does not, and its traps |
| 7 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) §9–§12, §14 | keys, context, reassembly, radio, the discard ladder. **§18.2, never §18.1 alone** |
| 8 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**, **B0**. **M6**, **M19**, **M20**, **M21**. **D1**, **D33**; **D34 amended**. **V-B9 re-run**. **BF-2**–**BF-9**, **BF-15**–**BF-17**, **BF-19**, **BF-20** built and, apart from BF-19a's split-off, exercised on the bench |
| Not done | **B3a**: the §10.5 catalogue by hand, then acceptance and the merges. **B3b**: BF-18, BF-19a, BF-21, BF-22. **BF-26** deferred. **BF-11a**, **BF-11b** |
| Queue | *The next job*'s four steps: the catalogue → B3a accepted → merge #59, #60, #61 → spec v0.12, then B3b |

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
(**P8**, with D34's amendment and spec v0.11) and `28ffd82` (the poll-to-answer instrument,
on `b3-poll-scheduler`).

**Merging a stack: never pass `--delete-branch`.** Deleting a base branch **closes** the PR
stacked on it rather than retargeting it. Merge each PR without it, retarget the next to
`main` while it is still open, then delete branches by hand.

**A push touching `.github/workflows/` needs workflow token scope.** Refused once, on
2026-09-08; accepted since. Try the push; if it is refused, the operator refreshes auth.

## Hardware state

**This table names the devices in this subproject's terms**; the range-test handoff owns
them in its own roles, and its rows do not transfer here.

| Device | Called here | Told apart by | Firmware / env | Stored state | Current state |
|---|---|---|---|---|---|
| Heltec WiFi LoRa 32 V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `bridge` / `heltec`, **USB-flashed from `28ffd82`, a clean tree**: banner `Version: 0.1.0`, `Slot: app0`, `Image state: not_pending`, `Registry:` with six rows | NVS: nothing this node depends on yet | On USB to the macOS build machine, last seen as `/dev/cu.usbserial-0001`. **Polls, receives and publishes**: WiFi, broker and radio all up |
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `simnode` / `simnode-heltec`, **USB-flashed from the stack's tip on 2026-09-15**, MAC `44:1b:f6:fa:bc:2c` | Nothing persists; identities reset on every boot | On USB, last seen as `/dev/cu.usbserial-4`. Its identities were deleted so the XIAO could hold `f0`–`f3`; a reboot brings back `f0` and `f2` |
| XIAO ESP32S3 + **Wio-SX1262 Kit** (p-5982, B2B) | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `simnode` / `simnode-xiao-wio`, **first flashed 2026-09-15**, MAC `68:ee:8f:4b:85:f4`. Native USB, so it enumerates as `/dev/cu.usbmodem*` | B1b position log, dumped and committed | On USB. Carries `f0` `ROLE_RANGE`, `f1` `ROLE_GATELINK`, `f2` and `f3` `ROLE_HEALTH`, all enrolled and `online` at the bridge |

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
**The OTA password is read from `LRAN_OTA_PASSWORD`** in the shell that runs the upload —
prompt for it with `read -rs` rather than putting it in a file or a history line.

**Its antenna stays on it.** The bridge uses the range test's 3.0 dBi 19 cm stick (Bridge
PRD **R-4.3a.1**); the gain is a term in D1's EIRP arithmetic, and `radio_config.h` asserts
the sum at compile time.

## Behaviour that changed, and will make older artifacts read differently

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

- **`ROLE_FAULT` answers no `POLL`** (`node.cpp`). An identity in that role, once enrolled,
  always goes `offline` after three missed polls. Use `ROLE_HEALTH` for an identity that must
  answer, and expect the offline line when arming faults on a polled one.
- **A bench identity is polled only after the bridge has heard it.** `push` works for
  `ROLE_GATELINK`; `fault <id> hdr_rsv` announces any role and moves no counter.
- **Two simnode boards boot with the same identities** (`f0`, `f2`), and opening either serial
  port resets its board to them. Reconfigure one in the same session that runs the test.
- **Opening a Heltec's serial port reboots it, even with DTR and RTS held low.** Arm faults
  after the banner, in the same connection you check them from. The XIAO's native USB does
  not reboot on open, so its banner is missed unless the port is already open.
- **`AUTH_FAIL` (reason 202) on the bridge's first WiFi attempt at every boot** is expected as
  of 2026-09-15, five boots out of five; a later attempt connects. Unexplained, harmless so
  far, and invisible except in the serial log.
- **V-B9 needs the broker.** The verdict requires `mqtt_connected`, so with the broker down a
  good image rolls back and reads as a firmware failure.
- **V-B9's bad images print the same version as the good image.** Read `Slot:`, the V-B9
  banner and the `WiFi SSID:` line to tell them apart.
- **A bench counter check must be read at the broker**, not from the console: `diag/state`
  publishes every 60 s, and a reflash resets every counter to zero.
- **`lib_extra_dirs = ..` in a library's own test project loses `unity.h`.** Use
  `lib_deps = symlink://../<dep>`, as `lib/lran-link/platformio.ini` does.
- **A simnode PING to `00` reports no echo.** The bridge does not answer PING yet.
- **An `RxLadder` with no `PeerKeys` refuses every frame**, as `unregistered_src`.
- **`unregistered_src` is not a §14.1 counter** and is outside `rx_dropped`. Do not rename it
  to an `rx_` name before spec v0.12 decides.
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
- **The sandbox broker refuses anonymous clients** (`CONNACK 5`). Keep the password out of
  history:
  `read -rs 'P?MQTT password: ' && echo && mosquitto_sub -h "$BROKER" -u "$MQTT_USER" -P "$P" -t 'lran/#' -v -F '%I %r %t %p'; unset P`
- **A retained message read at subscribe time is not evidence of this boot.** Compare the
  `version` payload, or wait for a live publication.
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

- **B3a's acceptance** — *The next job*, steps 1–2.
- **A key verifying a frame on air waits for BF-18** (B3b): nothing a node sends the bridge
  carries a MAC today.
- **The `radio_ok` half of the OTA verdict is untested on hardware.** V-B9's images fail by
  network or by panic; no image with a dead radio exists.
- **`poll_reply_timeout_ms` = 10 000 now has bench evidence** (522–1013 ms at 1 m, no
  contention) but no measurement under contention or at range. M22 / V-B12 is where that lands.

#### Spec v0.12 — every open question, in one place

B3b's gate. Each is raised in the engineering log entry named; none is patched.

| # | Question | Raised by | Blocks |
|---|---|---|---|
| 1 | §14 has no stage for a frame from an unregistered source | BF-15 | naming `unregistered_src` |
| 2 | How a `DUPLICATE_CACHED` `COMMAND_ACK` carries the cached result | BF-6 | **BF-18** |
| 3 | What a node answers to a repeated `CONFIG` | BF-6 | BF-18, GateLink |
| 4 | §7.4 relies on fragmenting config sets that §3.1's 196-byte reassembly cap rules out | BF-6 | GateLink config |
| 5 | §10.2 places a bridge-originated unauthenticated frame (`POLL`'s `seq`) in neither sequence space | BF-17 | nothing yet |
| 6 | §14.1's "per node by the bridge" for a discard made before the MAC check | BF-19 | per-node counters |
| 7 | Whether the bridge must send §14's `ERROR` replies, and to which `src` and `ctx_id` before the MAC is checked | BF-19 | **BF-19a** |
| 8 | §16.2 names `lran/bridge/version`, `lran/<node>/diag/state`, `config/set` and `config/ack` but defines no payload | BF-13, BF-19, BF-26 | **BF-26**, BF-23 |
| 9 | §12.1's node-address filtering appears unavailable in LoRa mode; unverified against the datasheet | BF-16 | duty-cycled nodes (§17.1) |

#### Work no task owns

- **`/lib/lran-config/`** — System PRD §9.4 describes it; BF-26 and BF-23 need it.
- **The bridge's MQTT receive path** — subscribe exists, no callback or inbound queue; BF-18
  and BF-26 need it.
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

- **B0 — accepted 2026-09-15.** Bench record in the engineering log and in #60.
- **V-B9 — re-run and passed 2026-09-15**, with the `radio_ok` gap noted above.
- **B2 — accepted and merged, 2026-09-13.**
- **D1 and D33 — CLOSED 2026-09-10.** 917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted,
  Envelope A. Reopening needs a new fact, not a re-reading of B1b.
- **M19 — DONE 2026-09-10.** Do not regenerate §15.1's table expecting different numbers.
- **M6 — CLOSED 2026-09-09.** Gate **~87 m**, well **~100 m**. No well walk is owed.
- **M20 — CLOSED 2026-09-05.** The tool ranks channels from the committed trace.
- **M21 — CLOSED 2026-09-06.** §15.23 home-built.
- **W4, W5, W9, W12 — closed.** §18.1 is annotated, not rewritten, and must not be read alone.
- **D31 — closed 2026-09-08.** Copyright holder is Robert J. Lee.
- **D32 — closed.** RadioLib, pinned. Run `tools/rangetest/check_pa_table.py` after any
  version bump.
