# `/lib/lran-protocol/` — engineering log

Dated entries. Measurements, surprises, and things that cost an hour.
Binding specification: `LRAN-Protocol-Specification` v0.7 (`ver = 2`).

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

---

## 2026-08-29 — P7: target build on Heltec WiFi LoRa 32 V3 (ESP32-S3)

All five suites now run on hardware. **97 tests pass on target**, including all 69 W4
vectors. Board: `heltec_wifi_lora_32_V3`, CP2102 at `/dev/cu.usbserial-0001`.

### The finding — `mbedtls_hkdf` does not exist on this platform

`platform/esp32/mbedtls_mac.cpp` compiled cleanly and then **failed at link**:

```
undefined reference to `mbedtls_hkdf'
```

`<mbedtls/hkdf.h>` is present in the Arduino-ESP32 headers, so the file compiles; but
`MBEDTLS_HKDF_C` is not enabled in the prebuilt mbedTLS the Arduino framework ships,
so the symbol is absent. `mbedtls_md_hmac` links fine — only HKDF is missing.

**This is exactly the class of defect P7 exists to catch, and it is unreachable from
the host.** The file had been written, reviewed and marked UNVERIFIED for precisely
this reason; a compile-only check would have passed it.

Fixed by building HKDF-SHA256 out of `mbedtls_md_hmac` per RFC 5869 §2 — extract then
expand — rather than depending on an optional mbedTLS module. That keeps the
derivation working under any future `sdkconfig`, and still runs the actual hashing
through the platform's vetted HMAC. `kNodeKeyLen` is exactly one SHA-256 block so a
single expand iteration suffices, but the loop is written out so a longer key length
cannot silently truncate.

Enabling `MBEDTLS_HKDF_C` was the alternative and was rejected: it needs a rebuilt
mbedTLS, which is not available under `framework = arduino` with precompiled
libraries, and it would make key derivation depend on a build-config flag that a
future framework bump could silently flip. On a fleet with no OTA, a key-derivation
dependency that can vanish in a dependency update is not worth the twenty lines it
saves.

### P7.2 — mbedTLS agrees with the portable implementation

Three new target-only tests in `test_crypto`, all passing on hardware:

- `test_mbedtls_hmac_matches_refimpl` — message sizes 0, 1, 16, 63, 64, 65, 128 and
  222, chosen to straddle the SHA-256 block boundary. A backend that mishandles the
  final block passes a short case and fails here.
- `test_mbedtls_hkdf_matches_refimpl` — every provisioned address and the whole bench
  range. One wrong key per node is one node that silently cannot be commanded.
- `test_mbedtls_hmac_rfc4231_case1` — the published KAT recomputed through mbedTLS, so
  the target is checked against the standard and not merely against its neighbour.

**The UNVERIFIED marker is removed.** Per P7.2 it came off only after these passed on
target, not on the host.

One self-inflicted failure worth recording: the RFC 4231 constant was first entered as
`b0 03 44 c6...` by mis-splitting the hex string `b0344c61...`, whose correct first
eight bytes are `b0 34 4c 61 d8 db 38 53`. The test caught it immediately, but it is
the same failure mode §9.1 warns about for `info` — a hand-transcribed byte string
that looks plausible and is wrong.

### P7.3 — the vector suite on hardware

**Zero host/target divergence.** All 69 vectors produce identical bytes on x86-64 and
xtensa. That is what §4's serialization rules exist to guarantee, and a divergence
would have meant something was reading struct layout somewhere.

Getting there required a change the host never forced: the suite read its vectors from
JSON through a fixed-arena parser costing **945 KB of static RAM**, against the
Heltec's 320 KB. It would not have linked. `tools/vectors/embed.py` now emits
`vectors_data.h`, a generated header of `const` arrays that live in flash (89 KB
against 8 MB) and cost essentially no RAM. The JSON remains the source of truth and
`check.py` still validates it; the header is a build artifact. Native and target now
consume byte-identical data, which is stronger than the arrangement it replaced.

The suites also needed dual entry points — `main()` on the host, `setup()`/`loop()`
under Arduino. Unity's `setUp`/`tearDown` are distinct names and do not collide.

### P7.4 — static footprint, ESP32-S3

Measured on target. These replace the host x86-64 table above, which was marked
indicative only.

| | ESP32-S3 (xtensa) | host (x86-64) |
|---|---|---|
| `Header` | 20 B | 20 B |
| `Frame` | 36 B | 56 B |
| `Counters` | 84 B | 80 B |
| `Reassembler` | **508 B** | 520 B |
| `GateLinkStatusV1` | 84 B | 84 B |
| `GateLinkEventV1` | 16 B | 16 B |
| `NodeHealthV1` | 20 B | 20 B |
| `GateLinkConfigV1` | 306 B | 306 B |

`Frame` is smaller on target (4-byte pointers); `Counters` grew by 4 B with the v0.4
additions.

**Per-peer reassembly cost (§11.3):** **508 B per set.** A bridge holding one set per
provisioned node costs **2,540 B for five nodes** — 0.8 % of the ESP32-S3's 320 KB.
The capacity rule §11.3 requires is cheap; there is no reason for the bridge to share
one `Reassembler` across peers, and if it does, `rx_reassembly_abandoned` is the
counter that will say so.

Whole test firmware, for scale: **RAM 18,544 B (5.7 %), flash 285,317 B (8.5 %)** —
and that includes all 69 embedded vectors and the Unity harness, neither of which
ships in node firmware.

### Still open

- **W9 bench runs** — the 222-byte `PING` and the fragmented `PING` over real RF, both
  before GateLink is installed at the gate. Needs two boards and an antenna; nothing
  in P7 exercises the SX1262 at all.
- `firmware/bridge/` and `firmware/simnode/` remain empty shells. The `esp32s3`
  environment added here is a **test** environment, deliberately: it needs no WiFi, no
  MQTT and no `secrets.h`, and it should not grow into firmware.

---

## 2026-08-29 — v0.5 conformance revision (C1–C4, L1–L3, V1–V5, T1–T3)

104 tests under `native`, **107 on target**, 71 W4 vectors, zero host/target
divergence. Heltec WiFi LoRa 32 V3 on `/dev/cu.usbserial-0001`.

### The counter registry (C1–C2)

§14.1 is now normative: 21 counters, each with the stage that raises it and whether it
sums into `rx_dropped`. Two renames — `rx_bad_mac` → `rx_rejected_mac`,
`rx_ctx_mismatch` → `rx_rejected_ctx` — and three additions: `rx_rejected_seq`,
`rx_dup_command` and `rx_frag_late`.

`rx_rejected_seq` and `rx_dup_command` are **carried but never raised here**. They
belong to §9.4 steps 4–6, which are out of scope by Implementation Plan §1 (W12).
`Counters` is the aggregate that reaches schema `0xF0`, and an `rx_dropped` missing the
replay rejections would understate drops on exactly the frames that move a gate.

`kCounterRegistry` now holds the name/field/`in_dropped` table, and `total_dropped()`
is computed **from** it. The sum can no longer drift from the registry the way the
names once did. The registry test spells all 21 names out independently of that table —
duplicating the list is the point, because a registry that checks itself checks
nothing, and this is the test that would have caught the missing `rx_` prefixes through
all of P1–P5.

### The late-fragment rule (L1–L3) — the one behaviour change

§11.2 now defines the post-completion case that v0.4 left undefined, and it defines it
**against** what the codec did. The rule was not overruled: the reasoning holds. Under
the old behaviour one echoed fragment opened a set that could never complete, held the
slot for `frag_reassembly_timeout_ms`, blocked a legitimate set behind it, and then
reported `rx_reassembly_timeout` — a counter naming a fault that did not occur.

The `Reassembler` now retains the key of the last **multi-fragment** set completed in
each slot and discards matching fragments as `rx_frag_late`, no `ERROR`. Ten bytes.
Only completion retains a key: a timed-out or abandoned set never completed, so a
fragment carrying its key is a legitimate retry that opens a fresh set.

### A pre-existing silent discard, found while gating L2

A single-frame frame arriving mid-set called `begin()`, which reset a live
multi-fragment set **with nothing counted** — a repo rule 4 violation, reachable by any
bridge routing all frames through one `Reassembler` per peer, where a `STATUS` would
silently destroy an in-progress fragmented `CONFIG_ACK` from the same node.

Now counted `rx_reassembly_abandoned` (§11.3's counter for a displaced slot). **This is
the rule-4 fix, not a design decision.** If the intent is that single frames should not
disturb the slot at all, that needs a spec answer and a second buffer.

### The vectors (V1–V5): 69 → 71, and not one existing frame byte changed

Every frame, fragment set and derived key was diffed against the committed v0.4 set:
**zero changed, two added.** That is independent confirmation of v0.5's claim to alter
no header field, no authentication scope and no schema layout — the revision is prose,
counters and provenance.

New: `ping_chunk14_late_fragment` (the vector relocated during P6 finding 2, back now
that §11.2 defines the case) and `reserved_type_0x00` (§6 reserves `0x00`, so a zeroed
type byte is rejected as unknown rather than defaulted).

**Provenance is now explicit per vector: 65 derived, 6 adjudicated.** The adjudicated
six are the stage 5b resolution, the three stage-9 counter renames, and the
late-fragment rule — each a case where the expected value reached the generator from
outside the prose. The stage 6/7/8/8a counter names stay *derived*: the generator
proposed them from v0.4 before §14.1 existed and the spec adopted them, so the
direction of travel was generator-to-spec.

**A clean run is weak evidence this round, and the number should be read that way.**
Three of the four P6 findings are now spec text whose answers were handed over, and
most groups are unchanged bytes that already agreed in P6 — re-running them witnesses
that neither side regressed, which is worth having but is not new evidence. The only
genuinely new independent content is `reserved_type_0x00` and the V4 gates. The most
interesting new rule, late fragments, is precisely the one that could not be derived.

§14.1 is now **enforced** rather than followed: `generate.py` and `check.py` each carry
the 21-counter table independently, so a vector naming a counter outside it or at the
wrong stage fails at generation *and* at check. The `rx_dropped` column is enforced
too — a fragmentation vector that reassembles correctly may only name a counter §14.1
marks `no`, so a health metric can never be asserted to climb during correct operation.

V4's hazard is gated at both ends: no negative frame may reproduce any positive frame,
and no two negative vectors may share bytes. Verified by injection.

### Target re-verification (T1–T3)

107 tests on hardware, zero divergence. The HKDF constraint (§9.1) is now pinned by
`tools/checks/no_mbedtls_hkdf.py`, which fails on the include, the call or the feature
macro under `lib/`. It ignores comments deliberately: the first version matched the
prose in `mbedtls_mac.cpp` explaining why the dependency is absent, and the name of the
test that verifies the replacement. **A guard that fires on its own documentation gets
disabled.** Verified in both directions.

`static_assert(kNodeKeyLen == 32)` pins §9.1's now-explicit `L`, and the expand loop
still cannot truncate for a longer one.

### P7.4 footprint — v0.5 delta, ESP32-S3

| | P7 (v0.4) | v0.5 | Δ |
|---|---:|---:|---:|
| `Header` | 20 B | 20 B | — |
| `Frame` | 36 B | 36 B | — |
| `Counters` | 84 B | **96 B** | +12 B |
| `Reassembler` | 508 B | **520 B** | +12 B |
| bridge, 5 peers | 2,540 B | **2,600 B** | +60 B |
| `GateLinkStatusV1` | 84 B | 84 B | — |

`Counters` grew 12 B for three new fields (`rx_rejected_seq`, `rx_dup_command`,
`rx_frag_late`); the two renames cost nothing. `Reassembler` grew 12 B for the retained
key — §11.2 priced it at ten bytes and it cost twelve after alignment. **A bridge
holding one set per provisioned node now costs 2,600 B for five nodes, 0.8 % of SRAM.**

Whole test firmware: RAM 18,544 B (5.7 %) unchanged, flash 286,997 B (8.6 %), up
1,680 B — the two new vectors and the registry table.

### Open

- **W12** — §9.4 steps 4–6 still have no home. Largest open item; a decision, not code.
- **W9** — needs the second board *and* an SX1262 driver that does not exist. Nothing
  built so far has touched the radio.
- **§11.2's dead-space clause has no vector.** A differing-length duplicate exhausting
  staging is unreachable from the `frag` vector shape, which derives fragments from
  `(payload, chunk)` and cannot express a non-uniform duplicate. Needs a raw-frames
  vector form or a C++ unit test. Covered in `test_frag` today but not by W4.
- **`Status` names diverge from the counters beside them.** `CtxMismatch` sits with
  `rx_rejected_ctx`, `BadMac` with `rx_rejected_mac`, while §9.4 says `REJECTED_CTX` /
  `REJECTED_MAC`. C1 scoped the rename to counters, but §14.1 names counters and still
  names no `Status` anywhere — the same shape of defect §14.1 was created to retire.

---

## 2026-08-30 — spec v0.6 cleanup: G1–G6

`LRAN-Protocol-v0_6-Library-Tasks`, branch `spec/v0.6-cleanup`. Two of the four items
the v0.5 entry closed with, now that v0.6 answers them; one decision recorded; and the
log header, which has read "v0.3" since the first entry and was missed by V6 in the
v0.5 list.

`ver` stays at `2`. No header field, authentication scope or schema layout is touched,
and the regenerated vectors confirm it independently — see G4.

### G1 — a single-frame frame never touches reassembly state

The v0.5 entry found the defect and counted the discard under
`rx_reassembly_abandoned`. §11.2 now says the frame had no business in the slot at
all: a frame declaring `frag` total 1 may not **begin, join, displace or expire** a
set, even one it shares `(src, ctx_id, schema)` with, and is delivered directly.

**The second buffer was not needed, and the reason is worth stating.** The v0.5 entry
expected one. Checking the assumption first, as the task asked: a `frag` total of 1
needs no staging, so what was missing was a *delivery path that bypasses the slot*,
not *storage that duplicates it*. `Reassembler` already has a delivery buffer — `out_`,
written when a set assembles — and it is not set state. The single-frame path copies
into `out_`, sets `complete_`, and returns. `active_`, `got_mask_`, `stage_`,
`start_ms_` and the set key are untouched, so the live set behind it completes into
`out_` afterwards exactly as it would have. **Footprint delta: zero** (below).

Two consequences fell out that were not obvious from the rule:

- **`complete_` and `active_` are no longer mutually exclusive.** A delivered single
  frame sets `complete_` while a set is still live. `tick()` read `if (!active_ ||
  complete_) return;` as a belt-and-braces guard, and under the new state combination
  that would have left the live set unable to *ever* expire. It now gates on `active_`
  alone. `accept()`'s `if (complete_) reset()` — which frees the slot after a
  completed set — gained the same `&& !active_`, or the fragment arriving next would
  have destroyed the set it belongs to. Both are cases where the old code was correct
  only because of an invariant this change removes, which is the kind of thing that
  survives review by looking untouched.
- **"Expire" needed the single-frame case handled before `tick()`, not after.**
  `accept()` ticked the clock on entry. A `STATUS` arriving on schedule would then age
  out a set it has nothing to do with — a discard the frame caused while being, by
  rule, unable to cause one. The single-frame path now returns above the `tick()`
  call. `tick()` from the receive loop is what expires sets, which is what its
  contract already said.

Three tests: `test_single_frame_does_not_disturb_live_set` (the round trip — the
single frame is delivered in full, the set completes to the same bytes, no counter
moves), `test_single_frame_does_not_expire_live_set` (the half above), and G2's
inversion.

### G2 — `rx_reassembly_abandoned` loses one of its callers

`test_single_frame_displacing_live_set_is_counted` asserted the counter moved; it is
now `test_single_frame_does_not_abandon_live_set` and asserts it does not. Inverted in
place rather than deleted, per the task: a test that flips is easier to miss than a
test that is missing.

The counter now has exactly one caller — one set displacing another with no slot free
— which restores what it was for. `rx_reassembly_abandoned` means the receiver is
undersized or a peer is interleaving sets; `rx_reassembly_timeout` means the RF path
dropped a fragment. A node's periodic status frame is neither, and while it could
raise the first, the counter could not be read as either.

### G3 — `Status` identifiers follow the wire code

`CtxMismatch` → `RejectedCtx`, `BadMac` → `RejectedMac`, and the rest of the enum
walked against §14.1's new third column. The rest was already converged; these two
were the drift, and they now agree with `AckResult::RejectedCtx` / `RejectedMac` — the
values a node actually puts on the wire in answer — as well as with the counters.

**`ErrCode::CtxMismatch` stays as it is.** §8.8's `err_code` 0x04 *is* spelled
`CTX_MISMATCH`; §14.1's third column names the stage 9 `COMMAND_ACK` result, which is
`REJECTED_CTX`. Two wire vocabularies, two frames, and each identifier follows its
own. Converging them would be the same error in the other direction.

`test_status_identifiers_follow_the_wire_code` spells §14.1 out independently, in the
same shape as C1's registry test. It checks the rule as a *transformation* rather than
as a second list: the wire code (or, where §14.1's column reads "—" or one code covers
several conditions, the counter) is mechanically converted to PascalCase and compared
against `to_string`. A name that drifts fails without anyone having to notice it
drifted. Verified by mutation — reverting `to_string(RejectedCtx)` to `"CtxMismatch"`
fails it. It also closes both ways: every registry counter is either mapped from a
`Status` or listed among the four that deliberately have none.

Where a wire code covers three conditions — `BAD_LENGTH` is `BadFrag`, `BadLength` and
`NotFragmentable` — the identifier follows the **counter**, because the counter is the
diagnosis and the three have three different fixes. §14.1's rule is a SHOULD about
vocabulary, not an instruction to collapse distinctions the registry keeps.

### G4 — vectors regenerated

72 vectors (71 + 1). `check.py`, `embed.py` and `test_vectors` pass on host; the target
suites build clean.

**No existing frame byte changed.** Checked mechanically rather than by reading the
diff: every `frame`, `frames`, `payload`, `fragment_lens` and `node_key` in all four
files compared against `main` — 65 vectors' frame bytes, zero changed, one added. That
is the independent confirmation that v0.6 altered no header field, authentication scope
or schema layout, and it is the same result the v0.5 regeneration produced.

The new vector, `config_ack_21_results_single_frame_interposed`, is **derived, not
adjudicated** — the first materially new derived vector since W4 closed, which is worth
having after a round where most of the new material was handed over. It needed a shape
extension: the `frag` group delivers fragments of one set by index, and the frame under
test here is *not a fragment of anything*, so `interpose` carries a complete frame of
its own — header, payload, frame bytes, `after` position. It is a fragmented
`CONFIG_ACK` from GateLink with a health `STATUS` from the same node landing between
its two fragments, which is the bridge case §11.2's v0.6 note describes.

Two assertions make it bite, and both were verified by mutation:

- The C++ consumer now treats **a frag vector naming no counter as asserting that no
  counter moved at all**, rather than asserting nothing. Reinstating v0.5's behaviour
  in `Reassembler::accept` fails the vector on `rx_reassembly_abandoned`.
- `check.py` re-derives the interposed frame from its own declared header and payload,
  runs it through the receive ladder, and refuses it if it shares the set's key, if it
  is one of the fragments, if its `frag` total is not 1, if `after` falls outside the
  set, or if the vector names a counter. Corrupting a byte of the stored frame fails
  it.

The two negative vectors carrying a decode-outcome name were renamed with G3
(`CtxMismatch` → `RejectedCtx`, `BadMac` → `RejectedMac`); `check.py`'s own ladder
raises the new names. `tools/vectors/README.md` documents `interpose`, and its
"Counter names" paragraph — which proposed `rx_ctx_mismatch` / `rx_bad_mac` and asked
that §14 name them — is rewritten to record that §14.1 now does, and settled on
`rx_rejected_ctx` / `rx_rejected_mac` instead.

### G5 — W13: unit-test coverage is the answer

**Decision: accept `test_frag`'s coverage as sufficient; no raw-frames vector form.**
Written into W13's note in §18, which now reads closed.

The reasoning, since the decision is the deliverable. §11.2's dead-space clause — a
duplicate fragment of differing length leaving the superseded copy in staging — is
reachable only from a non-conforming sender, because §11.1 fixes every non-final
fragment to one length. W4's generator emits conforming senders by construction. That
makes it a **boundary of the method rather than a gap in it**: the vector shape
witnesses two independent implementations of a conforming sender against each other,
and a frame no conforming sender emits has no second implementation to be witnessed
against. A raw-frames form would compare the codec against a frame someone wrote by
hand — which is a unit test with a JSON file in front of it, and most of what makes W4
worth having is that it is not that.

`test_duplicate_fragment_of_different_length_overwrites` covers both halves today: the
new bytes win, and the dead copy is charged against staging rather than against the
reassembly cap (charging it against the cap would reject a set that fits — the reason
`set_len_` is tracked separately from `stage_used_` at all).

Revisit if a second non-conforming-sender clause appears. One such clause is a unit
test; several are a vector form, and at that point the tooling has something to
amortise against.

### Footprint — ESP32-S3, v0.6 delta

| | v0.5 | v0.6 | Δ |
|---|---:|---:|---:|
| `Reassembler` | 520 B | **520 B** | — |
| `Counters` | 96 B | **96 B** | — |
| bridge, 5 peers | 2,600 B | **2,600 B** | — |
| test firmware RAM | 18,544 B | **18,544 B** | — |
| test firmware flash | — | — | **+1,468 B** |

Type sizes are the board's own report from `test_report_footprint`, and agree with a
probe TU compiled against both trees with `xtensa-esp32s3-elf-g++` — which is how the
v0.5 side of the column was obtained without rebuilding it. The firmware figures are
`xtensa-esp32s3-elf-size` on the `test_vectors` ELF built from each tree.
**G1 cost nothing**, which is what reusing the delivery buffer instead of adding a
second one was for; §11.3's 520 B / 2,600 B figures stand as written. The flash
growth is the 72nd vector's frames and payloads, and nothing else.

The flash delta is a like-for-like comparison of two ELFs, *not* comparable to the
286,997 B the v0.5 entry quotes — that is PlatformIO's own reported figure, which
sums a different set of sections. Compare deltas here, not absolutes across entries.

### Result

**107 tests under `native`**, up from 104, and **110 on the Heltec V3**, up from 107:
G1's two, G2's inversion in place, and G3's mapping test. The +3 gap between host and
target is unchanged from v0.5 and is the three `#ifdef ARDUINO` tests in `test_crypto`
that compare mbedTLS against the portable reference — target-only by construction, not
a divergence. **Zero host/target divergence**, as at every round so far. Clean under `-Wall
-Wextra`, with `-Werror` on `native`. 72 W4 vectors pass on host and on target.

Worth stating since G3 touched it: the vectors carry decode-outcome names as strings,
so the rename had to move in the JSON, in `check.py`'s ladder and in the C++
consumer's name table together. Any one of the three left behind fails as an unknown
status name rather than as a silent pass — checked, not assumed.

### Open

- **W12** — §9.4 steps 4–6 still have no home. Unchanged by v0.6, and the largest open
  item. It wants settling before the second firmware is written, not after.
- **W9** — needs the second board *and* an SX1262 driver, both arriving with the range
  test firmware. Nothing built so far has touched the radio.
