# LRAN Protocol Specification

**Document:** `LRAN-Protocol-Specification`
**Version:** 0.3
**Protocol version on the wire:** `ver = 2`
**Status:** Authoritative for `/lib/lran-protocol/`. Blocks all node firmware.
**Supersedes:** `lora-gatelink-wire-format-v0.1`
**Parent document:** [`LRAN-System-PRD`](./LRAN-System-PRD.md)
**Last updated:** 2026-08-19

> **Every LRAN node PRD and implementation plan references this document.** No node
> document may redefine a frame layout, an enumeration value, a schema ID or an MQTT
> topic. Where a node needs a new field, the change is made here and the version is
> bumped (§13).

---

## Table of contents

1. [Scope](#1-scope)
2. [Layer model](#2-layer-model)
3. [Frame structure](#3-frame-structure)
4. [Serialization rules](#4-serialization-rules)
5. [Header fields](#5-header-fields)
6. [Message types](#6-message-types)
7. [Payload schemas](#7-payload-schemas)
8. [Enumerations](#8-enumerations)
9. [Authentication and keying](#9-authentication-and-keying)
10. [Sequencing, context and replay](#10-sequencing-context-and-replay)
11. [Fragmentation](#11-fragmentation)
12. [Radio configuration and media access](#12-radio-configuration-and-media-access)
13. [Version tolerance and versioning policy](#13-version-tolerance-and-versioning-policy)
14. [Receive-path handling](#14-receive-path-handling)
15. [Airtime and power analysis](#15-airtime-and-power-analysis)
16. [MQTT interface](#16-mqtt-interface)
17. [Reserved and future features](#17-reserved-and-future-features)
18. [Open items](#18-open-items)
19. [Reference layout summary](#19-reference-layout-summary)
20. [Changelog](#20-changelog)

---

## 1. Scope

Defines the byte-level format of every frame exchanged over the LRAN LoRa link
between the bridge and any node, and the MQTT topic grammar the bridge presents to
Home Assistant. This document is authoritative for `/lib/lran-protocol/`; every
firmware target and all bench tooling link one implementation of it.

**In scope:** LoRa frame header, payloads, schemas, enumerations, authentication,
sequencing, fragmentation, radio configuration, media access, MQTT topic structure
and retention rules.

**Out of scope:** the VE.Direct text and HEX protocols themselves (Victron-owned —
LRAN transports HEX verbatim, §7.6), the TDT BLE BMS protocol
(`/docs/bms-protocol.md`), 1050 accessory-I/O semantics
([`LRAN-GateLink_Node-PRD`](./LRAN-GateLink_Node-PRD.md)), and HA entity definitions
(node PRDs).

### 1.1 Design constraints

| Constraint | Source | Consequence |
|---|---|---|
| Fixed binary, not JSON | System PRD §3.2 | JSON exists only on the bridge→MQTT hop |
| Multiple independent remote nodes on one channel | System PRD §2.1 | 8-bit addressing, per-node keys, CAD media access (§12.3) |
| Remote nodes have no OTA | System PRD §2.2 | Version tolerance N/N−1 (§13.1); explicit schema IDs (§7.1) |
| Basic authentication only, no confidentiality | System PRD §2.2 | 64-bit truncated MAC on command-class frames; accepted limits in §9.5 |
| No cross-reboot sequence persistence | System PRD §2.2 | Context-ID resync scheme (§10) |
| Remote nodes may be power-constrained | GateLink PRD §6, WellLink PRD | Short frames; optional asymmetric preamble (§17.1) |

---

## 2. Layer model

```
+-----------------------------------------------------------+
|  Application (gate FSM, MPPT cache, BMS client, HA)       |
+-----------------------------------------------------------+
|  LRAN frame   <-- THIS DOCUMENT                           |
|  header | payload | MAC | CRC16                           |
+-----------------------------------------------------------+
|  LoRa PHY (SX1262)                                        |
|  preamble | explicit header | payload | hardware CRC      |
+-----------------------------------------------------------+
```

**The PHY provides, and this layer therefore does not duplicate:**

- Payload length (LoRa explicit header mode)
- Preamble detection and sync-word filtering
- Optional hardware node-address filtering (§12.1)

### 2.1 Integrity — two layers, deliberately

Both a **hardware CRC** and an **application CRC16** are required.

> **Reversal from wire-format v0.1.** v0.1 defined no application CRC, on the
> grounds that the SX1262's hardware CRC already covers the whole frame and a second
> check costs 2 bytes and ~6 ms at SF9. Three things changed. **Fragmentation**
> (§11) means a reassembled payload is an assembly of separately-CRC'd pieces, and
> nothing checks the assembly. **Non-radio paths** — internal loopback (§17.3),
> microSD log replay, and the committed test vectors — carry frames that never touch
> a PHY CRC, and a spec whose only integrity check lives in a peripheral cannot be
> unit-tested. And v0.1's own §11.2 concluded that **byte count is not a constraint
> in this system**, which retires the cost argument it was built on.

> **Mandatory radio configuration.** Every node MUST configure the SX1262 with
> **explicit header mode** and **CRC enabled**, and MUST discard any frame whose CRC
> status indicates an error before the frame reaches the parser. This requirement
> belongs in the radio init path with a comment pointing here.

The application CRC16 is **CRC-16/CCITT-FALSE** (poly `0x1021`, init `0xFFFF`, no
reflection, no final XOR), computed over `header || payload || mac` and appended
little-endian. It is not covered by the MAC — it is a transport check, not a
security control.

---

## 3. Frame structure

```
        0        1        2        3        4        5        6        7
    +--------+--------+--------+--------+--------+--------+--------+--------+
  0 |  ver   |  type  |  src   |  dst   |      seq        |    ctx_id ...   |
    +--------+--------+--------+--------+--------+--------+--------+--------+
  8 |   ... ctx_id    |  frag  | schema |hdr_flag|         rsv (3 B)        |
    +--------+--------+--------+--------+--------+--------+--------+--------+
 16 |                                                                       |
    :                       payload  (0..204 bytes, §3.1)                   :
    |                                                                       |
    +-----------------------------------------------------------------------+
    |            mac (8 B, authenticated types only)                        |
    +-----------------------------------------------------------------------+
    |     crc16 (2 B, always)     |
    +-----------------------------+
```

| Region | Size | Present |
|---|---|---|
| Header | **16 B** | always |
| Payload | 0–204 B | per message type (§6), subject to §3.1 |
| MAC | **8 B** | authenticated types only (§9.2) |
| CRC16 | 2 B | always |

**Maximum frame size: 222 bytes.** The SX126x PHY allows 255; the cap leaves
headroom and makes buffer sizing static. Any frame exceeding it is a programming
error, not a runtime condition.

Overhead is **18 bytes** unauthenticated, **26 bytes** authenticated.

### 3.1 Payload caps are derived, not independently declared

One constant is normative — `LRAN_MAX_FRAME = 222`. Every payload cap is computed
from it, in one header, and nowhere else:

```
LRAN_MAX_FRAME        222              // the only hand-chosen number
LRAN_HDR_LEN           16              // §5
LRAN_CRC_LEN            2              // §2.1
LRAN_MAC_LEN            8              // §9.3

LRAN_MAX_PAYLOAD_AUTH  196   = 222 - 16 - 8 - 2
LRAN_MAX_PAYLOAD_PLAIN 204   = 222 - 16 - 2
LRAN_MAX_SCHEMA_PAYLOAD 196  = LRAN_MAX_PAYLOAD_AUTH
```

**`LRAN_MAX_SCHEMA_PAYLOAD` governs every schema-bearing and every fragmented
payload**, authenticated or not, and is the value §11 checks a reassembled set
against. Holding schema payloads to the authenticated cap means a schema can later
gain a MAC without any schema exceeding a frame — a `ver`-free change (§13.2) that a
split cap would silently forbid.

**`LRAN_MAX_PAYLOAD_PLAIN` is available only to unauthenticated, schema-free types**
— in practice `PING` (§6.6) and `HEX_REQ`/`HEX_RSP` reads. This is what lets a `PING`
produce an exactly-222-byte frame and so exercise the largest buffer the system will
ever handle.

> **Why derive rather than declare.** v0.2 stated a flat 200-byte payload cap
> alongside a 222-byte frame cap, and the two did not reconcile once the header grew
> — a 200-byte payload in a 12-byte header left 8 bytes unexplained, and a reader had
> no way to tell which number was normative. Deriving every cap from `LRAN_MAX_FRAME`
> makes the header growth in this revision a one-line change instead of an audit.

---

## 4. Serialization rules

1. **Little-endian** for all multi-byte integers, matching ESP32-S3 native order.
2. **Explicit byte-wise serialization. Do NOT `memcpy` a struct.** Structs are
   in-memory representations only; `serialize()` / `deserialize()` write and read
   fields individually.

   > This is not pedantry. `/tools/` contains host-side simulators and decoders
   > built with a different compiler on a different architecture. Struct-layout-
   > dependent code works on both ESP32s and breaks the moment the bench tooling is
   > written.

3. **Reserved fields and bits MUST be written as zero and MUST be ignored on
   receive.** Not validated as zero — that would break forward compatibility within
   a version (§13.2). The single exception is `hdr_flags` bit 7, which is a
   *must-understand* marker and is validated (§5.8).
4. **Strict length validation.** For a given `(ver, type, schema)` the payload
   length is fixed, except `PING`, `HEX_REQ`/`HEX_RSP` and `CONFIG`/`CONFIG_ACK`,
   which are explicitly variable and carry their own length fields. A frame whose
   payload length does not match the expected value is discarded and counted, not
   parsed.
5. **Signed integers are two's complement.** Temperatures in tenths of a degree
   Celsius use `int16`; per-cell temperatures use `int8` in whole degrees.
6. **Sentinels, not zero, for "not available."** `INT16_MIN` and `UINT16_MAX` mark
   unavailable numeric fields. A consumer must be able to distinguish "0 A" from "no
   reading," which is exactly the failure the `vedirect_stale` flag exists to prevent
   (§7.3.3).

---

## 5. Header fields

### 5.1 `ver` — byte 0, `uint8`

Protocol version. **`2`** for this document.

A frame with an unrecognized `ver` MUST be discarded and counted. A node MUST NOT
attempt best-effort parsing of an unknown version. The bridge is the sole exception
and accepts `N` and `N−1` (§13.1).

> **Widened from 4 bits in v0.1.** v0.1 packed `ver` and `msg_type` into one byte's
> nibbles. A full byte each costs one byte, removes a 16-version ceiling on a fleet
> that will drift out of sync because remote nodes have no OTA, and makes a hex dump
> readable at a glance. §15.2's finding that byte count is not a constraint applies.

### 5.2 `type` — byte 1, `uint8`

Message type. See §6.

### 5.3 `src` / `dst` — bytes 2 and 3, `uint8` each

Node IDs, one byte each.

| ID | Node |
|---|---|
| `0x00` | `lran-bridge` (LoRaBridge) |
| `0x01` | `lran-gatelink` (GateLink) |
| `0x02` | `lran-welllink` (WellLink, reserved) |
| `0x03`–`0xEF` | Available for future nodes |
| `0xF0` | `lran-simnode-0` — bench |
| `0xF1` | `lran-simnode-1` — bench |
| `0xF2` | `lran-simnode-2` — bench |
| `0xF3` | `lran-simnode-3` — bench |
| `0xF4`–`0xFE` | Bench range, unassigned — reserved, not provisioned |
| `0xFF` | Broadcast |

A node MUST discard frames whose `dst` is neither its own ID nor `0xFF`.

**The four bench IDs are enumerated, not left to the range, because keys are
provisioned per ID.** §9.1 derives `node_key` from `node_id` and the bridge's
provisioning table is static. A range is not provisionable: the bridge cannot derive
a key for an address nobody has named. Four concrete IDs are more than the §12.3
CAD/backoff test needs and cost nothing beyond four table rows. `0xF4`–`0xFE` stay
reserved so the range can grow without touching the node-ID space.

**A simnode holds only its own derived key and therefore cannot forge a `COMMAND` to
GateLink.** This follows from §9.1 rather than from any check in the bridge, but it
is the property that makes running bench nodes against production infrastructure
acceptable, so it is stated here rather than left to be inferred.

**Bench nodes are never silently mixed into production data.** A production bridge
build accepts frames from `0xF0`–`0xFE` and publishes them, but only under the
diagnostic topic tree and only when explicitly enabled — see §16.6.

> **Changed from v0.8 PRD §9.2.** That draft had `simnode` register as node `0x02`,
> which is WellLink's reserved address — meaning the multi-node bench test and the
> second real node could never coexist, and a simnode left running would collide with
> WellLink the day it is commissioned. A dedicated bench range costs nothing and lets
> several simulated nodes run at once, which is what the CAD/backoff test (§12.3)
> actually needs.

> **Widened from 4 bits in v0.1.** v0.1 packed `src` and `dst` into one byte, on the
> reasoning that the system would only ever have two nodes. The system PRD retired
> that assumption; 254 usable addresses at a cost of one byte is the right trade.

### 5.4 `seq` — bytes 4–5, `uint16`

Per-source monotonic counter, wrapping modulo 2^16. Two independent sequence spaces
per node pair; see §10.2. Comparison MUST use serial-number arithmetic (RFC 1982
style), never a plain `>`, so that a wrap does not cause a node to reject every
subsequent command until reboot.

### 5.5 `ctx_id` — bytes 6–9, `uint32`

The **remote node's current boot context ID**, regardless of frame direction. A node
generates a random non-zero `uint32` at boot and uses it in every frame it sends; the
bridge learns it per node and echoes it back. `0x00000000` is reserved to mean
"unknown." See §10.1.

> **Widened from 16 to 32 bits.** Wire-format v0.1 §8.4 recorded the 16-bit width as
> a known limitation: a reboot had a ~1-in-65535 chance of reusing a value an
> attacker had previously captured, briefly reopening a replay window. It named
> widening to 32 bits as the obvious upgrade, priced at 2 bytes and ~6 ms at SF9.
> With airtime established as a non-constraint (§15.2) and the fleet growing, that
> upgrade is taken here.

> **Unifies `ctx_id` and `boot_id`.** PRD v0.8 §6.5.3 described the same mechanism
> under the name `boot_id`. They are one thing. `ctx_id` is the name on the wire and
> in code; `boot_id` is retired as a synonym.

### 5.6 `frag` — byte 10, `uint8`

High nibble = fragment index (0-based), low nibble = total fragment count (1-based).
`0x01` is a single unfragmented frame. See §11.

### 5.7 `schema` — byte 11, `uint8`

Payload schema ID and version, for `STATUS`, `EVENT`, `CONFIG` and `CONFIG_ACK`.
Written as `0x00` and ignored for all other types. See §7.1.

### 5.8 `hdr_flags` — byte 12, `uint8`

| Bit | Meaning |
|---|---|
| 6:0 | **Reserved — write `0`, ignore on receive.** Optional-semantics extension space |
| 7 | **`CRITICAL_EXT`** — this frame uses a header extension the sender requires the receiver to understand |

A receiver that reads `hdr_flags` bit 7 set, and does not implement the extension it
denotes, **MUST discard the frame and reply `ERROR(UNKNOWN_HDR_EXT)`** (§8.8). It
MUST NOT parse the frame on a best-effort basis. Bits 6:0 follow the ordinary
reserved rule (§4.3) and are ignored when unrecognized.

> **Why this bit has to exist now rather than later.** §13.2 permits assigning
> meaning to a reserved field with no `ver` bump, because §4.3 makes receivers ignore
> what they do not recognize. That is sound only for **optional** semantics. A future
> header field that a receiver must understand to act correctly — one that changes
> how the payload is interpreted, or that carries a constraint on execution — would
> be silently ignored by an older node, producing exactly the quiet misbehaviour
> §13.2 forbids in its closing paragraph.
>
> A must-understand marker converts that failure into a loud, counted, diagnosable
> discard. It cannot be added retroactively: an older node ignores the bit that says
> "do not ignore this," which is the one instruction it cannot afford to miss. On a
> fleet with no OTA, defining it in the first shipping version is the only moment it
> is free.

### 5.9 Reserved — bytes 13–15

Three bytes, written `0`, ignored on receive (§4.3). Extension space with no
assigned meaning.

> **Why 16 bytes, and why now.** §13.2 makes any header change a `ver` bump, and on a
> fleet with no OTA a `ver` bump is a flag day: every node is reflashed over USB, in
> place, in whatever weather. Four bytes bought before first flash cost ~7 ms of
> airtime on a `STATUS` frame at SF9 — noise against §15.2's finding — and buy the
> ability to add header semantics later without a physical visit to a node 500 ft
> from the house.
>
> Sixteen is chosen over the minimum for two secondary reasons. The payload begins at
> offset `0x10`, so a hex dump breaks on the row boundary and the payload column
> reads directly — the same argument that justified unpacking `ver` and `type` to
> full bytes in v0.2. And §13.2's preference for schema IDs over header changes only
> holds while header changes stay rare; reserved space is what keeps them rare.
>
> This is a header resize and therefore a `ver` bump under §13.2. It is taken under
> `ver = 2` rather than bumping to `3` because no firmware has been built against
> v0.2 and no frame has ever been transmitted — there is no deployed peer to be
> incompatible with. See §20.

---

## 6. Message types

| `type` | Name | Direction | Payload | MAC | Schema |
|---|---|---|---|---|---|
| `0x00` | *reserved* | — | — | — | — |
| `0x01` | `COMMAND` | bridge → node | 4 B | **yes** | — |
| `0x02` | `COMMAND_ACK` | node → bridge | 6 B | no | — |
| `0x03` | `POLL` | bridge → node | 1 B | no | — |
| `0x04` | `STATUS` | node → bridge | per schema | no | yes |
| `0x05` | `EVENT` | node → bridge | per schema | no | yes |
| `0x06` | `ERROR` | either | 4 B | no | — |
| `0x07` | `PING` | either | 2 + N B | no | — |
| `0x08` | `HEX_REQ` | bridge → node | 2 + N B | conditional | — |
| `0x09` | `HEX_RSP` | node → bridge | 3 + N B | no | — |
| `0x0A` | `CONFIG` | bridge → node | variable | **yes** | yes |
| `0x0B` | `CONFIG_ACK` | node → bridge | variable | no | yes |
| `0x0C`–`0xFF` | *reserved* | — | — | — | — |

### 6.1 `EVENT` is a distinct type — a reversal from v0.1

Wire-format v0.1 collapsed `EVENT` into `STATUS` via a `status_reason` field, on the
grounds that an event would carry substantially the same fields, producing two
schemas to version and an ambiguity about which HA should trust.

**v0.2 restores `EVENT` as a separate type**, for reasons that did not exist when
v0.1 was written:

- The held-open alert and the FIRE assertion drive **email and SMS**, and must be
  delivered as **non-retained** MQTT messages that fire exactly once (§16.3). A
  retained state snapshot replays on HA restart and on discovery refresh, which
  produces spurious 2 AM notifications. `STATUS` is inherently a retained-state
  message; the event is inherently not.
- The payload shapes genuinely differ. `EVENT` is a small record of *something that
  happened*, carrying an `event_id` for deduplication across the non-retained path.
  `STATUS` is a large snapshot of *what is currently true*. Forcing one shape to
  serve both means a 78-byte frame for a fire alarm.
- Latency. An `EVENT` is pushed immediately; a `STATUS` may wait for the poll.

`status_reason` is **retained** in the `STATUS` payload regardless (§7.3.4). It still
answers "why was this snapshot sent," which is a different question from "what
happened."

### 6.2 `COMMAND` — 4 bytes

| Offset | Type | Field |
|---|---|---|
| 0 | `uint8` | `cmd` — §8.1 |
| 1 | `uint8` | `arg` — command-specific; `0` when unused |
| 2 | `uint16` | `arg2` — command-specific; `0` when unused |

Followed by an 8-byte MAC (§9).

### 6.3 `COMMAND_ACK` — 6 bytes

| Offset | Type | Field |
|---|---|---|
| 0 | `uint16` | `ack_seq` — `seq` of the `COMMAND` being acknowledged |
| 2 | `uint8` | `result` — §8.2 |
| 3 | `uint8` | `detail` — result-specific; `0` when unused |
| 4 | `uint16` | `reserved` — write `0` |

The ACK reports **two distinct things** and callers must not conflate them:
`result = ACCEPTED` means the frame authenticated and the command was dispatched to
the local actuator. It does **not** mean the gate moved. Motion is confirmed only by
a subsequent `STATUS` with `status_reason = GATE_STATE_CHANGE`.

### 6.4 `POLL` — 1 byte

| Offset | Type | Field |
|---|---|---|
| 0 | `uint8` | `poll_flags` — bit 0: request full status; bit 1: request config readback; 7:2 reserved, write `0` |

### 6.5 `ERROR` — 4 bytes

| Offset | Type | Field |
|---|---|---|
| 0 | `uint8` | `err_code` — §8.8 |
| 1 | `uint8` | `detail` |
| 2 | `uint16` | `ref_seq` — `seq` of the offending frame, or `0` |

### 6.6 `PING` — 2 + N bytes

| Offset | Type | Field |
|---|---|---|
| 0 | `uint8` | `ping_flags` — bit 0 = `PATTERN_FILL` (§6.6.2); 7:1 reserved, write `0` |
| 1 | `uint8` | `n` — echo payload length, **0–202** |
| 2 | `uint8[n]` | Echo bytes, echoed verbatim by the responder |

Used for RF loopback (§17.3), link-margin testing and buffer-path validation. A
responder swaps `src`/`dst`, preserves `seq`, `ping_flags` and the echo bytes, and
retransmits.

#### 6.6.1 A `PING` may fill the frame

`n` is capped at `LRAN_MAX_PAYLOAD_PLAIN − 2 = 202` (§3.1), so a maximum `PING`
produces a frame of exactly **222 bytes** — `LRAN_MAX_FRAME`.

> **Widened from 64 in v0.2.** A 64-byte echo exercised roughly a third of the
> largest frame the system will ever build. The buffers, the CRC path, the SX1262
> FIFO write and the reassembly of a full-size frame are all sized for 222 bytes and
> were, until now, never exercised at that size by anything the protocol could
> actually emit — the largest real frame is a 96-byte `STATUS`. A full-size `PING` is
> the cheapest way to prove the worst case before a node is 500 ft away with no OTA,
> and it doubles as the realistic link-margin test: margin measured on a 19-byte
> `POLL` is not the margin the longest frame will see.

#### 6.6.2 `PING` is the fragmentation test vehicle

**`PING` is fragmentable.** With `PATTERN_FILL` set and `n` requested above the
single-frame cap by the bench tool, the initiator splits the echo across up to 15
fragments per §11; the responder reassembles, then re-fragments the echo on the way
back.

This is the only mechanism in the protocol that exercises reassembly over the air.
§11 concedes that no currently defined schema needs fragmentation — the largest is 78
bytes — which means the reassembly path would otherwise ship untested on hardware
that cannot be patched remotely. The condition under which it is first exercised must
not be the day a payload outgrows a frame in the field.

#### 6.6.3 `PATTERN_FILL`

When `ping_flags` bit 0 is set, the echo bytes are a deterministic pattern rather than
arbitrary data:

```
data[i] = (uint8)( (seq & 0xFF) + i )
```

The initiator regenerates the pattern from the `seq` it sent and compares
byte-by-byte, so a mismatch reports **which offset** diverged, not merely that one
did. The CRC16 (§2.1) already tells you a frame is corrupt; the pattern tells you
where, which is what distinguishes a marginal RF path from a fragment-reassembly or
buffer-indexing bug. Both are live risks the first time §6.6.2 is run.

With bit 0 clear, the echo bytes are arbitrary and compared verbatim, unchanged from
v0.2 behaviour.

### 6.7 `HEX_REQ` / `HEX_RSP`

VE.Direct HEX transport. The node is **transport only** and does not interpret
register semantics; see §7.6 for the payload and the authentication rule.

---

## 7. Payload schemas

### 7.1 Schema registry

| `schema` | Meaning | Length |
|---|---|---|
| `0x00` | Not applicable (types without a schema) | — |
| `0x10` | **GateLink status v1** | 78 B |
| `0x11` | **GateLink event v1** | 16 B |
| `0x12` | **GateLink config v1** | variable |
| `0x20` | WellLink status v1 — *reserved, not defined* | — |
| `0x21` | WellLink event v1 — *reserved, not defined* | — |
| `0xF0` | **Generic node health** — all nodes | 20 B |
| `0xFE` | **Simnode synthetic status** — bench only | 78 B (mirrors `0x10`) |

**Why explicit rather than inferred from `src`.** The bridge *could* look up node type
from its registry. Explicit schema IDs additionally survive **firmware version skew**
— a node running an older build announces schema `0x10` while the bridge already
understands a later one, and the bridge decodes it correctly instead of misparsing.
Given that remote nodes have no OTA and will drift out of sync, this is worth one
byte.

Schema definitions live in `/lib/lran-protocol/schemas/` and are the versioned
contract between node and bridge.

**Adding a field to a schema requires a new schema ID, not a `ver` bump**, provided
the frame header is unchanged. This is the mechanism that lets one node gain a
capability without reflashing every other node — see §13.

---

### 7.2 Schema `0x10` — GateLink status v1 (78 bytes)

#### 7.2.1 Gate block — offsets 0–9

| Off | Type | Field | Notes |
|---:|---|---|---|
| 0 | `uint8` | `gate_state` | §8.3 — derived from IN1/IN2 |
| 1 | `uint8` | `input_bits` | Debounced raw inputs. Bit 0 = IN1 (OPEN), 1 = IN2 (MOVING), 2 = IN3 (SAFETY), 3 = IN4 (EXIT), 4 = IN5 (FIRE), 5 = IN6 (ALARM), 7:6 reserved |
| 2 | `uint8` | `hold` | Bit 0 = `held_open`; bits 3:1 = `hold_source` (§8.4); bits 7:4 reserved |
| 3 | `uint8` | `movement_cause` | §8.5 |
| 4 | `uint8` | `last_direction` | §8.6 |
| 5 | `uint8` | `detect_flags` | §7.2.5 |
| 6 | `uint32` | `last_traversal_age_s` | Seconds since the last classified **vehicle** traversal. `UINT32_MAX` = none since boot or since the last persisted value was lost; saturates rather than wraps. See §7.2.9 |

`input_bits` is carried alongside the derived `gate_state` deliberately. It is the raw
evidence for every derivation the node performs, and it lets a state-machine bug be
diagnosed from a logged frame without a firmware change or a walk to the gate. It
serves the same purpose `gate_raw_state` served in wire-format v0.1, in a form that
suits discrete inputs rather than a bus status byte.

`last_traversal_age_s` is an **age, not a timestamp**. The node has an RTC but no
guaranteed time sync, and an age is correct on the bridge the moment it arrives
regardless of clock skew.

> **The block is no longer 8-byte aligned, and that is fine.** §4.2 forbids `memcpy`
> of a struct and requires field-by-field serialization, so no alignment constraint
> exists on the wire. Padding the block back to a power of two would cost two bytes
> to satisfy a requirement this document does not have.

#### 7.2.2 MPPT block — offsets 10–35

| Off | Type | Field | Unit | VE.Direct |
|---:|---|---|---|---|
| 10 | `uint16` | `batt_mv` | mV | `V` |
| 12 | `int16` | `batt_ma` | mA | `I` — negative = discharge |
| 14 | `uint16` | `pv_cv` | **10 mV** | `VPV`. 10 mV units because the 75/15 accepts up to 75 V and mV would overflow `uint16` at 65.5 V |
| 16 | `uint16` | `pv_w` | W | `PPV` |
| 18 | `int16` | `load_ma` | mA | `IL`. `INT16_MIN` = not available |
| 20 | `uint16` | `yield_today` | 10 Wh | `H20` |
| 22 | `uint16` | `yield_yest` | 10 Wh | `H22` |
| 24 | `uint16` | `pmax_today` | W | `H21` |
| 26 | `uint32` | `yield_total` | 10 Wh | `H19`. `uint32` — `uint16` overflows at 655 kWh, reachable in a few years |
| 30 | `uint8` | `charge_state` | — | `CS`, passed through |
| 31 | `uint8` | `mppt_err` | — | `ERR`, passed through. Non-zero triggers a push |
| 32 | `uint8` | `mppt_tracker` | — | `MPPT`, passed through |
| 33 | `uint8` | `mppt_flags` | — | §7.2.6 |
| 34 | `int16` | `mppt_temp_c10` | 0.1 °C | Second of three thermal reference points. `INT16_MIN` = not available |

VE.Direct code fields are **passed through unmodified rather than normalized.**
Victron owns those enumerations; re-mapping them here would create a translation table
to maintain in two places, and the bridge can map to text on the MQTT side where it is
cheap to change.

#### 7.2.3 BMS block — offsets 36–63

Populated from the TDT BLE client. Frame formats and the register decode live in
`/docs/bms-protocol.md`; this block is the transport representation only.

| Off | Type | Field | Unit | Notes |
|---:|---|---|---|---|
| 36 | `uint8` | `bms_soc` | % | `0xFF` = not available |
| 37 | `uint8` | `bms_flags` | — | §7.2.7 |
| 38 | `uint16` | `pack_mv` | mV | |
| 40 | `int16` | `pack_ma` | mA | Sign convention pending — see §18, W6 |
| 42 | `uint8` | `cell_count` | — | Cells actually reported. `>4` requires a new schema ID |
| 43 | `uint8` | `bms_rssi_neg` | −dBm | Magnitude of the BLE RSSI, e.g. `80` means −80 dBm. `0` = no link. Ongoing evidence for GateLink **D28** |
| 44 | `uint16` | `cell_mv[0]` | mV | |
| 46 | `uint16` | `cell_mv[1]` | mV | |
| 48 | `uint16` | `cell_mv[2]` | mV | |
| 50 | `uint16` | `cell_mv[3]` | mV | |
| 52 | `int8` | `cell_temp_c[0]` | °C | |
| 53 | `int8` | `cell_temp_c[1]` | °C | |
| 54 | `int8` | `cell_temp_c[2]` | °C | |
| 55 | `int8` | `cell_temp_c[3]` | °C | Third thermal reference point (with `mppt_temp_c10` and `enclosure_temp_c10`) |
| 56 | `uint16` | `bms_cycles` | — | |
| 58 | `uint16` | `bms_capacity_dah` | 0.1 Ah | Reported pack capacity |
| 60 | `uint16` | `bms_alarms` | — | Protection/alarm bitfield, passed through from the BMS unmodified |
| 62 | `uint16` | `bms_age_s` | s | Seconds since the last successful BMS read. Saturates at `UINT16_MAX` — deliberately narrower than `last_traversal_age_s`; see §7.2.9 |

**Per-cell voltages are carried every frame, not published every frame.** Readings
jitter 1–2 mV between polls from ADC noise; the *bridge* rounds or publishes on change
(§16.4). Carrying them costs 8 bytes on a link where airtime is not a constraint
(§15.2), and keeps the decision about publication policy on the OTA-capable side of
the link.

`bms_age_s` is the field that prevents a dead BLE link from looking like a healthy pack
reporting unchanged values — the same failure mode `vedirect_stale` guards against on
the MPPT side.

#### 7.2.4 Node block — offsets 64–77

| Off | Type | Field | Unit | Notes |
|---:|---|---|---|---|
| 64 | `uint32` | `uptime_s` | s | |
| 68 | `uint16` | `boot_count` | — | From nonvolatile storage; `0` if unavailable |
| 70 | `uint16` | `node_mv` | mV | INA226 supply voltage — the node measures its own rail |
| 72 | `int16` | `node_ma` | mA | INA226 supply current |
| 74 | `int16` | `enclosure_temp_c10` | 0.1 °C | LM75. First of three thermal reference points |
| 76 | `uint8` | `node_flags` | — | §7.2.8 |
| 77 | `uint8` | `status_reason` | — | §8.7 — why this frame was sent |

#### 7.2.5 `detect_flags`

| Bit | Meaning |
|---|---|
| 0 | SAFETY currently asserted (debounced) |
| 1 | EXIT currently asserted (debounced) |
| 2 | Direction classifier is mid-sequence (awaiting a second edge) |
| 3 | **Vehicle detected while held open** — the §16.3 alert condition |
| 4 | A detection was suppressed by the re-alert interval |
| 7:5 | Reserved — write `0` |

Bits 0 and 1 duplicate `input_bits` bits 2 and 3 by design: `input_bits` is raw
evidence, `detect_flags` is the classifier's own view, and a disagreement between them
is itself a diagnostic.

#### 7.2.6 `mppt_flags`

| Bit | Meaning |
|---|---|
| 0 | Load output on |
| 1 | **VE.Direct frame stale** — no complete text frame within timeout |
| 2 | A HEX transaction is currently outstanding |
| 3 | **Charging inhibited** — derived per GateLink PRD (low-temperature detection) |
| 7:4 | Reserved — write `0` |

Bit 1 matters: without it, a dead VE.Direct link is indistinguishable from a healthy
MPPT reporting unchanged values, and HA would show plausible stale data indefinitely.
**The bridge SHOULD mark all MPPT entities unavailable when it is set.**

#### 7.2.7 `bms_flags`

| Bit | Meaning |
|---|---|
| 0 | BMS data valid (a successful read has occurred) |
| 1 | Charge MOSFET on |
| 2 | Discharge MOSFET on |
| 3 | Charge inhibited by the BMS (low temperature) |
| 4 | Any protection flag active — see `bms_alarms` |
| 5 | Cell balancing active |
| 7:6 | `soc_source` — `0` = `bms_ble`, `1` = `smartshunt`, `2` = `voltage_coarse`, `3` = unknown |

`soc_source` is on the wire rather than assumed by the bridge because the fallback
chain is a runtime condition, and an SOC figure whose provenance is invisible is worse
than no SOC figure.

#### 7.2.8 `node_flags`

| Bit | Meaning |
|---|---|
| 0 | **Configuration persisted** — clear means the node is running unsaved overrides (no usable microSD) |
| 1 | microSD present and writable |
| 2 | Relay dry-run mode active |
| 3 | BMS BLE polling enabled |
| 4 | Any debug mode active |
| 5 | Hard-shutdown / entrapment latch asserted |
| 6 | **Traversal age not persisted** — `last_traversal_age_s` is uptime-bounded, not restored from nonvolatile storage (§7.2.9) |
| 7 | Reserved — write `0` |

---

#### 7.2.9 `last_traversal_age_s` — width, persistence and ownership

**Placed here rather than in §7.2.1 because it constrains three layers**: the field
width, the node's nonvolatile state set, and the bridge's publication policy. Changing
one without the others produces a field that looks correct and is useless.

**A traversal is a vehicle, not a gate cycle.** The classifier in §7.2.5 registers a
traversal when the series-wired safety loops and the exit wand produce an edge
sequence it can classify. Gate motion is tracked separately by `gate_state` and
`movement_cause`. The v0.2 wording — "the last classified traversal" — read naturally
as an open/close cycle, which is a different and much more frequent event.

**Widened from `uint16` to `uint32`.** Sixteen bits saturates at 65 535 s — 18 h 12 m.
A driveway with no vehicle for eighteen hours is ordinary: a weekday at work, a
weekend away, a vacation. Under the correct reading of "traversal" the ceiling is hit
routinely, and it is hit *precisely* in the condition an away-from-home user cares
about. A field that goes blind exactly when it becomes interesting is not a field
worth carrying. `uint32` gives 136 years for two bytes, on a link where §15.2
establishes byte count as a non-constraint, and matches the width of `uptime_s` in
this schema and in §7.3.

**`bms_age_s` is deliberately left at `uint16`.** It is a staleness gate, not a
duration record: anything beyond roughly an hour already means the BLE link is dead,
and every consumer of it (§16.4) thresholds rather than reads the value. Widening it
would be symmetry for its own sake.

**The width is only meaningful if the value survives reboot.** Without persistence the
field's real ceiling is node uptime, not `UINT32_MAX`, and a power cycle resets it to
"none since boot" — so the 32-bit width would buy nothing in the one case it was
widened for. The last traversal timestamp is therefore **added to the microSD
nonvolatile state set** alongside last-known state, boot counter and configuration.
On boot the node restores it, computes the elapsed interval from the RTC, and resumes
ageing from there. With no usable microSD present the node reports `UINT32_MAX` and
sets `node_flags` bit 6 — honestly unavailable, never silently zero, consistent
with the `persist_status` discipline in §7.4.

**Long-horizon history is the bridge's job, not the node's.** The bridge holds NTP
time; the node does not. On receipt the bridge converts the age to an **absolute UTC
timestamp** and publishes that retained under `lran/gatelink/detect/state`. HA and its
recorder then own days, weeks and months of traversal history durably, across both
node reboots and bridge restarts. The node's field only has to be correct across the
gap — but it has to be correct across a gap measured in days, which is what the
widening and the persistence together provide.

### 7.3 Schema `0x11` — GateLink event v1 (16 bytes)

| Off | Type | Field | Notes |
|---:|---|---|---|
| 0 | `uint8` | `event_type` | §8.9 |
| 1 | `uint8` | `event_flags` | Bit 0 = this is a follow-up refining an earlier event (e.g. direction resolved later); 7:1 reserved |
| 2 | `uint8` | `hold_source` | §8.4 — the hold in force when the event occurred |
| 3 | `uint8` | `direction` | §8.6 — `UNDETERMINED` if not yet classified |
| 4 | `uint8` | `gate_state` | §8.3 — gate state at the moment of the event |
| 5 | `uint8` | `input_bits` | Raw inputs at the moment of the event (§7.2.1) |
| 6 | `uint16` | `detail` | Event-specific |
| 8 | `uint32` | `event_id` | Monotonic per node per boot, never reused within a `ctx_id` |
| 12 | `uint32` | `uptime_s` | Node uptime when the event occurred |

`event_id` is the deduplication key on the non-retained MQTT path. A `COMMAND` is
deduplicated by `(ctx_id, seq)`; an `EVENT` has no ACK and may be retransmitted after
a CAD backoff, so HA needs a value it can use to recognise a repeat. The bridge SHALL
suppress republication of an `(src, ctx_id, event_id)` triple it has already published.

**Follow-up events.** When a detection fires before its direction is classified, the
node emits the event immediately with `direction = UNDETERMINED`, then emits a second
event with `event_flags` bit 0 set and the same `event_id` once classification
completes. This preserves "alert on the first edge, no dead window" while still
delivering direction when it becomes known.

---

### 7.4 Schema `0x12` — GateLink config v1 (variable)

Carried by both `CONFIG` and `CONFIG_ACK`.

**`CONFIG` payload:**

| Off | Type | Field |
|---|---|---|
| 0 | `uint8` | `op` — §8.10 |
| 1 | `uint8` | `count` — number of entries following |
| 2.. | entries | see below |

**Entry (variable):**

| Off | Type | Field |
|---|---|---|
| 0 | `uint16` | `param_id` |
| 2 | `uint8` | `ptype` — `0x01` u8, `0x02` u16, `0x03` u32, `0x04` i16, `0x05` i32, `0x06` bool |
| 3 | `uint8` | `len` — value length in bytes |
| 4.. | `uint8[len]` | value, little-endian |

**`CONFIG_ACK` payload:**

| Off | Type | Field |
|---|---|---|
| 0 | `uint8` | `op` — echoed |
| 1 | `uint8` | `persist_status` — §8.11 |
| 2 | `uint8` | `count` |
| 3.. | result entries | `uint16 param_id`, `uint8 status` (§8.12), `uint8 ptype`, `uint8 len`, `uint8[len]` *effective* value |

Three properties are load-bearing and must survive into the implementation:

- **Per-entry results.** Unknown keys are rejected individually with a reason, never
  silently ignored, and the remainder of the set still applies.
- **The ACK carries the effective value, not the requested one.** Out-of-range values
  are clamped to the documented range and the clamp is reported (`status = CLAMPED`),
  rather than applied quietly.
- **`persist_status` is honest.** A node with no usable microSD still applies and
  still ACKs the change, with `persist_status = APPLIED_NOT_PERSISTED`. HA must never
  be told a value was saved when it was not.

`param_id` values, types, ranges and defaults are declared once in `/lib/lran-config/`
and generated from there into firmware defaults, HA discovery payloads and
`/docs/gatelink-config.md`. **This document does not enumerate them** — three
hand-maintained copies would drift.

---

### 7.5 Schema `0xF0` — Generic node health (20 bytes)

Emitted by every node type, including the bridge's own self-report and `simnode`.

| Off | Type | Field |
|---:|---|---|
| 0 | `uint32` | `uptime_s` |
| 4 | `uint16` | `boot_count` |
| 6 | `uint16` | `rx_frames` |
| 8 | `uint16` | `tx_frames` |
| 10 | `uint16` | `rx_dropped` — sum of all §14 discard counters |
| 12 | `uint16` | `cad_backoffs` |
| 14 | `int16` | `last_rssi_dbm` |
| 16 | `int16` | `last_snr_db10` — 0.1 dB units |
| 18 | `uint8` | `proto_ver` — the `ver` this node speaks |
| 19 | `uint8` | `health_flags` — bit 0 = any debug mode active; 7:1 reserved |

A node type with no application schema yet — a freshly bootstrapped WellLink, a
`simnode` — is still fully observable through `0xF0` alone. This is what makes the
multi-node bench test possible before WellLink's payload exists.

---

### 7.6 `HEX_REQ` / `HEX_RSP` payloads

**`HEX_REQ`** (bridge → node):

| Off | Type | Field |
|---|---|---|
| 0 | `uint8` | `flags` — bit 0 = write-class request (Set/Restart); 7:1 reserved |
| 1 | `uint8` | `n` — length of the HEX string |
| 2.. | `uint8[n]` | The VE.Direct HEX request, ASCII, **verbatim**, including the leading `:` and excluding the newline |

**`HEX_RSP`** (node → bridge):

| Off | Type | Field |
|---|---|---|
| 0 | `uint8` | `status` — §8.13 |
| 1 | `uint8` | `n` — length of the HEX string, `0` on error |
| 2.. | `uint8[n]` | The MPPT's response, ASCII, verbatim |

**Authentication rule.** A `HEX_REQ` whose HEX command nibble is **Set (`0x8`) or
Restart (`0x6`) MUST carry a valid MAC**; Get (`0x7`) and all read-only commands need
none (§9.2). The node inspects **only the command nibble** for this purpose and
otherwise treats the string as opaque. It does not hold a register cache, replay
writes, or interpret register semantics.

This is the first of three independent gates on MPPT writes. The other two — an armed
write-enable switch with auto-expiry, and a retained audit trail — are enforced on the
bridge and specified in [`LRAN-Bridge_Node-PRD`](./LRAN-Bridge_Node-PRD.md).

> Writing MPPT charge parameters under LiFePO4 is a **battery-damage path**.
> Re-enabling temperature compensation or equalization on a lithium pack is exactly
> the kind of one-character mistake that is invisible until the battery is harmed.

**On-node UART multiplexing** — interleaving HEX responses with the ~1 Hz text stream
on one UART — is a node implementation matter and is specified in
[`LRAN-GateLink_Node-Implementation-Plan`](./LRAN-GateLink_Node-Implementation-Plan.md).

---

## 8. Enumerations

### 8.1 `cmd`

| Value | Name | `arg` / `arg2` |
|---|---|---|
| `0x00` | `NOP` | — |
| `0x01` | `OPEN` | Momentary open; auto-close returns the gate |
| `0x02` | `CLOSE` | `arg` = `0` release only, `1` release then immediate close |
| `0x03` | `HOLD_OPEN` | Open and latch — OPEN+LOCK |
| `0x04` | `RELEASE_HOLD` | UNLOCK; auto-close then closes |
| `0x10` | `REQUEST_STATUS` | — |
| `0x11` | `REQUEST_CONFIG` | — |
| `0x20` | `SET_DEBUG_MODE` | `arg2` = bitmask of debug modes |
| `0x21` | `SET_RELAY_DRY_RUN` | `arg` = `0` off, `1` on |
| `0x22` | `SET_BMS_POLLING` | `arg` = `0` off, `1` on |
| `0x7F` | `REBOOT` | `arg` = `0xA5` required as a confirmation guard |

`0x00`–`0x0F` are **actuation commands** and are the only values that reach a physical
output. `0x10`+ are node-local. This split is deliberate: it lets a node apply a
stricter policy to actuation without parsing semantics.

> **Retired from v0.1:** `STOP`, `STEP_BY_STEP`, `PARTIAL_OPEN`, `LOCK`, `UNLOCK` as
> separate commands, and `SET_POWER_PROFILE`. The first four assumed BusT4 command
> access; the accessory-I/O interface exposes four momentary relays and no step or
> partial-open path (GateLink PRD). `LOCK`/`UNLOCK` are absorbed into
> `HOLD_OPEN`/`RELEASE_HOLD`, which name what the operator actually wants rather than
> the 1050's internal latch. `SET_POWER_PROFILE` went with the night profile (§17.1).

### 8.2 `result` (in `COMMAND_ACK`)

| Value | Name | Meaning |
|---|---|---|
| `0x00` | `ACCEPTED` | Authenticated and dispatched |
| `0x01` | `REJECTED_MAC` | MAC verification failed |
| `0x02` | `REJECTED_SEQ` | Sequence not greater than high-water mark (replay) |
| `0x03` | `REJECTED_CTX` | `ctx_id` mismatch — bridge must resync (§10.3) |
| `0x04` | `REJECTED_UNKNOWN_CMD` | Unrecognized `cmd` |
| `0x05` | `REJECTED_ARG` | Bad `arg` for this command |
| `0x06` | `REJECTED_NOT_SUPPORTED` | Valid command, not implemented by this node type |
| `0x07` | `DUPLICATE_CACHED` | Already executed; this is the cached ACK (§10.4) |
| `0x10` | `DRY_RUN` | Accepted and logged, but no output was energized |
| `0x11` | `ACTUATOR_BUSY` | A pulse or sequence was already in progress |
| `0x12` | `REJECTED_UNSAFE` | Refused by a local safety rule |

> **Retired from v0.1:** `BUST4_ERROR`, `BUST4_TIMEOUT`, `BUST4_UNAVAILABLE`. There is
> no bus to fail. A relay pulse either happens or the node is not running.

### 8.3 `gate_state`

| Value | Name | IN1 / IN2 |
|---|---|---|
| `0x00` | `UNKNOWN` | — (pre-first-read) |
| `0x01` | `CLOSED` | 0 / 0 — closed and idle |
| `0x02` | `MOVING` | 0 / 1 — opening, or closing past the open limit |
| `0x03` | `OPEN_COUNTDOWN` | 1 / 1 — open, auto-close timer running |
| `0x04` | `OPEN_HELD` | 1 / 0 — open with nothing counting down: a lock is in force |
| `0x08` | `FAULT` | Hard-shutdown latch asserted (IN6) |

> **Replaces v0.1's nine-value enum**, which was modelled on the 1050's BusT4 status
> byte and included `OPENING`/`CLOSING`, `STOPPED_PARTIAL`, `PARTIAL_OPEN` and
> `BLOCKED`. Two discrete inputs cannot distinguish opening from closing directly —
> direction during motion is inferred from the previous stable state and is carried in
> the HA `cover` entity, not here — and partial-open is out of scope. **The
> four-state table above is strictly more informative than v0.1's for the thing that
> matters**, because `OPEN_HELD` is directly observable rather than remembered.

### 8.4 `hold_source`

| Value | Name | Set by |
|---|---|---|
| `0x00` | `NONE` | Not held |
| `0x01` | `LRAN` | The node's own `HOLD_OPEN` command |
| `0x02` | `MANUAL` | Handheld remote, keyswitch, or panel |
| `0x03` | `KEYPAD_FIRE` | The keypad FIRE code |
| `0x04` | `UNKNOWN` | A hold observed across a node reboot, unattributable |

### 8.5 `movement_cause`

| Value | Name |
|---|---|
| `0x00` | `UNKNOWN` |
| `0x01` | `EXIT_WAND` |
| `0x02` | `LRAN_COMMAND` |
| `0x03` | `KEYPAD_FIRE` |
| `0x04` | `MANUAL_HOLD` — settles to open+held with no LRAN command |
| `0x05` | `EXTERNAL_MOMENTARY` — settles to open+countdown with no LRAN command |

> **Replaces v0.1's provisional table** (`RADIO_REMOTE`, `WALL_BUTTON`, `LOOP`,
> `OVIEW`, `AUTO_CLOSE`, `SAFETY_REVERSE`), which was a guess at what the 1050 would
> expose over BusT4 and was flagged as such (v0.1 open item W2). Cause is now
> **derived locally from observed inputs and timing**, so the values are exactly the
> distinctions the node can actually make. **W2 closes.**

### 8.6 `last_direction` / `direction`

| Value | Name |
|---|---|
| `0x00` | `NONE` — no traversal recorded since boot |
| `0x01` | `ENTRY` — SAFETY first, then EXIT: vehicle arriving |
| `0x02` | `EXIT` — EXIT first, then SAFETY: vehicle departing |
| `0x03` | `UNDETERMINED` — single sensor only, simultaneous, or partial traversal |

> **Semantics corrected from v0.1**, which defined `ENTRY` as "outside loop first" and
> `EXIT` as "inside loop first." The two safety loops are wired **in series** into one
> detector channel and are not separately observable. Discrimination is between the
> combined SAFETY contact and the separate EXIT wand.

### 8.7 `status_reason`

| Value | Name |
|---|---|
| `0x00` | `POLL_RESPONSE` |
| `0x01` | `GATE_STATE_CHANGE` |
| `0x02` | `HOLD_STATE_CHANGE` |
| `0x03` | `MPPT_ERROR` |
| `0x04` | `VEHICLE_DETECTED` |
| `0x05` | `BMS_ALARM` |
| `0x06` | `HARD_SHUTDOWN` |
| `0x07` | `FIRE` |
| `0x08` | `CONFIG_CHANGE` |
| `0x09` | `BOOT` |
| `0x0A` | `CHARGE_INHIBITED` |
| `0xFF` | `DEBUG_SYNTHETIC` |

`DEBUG_SYNTHETIC` marks frames produced by the dummy-status-push tool or by `simnode`.
**The bridge MUST propagate this marking to MQTT** so synthetic data is never mistaken
for real in HA history.

### 8.8 `err_code`

| Value | Name |
|---|---|
| `0x01` | `BAD_VERSION` |
| `0x02` | `BAD_LENGTH` |
| `0x03` | `UNKNOWN_TYPE` |
| `0x04` | `CTX_MISMATCH` |
| `0x05` | `NOT_ADDRESSED` |
| `0x06` | `BAD_CRC` |
| `0x07` | `UNKNOWN_SCHEMA` |
| `0x08` | `REASSEMBLY_TIMEOUT` |
| `0x09` | `FRAGMENT_OVERFLOW` |
| `0x0A` | `UNKNOWN_HDR_EXT` |


### 8.9 `event_type`

| Value | Name | Priority |
|---|---|---|
| `0x01` | `VEHICLE_WHILE_HELD_OPEN` | High — email/SMS |
| `0x02` | `FIRE_ASSERTED` | **Highest — routed separately** |
| `0x03` | `HARD_SHUTDOWN` | High |
| `0x04` | `VEHICLE_DETECTED` | Normal |
| `0x05` | `GATE_STATE_CHANGE` | Normal |
| `0x06` | `HOLD_STATE_CHANGE` | Normal |
| `0x07` | `BMS_ALARM` | High |
| `0x08` | `MPPT_ERROR` | Normal |
| `0x09` | `CHARGE_INHIBITED` | Low |
| `0x0A` | `BOOT` | Low |

FIRE has its own event type and its own MQTT topic rather than being folded in as
another hold source. The keypad FIRE code is used only for testing and in a real fire
emergency; **it is the one signal in the system that should be allowed to wake someone
up**, and that requires HA to be able to route it independently.

### 8.10 `op` (CONFIG)

| Value | Name |
|---|---|
| `0x01` | `SET` |
| `0x02` | `GET` — the listed `param_id`s |
| `0x03` | `GET_ALL` — the full effective configuration |
| `0x04` | `RESTORE_DEFAULTS` — clears all overrides |

### 8.11 `persist_status`

| Value | Name |
|---|---|
| `0x00` | `PERSISTED` |
| `0x01` | `APPLIED_NOT_PERSISTED` — no usable microSD; RAM only until reboot |
| `0x02` | `NOT_APPLIED` — the whole set was rejected |

### 8.12 per-entry config `status`

| Value | Name |
|---|---|
| `0x00` | `OK` |
| `0x01` | `UNKNOWN_PARAM` |
| `0x02` | `CLAMPED` — applied at a range endpoint; the ACK carries the effective value |
| `0x03` | `TYPE_MISMATCH` |
| `0x04` | `READ_ONLY` |

### 8.13 `HEX_RSP` `status`

| Value | Name |
|---|---|
| `0x00` | `OK` |
| `0x01` | `TIMEOUT` — no response within `hex_timeout_ms` |
| `0x02` | `REJECTED_UNAUTHENTICATED` — a write-class request with no valid MAC |
| `0x03` | `BUSY` — a transaction was already outstanding |
| `0x04` | `UART_ERROR` |
| `0x05` | `MALFORMED_REQUEST` |

On timeout the node returns a `HEX_RSP` carrying `TIMEOUT` rather than silence. A
silent transport failure is indistinguishable in HA from an MPPT that ignored the
request.

---

## 9. Authentication and keying

### 9.1 Per-node keys

A single fleet-wide key would mean **compromising the well sensor grants gate command
authority** — an unacceptable coupling between a low-value node and the only node that
moves a large motorized object.

Keys are derived per node from one master:

```
node_key = HKDF-SHA256( master_key, salt = "lran-v1", info = "node-" || node_id )
```

- Only `master_key` is provisioned to the bridge.
- Each node is flashed with only its own derived key.
- Compromising a node yields that node's key alone.
- Adding a node requires no change to existing nodes.

`master_key` is a 32-byte secret living in untracked build config and is never
committed. `mbedtls_md_hmac` and `mbedtls_hkdf` from ESP-IDF (Apache-2.0) provide the
primitives.

The salt string stays `"lran-v1"` across protocol version bumps. It is a key-derivation
domain separator, not a wire-format version; changing it would silently invalidate
every provisioned node.

### 9.2 What is authenticated

| Frame type | MAC | Rationale |
|---|---|---|
| `COMMAND` | **Yes** | Moves the gate |
| `CONFIG` | **Yes** | Changes pulse widths, detection windows and alert behaviour — a command by any other name |
| `HEX_REQ` — Set (`0x8`) / Restart (`0x6`) | **Yes** | Writes MPPT config — a battery-damage path under LiFePO4 (§7.6) |
| `HEX_REQ` — Get (`0x7`) and other reads | No | Read-only, consistent with status |
| `STATUS`, `EVENT`, `POLL`, `PING`, `ERROR` | No | Spoofed status is a nuisance, not a hazard (§9.5) |
| `COMMAND_ACK`, `CONFIG_ACK`, `HEX_RSP` | No | Correlated to an authenticated request by `seq` |

### 9.3 Computation

```
mac = HMAC-SHA256( node_key, ver || type || src || dst || seq || ctx_id
                             || frag || schema || hdr_flags || rsv[3]
                             || payload )[0..7]
```

That is **the entire 16-byte header followed by the entire payload**, serialized
exactly as on the wire, little-endian. The CRC16 is appended after the MAC and is not
covered by it.

Including the full header costs nothing and prevents an attacker from altering message
type, addressing or fragment position on a captured frame. There is no reason to
authenticate less than everything present. **The reserved bytes and `hdr_flags` are
inside the MAC** — including them now means a future header extension is authenticated
from the day it is defined, with no change to this computation and therefore no
`ver` bump for the MAC scope (§13.2, row 2).

**Widened from 32 to 64 bits.** Wire-format v0.1 used a 4-byte truncated MAC. Eight
bytes takes forgery odds from ~1-in-4.3×10⁹ to ~1-in-1.8×10¹⁹ per attempt, on a link
where the airtime analysis (§15.2) shows four bytes buys nothing worth having.

### 9.4 Verification order

A node MUST, in this order:

1. Check PHY CRC status, length, `ver`, `dst`, `type`, `schema`, payload length, and
   application CRC16. Reject → `ERROR`.
2. Check `ctx_id` equals its own. Mismatch → `COMMAND_ACK(REJECTED_CTX)`.
3. Verify the MAC **in constant time**. Failure → `COMMAND_ACK(REJECTED_MAC)`.
4. Check `(ctx_id, seq)` against the dedup cache (§10.4). Hit →
   `COMMAND_ACK(DUPLICATE_CACHED)` with the cached result; **do not re-execute.**
5. Check `seq > rx_high_water` by serial-number arithmetic. Failure →
   `COMMAND_ACK(REJECTED_SEQ)`.
6. Update `rx_high_water = seq`, dispatch.

Step 3 before step 5 is deliberate: `seq` is attacker-visible, so checking it before
authenticating would let an unauthenticated party learn the high-water mark from timing
or response differences.

### 9.5 Accepted limitations — stated plainly

- **No confidentiality.** All frames are plaintext. Gate state and traffic patterns are
  observable to anyone with an SDR.
- **`STATUS` and `EVENT` are unauthenticated.** An attacker can inject false status
  into HA — a false "gate open" reading, or a spurious held-open event. There is no
  actuation path, but **any HA automation triggering on gate state inherits this.**
  Worth remembering before writing an automation that unlocks a door on `ENTRY`.
- **A valid forgery burns the sequence number it uses.** Monotonic `seq` means an
  attacker gets one attempt per sequence value.
- The 32-bit `ctx_id` reduces the boot-collision replay window from v0.1's 1-in-65535
  to roughly 1-in-4.3×10⁹, which is no longer a listed limitation.

---

## 10. Sequencing, context and replay

### 10.1 Context ID

On boot, **each node** generates a random non-zero `uint32` `ctx_id` and uses it in
every frame it sends. The bridge learns it per node from any received frame and echoes
it in every frame it sends to that node. The bridge maintains a per-node table of
`(ctx_id, last_seq, last_seen)`.

`ctx_id` replaces cross-reboot sequence persistence: a node reboot produces a new
context, invalidating every previously captured command addressed to it.

The bridge's own frames carry the **destination node's** `ctx_id`, not one of its own.
The bridge does not have a context; it is the party that tracks everyone else's.

### 10.2 Two independent sequence spaces per node

| Space | Owner | Purpose | On node reboot |
|---|---|---|---|
| Command `seq` | bridge | **Replay protection** — strictly increasing, security-relevant | Bridge resets to `1` on learning a new `ctx_id` |
| Status `seq` | node | **Ordering and dedup only** — not security-relevant | Resets to `1` |

The bridge MUST treat status `seq` as advisory. It is useful for discarding duplicates
within a short window and for detecting loss in diagnostics. **It MUST NOT be used to
reject frames**, because a status frame arriving out of order is still current data.

### 10.3 Resync procedure

1. A node receives a `COMMAND` whose `ctx_id` does not match → replies
   `COMMAND_ACK(REJECTED_CTX)` carrying its **own** `ctx_id` in the header.
2. The bridge adopts the `ctx_id` from that ACK, resets its command `seq` for that node
   to `1`, and retries the original command **once**.
3. If a second `REJECTED_CTX` follows, the bridge stops retrying and publishes an
   availability/diagnostic fault rather than looping.

Step 3 exists to prevent a resync loop from becoming a transmit storm, which on a shared
multi-node channel is a problem for every other node as well as this one.

### 10.4 Command deduplication — required, because pulses are not idempotent

Commands are ACKed and the bridge retries with backoff. On a node driving **physical
relay pulses**, a retried command is a second pulse, not an idempotent re-send.

**A node SHALL cache the result of the last `dedup_cache_depth` (default 8) executed
commands keyed on `(ctx_id, seq)`, and on a repeat return the cached ACK without
re-executing.** The bridge retrying an ACK it never received must not move the gate
again.

The cache is RAM-only and is lost on reboot, which is correct: a reboot changes
`ctx_id`, so no pre-reboot `seq` can match anyway.

### 10.5 Wrap behavior

`seq` wraps modulo 2^16. At one command per minute, wrap takes ~45 days of continuous
commanding, and any node reboot resets both counters. Comparison MUST use
serial-number arithmetic (RFC 1982 style), not a plain `>`.

---

## 11. Fragmentation

- A sender splits an oversized payload into **≤15 fragments**, each a complete frame
  with its own header, MAC (if the type is authenticated) and CRC16.
- All fragments of a set share `(src, seq, schema)`; `frag` carries index and total.
- The receiver reassembles by `(src, ctx_id, seq, schema)`. Incomplete sets expire
  after `frag_reassembly_timeout_ms` (default 5000) and are discarded with
  `ERROR(REASSEMBLY_TIMEOUT)`.
- A fragment index ≥ the declared total, or a set exceeding
  `LRAN_MAX_SCHEMA_PAYLOAD` (196 B, §3.1) on reassembly, is discarded with
  `ERROR(FRAGMENT_OVERFLOW)`.
- **Fragments are individually acknowledged only for `COMMAND`.** `STATUS` and `EVENT`
  are fire-and-forget; a dropped fragment discards the set.
- **Fragmentation is not used for commands in v1.** Commands are 4 bytes. Keeping them
  single-frame keeps authentication and replay logic simple.

No schema currently defined requires fragmentation — the largest is 78 bytes.
Fragmentation is specified and implemented anyway because it costs one header byte, and
because a node whose payload outgrows one frame after it is 500 ft away and has no OTA
is a node that cannot be fixed.

**It is nonetheless exercised, by `PING` (§6.6.2).** A specified-but-never-executed
path is not a working path. Because `PING` can be driven above the single-frame cap
from the bench tool, the reassembly logic is validated on real hardware over real RF
before any schema depends on it. The `PING` reassembly cap is
`LRAN_MAX_PAYLOAD_PLAIN` rather than `LRAN_MAX_SCHEMA_PAYLOAD`, since `PING` carries
no schema and never carries a MAC.

---

## 12. Radio configuration and media access

### 12.1 Required settings

| Parameter | Value | Note |
|---|---|---|
| Band | 915 MHz ISM | Exact channel per **D1** |
| Header mode | **Explicit** | Required by §2.1 |
| CRC | **Enabled** | Required by §2.1 |
| Sync word | Private (`0x12` / SX126x `0x1424`) | Not the LoRaWAN value |
| SF / BW / CR / TX power | Per **D1** | §15 gives the airtime consequences |
| Node-address filtering | Enabled in the SX126x packet handler | Low value while nodes run continuous RX; retained because it matters for any duty-cycled node (§17.1) |

**All nodes share one frequency, SF, BW and sync word.** Per-node channels would
require the bridge to listen on multiple configurations, which one SX1262 cannot do.

**LoRa PHY parameters are not runtime-configurable.** Changing them from HA means
changing the link you are changing them over; one mismatch and the node is unreachable
until someone walks to it with a laptop. If this is ever wanted it needs a
commit-and-revert scheme — apply, require a confirmation frame within N seconds,
otherwise revert. Out of scope for v1.

### 12.2 Radio pin map is injected, not hardcoded

One SX1262 driver serves both the Heltec bridge (fixed internal pins, 1.8 V TCXO, DIO2
RF switch) and the GateLink carrier board (different pins, module-specific TCXO
voltage). **The pin map, TCXO voltage and RF-switch mode SHALL be supplied by
configuration at construction**, never compiled in.

### 12.3 Media access

Point-to-point had no contention. A shared channel with N nodes does.

| Mechanism | Detail |
|---|---|
| **Bridge serializes polls** | Never more than one outstanding poll across the fleet. Removes the largest predictable collision source for free |
| **CAD before TX** | Nodes use Channel Activity Detection before transmitting |
| **Randomized backoff** | On CAD-busy, back off `random(0, backoff_max_ms)` (default 500), retry up to `cad_retries` (default 5), then **transmit regardless** — an event push must not be starved indefinitely |
| **Single channel** | §12.1 |

**FCC.** 915 MHz ISM, digital modulation under Part 15.247. Not LoRaWAN, so no TTN
duty-cycle policy applies, but transmit power and bandwidth limits do. See §18, W5.

---

## 13. Version tolerance and versioning policy

### 13.1 Version tolerance — the no-OTA consequence

Remote nodes are USB-only. A protocol change means a physical visit to every node. To
make rollout incremental rather than a flag day:

- **The bridge SHALL accept protocol version `N` and `N−1`** and decode both.
- Nodes accept only their own version from the bridge; the bridge downgrades per-node
  based on the version last heard from that node.
- A node running an unsupported version is marked `unavailable` with a **distinct
  reason**, not silently ignored.
- The bridge publishes the per-node protocol version as a diagnostic sensor, so skew is
  visible rather than latent.

### 13.2 Versioning policy

| Change | Requires |
|---|---|
| Header field added, removed, resized or reordered | **`ver` bump** |
| Authentication scope or MAC computation changed | **`ver` bump** |
| Message type added | No bump — unknown types are discarded with `ERROR(UNKNOWN_TYPE)` |
| Payload schema field added, removed or resized | **New `schema` ID.** No `ver` bump |
| Enumeration value added | No bump — receivers treat unknown values as `UNKNOWN` |
| Reserved bit or field assigned **optional** meaning | No bump — §4.3 requires receivers to ignore them |
| Reserved header space assigned **must-understand** meaning | No bump — set `hdr_flags` bit 7 (§5.8); non-implementing receivers discard loudly |

**The schema-ID mechanism is what makes incremental rollout work.** A `ver` bump breaks
every node at once; a new schema ID breaks nothing, because the bridge decodes whatever
each node announces. Prefer a schema ID over a header change whenever there is a choice.

Every change of either kind requires an entry in `/docs/protocol-changelog.md` and a
regenerated set of test vectors.

Mixed-`ver` operation between a node and the bridge is a supported field condition
(N/N−1 only). Mixed operation outside that window must **fail loudly and diagnosably,
not degrade.**

---

## 14. Receive-path handling

A receiver MUST process in this order, discarding and **counting** at each stage:

| Stage | Check | On failure |
|---|---|---|
| 1 | PHY CRC status | Discard silently; `rx_crc_err` |
| 2 | Length ≥ 18 (`LRAN_HDR_LEN` + `LRAN_CRC_LEN`) | Discard; `rx_runt` |
| 3 | Application CRC16 | Discard; `rx_bad_crc`; optionally `ERROR(BAD_CRC)` |
| 4 | `ver` known | Discard; `rx_bad_ver`; optionally `ERROR(BAD_VERSION)` |
| 5 | `dst` is self or `0xFF` | Discard; `rx_not_addressed` |
| 5a | `hdr_flags` bit 7 clear, **or** the denoted extension is implemented (§5.8) | Discard; `rx_unknown_hdr_ext`; `ERROR(UNKNOWN_HDR_EXT)` |
| 6 | `type` known | `ERROR(UNKNOWN_TYPE)` |
| 7 | `schema` known (typed frames only) | `ERROR(UNKNOWN_SCHEMA)` |
| 8 | Payload length matches `(type, schema)` | `ERROR(BAD_LENGTH)` |
| 9 | Fragment reassembly (§11) | `ERROR(REASSEMBLY_TIMEOUT)` / `ERROR(FRAGMENT_OVERFLOW)` |
| 10 | Type-specific handling | §9.4 for authenticated types |

Every counter is published — per node by the bridge, and in schema `0xF0` by the node
itself. **Silent discards are the enemy of field debugging** on a link with no console
access: the counters are how a marginal link is distinguished from a firmware bug when
the node is 500 ft away in the rain.

---

## 15. Airtime and power analysis

Computed for BW 125 kHz, CR 4/5, explicit header, CRC on, 8-symbol preamble.

### 15.1 Airtime per frame

| Frame | Bytes | SF7 | SF8 | SF9 |
|---|---:|---:|---:|---:|
| `POLL` | 19 | 51 ms | 103 ms | 185 ms |
| `COMMAND_ACK` | 24 | 62 ms | 113 ms | 206 ms |
| `COMMAND` | 30 | 72 ms | 123 ms | 226 ms |
| `EVENT` (`0x11`) | 34 | 77 ms | 134 ms | 247 ms |
| Node health (`0xF0`) | 38 | 82 ms | 144 ms | 267 ms |
| **`STATUS` (`0x10`)** | **96** | **164 ms** | **297 ms** | **534 ms** |
| `PING`, maximum | 222 | 348 ms | 615 ms | 1107 ms |

Figures are the LoRa time-on-air formula applied to the frame sizes in §19, rounded to
the millisecond, and should be regenerated once **D1** fixes SF.

> **Two corrections in this revision.** The frame sizes grew by 4 bytes (16-byte
> header, §5.9) and by a further 2 for `STATUS` (§7.2.9). Separately, the v0.2 figures
> **omitted the 4.25-symbol sync interval** in the preamble term, computing
> `T_preamble = n_pre × T_sym` instead of `(n_pre + 4.25) × T_sym`. They were
> therefore ~4 % low independently of the size change. The table above uses
> `T_preamble = 12.25 × T_sym` with 8 preamble symbols. This does not disturb any
> conclusion in §15.2 — a 534 ms `STATUS` at SF9 is ~5 µAh, the same answer to two
> significant figures — but the table is used for **latency and channel-occupancy**
> budgeting in §12.3, where a systematic 4 % understatement compounds.
>
> The maximum-`PING` row is included because §6.6.1 now makes a 222-byte frame
> emittable. It is a bench-only figure and does not enter the duty-cycle budget, but
> it is the number to check a CAD/backoff window against: at SF9 a full-size `PING`
> occupies the channel for over a second.

### 15.2 The finding: airtime is not a power constraint here

A `STATUS` frame at SF9 costs ~534 ms of TX. At ~118 mA at +22 dBm on the 3.3 V rail
(~35 mA referred to the 12 V battery), that is **~5 µAh per frame.**

| Status rate | Frames/day | Battery cost |
|---|---:|---:|
| 1 / 5 min | 288 | 1.4 mAh/day |
| 1 / min | 1440 | **7 mAh/day** |

Against a GateLink budget of roughly **7 Ah/day**, even the aggressive rate is **under
0.1%.** For comparison, the two 2 W LED lights at the gate consume roughly *570 times*
as much.

**Design implication: do not optimize these schemas for byte count.** The 78-byte
GateLink status could be bit-packed to perhaps 50 bytes, saving ~2 mAh/day —
approximately nothing — in exchange for a decoder that is materially harder to debug
from a serial log. Airtime matters here for **latency, collision avoidance and channel
occupancy** (§12.3), not for the energy budget.

This also settles a live question: **SF9 is affordable if the range test wants it.**
Choose SF on link margin alone (**D1**), not on power.

---

## 16. MQTT interface

The bridge is the sole MQTT participant. Nodes never speak MQTT.

### 16.1 Topic grammar

```
lran/<node>/<domain>[/<item>]/<leaf>
```

| Element | Values |
|---|---|
| `<node>` | `bridge`, `gatelink`, `welllink`, `simnode0`–`simnode3` (§16.6) |
| `<domain>` | `gate`, `detect`, `battery`, `solar`, `vedirect`, `node`, `config`, `event`, `cmd`, `diag` |
| `<leaf>` | `state`, `set`, `ack`, `audit`, `availability` |

Discovery configs are published under the HA discovery prefix (default
`homeassistant/`) with `unique_id` prefixed per node (`lran_gatelink_*`) so IDs are
stable and non-colliding as the fleet grows.

### 16.2 Standard topics

| Topic | Direction | Retained | Purpose |
|---|---|---|---|
| `lran/<node>/availability` | bridge → HA | **Yes** | `online` / `offline` per node (§16.5) |
| `lran/<node>/<domain>/state` | bridge → HA | **Yes** | Decoded state |
| `lran/<node>/cmd/<action>/set` | HA → bridge | No | Command request |
| `lran/<node>/cmd/ack` | bridge → HA | No | `COMMAND_ACK` outcome |
| `lran/<node>/config/set` | HA → bridge | No | Configuration change |
| `lran/<node>/config/state` | bridge → HA | **Yes** | Full effective configuration, each value marked `default` or `override` |
| `lran/<node>/config/ack` | bridge → HA | No | Outcome of the last change |
| `lran/<node>/event/<name>` | bridge → HA | **No — never** | §16.3 |
| `lran/<node>/vedirect/hex/request` | HA → bridge | No | Raw HEX request string |
| `lran/<node>/vedirect/hex/response` | bridge → HA | No | Raw HEX response + status |
| `lran/<node>/vedirect/hex/audit` | bridge → HA | **Yes** | Every write attempt: payload, authorization outcome, MPPT response |
| `lran/<node>/vedirect/write_enable/{state,set}` | both | **Yes** | Armed write-enable, default off, auto-expiry |
| `lran/<node>/diag/state` | bridge → HA | **Yes** | RSSI/SNR, missed polls, counters, protocol version |
| `lran/bridge/version` | bridge → HA | **Yes** | Bridge firmware version |

### 16.3 Event topics are never retained — a hard rule

Held-open and FIRE events drive **email and SMS**. A retained message replays when HA
restarts and when discovery is refreshed, which produces notifications at arbitrary
times about things that happened days ago.

**Rule.** Every topic under `lran/<node>/event/` SHALL be published with the retain
flag clear, at QoS 1, and SHALL carry the `event_id` from §7.3 so HA can deduplicate a
retransmission.

Where an event also has dashboard value, the bridge publishes **both**: a retained
`binary_sensor` state under the relevant `<domain>/state` for visibility, and the
non-retained event for automation. **The event topic is the automation trigger; the
binary sensor is not.**

### 16.4 Publication policy is the bridge's, not the node's

The node transmits everything it knows in each `STATUS`. The bridge decides what to
publish and when:

- **Publish on change** for noisy values — per-cell voltages jitter 1–2 mV from ADC
  noise and must not be pushed to HA history at the poll rate.
- **Mark stale rather than republish.** When `mppt_flags` bit 1 (`vedirect_stale`) is
  set, mark MPPT entities unavailable. When `bms_age_s` exceeds a threshold, mark BMS
  entities unavailable. Absence is not zero.
- **Propagate `DEBUG_SYNTHETIC`** so synthetic data never enters HA history unmarked.

Keeping this on the bridge is deliberate: the bridge is mains-powered, OTA-capable and
in the house, and publication policy is exactly the kind of thing that changes often.

### 16.5 Per-node availability

MQTT LWT covers only the bridge's connection to the broker. It says nothing about
whether a node is alive.

- The bridge maintains `last_seen` per node.
- A node is marked `offline` after `missed_poll_threshold` (default **3**) consecutive
  unanswered polls, and `online` on any valid frame received.
- State is published to `lran/<node>/availability`, **retained**, and referenced by
  every entity belonging to that node in its discovery config.
- The bridge's own LWT marks all nodes unavailable implicitly.

### 16.6 Bench nodes on a production bridge

A production bridge build **does** accept and decode frames from the bench range
`0xF0`–`0xFE` (§5.3). This is deliberate: bench nodes are most useful when exercised
against the real bridge, the real broker and the real HA instance, not against a
parallel stack that can drift out of agreement with production.

The exposure is constrained on three axes rather than by refusing the frames:

**1. Diagnostic topics only.** Bench-node data is published exclusively under

```
lran/simnode<N>/diag/state
lran/simnode<N>/availability
```

and **never** under `gate`, `detect`, `battery`, `solar` or `event`. A simnode cannot
appear as a gate entity, cannot enter battery history, and — most importantly —
**cannot fire a `lran/*/event/*` topic**, so no bench activity can reach the email and
SMS path §16.3 exists to protect.

**2. Off by default, runtime-switchable.** Publication is gated on a bridge
configuration flag:

| Param | Type | Default | Effect |
|---|---|---|---|
| `simnode_diag_enable` | bool | **`false`** | When `false`, frames from `0xF0`–`0xFE` are decoded, counted in the bridge's own diagnostics, and **not published**. When `true`, they are published per the topic restriction above |

The flag is a bridge parameter in `/lib/lran-config/` and is settable at runtime from
HA over `lran/bridge/config/set` (§7.4, §16.2), with the same per-entry ACK and
`persist_status` semantics as any node parameter. It requires **no reflash and no
separate firmware build** — the point is to be able to enable bench visibility on a
running system during a development session and turn it off again afterwards.

> **Why a runtime flag rather than a build-time one.** A build-time switch means the
> bridge you debug against is not the bridge that runs the gate, which defeats the
> reason for accepting bench frames at all. A runtime flag keeps one binary in the
> field and makes the diagnostic surface an operational decision. Default-off means
> the safe state is the one you get by doing nothing.

**3. Discovery entities are marked diagnostic.** Where the bridge publishes HA
discovery configs for a simnode, they carry `entity_category: diagnostic` and a
`unique_id` prefix of `lran_simnode<N>_`, so they are segregated in the HA UI and
excluded from dashboards by default. Bench entities are **not** removed from HA when
the flag is cleared — the bridge publishes `offline` to their availability topic and
leaves the entities in place, so re-enabling does not churn `unique_id` registrations.

---

## 17. Reserved and future features

Specified but not used by any current node. Retained because a future node may need
them and because the design work is done.

### 17.1 Low-power RX duty-cycling and asymmetric preamble

**Not used by GateLink**, which has a 100 Ah pack behind it and runs continuous RX.
Retained because **WellLink may be battery powered** (**D19**) without that reserve.

**PV-aware adaptive profile.** A node reads its charge controller and uses PV output to
select a power profile, with **hysteresis** to avoid dawn/dusk flapping:

| Profile | Trigger | MCU | LoRa RX | Poll |
|---|---|---|---|---|
| Daytime | PV charging | awake | near-continuous | normal |
| Night/low-PV | PV fallen off | light-sleep | SX1262 hardware RX duty-cycle | reduced |

**Keeping duty-cycled RX responsive.** The bridge sends commands with an **extended
preamble** long enough to span the node's sleep window, so the sleeping receiver
detects the preamble on its next wake. Worst-case latency ≈ one duty-cycle period,
independent of sleep depth. Reference target: ≤ 2 s with a ~2 s duty-cycle period.

| Direction | Preamble | Rationale |
|---|---|---|
| Bridge → node, night profile | ≥ duty-cycle period (~2 s) | Node must catch the preamble on its next wake |
| Bridge → node, daytime | Short (8 symbols) | Node is in near-continuous RX |
| Node → bridge, always | Short (8 symbols) | Bridge is mains-powered and always listening |

**The expensive direction is the one that does not matter.** A 2-second preamble is
~489 symbols at SF9 — well within the SX1262's 16-bit preamble-length field — and every
bit of that airtime cost lands on the mains-powered bridge.

**This asymmetry belongs in the protocol layer, not just radio init**, because the two
directions use different preamble lengths on every transmission. The bridge must know
the node's current power profile to choose; it would come from a profile field in that
node's status schema. **On boot, before any status has arrived, the bridge MUST assume
night and use the long preamble** — the conservative choice costs airtime, the
optimistic choice costs a lost command.

**Multi-node caveat.** An extended preamble is address-agnostic: **every** duty-cycled
node on the channel wakes and receives the header before discarding a frame not
addressed to it. Enable SX126x hardware node-address filtering (§12.1) so the discard
happens in silicon.

> **Do not adopt this design for a node that does not need it.** Measured against
> adequate storage it buys single-digit percentages of the budget at real cost in
> complexity, latency and overnight data continuity. If WellLink turns out to be
> mains-powered (**D19**), this section stays unused.

If adopted, a power-profile field must be added to the adopting node's status schema —
a new schema ID, not a `ver` bump (§13.2).

### 17.2 Reserved identifiers

| Space | Reserved | For |
|---|---|---|
| `type` | `0x0C`–`0xFF` | Future message types |
| `schema` | `0x20`, `0x21` | WellLink status and event |
| `schema` | `0x30`–`0xEF` | Future node types |
| `src`/`dst` | `0x03`–`0xEF` | Future nodes |
| `src`/`dst` | `0xF0`–`0xF3` | Bench nodes `lran-simnode-0`…`-3`, provisioned (§5.3) |
| `src`/`dst` | `0xF4`–`0xFE` | Bench range, unassigned |
| `hdr_flags` | bits 6:0 | Optional-semantics header extensions (§5.8) |
| `hdr_flags` | bit 7 | **Assigned** — `CRITICAL_EXT` |
| header | bytes 13–15 | Header extension space (§5.9) |
| `ping_flags` | bits 7:1 | Future `PING` modes (§6.6) |

### 17.3 Loopback and simulation

Two loopback modes are required of every node build and are part of the protocol
contract, not an implementation detail, because they are how the framing is validated
without a peer:

- **RF loopback** — a node echoes a received `PING` with `src`/`dst` swapped, `seq` and
  echo bytes preserved. Validates link and framing without the real peer. Addressing
  makes this unambiguous: an echo is distinguishable from a genuine peer response.
- **Internal loopback** — serialized TX frames are fed straight back into the RX parser
  with no radio. This path has **no PHY CRC**, which is one of the reasons §2.1
  requires an application CRC16.

`simnode` targets register as `0xF0`–`0xF3` (§5.3), answer polls with schema `0xFE`
or `0xF0`, and emit events on demand — validating addressing, per-node keying, the
availability watchdog, fragmentation and CAD/backoff before a second real node exists.
Four provisioned bench IDs allow the multi-node contention test to run with three
simulated peers plus GateLink. Their MQTT exposure is governed by §16.6.

---

## 18. Open items

| # | Item | Blocks | Note |
|---|---|---|---|
| W1 | Final VE.Direct field list | §7.2.2 | The MPPT block covers the documented set for a 75/15. Confirm against live frames during VE.Direct bring-up; additions need a new schema ID, not a `ver` bump |
| W2 | ~~`movement_cause` values~~ | — | **Closed.** Cause is now derived locally from observed inputs; §8.5 lists exactly the distinctions the node can make |
| W3 | ~~Partial-open preset count~~ | — | **Closed.** Partial open is out of scope; the command is retired (§8.1) |
| W4 | **Test vectors** | implementation | Fixed key + known frames + expected MACs and CRCs, committed to `/lib/lran-protocol/test/`. **Needed before the node and bridge firmwares are developed independently** |
| W5 | **FCC Part 15 operating mode** | **D1**, TX power | §18.1 |
| W6 | **BMS `pack_ma` sign convention** | §7.2.3 | Bit `0x4000` is believed to be the discharge flag but has only ever been observed at 0.0 A. Capture once under charge and once under load. Tracked in the measurement backlog |
| W7 | Airtime table regeneration | §15.1 | Recompute once **D1** fixes SF/BW/CR. The v0.3 table corrects a systematic ~4 % understatement in v0.2 (omitted 4.25-symbol sync interval) and reflects the 16-byte header |
| W8 | **Header extension registry** | §5.8 | `hdr_flags` bit 7 is defined but denotes no extension yet. The first assignment must also define how a receiver identifies *which* extension is present — most likely from bits 6:0. Not needed until an extension exists, but the mechanism must be settled before one is designed |
| W9 | **Full-size `PING` bench run** | §6.6.1, §6.6.2 | The 222-byte frame path and the fragmented-`PING` reassembly path are specified but unexercised. Both belong in the bring-up sequence **before** GateLink is installed at the gate, since neither is fixable remotely |

### 18.1 On W5 — worth resolving before the range test

Part 15.247 digital-modulation operation generally requires at least 500 kHz of occupied
bandwidth. **LoRa at BW 125 kHz on a fixed channel does not meet that**, which is why
LoRaWAN US915 hops. A fixed-channel point-to-multipoint link at appreciable power may
therefore need either channel hopping or operation under the lower-power provisions of
15.249.

At the ~150 m range this system needs, low TX power is likely sufficient anyway, so this
may cost nothing — but it should be settled **before D1 fixes a TX power**, not after.
This is not a compliance determination; it is a flag that "respect FCC Part 15 limits"
is not specific enough to build against.

---

## 19. Reference layout summary

```
header (16 B, all frames)
  [ver][type][src][dst][seq:2][ctx_id:4][frag][schema][hdr_flags][rsv:3]

COMMAND      (30 B)  header + [cmd][arg][arg2:2]                 + mac:8 + crc:2
COMMAND_ACK  (24 B)  header + [ack_seq:2][result][detail][rsv:2]          + crc:2
POLL         (19 B)  header + [poll_flags]                                + crc:2
STATUS 0x10  (96 B)  header + [ 78-byte payload, §7.2 ]                   + crc:2
EVENT  0x11  (34 B)  header + [ 16-byte payload, §7.3 ]                   + crc:2
CONFIG 0x12  (var )  header + [op][count][ entries... ]          + mac:8 + crc:2
CONFIG_ACK   (var )  header + [op][persist][count][ results... ]          + crc:2
ERROR        (22 B)  header + [err_code][detail][ref_seq:2]               + crc:2
PING       (20+N B)  header + [ping_flags][n][data:N]                     + crc:2
HEX_REQ    (20+N B)  header + [flags][n][hex:N]              (+ mac:8)    + crc:2
HEX_RSP    (21+N B)  header + [status][n][hex:N]                          + crc:2
HEALTH 0xF0  (38 B)  header + [ 20-byte payload, §7.5 ]                   + crc:2
```

**Derived size constants** (§3.1) — one definition, in `/lib/lran-protocol/`:

```
LRAN_MAX_FRAME          222     LRAN_HDR_LEN             16
LRAN_CRC_LEN              2     LRAN_MAC_LEN              8
LRAN_MAX_PAYLOAD_AUTH   196     LRAN_MAX_PAYLOAD_PLAIN  204
LRAN_MAX_SCHEMA_PAYLOAD 196     LRAN_PING_MAX_ECHO      202
```

---

## 20. Changelog

- **v0.3** — Review revision; no deployed peer exists, so all changes land under
  `ver = 2` rather than bumping the wire version (see below). **Header grows
  12 → 16 bytes:** a new `hdr_flags` byte (§5.8) and three reserved bytes (§5.9),
  bought before first flash because §13.2 makes any later header change a `ver` bump
  and therefore a physical visit to every node on a fleet with no OTA. `hdr_flags`
  bit 7 is defined as **`CRITICAL_EXT`**, a must-understand marker that turns a future
  header extension from a silent misparse on an older node into a counted
  `ERROR(UNKNOWN_HDR_EXT)` discard — a bit that cannot be added retroactively, since
  an older node would ignore the very bit instructing it not to. §13.2 gains a
  corresponding row, §14 a stage 5a, §8.8 an `UNKNOWN_HDR_EXT` code, and §9.3 now
  covers the full 16-byte header so extensions are authenticated from the day they are
  defined. **Payload caps are now derived, not declared** (§3.1): `LRAN_MAX_FRAME` is
  the sole hand-chosen constant and every other limit computes from it, resolving the
  v0.2 inconsistency between a flat 200-byte payload cap and a 222-byte frame cap.
  **Bench node IDs are enumerated** — `0xF0`–`0xF3` as `lran-simnode-0`…`-3`, with
  `0xF4`–`0xFE` reserved-unassigned (§5.3) — because §9.1 derives keys per `node_id`
  against a static provisioning table, and a range cannot be provisioned. **New §16.6**
  defines how bench nodes appear on a production bridge: decoded and accepted, but
  published only under `diag` topics, never on `event` topics, gated by a new
  `simnode_diag_enable` bridge parameter that defaults **off** and is switchable at
  runtime from HA with no reflash. **`PING` may now fill the frame** (§6.6): `n`
  widened 64 → 202 so a maximum `PING` is exactly 222 bytes, exercising the largest
  buffer path the system can produce; `PING` becomes **fragmentable** (§6.6.2), making
  it the only mechanism that tests §11 reassembly over the air before a schema depends
  on it; and a `PATTERN_FILL` mode (§6.6.3) localizes a corrupted echo to a byte
  offset rather than merely detecting it. **`last_traversal_age_s` widened
  `uint16` → `uint32`** (§7.2.9): 16 bits saturated at 18 h 12 m, which a driveway
  reaches routinely and reaches precisely in the away-from-home condition the field
  exists to report. The field is added to the **microSD nonvolatile set** — without
  persistence its real ceiling is node uptime and the widening buys nothing — and the
  **bridge converts age to an absolute UTC timestamp** on receipt, so HA owns
  long-horizon history. `bms_age_s` stays `uint16` deliberately; it is a staleness
  gate, not a duration record. The v0.2 wording implying "traversal" meant a gate
  open/close cycle is corrected to **vehicle** traversal throughout. Status schema
  `0x10` grows 76 → 78 B and its block offsets shift accordingly (revised in place
  rather than minting a new schema ID, since it is unreleased). **§15.1 regenerated**
  for the new frame sizes and, separately, corrected for an omitted 4.25-symbol
  preamble sync interval that made every v0.2 figure ~4 % low — immaterial to the
  §15.2 energy conclusion, material to the §12.3 occupancy budget. Added open items
  **W8** (header extension registry) and **W9** (full-size and fragmented `PING` bench
  runs, required before GateLink is installed).
- **v0.2** — First release under the LRAN name; supersedes `lora-gatelink-wire-format`
  v0.1 and absorbs PRD v0.8 §6.4–6.8. **Rebuilt for a multi-node fleet:** 8-bit `src`
  and `dst` with a reserved bench range (§5.3), per-node HKDF keys (§9.1), per-node
  sequence and context tables (§10), explicit payload schema IDs (§7.1), fragmentation
  (§11), N/N−1 version tolerance (§13.1), CAD + randomized backoff media access
  (§12.3), and per-node availability (§16.5). **Header grows 6 → 12 bytes:** `ver` and
  `type` unpacked to a byte each, `src`/`dst` to a byte each, **`ctx_id` widened
  16 → 32 bits** (closing the replay-window limitation v0.1 §8.4 recorded), plus new
  `frag` and `schema` bytes. **MAC widened 4 → 8 bytes** (§9.3). **An application
  CRC16 is added** — a reversal of v0.1 §2.1, justified by fragmentation, non-radio
  frame paths and v0.1's own finding that byte count is not a constraint (§2.1).
  **`EVENT` is restored as a distinct type** — also a reversal — because held-open and
  FIRE alerts must reach HA as non-retained one-shot messages and carry an `event_id`
  for deduplication (§6.1). **The complete byte-level GateLink schemas are now
  defined**: status `0x10` (76 B), event `0x11` (16 B), config `0x12`, plus a generic
  node-health schema `0xF0` that makes a node observable before its application schema
  exists (§7). Added `HEX_REQ`/`HEX_RSP` for VE.Direct HEX transport with an
  authentication rule on write-class commands (§7.6), and `CONFIG`/`CONFIG_ACK` for
  authenticated runtime reconfiguration with honest persistence reporting (§7.4).
  Added **command deduplication on `(ctx_id, seq)`** because relay pulses are not
  idempotent (§10.4). Added a **§16 MQTT interface**: topic grammar, a hard
  never-retain rule for event topics, and publication policy placed on the bridge.
  Gate-side enumerations rebuilt on discrete accessory I/O rather than BusT4 —
  `gate_state` reduced to four observable states including `OPEN_HELD`, `movement_cause`
  derived locally (closing v0.1's W2), BusT4 result codes retired, and `ENTRY`/`EXIT`
  redefined for series-wired safety loops plus a separate exit wand (§8). Retained
  RX duty-cycling and the asymmetric preamble as a **reserved feature** for a possibly
  battery-powered WellLink (§17.1).
- **v0.1** — *(as `lora-gatelink-wire-format-v0.1`, 2026-07-23)* Initial byte-level
  spec for the two-node point-to-point link: 6-byte header with packed `ver`/`type` and
  `src`/`dst` nibbles, 16-bit `ctx_id`, 4-byte MAC on `COMMAND` only, no application
  CRC, `EVENT` collapsed into `STATUS` via `status_reason`, 36-byte status payload with
  a BusT4-derived gate block. Established the serialization rules, the receive-path
  discard-and-count discipline, the airtime analysis, and the finding that **airtime is
  not a power constraint** — all of which carry forward unchanged.
