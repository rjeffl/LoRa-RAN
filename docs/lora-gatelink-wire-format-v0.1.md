# LoRa GateLink — Wire Format Specification

**Document:** Wire Format v0.1 (draft)
**Protocol version on the wire:** `ver = 1`
**Companion to:** PRD v0.4 §6.4
**Status:** Draft for review. Blocks `/lib/lora-protocol/`, Phase 1 and Phase 2.
**Last updated:** 2026-07-23

---

## 1. Scope

Defines the byte-level format of every frame exchanged between `gatelink-gate`
and `gatelink-bridge` over the point-to-point LoRa link. This document is
authoritative for `/lib/lora-protocol/`; both firmwares and all bench tooling
link the same implementation of it.

**Out of scope:** MQTT topic structure and payloads (bridge → HA, JSON), BusT4
framing, VE.Direct text protocol.

### 1.1 Design constraints
| Constraint | Source | Consequence |
|---|---|---|
| Fixed binary, not JSON | PRD §6.4 | JSON exists only on the bridge→MQTT hop |
| Gate node is power-constrained | PRD §8 | Short frames; asymmetric preamble (§10.2) |
| Basic auth only, no strong security | PRD §2.2 | 32-bit truncated MAC, accepted limits in §8.4 |
| No cross-reboot sequence persistence | PRD §2.2 | Context-ID resync scheme (§9) |
| Two nodes only, ever | PRD §2.2 | Addressing is vestigial but retained (§5.2) |

---

## 2. Layer model

```
+---------------------------------------------+
|  Application  (gate FSM, MPPT cache, HA)    |
+---------------------------------------------+
|  GateLink frame   <-- THIS DOCUMENT         |
|  header | payload | MAC                     |
+---------------------------------------------+
|  LoRa PHY (SX1262)                          |
|  preamble | explicit header | payload | CRC |
+---------------------------------------------+
```

**The PHY provides, and this layer therefore does not duplicate:**
- Payload length (LoRa explicit header mode)
- **16-bit payload CRC, in hardware**
- Preamble detection and sync-word filtering

### 2.1 No application-layer CRC — and the dependency this creates
This spec defines **no CRC field.** The SX1262's hardware CRC covers the entire
GateLink frame, and for `COMMAND` frames the MAC provides integrity on top of it.
A second CRC would cost 2 bytes and ~6 ms of airtime at SF9 to re-check what is
already checked.

> **Mandatory radio configuration.** Both nodes MUST configure the SX1262 with
> **explicit header mode** and **CRC enabled**, and MUST discard any frame whose
> CRC status indicates an error. Disabling hardware CRC silently removes this
> layer's only integrity check on `STATUS`, `POLL`, and `EVENT` frames. This
> requirement belongs in the radio init path with a comment pointing here.

---

## 3. Frame structure

```
        0        1        2        3        4        5
    +--------+--------+--------+--------+--------+--------+
  0 | ver_ty |  addr  |      seq        |     ctx_id      |   <- header, 6 B
    +--------+--------+--------+--------+--------+--------+
  6 |                                                     |
    :                  payload  (0..N bytes)              :
    |                                                     |
    +--------+--------+--------+--------+
    |             mac (4 B, COMMAND only)                 |
    +--------+--------+--------+--------+
```

| Region | Size | Present |
|---|---|---|
| Header | 6 B | always |
| Payload | 0–200 B | per message type (§6) |
| MAC | 4 B | `COMMAND` only |

**Maximum frame size: 210 bytes.** LoRa allows 255; the cap leaves headroom and
makes buffer sizing static. Any frame exceeding it is a programming error, not a
runtime condition.

---

## 4. Serialization rules

1. **Little-endian** for all multi-byte integers, matching ESP32-S3 native order.
2. **Explicit byte-wise serialization. Do NOT `memcpy` a struct.** Structs are
   used as in-memory representations only; `serialize()` / `deserialize()`
   functions write and read fields individually.

   > This is not pedantry. `/tools/` will contain host-side simulators and
   > decoders (PRD §9) built with a different compiler on a different
   > architecture. Struct-layout-dependent code works on both ESP32s and breaks
   > the moment the bench tooling is written.

