# LRAN protocol test vectors — W4

**Binding specification:** `LRAN-Protocol-Specification` v0.14 (`ver = 2`)
**Vectors last regenerated against:** v0.14, on 2026-09-24. **D59 added an enumeration
value**, `event_type` `0x0B` `PHY_REVERTED`, and one vector, `event_phy_reverted`, covers
it. Every vector committed before it kept its bytes; only each file's `spec` field changed.
**v0.13's regeneration**, on 2026-09-23, was for D58's enumeration value, `cmd` `0x12`
`ROLL_CONTEXT`, which triggers §13.2's regeneration requirement, and three vectors cover
it. Every vector committed before it kept its bytes; only each file's
`spec` field changed. v0.7 through v0.12 changed no frame layout, header field,
enumeration value, schema or authentication scope, so the vectors were not regenerated
between v0.6 and v0.13. **v0.12 adds a counter** (`rx_unknown_src`, §14 stage 9a) and the
vectors are unaffected: §14.1 is enforced by the generator and the checker through the
registry's names, and no vector carries a counter value.
**Consumed by:** `/lib/lran-protocol/test/test_vectors/` (C++, Unity, `native`)
**Produced by:** `generate.py` (Python 3, this directory)

---

## Why this directory exists

Until these vectors existed, the codec had been checked **against itself** and against
published known-answer vectors for CRC-16/CCITT-FALSE, SHA-256, HMAC-SHA256 and
HKDF-SHA256. Those KATs witness the primitives. **Nothing witnessed the framing.**

Two implementations can each be internally consistent, each pass their own tests, and
still disagree about what a `CONFIG` fragment looks like on the air. The failure mode
is not a crash: it is a payload that reassembles into the wrong bytes with a valid
CRC16 on every frame, between two implementations that both believe they are
conformant. GateLink has no OTA and sits 500 ft from the house.

These vectors are the second witness. They gate simnode B0 and they gate the bridge
and node firmwares being developed independently.

## The rule that makes them worth anything

> **`generate.py` MUST share no code with the C++ codec — including the CRC
> implementation and the serializer — and MUST be written from the specification
> text, not from the codec.**

A generator that reuses the code under test witnesses nothing. A generator written by
reading the codec witnesses almost nothing: it reproduces the codec's reading of the
spec, including any misreading. Where the two disagree, **the disagreement is the
product** — resolve it against the spec text and record it in
`/docs/protocol-lib/engineering-log.md` with the section that settled it.

Python's `hashlib` and `hmac` are fine: they are independent implementations of
published primitives. The CRC-16, the header serializer, the fragmenter and the
payload builders must all be written here from scratch.

## Regenerating

§13.2 requires vectors to be regenerated on every protocol change. That is why this
file exists rather than the format living only in the generator.

```bash
python3 tools/vectors/generate.py            # rewrites the .json files in place
python3 tools/vectors/check.py               # self-check: re-derives and compares
python3 tools/vectors/embed.py               # copies them into the C++ suite's header
pio test -d lib/lran-protocol -e native      # the C++ side consumes them
```

A protocol change means: regenerate, **read the diff**, and confirm every changed byte
is a change you intended. **Skipping `embed.py` leaves the C++ suite testing the old
set.** The D57 vectors went unembedded from 2026-09-20 to 2026-09-23, and one of them had
found a codec defect in that time. A vector file diff that nobody read is a rubber stamp.

---

## File layout

One file per group. All are UTF-8 JSON, 2-space indented, with object keys in the
order given below so a regeneration diff stays readable.

| File | Group | Covers |
|---|---|---|
| `vectors_kdf.json` | `kdf` | §9.1 key derivation. **Generate and verify this one first.** |
| `vectors_single.json` | `single` | §6, §7, §19 — one frame per message type |
| `vectors_frag.json` | `frag` | §11 — fragmentation and reassembly |
| `vectors_negative.json` | `negative` | §14 — every discard stage, each naming its counter |
| `test_master_key.json` | — | the fixed `TEST-ONLY` master key (not generated) |

