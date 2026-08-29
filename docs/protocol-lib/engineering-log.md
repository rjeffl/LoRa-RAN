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