3. **Reserved fields and bits MUST be written as zero and MUST be ignored on
   receive.** Not validated as zero — that would break forward compatibility
   within a version.
4. **Strict length validation.** For a given `(ver, msg_type)` the payload length
   is fixed (except `PING`). A frame whose payload length does not match the
   expected value is discarded and counted, not parsed.

---

## 5. Header fields

### 5.1 `ver_ty` — byte 0
| Bits | Field | Notes |
|---|---|---|
| 7:4 | `ver` | Protocol version. **`1`** for this document. |
| 3:0 | `msg_type` | See §6. |

A frame with an unrecognized `ver` MUST be discarded and counted. A node MUST NOT
attempt best-effort parsing of an unknown version.

### 5.2 `addr` — byte 1
| Bits | Field | Notes |
|---|---|---|
| 7:4 | `src` | Sender node ID |
| 3:0 | `dst` | Recipient node ID |

| ID | Node |
|---|---|
| `0x0` | reserved / unassigned |
| `0x1` | `gatelink-bridge` |
| `0x2` | `gatelink-gate` |
| `0xF` | broadcast (loopback/bench use only) |

Addressing is vestigial in a two-node system, but it is retained because it makes
**RF loopback (PRD §9) unambiguous** — a node echoing a frame can be distinguished
from the real peer — and it costs one byte. A node MUST discard frames whose `dst`
is neither its own ID nor `0xF`.

### 5.3 `seq` — bytes 2–3, `uint16`
Per-direction monotonic counter. **Two independent sequence spaces** exist; see
§9.2. Wraps modulo 2^16.

### 5.4 `ctx_id` — bytes 4–5, `uint16`
The **gate node's current boot context ID**, regardless of frame direction. See
§9.1. Non-zero; `0x0000` is reserved to mean "unknown."

---

## 6. Message types and payloads

| `msg_type` | Name | Direction | Payload | MAC | Frame size |
|---|---|---|---|---|---|
| `0x0` | *reserved* | — | — | — | — |
| `0x1` | `COMMAND` | bridge → gate | 2 B | yes | 12 B |
| `0x2` | `COMMAND_ACK` | gate → bridge | 4 B | no | 10 B |
| `0x3` | `POLL` | bridge → gate | 1 B | no | 7 B |
| `0x4` | `STATUS` | gate → bridge | 36 B | no | 42 B |
| `0x5` | `EVENT` | *reserved* | — | — | — |
| `0x6` | `ERROR` | either | 4 B | no | 10 B |
| `0x7` | `PING` | either | 1 + N B | no | 7 + N B |

### 6.1 `EVENT` is deliberately unused — a simplification worth noting
PRD §6.4 lists `EVENT` as a distinct type. **This spec collapses it into
`STATUS`** via the `status_reason` field (§6.5, §7.6). Every unsolicited push —
gate state change, VE.Direct error, vehicle detection — is a `STATUS` frame that
says why it was sent.

Rationale: an `EVENT` type would carry substantially the same fields as `STATUS`,
producing two schemas to version, two decoders, and an ambiguity about which one
HA should trust when they disagree. One schema with a reason code has none of
that. `0x5` stays reserved in case a genuinely different payload shape is needed
later.

### 6.2 `COMMAND` — 2 bytes
| Offset | Type | Field |
|---|---|---|
| 0 | `uint8` | `cmd` — see §7.1 |
| 1 | `uint8` | `arg` — command-specific; `0` when unused |

Followed by a 4-byte MAC (§8).

### 6.3 `COMMAND_ACK` — 4 bytes
| Offset | Type | Field |
|---|---|---|
| 0 | `uint16` | `ack_seq` — `seq` of the `COMMAND` being acknowledged |
| 2 | `uint8` | `result` — see §7.2 |
| 3 | `uint8` | `detail` — BusT4 error code when `result` indicates a controller failure; else `0` |

The ACK reports **two distinct things** and callers must not conflate them:
`result = ACCEPTED` means the frame authenticated and the command was handed to
the 1050. It does **not** mean the gate moved. Motion is confirmed only by a
subsequent `STATUS` frame with `status_reason = GATE_STATE_CHANGE`.

### 6.4 `POLL` — 1 byte
| Offset | Type | Field |
|---|---|---|
| 0 | `uint8` | `poll_flags` — reserved, write `0` |

