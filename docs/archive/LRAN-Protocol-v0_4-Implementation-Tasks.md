# LRAN Protocol v0.4 — implementation and validation tasks

**For:** Claude Code, working in `/lib/lran-protocol/` and `/tools/`
**Binding specification:** `LRAN-Protocol-Specification` v0.4 (`ver = 2`)
**Starting state:** milestones P1–P5 complete, 71 Unity tests passing under `native`;
P6 and P7 not started
**Companion:** `/lib/lran-protocol/engineering-log.md` (the log that produced v0.4)

---

## 0. Read this first

Spec v0.4 is a correction of v0.3 driven by the P1–P5 engineering log. **The wire
version stays at `2`.** Nothing in v0.4 changes a header field, the authentication
scope, or a schema layout, so no frame that is valid under v0.3 becomes invalid
because of a size change. Every task below is a behaviour or validation change inside
the existing frame format.

Three of the tasks resolve findings the log raised. The rest resolve conflicts and
silences found when v0.3 was audited against the log. Where the log already made the
right call in code, the task is to make the spec's version normative and add the test
that pins it — not to rewrite working code.

### Symbol names

Names used below (`Reassembler`, `DecodeCtx::expect_ctx_id`, `IMac`, `Counters`,
`NotImplemented`, `BadLength`, `rx_bad_length`, `reassembly_timeout`) are taken from
the engineering log. **Confirm each against the tree before editing.** If a name has
drifted, use the real one and note the discrepancy in your PR description.

### Guardrails — do not do these

1. **Do not change any wire size, header layout, schema layout, or `ver`.** If a task
   appears to require one, stop and report; it means the task or the spec is wrong.
2. **Do not optimize `Reassembler` to uniform-chunk offsets.** §11.1's uniform-chunk
   rule is a *sender* obligation that exists to make W4 vectors deterministic. §11.2
   forbids a receiver from depending on it. The second staging buffer stays. The RAM
   it costs (~520 B per set) was priced and accepted; silent misassembly against a
   non-conforming peer was not.
3. **Do not widen `PING`'s `n`, and do not raise the `PING` reassembly cap.** §6.6.2
   was corrected by changing how the bench *drives* fragmentation, not by moving more
   bytes. Both remain as they are.
4. **Do not add Arduino, ESP-IDF, mbedTLS, `millis()`, `Serial`, `malloc` or `new`
   reachable from `include/` or `src/`.** The P1–P5 property holds; keep the grep
   clean.
5. **Do not relax `-Wall -Wextra -Werror`.** The log records the tree is also clean
   under `-Wconversion -Wshadow -Wpedantic`; try not to lose that.
6. **Nothing discards silently** (repo rule 4). Every new reject path gets a counter,
   and the counter is distinguishable from the ones already there.

### Branch and PR plan

Four branches, in this order. Each merges before the next starts, because later
branches test against earlier behaviour.

| Branch | Tasks | Gate |
|---|---|---|
| `spec/v0.4-errata` | E1–E5 | `pio test -d lib/lran-protocol -e native` green |
| `spec/v0.4-auth-frag` | F1–F8 | green, plus the new frag/auth suites |
| `p6/test-vectors` | P6.1–P6.6 | C++ and Python agree on every vector |
| `p7/target-build` | P7.1–P7.4 | builds and runs the vector suite on ESP32-S3 |

Write the PR description yourself, and in it list any place where the spec and the
code disagreed and how you resolved it.

---

## Branch 1 — `spec/v0.4-errata`

Small, independent, low risk. Land these first so the vectors in P6 are never
generated against a wrong length.

### E1 — `HEX_RSP` payload length

**Spec:** §6, §7.6, §19.
`HEX_RSP` is `status`(1) + `n`(1) + `hex[n]` = **2 + N**, giving a 20 + N byte frame.
The log already implemented this per §7.6; v0.4 corrects §6's `3 + N` and §19's
`21 + N` to match.

**Do:** confirm the codec emits and accepts 2 + N. Add a test asserting the exact
frame length for a known `n` (`n = 0` and `n = 10` at minimum) rather than only a
round-trip, so a future off-by-one in either direction fails loudly.

