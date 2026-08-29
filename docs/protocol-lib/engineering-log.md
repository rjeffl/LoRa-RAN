# `/lib/lran-protocol/` — engineering log

Dated entries. Measurements, surprises, and things that cost an hour.
Binding specification: `LRAN-Protocol-Specification` v0.3 (`ver = 2`).

---

## 2026-08-28 — P1–P5 built (skeleton, framing, MAC, schemas, fragmentation)

First code in the project. Milestones **P1–P5** of
`LRAN-Protocol-Library-Implementation-Plan`. **P6 (W4 vectors) and P7 (target build)
are not done** — see "Not done" below.

### Result

71 Unity tests across four suites, all passing under `native`:

```
pio test -d lib/lran-protocol -e native
native  test_frag     15   native  test_crypto   12
native  test_schema   11   native  test_framing  33
```

Compiles clean under `-Wall -Wextra -Werror`, and also under a stricter throwaway
pass adding `-Wconversion -Wshadow -Wpedantic` (not adopted in `platformio.ini`, but
worth knowing it is clean today). Every header under `include/lran/` compiles
standalone. No Arduino, ESP-IDF, mbedTLS, `millis()`, `Serial`, `malloc` or `new`
reachable from `include/` or `src/` — checked by grep, not by assumption.

### Static footprint (host, x86-64 — indicative only until P7)

| Type | Bytes |
|---|---:|
| `Reassembler` | 520 |
| `Counters` | 64 |
| `Frame` | 48 |
| `Header` | 20 |
| `schema::GateLinkStatusV1` | 84 |
| `schema::GateLinkConfigV1` | 306 |
| `schema::GateLinkConfigAckV1` | 324 |

`Reassembler` is the one worth noting: it holds **two** 204-byte payload buffers, not
one. See "Fragment placement" below. A bridge holding one per peer across four
simnodes plus GateLink is ~2.6 KB, which is affordable; the number is recorded here
so it is a decision rather than a discovery.

### Three specification findings

1. **`HEX_RSP` length — erratum.** §7.6's field table gives `status`(1) + `n`(1) +
   `hex[n]` = **2 + N**. §6's message-type table says **3 + N**, and §19 says
   **21 + N** while its own layout line `header + [status][n][hex:N] + crc:2`
   computes to 20 + N. §19 therefore contradicts itself. Implemented as **2 + N** per
   §7.6, on instruction. §6 and §19 need correcting.

2. **Fragmented `PING` cannot exceed an unfragmented one.** §11 caps `PING`
   reassembly at `LRAN_MAX_PAYLOAD_PLAIN` (204). The `PING` payload is
   `[ping_flags][n][data:N]`, so a reassembled set holds N ≤ 202 — exactly the
   single-frame cap §6.6.1 sets. §6.6.2 describes the bench tool requesting "`n`
   above the single-frame cap" to drive fragmentation; as specified that request
   cannot be satisfied, and `n` is a `uint8` so it could not exceed 255 in any case.
   Implemented **spec-literal at 204** on instruction. The mechanism §11 relies on to
   exercise reassembly over the air (open item **W9**) does not currently do what
   §6.6.2 says it does. Raising the cap, or widening `n`, is a specification decision.

3. **§11 does not define fragment placement.** It says a sender splits a payload into
   ≤15 fragments sharing `(src, seq, schema)` and that the receiver reassembles by
   `(src, ctx_id, seq, schema)`, but never says where fragment *i*'s bytes land. Two
   conventions are possible and they are not interchangeable: uniform chunk size with
   `offset = index × chunk`, or plain concatenation in index order.

   **Implemented as concatenation in index order**, which imposes no constraint on
   the sender and handles unequal fragment sizes and arbitrary arrival order. The
   cost is the second buffer in `Reassembler`: fragments land in a staging area in
   arrival order and are permuted into index order on completion, because with
   variable sizes a fragment's final offset is unknown until every lower-indexed
   fragment has arrived. A uniform-chunk rule would halve the RAM; it would also be a
   sender constraint the specification does not currently state, and getting it wrong
   between two independently written implementations produces silently misassembled
   payloads. **§11 should state whichever convention is intended** before a second
   implementation (the W4 Python generator) is written against it.