### 6.5 `STATUS` — 36 bytes

**Gate controller block — offsets 0–5**

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | `uint8` | `gate_state` | §7.3 — normalized enum |
| 1 | `uint8` | `gate_raw_state` | Raw 1050 state byte, passed through unmodified |
| 2 | `uint8` | `movement_cause` | §7.4; `UNKNOWN` if BusT4 does not expose it |
| 3 | `uint8` | `gate_flags` | §6.5.1 |
| 4 | `uint8` | `last_direction` | §7.5 |
| 5 | `uint8` | `bust4_err` | Last BusT4 error; `0` = none |

`gate_raw_state` exists so the diagnostic sensor (PRD §7.3) can surface states
the normalized enum flattens, and so sniff-derived discoveries can be interpreted
retroactively from logged data without a firmware change.

**MPPT block — offsets 6–29**

| Offset | Type | Field | Unit | Notes |
|---|---|---|---|---|
| 6 | `uint16` | `batt_mv` | mV | VE.Direct `V` |
| 8 | `int16` | `batt_ma` | mA | VE.Direct `I`; negative = discharge |
| 10 | `uint16` | `pv_cv` | 10 mV | VE.Direct `VPV`. **10 mV units, not mV** — the 75/15 accepts up to 75 V and mV would overflow `uint16` at 65.5 V |
| 12 | `uint16` | `pv_w` | W | VE.Direct `PPV` |
| 14 | `int16` | `load_ma` | mA | VE.Direct `IL`; `INT16_MIN` = not available |
| 16 | `uint16` | `yield_today` | 10 Wh | VE.Direct `H20` |
| 18 | `uint16` | `yield_yest` | 10 Wh | VE.Direct `H22` |
| 20 | `uint16` | `pmax_today` | W | VE.Direct `H21` |
| 22 | `uint32` | `yield_total` | 10 Wh | VE.Direct `H19`. **`uint32`** — `uint16` overflows at 655 kWh, reachable in a few years |
| 26 | `uint8` | `charge_state` | — | VE.Direct `CS`, passed through |
| 27 | `uint8` | `mppt_err` | — | VE.Direct `ERR`, passed through. Non-zero triggers a push |
| 28 | `uint8` | `mppt_tracker` | — | VE.Direct `MPPT`, passed through |
| 29 | `uint8` | `mppt_flags` | — | §6.5.2 |

VE.Direct code fields are **passed through unmodified rather than normalized.**
Victron owns those enumerations; re-mapping them here would create a translation
table to maintain in two places, and the bridge can map to text on the MQTT side
where it is cheap to change.

**Node diagnostics block — offsets 30–35**

| Offset | Type | Field | Notes |
|---|---|---|---|
| 30 | `uint32` | `uptime_s` | Seconds since boot |
| 34 | `uint8` | `power_profile` | `0` = daytime, `1` = night/low-PV (PRD §8.7) |
| 35 | `uint8` | `status_reason` | §7.6 — why this frame was sent |

#### 6.5.1 `gate_flags`
| Bit | Meaning |
|---|---|
| 0 | Inside loop asserted |
| 1 | Outside loop asserted |
| 2 | **Vehicle-detected-while-not-closed alert** (PRD §5.4.4) |
| 3 | Gate locked / BusT4 block active |
| 4 | BusT4 link healthy (frames seen within timeout) |
| 7:5 | Reserved — write `0` |

#### 6.5.2 `mppt_flags`
| Bit | Meaning |
|---|---|
| 0 | Load output on |
| 1 | **VE.Direct frame stale** — no complete frame within timeout |
| 7:2 | Reserved — write `0` |

Bit 1 matters: without it, a dead VE.Direct link is indistinguishable from a
healthy MPPT reporting unchanged values, and HA would show plausible stale data
indefinitely. The bridge SHOULD mark all MPPT entities unavailable when set.

### 6.6 `ERROR` — 4 bytes
| Offset | Type | Field |
|---|---|---|
| 0 | `uint8` | `err_code` — §7.7 |
| 1 | `uint8` | `detail` |
| 2 | `uint16` | `ref_seq` — `seq` of the offending frame, or `0` |