**Acceptance:** `test_framing` contains an explicit `HEX_RSP` length assertion; no
code change expected. If the codec turns out to implement 3 + N anywhere, that is a
bug — fix it and say so in the PR.

### E2 — oversize frames get their own counter

**Spec:** §3, §14 stage 2a.
An oversize frame (> `LRAN_MAX_FRAME`, 222) is a counted runtime discard, not an
assertion. The log already returns `BadLength` rather than asserting, which was right,
but folds it into `rx_bad_length`.

**Do:** add `rx_oversize` to `Counters` and count stage-2a rejects there. Leave
`rx_bad_length` for payload-length mismatches only.

**Why the split:** oversize means a foreign transmitter on the band or a misconfigured
PHY; bad length means a peer's encoder is wrong. Different faults, different fixes,
and at the gate the counter is the entire diagnosis.

**Acceptance:** a 255-byte input increments `rx_oversize` and nothing else; a
correctly sized frame with a wrong payload length increments `rx_bad_length` and
nothing else.

### E3 — `frag = 0x00` is a malformed header

**Spec:** §5.6, §14 stage 5b.
A declared total of `0` is meaningless. It is not a single-frame marker (`0x01` is).

**Do:** reject at stage 5b with `ERROR(BAD_LENGTH)`, counted `rx_bad_frag`. Add
`rx_bad_frag` to `Counters`. Index ≥ total keeps its existing
`ERROR(FRAGMENT_OVERFLOW)` treatment per §11.2 — check that it already has one.

**Acceptance:** tests for `frag = 0x00`, `frag = 0xF1` (index 15, total 1), and a
valid `frag = 0x01`, each landing on the right counter.

### E4 — schema/type pairing becomes normative

**Spec:** §7.1 (new carrying-type column), §14 stage 7.
The log implemented `(type, schema)` pairing rather than schema membership, and noted
it was stricter than v0.3 literally required. v0.4 makes it the rule.

**Do:** verify the pairing table in code matches §7.1 exactly, including the entry the
log had to guess: **schema `0xF0` travels as `STATUS`**, not as a message type of its
own. Add `0xFE` → `STATUS` if it is missing.

**Acceptance:** `test_schema` asserts acceptance for every valid pair in §7.1 and
rejection with `ERROR(UNKNOWN_SCHEMA)` for at least `STATUS`+`0x11`, `EVENT`+`0x10`,
and `STATUS`+`0x12`.

### E5 — split `NotImplemented` into two distinct errors

**Spec:** §9.2 (new normative sender rule).
`encode` currently returns `NotImplemented` for two unrelated conditions: an
authenticated type with no `IMac` (a **misconfiguration** — the caller wired the
library up wrong or shipped without key material), and fragmented `HEX_REQ` (a genuine
**library gap**). A field log cannot tell them apart, and the first is the more
serious and currently the less visible.

**Do:** introduce a distinct error — `MissingMac` or similar — for the authenticated-
without-`IMac` case. Keep `NotImplemented` for real gaps. §9.2 now states the refusal
as a normative sender rule, so keep the refusal exactly as it is; only the reporting
changes.

**Acceptance:** a test asserts `encode(COMMAND)` with no `IMac` returns the new error
and emits **no frame**.

---

## Branch 2 — `spec/v0.4-auth-frag`

The substantive branch. Read §9.4, §11 and §14 of v0.4 in full before starting.

### F1 — audit and report the current decode order

Before changing anything, determine where MAC verification currently sits relative to
reassembly, and write the answer into the engineering log and the PR description.

v0.3 was self-contradictory here — §14 put reassembly at stage 9 and §9.4 at stage 10,
while §11 gave each fragment its own MAC — so whichever order P1–P5 implemented, it
was implementing one half of a conflict. Knowing which half is the starting point for
F2 and tells you which tests are currently proving the wrong thing.

### F2 — split verification across the reassembly boundary

**Spec:** §9.4, §14 stages 9–11.

- **Per fragment, before it is buffered:** ctx check (§9.4 step 2), MAC verify
  (step 3).
- **Per set, once, on completion:** dedup (step 4), `seq` comparison (step 5),
  high-water update and dispatch (step 6).

