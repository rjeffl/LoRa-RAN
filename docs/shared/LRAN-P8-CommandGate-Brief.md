# LRAN P8 — `CommandGate` decision brief

**Document:** `LRAN-P8-CommandGate-Brief`
**Version:** 0.2
**Status:** **Superseded 2026-09-11.** The operator accepted every recommendation below;
the Decision Register's §3.2.1 is the record, and this brief is kept as the reasoning.
§5's question 5 — what GateLink's ACK waits for — had no recommendation and stays open
**Parent document:** [`LRAN-Protocol-Library-Implementation-Plan`](./LRAN-Protocol-Library-Implementation-Plan.md)
**Binding protocol:** [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) **v0.10**
**Decision status:** [`LRAN-Decision-Register`](./LRAN-Decision-Register.md) — **D34** is
resolved and stays resolved; what this brief proposes is an amendment to its consequences
**Last updated:** 2026-09-10

> **This document decides nothing.** It exists because library milestone **P8** is the
> only library work left, it gates simnode **B0** and through it bridge **B3**, and a
> review while writing the B2 handoff found that **P8 cannot be built as the library plan
> specifies it** without risking the one failure root rule 2 exists to prevent. When the
> operator decides, the register records it and this brief is marked superseded.

---

## 1. The short answer

**As specified, `CommandGate` can execute a retried command twice.** The library plan
(§3.10) has `record()` advance the `seq` high-water mark *after* the command executes.
The specification (§9.4 step 6) advances it *before* dispatch. On a receiver that
executes a command on a different task from the one receiving frames — **which is how
GateLink's own implementation plan is designed** — a bridge retry that arrives while the
first copy is still executing finds no cached ACK and a high-water mark that has not
moved. It passes both checks and executes again. At the gate, that is a second relay
pulse.

**Recommended, in one line each:**

1. **Advance the high-water mark in `check()`**, when it returns `Execute` — the order the
   specification already gives. **Required, not optional.**
2. **Add an in-flight state** so a retry inside the execution window is counted and not
   answered, and the bridge's next retry receives the real cached result. Needs a
   specification note (v0.11); **no wire change**.
3. **Size the cache for the configuration range, not the default:** 32 entries of
   static capacity, depth runtime-settable 1–32. **128 B per peer**, not the 32 B the
   plan states.
4. **Amend D34's consequences in the register; do not reopen it.** Its placement and its
   split are right.
5. **Build P8 on its own branch from `main`**, independent of B2's pull request. It needs
   no broker and no credentials, and the on-target run can use any Heltec on USB.

---

## 2. What is settled and stays settled

**D34 (resolved 2026-08-31) is sound in its main points**, and nothing here reopens them:

- **The split.** §9.4 steps 4, 5 and the high-water update are *validation against
  receiver state* and belong in `/lib/lran-protocol/` as `CommandGate`, one instance per
  peer, immediately after `Reassembler`. **Dispatch stays in the application.**
- **Two calls, `check` and `record`.** The cached value is the *result of execution*, so
  no single call can produce it.
- **The counters.** `rx_rejected_seq` counts into `rx_dropped`; `rx_dup_command` does
  not. Two new `Status` values, `DuplicateCached` and `RejectedSeq`, keep
  `Counters::bump()` the single mapping point.
- **The bridge receives an empty set.** §9.2 makes every authenticated type
  bridge → node, so the bridge instantiates a gate per node and it never fires. The
  obligation binds the **first firmware that accepts a `COMMAND`**: simnode
  `ROLE_GATELINK` at **B0**, and GateLink **M3**.

What follows concerns three sentences of D34's *consequences* and one API detail.

---

## 3. The premise that failed

**Library plan §3.10 states a precondition:**

> *"`check → execute → record` is atomic with respect to frame arrival … Unreachable on a
> single-threaded receive loop, which is what both the bridge's `lora_task` and every
> node use."*

**Specification §9.4 says the same, and names its own falsifier:**

> *"It is unreachable on a single-threaded receive loop … Revisit if a receiver ever
> dispatches asynchronously."*

**GateLink's implementation plan dispatches asynchronously.** §5.2 and §5.3 of
[`LRAN-GateLink_Node-Implementation-Plan`](../gatelink/LRAN-GateLink_Node-Implementation-Plan.md):

| Work | Task | Priority |
|---|---|---|
| Receive frames, MAC | `lora_task` | High |
| `COMMAND` dispatch, dedup cache, ACK (`commands.cpp`) | `app_task` | Normal |
| The relay pulse itself | `io_task`, reached through a queue | Highest |

`lora_task` keeps receiving while `app_task` waits on `io_task` to finish a pulse. **The
premise was false the day GateLink's task table was written**; nothing surfaced it,
because nothing tracked it. This is the pattern root `CLAUDE.md` names: a falsification
condition stated in prose and tracked nowhere.