### 6.7 `PING` — 1 + N bytes
| Offset | Type | Field |
|---|---|---|
| 0 | `uint8` | `n` — echo payload length, 0–64 |
| 1 | `uint8[n]` | Arbitrary bytes, echoed verbatim by the responder |

Used for RF loopback and link testing (PRD §9). A responder swaps `src`/`dst`,
preserves `seq` and the echo bytes, and retransmits.

---

## 7. Enumerations

### 7.1 `cmd`
| Value | Name | `arg` |
|---|---|---|
| `0x00` | `NOP` | — |
| `0x01` | `OPEN` | — |
| `0x02` | `CLOSE` | — |
| `0x03` | `STOP` | — |
| `0x04` | `STEP_BY_STEP` | — |
| `0x05` | `PARTIAL_OPEN` | `1`–`3` = preset index |
| `0x06` | `LOCK` | — |
| `0x07` | `UNLOCK` | — |
| `0x10` | `REQUEST_STATUS` | — |
| `0x20` | `SET_POWER_PROFILE` | `0` = auto, `1` = force day, `2` = force night |
| `0x21` | `SET_DEBUG_MODE` | bitmask of debug modes (PRD §5.6) |
| `0x7F` | `REBOOT` | `0xA5` required as a confirmation guard |

`0x00`–`0x0F` are gate-motion commands and are the only values that reach the
1050. `0x10`+ are node-local. This split is deliberate: it lets the gate node
apply a stricter policy to motion commands without parsing semantics.

### 7.2 `result` (in `COMMAND_ACK`)
| Value | Name | Meaning |
|---|---|---|
| `0x00` | `ACCEPTED` | Authenticated and dispatched to the 1050 |
| `0x01` | `REJECTED_MAC` | MAC verification failed |
| `0x02` | `REJECTED_SEQ` | Sequence not greater than high-water mark (replay) |
| `0x03` | `REJECTED_CTX` | `ctx_id` mismatch — bridge must resync (§9.3) |
| `0x04` | `REJECTED_UNKNOWN_CMD` | Unrecognized `cmd` |
| `0x05` | `REJECTED_ARG` | Bad `arg` for this command |
| `0x10` | `BUST4_ERROR` | Dispatched but the 1050 reported failure; see `detail` |
| `0x11` | `BUST4_TIMEOUT` | No response from the 1050 |
| `0x12` | `BUST4_UNAVAILABLE` | BusT4 link down |

### 7.3 `gate_state`
| Value | Name |
|---|---|
| `0x00` | `UNKNOWN` |
| `0x01` | `CLOSED` |
| `0x02` | `OPEN` |
| `0x03` | `OPENING` |
| `0x04` | `CLOSING` |
| `0x05` | `STOPPED_PARTIAL` |
| `0x06` | `PARTIAL_OPEN` |
| `0x07` | `BLOCKED` |
| `0x08` | `FAULT` |

Maps to the HA `cover` states (PRD §7.1). `0x05`–`0x08` have no cover
equivalent and are surfaced through the auxiliary diagnostic sensor.

### 7.4 `movement_cause`
| Value | Name |
|---|---|
| `0x00` | `UNKNOWN` |
| `0x01` | `GATELINK` — our own command |
| `0x02` | `RADIO_REMOTE` |
| `0x03` | `WALL_BUTTON` |
| `0x04` | `LOOP` |
| `0x05` | `OVIEW` |
| `0x06` | `AUTO_CLOSE` |
| `0x07` | `SAFETY_REVERSE` |

Populated only if BusT4 exposes it (PRD §5.5, D3). Until sniffing confirms,
firmware emits `UNKNOWN` and this table is provisional.

### 7.5 `last_direction`
| Value | Name |
|---|---|
| `0x00` | `NONE` — no traversal recorded since boot |
| `0x01` | `ENTRY` — outside loop first |
| `0x02` | `EXIT` — inside loop first |
| `0x03` | `UNDETERMINED` — single loop only, or simultaneous |

