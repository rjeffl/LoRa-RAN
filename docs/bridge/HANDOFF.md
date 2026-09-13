# Bridge Node — session handoff

**Written 2026-09-13, at the end of the session that ran B2's bench session, merged B2 and
built BF-16.** BF-16 is the first B3 task and has not run on a board. It replaces the
earlier 2026-09-13 file wholesale; that file's B2 content is carried over where it is
still true.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**Read this file on the B3 milestone branch, `b3-protocol-registry`, not on `main`.** B3's
pull request stays a draft until the operator accepts the milestone, so `main`'s copy of
this file predates BF-16.

```bash
git fetch origin -p
git switch b3-protocol-registry && git pull --ff-only
gh pr list --state open
```

**Three jobs are open. Start with the first; it needs only the bridge board.**

1. **BF-16's on-air bring-up, and V-B9 again.** USB-flash the branch to the flat-case
   Heltec and read three lines off the boot log: `LoRa: radio up - 917400000 Hz, SF9, ...`,
   `LoRa: stack high-water N bytes free`, and no repeating `LoRa: radio down`. **Then re-run
   Impl Plan §6.5.2**: BF-16 changed `ota_policy.cpp`, so V-B9's 2026-09-13 pass no longer
   covers the verdict in the image. Receiving and sending frames needs job 3's second
   transmitter.
2. **BF-15, the registry.** It supplies `PeerKeys` and an `IMac` to `lora_set_auth()`
   (call it before `start_tasks()`), and it restricts reassembly slots to registered nodes
   (`TODO(BF-15)` in `rx_ladder.h`). Opus, per the Tasks document.
3. **Simnode B0.** No library gate is left since P8. Its command path calls
   `CommandGate::check()` before dispatch and sends `COMMAND_ACK` only after `record()`; on
   `InFlight` it sends nothing (spec §9.4 v0.11). **It is also the second transmitter on
   917.4 MHz / SF9 that BF-16 needs**: the range-test firmware sits on 915.0 MHz. B3
   cannot finish without it.

## What the last session established

**BF-16, 2026-09-13.** The bridge engineering log's BF-16 entry and Impl Plan §5.3.1 have
the full account.

- **The radio link is built in four pieces**: `radio_config.h`, `rx_ladder.{h,cpp}`,
  `media_access.{h,cpp}` and `lora_link.{h,cpp}`. The middle two are Arduino-free and
  covered by 27 host tests in `test_lora`. `lora_link` is the only file that includes
  RadioLib.
- **`lora_task` runs spec §14 stages 1–10** and queues a decoded header with the complete
  payload. A backoff is state, so `lora_task` receives through it.
- **The codec skips MAC verification when it has no key** and returns `Ok` with
  `mac_verified = false`. `RxLadder` refuses any frame that should carry a MAC and was not
  verified. The library is unchanged.
- **ESP-IDF task stacks are bytes; BF-11 sized them as words.** Every task had a quarter
  of the stack intended. `lora_task` is now 8192 bytes; **the other six are unmeasured.**
- **The OTA verdict requires `radio_ok`.** An image whose radio never initialises rolls
  back at the deadline.
- **Not supported by this session:** that the radio is configured as `radio_config.h`
  says, that DIO1 wakes `lora_task`, or that any frame goes out or comes in.
  `begin()` succeeding would prove none of it.

**B2's bench session, 2026-09-13.** The engineering log's earlier 2026-09-13 entry has the
banner lines verbatim.

- **All seven B2 criteria passed** on the flat-case Heltec: WiFi and MQTT reconnect
  without a reboot, the LWT fired on unplug and on a WiFi drop, A/B partitioning, OTA,
  both bad images rolled back, version published, OLED page.
- **One OTA upload failed** with `Receive Failed` 1 s after it started; the retry passed in
  8.5 s. **Suspected, not proven:** WiFi modem sleep against ArduinoOTA's 1000 ms
  first-data timeout. Nothing was changed for one failure; the fix candidates touch M22's
  premise.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the two 2026-09-13 entries: the bench session, then BF-16's decisions and findings |
| 3 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§5.3.1** for BF-16; **§8** owns B3's acceptance criteria; **§6.5.2** is V-B9, owed again; §10 is the simnode |
| 4 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | B3's task order — BF-15 to BF-22 — and which model each suits |
| 5 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and what breaks silently |
| 6 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) §3.2.1 | D34's amendment, which the simnode's command path follows |
| 7 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) §9.4, §11, §12, §14 | replay, reassembly, radio, the discard ladder. **§18.2, never §18.1 alone** |
| 8 | [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md) | the requirements; §8's `V-B*` rows are what a milestone is checked against |
| 9 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**. **M6**, **M19**, **M20**, **M21**. **D1**, **D33**; **D34 amended**. **BF-16 built**, host-tested only |
| Not done | **B3**, every criterion. **V-B9's re-run.** **BF-11a**, **BF-11b** |
| Queue | BF-16 on-air bring-up and V-B9 → **BF-15** → simnode **B0** → BF-17 to BF-22 |

