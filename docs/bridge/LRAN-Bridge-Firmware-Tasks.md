# LRAN bridge firmware — prioritized task list

**Document:** `LRAN-Bridge-Firmware-Tasks`
**Version:** 0.37
**For:** Claude Code, working in `firmware/bridge/` and `firmware/simnode/`
**Requirements source:** [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md) v0.14
**Build source:** [`LRAN-Bridge_Node-Implementation-Plan`](./LRAN-Bridge_Node-Implementation-Plan.md) v0.48
**Binding protocol:** [`LRAN-Protocol-Specification`](../shared/LRAN-Protocol-Specification.md) **v0.13**
**Shared codec:** [`LRAN-Protocol-Library-Implementation-Plan`](../shared/LRAN-Protocol-Library-Implementation-Plan.md) v0.12
**Decision status:** [`LRAN-Decision-Register`](../shared/LRAN-Decision-Register.md)
**Last updated:** 2026-09-23

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
| `/lib/lran-protocol/` **P8** (`CommandGate`, D34) | **Met 2026-09-11**, on D34 as amended — 127 host tests, 130 on target. No library work stands between here and simnode B0 |
| Range test **pass 1 and pass 2** | Complete. **B1a and B1b are done** — the gate closed 0 % PER at the D33 ceiling on the deployed pairing |
| **M6** (both bearings), **M20** (ambient survey), **M21** (grant conditions) | **Closed** |
| **D1** (SF / BW / CR / TX power / frequency) | **Closed 2026-09-10** — 917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted. **D33 closed with it**, on Envelope A |

### 1.1 D1 is closed — what the firmware inherits

**D1 and D33 closed together on 2026-09-10.** The firmware builds against **917.4 MHz,
SF9, BW 125 kHz, CR 4/5, −4 dBm conducted** with a 3.0 dBi antenna, under §15.249
Envelope A. Protocol Spec §12.1 states them, §12.3 carries the `backoff_max_ms` default
they moved, and Decision Register §3.4 records why.

Three consequences reach the code rather than the documents:

- **`backoff_max_ms` defaults to 1500, not 500.** A maximum `PING` at SF9 runs 1107 ms, so
  the old window could not outlast the frame it backed off for. It is runtime-configurable
  from HA, which is what made SF9 affordable — the SF it protects is not.
- **The PHY parameters change at runtime only through §12.4's commit-and-revert**
  (Protocol Spec v0.13, **D56**). §12.1's *"not runtime-configurable"* was withdrawn in
  v0.13. A node that boots on the wrong channel is a walk to the gate with a laptop, and
  the revert window exists for that case. Until **BF-33** builds it, the six PHY rows are
  `READ_ONLY`. The pin map, TCXO voltage and RF-switch flag stay in the injected radio
  config (§12.2).
- **`cad_backoffs` is the instrument to watch after bring-up.** M20 measured 125 kHz every
  200 kHz, so 37.5 % of the band was never looked at, and §12.3's retry defaults were
  chosen against an empty channel.

**The range-test firmware still transmits on the provisional 915.0 MHz**, which is
`weather-island`'s own peak. It is a bench instrument and no task here depends on it, but a
re-run on the old channel produces data that will be distrusted later.

---

### 1.2 The bridge in CI — the decision, and what it covers

**`firmware/bridge/` is the first firmware in this repo that needs `secrets.h`** — it is
the only one holding `LRAN_MASTER_KEY`, WiFi and broker credentials (Impl Plan §11.4).
[`ci.yml`](../../.github/workflows/ci.yml) anticipated this in its own header: *"a target
that starts needing `secrets.h` needs a decision about CI, not a secret pasted into a
workflow."*

**The decision, taken 2026-09-10 and wired up at the end of BF-12: CI copies the committed
template.** `secrets.h.example` holds placeholders and a 32-zero-byte `LRAN_MASTER_KEY`, so
**nothing secret enters the workflow**, and a build made that way cannot pass for a
provisioned one — `main.cpp` checks the key at boot and says so on every line of its log.
What CI tests is that the bridge **compiles**; anything needing a real key is bench work
against a real broker (**B4**).

All three now run on every pull request:

```bash
python3 tools/checks/lora_task_never_blocks.py   # `checks` job, seconds, no toolchain
pio test -d firmware/bridge -e native            # `native` job, no secrets
pio run  -d firmware/bridge -e heltec            # `firmware` job, template copied first
```

**What CI still does not cover.** Everything that needs a radio, an access point or a
broker: the reconnect actually reconnecting, the LWT actually landing, a discovery config
actually appearing in Home Assistant. Those are B2 and B4 bench work, and the host tests are
deliberately arithmetic and string handling so the bench is spent on what only the bench can
answer.

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
| ~~**BF-0**~~ | ~~**Close D1**~~ — **done 2026-09-10.** SF9 / BW125 / CR 4/5 / 917.4 MHz / −4 dBm conducted, recorded in Decision Register §3.4 and stated in Protocol Spec v0.10 §12.1. **M19 done and W7 closed** — §15.1's table was already on this basis and needed confirming rather than recomputing | **Opus** | Four bounds interacting across three documents, with a measured SF7 tail pulling against W9 and a frequency that must move off a confirmed occupant. A wrong choice here is re-flashed into every node on the property |
| ~~**BF-1**~~ | ~~**`CommandGate`**~~ — **done 2026-09-11**, library milestone **P8** on D34 as amended (Decision Register §3.2.1). The mark advances in `check()`; a retry inside the execution window is `InFlight` and gets no answer. The superseded [`LRAN-P8-CommandGate-Brief`](../shared/LRAN-P8-CommandGate-Brief.md) keeps the reasoning | **Opus** | This *is* root rule 2. Dedup must return the **cached** ACK without re-executing; the step-4-before-step-5 order must be asserted by a test that fails if reversed. The failure mode is a second pulse at a driveway gate |

**Both are done.** BF-1 was the last library gate on B0; BF-0 closed D1 ahead of B3.

---

## 4. B0 — simnode bring-up

**Gated on `/lib/lran-protocol/` P6 and P8.** Build the instrument before the thing it
measures — a B3 failure must not be ambiguous between the two (Implementation Plan §8).