### Decisions made in code, worth review

- **`DecodeCtx::expect_ctx_id`.** The implementation plan's `DecodeCtx` has no
  ctx field, so §9.4 step 2 (ctx before MAC) had nowhere to live in the codec. Added
  as an optional field: zero means "do not check", which is the bridge's position
  since it has no context of its own (§10.1). Checked only for authenticated types,
  which is where §9.4 places it. Ordering is asserted by
  `test_ctx_mismatch_precedes_mac_check` — a stale context must read as
  `REJECTED_CTX` and a resync, never as a forgery.

- **Schema validation is `(type, schema)` pairing, not membership.** A `STATUS`
  announcing schema `0x11` (an EVENT schema) is rejected at §14 stage 7 rather than
  misparsed at stage 8. Stricter than §7.1 literally requires; stage 8's wording
  ("payload length matches `(type, schema)`") implies the pair is the unit.

- **`encode` refuses an authenticated type with no `IMac`.** Returns
  `NotImplemented` rather than emitting the frame unauthenticated. A silently
  unauthenticated `COMMAND` is a relay pulse anyone with an SDR can forge.

- **Fragmented `HEX_REQ` returns `NotImplemented`.** MAC presence for `HEX_REQ` is
  content-dependent (§7.6) and `n` lives only in fragment 0, so a receiver cannot
  infer the payload boundary for fragments 1..N. Unreachable in practice — a
  VE.Direct HEX string is tens of bytes against a 194-byte authenticated single-frame
  cap — and reported rather than guessed at. Marked `TODO(spec-11)`.

- **Oversize frame (> 222 B) returns `BadLength`, not an assertion.** §3 calls it a
  programming error, but the PHY hands up to 255 bytes and a foreign transmitter can
  produce one, so it is counted under `rx_bad_length` like any other discard.

- **KDF `info` is `"node-"` plus the RAW address byte**, 6 bytes, not the ASCII
  rendering of the node ID. `||` is byte concatenation everywhere else in §9. **The
  Python side of W4 must agree**: get this wrong and every node is provisioned with a
  key the bridge cannot reproduce, and the only symptom is that every command is
  rejected with nothing pointing at the cause.

- **`Reassembler` abandonment is counted under `reassembly_timeout`.** Only one set
  is held; a new `(src, ctx_id, seq, schema)` arriving mid-set abandons the old one.
  From that set's point of view it never completed within its window, and repo rule 4
  forbids the discard being silent.

### An hour lost

`test_fifteen_fragments_reversed` failed with the set never completing. The cause was
in the test, not the library: delivering fragments 14→0 with timestamps `1000 + i`
fed the reassembler a clock running **backwards**, so `now_ms - start_ms_` underflowed
to ~4.29 × 10⁹ and expired the set on every fragment. The underflow is deliberate —
it is what makes `test_timeout_survives_millis_wrap` work across the ~49-day
`millis()` rollover — but it means an injected clock must be monotonic. Worth
remembering when the bridge's `lora_task` starts driving `tick()`.

### Not done

- **P6 — W4 test vectors.** `/tools/vectors/` does not exist. Until it does, the
  codec has only been checked against itself and against published known-answer
  vectors for CRC-16/CCITT-FALSE, SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 4231 cases
  1 and 2) and HKDF-SHA256 (RFC 5869 case 1). **The framing itself has no independent
  witness.** P6 gates simnode B0 for exactly this reason.
- **P7 — target build.** `platform/esp32/mbedtls_mac.cpp` is written but **has never
  been compiled**; it is marked UNVERIFIED in its own header. Flash and RAM figures
  for the ESP32-S3 belong in this log once P7 runs.
- `/lib/lran-config/` (Implementation Plan §4) is not started.

---

## 2026-08-29 — spec v0.4 errata and the auth/fragmentation split (E1–E5, F1–F8)

Two branches against `LRAN-Protocol-Specification` v0.4. No wire change: `ver` stays
`2`, no header field, authentication scope or schema layout moved, and every frame
valid under v0.3 stays valid. 71 tests → 90.

