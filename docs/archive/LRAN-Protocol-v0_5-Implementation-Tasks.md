# LRAN Protocol v0.5 — implementation and validation tasks

**For:** Claude Code, working in `/lib/lran-protocol/` and `/tools/vectors/`
**Binding specification:** `LRAN-Protocol-Specification` v0.5 (`ver = 2`)
**Supersedes:** `LRAN-Protocol-v0_4-Implementation-Tasks` (all tasks complete)
**Starting state:** P1–P7 complete. 94 tests under `native`, 97 on target, 69 W4
vectors with zero host/target divergence
**Companion:** `/lib/lran-protocol/engineering-log.md`

---

## 0. Read this first

v0.5 is a **conformance revision**. Almost all of it comes from W4 — an independent
Python generator written from v0.4 with `/lib/` off limits — plus two findings from the
P7 target build. The wire version stays at `2`. No header field, authentication scope
or schema layout moves.

Most of what v0.5 says, the code already does. Four of the five P6 findings resolved in
the codec's favour or were already applied during P6 itself. **One did not**, and it is
the only behaviour change in this task list: §11.2 now defines what happens to a
fragment arriving after its set completed, and it does not define it the way the codec
currently behaves. See L1, and read the pushback note there before implementing it.

The rest is bookkeeping that matters more than bookkeeping usually does. The counter
registry exists because two counters shipped without the `rx_` prefix the prose used
and survived every review of P1–P5, found only when something finally compared the
names against the specification text. Writing the registry surfaced that stages 6
through 11 had never named a counter at all, while §14's opening line required one.

### Guardrails — do not do these

1. **Do not change any wire size, header layout, schema layout, or `ver`.** If a task
   appears to need one, stop and report.
2. **Do not sum `rx_frag_duplicate`, `rx_frag_late` or `rx_dup_command` into
   `rx_dropped`** (§14.1). Each is normal traffic. A health metric that climbs during
   correct operation is worse than no metric.
3. **Do not reintroduce a dependency on `mbedtls_hkdf`** or on `MBEDTLS_HKDF_C`. §9.1
   now forbids it. P7 already paid for this discovery once.
4. **Do not let the `esp32s3` environment grow into firmware.** It is deliberately a
   test environment with no WiFi, no MQTT and no `secrets.h`.
5. **Do not implement §9.4 steps 4–6 in `/lib/lran-protocol/`.** They are out of scope
   by Implementation Plan §1 and that has not changed. W12 is where they go; see
   section 5 below, which is a decision to bring back, not code to write.
6. Standing: no Arduino, ESP-IDF, mbedTLS, `millis()`, `Serial`, `malloc` or `new`
   reachable from `include/` or `src/`; `-Wall -Wextra -Werror` stays; nothing
   discards silently.

### Branch plan

Order matters — vectors assert counter names and reassembly behaviour, so both have to
settle before regeneration.

| Branch | Tasks | Gate |
|---|---|---|
| `spec/v0.5-counters` | C1–C4 | 5 suites green under `native` |
| `spec/v0.5-late-fragment` | L1–L3 | green, plus the new late-fragment tests |
| `p6b/vector-regen` | V1–V6 | `check.py` passes, `test_vectors` passes, count recorded |
| `p7b/target-reverify` | T1–T3 | green on hardware, zero host/target divergence |

Sections 5 (W12) and 6 (W9) are not branches. One needs a decision from outside the
library; the other needs two boards on a bench.

---

## Branch 1 — `spec/v0.5-counters`

### C1 — reconcile `Counters` against §14.1 exactly

§14.1 is now the normative registry: 21 counters, each with the stage that raises it
and whether it sums into `rx_dropped`. Before v0.5 the names were scattered across
§14's stage table, §11 and §7.5, and several stages named none at all.

**Do:** make `Counters` match the registry name for name. Expect to add the ones stages
6–11 never had: `rx_unknown_type`, `rx_unknown_schema`, `rx_rejected_ctx`,
`rx_rejected_mac`, `rx_rejected_seq`, `rx_dup_command`. Some may exist under other
names — rename rather than duplicate, and list every rename in the PR description.