**The window is not theoretical.** The execution takes at least `relay_pulse_ms` (default
500, range 100–2000) plus `post_wake_settle_ms` (500). The bridge retries after
`command_ack_timeout_ms` (default 3000, Bridge Impl Plan §6.2). **If GateLink sends its
ACK only after confirming the gate responded** — `command_confirm_timeout_s` is 5 s — then
execution routinely outlasts the bridge's timeout and **the retry lands inside the window
on every slow command**. The GateLink plan does not say what the ACK waits for; that is
unverified, and it is the difference between "occasionally" and "usually".

### 3.1 Why the documents disagree about the consequence

Both documents say a retry inside the window receives `REJECTED_SEQ`. **That is true only
if the high-water mark has already advanced**, and the two documents put the advance in
different places:

| Document | Where the high-water mark advances | A retry inside the window |
|---|---|---|
| **Spec §9.4 step 6** — *"Update `rx_high_water = seq`, dispatch."* | **Before** dispatch | Fails step 5 → `REJECTED_SEQ`. Safe, but a wrong answer |
| **Library plan §3.10** — `record()` *"Advances the high-water mark"*, called *"AFTER executing"* | **After** execution | Passes step 4 (no cache entry) **and** step 5 (mark not moved) → **`Execute` again** |

**Root `CLAUDE.md`: if code and the specification disagree, the specification is right.**
The plan is the document that drifted, and code written from it would be wrong.

---

## 4. The decisions

### 4.1 Where the high-water mark advances — **required**

| Option | Behaviour | Assessment |
|---|---|---|
| **A. In `check()`, on `Execute`** | Matches spec §9.4 step 6. `record()` only stores the cache entry | **Recommended.** Spec-conformant; closes the double execution |
| B. In `record()`, as the plan says | A retry inside the window executes twice | **Reject.** Violates the specification and root rule 2 |

**Consequence of A worth knowing:** a command whose execution *fails* has still consumed
its `seq`. That is already true under the specification and is correct — `seq` is
attacker-visible and must not be reusable — and the failure is recorded as its result, so
a retry receives the cached failure.

### 4.2 What a retry inside the window receives

Given 4.1 A, a retry inside the window no longer executes. The question is what it is told.

| Option | Behaviour | Cost | Assessment |
|---|---|---|---|
| **A. `REJECTED_SEQ`** (what 4.1 A gives by default) | The node answers immediately | **The bridge publishes "rejected" to Home Assistant for a command the gate is carrying out.** Bridge §6.2 has no branch for `REJECTED_SEQ` on a retry; it falls through to publishing the result | Safe, misleading, and it will happen on every slow command |
| **C. In-flight marker** | `check()` returns `Execute` and stores the entry as *pending*. A retry that finds a pending entry gets a new verdict, **`InFlight`**: the node counts it in `rx_dup_command` and **sends nothing**. The bridge's next retry lands after `record()` and receives `DUPLICATE_CACHED` with the real result | One flag per cache entry. **No wire change** — no new result code, no `ver` bump, no vector regeneration. **Needs a specification note**, because §9.4 and §10.4 are silent and this repo does not close a spec gap locally | **Recommended** |
| D. Make execution synchronous | `lora_task` waits for the pulse | Contradicts GateLink §5.2: `io_task` owns pulse timing, and a receive loop blocked for a second misses frames | **Reject** |

**The residual risk under C is bounded and visible.** If one execution outlasts all of the
bridge's retries (`cmd_retries` 3 at a 3 s timeout plus backoff, roughly ten seconds), the
bridge publishes failure while the gate moves — the same misreport as A, but only for an
execution that slow, and `rx_dup_command` on the node shows why.

**Silence is not a silent discard here.** Root rule 4 asks for a named counter; the retry is
counted in `rx_dup_command`, which §14.1 already classes as normal traffic excluded from
`rx_dropped`.

### 4.3 Cache capacity against a runtime depth

The documents contradict each other on size, and static allocation (root rule 3) means
they cannot both hold:

- Library plan §3.10: **"Cost is 32 B per peer"** — 8 entries of 4 bytes.
- Library plan §4's parameter table: `dedup_cache_depth`, **range 1–32**, default 8,
  runtime-settable (root rule 8).

A depth of 32 needs 32 entries of storage, fixed at compile time.

| Option | Cost | Assessment |
|---|---|---|
| **A. Capacity 32, depth runtime 1–32** | **128 B per peer**; 640 B on a five-peer bridge | **Recommended.** Trivial on every target; keeps the parameter's range; fix the plan's cost sentence |
| B. Capacity 8, narrow the range to 1–8 | 32 B per peer | Keeps the stated cost, removes headroom nobody has asked for yet — defensible, but it edits a node parameter range for 96 bytes |
| C. Capacity as a template parameter | Per-target choice | Complexity with no consumer. Reject |