### F1 — where MAC verification actually sat (the audit, done before changing anything)

v0.3 contradicted itself: §14 put reassembly at stage 9 and §9.4 at stage 10, while
§11 gave every fragment its own MAC. P1–P5 implemented **the §11 half, which is the
half v0.4 keeps**:

- `decode_header` runs §14 stages 2–6.
- `decode_payload` runs stages 7–8 and then §9.4 step 2 (`ctx_id`) and step 3 (MAC),
  over that frame's own header and payload chunk.
- `Reassembler::accept` is called by the caller *afterwards*.

So per-fragment authentication already preceded reassembly. The `test_ctx_mismatch_
precedes_mac_check` and `test_command_mac_roundtrip_and_tamper` tests were proving the
right thing, and no reordering was needed.

**What was missing is the other half.** §9.4 steps 4–6 — dedup cache, `seq`
comparison, high-water update, dispatch — **do not exist anywhere in this library**,
and by the Implementation Plan §1 they should not: "sequence arithmetic" is in scope,
node behaviour is not. `seq_newer()` is the arithmetic primitive and nothing consumes
it. The consequence is that the v0.3 failure mode F2 describes — fragment 0 setting
`rx_high_water` and fragment 1 being rejected as a replay of itself — was never
reachable here, because the replay half had no implementation to get wrong.

That leaves an **open question for the bridge and GateLink plans, not for this
library**: steps 4–6 have to live somewhere, they are the same logic on both sides,
and §10.4 makes command dedup mandatory because a relay pulse is not idempotent.
Written up in the PR description rather than patched locally.

What the library *can* own, and now does, is the ordering itself — see F2/F3 below.

### F2 / F3 — the ordering is now enforced, not merely documented

`Frame` gains `mac_verified`, set only when `decode_payload` actually computed the MAC
and it matched. A frame decoded with `DecodeCtx::mac == nullptr` carries `mac` but not
the flag: the bytes are present and were never checked.

`Reassembler::accept` refuses a **multi-fragment** frame of an authenticated type
without it, with `Status::BadMac`. Single-frame frames are exempt — nothing is held
and the caller has the frame either way.

**Why enforce rather than document.** §14's "stage 9 precedes stage 10" is a security
property: without it an attacker holding no key fills every reassembly slot on the
bridge with forged fragment-0 frames and holds each for
`frag_reassembly_timeout_ms`, and the forgery is detected only on completion, which
the attacker never allows. Four firmwares will call this library. A precondition that
one of them forgets is a denial-of-service on the bridge, so the precondition is
checked rather than written down.

`test_forged_fragment_zero_does_not_occupy_slot` cost twenty minutes to a bug in the
test: the forgery varied `seq` by writing `attempt` into byte 4, and the genuine set's
`seq` was `0x0101`, so `attempt == 1` reproduced the real frame byte for byte and
verified legitimately. Offset clear of the real value now. Worth remembering when
building negative vectors in P6 — a "corrupted" frame that happens to land on a valid
one is a false pass, and the vector generator will be doing this a lot.

### F5 — the duplicate-overwrite rule needed a second length

§11.2 makes a duplicate index an **overwrite**, where P1–P5 ignored it. Overwriting is
not free given the staging buffer holds fragments at *arrival-order* offsets: a
duplicate of the same length overwrites in place, but one of a different length has to
be appended, leaving the superseded copy as dead space.

That broke the cap check. `stage_used_` was doing two jobs — staging occupancy and
set length — and charging dead bytes against §11.2's reassembly cap would reject a set
that fits. Split into `stage_used_` (staging, dead space included, bounded by the
buffer) and `set_len_` (the sum of the *current* fragment lengths, bounded by the
cap). Exhausting staging is `FRAGMENT_OVERFLOW` — loud, not a misassembly.

`rx_frag_duplicate` counts an overwrite, not a discard, so it is deliberately **not**
summed into `total_dropped()` and does not reach schema `0xF0`'s `rx_dropped`. It is
the one counter with no `Status` mapping; the `Reassembler` increments it directly.

### F6 — reassembly capacity, and the answer to the question §11.3 asks

