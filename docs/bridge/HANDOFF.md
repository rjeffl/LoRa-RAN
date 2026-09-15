# Bridge Node — session handoff

**Written 2026-09-14, at the end of the session that brought BF-16 up on the bridge board,
built BF-15, built simnode B0 and proved it on air, built BF-17, BF-19 and BF-20, and split B3
into B3a and B3b.** It replaces the 2026-09-13 file wholesale; that file's content is carried
over where it is still true.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**The next session has every board and the broker. Its goal is to accept B0 and B3a on the
bench and merge the three-PR stack**, before B3b's work makes the stack taller. B3 was split
for exactly this (Impl Plan v0.32 §8; engineering log, "B3 split into B3a and B3b").

**Read this file on `b3-poll-scheduler`, the top of the stack:** BF-17, BF-19 and BF-20 on
`b3-poll-scheduler`, on B0's `b0-simnode-bringup`, on BF-15 and BF-16's
`b3-protocol-registry`. Every lower branch's copy of this file is older.

```bash
git fetch origin -p
git switch b3-poll-scheduler && git pull --ff-only
gh pr list --state open
for n in $(gh pr list --state open --json number -q '.[].number'); do gh pr checks "$n"; done
```

### Do these in order — each one unblocks the next

**Test on the stack's tip, from a clean tree.** The three PRs merge together, so the tip is
what `main` will run. A fix found on the bench goes on `b3-poll-scheduler`, whichever PR
first introduced the code.

1. **Desk check, before touching a board.** CI green on all three PRs; the host suites and
   the `heltec`, `simnode-heltec` and `simnode-xiao-wio` builds pass locally (*Where things
   stand* lists them). Confirm `secrets.h` names the broker's **LAN** address (see *Traps*).
2. **V-B9 re-run on the bridge board** (Impl Plan §6.5.2), broker up. Owed since BF-16 changed
   the OTA verdict, and it is the one B2 obligation #59 carries. **It needs the broker**: with
   it down, a good image rolls back and reads as a firmware failure.
3. **The bridge board on the stack's tip, with the broker watched.** USB-flash `heltec`, then
   subscribe to `lran/#` (the `mosquitto_sub` line in *Traps*). Expect, and record:
   - `lran/bridge/availability online` and `lran/bridge/version` on connect;
     `lran/bridge/diag/state` and `lran/bridge/diag/radio/state` on the next tick.
   - **`lran/gatelink/availability` and `lran/welllink/availability` `offline`, retained**,
     about 2½ minutes after boot. Neither node exists, so each misses three polls. This is
     B3a's "retained `offline` seen at the broker".
   - The OLED reads `nodes 0/2`. The bridge's `POLL`s are now on air.
4. **Flash the XIAO** with `simnode-xiao-wio`, for the first time. Check the banner, that the
   expansion-board panel reads the right way up (`flip_vertically` is false; the board is
   rotated 180° from its range-test mounting), and that it boots as `f1 ROLE_GATELINK`.
5. **B0 acceptance** — the operator, against Impl Plan §8's B0 row, clause by clause. Record
   it in the engineering log and in #60's criteria section. Every clause has code behind it;
   what was missing was the XIAO flashing and running.
6. **B3a on air**, with the bridge board, the simnode Heltec and the XIAO. Impl Plan §8's
   B3a row is the list; in bench terms:
   1. **Polls answered.** On the simnode Heltec, `push f0`: the bridge enrols `f0` and polls
      it. Read the bridge's serial `availability: simnode0 online` and the OLED's `nodes 1/3`.
      **Time about 20 poll-to-answer exchanges** and record them against
      `poll_reply_timeout_ms` = 10 000 (Impl Plan §6.1.1). No measurement of it exists.
   2. **Four identities from one board.** `id add` until `f0`–`f3` exist on one board, `push`
      each, and see four `online` lines and four identities answering polls. A simnode's
      availability and per-node diagnostics are **not published** until BF-26 (spec §16.6), so
      the bridge's serial console is the record.
   3. **V-B3.** `disable f0`: after three missed polls the bridge prints
      `availability: simnode0 offline (missed_polls 3, threshold 3)`. `enable f0` and `push f0`:
      `online` on that frame.
   4. **The discard counters, by hand.** A fault's `dst` defaults to the bridge. Arm the §10.5
      entries one at a time (`fault f0 <name>`), and after each read
      `lran/bridge/diag/state`: **exactly the named counter moves**, and `rx_dropped` moves only
      for counters §14.1 marks yes. It publishes every 60 s. **`hdr_rsv` must move nothing** and be
      delivered. Stage 1 is not injectable.
   5. **W9** was observed between two simnode Heltecs on 2026-09-14 (engineering log); B3a cites
      it. The bridge answers no `PING` (spec §17.3 gap, below), so do not aim one at `00`.