### 4.4 The record — what changes, and where

| Document | Change |
|---|---|
| **Decision Register** | **D34 amended, not reopened** — the split and the placement stand; the consequences change: the high-water mark advances in `check()`, the precondition is withdrawn, and the in-flight state is added. Same form as D17's 2026-09-10 amendment |
| **Protocol Spec v0.11** | §9.4's "one silence" paragraph and §10.4 state what a retry inside the window receives: nothing, counted in `rx_dup_command`. **No wire change**: `ver` stays 2, no new `AckResult`, **no W4 vector regenerates**. The 25 binding citations move to v0.11, reconciled first |
| **Library plan** | §3.10: `check()` advances the mark; `Verdict` gains `InFlight`; the precondition blockquote is replaced by the in-flight behaviour; the cost sentence becomes 128 B. **P8's acceptance row gains the window test** (§4.5) |
| **GateLink Impl Plan** | §5.2 gains one line: the ACK is sent by `app_task` after `record()`, and the dedup cache is consulted in the receive path *before* dispatch. Plus a decision on what the ACK waits for (§3) |

### 4.5 Tests P8 must add beyond the plan's list

The plan's P8 row already asks for the step-4-before-step-5 order, cached ACKs without
re-execution, `seq` below the mark refused, `reset_context()` clearing the cache,
exhaustive tests near the `seq` wrap, and the counter semantics. **Add:**

1. **The window.** `check(s)` → `Execute`; a second `check(s)` *before* `record(s)` →
   `InFlight`, **never `Execute`**; after `record(s)`, `check(s)` → `ReturnCached` with the
   recorded result. **This is the falsifier the precondition never had**, and it fails
   under the plan's current API.
2. **A failed execution is still cached.** `record(s, RejectedArg, …)` → a retry receives
   the cached failure, not a second attempt.
3. **Depth changes at runtime** evict oldest-first and never read beyond capacity.

### 4.6 Sequencing

| Option | Assessment |
|---|---|
| **A. P8 next, on branch `p8-command-gate` from `main`** | **Recommended.** It touches only `lib/` and the documents above, so it does not wait on B2's pull request or the broker. The P7-style on-target run (`pio test -d lib/lran-protocol -e esp32s3`) needs any Heltec on USB and **no credentials** |
| B. BF-16 first | Possible — `lora_link.cpp` needs neither — but B3 cannot finish without B0, and B0 cannot start without P8 |
| C. Both in parallel | Fine with two sessions; they share no files |

**Model:** Opus, per `LRAN-Bridge-Firmware-Tasks` BF-1 — *"the failure mode is a second
pulse at a driveway gate."*

---

## 5. Decisions wanted from the operator

1. **4.1** — high-water mark in `check()`. *Recommended A; B is a spec violation.*
2. **4.2** — in-flight marker (C), or accept `REJECTED_SEQ` misreports (A).
3. **4.3** — capacity 32 at 128 B per peer (A), or narrow the range (B).
4. **4.4** — amend D34 and revise the spec to v0.11 in the same branch as P8, or as a
   documents-only branch first.
5. **§3** — what GateLink's ACK waits for. Not needed for P8, but it sets how often the
   window is hit, and GateLink M3 needs the answer.

---

## 6. Changelog

| Version | What changed |
|---|---|
| **v0.2** | **Superseded** — the operator accepted the recommendations; the register records D34's amendment |
| **v0.1** | Initial release — P8's decisions assembled after a handoff review found the plan's API double-executes on an asynchronous receiver |

- **v0.2** — **Superseded, 2026-09-11.** The operator accepted recommendations 4.1 A,
  4.2 C, 4.3 A and 4.6 A, and §1's item 4: D34 amended, not reopened. For 4.4, which this
  brief left to the operator, the amendment, Protocol Spec v0.11 and P8's code landed in
  one branch. The Decision Register's §3.2.1 is the record. **One refinement in the
  build:** 4.2 C said a retry inside the window is counted in `rx_dup_command`, and it is
  — but under its own `Status`, `DuplicateInFlight`, not `DuplicateCached`, so a field log
  does not name a resend that never happened. The body above is unchanged.

- **v0.1** — Written at the end of the 2026-09-10 bridge session. **The finding:** library
  plan §3.10 advances the `seq` high-water mark in `record()`, after execution, where spec
  §9.4 advances it before dispatch; and the precondition that made the difference moot —
  a single-threaded receive loop — is contradicted by GateLink Impl Plan §5.2, which
  receives in `lora_task` and pulses in `io_task`. Spec §9.4 named that exact condition as
  the trigger to revisit. **No code exists for P8 yet**, so the correction costs document
  edits and nothing else.
