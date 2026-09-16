# LRAN spec v0.12 — decision brief

**Document:** `LRAN-Spec-v0.12-Brief`
**Version:** 0.1
**Status:** Open. Awaiting the operator's decisions on §§2–10
**Parent document:** [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md)
**Binding protocol:** [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) **v0.11** (`ver = 2`)
**Decision status:** [`LRAN-Decision-Register`](./LRAN-Decision-Register.md) — nothing here has a `D` number yet
**Last updated:** 2026-09-16

> **This document decides nothing.** Nine questions were raised during bridge B3a and
> simnode B0 and deliberately left unpatched, each recorded in the engineering log entry
> that raised it. Bridge milestone **B3b** is gated on three of them. This brief collects
> all nine, gives the options for each, and recommends one. When the operator decides, the
> Decision Register records it, the specification carries the change, and this brief is
> marked superseded.

---

## 1. The short answer

**Nine questions, three of which block B3b, and none of the recommendations below changes
a frame layout.** `ver` stays `2`, no W4 vector regenerates, and v0.12 is a revision of
prose and semantics rather than of the wire. That matters on a fleet with no OTA: a wire
change means a USB reflash at the gate, and none of these answers needs one.

| # | Question | Raised by | Blocks | Recommendation | §|
|---|---|---|---|---|---|
| 1 | §14 has no stage for a frame from an unregistered source | BF-15 | naming `unregistered_src` | Add stage 9a, counter `rx_unknown_src`, no `ERROR` | [§2](#2-question-1--a-frame-from-a-source-the-receiver-does-not-know) |
| 2 | How a `DUPLICATE_CACHED` `COMMAND_ACK` carries the cached result | BF-6 | **BF-18** | `detail` carries the cached `result` | [§3](#3-question-2--how-duplicate_cached-carries-the-cached-result) |
| 3 | What a node answers to a repeated `CONFIG` | BF-6 | BF-18, GateLink | The cached `CONFIG_ACK`, through the same gate | [§4](#4-question-3--a-repeated-config) |
| 4 | §7.4 relies on fragmenting config sets that §3.1's 196-byte cap rules out | BF-6 | GateLink config | Declare `CONFIG` / `CONFIG_ACK` single-frame in v1 | [§5](#5-question-4--config-fragmentation-cannot-carry-more-than-one-frame) |
| 5 | §10.2 places `POLL`'s `seq` in neither sequence space | BF-17 | nothing yet | State it is bridge-local and advisory | [§6](#6-question-5--polls-seq-belongs-to-no-sequence-space) |
| 6 | §14.1's "per node by the bridge" for a discard made before the MAC check | BF-19 | per-node counters | Pre-authentication counters are the receiver's own | [§7](#7-question-6--a-counter-attributed-to-an-unauthenticated-src) |
| 7 | Whether the bridge sends §14's `ERROR` replies, and with which `src` and `ctx_id` | BF-19 | **BF-19a** | Registered sources only, rate-limited, `ctx_id` = 0 | [§8](#8-question-7--error-replies-sent-before-the-mac-is-checked) |
| 8 | §16.2 names four topics and defines no payload | BF-13, BF-19, BF-26 | **BF-26**, BF-23 | Pin `version` and `diag/state`; defer `config/*` | [§9](#9-question-8--topics-named-without-payloads) |
| 9 | §12.1's node-address filtering appears unavailable in LoRa mode | BF-16 | duty-cycled nodes (§17.1) | Not a decision — verify, then amend, and track the check | [§10](#10-question-9--node-address-filtering-in-lora-mode) |

**BF-21 and BF-22 wait on none of this** and can start against `main` while v0.12 is
settled.

---

## 2. Question 1 — a frame from a source the receiver does not know

**The gap.** BF-15 gave the bridge a registry and had the ladder refuse any `src` it does
not hold a key for. Spec §14 has no stage for that refusal, so root rule 4 — every discard
increments a named counter and maps to one `Status` value and one §14 stage — can be
satisfied only in part. The bridge counts it as `unregistered_src`, outside §14.1 and
outside `rx_dropped`.

| Option | For | Against |
|---|---|---|
| **A. New stage 9a, counter `rx_unknown_src`, no `ERROR`** | Root rule 4 holds whole. The diagnosis stays distinct from "not addressed to me". A node holds one peer, so the counter costs it nothing | Adds a normative counter; `kCounterRegistry` grows to 22 and every node's `rx_dropped` sum changes value |
| **B. Leave it a bridge-local diagnostic, named as it is** | No specification churn at all | Root rule 4 stays half-satisfied, and a bridge-only counter cannot be charted beside the §14.1 ones |
| **C. Fold it into `rx_not_addressed` (stage 5)** | No new counter | Conflates *not for me* with *I do not know you*. On a marginal link those are different diagnoses, and this is the counter a misprovisioned node would light up |

**Recommendation: A**, placed after stage 9 — a stranger is refused after authentication
would have run, and is never answered. Rename the bridge's `unregistered_src` to
`rx_unknown_src` in the same change, and put the counter **in** `rx_dropped`: a frame from
an unknown source is a discard by any reading.

---

## 3. Question 2 — how `DUPLICATE_CACHED` carries the cached result

**The gap.** §9.4 step 4 says a dedup hit is answered `COMMAND_ACK(DUPLICATE_CACHED)`
*"with the cached result"*, and §6.3's six-byte layout has no field named for one. BF-6
had to choose, and the simnode puts the cached `result` in `detail`.

| Option | For | Against |
|---|---|---|
| **A. `detail` carries the cached `result` enum** | No wire change; §6.3 already calls `detail` result-specific. Already implemented and exercised | A cached result that itself wanted a `detail` byte loses it. The loss is silent unless §6.3 says so |
| **B. Resend the original ACK byte for byte** | Simplest receiver; the node's `rx_dup_command` already records the hit | The bridge cannot tell a first answer from a replay, which is the one thing the ACK is being sent to disambiguate |
| **C. Spend a bit of §6.3's `reserved` field as a replay flag** | Keeps `result` and `detail` both free | A reserved field gaining meaning is a `ver` bump under §13.2, and §13.1's forward-compatibility rule exists precisely so this space stays untouched |

**Recommendation: A.** Codify what BF-6 shipped, and state the limitation in §6.3 rather
than leaving a reader to find it: when `result = DUPLICATE_CACHED`, `detail` is the cached
result and any detail the original answer carried is not reproduced.

---

## 4. Question 3 — a repeated `CONFIG`

**The gap.** §7.4 defines `CONFIG` and its ACK; nothing says what a node does when the same
`CONFIG` arrives twice. BF-6 needed an answer to build the simnode's config store.

| Option | For | Against |
|---|---|---|
| **A. Answer from the dedup cache, like any authenticated type** | `CONFIG` is authenticated, so §9.4 steps 4–6 already apply. One rule for every authenticated type. Because §7.4's ACK carries *effective* values, the cached ACK is also a correct readback | The cached ACK ages: a value changed by another path between the two arrivals is not reflected |
| **B. Re-apply the set and answer freshly** | Always current | Safe only while every `op` in §8.10 is idempotent. Nothing commits to keeping that true, and a non-idempotent `op` added later would break silently |
| **C. Re-read and answer without re-applying** | Always current, never re-applies | A third behaviour for one receiver to implement, and it contradicts §10.4's "do not re-execute" framing by treating `CONFIG` as special |

**Recommendation: A.** Consistency with §10.4 beats a special case, and the readback
property comes free. Where a caller needs current values it should poll with §6.4's
`poll_flags` bit 1, which exists for exactly that.

---

## 5. Question 4 — `CONFIG` fragmentation cannot carry more than one frame

**The contradiction.** §11.4 lists `CONFIG` as fragmentable and notes it *"exceeds one
frame at 24 `uint32` entries"*. §3.1 caps a reassembled schema-bearing set at
`LRAN_MAX_SCHEMA_PAYLOAD`, 196 bytes — **the same as one authenticated frame's payload**.
A fragmented config set therefore cannot carry a single byte more than an unfragmented
one, and a set that needs to is unreachable by any legal encoding.

| Option | For | Against |
|---|---|---|
| **A. Declare `CONFIG` / `CONFIG_ACK` single-frame in v1** | Removes the contradiction outright. Costs no RAM. `PING` remains §6.6.2's fragmentation vehicle, so §11 keeps a test path | A config set above the cap must be split into several `CONFIG` messages by the bridge, and nothing yet defines how a caller knows they belong together |
| **B. Raise the reassembly cap above 196 for schema-bearing sets** | Makes §11.4's claim true as written | Eight reassembly slots on every node each grow by the increase. §3.1 derives the cap rather than choosing it, so this is a wider change than it looks |
| **C. Define batching: `op` gains BEGIN / CONTINUE / COMMIT** | Solves the real problem — large sets applied atomically — without touching the cap | New enumeration values and new receiver state, for a case no node has yet met |

**Recommendation: A for v1, with C recorded as the v2 path.** One measurement should come
first and it is not a decision: **count GateLink's actual parameters against the 24-entry
ceiling.** If they fit, A costs nothing at all.

---

## 6. Question 5 — `POLL`'s `seq` belongs to no sequence space

**The gap.** §10.2 defines two spaces: the bridge's command `seq` and the node's status
`seq`. BF-17 gives each `POLL` a `seq` from the scheduler. `POLL` is unauthenticated and
bridge-originated, so it is in neither.

| Option | For | Against |
|---|---|---|
| **A. State that a bridge-originated unauthenticated `seq` is local and advisory, and MUST NOT advance any high-water mark** | Documents what BF-17 already does. No implementation changes | Leaves §10.2's table describing two spaces while a third kind of `seq` exists on the wire |
| **B. Give the bridge a third, named space** | §10.2's table then covers everything on the wire | Implies replay semantics for a frame type that has none, and invites a receiver to enforce them |

**Recommendation: A.** Add a sentence to §10.2 and a note at §6.4. Nothing depends on the
answer, which is why this one should not hold the revision up.

---

## 7. Question 6 — a counter attributed to an unauthenticated `src`

**The gap.** §14.1 opens *"Every counter below is published: per node by the bridge"*.
Most discards happen at stages 1–8a, **before** the MAC is checked, where `src` is a
claim rather than an identity. Publishing those per node lets anyone with a transmitter
move another node's counters.

| Option | For | Against |
|---|---|---|
| **A. Pre-authentication counters are the receiver's own aggregate; only stage 9 onward may be attributed per node** | A spoofer cannot poison a node's Home Assistant history. This is what BF-19 built and what the bridge publishes today | §14.1's sentence has to change, and a per-node view of early discards is lost — the bridge's own counters are where a link problem shows |
| **B. Keep per-node attribution, marked unauthenticated** | Preserves per-node granularity everywhere | The mark is invisible on a chart, and the failure mode is a wrong diagnosis about a node that did nothing |

**Recommendation: A.** `diag_json.h` carries the reasoning already; §14.1 should carry it
normatively.

---

## 8. Question 7 — `ERROR` replies sent before the MAC is checked

**The gap.** §14 names an `ERROR` for stages 5a through 8a, two of them optional. All of
them fire **before** authentication, so the bridge would be answering a frame it cannot
attribute, and §10.1 gives the bridge no `ctx_id` of its own to put in the header. BF-19
left them unbuilt and BF-19a owns the answer.

| Option | For | Against |
|---|---|---|
| **A. Every §14 `ERROR` is optional; the bridge counts and stays silent** | Zero unauthenticated transmit surface. Cheapest to build — it is today's behaviour | A node whose frames are being refused has no way to learn why, on a link whose whole diagnostic story is counters it cannot read remotely |
| **B. Mandate `ERROR` to any `src`, `ctx_id` = 0** | Fully defined and simple to implement | **A reflection vector.** A spoofed frame makes the bridge transmit, on a shared channel, at an attacker's chosen rate |
| **C. Mandate `ERROR` to registered sources only, rate-limited per source, `ctx_id` = 0** | Keeps the diagnosis for real nodes. Bounds reflection to the fleet's own addresses and to a known rate | More state than A: a per-source timestamp. Still answers a forged frame that borrows a fleet address |

**Recommendation: C**, with a stated per-source minimum interval and an explicit rule that
`ctx_id = 0` means unknown (§5.5) and a node MUST NOT adopt it. Question 1's stage 9a then
follows the same logic: an unknown source is counted and never answered.

---

## 9. Question 8 — topics named without payloads

**The gap.** §16.2 names `lran/bridge/version`, `lran/<node>/diag/state`,
`lran/<node>/config/set` and `lran/<node>/config/ack`, and defines the payload of none of
them. BF-13 and BF-19 each had to invent one; BF-26 was deferred rather than invent two
more.

| Option | For | Against |
|---|---|---|
| **A. Define all four normatively now** | One revision, no loose ends | `config/set` and `config/ack` have no implementation, no `/lib/lran-config/` and no MQTT receive path. A payload specified before its first caller is a guess with a version number |
| **B. Pin `version` and `diag/state`; leave `config/*` to BF-23 and BF-26** | Both are published today, and Home Assistant's entity registry makes a later rename breaking. Codifies working code rather than speculation | §16.2 stays partly undefined, and the next reader has to check which half is real |
| **C. Leave all payloads to the bridge and document them in the Bridge PRD** | Keeps publication policy where §16.4 already puts it — with the bridge | A second node publishing to the same topics has nothing to conform to, and §16.2 loses its point |

**Recommendation: B.** Pin what ships, defer what has no caller. Deferring BF-26 was the
same judgement and it was right.

---

## 10. Question 9 — node-address filtering in LoRa mode

**This is not a decision.** §12.1 lists hardware node-address filtering as enabled in the
SX126x packet handler, and §17.1 relies on it for duty-cycled nodes. BF-16 found it
appears unavailable in LoRa mode — address filtering belongs to the FSK/GFSK packet
handler, and LoRa discriminates by sync word — and recorded the finding as **unverified
against the datasheet**.

**Do this rather than decide anything:** verify against the SX1262 datasheet. If BF-16 is
right, amend §12.1 to mark the feature FSK-only and strike it from §17.1's plan, which
then needs a different answer for how a duty-cycled node avoids waking on every frame.

**This premise currently names no check.** Root `CLAUDE.md` requires that a load-bearing
premise say what would falsify it and point at where that check is tracked. §17.1 rests on
this one and tracks it nowhere, so the amendment should create an `M` item, not just fix
the sentence.

---

## 11. Suggested order

1. **Free now** — questions 1, 5 and 6. Prose, and no implementation waits on the wording.
2. **Unblocks BF-18** — questions 2 and 3.
3. **Unblocks BF-19a** — question 7.
4. **Partial** — question 8's `version` and `diag/state`.
5. **Deferred behind a named check** — question 4 (GateLink's parameter count) and
   question 9 (the datasheet).

**What the operator should confirm before any of this is written into the specification:**
that v0.12 stays a semantics revision. If any answer here turns into a layout change, the
W4 vectors regenerate (§13.2) and every flashed node is a walk away.