**On the two the library cannot raise:** `rx_rejected_seq` and `rx_dup_command` belong
to stage 11, which is §9.4 steps 4–6 and out of this library's scope (W12). **Carry
them in `Counters` anyway**, with a comment naming who increments them. `Counters` is
the published aggregate that reaches schema `0xF0`, and a `rx_dropped` missing the
replay rejections would understate drops on exactly the frames that matter most.

**Acceptance:** a test that walks the whole registry — every name present, no extras.
This is the test that would have caught the missing `rx_` prefixes; write it so that
adding a counter to the struct without adding it to the test fails.

### C2 — `rx_dropped` sums the marked counters and only those

§7.5's field description and §14.1's column are now the definition. `total_dropped()`
must sum the 18 marked yes and exclude the 3 marked no.

**Do:** confirm `rx_frag_duplicate` is still excluded (it was, per F5) and exclude
`rx_frag_late` (new, L1) and `rx_dup_command` on the same grounds.

**Acceptance:** a test asserting `total_dropped()` against a `Counters` with every
field set to a distinct known value, so the sum is checked by construction rather than
by re-listing the fields in the test.

### C3 — `rx_not_fragmentable`, confirmed against the spec

Already added during P6 finding 4, ahead of the spec. §14 stage 8a and §14.1 now name
it. Confirm the code matches and that the wire answer is still `ERROR(BAD_LENGTH)` per
§11.4 — the status/error asymmetry is intentional and matches `BadFrag`.

**Acceptance:** existing coverage probably suffices. Verify rather than rewrite.

### C4 — stage 5b, now that the spec agrees

§14 stage 5b checks the declared total only. Index ≥ total is a stage 10
`FRAGMENT_OVERFLOW` counted `rx_fragment_overflow`. The code already does this — E3 said
so — but v0.4's stage 5b row said otherwise and an independent implementer followed it.

**Do:** no behaviour change expected. Add a test asserting both paths land on their own
counter and their own wire error, and put a comment at the check citing §14 stage 5b
and §11.2 together, so the next person reading one section doesn't reopen it.

---

## Branch 2 — `spec/v0.5-late-fragment`

### L1 — a fragment matching the last completed set is discarded

**Spec:** §11.2. **This is the one real behaviour change in v0.5.**

Today `Reassembler::accept` resets on a completed set before beginning a new one, so a
fragment arriving after its set completed starts a fresh set. v0.5 requires instead
that the receiver retain the key `(src, ctx_id, seq, schema)` of the last set completed
in each slot, and discard any fragment matching it, counted `rx_frag_late`. No `ERROR`
is returned — a late RF echo and a sender retry both produce this legitimately, exactly
as a mid-set duplicate does.

**Check order in `accept`:**

1. Matches the live set's key → existing path (join, or overwrite as a duplicate).
2. Matches the retained completed key → discard, `rx_frag_late`, return.
3. Otherwise → new set, abandoning any live one per §11.3.

The retained key is set when a set completes and is displaced by the next set that
completes in that slot. **A timed-out or abandoned set does not set it** — only
completion does.

> **Pushback note.** The existing behaviour is defensible and it is what the codec
> already does; this rule was chosen against it, so overrule it if you disagree. The
> reasoning is in §11.2: one echoed fragment opens a set that can never complete, holds
> the slot for `frag_reassembly_timeout_ms`, blocks a legitimate set behind it, and
> then reports `rx_reassembly_timeout` — a counter naming a fault that did not occur.
> Retaining eight bytes of key plus a validity flag removes all of that. If you take a
> different view, say so in the PR and the spec changes rather than the code.

**Acceptance tests:**

- `test_late_fragment_after_completion_counted` — complete a 15-fragment set, replay
  index 4, assert `rx_frag_late` moved, no new set started, and the slot accepts a
  genuinely new set immediately afterwards.
- `test_late_fragment_does_not_block_slot` — the case the rule exists for. Complete a
  set, deliver a late fragment, then deliver a full new set and assert it completes
  without waiting out any timeout.
