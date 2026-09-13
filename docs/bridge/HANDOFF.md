# Bridge Node — session handoff

**Written 2026-09-13, at the end of the session that ran B2's bench session.** Every B2
criterion in Impl Plan §8 passed on the flat-case Heltec, V-B9 among them. It replaces
the 2026-09-11 file wholesale; that file's P8 and B2-code content is carried over where
it is still true.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**Read this file on the B2 milestone branch, not on `main`.** B2's pull request stays a
draft until the operator accepts the milestone, so `main`'s copy of this file predates the
bench session. `gh pr list --state open` shows it.

```bash
git fetch origin -p
git switch b2-board-bringup && git pull --ff-only
```

**B2 needs only the operator's acceptance: review the pull request and mark it ready.**
Nothing on the bench is owed. After that, two jobs are open, and neither needs the
broker to start:

1. **BF-16 (`lora_link.cpp`)** opens **B3**, on its own branch. It also closes the
   `TODO(BF-16)` in `ota_policy.cpp`: an image that cannot hear the fleet is bad too.
2. **Simnode B0**, which has no library gate left since P8. Its command path calls
   `CommandGate::check()` before dispatch and sends `COMMAND_ACK` only after `record()`;
   on `InFlight` it sends nothing (spec §9.4 v0.11). It needs a second board. B3 cannot
   finish without it.

**The bridge board runs a committed build**, USB-flashed at the end of the 2026-09-13
session (*Hardware state*). Check its banner's git field against the branch before B3's
first bench session.

## What the last session established

**2026-09-13 — B2's bench session.** The full record, with banner lines verbatim, is the
bridge engineering log's 2026-09-13 entry.

- **All seven B2 criteria passed.** WiFi connects and reconnects without a reboot; MQTT
  connects with the LWT registered; A/B partitioning; OTA; both bad images roll back;
  version published; OLED status page.
- **The BF-13 override works on hardware.** The first boot after an OTA read
  `Image state: pending_verify`, so the core did not bless the image before `setup()`.
  The good image was kept at 119.9 s; the no-network image rolled back at 89.6 s; the
  panic image printed its banner once and the bootloader rolled it back.
- **The LWT fired twice**: on USB unplug, and 19 s after a WiFi drop at the access point.
- **One OTA upload failed** with `Receive Failed` exactly 1 s after it started, and the
  retry uploaded in 8.5 s. **Suspected, not proven:** WiFi modem sleep against
  ArduinoOTA's 1000 ms first-data timeout. Nothing was changed for one failure. The fix
  candidates touch M22's premise, so a recurrence is a decision, not a bench fix.
- **Not supported by this session:** anything about the radio. There is none yet, so
  R-5.3d (an OTA upload deferred during a LoRa transaction) is untested.

**2026-09-11 — P8, and B2 moved onto it.** P8 is built and merged on D34 as amended
(Decision Register §3.2.1). Protocol Spec is v0.11, with no wire change. B2's branch was
rebased onto the P8 merge.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`engineering-log.md`](./engineering-log.md) | the 2026-09-13 entry for the bench results and the OTA watch item; the 2026-09-10 entries for why B2's code is shaped as it is |
| 3 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | what to build next, in what order: BF-16 onward |
| 4 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **owns B0–B7 and their acceptance criteria (§8)**; §10 is the simnode; §6.5.2 is V-B9, to re-run after any OTA change |
| 5 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) §3.2.1 | D34's amendment, which the simnode's command path must follow |
| 6 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and the two OTA details that break silently |
| 7 | [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md) | the requirements; §8's `V-B*` rows are what a milestone is checked against |
| 8 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) §9.4, §10.4, §12, §14, §16 | replay and dedup, radio, the discard ladder, MQTT. **§18.2, never §18.1 alone** |
| 9 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**. **B2**, bench included, **V-B9** within it. **M6**, **M19**, **M20**, **M21**. **D1**, **D33**; **D34 amended** |
| Not done | **B2's acceptance by the operator.** **BF-11a**, **BF-11b**, split out |
| Queue | **BF-16** → B3. Simnode **B0** |

