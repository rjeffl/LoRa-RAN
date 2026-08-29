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