| # | Task | Model | Why |
|---|---|---|---|
| **BF-2** | Project skeleton, two PlatformIO environments, `RadioPins` profiles for Heltec and XIAO+Wio Kit (§10.8, §10.8.1). **Built 2026-09-14; Heltec proven on air. The XIAO first ran `simnode-xiao-wio` on 2026-09-15**, at B0's acceptance — Impl Plan §10.9 | **Sonnet** | The pin maps are transcribed and already proven over the air — 192 frames out, 192 echoes back. Wrong values fail loudly at `begin()` or at the first probe |
| **BF-3** | Identity table — up to four logical nodes, independent key, `ctx_id`, sequence spaces, `enabled` (§10.3). **Built 2026-09-14, host-tested; three identities across two boards on air** | **Opus** | The bridge must not be able to tell four identities on one radio from four radios. If anything keys on the radio rather than `node_id`, **BG-2** is already broken and the seam stays hidden until WellLink |
| **BF-4** | Serial console — every command in §10.4's table. **Built 2026-09-14**: the core first, `fault` with BF-8, and `push`, `event`, `ack`, `field` with BF-6 | **Sonnet** | A closed command table with defined effects. Wrong parsing is immediately visible at the prompt |
| **BF-5** | `ROLE_RANGE` and `ROLE_HEALTH` (§10.2). **Built 2026-09-14; PING echo proven on air**, single, full-size and 15-fragment | **Sonnet** | Deliberately impoverished by design. `PING` echo and `0xF0` on poll |
| **BF-6** | `ROLE_GATELINK` — `0xFE` status, `0x11` events, `COMMAND_ACK`, `0x12` config (§10.2). **Built 2026-09-14, host-tested; on air from 2026-09-15** — Impl Plan §10.9.2. The five command-path faults came with it | **Opus** | The only role that accepts a `COMMAND`, so it is where `CommandGate` is exercised and where the synthetic marking rule bites. Synthetic data reaching HA history unmarked is **a bug in both nodes at once**, and it looks like real history |
| **BF-7** | `/lib/lran-sim/` — the **patch-after-encode primitive** (§10.5.2). **Built 2026-09-14**; host-tested against the W4 negative vectors, and not called by the simnode until BF-8 | **Opus** | §10.6 rule 1: never a second serializer. The primitive's surface is what keeps that true while making `oversize`, `frag_zero` and `frag_command` reachable. Scope it before B0, not during — "discovering it mid-milestone is how a second serializer gets written" |
| **BF-8** | The §10.5 fault catalogue — 27 entries against the primitive from BF-7. **Built 2026-09-14**; host-tested against the codec's receive ladder, command-path entries wait for BF-6 | **Sonnet** | Each row states the frame, the counter and the expected behaviour, and §14.1 is the normative counter registry. Table-driven, verifiable, high volume — the best delegation candidate in the list |
| **BF-9** | Fault self-disarm and OLED armed-state display (§10.6 rule 2). **Built 2026-09-14; confirmed on the Heltec's panel by eye, and on the XIAO's on 2026-09-15**, where the operator watched a fault bar count down and clear. Self-disarm came with BF-8. Both profiles drive a panel (Impl Plan §10.9.1) | **Sonnet** | Bounded count, then disarm. A short rule with an obvious test |

> **BF-8 depends on BF-7 and must not start before it.** Handing the catalogue out while
> the primitive is still undesigned is the exact path §10.5.2 warns about.

---

## 5. B2 — bridge board bring-up and OTA

**Gated on P7 only** (met), so this runs in parallel with B0.

| # | Task | Model | Why |
|---|---|---|---|
| ~~**BF-10**~~ | ~~Project skeleton, `secrets.h.example`, `native` environment, `CLAUDE.md` (§5.4, §11.4)~~ — **done 2026-09-10.** `firmware/bridge/` builds on `heltec` and passes three `native` tests that link the shared codec from this project. `secrets.h` was already gitignored and `secrets.h.example` already written; what this task added is the build that consumes them, and a boot check for the template's all-zero key. **The bridge target is not in CI** — see §1.2 | **Sonnet** | Layout is specified. One rule to honour: **`secrets.h` in `.gitignore` in the first commit, before it exists** |
| ~~**BF-11**~~ | ~~**Task structure** — the seven tasks of §5.2, priorities, and the never-block rule~~ — **done 2026-09-10.** Seven static FreeRTOS tasks, four queue boundaries, drop-newest-and-count on a full queue, and the numbers argued in Impl Plan §5.2.1. The never-block rule has `tools/checks/lora_task_never_blocks.py` rather than only a paragraph. **Two follow-ons split out: BF-11a and BF-11b** | **Opus** | *"`lora_task` is highest priority and never blocks on the network"* is the one place a naive "publish inline on receive" quietly loses data. Getting the priorities and queue boundaries right is architecture, and retrofitting them is not a small edit |
| **BF-11a** | **Log queue and `log_task`'s drain** — `LogMessage`, the leveled serial log, the raw frame log (§6.6) | **Sonnet** | The queue's depth and its drop accounting exist; what is missing is the message type and the drain. Lowest priority on purpose — a log that can preempt the radio changes what it measures |
| **BF-11b** | **Hardware watchdog, fed from `sched_task`** (§5.2) | **Sonnet** | One feed point, and it must be the task that would notice a stall. Enabling it before BF-16 means a watchdog reset for a radio that is not there yet |
| ~~**BF-12**~~ | ~~WiFi station, reconnect, `MqttTransport` interface over PubSubClient, LWT (§4.3)~~ — **done 2026-09-10.** Capped exponential reconnect (1 s → 30 s, deterministic), `MqttTransport` with `PubSubTransport` behind it, LWT on `lran/bridge/availability`, and spec §16.3's never-retain-an-event rule enforced on the publish path rather than trusted. **The bridge joined CI with this task** — §1.2 | **Sonnet** | Well-trodden, and the one trap is written down: **`MQTT_MAX_PACKET_SIZE` ≥ 1024 in the build flags on day one**, or discovery configs vanish with no error |
| ~~**BF-13**~~ | ~~**OTA — A/B partition table and rollback** (§6.5)~~ — **built 2026-09-10; V-B9 not yet run.** Committed partition table, ArduinoOTA, and a rollback verdict that replaces Arduino-ESP32's default — which marks every image valid before `setup()` and would have kept an image that never finds the LAN. Impl Plan §6.5.1–§6.5.2 | **Opus** | The partition table is a build-time decision that *cannot* be retrofitted without a USB flash, and the bridge is the node whose failure takes the whole property's telemetry. **V-B9** requires a deliberately bad image to roll back |
| ~~**BF-14**~~ | ~~OLED status page (§5.3 `ui.cpp`)~~ — **built 2026-09-10; not yet seen on the panel.** Host-tested page model, ThingPulse renderer, Vext order from the range test, burn-in mitigation, `--` for unknown. Impl Plan §5.1.2 | **Sonnet** | R-4.1c is MAY-level — a glanceable "N nodes online". Reuse the ThingPulse driver and the range test's Vext bring-up sequence |