7. **B3a acceptance by the operator, then merge bottom-up**, in the same sitting. Before each
   merge, update that PR's criteria section to B3a/B0 and mark it ready.
   ```bash
   gh pr ready 59 && gh pr merge 59 --merge          # never --delete-branch
   gh pr edit 60 --base main && gh pr ready 60 && gh pr merge 60 --merge
   gh pr edit 61 --base main && gh pr ready 61 && gh pr merge 61 --merge
   git push origin --delete b3-protocol-registry b0-simnode-bringup b3-poll-scheduler
   ```
   Retarget each PR **before** deleting the branch under it, or GitHub closes it. After the
   merges, this handoff's *Git state* rule applies again: cite the merges as history.
8. **Then B3b, from `main`.** A spec v0.12 revision is B3b's real gate: BF-18, BF-19a and BF-26
   each wait on it (*Open*, the consolidated list). BF-21's `simctl` scripts and BF-22's version
   tolerance need no spec change and can start on a new branch meanwhile.

**If B3a fails a clause on the bench**, fix it on `b3-poll-scheduler`, re-run that clause, and
keep the merge in the same session if you can. A B3a that waits a week is the tall stack again.

## What the last session established

**B3 split, 2026-09-14, at the end of the session.** Impl Plan v0.32 §8; engineering log.

- **B3a** (BF-15, BF-16, BF-17, BF-19, BF-20) is accepted on the bench; **B3b** (BF-18, BF-19a,
  BF-21, BF-22) after spec v0.12. Decided with the operator so the stack can merge.
- **CI was green on all three PRs** at the end of the session, including the GCC fix.

**BF-19, 2026-09-14, after BF-20.** Impl Plan §4.3.2 has the topics and rules.

- **Every §14.1 counter is published under its name**, on `lran/bridge/diag/state`, with
  radio and queue numbers on `.../diag/radio/state` and each watched node's link on
  `lran/<node>/diag/state`. 126 bridge host tests pass; two mutations failed two and one.
  **Not supported:** any document seen at a broker.
- **Decided with the operator:** discard counters are the bridge's, not per node; `ERROR`
  replies are BF-19a; BF-26 is deferred, with a serial `diag on|off` as its interim toggle.
- **`kMaxPayloadLen` is 768**, because the counter document is 681 bytes at worst.

**BF-20, 2026-09-14, after BF-17.** Impl Plan §6.1.2 has the rules.

- **`offline` at 3 missed polls, `online` on any frame, retained.** A node not yet judged
  since boot publishes nothing. 116 bridge host tests pass; two mutations failed one and six.
  **Not supported:** any publication seen at a broker, or V-B3 on the bench.
- **A simnode's availability is printed, not published**, until BF-26 (spec §16.6). V-B3
  reads `availability: simnode1 offline ...` on the bridge's serial console.
- **CI's GCC 13 crashed on BF-6's `gatelink.cpp`**; fixed in `10e3d6c` on B0's branch and
  merged up. A clean macOS `native` run does not prove CI's compiler agrees.

**BF-17, 2026-09-14, after BF-6.** Impl Plan §6.1.1 has the parameters.

- **One poll outstanding fleet-wide; a miss after a 10 s reply window.** No document gave
  that number; §6.1.1 now does, derived from spec §12.3's worst-case node backoff. 103 bridge
  host tests pass; a mutation allowing a second outstanding poll failed three. **Not
  supported:** any poll on air.
- **Production rows are polled from boot; `f0`–`f3` only once heard** (decided with the
  operator). A simnode enrols itself with `push`.

**BF-6, 2026-09-14.** The engineering log's BF-6 entry has the
decisions and the spec questions; Impl Plan §10.9.2 the summary.

- **`ROLE_GATELINK` answers `POLL` (`0xFE`), `COMMAND` (through `CommandGate`) and `CONFIG`
  (a 21-entry RAM store), and sends events.** `push`, `event`, `ack` and `field` exist, and
  all five command-path faults arm. 108 host tests pass; a mutation that let a cached retry
  actuate failed five of them. **Not supported:** anything on air, or a bridge that retries.
- **Four decisions with the operator:** schema `0xFE` is the synthetic marker; `CONFIG` uses
  a generic RAM store; `ack suppress|dup` are bounded faults and `ack delay` a setting;
  `cmd_replay` and `cmd_stale_seq` to a same-board target never transmit.
