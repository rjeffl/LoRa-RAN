# Bridge Node — session handoff

**Written 2026-09-10, at the end of the session that closed D1 and D33 and settled the
bridge antenna.** It replaces the
earlier 2026-09-10 file — written after the document audit, the `LoRaBridge` retirement and
the firmware task list — wholesale.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**Run V-B9 on the flat-case Heltec — it is the one B2 acceptance test that only the bench
can answer, and the procedure is written:** Impl Plan **§6.5.2**, five steps, about twenty
minutes. **Stop at step 2 if the banner's `Image state:` reads `not_pending`** on the first
boot after an upload; that means rollback is not in effect and every later step would pass
without meaning anything. Then **`BF-14`** (the OLED page) and **`BF-16`**
(`lora_link.cpp`, the first task that puts a frame on the air).

**`BF-10` to `BF-13` are built.** `firmware/bridge/` builds on `heltec` and passes 40 host
tests, **and all of it runs in CI** — the workflow copies `secrets.h.example` for the target
build, so nothing secret is in it (Bridge Firmware Tasks §1.2).

- Seven statically allocated FreeRTOS tasks, `lora` strictly highest and pinned off the
  WiFi core, `log` strictly lowest, queues that **drop the newest item and count it**.
  Impl Plan **§5.2.1**.
- WiFi with a **capped, deterministic reconnect**; the `MqttTransport` seam (**D5**); **LWT
  on `lran/bridge/availability`**. Impl Plan **§4.3.1**.
- **OTA with a committed A/B table and a rollback verdict that replaces Arduino's.**
  Arduino-ESP32 2.0.x marks every image valid before `setup()` runs, which would have kept
  an image that never finds the LAN. The fix is an **`extern "C"`** override, and CI checks
  the symbol table because a C++ one links cleanly and does nothing. Impl Plan **§6.5.1**.

**Still absent: the radio, discovery, the publication policy and the OLED.** **None of the
runtime behaviour is proven** — no reconnect has reconnected, no LWT has landed, no image
has been OTA'd, and **V-B9 is not met.**

**The radio is no longer a question.** D1 closed 2026-09-10: **917.4 MHz, SF9, BW 125 kHz,
CR 4/5, −4 dBm conducted** with the fitted 3.0 dBi antenna, under §15.249 Envelope A. Two
things that reach code — **`backoff_max_ms` defaults to 1500**, not 500, and **the PHY
parameters go in the injected radio config beside the pin map**, never in the HA-visible
configuration set (Protocol Spec §12.1).

**Simnode B0 is blocked** on library milestone **P8** (`CommandGate`, D34), which is the
only library work outstanding — and the only remaining alternative to board bring-up.

## What the last session established

**No firmware was written, and no measurement was taken. D1 and D33 were closed on the
evidence already in hand**, on the operator's agreement with the decision brief's
recommendation, unchanged.

- **The parameters: 917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted** with the fitted
  3.0 dBi antenna. Decision Register **§3.4** is the record and the only place the status
  lives.
- **`BW` and the rule section are one decision**, so **D33 closed in the same motion**, on
  Envelope A. Envelope B is untouched and stays a fallback behind three triggers; triggering
  it reopens **D28** as well.
- **SF9 rather than SF7 was the only contested knob**, and it was decided on the cost of
  being wrong rather than on the measurements, which point both ways. W9 wants SF7 and keeps
  §12.3's defaults valid; B1b's worst single SF7 probe at the gate reached **2.2 dB of
  margin**. **SF9's cost is a runtime-configurable number; SF7's risk is a USB reflash at a
  gate with no OTA.**
- **`backoff_max_ms` rises 500 → 1500**, above SF9's 1107 ms full-frame airtime. It is the
  one configuration change SF9 forces, and the reason SF9 was affordable.
- **M19 done, W7 closed** — §15.1's airtime table was already computed at BW125 / CR 4/5, so
  it needed confirming rather than recomputing. Its basis is now written down, which it was
  not.
- **Protocol Spec v0.10** carries §12.1, §12.3 and §15.1, and renames §5.3's `0x00` gloss to
  **Bridge Node** — the one item D17 deferred to the next substantive revision. **Nothing on
  the wire moved:** `ver` stays at `2` and no W4 vector regenerates.
- **The bridge antenna is decided: the 3.0 dBi 19 cm stick the range test ran on**
  (Bridge PRD **R-4.3a.1**). It is not a new selection — B1a and B1b measured through that
  part at both ends, and D1's −4 dBm conducted ceiling is computed against its gain, so
  **swapping it invalidates the measurements and the compliance arithmetic together.**
  **V-B1's remaining gap is the bridge's position**, not the antenna.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) **§3.4** | what D1 fixed and why, including the SF tie-break. The brief it came from is **superseded** and is kept only as the account of how the choice was framed |
