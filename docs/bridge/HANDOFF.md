# Bridge Node — session handoff

**Written at the end of the 2026-09-11 session, a clean stop.** That session decided and
built library milestone P8, merged it, rebased B2's branch onto the merge, and saw CI pass on
the result. The 2026-09-10 file it replaces was written at the end of the session that built
bridge tasks BF-10 to BF-14 — B2's code. Everything about B2's code below is carried over
from it; what changed is P8, the branch, and one board.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**Read this file on the B2 milestone branch, not on `main`.** B2's pull request is held as a
draft until its bench session passes, so `main`'s copy of this file predates both the B2 and
the P8 sessions. `gh pr list --state open` shows it; check out its branch first. **Nothing
from 2026-09-11 is local-only**: every commit is pushed. The macOS machine holds one older
stash, `WIP on field-prep` (range-test `capture.py` and firmware), which predates this
subproject and is the range-test handoff's to judge.

**To resume, in this order:**

```bash
git fetch origin -p
git switch b2-board-bringup && git pull --ff-only   # the branch was force-pushed on 2026-09-11
gh pr checks "$(gh pr list --head b2-board-bringup --json number -q '.[0].number')"
```

**If the pull refuses to fast-forward**, the other machine still holds the pre-rebase
branch, or this one does. Reset to `origin/b2-board-bringup` rather than merging — a merge
resurrects the dropped brief commit and every pre-rebase BF commit alongside its rewrite.

**P8 is done** (task **BF-1**), so simnode **B0** has no library gate left. **Three jobs are
open:**

1. **The B2 bench session, once the sandbox broker is up.** Only a broker — not Home
   Assistant, which is needed from B4. **First, identify the board**: one Heltec now runs
   P8's Unity test image and which one is not recorded (*Hardware state*). Pick the
   flat-case unit by its enclosure and make it the only Heltec on USB before flashing —
   the bridge banner does not print a board name, so the enclosure is the check. One
   session on the flat-case Heltec checks every B2
   criterion in Impl Plan §8 that code cannot: WiFi connects and reconnects, MQTT with the
   LWT registered, version published, **the OLED page**, and **V-B9** — Impl Plan
   **§6.5.2**, five steps. **Stop at V-B9's step 2 if the banner's `Image state:` reads
   `not_pending`** on the first boot after an upload. **The operator fills `secrets.h` and
   runs the three OTA uploads**; `LRAN_OTA_PASSWORD` stays out of any command an assistant
   writes. The USB flash, the serial log and the write-up can be delegated.
   **`secrets.h` on the macOS build machine was filled in by the operator on 2026-09-11**,
   uncommitted and gitignored; do not read it into a command or a log.
2. **Simnode B0** — unblocked by P8. The simnode's command path calls
   `CommandGate::check()` before dispatch and sends the `COMMAND_ACK` only after
   `record()`; on `InFlight` it sends nothing (spec §9.4 v0.11, Bridge Firmware Tasks
   v0.9). Needs a second board.
3. **BF-16 (`lora_link.cpp`)** — opens **B3**, not B2, on its own branch. Needs neither the
   broker nor the bench to start, but B3 cannot finish without B0.

## What the last two sessions established

**2026-09-11 — P8, and B2 moved onto it.**

- **P8 is built and merged**, on D34 as amended (Decision Register §3.2.1): 127 library tests
  on host, 130 on a Heltec, and two mutations proving the key tests can fail. Details are in
  the protocol-lib engineering log's 2026-09-11 entry.
- **Protocol Spec is v0.11.** It answers §9.4's check/record window with no wire change; no
  vector regenerated.
- **B2's branch was rebased onto the P8 merge** and a reconciliation commit added. CI
  passed on the result: firmware targets, host suites, repository checks.
- **P8's changelog entries in two bridge documents were renumbered** after B2's own: Tasks
  v0.9 and Impl Plan v0.20. Each says so.

**2026-09-10 — B2's code.** **Tasks BF-10 to BF-14 are built**, host-tested and in CI.
**None of it has run on a board.** D1 and D33 closed earlier the same day; see *Closed*.