### Conventions

- **All byte strings are lowercase hex**, no `0x`, no separators, even length. The
  empty string `""` is a zero-length payload and is distinct from `null`.
- **All numeric header fields are JSON numbers in decimal**, host-order integers, not
  hex — `seq` is `4660`, not `"0x1234"`. Single-byte bitfields (`frag`, `schema`,
  `hdr_flags`, `ping_flags`) are the exception and are hex strings like `"0x01"`,
  because they are read as bit patterns and a decimal `241` for `frag` helps nobody.
- **`type` is the spec §6 name**, `"POLL"`, `"COMMAND_ACK"`, `"HEX_REQ"` — not the
  numeric value. The name-to-value mapping is itself part of what is being witnessed,
  so writing the number in the vector would hide a disagreement about it.
- **`key`** identifies which derived key signs and verifies the frame:
  `"node:0x01"` means the key HKDF-derived for node `0x01` from the fixed test master
  key (§9.1). `null` means the frame carries no MAC.
- **`spec_ref`** is required on every vector and names the section that fixes the
  expected value. A vector nobody can trace back to a section cannot be adjudicated
  when it fails.

---

## Common envelope

Every generated file has this shape:

```json
{
  "format": "lran-test-vectors/1",
  "spec": "LRAN-Protocol-Specification v0.6",
  "wire_ver": 2,
  "group": "single",
  "generated_by": "tools/vectors/generate.py",
  "vectors": [ ... ]
}
```

`format` is bumped only when the *vector schema* changes, which is independent of the
protocol `ver`. The C++ consumer refuses a `format` it does not know rather than
guessing at a field that moved.

---

## Vector shapes

### `kdf` — §9.1

```json
{
  "name": "node_key_gatelink",
  "spec_ref": "§9.1",
  "node_id": "0x01",
  "salt": "6c72616e2d7631",
  "info": "6e6f64652d01",
  "master_key": "4c52...4f54",
  "node_key": "<32 bytes, hex>"
}
```

`info` is carried explicitly and byte for byte because getting it wrong is
**undetectable by inspection**: the two sides derive different keys, every
authenticated frame fails its MAC, and no counter points at key derivation. It is
`"node-" || <raw address byte>` — six bytes — and it is **not** `"node-1"`, not
`"node-01"`, not `"node-0x01"`.

### `single` — one complete frame

```json
{
  "name": "poll_full_status",
  "spec_ref": "§6.4, §19",
  "note": "optional prose, one line",
  "header": {
    "ver": 2, "type": "POLL", "src": 0, "dst": 1,
    "seq": 4660, "ctx_id": 2309737967,
    "frag": "0x01", "schema": "0x00", "hdr_flags": "0x00"
  },
  "payload": "01",
  "key": null,
  "frame": "<complete frame bytes, hex>",
  "frame_len": 19,
  "decode": {
    "self": 1,
    "expect_ctx_id": 0,
    "status": "Ok",
    "payload": "01",
    "mac_present": false
  }
}
```

### `decode_only` — a frame no conforming encoder emits

Some receive-side rules can only be witnessed with a frame a **conforming sender would
never produce**. §5.8 makes `hdr_flags` bits 6:0 "write `0`, ignore on receive": a
receiver must accept them set, and an encoder must never set them. Both halves are
normative and they cannot be exercised by the same encode-and-compare vector.

Add `"decode_only": true` to such a vector. The C++ consumer skips the encode
comparison and checks only the decode outcome. Use it **only** where the spec makes
the sender rule and the receiver rule deliberately asymmetric — never to paper over a
frame the generator got wrong.