| 3 | [`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) | what to build, in what order, and which tasks suit which model |
| 4 | [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) | **owns milestones B0–B7 and their acceptance criteria.** §5.2 task structure and §6 implementation specifics before writing any firmware |
| 5 | [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md) | the requirements the plan implements. §8's `V-B*` rows are what a milestone is checked against |
| 6 | [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md) | **the only place a decision's status is recorded.** §2 for what is open |
| 7 | [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) §12, §14, §16 | radio config, the discard ladder and counter registry, MQTT topic grammar. **§18.2, never §18.1 alone** |
| 8 | root [`CLAUDE.md`](../../CLAUDE.md) | the nine rules that bind everywhere. Rule 2 and rule 10 are the ones this node can break expensively |

## Where things stand

| | |
|---|---|
| Branch and merge state | **Not written here — it cannot be kept true.** Run the commands in *Git state* |
| Done | Library **P1–P7**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**. **M6**, **M20**, **M21**, **M19**. **D1**, **D33** |
| Queue | **B2** board bring-up (`BF-10`–`BF-14`), unblocked now. Library **P8**, which gates **B0**. **B3** after both |

```bash
pio test -d lib/lran-protocol -e native       # host Unity suite
pio test -d lib/lran-protocol -e esp32s3      # same suite on a Heltec V3
python3 tools/vectors/check.py                # W4 vectors, self-check
python3 tools/checks/spec_citation_version.py # binding citations vs. the spec header
pio test -d firmware/range-test -e native     # range test host suite
pio run  -d firmware/range-test -e heltec     # Heltec V3 target build
pio run  -d firmware/range-test -e xiao       # XIAO + Wio-SX1262 Kit target build
python3 tools/rangetest/check_pa_table.py     # PA table mirror vs. pinned RadioLib
```

**CI runs these on every push and pull request.** Run them locally when you are about to
spend bench time on the result; otherwise a green run already answers it.

**`firmware/bridge/` and `firmware/simnode/` do not exist yet.** Only their `CLAUDE.md`
context files are written. Nothing in this node builds today.

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
```

**Run `git fetch` before trusting any of it.** Two machines push to this repository.

**Permanent history is citable; moving state is not.** Two commits worth reading in order
for how this node's documents reached their current state: `4250e00` (six document defects,
including the duplicate `V-B2` and the B0-on-P8 gate) and `ebdcf0d` (the `LoRaBridge`
retirement and the D33 bench-power reconciliation).

**A push touching `.github/workflows/` fails without workflow token scope.** The refusal
names the scope; the operator has to refresh auth, and no amount of retrying helps.

**Merging a stack: never pass `--delete-branch`.** Deleting a base branch **closes** the PR
stacked on it rather than retargeting it, and a closed PR's base cannot be changed while its
base branch is missing. Merge each PR without it, retarget the next to `main` while it is
still open, then delete branches by hand.

## Hardware state

**No board has ever been flashed as the bridge or as a simnode.** Every device below is in a
range-test role today. **This table names them in *this* subproject's terms**; the
range-test handoff owns them in its own roles and its rows do not transfer here.

| Device | Called here | Told apart by | Firmware / env | Stored state | Current state |
|---|---|---|---|---|---|
| Heltec WiFi LoRa 32 V3, **Meshtastic flat case** | **the bridge board** | Its enclosure — flat case, not the handheld one | `range-test` / `heltec`. Never a bridge build | Range-test settings and position log. Nothing this node needs | Was B1b's tethered initiator at the bridge's target location. Powered down |
| Heltec WiFi LoRa 32 V3, **handheld dev-board case** | **simnode Heltec** | Its enclosure — handheld case | `range-test` / `heltec`. Never a simnode build | **Whether its stored survey campaign was erased is not recorded** — see the range-test handoff. Irrelevant to this node | Went to the gate for B1b. Powered down |
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

**Its antenna stays on it.** The bridge uses the same 3.0 dBi 19 cm stick these boards ran
the range test with (Bridge PRD **R-4.3a.1**) — the sticks are interchangeable as parts, but
**the gain is a term in D1's EIRP arithmetic**, so a different antenna is a decision to
record, not a swap to make at the bench.

## Behaviour that changed, and will make older artifacts read differently

- **`LoRaBridge` is retired in favour of `Bridge Node`, 2026-09-10** (**D17** amended).
  `lran-bridge` remains the firmware target and **"LoRa Bridge" remains the HA device
  name**. **Protocol Spec §5.3 still glosses node `0x00` as `(LoRaBridge)`** — deliberately,
  deferred to the next substantive specification revision rather than restaking 22 binding
  citations for a name gloss.
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
- **`begin()` succeeding proves nothing about a radio pin map.** A wrong `rf_sw`
  initialises just as cleanly and transmits into a dead end. Only frames out and echoes back
  prove it.

## Open, and not closable from here

- **P8** (`CommandGate`, D34) — the only outstanding library work. **Gates simnode B0.**
- **The bridge target is not in CI**, and by decision rather than oversight (2026-09-10).
  It is the first firmware here needing `secrets.h`, which the workflow header says is a
  decision to take rather than a secret to paste. **Taken: the host tests belong in the
  `native` job, the target build stays out while it is a banner.** Neither is wired up —
  the workflow edit needs a token scope refresh — so `pio run -d firmware/bridge -e heltec`
  **is verified locally only, and `main` being green does not cover it.**
- **M22** — bridge LoRa PER with WiFi idle versus saturated. The evidence for §4.4's
  deliberate lack of mutual exclusion; **V-B12** is its verification row. Without it the
  asymmetry rests on argument alone.
- **Protocol Spec §5.3's `(LoRaBridge)` gloss** — deferred by choice, recorded in **D17**.
- **GateLink M0's LDO margin above the operating point** — the rail is sized for Envelope
  B's 19.6 dBm but will be tested only at −4 dBm. M0 says to re-run if Envelope B is ever
  triggered.

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