- **The bridge is in CI, and CI still holds no secret.** The bridge is the first target
  needing `secrets.h`; the workflow copies the committed template, whose all-zero
  `LRAN_MASTER_KEY` `main.cpp` reports loudly at boot. Bridge Firmware Tasks **§1.2**.
- **Task structure, Impl Plan §5.2.1.** Seven statically allocated tasks; `lora` strictly
  highest and pinned off the WiFi core; queues **drop the newest item and count it**.
  **Root rule 4 is not stretched to a queue overflow** — a frame dropped there has passed
  the whole §14 ladder and has no stage; per-queue counters honour the rule's substance.
  `tools/checks/lora_task_never_blocks.py` makes the never-block rule falsifiable.
- **Network, Impl Plan §4.3.1.** Capped, deterministic reconnect; boot waits for no network;
  **spec §16.3's never-retain-an-event rule is enforced twice on the publish path**; LWT on
  `lran/bridge/availability`.
- **OTA, Impl Plan §6.5.1.** **Arduino-ESP32 2.0.17 marks every image valid before
  `setup()` runs**, which would have kept an image that never finds the LAN — the one image
  this bridge could not be OTA'd back from. `ota.cpp` overrides the core's
  `verifyRollbackLater()`; **the override must be `extern "C"`**, and CI checks the linked
  symbol because a C++ definition links cleanly and overrides nothing.
- **OLED, Impl Plan §5.1.2.** A dead panel never stops the bridge; unknown reads `--`;
  burn-in is designed against. Host budget tests caught two overruns and a **`millis()`
  wrap** that would have shown a phantom reboot every seven weeks.
- **P8 landed 2026-09-11, on D34 as amended** (Decision Register §3.2.1). `check()`
  advances the `seq` high-water mark before dispatch; a retry inside the execution window
  is counted and gets no answer. Spec **v0.11** carries it with no wire change. The bridge
  sees silence there, which takes Impl Plan §6.2's existing `no ACK` path.
- **One specification gap, recorded, not closed:** §16.2 names `lran/bridge/version` but
  not its payload (engineering log, BF-13 entry). §9.4/§10.4's execution window, the other
  gap, closed in v0.11.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) §3.2.1 | D34's amendment — what the simnode's command path must do with `InFlight`. The superseded [P8 brief](../shared/LRAN-P8-CommandGate-Brief.md) keeps the reasoning |
| 3 | [`engineering-log.md`](./engineering-log.md) | the 2026-09-10 entries, one per task — the reasoning that is not a number. For P8, the [protocol-lib log](../protocol-lib/engineering-log.md)'s 2026-09-11 entry |
| 4 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | what to build, in what order, which model; **§1.2** for the CI decision |
| 5 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **owns B0–B7 and their acceptance criteria (§8).** §4.3.1, §5.1.2, §5.2.1 and §6.5.1 record what BF-11 to BF-14 fixed; **§6.5.2 is the V-B9 procedure** |
| 6 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and the two OTA details that break silently |
| 7 | [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md) | the requirements; §8's `V-B*` rows are what a milestone is checked against |
| 8 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) | **the only place a decision's status is recorded.** §3.2 for D34, §3.4 for D1 |
| 9 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) §9.4, §10.4, §12, §14, §16 | replay and dedup, radio, the discard ladder, MQTT. **§18.2, never §18.1 alone** |
| 10 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere. Rule 2 is the one P8 exists for |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**. **M6**, **M19**, **M20**, **M21**. **D1**, **D33**; **D34 amended**. Bridge **BF-10 to BF-14 built** — B2's code |
| Not done | **B2 acceptance** — every criterion but the partition table needs the bench, and **V-B9** among them. **BF-11a**, **BF-11b** split out |
| Queue | The **B2 bench session** when the broker is up. Simnode **B0**, now unblocked. **BF-16** → B3 |