- `test_timed_out_set_does_not_retain_key` — let a set expire, then deliver a fragment
  with that key and assert it starts a new set rather than counting `rx_frag_late`.
- `test_retained_key_displaced_by_next_completion` — complete set A, complete set B,
  then replay a fragment of A and assert it starts a new set.

### L2 — single-frame frames are unaffected

Confirm the retained-key check cannot reach a frame with `frag` total 1. Such a frame
never enters the reassembler, so a legitimately retransmitted single-frame `CONFIG_ACK`
must not be eaten as a late fragment. If the code path is shared, gate it.

**Acceptance:** `test_single_frame_retransmit_not_late` — deliver the same unfragmented
frame twice, assert `rx_frag_late` did not move.

### L3 — `seq` reuse, documented at the check

§11.2 notes the rule leans on `seq` not being reused: a genuinely new set sharing a
completed set's full key would need a repeated `seq`, which §10.2 forbids in the
command space and which in the status space takes a full 2^16 wrap. The exposure is one
set, since the retained key is displaced by the next completion.

**Do:** put that in a comment at the retained-key check, citing §10.2. This is the kind
of assumption that is obvious while writing it and invisible six months later.

---

## Branch 3 — `p6b/vector-regen`

§13.2 requires a regenerated vector set on every protocol change, and v0.5 is one. Both
the late-fragment rule and the stage 5b correction touch existing vectors.

### V1 — the independence question, answered honestly

The 69 vectors were worth what the method was worth: written from v0.4 alone, with
`/lib/` off limits. That cannot be reproduced exactly for v0.5, because the party who
wrote them has now seen how four disagreements were adjudicated.

**Do not pretend otherwise, and do not use it as a reason to lower the bar.** Keep the
generator authored from the specification text. Where a v0.5 vector encodes a
resolution that originated in the codec rather than in the spec — the stage 5b
adjudication, the counter renames, and now the late-fragment rule — mark it in the
vector file as adjudicated rather than independently derived. The log already does this
for the two items handed over as external authority in P6; extend the same bookkeeping.
A reader assessing what the agreement is worth needs to know which parts were
independent.

### V2 — vectors the v0.5 rules require

- **Late fragment.** Deliver a full set, then a fragment of it, asserting
  `rx_frag_late` and no new set. This is the vector that was relocated during P6
  finding 2 because the case was undefined; it comes back now that §11.2 defines it.
- **Reserved `hdr_flags` bits, decode-only.** §4 rule 3 now states the encoder and
  decoder obligations are deliberately asymmetric and that conformance material needs a
  decode-only form. The `decode_only` flag added to `/tools/vectors/README.md` during
  P6 is now spec-backed. Restore the vector that motivated it.
- **Stages 6 and 7 counters.** Negative vectors for unknown `type` and for an invalid
  `(type, schema)` pair, each asserting exactly one counter — the two stages that had
  no counter to assert before C1.
- **`rx_fragment_overflow` vs `rx_bad_frag`.** One vector each: `frag = 0x00` and index
  ≥ total. The pair that v0.4's stage 5b row made ambiguous.

### V3 — counter-name sweep

Any vector naming a counter needs checking against §14.1. The renames from P6 finding 5
are in; C1's additions are not.

### V4 — the negative-vector hazard, still

F2/F3 cost twenty minutes to a forged frame that landed byte-for-byte on a valid one,
because the corruption varied a field that happened to reproduce the real value. The
generator does this a lot. **Assert at generation time that every negative vector
differs from every positive vector**, if that check isn't already there. A negative
vector that accidentally encodes a valid frame is a false pass, and it passes quietly.

### V5 — regenerate, embed, converge

Run the generator, `check.py`, `embed.py` for `vectors_data.h`, and `test_vectors`.
The JSON stays the source of truth; the header is a build artifact.

### V6 — log the outcome

New engineering-log entry: the vector count, every disagreement and its resolution, and
the independence bookkeeping from V1. If v0.5 produces no disagreements, say so and say
why that is expected this time — the spec was written from the codec's findings, so
agreement is much weaker evidence here than it was in P6.