```bash
pio test -d lib/lran-protocol -e native         # library host suite
pio test -d lib/lran-protocol -e esp32s3        # same suite on a Heltec over USB - overwrites the board
python3 tools/vectors/check.py                  # W4 vectors, self-check
python3 tools/checks/spec_citation_version.py   # binding citations vs. the spec header
pio test -d firmware/bridge -e native           # bridge host suite, no secrets
pio run  -d firmware/bridge -e heltec           # bridge target - NEEDS secrets.h
python3 tools/checks/lora_task_never_blocks.py  # lora_task blocks on nothing
python3 tools/checks/bridge_partitions.py       # A/B table; add --firmware/--elf after a build
```

**CI runs all of these except the on-target suite**, on every pull request, and builds both
V-B9 bad images. Run them locally when you are about to spend bench time on the result.

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
D34's amendment and spec v0.11). **B2's commits are not cited by SHA**: the branch was
rebased on 2026-09-11 and is not permanent history until it merges.
`git log --oneline origin/main..origin/b2-board-bringup` lists them.

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
| Heltec WiFi LoRa 32 V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `bridge` / `heltec`, **USB-flashed from a clean tree after V-B9**: banner `Version: 0.1.0`, a git field with no `-dirty`, `Slot: app0`, `Image state: not_pending`. `app1` still holds step 2's `0.1.1` test image, unreachable because the USB flash reset `otadata` to `app0` | NVS: nothing this node depends on yet | On USB to the macOS build machine, 2026-09-13, connected to the sandbox broker. **Transmits nothing** — no radio code |
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `range-test` / `heltec`, **or P8's Unity test image** — which Heltec took that image on 2026-09-11 is not recorded, and the flat-case board was USB-flashed on 2026-09-13 without its prior firmware being read. Never a simnode build | Whether its stored survey campaign was erased is not recorded. Irrelevant to this node | Powered down |
| XIAO ESP32S3 + **Wio-SX1262 Kit** (p-5982, B2B) | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `range-test` / `xiao`. Never a simnode build | B1b position log, dumped and committed | Powered down |

**A USB flash puts the bridge board back in a known state.** `pio run -t upload -e heltec`
writes the bootloader, the table, `boot_app0.bin` (which resets `otadata` to `app0`) and
the image. Flash from a committed tree: a `-dirty` git field on the banner means the
running image matches no commit.

**A wrong board selection is silent.** It writes the wrong pin map into a normal-looking
artifact. **Tell the two Heltecs apart by enclosure** — both CP2102 bridges report
`SER=0001`, and the port name is not stable across replug.

**No stored state on any of these boards is the only copy.** Every survey site and every
B1b position is committed under `docs/rangetest/data/`.

**`secrets.h` on the macOS build machine was corrected by the operator on 2026-09-13.** It
is gitignored and uncommitted; never read it into a command, a log or a commit.

**Its antenna stays on it.** The bridge uses the range test's 3.0 dBi 19 cm stick (Bridge
PRD **R-4.3a.1**); the gain is a term in D1's EIRP arithmetic, so a different antenna is a
decision to record, not a swap.

## Behaviour that changed, and will make older artifacts read differently

- **V-B12 moved from B2 to B3 in Impl Plan v0.21**, 2026-09-13. An older revision lists it
  under B2; that listing was a defect, not a different plan.
- **`LoRaBridge` is retired in favour of `Bridge Node`, 2026-09-10** (**D17** amended).
  `lran-bridge` remains the firmware target and **"LoRa Bridge" remains the HA device
  name**. A spec revision before v0.10 still reads `(LoRaBridge)`.
- **The PHY parameters are stated rather than deferred, 2026-09-10.** A document revision
  citing Protocol Spec v0.9 or earlier reads §12.1 as "per D1" and `backoff_max_ms` as 500.
- **`+22 dBm` no longer appears as an operating point** in either node's documents. A
  revision that still states it is correct for when it was written.
- **`V-B2` means the per-node registry verification and nothing else.** A revision before
  Bridge PRD v0.6 may use it for the coexistence measurement, which is now `V-B12`.

## Traps that cost real time here

