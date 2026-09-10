# Bridge Node — session handoff

**Written 2026-09-10, at the end of the session that audited the system and bridge
documents, retired the `LoRaBridge` name, reconciled bench TX power with D33, and produced
the bridge firmware task list.** It replaces the previous file wholesale.

> **This file goes stale, and it is rewritten rather than annotated.** It records *session
> state and next actions*, nothing else. That is what separates it from the engineering
> log, which is a dated record and is only ever appended to. Where this file disagrees with
> the documents below, they win — check the log's last entry against the date above before
> trusting anything here.

## The next job, in one place

**Close D1, and it does not start in this directory.**

D1 fixes SF, BW, CR, frequency and conducted power. **Every input it was waiting on has
closed** — M6, M20 and M21 have all reported — so this is a decision to make, not a
measurement to run, and no bench or field work is owed. The options, the evidence and a
recommendation are assembled in
[`LRAN-D1-PHY-Decision-Brief`](../shared/LRAN-D1-PHY-Decision-Brief.md); the outcome is
recorded in [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md), which is the
only file that may state D1's status.

**If D1 is not the job you want**, `BF-10` through `BF-14` in
[`LRAN-Bridge-Firmware-Tasks`](./LRAN-Bridge-Firmware-Tasks.md) are unblocked and run in
parallel — bridge board bring-up (**B2**), gated only on protocol library **P7**, which is
met. **`BF-11`, the task structure, is the one to think hardest about**: `lora_task` is
highest priority and never blocks on the network, and retrofitting that is not a small edit.

**Simnode B0 is blocked** on library milestone **P8** (`CommandGate`, D34), which is the
only library work outstanding.

## What the last session established

**No firmware was written. This was a documents session**, and the results are findings
about the document set rather than measurements.

- **D1's inputs have all closed**, which no single document said in one place. The register
  carries the four bounds in §2.1 and their closures in §2.2 and §5.4; nothing joined them
  up into "this is now a decision."
- **`+22 dBm` appeared in four places across two node documents as an operating point.**
  Neither D33 envelope permits it: Envelope A caps conducted power at **−4 dBm** with the
  fitted 3.0 dBi antenna, Envelope B's ceiling is the modules' tested **19.6 dBm** (Wio) and
  **13.9 dBm** (Heltec). Corrected in Bridge Implementation Plan §2.2, §2.3 and §7.2, and in
  GateLink Implementation Plan §3.4, §8's **M0** and §9.7. **This does not support a claim
  that any rail or budget decision changed** — every one of them gained headroom.
- **`LoRaBridge` and `Bridge Node` had both been live names for the whole document set**,
  and **D17 recorded the retired one**. Because the register is the only place a decision's
  status lives, nothing else could reconcile until it did.
- **The Bridge PRD had two verification rows numbered `V-B2`**, so the WiFi/LoRa coexistence
  criterion added in its v0.5 was verified nowhere. It is now `V-B12`.
- **Bridge Implementation Plan §8 and §11.1 gated simnode B0 on library P6 alone.** The
  library plan's §6 has said since its v0.3 that **P8 gates B0 as well**.

## Read these, in this order

| # | Document | Why |
|---|---|---|
| 1 | **this file** | where things stand, and what to do next |
| 2 | [`LRAN-D1-PHY-Decision-Brief`](../shared/LRAN-D1-PHY-Decision-Brief.md) | the next job. Options, evidence and a recommendation, assembled from five documents |
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
| Done | Library **P1–P7**. Range test **pass 1** and **pass 2**. **B1a**, **B1b**. **M6**, **M20**, **M21** |
| Queue | **D1** (decision). Library **P8**. Then **B0** and **B2** in parallel, **B3** after both |

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

## Behaviour that changed, and will make older artifacts read differently

- **`LoRaBridge` is retired in favour of `Bridge Node`, 2026-09-10** (**D17** amended).
  `lran-bridge` remains the firmware target and **"LoRa Bridge" remains the HA device
  name**. **Protocol Spec §5.3 still glosses node `0x00` as `(LoRaBridge)`** — deliberately,
  deferred to the next substantive specification revision rather than restaking 22 binding
  citations for a name gloss.
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

- **D1** — SF/BW/CR/frequency/power. Open, all inputs closed; a decision, not a measurement.
  Closed by recording the choice in the register. See the brief.
- **P8** (`CommandGate`, D34) — the only outstanding library work. **Gates simnode B0.**
- **M22** — bridge LoRa PER with WiFi idle versus saturated. The evidence for §4.4's
  deliberate lack of mutual exclusion; **V-B12** is its verification row. Without it the
  asymmetry rests on argument alone.
- **M19 / W7** — the airtime table regenerates once D1 fixes SF. Blocked on D1, not on work.
- **Protocol Spec §5.3's `(LoRaBridge)` gloss** — deferred by choice, recorded in **D17**.
- **GateLink M0's LDO margin above the operating point** — the rail is sized for Envelope
  B's 19.6 dBm but will be tested only at −4 dBm. M0 says to re-run if Envelope B is ever
  triggered.

### Closed, and not to be reopened by habit

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