`rx_reassembly_abandoned` split out of `reassembly_timeout`. A timeout means the RF
path dropped a fragment; an abandonment means the receiver is undersized or a peer is
interleaving sets. Both are drops, both reach `rx_dropped`, and they point at
different fixes.

**§11.3 capacity: the library is already fine.** `Reassembler` is a plain class, not a
singleton and holding no static state, so a bridge instantiates an array of them — one
per provisioned node — and a node holds one. Measured at 520 B per instance on host
x86-64 (two 204-byte payload buffers plus the index tables), five provisioned nodes
cost ~2.5 KB. The ESP32-S3 figure belongs here after P7. That is a **bridge implementation-plan item**, not a library change: nothing
here needs to move, but the bridge has to actually allocate per peer rather than share
one, and if it shares one, `rx_reassembly_abandoned` is the counter that will say so.

### F7 — `TODO(spec-11)` is answered by ruling the case out

The TODO asked how a receiver infers MAC presence for a fragmented `HEX_REQ` whose `n`
lives only in fragment 0. §11.4 answers it by excluding the case: a receiver holding
fragments 1..N can determine neither the payload boundary nor whether the set should
have been authenticated. Marker removed.

Implemented as §14 **stage 8a** generally rather than as a `HEX_REQ` special case,
since §11.4's table also rules out `COMMAND`, `COMMAND_ACK`, `POLL` and `ERROR`, and
stage 8a is written as "type is fragmentable". A fragmented `COMMAND` would otherwise
have passed: stage 8's fixed-length check is skipped for fragments, so nothing else
would have caught it.

Sender-side the refusal is `Status::NotFragmentable` — a refusal to violate the
specification, distinct from `MissingMac` (a misconfiguration, E5) and
`NotImplemented` (a genuine gap). Three conditions that a field log previously could
not tell apart, of which the misconfiguration was the most serious and the least
visible.

### Errata, briefly

- **E1** `HEX_RSP` was already 2 + N per §7.6; v0.4 corrected §6 and §19 to match. No
  code change, but the length is now asserted exactly at `n = 0` and `n = 10` rather
  than only round-tripped against itself.
- **E2** `rx_oversize` (§14 stage 2a) split from `rx_bad_length`.
- **E3** `frag = 0x00` is stage 5b / `rx_bad_frag`, moved into `decode_header` ahead of
  stage 6. Index ≥ total keeps `FRAGMENT_OVERFLOW` per §11.2.
- **E4** The `(type, schema)` pairing table already matched §7.1's new carrying-type
  column, `0xF0` → `STATUS` and `0xFE` → `STATUS` included. No code change; the whole
  registry is now pinned by a test.

### One spec wording discrepancy, resolved against the task list

§14's stage 5b row describes the check as "`frag` well formed — total ≥ 1, index <
total" and assigns the whole row `rx_bad_frag` / `ERROR(BAD_LENGTH)`. §11.2 and §5.6
assign index ≥ total to `ERROR(FRAGMENT_OVERFLOW)` instead. E3 settles it — total `0`
is `rx_bad_frag`, index ≥ total keeps `FRAGMENT_OVERFLOW` — and the code follows E3.
**§14's stage 5b row is worth a wording fix** so it does not read as overriding §11.2.

### `tick()` preconditions are now on the function

Promoted from the 2026-08-28 entry to a documented precondition in `reassembly.h`, on
both `accept()` and `tick()`: the injected clock MUST be monotonic. The unsigned age
arithmetic is deliberate — it is what makes `test_timeout_survives_millis_wrap` work
across the ~49-day rollover — and the cost is that a clock running backwards underflows
to ~4.29 × 10⁹ and expires every set on arrival. The bridge's `lora_task` is the
second caller.

### Not done

- **P6 — W4 test vectors.** Still not started; `/tools/vectors/` does not exist. The
  framing still has no independent witness.
- **P7 — target build.** `platform/esp32/mbedtls_mac.cpp` still has never been
  compiled.
- **§9.4 steps 4–6.** Out of this library's scope by the Implementation Plan; needs a
  home. See F1.

---

## 2026-08-29 — P6 / W4: the framing gets an independent witness