- **The broker address in `secrets.h` was a Tailscale address.** The build machine reaches
  it over its tunnel, so it tests fine from a laptop; the bridge has no route to it. Use
  the broker's LAN address.
- **`4WAY_HANDSHAKE_TIMEOUT` on every WiFi attempt is a wrong passphrase**, not a radio or
  range problem.
- **The serial log is silent on WiFi and MQTT state.** Only the OLED (`MQTT up` / `--`)
  and the broker show it. Read the broker with `mosquitto_sub`.
- **The sandbox broker refuses anonymous clients** (`CONNACK 5`). The bench subscription
  needs its user and password, so the operator runs it; a prompt keeps the password out
  of history. Set `BROKER` and `MQTT_USER` to the sandbox values first:
  `read -rs 'P?MQTT password: ' && echo && mosquitto_sub -h "$BROKER" -u "$MQTT_USER" -P "$P" -t 'lran/bridge/#' -v -F '%I %t %p'; unset P`
- **An OTA upload can fail 1 s in with `Receive Failed`**, while espota's progress bar keeps
  climbing — that is the Mac's send buffer, not the bridge. Retry once before debugging.
- **V-B9's bad images print the same version as step 2's good image.** Read `Slot:`, the
  V-B9 banner and the `WiFi SSID:` line to tell them apart.
- **Opening the serial port can press PRG.** GPIO 0 is on the CP2102's DTR. Construct the
  port unopened and set `dtr = False` before opening.
- **`pio test -e esp32s3` overwrites whatever the board was running** with a Unity image
  that leaves no settings dump. Identify the board before the upload.
- **A C++ `verifyRollbackLater()` links cleanly and does nothing.** It must be
  `extern "C"`. `bridge_partitions.py --elf` is the check; V-B9 is the proof.
- **`MQTT_MAX_PACKET_SIZE` defaults to 256 bytes in PubSubClient.** Discovery configs exceed
  it and silently do not appear. Set it ≥ 1024.
- **HA's entity registry remembers every `unique_id`, and a retained discovery config
  survives a reflash.** Develop against the dev HA VM and dev broker until **B6**.
- **The Heltec V3's TCXO runs at 1.8 V**, and the wrong value presents as a radio that will
  not calibrate. **The OLED sits behind Vext.** **The vendor header's `DIO0` on GPIO 14 is
  the SX1262's DIO1.**
- **`begin()` succeeding proves nothing about a radio pin map.** Only frames out and echoes
  back prove it.
- **Rebasing B2 over a documents change on `main` conflicts in every BF commit**, because
  each bumps the bridge documents' version. Keep B2's entries, renumber `main`'s after
  them with a note, and fix every header in one reconciliation commit.

## Open, and not closable from here

- **B2 acceptance** — the operator reviews the pull request and marks it ready.
- **The OTA first-data timeout** — one failure in two uploads. If it recurs, decide between
  `ArduinoOTA.setTimeout()` and `WiFi.setSleep(false)`; the second changes what M22
  measures.
- **R-5.3d on hardware** — an OTA upload deferred during a LoRa transaction. Needs BF-16.
- **A serial log line per network state change** — proposed in the 2026-09-13 log entry,
  not assigned to a task.
- **BF-11a** (log queue drain) and **BF-11b** (hardware watchdog from `sched_task`) — named
  by `TODO`s in the code.
- **Specification gap for the next revision:** §16.2's `lran/bridge/version` payload.
- **What GateLink's `COMMAND_ACK` waits for** — pulse complete, or gate confirmed. GateLink
  **M3** needs the answer; GateLink Impl Plan §5.2 flags it.
- **M22 / V-B12** — bridge LoRa PER with WiFi idle versus saturated, now under B3.
- **GateLink M0's LDO margin** — sized for Envelope B's 19.6 dBm, tested only at −4 dBm.
- **The range-test firmware still transmits on the provisional 915.0 MHz.** Not urgent.

### Closed, and not to be reopened by habit

- **V-B9 — PASSED 2026-09-13**, bridge engineering log. Re-run §6.5.2 after any change to
  the OTA code, the partition table or the Arduino-ESP32 version; do not re-run it for
  anything else.
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
  version bump.