---

## 6. B3 — protocol and registry

**Gated on B2 and B0.** The largest milestone, and the one carrying most of the
protocol risk. **Split on 2026-09-14 into B3a and B3b** (Impl Plan §8 v0.32): **B3a** is
BF-15, BF-16, BF-17, BF-19 and BF-20, **accepted 2026-09-16**; **B3b** is BF-15a and
BF-19a (both accepted on air, 2026-09-16), BF-18, BF-21 and BF-22, **accepted
2026-09-17**.

**B3b's last criterion left the milestone rather than gaining a task.** V-B12 moved to
B4 on 2026-09-17, because its saturated arm needs a runtime lever **BF-23** builds and
bench diagnostics **BF-26** builds. Impl Plan §8.1 is the record, and no `BF-*` number
was created.

**Spec v0.12 answered what B3b was waiting for** (2026-09-16, `LRAN-Spec-v0.12-Brief`,
Decision Register D35–D42). BF-18 has §6.3's `detail` for a `DUPLICATE_CACHED` result and
§7.4's rule for a repeated `CONFIG`; BF-19a has §14.2; BF-15a is new and exists because
§14 now names the stage the bridge has been counting without one.

| # | Task | Model | Why |
|---|---|---|---|
| **BF-15** | **Per-node registry** — §4.2's table, HKDF key derivation at load, `is_bench` (§4.2, R-3.1c). **Built 2026-09-14, host-tested** — Impl Plan §4.2.1 | **Opus** | The abstraction the whole fleet story rests on. *"If adding a node requires touching the scheduler, the availability watchdog or the MQTT layer, the abstraction has leaked."* The bench IDs being ordinary entries is itself the test |
| **BF-16** | `lora_link.cpp` — RadioLib, frame in and out, MAC verify, reassembly (§5.3). **Built 2026-09-13, host-tested; on air from 2026-09-15**, when B3a went on air — Impl Plan §5.3.1 | **Opus** | Where the never-block rule is honoured or lost, and where reassembly state either respects §11.2 or destroys a peer's in-progress set |
| **BF-17** | Poll scheduler — per-node interval, **fleet-wide serialization** (§6.1, R-3.1d). **Built 2026-09-14, host-tested; polling on air from 2026-09-15** — Impl Plan §6.1.1 | **Sonnet** | One timer per node and one outstanding poll fleet-wide. Cheap, bounded, and testable against simnode |
| **BF-18** | **Command path and retry** — §6.2's state machine, **same `seq` on retry** (**BS-3**), **and the bridge's MQTT receive path**. **Built and confirmed on air 2026-09-16** — `command.{h,cpp}`, 20 host tests; a command from Home Assistant executed at a simnode, a suppressed ACK was retried with the same `seq` and answered `DUPLICATE_CACHED` *not executed*, and spec §10.3's resync adopted the node's context and retried once. **`ResyncFailed` stays host-tested**: the rejection-to-retry window is under a second and the bench could not force a second `REJECTED_CTX`. Impl Plan §6.2.1 | **Opus** | Root rule 2 at the bridge end. Incrementing `seq` on retry *looks like a fix for a stuck command* and is a second gate command. The context resync must retry exactly once — a resync loop is a transmit storm across the whole channel |
| **BF-19** | §14 discard ladder wiring — every counter in `kCounterRegistry`, named and published. **Built 2026-09-14, host-tested; the discard counters are the bridge's, not per node** — Impl Plan §4.3.2 | **Sonnet** | The registry is normative and the fault catalogue tests each stage. Mechanical, high-volume, and caught immediately by BF-8's faults |
| **BF-19a** | `ERROR` replies for §14 stages 3–10, **to spec v0.12 §14.2**. **Built and confirmed on air 2026-09-16** — `error_reply.{h,cpp}`, 13 tests; ten replies read at the simnode, one per §14 stage that names one, and `errors_suppressed` reached 2. **Bound 1 stays host-only**: no unregistered source was produced on the bench. Registered sources only, rate-limited by `error_min_interval_ms` (default 1000, runtime-settable), `src` the bridge, `ctx_id` `0`, `ref_seq` the offending frame's. `BAD_CRC` and `BAD_VERSION` stay optional and unbuilt. Split from BF-19 with the operator, 2026-09-14 | **Opus** | Every reply goes to a solar node on the strength of an unauthenticated header and competes with polls for airtime. **The rate limit is the load-bearing part**: without it a forged frame makes the bridge transmit at a rate someone else chooses |
| **BF-15a** | Move the bridge's `unregistered_src` into the codec as **`rx_unknown_src`** — spec v0.12 §14 stage 9a and §14.1. **Built and on air 2026-09-16**, the published document's shape confirmed at the broker: `Status::UnknownSrc`, `lran::Counters`, `kCounterRegistry` (22 rows), the `sizeof` `static_assert`, both vector registries, and the `lran/bridge/diag/state` payload | **Sonnet** | A rename with a `static_assert` behind it, and the counter joins `rx_dropped`, which changes a published number. Home Assistant charts the old name, so the change is breaking and was made before B4 builds discovery on it |
| **BF-20** | Availability watchdog — `missed_poll_threshold`, retained publication (§3.4). **Built 2026-09-14, host-tested; bench availability unpublished until BF-26** — Impl Plan §6.1.2 | **Sonnet** | Four requirements, a default of 3, and **V-B3** tests it by stopping one logical identity |
| **BF-21** | `simctl` scenario scripts for the whole §10.5 catalogue (§7.2). **Built 2026-09-16, and run on the bench** — `tools/simctl/` with 17 host tests and `tools/checks/simctl_catalogue.py`, both in CI. Six rows read at the broker, five passing and `bad_ver` reported `DIVERGED` naming BF-22. **Both decisions made**: `fault.cpp` completes `set_displaced`'s displacing set, so the row moves one counter; and the simnode gains **`ctx_reject`**, which closed B3b's last open criterion on air — `resync_failed`, two `COMMAND`s and no third. Impl Plan §7.2.1 | **Sonnet** | Scripting a table that already exists. The entries most likely to be skipped by hand are the ones whose correct result is *nothing happens*, which is exactly what a script does not skip |
| **BF-22** | Version tolerance — accept N and N−1, per-node downgrade, distinct reason for unsupported (**V-B10**, R-3.1e/f). **Built and confirmed on air 2026-09-16** — the ladder accepts N−1 and refuses N−2, `node_tx_ver()` addresses each node in the version it announced, and an unsupported version is recorded from the discard path and published as `unsupported_ver`. `simctl`'s `bad_ver` row is green: `rx_bad_ver +1` where it read +2. **V-B10 met** | **Opus** | This is what makes an incremental rollout possible instead of a flag day, on a fleet where a flag day means a walk to the gate |