Neither v0.3 ordering worked whole. Verifying after reassembly leaves no MAC to check,
since each fragment carries its own. Running all six steps per fragment means fragment
0 sets `rx_high_water` and fragment 1 — which shares the set's `seq` per §11.1 — is
rejected as a replay of itself, with the dedup cache flagging it first.

**Acceptance tests:**

- `test_fragmented_config_authenticated_roundtrip` — a 3-fragment authenticated
  `CONFIG` reassembles and dispatches once.
- `test_fragment_one_not_rejected_as_replay` — fragments sharing a `seq` do not trip
  step 5. This is the test that would have caught the v0.3 conflict.
- `test_bad_mac_fragment_never_buffered` — a fragment with a corrupt MAC is rejected
  and the reassembly slot is observably **untouched** (assert on state, not just on
  the return code).
- `test_dedup_applies_to_set_not_fragment` — replaying a whole completed set returns
  the cached ACK; replaying a single fragment of it does not re-dispatch.

### F3 — unauthenticated fragments cannot occupy a slot

**Spec:** §9.4 (bridge obligations, and the note on ordering).

This falls out of F2 but deserves its own test because it is a security property, not
a correctness one. Under v0.3's ordering, an attacker with no key could fill every
reassembly slot on the bridge with forged fragment-0 frames and hold them for
`frag_reassembly_timeout_ms` each, never completing the set.

**Acceptance:** `test_forged_fragment_zero_does_not_occupy_slot` — N forged fragment-0
frames arrive, then a legitimate set completes normally.

### F4 — sender-side fragmentation becomes deterministic

**Spec:** §11.1.

Every fragment except the highest index carries the **same** payload length. Chunk
size defaults to the maximum payload for the type — `LRAN_MAX_PAYLOAD_AUTH` (196)
authenticated, `LRAN_MAX_PAYLOAD_PLAIN` (204) plain — and fragment count is
`ceil(payload_len / chunk)`.

**Do:** add an optional `frag_chunk` parameter to the encode path, defaulting to the
per-type maximum. This is a **local sender parameter, not a wire field** — nothing in
the header carries it and a receiver cannot detect it.

**Why:** fragmentation must be a pure function of `(payload, type, chunk)` or the
Python generator in P6 and this codec can both be conformant and still emit different
bytes for the same input, and the vectors cannot be compared byte for byte. This is
the whole reason W4 gates the second implementation.

**Acceptance:** `test_fragment_sizes_uniform_except_last`, and a determinism test that
fragments the same payload twice and asserts identical bytes.

### F5 — receiver placement rule, made explicit

**Spec:** §11.2.

Reassembly is concatenation of fragment payloads in **ascending index order**. The log
already implemented this; v0.4 makes it normative and adds two rules that may not be
implemented yet:

- A duplicate index within a live set **overwrites** the stored fragment and is
  counted `rx_frag_duplicate`. It is not an error — retransmission and RF echo both
  produce it. Add the counter; note it counts an overwrite, not a discard, so it is
  **not** summed into `rx_dropped` in schema `0xF0`.
- A receiver must not assume placement before every lower-indexed fragment has
  arrived. Keep the staging buffer (guardrail 2).

**Acceptance:** `test_duplicate_fragment_index_overwrites`, plus keep
`test_fifteen_fragments_reversed` passing.

### F6 — reassembly displacement gets its own counter

**Spec:** §11.3.

The log counts abandonment (a new `(src, ctx_id, seq, schema)` displacing a live set)
under `reassembly_timeout`, reasoning that from the displaced set's point of view it
never completed. That satisfies repo rule 4 but merges two different diagnoses: a
timeout means the RF path dropped a fragment; an abandonment means the receiver is
undersized or a peer is interleaving sets.

**Do:** add `rx_reassembly_abandoned` and count displacement there.

§11.3 also now states minimum capacity: **one set per peer the receiver can receive
from** — one per provisioned node for the bridge, one for a node. Confirm the library
lets the bridge instantiate per peer; if `Reassembler` is a singleton, that is a
design note for the bridge implementation plan, not a change here. Report it either
way.

