# LRAN bridge firmware — prioritized task list

**Document:** `LRAN-Bridge-Firmware-Tasks`
**Version:** 0.1
**For:** Claude Code, working in `firmware/bridge/` and `firmware/simnode/`
**Requirements source:** [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md) v0.8
**Build source:** [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) v0.13
**Binding protocol:** [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) **v0.9**
**Shared codec:** [`LRAN-Protocol-Library-Implementation-Plan`](../shared/LRAN-Protocol-Library-Implementation-Plan.md) v0.5
**Decision status:** [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md)
**Last updated:** 2026-09-10

> **This document owns no requirement and no acceptance criterion.** Milestones **B0–B7**
> and their acceptance criteria belong to Implementation Plan §8; requirements belong to
> the PRD. What lives here is the **order the work should be done in**, the task-level
> breakdown under each milestone, and **which model each task is suited to**. Where this
> document and the implementation plan disagree about what a milestone requires, the plan
> is right.

Task identifiers are **`BF-*`**, a new family alongside `R-*`, `BG-*`, `BS-*`, `V-B*`,
`D*`, `W*` and `M*`. Cite them in commits and PR descriptions as root `CLAUDE.md` asks.

---

## Table of contents

1. [Where the work actually stands](#1-where-the-work-actually-stands)
2. [How to read the model column](#2-how-to-read-the-model-column)
3. [Phase 0 — unblock](#3-phase-0--unblock)
4. [B0 — simnode bring-up](#4-b0--simnode-bring-up)
5. [B2 — bridge board bring-up and OTA](#5-b2--bridge-board-bring-up-and-ota)
6. [B3 — protocol and registry](#6-b3--protocol-and-registry)
7. [B4 — MQTT, discovery and publication policy](#7-b4--mqtt-discovery-and-publication-policy)
8. [B5–B7 — HEX proxy, integration, soak](#8-b5b7--hex-proxy-integration-soak)
9. [Delegating a task safely](#9-delegating-a-task-safely)
10. [Changelog](#10-changelog)

---

## 1. Where the work actually stands

Run these rather than trusting a sentence here — where `main` points and what is open is
derivable state, and this document does not record it (root `CLAUDE.md`):

```bash
git fetch origin -p && git log --oneline -5 origin/main
pio test -d lib/lran-protocol -e native
```

**What the documents record as settled:**

| Item | State |
|---|---|
| `/lib/lran-protocol/` **P1–P7** | Met, against specification v0.6 — 107 host tests, 110 on target, 72 W4 vectors |
| `/lib/lran-protocol/` **P8** (`CommandGate`, D34) | **Outstanding.** The only library work between here and simnode B0 |
| Range test **pass 1 and pass 2** | Complete. **B1a and B1b are done** — the gate closed 0 % PER at the D33 ceiling on the deployed pairing |
| **M6** (both bearings), **M20** (ambient survey), **M21** (grant conditions) | **Closed** |
| **D1** (SF / BW / CR / TX power / frequency) | **Open — and this is the constraint that matters** |

### 1.1 D1 is now a decision, not a measurement

**Every input D1 was waiting on has closed.** Register §2.1 names four bounds — TX power
capped by D33, frequency requiring the M20 survey, the site's known occupants, and `BW`
bound to the rule section — and M6, M20 and M21 have all reported. What remains is
someone choosing SF, BW, CR, frequency and power *in one motion* and recording it in the
register.

Two results constrain the choice and are easy to lose:

- **The provisional 915.0 MHz must move.** It is `weather-island`'s own peak at −80 dBm
  against a −115 dBm floor (register §2.2). Envelope A's genuinely uncommitted region is
  roughly **915.2–923.0 MHz**.
- **The SF7 tail is thinner than the mean suggests.** B1b's margin on the mean is
  17–25 dB of SNR, but one probe reached **2.2 dB at −119.0 dBm**. That pulls against
  W9's preference and is a live constraint on the SF choice.

**D1 does not block B0 or B2.** Bench work runs at whatever provisional channel the
range test used. It blocks anything that ships and it blocks **M19**'s airtime
regeneration, so close it before B3 rather than after.

---

## 2. How to read the model column

The split below is a **delegation heuristic, not a capability claim**. It uses one test,
and it is the test this repo already applies everywhere else:

> **If this is wrong, what finds out — a committed test, or the field?**

**Sonnet** suits a task whose correctness is settled by something already written down:
a spec table to transcribe against, a W4 vector that fails, a Unity assertion, an
acceptance criterion in Implementation Plan §8. The work is real but the judgement has
already been made, and a wrong answer is loud.

**Opus** suits a task where a wrong answer is **silent, expensive, or both** — the
failure shows up as a second relay pulse at a gate, a dashboard of plausible stale
numbers, a battery charged on the wrong profile, or a protocol seam that only reveals
itself when WellLink is commissioned in eighteen months. These are also the tasks that
need several documents held in view at once.

That test lines up almost exactly with the repo's own governing question — *does changing
this require a physical visit to a node?* — which is not a coincidence. The same
asymmetry that put the fleet's complexity on the bridge is what makes those parts
expensive to get wrong.

**Two standing rules for the whole list:**

1. **A Sonnet task is only delegable once its acceptance criterion is written.** If the
   criterion is "it works", the task is not ready to hand off, whatever the model.
2. **Do not spawn subagents unless the operator asks.** This column says which tasks are
   *suited* to delegation; the operator decides whether any of it is delegated at all.

---

## 3. Phase 0 — unblock

Neither task is bridge firmware. Both gate it.

| # | Task | Model | Why |
|---|---|---|---|
| **BF-0** | **Close D1** — fix SF, BW, CR, frequency and conducted power in one motion; record in the register; regenerate the airtime table (**M19**) | **Opus** | Four bounds interacting across three documents, with a measured SF7 tail pulling against W9 and a frequency that must move off a confirmed occupant. A wrong choice here is re-flashed into every node on the property |
| **BF-1** | **`CommandGate`** — library milestone **P8**, D34. §9.4 steps 4–5 plus step 6's high-water update, per peer | **Opus** | This *is* root rule 2. Dedup must return the **cached** ACK without re-executing; the step-4-before-step-5 order must be asserted by a test that fails if reversed. The failure mode is a second pulse at a driveway gate |

**BF-1 gates B0. BF-0 should close before B3** and can run in parallel with everything.

---

## 4. B0 — simnode bring-up

**Gated on `/lib/lran-protocol/` P6 and P8.** Build the instrument before the thing it
measures — a B3 failure must not be ambiguous between the two (Implementation Plan §8).

| # | Task | Model | Why |
|---|---|---|---|
| **BF-2** | Project skeleton, two PlatformIO environments, `RadioPins` profiles for Heltec and XIAO+Wio Kit (§10.8, §10.8.1) | **Sonnet** | The pin maps are transcribed and already proven over the air — 192 frames out, 192 echoes back. Wrong values fail loudly at `begin()` or at the first probe |
| **BF-3** | Identity table — up to four logical nodes, independent key, `ctx_id`, sequence spaces, `enabled` (§10.3) | **Opus** | The bridge must not be able to tell four identities on one radio from four radios. If anything keys on the radio rather than `node_id`, **BG-2** is already broken and the seam stays hidden until WellLink |
| **BF-4** | Serial console — every command in §10.4's table | **Sonnet** | A closed command table with defined effects. Wrong parsing is immediately visible at the prompt |
| **BF-5** | `ROLE_RANGE` and `ROLE_HEALTH` (§10.2) | **Sonnet** | Deliberately impoverished by design. `PING` echo and `0xF0` on poll |
| **BF-6** | `ROLE_GATELINK` — `0xFE` status, `0x11` events, `COMMAND_ACK`, `0x12` config (§10.2) | **Opus** | The only role that accepts a `COMMAND`, so it is where `CommandGate` is exercised and where the synthetic marking rule bites. Synthetic data reaching HA history unmarked is **a bug in both nodes at once**, and it looks like real history |
| **BF-7** | `/lib/lran-sim/` — the **patch-after-encode primitive** (§10.5.2) | **Opus** | §10.6 rule 1: never a second serializer. The primitive's surface is what keeps that true while making `oversize`, `frag_zero` and `frag_command` reachable. Scope it before B0, not during — "discovering it mid-milestone is how a second serializer gets written" |
| **BF-8** | The §10.5 fault catalogue — 27 entries against the primitive from BF-7 | **Sonnet** | Each row states the frame, the counter and the expected behaviour, and §14.1 is the normative counter registry. Table-driven, verifiable, high volume — the best delegation candidate in the list |
| **BF-9** | Fault self-disarm and OLED armed-state display (§10.6 rule 2) | **Sonnet** | Bounded count, then disarm. A short rule with an obvious test |

> **BF-8 depends on BF-7 and must not start before it.** Handing the catalogue out while
> the primitive is still undesigned is the exact path §10.5.2 warns about.

---

## 5. B2 — bridge board bring-up and OTA

**Gated on P7 only** (met), so this runs in parallel with B0.

| # | Task | Model | Why |
|---|---|---|---|
| **BF-10** | Project skeleton, `secrets.h.example`, `native` environment, `CLAUDE.md` (§5.4, §11.4) | **Sonnet** | Layout is specified. One rule to honour: **`secrets.h` in `.gitignore` in the first commit, before it exists** |
| **BF-11** | **Task structure** — the seven tasks of §5.2, priorities, and the never-block rule | **Opus** | *"`lora_task` is highest priority and never blocks on the network"* is the one place a naive "publish inline on receive" quietly loses data. Getting the priorities and queue boundaries right is architecture, and retrofitting them is not a small edit |
| **BF-12** | WiFi station, reconnect, `MqttTransport` interface over PubSubClient, LWT (§4.3) | **Sonnet** | Well-trodden, and the one trap is written down: **`MQTT_MAX_PACKET_SIZE` ≥ 1024 in the build flags on day one**, or discovery configs vanish with no error |
| **BF-13** | **OTA — A/B partition table and rollback** (§6.5) | **Opus** | The partition table is a build-time decision that *cannot* be retrofitted without a USB flash, and the bridge is the node whose failure takes the whole property's telemetry. **V-B9** requires a deliberately bad image to roll back |
| **BF-14** | OLED status page (§5.3 `ui.cpp`) | **Sonnet** | R-4.1c is MAY-level — a glanceable "N nodes online". Reuse the ThingPulse driver and the range test's Vext bring-up sequence |

---

## 6. B3 — protocol and registry

**Gated on B2 and B0.** The largest milestone, and the one carrying most of the
protocol risk.

| # | Task | Model | Why |
|---|---|---|---|
| **BF-15** | **Per-node registry** — §4.2's table, HKDF key derivation at load, `is_bench` (§4.2, R-3.1c) | **Opus** | The abstraction the whole fleet story rests on. *"If adding a node requires touching the scheduler, the availability watchdog or the MQTT layer, the abstraction has leaked."* The bench IDs being ordinary entries is itself the test |
| **BF-16** | `lora_link.cpp` — RadioLib, frame in and out, MAC verify, reassembly (§5.3) | **Opus** | Where the never-block rule is honoured or lost, and where reassembly state either respects §11.2 or destroys a peer's in-progress set |
| **BF-17** | Poll scheduler — per-node interval, **fleet-wide serialization** (§6.1, R-3.1d) | **Sonnet** | One timer per node and one outstanding poll fleet-wide. Cheap, bounded, and testable against simnode |
| **BF-18** | **Command path and retry** — §6.2's state machine, **same `seq` on retry** (**BS-3**) | **Opus** | Root rule 2 at the bridge end. Incrementing `seq` on retry *looks like a fix for a stuck command* and is a second gate command. The context resync must retry exactly once — a resync loop is a transmit storm across the whole channel |
| **BF-19** | §14 discard ladder wiring — every counter in `kCounterRegistry`, named and published | **Sonnet** | The registry is normative and the fault catalogue tests each stage. Mechanical, high-volume, and caught immediately by BF-8's faults |
| **BF-20** | Availability watchdog — `missed_poll_threshold`, retained publication (§3.4) | **Sonnet** | Four requirements, a default of 3, and **V-B3** tests it by stopping one logical identity |
| **BF-21** | `simctl` scenario scripts for the whole §10.5 catalogue (§7.2) | **Sonnet** | Scripting a table that already exists. The entries most likely to be skipped by hand are the ones whose correct result is *nothing happens*, which is exactly what a script does not skip |
| **BF-22** | Version tolerance — accept N and N−1, per-node downgrade, distinct reason for unsupported (**V-B10**, R-3.1e/f) | **Opus** | This is what makes an incremental rollout possible instead of a flag day, on a fleet where a flag day means a walk to the gate |

---

## 7. B4 — MQTT, discovery and publication policy

Reachable with **no node hardware present** (**V-B11**). Develop against the dev HA VM
and a dev broker, not production, until B6 (§11.3).

| # | Task | Model | Why |
|---|---|---|---|
| **BF-23** | Discovery generation, per-node-type templates, republish on broker reconnect (§4.4, R-3.3b/c/d) | **Sonnet** | Payload shape is specified and example payloads are committed to `/ha/`. The reconnect path is the one that gets skipped, so make it an explicit test rather than a hope |
| **BF-24** | **Publication policy** — `publish.cpp`, §6.3's whole table | **Opus** | **R-5.2b is the requirement most easily lost in implementation**, because republishing the cached value is the path of least resistance and produces a dashboard that looks healthy. A dead VE.Direct link showing plausible unchanged numbers indefinitely is worse than an obviously unavailable entity |
| **BF-25** | Event republication — non-retained, dedup on `(src, ctx_id, event_id)` (§6.3, **V-B8**) | **Opus** | These drive email and SMS. A retained event replays on every HA restart and discovery refresh, and the failure is a phone buzzing at 3 AM about a gate that opened last week |
| **BF-26** | Bench publication gate — `simnode_diag_enable` (§4.2a) | **Sonnet** | The table in §4.2a is the implementation. One rule carries the weight and is stated: **gate on publication, never on reception** |
| **BF-27** | Debug tooling — dummy publish, bridge-side simulators, raw frame log (§6.6) | **Sonnet** | Specified per tool. One constraint to respect: the bridge-side simulator and `simnode` **must not share a generator** |

---

## 8. B5–B7 — HEX proxy, integration, soak

| # | Task | Model | Why |
|---|---|---|---|
| **BF-28** | HEX wrap/unwrap, request and response plumbing, retained audit trail (§6.4) | **Sonnet** | Transport and logging. The node is transport only; the shape is written out in §6.4's flow |
| **BF-29** | **Write authorization** — arm, auto-expiry, refusal while disarmed, the three gates (§3.5b, **BS-2**) | **Opus** | *"The failure mode is battery damage, and it is invisible until it is not."* Three gates are only three gates if each is independently enforced and independently tested (**V-B6**) |
| **BF-30** | Register semantics for charge-parameter readback as diagnostic sensors (R-3.5d) | **Opus** | Getting a register wrong under LiFePO4 is a battery-damage path, and this is the most change-prone part of the interface. Do **not** model 100+ registers as entities (R-3.5e) |
| **BF-31** | **B6** GateLink integration, **B7** soak | **Opus** | Cross-node triage against real hardware. The work is mostly the operator's; the model's job is reading a symptom across the bridge, the node, the RF path and HA at once |

---

## 9. Delegating a task safely

A subagent starts cold. It has none of this conversation and none of the reasoning above,
so a task handed over needs to carry its own context.

**Give a delegated task all four of these, or do not delegate it:**

1. **The `BF-*` identifier and its milestone**, so the acceptance criterion is findable in
   Implementation Plan §8.
2. **The reading list, scoped** — the sections that bind this task and no more. The range
   test tasks document's *"Read these, and only these sections"* table is the pattern that
   worked.
3. **The invariants it could break.** Root `CLAUDE.md`'s nine rules are not optional and
   are not obvious from the code: never `memcpy` a struct to the wire, never vary `seq` on
   retry, no dynamic allocation, never discard a frame silently, reserved bits written
   zero and ignored, sentinels not zero, the library keeps building `native`, timing is
   runtime-configurable, RadioLib pinned.
4. **How its output gets checked** — which test, which vector, which milestone criterion.

**Review every delegated result against the invariant, not against the diff.** The
failures this repo cares about are the ones that compile, pass a naive test and look
right: a symmetric encoder and decoder agreeing on a wrong offset, a simnode validated
only against the bridge, a cached value republished as current.

---

## 10. Changelog

| Version | What changed |
|---|---|
| **v0.1** | Initial release — task breakdown under B0–B7, with model suitability |

- **v0.1** — Initial release. Created because Implementation Plan §8 owns **milestones and
  acceptance criteria** but nothing owned the **task-level breakdown or the order**, and
  the range test's own pass 1 and pass 2 task documents had already established where that
  belongs. Follows those documents' shape deliberately. **Adds no requirement and no
  acceptance criterion**; every task points at the plan or the PRD for what "done" means.
  **Two things this list surfaced that the plan does not state in one place:** that **D1's
  inputs have all closed**, so it is now a decision to make rather than a measurement to
  run — and that **BF-7's patch-after-encode primitive must be scoped before the fault
  catalogue is handed to anyone**, which §10.5.2 says and no ordering document carried.