---

## 7. B4 — MQTT, discovery and publication policy

Reachable with **no node hardware present** (**V-B11**). Develop against the dev HA VM
and a dev broker, not production, until B6 (§11.3).

**V-B12 is the exception, and it is deliberate.** It arrived here on 2026-09-17 and it
needs a board, so B4's "no node hardware" claim covers the discovery and publication work
rather than every criterion in the milestone. **V-B12 is met, 2026-09-23.** A bench-only
UDP blaster saturated the bridge's WiFi, not `g_diag_interval_s`, whose 10 s floor could
not (Impl Plan §8.1.2). The loaded arm lost no more than the idle one, and Impl Plan §8.1.3
has the numbers.

**BF-33 may not belong in B4.** It is here because it consumes BF-32's table and nothing
else is closer, but spec §12.4's commit-and-revert is radio work with a bench cost of its
own. Moving it to its own milestone is the operator's call.

**BF-32 is the configuration path that BF-23's lever half and BF-26 both wait on.** It
was added on 2026-09-19, when the operator chose the general `config/set` route (**D43**)
and spec v0.13 §16.7 defined its payloads (**D48**). Build it first; the other two then
reduce to *"this value reads the table"*.

| # | Task | Model | Why |
|---|---|---|---|
| **BF-23** | Discovery generation, per-node-type templates, republish on broker reconnect (§4.4, R-3.3b/c/d). **Built and host-tested 2026-09-17**, Impl Plan §4.4.1: one device per registered node plus the bridge, retained, on boot and on every reconnect through the same path. `ha/discovery/` is generated from `discovery.cpp` and checked in CI. **The reconnect path is not an explicit test but an explicit absence of a branch** — there is no "first time" flag to skip. **Bench nodes are gated out until BF-26** (spec §16.6). **The runtime timing levers are the lever half, built and host-tested 2026-09-23**, Impl Plan §4.4.2. Each bridge row of BF-32's table now reaches its consumer: at boot after the NVS restore, and after every set that changes one. `simnode_diag_enable` is BF-26's. **Confirmed on air 2026-09-23**: an NVS restore at boot, a `diag_interval_s` set, a `poll_interval_s` retime and `sched_task`'s stack, all on the bridge board. V-B12's saturated arm is now runnable | **Sonnet** | Payload shape is specified and example payloads are committed to `/ha/`. The reconnect path is the one that gets skipped, so make it an explicit test rather than a hope |
| **BF-24** | **Publication policy** — `publish.cpp`, §6.3's whole table | **Opus** | **R-5.2b is the requirement most easily lost in implementation**, because republishing the cached value is the path of least resistance and produces a dashboard that looks healthy. A dead VE.Direct link showing plausible unchanged numbers indefinitely is worse than an obviously unavailable entity |
| **BF-25** | Event republication — non-retained, dedup on `(src, ctx_id, event_id)` (§6.3, **V-B8**) | **Opus** | These drive email and SMS. A retained event replays on every HA restart and discovery refresh, and the failure is a phone buzzing at 3 AM about a gate that opened last week |
| **BF-26** | Bench publication gate — `simnode_diag_enable` (§4.2a). **Deferred 2026-09-14** with the operator: it needs `/lib/lran-config/`, an MQTT receive path and a `lran/<node>/config/set` payload, and none exists or has a task. Until HA can set it, the bench toggle will be a serial `diag on\|off`, RAM only, off at boot (operator). **Unblocked 2026-09-19 by BF-32**, which builds all three | **Sonnet** | The table in §4.2a is the implementation. One rule carries the weight and is stated: **gate on publication, never on reception** |
| **BF-32** | **`/lib/lran-config/` and the `config/*` path** — the table (Library Plan §4, D44, D46, D47), NVS persistence (D49), the `config/set` subscriber, the split between bridge-held and node-held halves, and `config/ack` and `config/state` publication (spec §16.7), and the reassembly of a readback split across several `CONFIG_ACK` messages (spec §7.4.1, **D57**). **Added 2026-09-19. BUILT AND CONFIRMED ON AIR 2026-09-21**, Impl Plan §6.7: the library half, the bridge's stores over NVS, the `config/set` subscriber, the owner split, the node half's CONFIG and split readback, and `config/ack` and `config/state`. The bench found three defects the host tests could not — a simnode answering a solicited `CONFIG_ACK` under its status `seq`, a `sched_task` stack overflow, and a set's ACK blanking rows it did not name — all fixed; engineering log, 2026-09-21 | **Opus** | Every name in the table becomes a permanent HA `object_id`, so the operator reviews Library Plan §4's names and ranges before this codes them. **The `unknown` outcome is the path that gets skipped**: a `CONFIG` with no `CONFIG_ACK` must publish `unknown`, request a readback and publish again, never report failure or retry the write (spec §7.4). The library half is host-tested in `native`, like `/lib/lran-link/`. **The split readback has two traps of its own**: the bridge must accept more than one `CONFIG_ACK` for a single `seq`, and it must publish `config/state` only once the answer completes |
| **BF-33** | **PHY commit-and-revert** — spec §12.4 (**D56**): one atomic `CONFIG` carrying frequency, SF, BW, CR and TX power; last known-good persisted before the radio is retuned; `phy_trial_s` from apply; confirmation is a frame **received** on the new settings; revert at both ends on silence, and an `EVENT` once the link is back. Bridge and simnode. **Added 2026-09-19.** Until it lands, the PHY rows answer `READ_ONLY` (Library Plan §4) | **Opus** | **The failure mode is a node nobody can reach**, ~87 m away with no OTA. Three things carry the weight: the revert survives a reboot mid-trial, the confirmation is a frame *received* rather than one sent, and the fleet moves together because one SX1262 listens on one configuration (§12.1). TX power is clamped by D33 in the table, not by whoever types into Home Assistant |
| **BF-34** | **Context roll after a bridge restart** — spec §10.6 (**D58**, Bridge PRD **R-3.1h**). Bridge: a `POLL` to every registered node at boot, `ROLL_CONTEXT` when each node is first heard, a retry on `ACTUATOR_BUSY`, a failed roll kept pending, and no `COMMAND` or `CONFIG` until the roll completes. A refusal names the roll on `cmd/ack`, and on `config/ack` as `context_roll_pending`. The bridge counts `ctx_rolls` and `ctx_roll_failed`. Simnode `ROLE_GATELINK`: refuse a roll while a command is in flight; otherwise take a new `ctx_id` and reset both sequence spaces. **Added 2026-09-23. Built and host-tested 2026-09-23, and confirmed on air the same day** — `context_roll.{h,cpp}`, Impl Plan §6.2.2. Two points were decided with the operator the same day: a bench row rolls when first heard rather than after a boot `POLL`, and every simnode role answers a roll | **Opus** | Root rule 2 from the other side: a `seq` the node already cached is a command that is acknowledged and never runs. The roll's own retry reuses its `seq`, and a failed roll stays pending rather than falling back to commanding a node whose context is stale |
| **BF-27** | Debug tooling — dummy publish, bridge-side simulators, raw frame log (§6.6). **The raw frame log is built, 2026-09-17** (Impl Plan §6.6.1), pulled ahead of the rest for the receive path's 1 s knee. The other three tools are untouched and block nothing | **Sonnet** | Specified per tool. One constraint to respect: the bridge-side simulator and `simnode` **must not share a generator**. The log deviates from §16.2's retention rule and the deviation is **raised against the specification**, not settled in the firmware |

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
3. **The invariants it could break.** Root `CLAUDE.md`'s ten rules are not optional and
   are not obvious from the code: never `memcpy` a struct to the wire, never vary `seq` on
   retry, no dynamic allocation, never discard a frame silently, reserved bits written
   zero and ignored, sentinels not zero, the library keeps building `native`, timing is
   runtime-configurable, RadioLib pinned, TX power capped by D33.