```json
{
  "name": "poll_reserved_hdr_flags_ignored",
  "spec_ref": "§5.8, §4.3",
  "decode_only": true,
  "note": "hdr_flags bit 6 set. §5.8 requires a sender write 0 here, so no encoder emits this frame; a receiver must ignore it.",
  "header": { "...": "..." },
  "payload": "01",
  "key": null,
  "frame": "...",
  "frame_len": 19,
  "decode": { "...": "..." }
}
```

`frame_len` is redundant against `frame` and is present on purpose: §19's length table
is exactly where v0.3's `HEX_RSP` errata lived, and an explicit length makes an
off-by-one fail as a length mismatch rather than as an opaque byte diff.

`decode.self` is the receiver's own node id, so the §14 stage 5 addressing check is
exercised rather than assumed. `decode.expect_ctx_id` of `0` means **skip** the §9.4
step 2 check — the bridge's position — and never "expect zero" (§9.4, §5.5).

### `frag` — a set of frames

```json
{
  "name": "ping_chunk14_fifteen_fragments",
  "spec_ref": "§6.6.2, §11.1",
  "header": { ...as above, `frag` is IGNORED and computed... },
  "payload": "<the COMPLETE pre-fragmentation payload, hex>",
  "frag_chunk": 14,
  "key": null,
  "frames": ["<fragment 0>", "<fragment 1>", "..."],
  "fragment_lens": [14, 14, "...", 6],
  "decode": {
    "self": 1,
    "expect_ctx_id": 0,
    "delivery_order": [0, 1, 2, "..."],
    "status": "Ok",
    "reassembled": "<hex, MUST equal `payload`>"
  }
}
```

- `frag_chunk` is a **local sender parameter and appears nowhere on the wire** (§11.1,
  §6.6.2). A receiver cannot distinguish a bench-driven split from a necessary one,
  which is exactly what makes it a valid test.
- `fragment_lens` pins §11.1's uniform-chunk rule explicitly: every entry but the last
  is equal, the last is the remainder, and the last is never longer and never empty
  unless the whole payload is.
- `delivery_order` lists fragment **indices in arrival order**, so one payload can be
  reused for in-order, reversed, shuffled and duplicate-index cases. A repeated index
  is a §11.2 duplicate and must land on `rx_frag_duplicate` without changing
  `reassembled`.

#### `interpose` — a single-frame frame delivered into a live set

Optional, and the one thing in a `frag` vector that is **not** a fragment of the set.
§11.2 says a frame declaring `frag` total 1 may not begin, join, displace or expire a
set, even one it shares `(src, ctx_id, schema)` with, and must still be delivered
whole. Witnessing that needs a complete second frame, so `interpose` carries its own
header, payload and frame bytes rather than an index into `frames`:

```json
"interpose": {
  "after": 1,
  "spec_ref": "§11.2",
  "note": "A health STATUS from the same node, mid-set.",
  "header": { "...": "...", "frag": "0x01" },
  "payload": "<hex>",
  "key": null,
  "frame": "<complete frame bytes, hex>",
  "frame_len": 38,
  "decode": { "status": "Ok", "payload": "<hex>" }
}
```

`after` is a position in `delivery_order`: the frame arrives once that many
fragments have been delivered, and must land **inside** the set — after at least one
fragment and no later than the fragment that completes it. Past completion it would
witness §11.2's retained-key rule instead, which is a different one.

Its key must differ from the set's, or the case under test is a duplicate fragment.
The vector names **no** counter: nothing here is a fault, and the consumer asserts
every counter stayed at zero. That assertion is the vector — before v0.6 a single
frame arriving mid-set displaced the set and moved `rx_reassembly_abandoned`, which
on a bridge is a node's periodic `STATUS` destroying that node's in-flight
`CONFIG_ACK`.

### `negative` — a discard, and the counter that must move

```json
{
  "name": "frag_total_zero",
  "spec_ref": "§5.6, §14 stage 5b",
  "frame": "<the malformed frame bytes, hex>",
  "decode": {
    "self": 1,
    "expect_ctx_id": 0,
    "status": "BadFrag",
    "counter": "rx_bad_frag",
    "stage": "5b"
  }
}
```

