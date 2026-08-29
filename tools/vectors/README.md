# LRAN protocol test vectors — W4

**Binding specification:** `LRAN-Protocol-Specification` v0.4 (`ver = 2`)
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
pio test -d lib/lran-protocol -e native      # the C++ side consumes them
```

A protocol change means: regenerate, **read the diff**, and confirm every changed byte
is a change you intended. A vector file diff that nobody read is a rubber stamp.

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
  "spec": "LRAN-Protocol-Specification v0.4",
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

`counter` names the **single** counter that must increment, and the C++ consumer
asserts that every other counter stayed put. That is the assertion that keeps two
different faults from quietly sharing one bucket — the reason v0.4 split `rx_oversize`
out of `rx_bad_length` and `rx_reassembly_abandoned` out of `rx_reassembly_timeout` in
the first place.

`stage` is the §14 row, for the human reading a failure.

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

**`negative`** — each naming the counter that must move.
- Bad application CRC16 · unknown `ver` · wrong `dst` · `frag = 0x00` ·
  index ≥ total · oversize frame · payload length mismatch ·
  invalid `(type, schema)` pair · corrupt MAC · fragmented `HEX_REQ`.