4. **How its output gets checked** — which test, which vector, which milestone criterion.

**Review every delegated result against the invariant, not against the diff.** The
failures this repo cares about are the ones that compile, pass a naive test and look
right: a symmetric encoder and decoder agreeing on a wrong offset, a simnode validated
only against the bridge, a cached value republished as current.

---

## 10. Changelog

- **v0.37** — **V-B12 is met**, and §7 says so; its paragraph no longer names
  `g_diag_interval_s` as the saturated arm's lever. The Impl Plan citation moves from v0.47
  to v0.48. **The PRD citation moves from v0.12 to v0.14.** It missed v0.13, whose one
  change, R-3.1h's context roll, this list already builds as BF-34.

- **v0.36** — The Impl Plan citation moves from v0.46 to v0.47, which adds §8.1.2: V-B12's
  saturated arm is loaded by a UDP blaster in the `v_b12_blaster` environment.

- **v0.35** — **BF-34 is confirmed on air**: after a bridge reflash, f1's first command
  executed with no `DUPLICATE_CACHED`, and the `REJECTED_CTX`, `ACTUATOR_BUSY` and
  `ctx_roll_failed` branches passed too (engineering log, 2026-09-23). The Impl Plan
  citation moves from v0.43 to v0.46.

- **v0.34** — **BF-34 is built and host-tested**, and not yet confirmed on air. Its row
  names Impl Plan §6.2.2 and the two points the operator decided on 2026-09-23.

- **v0.33** — **Protocol specification v0.12 → v0.13, and new BF-34.** **BF-34** builds
  spec §10.6's context roll after a bridge restart (**D58**, Bridge PRD R-3.1h). §1's PHY
  bullet no longer says the PHY parameters are not runtime-configurable: spec v0.13
  withdrew that under **D56**, and §12.4's commit-and-revert is BF-33's. BF-32 already
  built against v0.13's §7.4.1 and §16.7, so its row does not change.

- **v0.32** — **BF-23's lever half is confirmed on air.** The engineering log's second
  2026-09-23 entry has the bench run.

- **v0.31** — **BF-23's lever half is built and host-tested.** The row no longer says the
  levers wait on a library with no task: BF-32 built that library, and Impl Plan §4.4.2
  records how each row reaches its consumer. It is not yet confirmed on air.

- **v0.30** — **BF-32's row gains the split readback** (spec §7.4.1, **D57**): a `GET_ALL`
  answer too large for one frame arrives as several `CONFIG_ACK` messages, and the row names
  the two traps — the bridge accepts more than one `CONFIG_ACK` per `seq`, and it publishes
  `config/state` only once the answer completes. **The shared-codec citation moves from
  v0.10 to v0.12**, reconciled rather than bumped: v0.11 declared the PHY rows, which BF-33's
  row already covers, and v0.12 added the readback count and `config_readback_timeout_ms`,
  which BF-32's row now covers.