`status` is the decode outcome by the name the consuming implementation uses for it.
Since v0.6 those follow §14.1's wire-code column — `RejectedCtx`, not `CtxMismatch`;
`RejectedMac`, not `BadMac` — which is the same convergence §14.1 asks of the code.

`counter` names the **single** counter that must increment, and the C++ consumer
asserts that every other counter stayed put. That is the assertion that keeps two
different faults from quietly sharing one bucket — the reason v0.4 split `rx_oversize`
out of `rx_bad_length` and `rx_reassembly_abandoned` out of `rx_reassembly_timeout` in
the first place.

`stage` is the §14 row, for the human reading a failure.

**Every counter is `rx_*`.** §11.3 names `rx_reassembly_timeout` explicitly, and the
codec's `reassembly_timeout` / `fragment_overflow` were renamed to match — a naming
inconsistency the generator caught. `rx_fragment_overflow` is a discard and IS summed
into `rx_dropped`; `rx_frag_duplicate` counts an overwrite and is not (§14).

**Counter names.** §14.1 is now the registry and names every counter for every
stage, so a vector's `counter` traces to the specification rather than to a
convention held in two places. It exists because of this paragraph: through v0.4 §14
named counters for stages 1–5b and nothing for stages 6 through 9, these vectors and
the C++ consumer agreed on names the specification did not state, and two of them —
`rx_reassembly_timeout` and `rx_fragment_overflow` — had shipped in code without the
`rx_` prefix. The two stage 9 names settled as `rx_rejected_ctx` and
`rx_rejected_mac`, after §9.4's wire codes rather than after the `rx_ctx_mismatch` /
`rx_bad_mac` this file used to propose. `generate.py` and `check.py` each hold their
own copy of §14.1 and assert against it.

A negative vector is built by constructing a **well-formed frame and then breaking
exactly one thing**, repairing the CRC16 unless the CRC is what is under test. A frame
that is malformed in two ways proves nothing about which stage caught it.

> **Trap, learned the hard way.** When corrupting a field to force a failure, check
> the corrupted value differs from the original. A "forged" frame that happens to
> reproduce a valid one verifies legitimately and passes as a false negative.

---

## Required coverage

Minimum. Add more freely.

**`kdf`** — do this group first and confirm it before generating anything else.
Everything downstream is meaningless if key derivation is wrong.
- The `info` byte string for GateLink: `6e 6f 64 65 2d 01`.
- A derived key for each of `0x01`, `0x02` and `0xF0`.

**`single`** — one per message type at minimum length, and where variable, at maximum.
- The 222-byte maximum `PING` (§6.6.1) — `LRAN_MAX_FRAME` exactly.
- Both `HEX_REQ` sizes: `20 + N` plain and `28 + N` write-class authenticated (§19).
- `HEX_RSP` at `20 + N` (§19 — the v0.3 errata).
- `STATUS` `0x10` (96 B), `STATUS` `0xF0` (38 B), `EVENT` `0x11` (34 B),
  `COMMAND` (30 B), `COMMAND_ACK` (24 B), `POLL` (19 B), `ERROR` (22 B).

**`frag`**
- A `PING` with `frag_chunk = 14` producing the full 15-fragment set (§6.6.2).
- An authenticated multi-fragment `CONFIG` — each fragment carries its own MAC.
- The same set delivered reversed and shuffled, reassembling to identical bytes.
- A set with a duplicated index.
- A set with a single-frame frame interposed mid-delivery (§11.2), reassembling to
  identical bytes with no counter moved.

**`negative`** — each naming the counter that must move.
- Bad application CRC16 · unknown `ver` · wrong `dst` · `frag = 0x00` ·
  index ≥ total · oversize frame · payload length mismatch ·
  invalid `(type, schema)` pair · corrupt MAC · fragmented `HEX_REQ`.