```bash
pio test -d lib/lran-protocol -e native         # library host suite
pio test -d firmware/bridge -e native           # bridge host suites, test_lora among them
pio run  -d firmware/bridge -e heltec           # bridge target - NEEDS secrets.h
python3 tools/checks/lora_task_never_blocks.py  # lora_task blocks on nothing; reads all three LoRa files
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
origin/main` finds the merge, and its BF-10 to BF-14 commits read in order. **BF-16 is not
cited by SHA** until it merges.

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
| Heltec WiFi LoRa 32 V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `bridge` / `heltec`, **B2's code, USB-flashed from a clean tree after V-B9**: banner `Version: 0.1.0`, no `-dirty`, `Slot: app0`, `Image state: not_pending`. **No BF-16 radio code on it yet** | NVS: nothing this node depends on yet | On USB to the macOS build machine, connected to the sandbox broker. **Transmits nothing** |
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `range-test` / `heltec`, **or P8's Unity test image** — which Heltec took that image on 2026-09-11 is not recorded. Never a simnode build | Whether its stored survey campaign was erased is not recorded. Irrelevant to this node | Powered down |
| XIAO ESP32S3 + **Wio-SX1262 Kit** (p-5982, B2B) | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `range-test` / `xiao`. Never a simnode build | B1b position log, dumped and committed | Powered down |

**Flashing BF-16 still transmits nothing on its own.** `lora_task` sends only what the TX
queue holds, and nothing queues a frame until BF-17 or BF-18. It will receive, and it will
print the radio-up and stack lines.

**A USB flash puts the bridge board back in a known state.** `pio run -t upload -e heltec`
writes the bootloader, the table, `boot_app0.bin` (which resets `otadata` to `app0`) and
the image. Flash from a committed tree: a `-dirty` git field on the banner means the running
image matches no commit.

**A wrong board selection is silent.** It writes the wrong pin map into a normal-looking
artifact. **Tell the two Heltecs apart by enclosure** — both CP2102 bridges report
`SER=0001`, and the port name is not stable across replug.

**No stored state on any of these boards is the only copy.** Every survey site and every
B1b position is committed under `docs/rangetest/data/`.

**`secrets.h` on the macOS build machine was corrected by the operator on 2026-09-13.** It
is gitignored and uncommitted; never read it into a command, a log or a commit.

**Its antenna stays on it.** The bridge uses the range test's 3.0 dBi 19 cm stick (Bridge
PRD **R-4.3a.1**); the gain is a term in D1's EIRP arithmetic, and `radio_config.h` now
asserts the sum at compile time.

## Behaviour that changed, and will make older artifacts read differently

- **`RxMessage` carries a decoded header and complete payload since BF-16**, not raw frame
  bytes. BF-11-era text describing `app_task` as the decoder is correct for when it was
  written.
- **`TaskSpec::stack_words` is `stack_bytes` since BF-16**, and Impl Plan §5.2.1's column
  reads bytes from v0.22. The numbers did not change except `lora`, 4096 → 8192; only the
  unit was corrected.
- **The OTA verdict requires `radio_ok` since BF-16.** V-B9's 2026-09-13 pass tested the
  verdict before that change.
- **V-B12 moved from B2 to B3 in Impl Plan v0.21.** An older revision lists it under B2;
  that listing was a defect, not a different plan.
- **`LoRaBridge` is retired in favour of `Bridge Node`, 2026-09-10** (**D17** amended).
  `lran-bridge` remains the firmware target and **"LoRa Bridge" remains the HA device
  name**.
- **The PHY parameters are stated rather than deferred, 2026-09-10.** A document revision
  citing Protocol Spec v0.9 or earlier reads §12.1 as "per D1" and `backoff_max_ms` as 500.
- **`V-B2` means the per-node registry verification and nothing else.** A revision before
  Bridge PRD v0.6 may use it for the coexistence measurement, which is now `V-B12`.

## Traps that cost real time here

- **The codec returns `Ok` for an authenticated frame it had no key to check.** Test
  `mac_verified`, never the status alone. `RxLadder` does; anything else decoding frames
  must too.
- **ESP-IDF stack depth is bytes.** Upstream FreeRTOS documentation says words, and BF-11
  followed it. Size a stack from `uxTaskGetStackHighWaterMark`.
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
- **The serial log is silent on WiFi and MQTT state.** Only the OLED and the broker show it.
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

- **BF-16 on air** — radio up, stack high-water mark, then frames both ways once simnode B0
  exists.
- **V-B9 re-run** — owed since BF-16 changed the verdict.
- **Spec §12.1's node-address filtering** — RadioLib 7.7.1 has no SX126x setter, and the
  reading that the part filters only in GFSK is unverified against the datasheet. Needs a
  specification decision before a duty-cycled node relies on it (§17.1).
- **Six task stacks unmeasured** — every size but `lora` is still BF-11's figure, now
  known to be bytes.
- **The OTA first-data timeout** — one failure in two uploads. If it recurs, decide between
  `ArduinoOTA.setTimeout()` and `WiFi.setSleep(false)`; the second changes what M22
  measures.
- **R-5.3d on hardware** — an OTA upload deferred during a LoRa transaction.
- **A serial log line per network state change** — proposed, not assigned to a task.
- **BF-11a** (log queue drain) and **BF-11b** (hardware watchdog from `sched_task`).
- **Specification gap for the next revision:** §16.2's `lran/bridge/version` payload.
- **What GateLink's `COMMAND_ACK` waits for** — pulse complete, or gate confirmed. GateLink
  **M3** needs the answer.
- **M22 / V-B12** — bridge LoRa PER with WiFi idle versus saturated, under B3.
- **GateLink M0's LDO margin** — sized for Envelope B's 19.6 dBm, tested only at −4 dBm.
- **The range-test firmware still transmits on the provisional 915.0 MHz**, which is also
  why it cannot stand in for simnode B0 against BF-16.

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