- **v0.29** — **BF-33 added: spec §12.4's PHY commit-and-revert**, on **D56**, which brought
  frequency, SF, BW, CR and TX power into runtime configuration. The row says what carries
  the weight, and §7 says BF-33 may belong in its own milestone rather than B4.

- **v0.28** — **BF-32 added: `/lib/lran-config/` and the `config/*` path.** The operator
  chose the general `config/set` route on 2026-09-19 and accepted `LRAN-Config-Set-Brief`
  (**D43–D49**), then D50–D54 the same day. The gap BF-26's deferral and BF-23's lever half
  both named, *"none exists or has a task,"* now has one, and BF-26's and BF-23's rows say
  so. **Five rows' on-air status is corrected** from the engineering log's 2026-09-15 entry:
  the XIAO ran `simnode-xiao-wio` and showed BF-9's fault bar, and BF-6, BF-16 and BF-17
  went on air with B3a. §9 counts root `CLAUDE.md`'s rules as ten, with D33's TX-power
  cap, and the header cites Impl Plan v0.39 and Library Plan v0.10. The binding citation stays at
  v0.12 until spec v0.13's sweep.

- **v0.27** — **BF-27's raw frame log is built**, out of task order and on purpose: it was
  the last instrument the receive path's 1 s knee had left, and the other three tools in
  §6.6 block nothing. The BF-27 row says what is done and what is not. The measurement it
  produced is in the engineering log and **it did not reproduce the spacing curve**, which
  makes an interleaved sweep the thing to run before BF-24. This document inherits Bridge
  Impl Plan v0.38.

- **v0.26** — **B3b is accepted, and V-B12 left it rather than gaining a task.** The gap
  v0.25 raised — *"V-B12 remains, and no task owns it"* — is closed by moving the
  criterion to B4, where **BF-23** builds the runtime lever its saturated arm needs and
  **BF-26** builds the bench diagnostics. The idle arm was measured first, because it is
  the baseline the saturated arm is compared against and nothing blocked it;
  `tools/simctl/per_measure.py` is the instrument both arms use. This document inherits
  Bridge Impl Plan v0.37.

- **v0.25** — **BF-22 is built, and B3b's tasks are all done.** The ladder accepts N−1;
  the downgrade is per node from what `observe()` already recorded; R-3.1f's distinct
  reason survives the stage-4 discard by being read from the buffer and handed to
  `sched_task` as one atomic word, because `lora_task` cannot reach the registry.
  `simctl`'s `bad_ver` divergence is deleted and the row passes. **V-B12 remains, and no
  task owns it** — raised here rather than left implicit.

- **v0.24** — **BF-21 is built and run.** `tools/simctl/` judges the §10.5 catalogue from
  `lran/bridge/diag/state`, one 60 s window per row, with the deciding half host-tested and
  no I/O in it. Both decisions the task carried are made: `set_displaced` completes its
  displacing set, and **`ctx_reject`** is a new §10.5 row — it closed **B3b's last open
  criterion**, spec §10.3 step 3, which BF-18 could not force by racing the console.
  **B3b now needs only BF-22.** This document inherits Bridge Impl Plan v0.36.

- **v0.23** — **BF-18 is built and on air, and it built the MQTT receive path with it.**
  The gap this document raised at v0.19 — *"no task owns `/lib/lran-config/` or the
  bridge's MQTT receive path, which BF-18's command topics also need"* — is half closed:
  the receive path is folded into BF-18 rather than given a task number, because a
  command path with no command source cannot be tested. `/lib/lran-config/` is still
  unowned. **BF-21 gains a decision**: a simnode `ctx_reject` fault, so §10.3 step 3 can
  be forced rather than raced. `lran/bridge/diag/cmd/state` is new. This document
  inherits Bridge Impl Plan v0.35.

- **v0.21** — **Protocol specification v0.11 → v0.12; §6 gains BF-15a and BF-19a is
  unblocked.** **BF-15a** moves the bridge's `unregistered_src` into the codec as
  **`rx_unknown_src`** (spec §14 stage 9a, §14.1), which makes `kCounterRegistry` 22 rows
  and changes a published number — better before B4 builds discovery on the old name.
  **BF-19a** now has spec §14.2 to build against rather than a question. **BF-18** has
  §6.3's `detail` for a `DUPLICATE_CACHED` result and §7.4's rule for a repeated `CONFIG`.
  B3a is recorded as accepted 2026-09-16. Decision Register **D35–D42**.

| Version | What changed |
|---|---|
| **v0.22** | **BF-15a and BF-19a confirmed on air** — both rows record what the bench read; §6's B3b line follows |
| **v0.20** | **B3 split** into B3a (BF-15–17, 19, 20) and B3b (BF-18, 19a, 21, 22) |
| **v0.19** | **BF-19 built** — counters published; BF-19a split out; **BF-26 deferred** |
| **v0.18** | **BF-20 built** — the availability watchdog; bench publication waits for BF-26 |
| **v0.17** | **BF-17 built** — the poll scheduler, on a branch stacked on B0's |
| **v0.16** | **BF-6 built** — `ROLE_GATELINK`; B0's tasks are all built, bench work remains |
| **v0.15** | **BF-9 built** — the simnode's OLED page; B0 leaves only BF-6 |
| **v0.14** | **BF-8 built** — the fault catalogue and `fault` command; command-path entries wait for BF-6 |
| **v0.13** | **BF-7 built** — `lib/lran-sim/`'s patch primitive; BF-8 may start |
| **v0.12** | **B0 started** — BF-2, BF-3, BF-5 built and BF-4's core; two boards echo on air |
| **v0.11** | **BF-15 built** — the registry, host-tested; BF-16's radio came up on the board |
| **v0.10** | **BF-16 built** — the radio link, host-tested and not yet on air |
| **v0.9** | Spec v0.11 citation; library P8 built, so B0's library dependency is met |
| **v0.8** | **BF-14 built** — **B2's code is complete**; everything left is bench work |
| **v0.7** | **BF-13 built** — OTA and rollback; **V-B9 still owed on the bench** |
| **v0.6** | **BF-12 done** — WiFi, MQTT, LWT; **the bridge is in CI**, §1.2 rewritten |
| **v0.5** | **BF-11 done** — seven tasks, four queues; BF-11a and BF-11b split out |
| **v0.4** | **BF-10 done** — the skeleton builds; §1.2 records why CI does not build it |
| **v0.3** | **D1 closed** — BF-0 done, §1.1 becomes what the firmware inherits |
| **v0.2** | BF-0 points at the D1 decision brief; §1.1's D1 summary defers to it |
| **v0.1** | Initial release — task breakdown under B0–B7, with model suitability |