### 7.6 `status_reason`
| Value | Name |
|---|---|
| `0x00` | `POLL_RESPONSE` |
| `0x01` | `GATE_STATE_CHANGE` |
| `0x02` | `MPPT_ERROR` |
| `0x03` | `VEHICLE_DETECTED` |
| `0x04` | `BOOT` |
| `0x05` | `POWER_PROFILE_CHANGE` |
| `0x06` | `BUST4_LINK_CHANGE` |
| `0x07` | `DEBUG_SYNTHETIC` |

`DEBUG_SYNTHETIC` marks frames produced by the dummy-status-push tool (PRD §9).
**The bridge MUST propagate this marking to MQTT** so synthetic data is never
mistaken for real in HA history.

### 7.7 `err_code`
| Value | Name |
|---|---|
| `0x01` | `BAD_VERSION` |
| `0x02` | `BAD_LENGTH` |
| `0x03` | `UNKNOWN_TYPE` |
| `0x04` | `CTX_MISMATCH` |
| `0x05` | `NOT_ADDRESSED` |

---

## 8. Authentication

### 8.1 Scope
`COMMAND` frames only. `STATUS`, `POLL`, `COMMAND_ACK`, `ERROR`, and `PING` are
unauthenticated, per PRD §2.2 and §6.4.

### 8.2 Computation
```
mac = HMAC-SHA256( key, ver_ty || addr || seq || ctx_id || payload )[0..3]
```

Little-endian for `seq` and `ctx_id`, exactly as on the wire.

**This covers the full header, not just `(ctx_id, seq, cmd)` as sketched in PRD
§6.4.** Including `ver_ty` and `addr` costs nothing and prevents an attacker from
altering message type or addressing on a captured frame. There is no reason to
authenticate less than everything present.

`key` is a 32-byte shared secret provisioned at build time via untracked config
(PRD §10). `mbedtls_md_hmac` from ESP-IDF (Apache-2.0) provides the primitive.

### 8.3 Verification
The gate node MUST, in this order:
1. Check `ver`, `dst`, `msg_type`, payload length. Reject → `ERROR`.
2. Check `ctx_id` equals its own. Mismatch → `COMMAND_ACK(REJECTED_CTX)`.
3. Verify MAC in **constant time**. Failure → `COMMAND_ACK(REJECTED_MAC)`.
4. Check `seq > rx_high_water`. Failure → `COMMAND_ACK(REJECTED_SEQ)`.
5. Update `rx_high_water = seq`, dispatch.

Step 3 before step 4 is deliberate: `seq` is attacker-visible, so checking it
before authenticating would let an unauthenticated party learn the high-water
mark from timing or response differences.

### 8.4 Accepted limitations — state these plainly
A 32-bit MAC gives ~1 in 4.3 billion forgery odds per attempt, and monotonic
`seq` means an attacker gets **one attempt per sequence value** — a valid forgery
also burns the sequence number it used. This is proportionate to the stated bar.

Known and accepted:
- **`ctx_id` is 16-bit.** A reboot has a ~1-in-65535 chance of reusing a value an
  attacker previously captured, briefly reopening a replay window. Widening to
  32 bits costs 2 bytes and ~6 ms at SF9 and is the obvious upgrade if the
  security posture is ever raised.
- **`STATUS` is unauthenticated.** An attacker can inject false status into HA —
  a false "gate open" reading, for instance. There is no actuation path, but any
  HA automation triggering on gate state inherits this. Worth remembering before
  writing an automation that unlocks a door on `ENTRY`.
- **No confidentiality.** All frames are plaintext. Gate state and traffic
  patterns are observable to anyone with an SDR.

---

## 9. Sequencing and replay

### 9.1 Context ID
On boot, the **gate node** generates a random non-zero `uint16` `ctx_id` and uses
it in every frame it sends. The bridge learns it from any received gate frame and
echoes it in every frame it sends.

`ctx_id` replaces cross-reboot sequence persistence (PRD §2.2 non-goal): a
gate reboot produces a new context, invalidating every previously captured
command.

### 9.2 Two independent sequence spaces
| Space | Owner | Purpose | On gate reboot |
|---|---|---|---|
| Command `seq` | bridge | **Replay protection** — strictly increasing, security-relevant | Bridge resets to `1` on learning a new `ctx_id` |
| Status `seq` | gate | **Ordering and dedup only** — not security-relevant | Resets to `1` |