```bash
pio test -d lib/lran-protocol -e native         # library host suite
pio test -d lib/lran-protocol -e esp32s3        # same suite on a Heltec over USB - P7 and P8 ran it
python3 tools/vectors/check.py                  # W4 vectors, self-check
python3 tools/checks/spec_citation_version.py   # binding citations vs. the spec header
pio test -d firmware/bridge -e native           # bridge host suite, no secrets
pio run  -d firmware/bridge -e heltec           # bridge target - NEEDS secrets.h
python3 tools/checks/lora_task_never_blocks.py  # lora_task blocks on nothing
python3 tools/checks/bridge_partitions.py        # A/B table; add --firmware/--elf after a build
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

**Local branches accumulate on each machine** — merged docs branches whose remote is gone,
and a local `main` behind `origin/main`. They hold nothing unpushed when the command above
is empty; delete them by hand.

**Run `git fetch` before trusting any of it.** Two machines push to this repository.

**Permanent history is citable; moving state is not.** For the documents: `4250e00` (six
defects, including the duplicate `V-B2` and the B0-on-P8 gate) and `ebdcf0d` (the
`LoRaBridge` retirement and the D33 bench-power reconciliation), and `8253085` (**P8**, with
D34's amendment and spec v0.11). **For B2's code, one commit per task, each message carrying
its reasoning** — `git log --oneline origin/main..origin/b2-board-bringup` lists them. They
are not cited by SHA here: the branch was rebased onto P8 on 2026-09-11, which rewrote every
one, and it is not permanent history until it merges.

**A push touching `.github/workflows/` needs workflow token scope.** Refused on 2026-09-08,
accepted on 2026-09-10 from the macOS machine — so try the push. If it is refused, the
refusal names the scope and the operator refreshes auth; retrying does not help.

**Merging a stack: never pass `--delete-branch`.** Deleting a base branch **closes** the PR
stacked on it rather than retargeting it, and a closed PR's base cannot be changed while its
base branch is missing. Merge each PR without it, retarget the next to `main` while it is
still open, then delete branches by hand.

## Hardware state

**No board has ever been flashed as the bridge or as a simnode.** Every device below was in
a range-test role until 2026-09-11, when one Heltec took P8's test image (below). **This table names them in *this* subproject's terms**; the
range-test handoff owns them in its own roles and its rows do not transfer here.

| Device | Called here | Told apart by | Firmware / env | Stored state | Current state |
|---|---|---|---|---|---|
| Heltec WiFi LoRa 32 V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `range-test` / `heltec`, a **pass-2 build whose banner cites spec v0.8** (read off the port 2026-09-10) — **or P8's Unity test image**, if it was the board flashed 2026-09-11. Never a bridge build | Range-test settings and position log. Nothing this node needs | On USB to the macOS build machine as `/dev/cu.usbserial-0001`, 2026-09-10. **With the range-test build it boots `INITIATOR`, which transmits on 915.0 MHz**; the test image transmits nothing |
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `range-test` / `heltec` — **or P8's Unity test image**, if it was the board flashed 2026-09-11. Never a simnode build | **Whether its stored survey campaign was erased is not recorded** — see the range-test handoff. Irrelevant to this node | Went to the gate for B1b. Powered down |
| XIAO ESP32S3 + **Wio-SX1262 Kit** (p-5982, B2B) | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module, not a Heltec | `range-test` / `xiao`. Never a simnode build | B1b position log, dumped and committed | Was B1b's walking responder. Powered down |

**A wrong board selection is silent.** It writes the wrong pin map and antenna gain into a
normal-looking artifact. **Read `board=` off the settings dump** rather than trusting a port
name — both CP2102 bridges report `SER=0001` and the nodes are not stable across replug.

**No stored state on any of these boards is the only copy.** Every survey site and every
B1b position is committed under `docs/rangetest/data/`. **NVS on a bench board is not a
backup**, and none of it is input to this node's work.

**The flat-case Heltec is the intended bridge unit** (range-test handoff, 2026-09-09). It
carries no bridge firmware and nothing reserves it, so say so before reflashing it for
something else.

**`secrets.h` on the macOS build machine was filled in by the operator on 2026-09-11.** It is
gitignored and uncommitted; never read it into a command, a log or a commit. CI still builds
against the committed template, whose placeholder key `main.cpp` reports at every boot.

**One Heltec was flashed with P8's Unity test image on 2026-09-11** — the only CP2102 on
the macOS machine that day. **Which one is not recorded**: `board=` was not read off a
settings dump first, and the handoff expected the flat-case unit on that port. A board
running the test image prints Unity results once after reset and then nothing; it has no
settings dump to read. **Tell the two Heltecs apart by enclosure.**

**Its antenna stays on it.** The bridge uses the same 3.0 dBi 19 cm stick these boards ran
the range test with (Bridge PRD **R-4.3a.1**) — the sticks are interchangeable as parts, but
**the gain is a term in D1's EIRP arithmetic**, so a different antenna is a decision to
record, not a swap to make at the bench.

## Behaviour that changed, and will make older artifacts read differently

- **`LoRaBridge` is retired in favour of `Bridge Node`, 2026-09-10** (**D17** amended).
  `lran-bridge` remains the firmware target and **"LoRa Bridge" remains the HA device
  name**. **Protocol Spec §5.3's `0x00` gloss was renamed in v0.10**, the substantive
  revision D17 had deferred it to; a spec revision before v0.10 still reads `(LoRaBridge)`.
- **The PHY parameters are stated rather than deferred, 2026-09-10.** A document revision
  citing Protocol Spec v0.9 or earlier reads §12.1 as "per D1" and §12.3's `backoff_max_ms`
  as 500. Both are current for when they were written; **v0.10 is where the numbers are**.
- **`+22 dBm` no longer appears as an operating point** in either node's documents. **A
  document revision that still states it is correct for when it was written** and is not to
  be re-stamped; the archive keeps it throughout.
- **`V-B2` means the per-node registry verification and nothing else.** A document revision
  before Bridge PRD v0.6 may use it for the coexistence measurement, which is now `V-B12`.
- **Milestone `M0` in GateLink's plan no longer accepts at +22 dBm**, and its LDO is sized
  against 19.6 dBm rather than tested there. Older readings of M0 asked for a different test.

## Traps that cost real time here

- **`MQTT_MAX_PACKET_SIZE` defaults to 256 bytes in PubSubClient.** Discovery configs exceed
  it and **simply do not appear, with no error pointing at the cause**. Set it ≥ 1024 in the
  build flags on day one.
- **The Heltec V3's TCXO runs at 1.8 V, not 3.3 V**, and the wrong value presents as a radio
  that will not calibrate rather than as an error.
- **The Heltec V3's OLED sits behind Vext.** A display dark on boot is usually Vext, not the
  driver.
- **The vendor header calls GPIO 14 `DIO0`, an SX127x name.** On the SX1262 that line is
  **DIO1**, which is what RadioLib wants as its IRQ pin. Trusting the symbol over the number
  sends you hunting for a GPIO that does not exist.
- **HA's entity registry remembers every `unique_id` it has ever seen.** Iterating on
  discovery payloads against production leaves orphans, and the second `sensor.gate_state`
  arrives as `sensor.gate_state_2`. Develop against the dev HA VM and a dev broker until
  **B6** (Implementation Plan §11.3).
- **A retained discovery config survives a bridge reflash** and re-registers the bad entity
  on the next HA restart. `mosquitto_sub -t 'homeassistant/#' -v --retained-only` and a
  retained-clear pass should be routine during B4.
- **Opening the serial port can press PRG.** GPIO 0 is both the PRG button and IO0, driven
  by the CP2102's DTR, and `serial.Serial(port, …)` asserts DTR as it opens. **Construct
  the port unopened and set `dtr = False` before opening** — setting it afterwards is too
  late (range-test `CLAUDE.md`). A 2026-09-10 read of the bridge board did it the wrong way;
  harmless in `INITIATOR`, a stored bogus survey run in `SURVEY`.
- **A C++ `verifyRollbackLater()` links cleanly and does nothing.** Arduino's weak default
  is in a C file with no header, so the override must be `extern "C"`. Without it every OTA
  image is kept. `bridge_partitions.py --elf` is the check.
- **CI's firmware job takes about seven minutes**, half of it the two V-B9 bad images. That
  is the price of the test harness not rotting between runs; drop them if speed matters
  more.
- **Rebasing B2 over a documents change on `main` conflicts in every BF commit.** Each BF
  commit bumps the bridge documents' version and adds a changelog row, so any change on
  `main` that also bumped them collides five times over. What worked on 2026-09-11: keep
  B2's entries as written, renumber `main`'s after B2's last with a note saying why, take
  B2's header lines with the spec citation moved up, and fix every header in one
  reconciliation commit at the end. **Drop a commit `main` already carries**; don't merge
  it twice.
- **`pio test -e esp32s3` overwrites whatever the board was running** with a Unity image
  that leaves no settings dump. Identify the board before the upload, not after.
- **`begin()` succeeding proves nothing about a radio pin map.** A wrong `rf_sw`
  initialises just as cleanly and transmits into a dead end. Only frames out and echoes back
  prove it.

## Open, and not closable from here

- **B2 acceptance, and V-B9 within it** — code complete, bench owed, **blocked on the
  sandbox broker**.
- **BF-11a** (log queue drain) and **BF-11b** (hardware watchdog from `sched_task`) — split
  out of BF-11, named by `TODO`s in the code.
- **Specification gap for the next revision:** §16.2's `lran/bridge/version` payload.
- **What GateLink's `COMMAND_ACK` waits for** — pulse complete, or gate confirmed. Sets how
  often P8's window is hit, and GateLink **M3** needs the answer. GateLink Impl Plan §5.2
  flags it; D34's amendment left it open deliberately.
- **M22** — bridge LoRa PER with WiFi idle versus saturated. The evidence for §4.4's
  deliberate lack of mutual exclusion; **V-B12** is its verification row. `lora` is pinned
  off the WiFi core as one lever if it fails.
- **GateLink M0's LDO margin above the operating point** — the rail is sized for Envelope
  B's 19.6 dBm but will be tested only at −4 dBm. M0 says to re-run if Envelope B is ever
  triggered.
- **The range-test firmware still transmits on the provisional 915.0 MHz**, `weather-island`'s
  peak. Not urgent; a re-run on that channel produces data that will be distrusted.

### Closed, and not to be reopened by habit

- **D1 and D33 — CLOSED 2026-09-10.** 917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted,
  Envelope A. **The measurements do not settle SF and re-reading them will not**: PER was
  0 % at every SF at the gate, and the choice came from the fade tail against the cost of
  being wrong. Reopening needs a new fact — a `cad_backoffs` reading the channel does not
  explain, or an Envelope B trigger — not a re-reading of B1b.
- **M19 — DONE 2026-09-10, W7 closed with it.** §15.1's table was already at BW125 / CR 4/5;
  it was confirmed, not recomputed. **Do not regenerate it expecting different numbers.**
- **M6 — CLOSED 2026-09-09.** Both bearings measured. The gate is **~87 m**, 0 % PER at all
  24 configurations on the deployed pairing; the well is **~100 m**, 2.08 % PER. **The
  "~500 ft" in M6's own wording was a guess predating any walk** and is retired, not
  reworded. **No well walk is owed** — the 2026-09-04 walk's P3 is the well site.
- **M20 — CLOSED 2026-09-05**, re-integrated 2026-09-06 with no new field work. **Re-walking
  to rank channels would be an expensive, invisible mistake**; the tool does it from the
  committed trace.
- **M21 — CLOSED 2026-09-06.** Both grants recorded. **D33 reopened on that basis**, ceiling
  unchanged, reasoning changed to §15.23 home-built.
- **W5 — closed.** The operating mode question is settled and has been since spec v0.6.
  **§18.1 is annotated rather than rewritten and must not be read alone.**
- **W4, W9, W12 — closed.** Vectors committed, `PING` bench runs passed over RF, `D34`
  placed the replay and dedup gate in the library.
- **D31 — closed 2026-09-08.** Copyright holder is Robert J. Lee.
- **D32 — closed.** RadioLib, pinned in every `platformio.ini`. **Run
  `tools/rangetest/check_pa_table.py` after any version bump** — a change to RadioLib's
  file-static `paOptTable` is silent in every other check.