`/tools/vectors/` now exists: a Python generator, a self-check, and 69 vectors in
four groups (kdf 6, single 34, frag 9, negative 20), consumed by a new `test_vectors`
Unity suite under `native`.

### Method, because the result is only worth what the method was

The generator was written by a party working from `LRAN-Protocol-Specification-v0_4.md`
**alone**, with `/lib/` off limits — not the headers, not the sources, not the tests,
not this log, not the implementation plan or the task list, and not v0.3. It shares no
code with the C++ codec: its CRC-16, header serializer, fragmenter and payload
builders are all written from the prose. Only `hashlib` and `hmac` are shared ground,
and those are independent implementations of published primitives, cross-checked
against RFC 5869 HKDF cases 1 and 3 and CRC-16/CCITT-FALSE `"123456789"` → `0x29B1`
before anything else was built.

This matters because by the time P6 started, the codec had been read closely enough
that anyone who had done E1–E5 and F1–F8 could no longer write an independent
generator. A generator written by reading the codec reproduces that codec's reading of
the spec, misreadings included, and agrees on the first run for the wrong reason.

**Disagreements were the deliverable, and there were four.** Recorded below with the
section that settled each.

### Finding 1 — reserved `hdr_flags` bits: the codec was right, the FORMAT was wrong

The generator emitted a `POLL` with `hdr_flags = 0x40`. §5.8's table is explicit —
bits 6:0 are "Reserved — **write `0`**, ignore on receive" — so a conforming encoder
writes zero and the codec's masking is correct.

But the vector's intent was right and valuable: a receiver **must** accept those bits
set. The two halves are deliberately asymmetric and cannot be witnessed by one
encode-and-compare vector. The vector format had no way to say "no encoder emits this,
a receiver must accept it", which was a defect in the format — mine — not in the
generator. Fixed by adding `decode_only` to `/tools/vectors/README.md`; the generator
now asserts at generation time that `decode_only` and "sets reserved bits" imply each
other in both directions.

### Finding 2 — a fragment arriving AFTER its set completed is undefined

The duplicate-index vector delivered indices `0..14` — completing the set — and then
index 4 twice more. §11.2 defines the overwrite for "a duplicate index within a
**live** set", and a completed set is not live; §11.2 defines expiry only for
*incomplete* sets. **The specification says nothing about a fragment arriving after
its set completed.**

The codec starts a new set (`accept` resets on a completed set before beginning). The
vector assumed the duplicate was absorbed into the completed one. Both are defensible
readings of silence, so the vector was moved to deliver its duplicates mid-set, which
is the case §11.2 actually describes.

**This needs a v0.5 decision.** It is not exotic: an RF echo of a late fragment and
the bridge's own retry both produce it. A receiver today may plausibly start a bogus
new set, resurrect a completed one, or drop the frame silently — and the third
violates repo rule 4. Whatever v0.5 chooses wants a counter.

### Finding 3 — `index ≥ total`: §14 stage 5b and §11.2 flatly contradict each other

- §14 stage 5b: "`frag` well formed — total ≥ 1, **index < total** (§5.6)" →
  `rx_bad_frag`, `ERROR(BAD_LENGTH)`
- §11.2: "A fragment index ≥ the declared total ... is discarded with
  `ERROR(FRAGMENT_OVERFLOW)`"

The codec follows §11.2, because task E3 says so explicitly. The generator followed
§14, reasoning that the ladder is ordered and stage 5b names the exact condition.

**The value here is that the disagreement was reproduced independently.** The §14
stage 5b wording was already flagged as suspect during E3 from reading it alone. An
implementer who read that section cold, with no access to the codec, landed on the
opposite answer. That is about as strong as evidence gets that the wording actively
misleads, and it is the failure class §11.2's own note calls out — two conformant
implementations disagreeing, with a valid CRC on every frame.

**Proposed errata:** stage 5b's row should read "`frag` well formed — total ≥ 1" and
drop "index < total" entirely. A summary table that contradicts the section it
summarises is the summary's bug.

### Finding 4 — stage 8a's counter: the CODEC was wrong