The bridge MUST treat status `seq` as advisory. It is useful for discarding
duplicates within a short window and for detecting loss in diagnostics. It MUST
NOT be used to reject frames, because a status frame arriving out of order is
still current data.

### 9.3 Resync procedure
1. Gate receives a `COMMAND` whose `ctx_id` does not match → replies
   `COMMAND_ACK(REJECTED_CTX)` carrying its **own** `ctx_id` in the header.
2. Bridge sees `REJECTED_CTX`, adopts the `ctx_id` from that ACK, resets its
   command `seq` to `1`, and retries the original command once.
3. If a second `REJECTED_CTX` follows, the bridge stops retrying and publishes
   an availability/diagnostic fault rather than looping.

Step 3 exists to prevent a resync loop from becoming a transmit storm, which on a
duty-cycled night link would be both a power and a latency problem.

### 9.4 Wrap behavior
`seq` wraps modulo 2^16. At one command per minute, wrap takes ~45 days of
continuous commanding, and any gate reboot resets both counters. Comparison MUST
use serial-number arithmetic (RFC 1982 style) rather than a plain `>`, so that a
wrap does not cause the gate to reject every subsequent command until reboot.

---

## 10. Radio configuration

### 10.1 Required settings
| Parameter | Value | Note |
|---|---|---|
| Frequency | 915 MHz band | Exact channel per D1 |
| Header mode | **Explicit** | Required by §2.1 |
| CRC | **Enabled** | Required by §2.1 |
| Sync word | Private (`0x12` / SX126x `0x1424`) | Not the LoRaWAN value |
| SF / BW / CR | Per D1 | §11 gives the airtime consequences |

### 10.2 Asymmetric preamble — a consequence worth stating explicitly
PRD §8.8 requires the bridge to use an extended preamble spanning the gate's RX
sleep window. **This asymmetry belongs in the protocol layer, not just the radio
init**, because the two directions use different preamble lengths on every
transmission:

| Direction | Preamble | Rationale |
|---|---|---|
| Bridge → gate, night profile | ≥ duty-cycle period (~2 s) | Gate is duty-cycling; must catch the preamble on its next wake |
| Bridge → gate, daytime | Short (8 symbols) | Gate is in near-continuous RX |
| Gate → bridge, always | Short (8 symbols) | Bridge is mains-powered and always listening |

**The expensive direction is the one that doesn't matter.** A 2-second preamble
is ~489 symbols at SF9 — well within the SX1262's 16-bit preamble length field —
and every bit of that airtime cost lands on the mains-powered bridge. The gate
node never transmits a long preamble.

This implies the bridge must know the gate's current power profile to choose
preamble length. It gets it from `status.power_profile` (§6.5). On boot, before
any status has arrived, the bridge MUST assume **night** and use the long
preamble — the conservative choice costs airtime, while the optimistic choice
costs a lost command.

---

## 11. Airtime and power analysis

Computed for BW 125 kHz, CR 4/5, explicit header, CRC on, 8-symbol preamble.

### 11.1 Airtime per frame
| Frame | Bytes | SF7 | SF8 | SF9 |
|---|---:|---:|---:|---:|
| `COMMAND` | 12 | 41 ms | 82 ms | 144 ms |
| `COMMAND_ACK` | 10 | 41 ms | 82 ms | 144 ms |
| `POLL` | 7 | 33 ms | 66 ms | 115 ms |
| **`STATUS`** | **42** | **87 ms** | **154 ms** | **288 ms** |
| Night `COMMAND` w/ 2 s preamble | 12 | ~2.03 s | ~2.06 s | ~2.14 s |

### 11.2 The finding: airtime is not a power constraint here
A `STATUS` frame at SF9 costs ~288 ms of TX. At ~100 mA on the 5 V rail (~50 mA
referred to the 12 V battery), that is **~4 µAh per frame.**

| Status rate | Frames/day | Battery cost |
|---|---:|---:|
| 1 / 5 min | 288 | 1.2 mAh/day |
| 1 / min | 1440 | **5.8 mAh/day** |

Against the PRD §8.2 total of **7.3 Ah/day**, even the aggressive rate is
**under 0.1%.** For comparison, the 2×2 W LED lights consume roughly *700 times*
as much.