- **v0.20** — **B3 is split into B3a and B3b**, following Impl Plan v0.32, so the built half
  can be accepted and merged while BF-18 waits for spec v0.12. No task's scope or model
  column changes. This document inherits Bridge Impl Plan v0.32.

- **v0.19** — **BF-19 is built**: all 21 §14.1 counters, `rx_dropped` and the radio and
  queue diagnostics are published under `lran/bridge/diag/`, and each watched node's link
  under `lran/<node>/diag/state`. **New BF-19a** holds the §14 `ERROR` replies until spec
  v0.12 answers how the bridge addresses one. **BF-26 is deferred**: its row names three
  missing pieces, and **no task owns `/lib/lran-config/` or the bridge's MQTT receive path**,
  which BF-18's command topics also need. That gap is raised here rather than filled with a
  task number. This document inherits Bridge Impl Plan v0.31.

- **v0.18** — **BF-20 is built**: `offline` after `missed_poll_threshold` missed polls,
  `online` on any valid frame, retained per node and republished on every broker connect. A
  simnode's availability is judged and logged but not published, because spec §16.6 gates it
  on `simnode_diag_enable` and **BF-26** has not built that flag. V-B3 reads the serial log
  until then. No scope or model column changes. This document inherits Bridge Impl Plan v0.30.

- **v0.17** — **BF-17 is built**: per-node poll intervals, one outstanding poll fleet-wide,
  `missed_polls` counted and cleared. It is the first B3 task built after B0, on
  `b3-poll-scheduler`, stacked on B0's branch because B3's carries older documents. Bench rows
  are polled only once heard. No scope or model column changes. This document inherits Bridge
  Impl Plan v0.29.

- **v0.16** — **BF-6 is built**, and with it the rest of **BF-4**. `ROLE_GATELINK` answers
  `POLL`, `COMMAND` and `CONFIG` and sends events; the five command-path faults arm. **Every
  B0 task is built.** What remains of B0 is bench work, flashing the XIAO profile and running
  `ROLE_GATELINK` on air, then the operator's acceptance. No scope or model column changes.
  This document inherits Bridge Impl Plan v0.28.

- **v0.15** — **BF-9 is built.** The Heltec simnode's OLED shows the last frame, the identity
  table, and each armed fault as an inverted bar until it disarms. Host-tested, and the
  Heltec's panel answers at boot. The XIAO drives the Seeeduino expansion board's panel.
  **B0's remaining task is BF-6.** No scope or model column changes. This document inherits Bridge Impl Plan v0.27.

- **v0.14** — **BF-8 is built.** `firmware/simnode/fault.{h,cpp}` and the `fault` console
  command arm every §10.5 and §10.5.1 entry, each frame from BF-7's `FramePatch`. The
  command-path faults answer ERR naming **BF-6**, and `bad_phy_crc` is refused. The host suite
  asserts the §14 counter each fault names. **B0's fault criterion is met bar the OLED**, which
  is **BF-9**. No scope or model column changes. This document inherits Bridge Impl Plan v0.26.

- **v0.13** — **BF-7 is built**: `lran::sim::FramePatch` encodes through the codec, patches
  named bytes, and requires an explicit reseal. Its host suite rebuilds 18 of the 21 W4
  negative vectors byte for byte. **BF-8's dependency is met.** BF-7 is marked built rather
  than done because nothing sends its frames yet. BF-8 is the first caller, and B0's fault
  criterion is BF-8's. No scope or model column changes. This document inherits Bridge Impl
  Plan v0.25.

- **v0.12** — **Simnode B0 has started**, on `b0-simnode-bringup`, stacked on B3's branch
  because spec §12.3 media access moved into `lib/lran-link/` for both firmwares to share.
  BF-2, BF-3 and BF-5 are built and BF-4's core; two Heltecs complete every PING round
  trip on air (Impl Plan §10.9). **BF-6 to BF-9 are not started, and BF-8 still waits for
  BF-7.** No task's scope or model column changes. **A gap with no task:** spec §17.3
  requires RF loopback of every node build, and nothing here gives it to the bridge. This
  document inherits Bridge Impl Plan v0.24.

- **v0.11** — **BF-15 is built.** Marked built rather than done for the same reason as
  BF-16: B3's criteria need simnode **B0**. BF-15 leaves the §4.2 fields that later tasks
  write in the struct, each with a `TODO` naming its owner: `cmd_seq` (**BF-18**),
  `missed_polls` (**BF-17**, **BF-20**), `proto_ver` (**BF-22**) and `poll_interval_s`
  (**BF-23**). **Decoding per schema has no task of its own**: Impl Plan §5.3's `decode/`
  is named by no `BF-*` row, and `app_task`'s `TODO` gives it to **BF-24**, whose
  publication policy is its first consumer. Separately, **BF-16 came up on the bridge
  board** on 2026-09-14 (engineering log); V-B9's re-run is still owed. No task's scope or
  model column changes. This document inherits Bridge Impl Plan v0.23.

- **v0.10** — **BF-16 is built, and B3 has started.** The row is marked built rather than
  done: nothing has gone over the air, and B3's acceptance needs simnode **B0** in any case.
  BF-16 leaves named hand-offs in the code for **BF-15** (keys, and slots for registered
  nodes only), **BF-17** (a poll awaiting its reply as an OTA deferral), **BF-19** (ERROR
  replies), **BF-22** (N−1) and **BF-23** (timing from Home Assistant). No task's scope or
  model column changes. This document inherits Bridge Impl Plan v0.22.