§14 stage 8a names the wire error `ERROR(BAD_LENGTH)` but names **no counter**, and
the codec folded it into `rx_bad_length`. The generator split it out as
`rx_not_fragmentable`, arguing that "a peer fragmented a type that may not be
fragmented" and "a peer's encoder got a length wrong" are different faults with
different fixes.

That is precisely the reasoning v0.4 itself used to split `rx_oversize` out of
`rx_bad_length` (§14 stage 2a) and `rx_reassembly_abandoned` out of
`rx_reassembly_timeout` (§11.3). **The codec changed.** `Status::NotFragmentable` now
serves both directions and maps to a new `rx_not_fragmentable`; the wire answer stays
`ERROR(BAD_LENGTH)` per §11.4, the same status/error asymmetry §5.6 already uses for
`BadFrag`. `encode()` never touches `Counters`, so the sender's use of the status
costs nothing.

### Finding 5 — two counters were missing their `rx_` prefix, and one contradicts §11.3

Raised by the generator while checking names. §11.3 names the counter
`rx_reassembly_timeout` in prose ("counted `rx_reassembly_abandoned`, **not**
`rx_reassembly_timeout`"). The codec had `reassembly_timeout`, unprefixed — a
**code-versus-spec disagreement**, and the specification wins. `fragment_overflow` was
unprefixed too, and v0.4's new `rx_reassembly_abandoned` had been sitting directly
beside its unprefixed sibling without anyone noticing.

Renamed to `rx_reassembly_timeout` and `rx_fragment_overflow`. A P1–P5 slip that
survived every review because nothing had ever compared the counter names against the
spec text. Cheap to fix now; it would have been a breaking rename once the bridge
published these to MQTT.

### What agreed on the first run

Everything else, byte for byte: all six derived keys, every §19 single-frame length
including the 222-byte maximum `PING` and both `HEX_REQ` sizes, and all nine
fragmentation sets — in-order, reversed, shuffled and duplicate delivery — reassembling
to the pre-fragmentation bytes exactly.

The key derivation agreeing matters most. §9.1's `info` is `"node-" || <raw byte>`,
and HKDF's output length `L` is **never stated in §9.1**; the generator inferred 32
from the key feeding HMAC-SHA256. Had either differed, every authenticated frame would
have failed its MAC with no counter pointing at key derivation. **§9.1 should state
`L = 32` explicitly.**

### Convergence

Three rounds. After the four findings above plus the counter rename were applied, the
two implementations agree on **all 69 vectors**: `check.py` passes and `test_vectors`
passes with zero disagreements. Total suite: **94 tests** under `native`, from 71 at
the start of v0.4 work.

Adjudication was necessarily joint — the generator was told what the *spec* said, in
citations it could check itself (§5.8, §11.2, §11.3), never what the codec did. Two
items reached it as external authority it could not have derived: the stage-10
adjudication for `index ≥ total` (task E3) and the `rx_` prefix renames (§11.3 line
1437). Both are on the record here because a reader assessing how much the agreement
is worth needs to know which parts were independent and which were handed over.

### Notes for the C++ suite

- It reports **every** disagreement rather than aborting at the first. Unity stops a
  test function on its first failed assertion, which surfaced one finding and hid
  eleven. The complete inventory is the product of this milestone.
- Negative vectors assert **exactly one** counter moved — one frame, one discard.
  Fragmentation vectors assert only that the named counter moved and no other did: a
  set delivers many frames, and a counter can legitimately move once per fragment.
- The JSON reader is test-only, fixed-arena, and nothing in `/lib/` may include it.

### Still open

- **P7 target build.** `platform/esp32/mbedtls_mac.cpp` is still UNVERIFIED and has
  still never been compiled. The `espressif32` platform is installed locally, so the
  compile itself (P7.1) is reachable without hardware; `firmware/bridge/` is an empty
  shell and needs a build harness first. **P7.2–P7.4 need a board on USB** — the point
  of that file is that the target uses mbedTLS where the host uses the portable
  implementation, and agreement is the thing being tested.
- The vector suite reads JSON from disk, which works under `native`. **P7.3 will need
  the vectors embedded** as a generated header, since the target has no filesystem.