**Design implication:** do not optimize this schema for byte count. The 36-byte
`STATUS` payload could be bit-packed to perhaps 24 bytes, saving ~1.7 mAh/day —
approximately nothing — in exchange for a decoder that is materially harder to
debug from a serial log. Airtime matters here for **latency, collision
avoidance, and the duty-cycle interaction (§10.2)**, not for the energy budget.

This also settles a live question: SF9 is affordable if the range test wants it.
Choose SF on link margin alone (D1), not on power.

---

## 12. Receive-path handling

A receiver MUST process in this order, discarding and **counting** at each stage:

| Stage | Check | On failure |
|---|---|---|
| 1 | PHY CRC status | Discard silently; increment `rx_crc_err` |
| 2 | Length ≥ 6 | Discard; `rx_runt` |
| 3 | `ver` known | Discard; `rx_bad_ver`; optionally `ERROR(BAD_VERSION)` |
| 4 | `dst` is self or `0xF` | Discard; `rx_not_addressed` |
| 5 | `msg_type` known | `ERROR(UNKNOWN_TYPE)` |
| 6 | Payload length matches type | `ERROR(BAD_LENGTH)` |
| 7 | Type-specific handling | §8.3 for `COMMAND` |

Every counter is exposed as a bridge diagnostic. **Silent discards are the
enemy of field debugging** on a link with no console access — the counters are
how a marginal link is distinguished from a firmware bug when the gate is 500 ft
away in the rain.

---

## 13. Versioning policy

- Any change to a payload struct — field added, removed, resized, or reordered —
  **requires a `ver` bump.** There is no in-version extensibility mechanism.
- Reserved bits and reserved fields may be assigned meaning **within** a version,
  because §4.3 requires receivers to ignore them.
- Both nodes are flashed together over USB (PRD §2.2, no OTA), so mixed-version
  operation is a bench and development condition, not a field condition. It must
  fail loudly and diagnosably, not degrade.

---

## 14. Open items

| # | Item | Blocks | Note |
|---|---|---|---|
| W1 | Final VE.Direct field list | §6.5 MPPT block | PRD §7.3 says "all available fields"; the block above covers the documented set for a 75/15. Confirm against live frames in Phase 3 and bump `ver` if additions are needed. |
| W2 | `movement_cause` values (§7.4) | §7.4 | Provisional until BusT4 sniffing (D3). Table is a guess at what the 1050 distinguishes. |
| W3 | Partial-open preset count | §7.1 `arg` | Assumes 3. Confirm against the 1050. |
| W4 | Test vectors | implementation | Fixed key + known frames + expected MACs, committed to `/lib/lora-protocol/test/`. Needed before the two firmwares are developed independently. |
| W5 | **FCC Part 15 operating mode** | D1, TX power | See below. |

### 14.1 On W5 — worth resolving before the range test
Part 15.247 digital-modulation operation generally requires at least 500 kHz of
occupied bandwidth. **LoRa at BW 125 kHz on a fixed channel does not meet that**,
which is why LoRaWAN US915 hops. A fixed-channel point-to-point link at
appreciable power may therefore need either channel hopping or operation under
the lower-power provisions of 15.249.

At the ~150 m range this system needs, low TX power is likely sufficient anyway,
so this may cost nothing — but it should be settled **before** D1 fixes a TX
power, not after. I am not a lawyer and this is not a compliance determination;
it is a flag that the current PRD language ("respect FCC Part 15 limits") is not
specific enough to build against.

---

## 15. Reference layout summary

```
COMMAND   (12 B)  [ver_ty][addr][seq:2][ctx_id:2][cmd][arg][mac:4]
CMD_ACK   (10 B)  [ver_ty][addr][seq:2][ctx_id:2][ack_seq:2][result][detail]
POLL       (7 B)  [ver_ty][addr][seq:2][ctx_id:2][poll_flags]
STATUS    (42 B)  [ver_ty][addr][seq:2][ctx_id:2][ 36-byte payload, §6.5 ]
ERROR     (10 B)  [ver_ty][addr][seq:2][ctx_id:2][err_code][detail][ref_seq:2]
PING    (7+N B)   [ver_ty][addr][seq:2][ctx_id:2][n][data:N]
```