**Acceptance:** `test_displaced_set_counts_abandoned_not_timeout`.

### F7 — `HEX_REQ` / `HEX_RSP` are single-frame in v1

**Spec:** §11.4, §14 stage 8a.

The log returned `NotImplemented` for fragmented `HEX_REQ` and marked it
`TODO(spec-11)`. v0.4 resolves the TODO by **ruling the case out** rather than
defining it: `n` lives only in fragment 0 and `HEX_REQ`'s MAC requirement is
content-dependent on the command nibble inside the payload, so a receiver holding
fragments 1..N can determine neither the payload boundary nor whether the set should
have been authenticated.

**Do:**
- Sender: refuse to fragment these types (this is now a spec violation, not a gap —
  use a distinct error from E5's `NotImplemented`).
- Receiver: discard a fragmented `HEX_REQ`/`HEX_RSP` at stage 8a with
  `ERROR(BAD_LENGTH)`.
- Remove the `TODO(spec-11)` marker; it is answered.

**Acceptance:** `test_fragmented_hex_req_rejected` on both paths.

### F8 — `expect_ctx_id` semantics, pinned

**Spec:** §9.4 (bridge obligations).

The log added `DecodeCtx::expect_ctx_id` because v0.3's §9.4 step 2 had nowhere to
live, with zero meaning "do not check" — the bridge's position, since it has no
context of its own. v0.4 states this, and adds one requirement: **"no expected
context" means *skip*, never *expect zero*.** `0x00000000` means unknown (§5.5) and no
node ever sends it.

**Do:** confirm the implementation cannot be read as expecting zero. Keep
`test_ctx_mismatch_precedes_mac_check` — a stale context must read as `REJECTED_CTX`
and a resync, never as a forgery.

**Acceptance:** `test_zero_expect_ctx_skips_check` — a frame carrying any non-zero
`ctx_id` is accepted when `expect_ctx_id == 0`.

---

## Branch 3 — `p6/test-vectors` (milestone P6, open item W4)

This is the gate on simnode B0 and on the node and bridge firmwares being developed
independently. Until it lands, the codec has been checked against itself and against
published KATs for CRC-16/CCITT-FALSE, SHA-256, HMAC-SHA256 and HKDF-SHA256 — **the
framing has no independent witness.**

### P6.1 — write the generator from the specification, not from the codec

`/tools/vectors/` is a Python generator. It MUST share no code with the C++ codec —
including the CRC implementation and the serializer. A generator that reuses the code
under test witnesses nothing.

**Method, and this matters more than the code:** write the generator working from
`LRAN-Protocol-Specification-v0_4.md` alone. Do not read the C++ codec while writing
it. When the two disagree, **that disagreement is the finding** — resolve it against
the spec text, and record every one in the engineering log with the section that
settled it. Disagreements are the entire product of this milestone; a generator that
agrees on the first run because it was written by reading the codec has produced
nothing.

### P6.2 — vector file format

JSON, hex strings, one file per group under `/tools/vectors/`, committed alongside the
generator. Each vector carries at minimum: a name, the inputs (type, schema, header
fields, payload, key identity), the expected frame bytes, and the expected decode
outcome including the counter that should move on a negative case.

Fix the format before generating, and document it in `/tools/vectors/README.md` — §13.2
requires vectors to be regenerated on every protocol change, so this file is
long-lived.

### P6.3 — required vectors

Minimum set. Add more freely.

**Key derivation (do this one first):**
- HKDF-SHA256 KAT proving `info` byte for byte: `6e 6f 64 65 2d 01` for GateLink
  (§9.1). Not `"node-1"`, not `"node-01"`, not `"node-0x01"`.
- Derived key for each of `0x01`, `0x02`, `0xF0` from a fixed test master key.

This is first because getting it wrong is undetectable by inspection: the two sides
derive different keys, every authenticated frame fails its MAC, and no counter points
at key derivation. Everything downstream is meaningless if this is wrong.

**Single frames:** one per message type at minimum length and, where variable, at
maximum. Include the 222-byte maximum `PING` (§6.6.1) and both `HEX_REQ` sizes —
20 + N plain and 28 + N authenticated (§19).

**Fragmentation:**
- A `PING` fragmented with `frag_chunk = 14` into the full 15-fragment set (§6.6.2).
- An authenticated multi-fragment `CONFIG` — the case F2 exists for.
- The same set delivered in reverse and in shuffled order, expecting identical
  reassembled bytes (§11.2).
- A set with a duplicated index.

**Negative cases, each naming the counter that must move:** bad application CRC,
unknown `ver`, wrong `dst`, `frag = 0x00`, index ≥ total, oversize frame, payload
length mismatch, invalid `(type, schema)` pair, corrupt MAC, replayed set, fragmented
`HEX_REQ`.

### P6.4 — the fixed test key

Commit it in the clear, named unmistakably (`TEST-ONLY`), and add a build-time guard
or a loud warning if it is ever selected in a target build. It must never reach
hardware. It is not `secrets.h`; it is a test fixture and belongs in the repo.

### P6.5 — C++ vector suite

Add `test_vectors` to the Unity suites, reading the JSON and asserting encode output
byte-for-byte and decode outcome including counters. It runs under `native` alongside
the existing four suites.

### P6.6 — log the outcome

Append to the engineering log: every generator-vs-codec disagreement, which side was
wrong, and the spec section that settled it. If the count is zero, say so explicitly
and say how you avoided contaminating the generator — a zero here is either the best
possible result or evidence the method was not followed, and the log should let a
reader tell which.

---

## Branch 4 — `p7/target-build` (milestone P7)

### P7.1 — compile `platform/esp32/mbedtls_mac.cpp`

It has never been compiled and is marked UNVERIFIED in its own header. Build it for
ESP32-S3 under Arduino-ESP32 with ESP-IDF accessible.

### P7.2 — verify before unmarking

Remove the UNVERIFIED marker only after the HMAC-SHA256 and HKDF-SHA256 KATs from
P6.3 pass **on target**, not on host. The whole point of that file is that the target
uses mbedTLS where the host uses the portable implementation; agreement is the thing
being tested.

### P7.3 — run the vector suite on target

The full P6 vector set, on hardware. Any host/target divergence is a finding — the
serialization rules (§4) exist specifically to prevent it, and a divergence means
something is reading struct layout somewhere.

### P7.4 — record the footprint

Flash and RAM figures for ESP32-S3 into the engineering log, replacing the host
x86-64 table currently marked "indicative only until P7". Include `Reassembler`,
`Counters`, `Frame`, `Header` and the schema structs, and state the per-peer
reassembly cost for a bridge holding one set per provisioned node (§11.3).

---

## After P7 — W9, and what needs hardware

Not part of these branches; listed so the sequence is visible.

- **W9 bench runs.** The 222-byte `PING` and the fragmented `PING` over real RF, both
  before GateLink is installed at the gate. Neither is fixable remotely.
- **W10.** Once `/lib/lran-config/` exists, count the parameters. Past 21 `uint32`
  entries a full-set `CONFIG_ACK` readback fragments (§7.4), which moves §11 from a
  bench feature onto the production path for the first config read.
- **W11.** Decide whether the bridge excludes echoed `PING` frames from its per-node
  loss and ordering statistics, before the range test produces numbers anyone trusts.

---

## Standing notes

**`tick()` requires a monotonic clock.** The log lost an hour to
`test_fifteen_fragments_reversed` failing because the test fed timestamps `1000 + i`
while delivering fragments 14→0, running the clock backwards and underflowing
`now_ms - start_ms_` to ~4.29 × 10⁹. The underflow is deliberate — it is what makes
`test_timeout_survives_millis_wrap` work across the ~49-day rollover — but it means an
injected clock must be monotonic. **Promote this from a log entry to a documented
precondition on `tick()`**, in the header comment. The bridge's `lora_task` is about
to become the second caller.

**Raise rather than guess.** If a task requires a wire change, or the spec and the
code disagree in a way no section settles, stop and report it. v0.4 exists because
three such questions were reported instead of guessed at, and one of them —
undefined fragment placement — would have produced payloads that reassemble into the
wrong bytes with a valid CRC on every frame, between two implementations that both
believed they were conformant.