**Also fix the log header.** Line 4 still reads `Binding specification:
LRAN-Protocol-Specification v0.3`. It is v0.5.

---

## Branch 4 — `p7b/target-reverify`

### T1 — rerun on hardware

All five suites plus the regenerated vectors on the Heltec V3. Zero host/target
divergence is the bar; anything else means something is reading struct layout, which
§4 exists to prevent.

### T2 — pin the HKDF constraint against regression

§9.1 now forbids depending on `mbedtls_hkdf` or `MBEDTLS_HKDF_C`. P7 found this at
link time on a platform that compiled the header fine.

**Do:** add a repo check that fails if `<mbedtls/hkdf.h>` or `mbedtls_hkdf` appears
anywhere under `lib/` or `platform/`, with the §9.1 citation in the failure message. A
future framework bump that quietly enables the module must not make this dependency
reappear by looking harmless.

### T3 — `L = 32`, asserted

§9.1 now states the HKDF output length explicitly; v0.4 never did, and the generator
inferred it correctly from the width HMAC-SHA256 consumes. Add a static assertion that
`kNodeKeyLen == 32`, and confirm the expand loop still cannot silently truncate for a
longer `L` — the log records it was written out for exactly this reason.

**Acceptance:** footprint deltas for the v0.5 counter additions into the log, against
P7.4's table.

---

## 5. W12 — a home for §9.4 steps 4–6 (decision, not code)

This is now the largest open item and it is **not** a task to close inside this
library. F1 established that steps 4–6 — dedup cache, `seq` high-water, dispatch — are
implemented nowhere, and that Implementation Plan §1 correctly keeps them out of a
framing library. §9.4 in v0.5 says where they belong; it does not say what module holds
them.

They are mandatory (§10.4 — a relay pulse is not idempotent) and **identical on every
side**: the bridge needs them, and so does GateLink, WellLink, and any simnode that
accepts a command. Four independent implementations of a replay check is three too
many, and the failure mode of getting one wrong is a gate that opens twice.

**Do:** write a short proposal — not an implementation — covering what the component
holds (dedup cache keyed `(ctx_id, seq)`, high-water per peer, `ctx_id` tracking and
resync), where it lives (a sibling library alongside `lran-protocol` and `lran-config`
is the obvious candidate), how the bridge's per-peer instantiation differs from a
node's single instance, and which counters from §14.1 it owns — `rx_rejected_seq` and
`rx_dup_command`, per C1.

Bring it back for a decision before the second firmware is written. After two firmwares
exist, this becomes a refactor across both.

---

## 6. W9 — RF bench runs (hardware gated)

Needs two boards and an antenna. Nothing in P7 exercised the SX1262 at all.

- **222-byte `PING`** (§6.6.1) — the maximum frame over real RF. §15.1 notes it
  occupies the channel for over a second at SF9; check a CAD/backoff window against
  that figure while you have the boards out.
- **Fragmented `PING`** (§6.6.2) — driven by the `frag_chunk` override, not by an
  oversized `n`. A 202-byte echo at `frag_chunk = 14` gives the full 15-fragment set.
- **Late fragment over the air**, if the bench can produce one. L1's rule was chosen
  against a real RF echo, and a bench confirmation that echoes occur — or that they
  don't, on this link — is worth having before the rule is relied on at the gate.

Both of the first two belong in the bring-up sequence **before** GateLink is installed.
Neither is fixable remotely.

---

## Standing notes

**Raise rather than guess.** v0.5 exists because an implementation working from the
specification alone disagreed with the codec four times and reported each one instead
of conforming quietly. Two of those — the stage 5b contradiction and the undefined
post-completion fragment — were cases where both readings were defensible, which is the
definition of a specification bug and not something a careful implementer can resolve
by being more careful.

**The counters are the field interface.** Once the bridge publishes them to MQTT and
Home Assistant charts them, every name in §14.1 is frozen by things outside this repo.
C1 is the last cheap moment.