- **v0.9** — Citation refresh. Protocol specification **v0.10 → v0.11**, which answers §9.4's
  check/record window: a retry reaching a node mid-execution is counted and not answered
  (D34 amended 2026-09-11). **Library P8 is built on that basis**, host and target, which
  meets the library dependency simnode **B0** was waiting on. No task's scope or model
  column changes. **For whoever writes the simnode's command path:** call
  `CommandGate::check()` before dispatch and send the `COMMAND_ACK` only after `record()`;
  on `InFlight`, send nothing. **BF-1 is marked done** and §1's P8 row reads met. This
  document inherits Bridge PRD v0.11 and Bridge Impl Plan v0.20. *Numbered v0.9 because
  B2's rebase onto the P8 merge placed it after BF-10 to BF-14's v0.4–v0.8; it was written
  as v0.4 on the P8 branch.*

- **v0.8** — **BF-14 is built, and with it B2's code is complete.** The OLED status page
  R-4.1c asks for: a host-tested model of what the page says, a thin renderer, and two
  things a permanently lit panel needs — `--` rather than `0` for what is not yet known,
  and burn-in mitigation. **Every B2 acceptance criterion except the partition table now
  needs the bench**, and the bench needs the sandbox broker. **BF-16 is not a B2 task** —
  it opens B3 — which earlier revisions of the handoff and of the B2 pull request said
  otherwise.

- **v0.7** — **BF-13 is built, and its acceptance test is not yet run.** A committed A/B
  partition table, ArduinoOTA over the LAN, and a verdict on each new image. **The finding
  that shaped it: Arduino-ESP32 2.0.x marks every image valid before `setup()` runs**, so
  the table alone would have kept an image that never finds the network — the one image
  this bridge could not be OTA'd back from. The override that fixes it must be
  `extern "C"`, and a C++ one links cleanly and does nothing, so CI now checks the symbol
  table. **The row is marked built, not done**, because **V-B9** — a deliberately bad image
  rolling back — needs the bridge board, and Impl Plan §6.5.2 is its procedure.

- **v0.6** — **BF-12 is done, and the bridge is in CI.** WiFi station with a capped
  exponential reconnect — 1 s to 30 s, deterministic because there is one bridge and a
  recognizable sequence in a log beats lockstep avoidance — the `MqttTransport` seam D5
  asked for with `PubSubTransport` behind it, and LWT on `lran/bridge/availability` so
  "the bridge is gone" is distinguishable from "the bridge has nothing to say" (spec
  §16.5). **Spec §16.3's hard rule is enforced rather than remembered:** a retained
  publication on an event topic is refused on the path, twice, because the failure is a
  2 AM SMS about last Tuesday. **§1.2 is rewritten** — the CI question it recorded is
  answered: the workflow copies `secrets.h.example`, nothing secret enters it, and all
  three bridge checks run on every pull request. What CI still cannot cover is named
  there too.

- **v0.5** — **BF-11 is done: the task structure exists, and it is the part that was
  expensive to get wrong.** Seven statically allocated FreeRTOS tasks in the priority bands
  §5.2 gives, `lora` strictly highest and pinned off the WiFi core, `log` strictly lowest,
  four queue boundaries with drop-newest-and-count, and thirteen host tests over the table
  and the accounting. **The never-block rule stopped being only a paragraph:**
  `tools/checks/lora_task_never_blocks.py` fails on a blocking primitive or a network call
  in the code `lora_task` owns. Impl Plan **§5.2.1** now carries the numbers and the
  argument for each; the engineering log carries what is not a number, including **why root
  rule 4 does not reach a queue overflow.** **Two follow-ons are split out rather than
  smuggled in:** `BF-11a` (the log queue's message type and drain) and `BF-11b` (the
  hardware watchdog fed from `sched_task`) — both are named by `TODO`s in the code, so the
  identifiers resolve to rows here. Sibling citations in the header are resynced in the same
  pass.

- **v0.4** — **BF-10 is done: `firmware/bridge/` exists and builds.** A `heltec`
  environment, a `native` environment with three tests that link `/lib/lran-protocol/`
  *through this project*, and a `main.cpp` that boots, prints the binding spec version and
  D1's working point, and says so loudly when it was built against the template's all-zero
  key. **New §1.2 records a decision this task forced:** the bridge is the first firmware
  here needing `secrets.h`, CI's own header says that needs a decision rather than a secret
  in a workflow, and the decision is that **the host tests belong in the `native` job while
  the target build stays out of CI for now.** Neither is wired up yet, so the target build
  is verified locally only — which is the kind of thing that goes unnoticed unless it is
  written where the next task will be read.

- **v0.3** — **D1 and D33 closed on 2026-09-10, so BF-0 is done and the list starts at
  BF-10.** §1.1 stops arguing that D1 is a decision and states what the firmware inherits
  instead: 917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted, and a `backoff_max_ms`
  default of **1500** rather than 500. The three consequences that reach code are called out
  where a task author will hit them — the raised window, the PHY parameters staying out of
  the runtime-configurable set, and `cad_backoffs` as the instrument for a channel whose
  survey missed 37.5 % of the band. **This document inherits Protocol Spec v0.10 and Bridge
  PRD v0.9.** Nothing on the wire moved and no task's model column changed.

- **v0.2** — **BF-0 now points at [`LRAN-D1-PHY-Decision-Brief`](../shared/LRAN-D1-PHY-Decision-Brief.md)**,
  which assembles D1's options and recommends a working point. §1.1 keeps its summary of why
  D1 is a decision rather than a measurement, because that is the fact which sets this list's
  order, and defers the parameter argument to the brief rather than restating it in a second
  place.

- **v0.1** — Initial release. Created because Implementation Plan §8 owns **milestones and
  acceptance criteria** but nothing owned the **task-level breakdown or the order**, and
  the range test's own pass 1 and pass 2 task documents had already established where that
  belongs. Follows those documents' shape deliberately. **Adds no requirement and no
  acceptance criterion**; every task points at the plan or the PRD for what "done" means.
  **Two things this list surfaced that the plan does not state in one place:** that **D1's
  inputs have all closed**, so it is now a decision to make rather than a measurement to
  run — and that **BF-7's patch-after-encode primitive must be scoped before the fault
  catalogue is handed to anyone**, which §10.5.2 says and no ordering document carried.