- **Three spec questions for v0.12**: the `DUPLICATE_CACHED` ACK's encoding, what answers a
  repeated `CONFIG`, and §7.4's reliance on fragmentation that §3.1's 196-byte set cap rules
  out. Not patched.

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
| 2 | [`engineering-log.md`](./engineering-log.md) | the 2026-09-14 entries, last first: the B3 split, BF-19 (and BF-26 deferred), BF-20, BF-17, BF-6's spec questions |
| 3 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **§10** is the simnode, **§10.9** what B0 has built, **§10.5.2** BF-7's primitive and the fault-to-operation map; **§4.2.1** is BF-15; **§6.1.1–§6.1.2** BF-17 and BF-20; **§4.3.2** BF-19; **§8** owns B0's, B3a's and B3b's criteria; **§6.5.2** is V-B9, owed |
| 4 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | §4 is B0's tasks, all built; §6 is B3a's and B3b's, with BF-19a new and BF-26 (§7) deferred |
| 5 | [`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) | what exists in the project, and what breaks silently |
| 6 | [`firmware/simnode/CLAUDE.md`](../../firmware/simnode/CLAUDE.md) | what the simnode has, what it does not, and its traps |
| 7 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) §3.2.1 | D34's amendment, which the simnode's command path follows |
| 8 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) §9, §10, §11, §12, §14 | keys, context, reassembly, radio, the discard ladder. **§18.2, never §18.1 alone** |
| 9 | root [`CLAUDE.md`](../../CLAUDE.md) | the rules that bind everywhere |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P8**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**, **B2**. **M6**, **M19**, **M20**, **M21**. **D1**, **D33**; **D34 amended**. **BF-15** and **BF-16** built; BF-16's radio up on the board. **BF-2**, **BF-3**, **BF-5** and BF-4's core built; PING echo on air. **BF-4**, **BF-6**, **BF-7** and **BF-8** built, host-tested; **BF-9** confirmed on the Heltec's panel. **BF-17**, **BF-19** and **BF-20** built, host-tested |
| Not done | **B0**: the XIAO flashed and the operator's acceptance. **B3a**: every criterion needs the bench (steps 3 and 6 above). **B3b**: BF-18, BF-19a, BF-21, BF-22. **V-B9's re-run.** **BF-26** deferred. **BF-11a**, **BF-11b** |
| Queue | *The next job*'s eight steps: V-B9 → bridge on the tip → XIAO → B0 accepted → B3a on air → B3a accepted → merge #59, #60, #61 → spec v0.12, then B3b |

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

**Merging the stack: bottom-up.** Merge B3's pull request without `--delete-branch`, retarget
B0's to `main` while it is still open, then delete B3's branch by hand. Repeat for B0's, with
`b3-poll-scheduler`'s pull request retargeted to `main`.

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
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `simnode` / `simnode-heltec`, **USB-flashed from `c3ef4ca` (BF-9)**, MAC `44:1b:f6:fa:bc:2c`. Boots as `f0 ROLE_RANGE` and `f2 ROLE_HEALTH`, with `OLED: up`. **It ran an old range-test image (spec v0.8 banner) until 2026-09-14** | Nothing persists; identities reset on every boot. Any range-test NVS from before is not the only copy of anything | On USB to the macOS build machine, last seen as `/dev/cu.usbserial-3`. **Its image predates BF-6**: reflash it from the stack's tip before B3a so both simnodes run the same code |
| XIAO ESP32S3 + **Wio-SX1262 Kit** (p-5982, B2B) | **target-radio simnode** | Different board entirely — XIAO with a B2B-connected module | `range-test` / `xiao`. **`simnode-xiao-wio` builds and has never been flashed** | B1b position log, dumped and committed | Powered down. **Back on the bench for the next session**; its first simnode flash is step 4 |

**The bridge board, as flashed, transmits nothing.** It runs `cab05e8`, which predates
BF-17. **The stack's tip polls from boot**: flashed with it, the bridge transmits a `POLL` to
`0x01` and `0x02` about once a minute, and to each simnode identity it has heard.

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

- **The XIAO simnode boots as `0xF1 ROLE_GATELINK` since BF-6**, not `ROLE_RANGE`. Text
  describing `push`, `event`, `ack`, `field` or the command-path faults as `ERR not
  implemented` is correct for before BF-6.
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

- **B0's and B3a's acceptance** — *The next job*, steps 5–7.
- **Frames to and from the bridge on air** — DIO1 waking `lora_task`, frames both ways. B3a's
  polls prove the first exchange; **a key verifying a frame on air waits for BF-18** (B3b),
  because nothing a node sends the bridge carries a MAC.
- **`poll_reply_timeout_ms` = 10 000 is a derived default**, never measured (Impl Plan
  §6.1.1). B3a now requires the measurement.

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
- **The XIAO simnode profile on hardware** — builds, never flashed.
- **V-B9 re-run** — owed since BF-16 changed the verdict; *The next job*, step 2.
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
