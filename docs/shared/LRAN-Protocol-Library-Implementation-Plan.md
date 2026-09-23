# LRAN Protocol Library Implementation Plan

**Document:** `LRAN-Protocol-Library-Implementation-Plan`
**Version:** 0.16
**Artifact:** `/lib/lran-protocol/` — the shared codec
**Binding specification:** [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) **v0.13**
**Consumers:** `lran-bridge`, `lran-simnode`, `lran-gatelink`, `/tools/`
**Status:** **Built — P1 through P8 complete.** The record is
[`/docs/protocol-lib/engineering-log.md`](../protocol-lib/engineering-log.md); this document
remains the owning specification for the API and its tests.
**Last updated:** 2026-09-23

> **This library is the contract three firmware targets and the host tooling all depend
> on.** It is specified separately, and built first, because an API invented as a side
> effect of writing the bridge would be inherited by everything else without ever having
> been reviewed. Nothing else starts until this compiles and its tests pass.

---

## Table of contents

1. [Scope and design rules](#1-scope-and-design-rules)
2. [Repository placement and build integration](#2-repository-placement-and-build-integration)
3. [API surface](#3-api-surface)
4. [Companion library: `/lib/lran-config/`](#4-companion-library-liblran-config)
5. [Test vectors — W4](#5-test-vectors--w4)
6. [Milestones and acceptance criteria](#6-milestones-and-acceptance-criteria)
7. [Implementation notes and traps](#7-implementation-notes-and-traps)
8. [Changelog](#8-changelog)

---

## 1. Scope and design rules

**In scope:** frame encode/decode, header handling, CRC16, MAC computation and
verification, sequence arithmetic, fragmentation and reassembly, typed payload schemas,
enumerations, discard counters, and — since **D34** — the per-peer replay and
deduplication gate of Protocol Spec §9.4 steps 4–5.

**Out of scope:** radio drivers, MQTT, scheduling, publication policy, node behaviour.
The library moves bytes and validates them. **It does not decide anything** — and D34
does not change that. `CommandGate` returns a *verdict*; executing a command, and
choosing what to do when one is refused, stay with the caller. The test for whether
something belongs here is **"is it validation against receiver state, with no allocation,
no I/O and an injected clock"**, not "is it framing". `Reassembler` already passes it, and
the same test put steps 4–5 on this side of the line while dispatch stayed on
the other.

### 1.1 Six rules, each with a consequence

These are not style preferences. Each one exists because breaking it produces a specific
failure that is hard to find later.

| # | Rule | Consequence if broken |
|---|---|---|
| 1 | **No dynamic allocation anywhere.** All buffers caller-owned, all sizes compile-time | A heap fragmentation fault on a node 500 ft away with no OTA is unrecoverable |
| 2 | **No Arduino, no ESP-IDF, no platform headers in the core.** C++17 and `<cstdint>` only | The library must compile in the `native` environment, or none of it is unit-testable and the W4 vectors cannot be generated |
| 3 | **No time, no logging, no I/O.** Time is passed in as a `uint32_t now_ms` argument | A reassembly-timeout test that needs to wait five real seconds will not be written, and so the path will not be tested |
| 4 | **Crypto is injected through an interface**, not called directly | mbedTLS is an ESP-IDF component; calling it directly makes rule 2 impossible |
| 5 | **Explicit byte-wise serialization. No `memcpy` of structs, no packed attributes** | Spec §4.2. Struct-layout-dependent code works on two ESP32s and breaks the moment the host tooling is written |
| 6 | **Every failure returns a `Status` that maps to exactly one §14 stage and one counter** | Silent discards are the enemy of field debugging on a link with no console |

> **Rule 2 is the one under most pressure.** It will be tempting to reach for
> `Serial.print` while debugging, or for `millis()` in the reassembly code. Both compile
> on the target and both silently break the native build, which is where the tests live.
> If the native environment stops building, that is a defect to fix immediately, not a
> nuisance to work around.

---

## 2. Repository placement and build integration

Firmware targets are **separate PlatformIO projects**, so the shared libraries cannot
live inside any one of them.

```
lran/
  lib/
    lran-protocol/          <-- this document
      include/lran/
      src/
      test/                 Unity tests, run under the native environment
      library.json
      platformio.ini        native environment for standalone test runs
    lran-config/            §4
    lran-sim/               malformed-frame primitives (Bridge Impl Plan §5.4)
    vedirect/
  firmware/
    bridge/platformio.ini
    simnode/platformio.ini
    gatelink/platformio.ini
    rangetest/platformio.ini
  tools/
    simctl/                 Python
    vectors/                Python — W4 generation and checking
  docs/
  ha/
```

Each firmware project references the shared libraries by relative path:

```ini
lib_extra_dirs = ../../lib
```

> **Why `lib_extra_dirs` rather than a git submodule or a published package.** The
> library and its consumers change together during bring-up, and a submodule adds a
> commit-and-bump cycle to every edit. Revisit only if the library stabilizes and a
> second project outside this repo needs it.

**Framework:** `arduino` with the ESP-IDF component layer accessible, so `mbedtls` is
available to the platform MAC implementation without moving the whole project to IDF.

**Every firmware project must also build its `native` test environment in CI.** A
library that only compiles for ESP32-S3 has quietly acquired a platform dependency.

---

## 3. API surface

Namespace `lran`. Headers under `include/lran/`.

### 3.1 `lran/config.h` — derived constants

Spec §3.1 makes `LRAN_MAX_FRAME` the only hand-chosen number. That property must survive
into code, so **every other size is a `constexpr` computed from it** — not a second
literal that can drift.

```cpp
namespace lran {

inline constexpr size_t kMaxFrame           = 222;   // the only chosen number
inline constexpr size_t kHdrLen             = 16;
inline constexpr size_t kCrcLen             = 2;
inline constexpr size_t kMacLen             = 8;

inline constexpr size_t kMaxPayloadAuth     = kMaxFrame - kHdrLen - kMacLen - kCrcLen; // 196
inline constexpr size_t kMaxPayloadPlain    = kMaxFrame - kHdrLen - kCrcLen;           // 204
inline constexpr size_t kMaxSchemaPayload   = kMaxPayloadAuth;                          // 196
inline constexpr size_t kPingMaxEcho        = kMaxPayloadPlain - 2;                     // 202

inline constexpr uint8_t kProtoVer          = 2;
inline constexpr uint8_t kMaxFragments      = 15;

static_assert(kHdrLen == 16, "header layout changed - bump ver, see spec 13.2");
static_assert(kMaxPayloadAuth + kHdrLen + kMacLen + kCrcLen == kMaxFrame, "");

}  // namespace lran
```

The `static_assert` on `kHdrLen` is deliberate friction. A header resize is a `ver` bump
and a flag day across a fleet with no OTA; it should not be possible to do it by editing
one number without reading the comment.

### 3.2 `lran/types.h` — identifiers and status

```cpp
namespace lran {

using NodeId   = uint8_t;
using SchemaId = uint8_t;
using Seq      = uint16_t;
using CtxId    = uint32_t;

inline constexpr NodeId kNodeBridge    = 0x00;
inline constexpr NodeId kNodeGateLink  = 0x01;
inline constexpr NodeId kNodeWellLink  = 0x02;
inline constexpr NodeId kNodeSim0      = 0xF0;
inline constexpr NodeId kNodeSim3      = 0xF3;
inline constexpr NodeId kNodeBroadcast = 0xFF;

constexpr bool is_bench_node(NodeId id) { return id >= 0xF0 && id <= 0xFE; }

enum class MsgType : uint8_t {
  Command = 0x01, CommandAck = 0x02, Poll = 0x03, Status = 0x04,
  Event   = 0x05, Error      = 0x06, Ping = 0x07, HexReq = 0x08,
  HexRsp  = 0x09, Config     = 0x0A, ConfigAck = 0x0B,
};

// One value per failure. Maps 1:1 onto spec 14 stages and the counter set.
enum class Status : uint8_t {
  Ok = 0,
  Runt,             // 14 stage 2
  BadCrc,           // stage 3
  BadVersion,       // stage 4
  NotAddressed,     // stage 5
  UnknownHdrExt,    // stage 5a  <- spec 5.8
  UnknownType,      // stage 6
  UnknownSchema,    // stage 7
  BadLength,        // stage 8
  ReassemblyTimeout,// stage 9
  FragmentOverflow, // stage 9
  BadMac,           // 9.4
  CtxMismatch,      // 10.1
  BufferTooSmall,   // caller error, not a wire condition
  NotImplemented,
};

const char* to_string(Status);   // for logs. The ONLY string in the library.

}  // namespace lran
```

> **`Status` is exhaustive by construction.** Adding a discard reason means adding an
> enumerator, which makes every `switch` over it a compiler warning until the new case is
> handled — including the counter mapping and the bridge's diagnostic publication. That is
> the mechanism that keeps "discard silently" from ever being the default.

### 3.3 `lran/frame.h` — the frame view

```cpp
namespace lran {

struct Header {
  uint8_t  ver;
  MsgType  type;
  NodeId   src, dst;
  Seq      seq;
  CtxId    ctx_id;
  uint8_t  frag;        // high nibble index, low nibble total
  SchemaId schema;
  uint8_t  hdr_flags;   // bit 7 = CriticalExt
  uint8_t  reserved[3];

  uint8_t frag_index() const { return frag >> 4; }
  uint8_t frag_total() const { return frag & 0x0F; }
  bool    critical_ext() const { return hdr_flags & 0x80; }
};

// A NON-OWNING view into a caller-supplied buffer. Never allocates, never copies.
struct Frame {
  Header         hdr;
  const uint8_t* payload;
  size_t         payload_len;
};

}  // namespace lran
```

**`Frame` does not own its payload.** It points into the RX buffer the radio filled. This
is what keeps rule 1 satisfiable — but it means a `Frame` must not outlive the buffer it
was decoded from. The bridge's `lora_task` copies into a queue entry before handing off.
That copy is the task's responsibility, not the library's. It is stated here so the
lifetime rule is documented where the type is defined rather than discovered at runtime.

### 3.4 `lran/mac.h` — injected crypto

```cpp
namespace lran {

class IMac {
 public:
  virtual ~IMac() = default;
  // HMAC-SHA256 over `data`, truncated to the first kMacLen bytes.
  virtual void hmac_sha256_trunc(const uint8_t* key, size_t key_len,
                                 const uint8_t* data, size_t data_len,
                                 uint8_t out[kMacLen]) = 0;
};

// Key derivation, spec 9.1. Salt "lran-v1" is FIXED across ver bumps.
class IKdf {
 public:
  virtual ~IKdf() = default;
  virtual void derive_node_key(const uint8_t master[32], NodeId id,
                               uint8_t out[32]) = 0;
};

}  // namespace lran
```

Two implementations, neither in this library:

- `platform/esp32/mbedtls_mac.cpp` — `mbedtls_md_hmac`, `mbedtls_hkdf`
- `platform/native/refimpl_mac.cpp` — a public-domain SHA-256 for host tests and the
  W4 generator

> **The salt string is a compile-time constant with a comment saying not to change it.**
> It is a key-derivation domain separator, not a wire version; changing it silently
> invalidates every provisioned node in the field, and the symptom is "every command is
> rejected" with nothing pointing at the cause.

### 3.5 `lran/codec.h` — encode and decode

Decode is **two-phase**, because §14 requires staged checks with counting, and because
the MAC covers the header and so must be verified before the payload is interpreted.

```cpp
namespace lran {

struct DecodeCtx {
  NodeId   self;
  uint8_t  accept_ver_min, accept_ver_max;   // bridge: N-1..N. Node: N..N.
  IMac*    mac;                              // nullptr = skip MAC (bench only)
  const uint8_t* node_key;                   // key of the PEER, 32 bytes
  Counters* counters;                        // incremented on every failure
};

// Phase 1: length, CRC16, ver, dst, hdr_flags bit 7, type. Fills out->hdr.
// Does NOT validate schema, payload length, or MAC.
Status decode_header(const uint8_t* buf, size_t len, const DecodeCtx&, Frame* out);

// Phase 2: schema known, payload length matches (type, schema), MAC verified
// for authenticated types. Call only after decode_header returns Ok.
Status decode_payload(const uint8_t* buf, size_t len, const DecodeCtx&, Frame* inout);

struct EncodeCtx {
  IMac*          mac;
  const uint8_t* node_key;
};

// Writes a complete frame into `buf`. Returns bytes written via `out_len`.
// Sets reserved bytes and unused hdr_flags bits to zero (spec 4.3).
Status encode(const Header&, const uint8_t* payload, size_t payload_len,
              const EncodeCtx&, uint8_t* buf, size_t buf_cap, size_t* out_len);

}  // namespace lran
```

**Splitting decode is the single most consequential API choice here.** A one-shot
`decode()` forces every caller to either accept the library's ordering or reimplement the
ladder. The ordering *is* the specification: §14 exists so that a frame is rejected at the
earliest possible stage and counted there. Two phases also let the bridge
count and log a frame it cannot fully parse, which is what makes a version-skew or
schema-skew problem diagnosable rather than merely visible as a rising discard count.

### 3.6 `lran/counters.h`

```cpp
namespace lran {

struct Counters {
  uint32_t rx_frames, tx_frames;
  uint32_t rx_crc_err, rx_runt, rx_oversize, rx_bad_crc, rx_bad_ver;
  uint32_t rx_not_addressed, rx_unknown_hdr_ext, rx_bad_frag;
  uint32_t rx_unknown_type, rx_unknown_schema, rx_bad_length, rx_not_fragmentable;
  uint32_t rx_rejected_ctx, rx_rejected_mac;
  uint32_t rx_unknown_src;                       // spec v0.12 - 14 stage 9a
  uint32_t rx_reassembly_timeout, rx_fragment_overflow, rx_reassembly_abandoned;
  uint32_t rx_rejected_seq, rx_dup_command;      // D34 - raised by CommandGate
  uint32_t rx_frag_duplicate, rx_frag_late;      // counted, NOT in rx_dropped
  uint32_t cad_backoffs;

  void bump(Status);                 // the single mapping point
  uint32_t total_dropped() const;    // feeds schema 0xF0 rx_dropped
};

// spec 14.1 - the registry, in the specification's own order. The bridge publishes
// by these names, so this table is the single place a name is written down.
extern const CounterField kCounterRegistry[kCounterRegistryLen];

}  // namespace lran
```

**Field names are normative** — Protocol Spec §14.1 is the registry and this struct
mirrors it in order. The v0.5 revision exists because `rx_reassembly_timeout` and
`rx_fragment_overflow` shipped through P1–P5 without the `rx_` prefix the prose used, and
nothing anywhere listed the names together. `kCounterRegistry` is that list. A
`static_assert` on `sizeof(Counters)` fails the build if a field is added without a row.
`total_dropped()` sums only the registry rows §14.1 marks — `rx_frag_duplicate`,
`rx_frag_late` and `rx_dup_command` are normal traffic and must not make a health metric
climb during correct operation.

**`rx_unknown_src` arrived with spec v0.12** (§14 stage 9a): a frame from a `src` the
receiver holds no key for. It counts into `rx_dropped`, and it is never answered. The
registry is **22** rows, so `kCounterRegistryLen` and the `sizeof(Counters)`
`static_assert` both move; that assert is what makes the addition impossible to make
halfway. A node holds one peer, so the check is a single comparison.

`bump(Status)` being the only place the mapping exists is what guarantees the bridge and
every node report the same thing under the same name. It carries **no `default:` label**,
so adding a `Status` enumerator without a counter is a `-Werror=switch` build failure.
`rx_crc_err` is the PHY CRC and is bumped by the radio driver, not by the codec. It is the
one counter the library cannot own, and Bridge Impl Plan §10.5 records that it is also the
one discard path that cannot be tested at a desk.

### 3.7 `lran/seq.h` — serial-number arithmetic

```cpp
namespace lran {
// RFC 1982 comparison. NEVER use a plain > on Seq.
bool seq_newer(Seq a, Seq b);                 // true if a is newer than b
inline Seq seq_next(Seq s) { return static_cast<Seq>(s + 1); }
}
```

Four lines of code guarding a failure that presents as "the gate stopped responding to
commands after about two months and a reboot fixed it."

### 3.8 `lran/reassembly.h`

```cpp
namespace lran {

class Reassembler {
 public:
  // now_ms is PASSED IN (rule 3). Never calls millis().
  Status accept(const Frame&, uint32_t now_ms);
  bool   complete() const;
  const uint8_t* data() const;
  size_t len() const;
  void   tick(uint32_t now_ms);   // expires stale sets
  void   reset();

 private:
  uint8_t  buf_[kMaxSchemaPayload];
  uint16_t got_mask_;             // one bit per fragment index
  // ... key: (src, ctx_id, seq, schema)
};

}  // namespace lran
```

**One in-flight set per peer, not a pool.** Sixteen bits of `got_mask_` covers the
15-fragment maximum with a bit to spare, and a second concurrent set from the same peer is
a protocol violation rather than a case to support.

### 3.9 `lran/schema/` — typed payloads

One header per schema, each with a struct and a matched pair of functions:

```cpp
namespace lran::schema {

struct GateLinkStatusV1 {              // schema 0x10, 78 bytes
  uint8_t  gate_state, input_bits, hold, movement_cause;
  uint8_t  last_direction, detect_flags;
  uint32_t last_traversal_age_s;       // uint32 - spec 7.2.9
  // ... MPPT block, BMS block, node block
};

inline constexpr size_t kGateLinkStatusV1Len = 78;

Status serialize  (const GateLinkStatusV1&, uint8_t* out, size_t cap, size_t* written);
Status deserialize(const uint8_t* in, size_t len, GateLinkStatusV1* out);

}  // namespace lran::schema
```

Serialization goes through bounds-checked helpers, never raw pointer arithmetic:

```cpp
namespace lran {
class ByteWriter {
 public:
  ByteWriter(uint8_t* buf, size_t cap);
  bool u8(uint8_t);  bool u16(uint16_t);  bool u32(uint32_t);   // little-endian
  bool i8(int8_t);   bool i16(int16_t);   bool i32(int32_t);
  bool bytes(const uint8_t*, size_t);
  bool skip(size_t n);       // writes n zero bytes - use for reserved fields
  size_t written() const;
  bool ok() const;           // false if any call overran
};
class ByteReader { /* symmetric, bounds-checked */ };
}
```

> **`ByteWriter` returning `bool` and latching an `ok()` flag, rather than asserting,**
> lets a schema be written as a straight run of calls with one check at the end. The
> alternative — checking every call — produces schema code so noisy that offsets get
> transposed in the noise. Offset transposition is the most likely bug in this file, and
> the W4 vectors are what catch it.

**Each schema header carries the spec section number in a comment**, so a field's meaning
is one search away and the document stays the authority rather than the code.

### 3.10 `lran/command_gate.h` — replay and dedup (**D34**)

> **Corrected 2026-09-11 (v0.8), D34 as amended.** Until v0.7 this section had
> `record()` advance the `seq` high-water mark *after* execution, and rested that on a
> precondition — no frame arrives between `check()` and `record()` — that GateLink Impl
> Plan §5.2 contradicts. A retry inside the execution window would have **executed
> twice**. The reasoning is in [`LRAN-P8-CommandGate-Brief`](./LRAN-P8-CommandGate-Brief.md)
> (superseded) and Decision Register §3.2.1.

Protocol Spec §9.4 **steps 4 and 5, and step 6's state half**. One instance per peer,
called once per *completed set*, immediately after `Reassembler` and on authenticated
types only. Built as P8: `include/lran/command_gate.h`, `src/command_gate.cpp`,
`test/test_gate/`.

```cpp
namespace lran {

inline constexpr uint8_t kDedupCacheCapacity     = 32;  // spec 10.4's range, 1..32
inline constexpr uint8_t kDefaultDedupCacheDepth = 8;

enum class Verdict : uint8_t { Execute, ReturnCached, InFlight, Reject };

struct GateResult {
  Verdict   verdict;
  Status    status;         // Ok | DuplicateCached | DuplicateInFlight | RejectedSeq
  AckResult cached_result;  // valid ONLY when verdict == ReturnCached
  uint8_t   cached_detail;
};

class CommandGate {
 public:
  explicit CommandGate(Counters* counters = nullptr);

  // spec 10.4 - default 8, clamped to 1..32. Runtime-settable: no timing or sizing
  // constant is fixed at compile time in a node that cannot be reflashed without a
  // walk to the gate. Shrinking evicts oldest-first.
  void set_cache_depth(uint8_t n);

  // spec 9.4 steps 4-6. Step 4 BEFORE step 5, and the order is load-bearing: a
  // retry carries seq == high_water, which step 5 rejects. Checking seq first
  // answers REJECTED_SEQ to a frame that must receive the cached ACK.
  //
  // On Execute, step 6's state half is already done: the mark is at seq and the
  // entry is held IN FLIGHT. A retry before record() gets InFlight - never Execute.
  GateResult check(Seq seq);

  // Stores the result the application is about to ACK. Returns false, changing
  // nothing, if no in-flight entry for seq is held.
  bool record(Seq seq, AckResult result, uint8_t detail);

  // spec 10.1, 10.3, 10.6 - a new context invalidates every cached entry and the mark.
  void reset_context(CtxId new_ctx);

  // spec 10.6 - true while any entry awaits record(). A node refuses ROLL_CONTEXT
  // while this holds, because the roll would drop the result.
  bool any_in_flight() const;
};

}  // namespace lran
```

**Two calls, not one.** The cached value is the *result of execution*, so no single
call can produce it, and caching before execution would return a success ACK for a
command that then failed.

**The mark advances in `check()`, before dispatch** — the order spec §9.4 step 6 gives.
A command whose execution fails has still consumed its `seq`. That is correct, because
`seq` is attacker-visible and must not be reusable. The failure is recorded as its result,
so a retry receives the cached failure rather than a second attempt.

**Between `check()` and `record()` the entry is in flight.** A retry that finds it gets
`Verdict::InFlight` and `Status::DuplicateInFlight`, is counted in `rx_dup_command`, and
**the caller sends nothing** (spec §9.4, v0.11). The bridge's next retry lands after
`record()` and receives `DUPLICATE_CACHED` with the real result. **No threading
precondition remains**: the receive task and the executing task can be different, and
the caller need only serialize its own calls into one gate.

**An in-flight entry can be evicted** by `dedup_cache_depth` newer accepted commands, or by
shrinking the depth. Its retry then sits at or below the mark and step 5 refuses it —
never re-executed. `record()` returns `false` for it, and the caller should log that: the
bridge will read `REJECTED_SEQ` for a command that ran.

**Cost is 128 B of cache per peer**: 32 entries of `(seq, result, detail)` at 4 bytes,
sized for the top of the 1–32 range because static allocation cannot follow a runtime
depth. One in-flight bit per slot, a `ctx_id`, the mark, the ring indices and the
`Counters*` bring the object to the figure P8 recorded in
[`/docs/protocol-lib/engineering-log.md`](../protocol-lib/engineering-log.md). 640 B of
cache on a five-peer bridge.

**Three new `Status` values.** `DuplicateCached` and `RejectedSeq` are named after the
wire code per §14.1 and match `AckResult`. `DuplicateInFlight` has no wire code and
shares `rx_dup_command` with `DuplicateCached`, so its name follows the condition; it
is held apart so a field log does not read "DuplicateCached" for a frame that received
no answer. All three exist so `Counters::bump()` stays the single mapping point — its
missing `default:` label is a `-Werror=switch` guard that only works if every discard
reason is in the enum. `rx_rejected_seq` counts into `rx_dropped`; `rx_dup_command` does
not.

**Threading, stated once.** `CommandGate` holds no lock. A receiver that calls `check()`
from its receive task and `record()` from another must serialize those calls — a mutex,
or posting the result back to the receive task. The window the amendment closes is
between the calls, not inside one.

**`dedup_cache_depth`** joins `/lib/lran-config/` (§4) as a node parameter.

**A context roll does not pass through `check()`** (spec §9.4, §10.6, **D58**). A node that
receives `ROLL_CONTEXT` asks `any_in_flight()` first and answers `ACTUATOR_BUSY` while it
holds. Otherwise the node calls `reset_context()` with its new `ctx_id`, which clears the
cache and the mark as a resync does. The dispatch decision stays the application's, as for
every other command. **BF-34** built `any_in_flight()`, `Cmd::RollContext` (`0x12`) and
`kRollContextGuard` (`0xA5`). The guard has its own name, although its value is
`kRebootGuard`'s, so changing one guard cannot move the other. **An evicted in-flight
entry does not count as in flight.** Step 5 already refuses its retry, so a roll drops no
result that a retry could still receive.

---

## 4. Companion library: `/lib/lran-config/`

**One hand-written C++ table per owner, and every other copy derived from it by code**
(**D44**). Firmware defaults and HA `number` discovery read the table directly. A host tool,
built the way `tools/ha/dump_discovery.cpp` is, writes `/docs/gatelink-config.md`, and a
check diffs it, so the document cannot drift from the header. No generator, no YAML.

> **Changed in v0.10.** v0.1 through v0.9 chose hand-written headers and accepted that HA
> discovery payloads and the GateLink document would be *"maintained by hand against this
> header."* BF-23 then built discovery from the firmware's own `discovery.cpp` and
> generated `/ha/` from it, which showed the hand-written table and code-derived outputs
> are compatible. System PRD §9.4 had said *generated* throughout; D44 reconciles the two.

```cpp
namespace lran::config {

enum class PType : uint8_t { U8=1, U16=2, U32=3, I16=4, I32=5, Bool=6 };  // spec 7.4 ptype

// D56 - a parameter the node publishes but cannot yet apply answers a SET with
// READ_ONLY (spec 8.12, 12.4). Honest, and it costs one byte per row.
enum class Access : uint8_t { ReadWrite, ReadOnly };

// D47 - who holds the value, and so which topic sets it (spec 16.7.1).
enum class Owner : uint8_t {
  BridgeGlobal,   // lran/bridge/config/set; never carried by a frame
  BridgePerNode,  // lran/<node>/config/set; applied by the bridge, one value per node
  Node,           // lran/<node>/config/set; sent to the node as CONFIG
};

struct ParamDef {
  uint16_t    id;           // spec 7.4, D46 - one namespace, a block per owner
  const char* name;         // the HA object_id: permanent once published (spec 16.7)
  Owner       owner;
  Access      access;      // D56 - ReadOnly for a row the firmware cannot apply yet
  PType       type;
  int32_t     min, max, def;
  const char* unit;         // nullptr if unitless
  const char* doc;          // one line - this IS the documentation
};

// 0x0000-0x00FF - the bridge. Defaults are the firmware's as of BF-19/BF-19a/BF-20;
// ranges are proposed (see below).
inline constexpr ParamDef kBridgeParams[] = {
  {0x0001, "simnode_diag_enable",        Owner::BridgeGlobal, Access::ReadWrite,  PType::Bool,   0,     1,     0, nullptr, "Publish bench nodes, spec 16.6"},
  {0x0002, "diag_interval_s",            Owner::BridgeGlobal, Access::ReadWrite,  PType::U16,   10,  3600,    60, "s",  "Diagnostics publication period (BF-19)"},
  {0x0003, "missed_poll_threshold",      Owner::BridgeGlobal, Access::ReadWrite,  PType::U8,     1,    20,     3, nullptr, "Unanswered polls before offline, spec 16.5"},
  {0x0004, "poll_reply_timeout_ms",      Owner::BridgeGlobal, Access::ReadWrite,  PType::U16, 2000, 30000, 10000, "ms", "Poll outstanding before it counts as missed"},
  {0x0005, "command_ack_timeout_ms",     Owner::BridgeGlobal, Access::ReadWrite,  PType::U16,  500, 30000,  3000, "ms", "ACK wait before retry, Impl Plan 6.2"},
  {0x0006, "cmd_retries",                Owner::BridgeGlobal, Access::ReadWrite,  PType::U8,     0,    10,     3, nullptr, "Retries after the first, SAME seq"},
  {0x0007, "cad_retries",                Owner::BridgeGlobal, Access::ReadWrite,  PType::U8,     0,    10,     5, nullptr, "CAD attempts before transmitting regardless, spec 12.3"},
  {0x0008, "backoff_max_ms",             Owner::BridgeGlobal, Access::ReadWrite,  PType::U16,  100,  5000,  1500, "ms", "Upper bound of the random CAD backoff, spec 12.3"},
  {0x0009, "frag_reassembly_timeout_ms", Owner::BridgeGlobal, Access::ReadWrite,  PType::U16,  500, 30000,  5000, "ms", "Fragment set window, spec 11.2"},
  {0x000A, "error_min_interval_ms",      Owner::BridgeGlobal, Access::ReadWrite,  PType::U16,  100, 60000,  1000, "ms", "Floor between ERRORs to one peer, spec 14.2"},
  {0x000B, "config_readback_timeout_ms",  Owner::BridgeGlobal, Access::ReadWrite,  PType::U16, 1000, 60000, 15000, "ms", "Wait for a split readback to complete, spec 7.4.1 (D57)"},
  {0x000C, "config_ack_timeout_ms",      Owner::BridgeGlobal, Access::ReadWrite,  PType::U16, 1000, 60000,  8000, "ms", "CONFIG_ACK wait before the outcome is unknown, spec 7.4"},
  {0x0080, "poll_interval_s",            Owner::BridgePerNode, Access::ReadWrite, PType::U16,   10,  3600,    60, "s",  "Poll period for this node, BG-4"},
};

// 0x0100-0x01FF - every node. dedup_cache_depth is D34's, and is runtime-settable
// because a node that cannot be reflashed without a walk to the gate may not carry a
// fixed sizing constant either (root rule 8). Its max is the compiled cache size.
inline constexpr ParamDef kNodeCommonParams[] = {
  {0x0100, "dedup_cache_depth",          Owner::Node, Access::ReadWrite, PType::U8,     1,    32,     8, nullptr, "Cached command results, spec 10.4"},
  {0x0101, "frag_reassembly_timeout_ms", Owner::Node, Access::ReadWrite, PType::U16,  500, 30000,  5000, "ms", "Fragment set window, spec 11.2"},
  {0x0102, "cad_retries",                Owner::Node, Access::ReadWrite, PType::U8,     0,    10,     5, nullptr, "CAD attempts before transmitting regardless, spec 12.3"},
  {0x0103, "backoff_max_ms",             Owner::Node, Access::ReadWrite, PType::U16,  100,  5000,  1500, "ms", "Upper bound of the random CAD backoff, spec 12.3"},

  // D56 - the PHY, spec 12.1's table. Every node holds a copy and so does the bridge,
  // because one SX1262 listens on one configuration: a change is a fleet operation.
  // READ_ONLY until BF-33 builds spec 12.4's commit-and-revert, so HA can read the
  // working point before it can change it. The defaults are D1's.
  {0x0110, "freq_hz",                 Owner::Node, Access::ReadOnly, PType::U32, 902000000, 928000000, 917400000, "Hz", "Channel, spec 12.1 - fleet-wide"},
  {0x0111, "spreading_factor",        Owner::Node, Access::ReadOnly, PType::U8,     7,    12,     9, nullptr, "SF, spec 12.1 - fleet-wide"},
  {0x0112, "bandwidth_khz",           Owner::Node, Access::ReadOnly, PType::U16,  125,   500,   125, "kHz", "BW - 125 until an envelope decision, spec 18.2"},
  {0x0113, "coding_rate_denominator", Owner::Node, Access::ReadOnly, PType::U8,     5,     8,     5, nullptr, "CR 4/N, spec 12.1"},
  {0x0114, "tx_power_dbm",            Owner::Node, Access::ReadOnly, PType::I16,   -9,    -4,    -4, "dBm", "Conducted; the maximum IS D33's ceiling, spec 18.2"},
  {0x0115, "phy_trial_s",             Owner::Node, Access::ReadOnly, PType::U16,   30,   900,   120, "s",  "Revert window after a PHY change, spec 12.4"},
};

// 0x1000-0x1FFF GateLink and 0x2000-0x2FFF WellLink are declared by their own
// milestones, counted against spec 7.4's ceilings first (W10).

constexpr const ParamDef* find(uint16_t id);   // constexpr - no runtime table build

}  // namespace lran::config
```

A `static_assert` verifies that IDs are unique and ascending, and that each falls in its
owner's block. That is the one thing a generator would have given for free, and it is
cheap to keep.

**The bridge's list is an inventory of the firmware, not a design.** Each row is a value
the bridge already has. Most carry a `TODO(BF-23)` or `TODO(BF-26)` marker; the rest sit
behind `lora_configure()`, `lora_configure_errors()` or `command.h`'s defaults. v0.9's sketch
named `republish_interval_s` and `mppt_write_arm_timeout_s`; they are not here because
nothing implements them yet. BF-24 and BF-29 add them when they build the behaviour.

**The PHY rows are read-only until BF-33.** D56 brought spec §12.1's parameters into
runtime configuration under §12.4's commit-and-revert, and declaring them now lets Home
Assistant read the working point from the first release that carries the table. A `SET`
answers `READ_ONLY` (spec §8.12) until the trial-and-revert path exists, because a PHY
change that half-applies strands a node that has no OTA. **`tx_power_dbm`'s maximum is
D33's ceiling**, and **`bandwidth_khz` stays at 125** until an envelope decision; widening
either range is a decision, not a configuration change.

**GateLink's block, `0x1000`–`0x1FFF`, is not written yet.** It waits for the GateLink
milestone. **W10's count was run on 2026-09-20 and W10 is closed** (**D57**).

**What a readback costs, counted against spec §7.4's budget.** A `CONFIG_ACK` has 193
bytes for results, and a result entry is `5 + len`. Every node carries the node-common and
PHY blocks, so a node's readback is those plus its own block:

| Block | Rows | Bytes |
|---|---|---|
| Node-common, `0x0100`–`0x0103` | 4 | 26 |
| PHY, `0x0110`–`0x0115` | 6 | 42 |
| GateLink, named in its PRD and Implementation Plan today | 15 | 103 |
| **A GateLink readback, as the documents stand** | **25** | **171 of 193** |
| The rows GateLink PRD R-5.3a and R-5.4 imply but do not name | 31 | 211 of 193 |

The 15 named are `relay_pulse_ms`, `post_wake_settle_ms`, `command_confirm_timeout_s`,
`hold_confirm_ms`, `input_poll_ms`, `input_debounce_samples`, `detect_sequence_window_ms`,
`detect_sequence_idle_ms`, `held_open_alert_repeat_s`, `hex_timeout_ms`, `bms_poll_s`,
`charge_inhibit_confirm_s`, `display_timeout_s`, `unlock_settle_ms` and `cause_window_ms`.
The unnamed six are the dry-run switch (R-5.4a), the buzzer, injection spacing (R-5.4b),
VE.Direct staleness, `mppt_write_arm_timeout_s` and `republish_interval_s`.

**GateLink fits one frame today, with three `uint16` rows to spare**, and R-5.3a requires
*every* interval, window, threshold and debounce to be configurable. Spec §7.4.1 is what a
node does when the margin runs out: several `CONFIG_ACK` messages, marked `MORE_FOLLOWS`.

**Two findings from the count belong to GateLink's documents, not to this one**, and are
tracked in [`docs/gatelink/doc-findings.md`](../gatelink/doc-findings.md). GateLink
PRD §5.3.1 still lists the PHY parameters as not runtime-configurable, which **D56**
reversed, and those six rows are the whole margin above. The PRD also calls the transmit
power `tx_conducted_dbm` where this table calls it `tx_power_dbm`, and this table's name
becomes a permanent Home Assistant `object_id`.

**Built on 2026-09-20, and four things differ from the sketch above.** Each is recorded
here rather than left for a reader to find by diffing:

- **`PType`, `ParamStatus`, `ConfigOp` and `PersistStatus` come from `/lib/lran-protocol/`**
  rather than being declared again in `lran::config`. The sketch declared its own `PType`
  before the codec existed. Two enumerations for one concept is the drift D44 exists to
  stop, and the codec's is the one the wire already uses.
- **A value is `int32_t` inside the library**, named `Value`, and packed to its `ptype` on
  the wire. That covers every declared row; for `u32` it covers everything below 2^31,
  which `freq_hz`'s 928 MHz ceiling sits well under. A `static_assert` says so, because a
  `u32` parameter above that needs a wider representation before it can be declared.
- **`find()` is a member of a `Table` assembled from blocks**, not a free function over one
  array. A node's table is node-common plus its own; the bridge's is its own plus its
  per-node rows. `Table::add_block` refuses a block that would break ascending order,
  because spec §7.4.1's readback walk depends on it.
- **Persistence is injected as a `Persist` interface.** The library names neither NVS nor
  microSD, which is what keeps it building in `native` under root rule 7.

**`kMaxTableParams` is 64**, and a `static_assert` ties it to spec §7.4.1's bound of 4
messages: a table larger than 4 × 32 rows would carry rows no readback could ever reach,
and no test would catch it because those rows would simply never appear in an answer.

**Three things in the table are proposals for the operator to review before BF-32 codes
them**, because each becomes permanent the moment HA sees it:

- **The names.** Each is an HA `object_id` (spec §16.7). `diag_interval_s` drops the
  firmware's `g_` prefix; the rest are the names the firmware and the specification
  already use.
- **The ranges.** Every default is the firmware's, and no range is. Each range here was
  chosen to contain the default with room either side. None is derived from a
  measurement. A value outside its range is clamped and the clamp reported (spec §7.4).
- **Names shared by two owners.** `frag_reassembly_timeout_ms`, `cad_retries` and
  `backoff_max_ms` exist on the bridge and on every node, with different IDs. HA sees each
  as an entity of a different device, so the names do not collide there.

---

## 5. Test vectors — W4

**W4 is a prerequisite for everything, including simnode B0.** Bridge Impl Plan §9.3 sets
out why: simnode and the bridge link the same codec, so a bug in the codec is invisible to
any test that uses both ends of it. Both sides agree, everything passes, the frame is
wrong. The vectors are the only reference neither firmware can vote on.

**Format** — `/tools/vectors/vectors.json`, one object per case:

```json
{
  "name": "gatelink_status_nominal",
  "spec_ref": "7.2",
  "master_key_hex": "00112233...",
  "header": { "ver": 2, "type": 4, "src": 1, "dst": 0, "seq": 42,
              "ctx_id": 305419896, "frag": 1, "schema": 16,
              "hdr_flags": 0, "reserved": [0,0,0] },
  "payload_fields": { "gate_state": 2, "last_traversal_age_s": 4294967295, "...": 0 },
  "expect_frame_hex": "0204010...",
  "expect_status": "Ok"
}
```

Generated and checked by `/tools/vectors/` in Python, **with an independent
implementation of the framing** — not by calling into the C++ library through a binding.
A vector generated by the code under test proves only that the code is self-consistent.

**Minimum coverage before B0 is accepted:**

| Group | Cases |
|---|---|
| Nominal | One per message type, one per schema, MAC'd and plain |
| Boundary | Zero-length payload, `kMaxPayloadPlain`, 222-byte `PING`, `UINT32_MAX` traversal age, all sentinels |
| Sequence | Wrap through `0xFFFF`, RFC 1982 both directions |
| Fragmentation | 2-fragment and 15-fragment sets, in order and reversed |
| Malformed | One per `Status` enumerator except `Ok` and `BufferTooSmall` |
| Forward compat | Non-zero reserved bytes and `hdr_flags` bits 6:0 set — **must decode Ok** |

That last row is the one most likely to be skipped, because its expected result is that
nothing happens. It is also the row that protects the §5.9 extension space the header was
widened to provide.

---

## 6. Milestones and acceptance criteria

| # | Milestone | Acceptance |
|---|---|---|
| **P1** | **Skeleton and native build** | Library compiles in `native` with `-Wall -Wextra -Werror`. No Arduino or IDF header reachable from `include/lran/`. Unity harness runs and reports zero tests |
| **P2** | **Framing and CRC** | `encode`/`decode_header` round-trip. CRC16-CCITT-FALSE matches an independent implementation. All `Status` values reachable and asserted by a test |
| **P3** | **MAC and keying** | `IMac` native implementation matches a known-answer HMAC-SHA256 vector. HKDF derivation of a node key matches the Python side byte-for-byte |
| **P4** | **Schemas** | Every defined schema round-trips. **Offsets asserted against the spec tables field by field**, not merely round-tripped — a symmetric encoder and decoder agree on a wrong offset |
| **P5** | **Fragmentation and sequencing** | Reassembly across 2 and 15 fragments, out of order, with timeout expiry driven by an injected clock. RFC 1982 comparison exhaustively tested near the wrap |
| **P6** | **W4 vectors committed** | Every vector in §5 passes. **The vector generator and the library disagree nowhere.** Test run wired into CI |
| **P7** | **Target build** | Compiles for ESP32-S3 under the Arduino framework with the mbedTLS `IMac`. Flash and RAM footprint recorded in `/docs/protocol-lib/engineering-log.md` |
| **P8** | **`CommandGate` — D34, amended 2026-09-11** | §9.4 steps 4–5 and step 6's high-water update, per peer, **the mark advancing in `check()`**. Dedup returns the **cached** ACK without re-executing; `seq` below the high-water mark is refused; the step-4-before-step-5 order is asserted by a test that would fail if reversed. `reset_context()` clears the cache. Exhaustive `seq` tests near the wrap, as P5. `rx_rejected_seq` and `rx_dup_command` move, and `total_dropped()` includes the first and not the second. **Added by the amendment:** a second `check(s)` before `record(s)` returns `InFlight`, never `Execute`, and after `record(s)` returns the recorded result; a failed execution is cached and never retried; runtime depth changes evict oldest-first and never read beyond capacity. The suite runs on the ESP32-S3 as P7's does |

**P6 gates simnode B0. P7 gates bridge B2. P8 gates simnode B0 as well** — `ROLE_GATELINK`
accepts `COMMAND` and must deduplicate it.

**P8 is met, 2026-09-11**: 20 tests in `test/test_gate/`, **127 under `native` and 130
on the Heltec V3**, all passing, with the W4 vectors unchanged. Two deliberate mutations
confirm the suite can fail — advancing the mark in `record()` fails the window test, and
checking `seq` before the cache fails the order test. `sizeof(CommandGate)` is **148 B
on the ESP32-S3**. The engineering log's 2026-09-11 entry carries the detail. **Nothing
in this library now stands between simnode B0 and its start.**

> **Superseded by the paragraph above.** The next paragraph is the status as of
> 2026-08-30 and is left as written. Since then P8 was met (2026-09-11) and **W9 closed on
> 2026-09-05**, over RF on the range test firmware.

**P1–P7 are met** as of 2026-08-30, against specification **v0.6**: 107 tests under
`native` and 110 on the Heltec V3, 72 W4 vectors passing on host and on target with zero
divergence, and the ESP32-S3 footprint recorded in the engineering log. **W4 is closed**
(Protocol Spec §18). **P8 is outstanding**, and is the only library work between here and
simnode B0. **W12 is closed** — D34 placed §9.4 steps 4–5 here rather than outside, which
is what P8 builds; §9.2 makes every authenticated type bridge → node, so the obligation
binds the first firmware that accepts a `COMMAND`, not the range test. **W9** remains, and
needs the second board and an SX1262 driver — both arrive with the range test firmware.

---

## 7. Implementation notes and traps

**Little-endian is not a free assumption.** The ESP32-S3 is little-endian and the host is
almost certainly little-endian too, which means a byte-order bug will not show up on
either. Write the `ByteWriter` shifts explicitly (`buf[0] = v & 0xFF; buf[1] = v >> 8;`)
rather than casting a pointer — the cast works on both machines and is wrong.

**The MAC covers the reserved bytes.** Spec §9.3. A future extension is therefore
authenticated from the day it is defined, but it also means an encoder that leaves the
reserved bytes uninitialized produces a frame that fails verification intermittently,
depending on stack contents. `encode` zeroes them explicitly; do not optimize that away.

**`hdr_flags` bit 7 is validated; bits 6:0 are not.** This is the one exception to §4.3's
ignore-reserved rule and it is easy to implement as "validate the whole byte is zero,"
which breaks forward compatibility on the first optional extension. The `hdr_rsv` test
vector exists to catch exactly this.

**Do not add a `Frame::operator==`.** It invites comparing frames in tests, which passes
trivially when both sides use the same encoder. Compare against hex from the vector file
instead.

**Two counters look alike and are not.** `rx_crc_err` is the PHY CRC, bumped by the radio
driver. `rx_bad_crc` is the application CRC16, bumped by the codec. Conflating them
during bring-up will produce a confident and wrong conclusion about whether a link problem
is RF or software.

---

## 8. Changelog

- **v0.16** — **§3.10's D58 half is built** by **BF-34**: `CommandGate::any_in_flight()`,
  `Cmd::RollContext` and `kRollContextGuard`, with three `test_gate` cases. §3.10 records
  that an evicted in-flight entry does not hold a roll back.

- **v0.15** — **Protocol specification v0.12 → v0.13.** §4's table and store were built
  against v0.13's §7.4, §7.4.1 and §12.4, so they do not change. **D58** reaches §3.10: a
  context roll bypasses `check()` and calls `reset_context()`, and a node refuses the roll
  while `any_in_flight()` holds. Both `any_in_flight()` and `Cmd::RollContext` are planned
  for **BF-34** and not built. §5's vectors gain three `ROLL_CONTEXT` cases and move to
  v0.13 with every committed vector's bytes unchanged.

- **v0.14** — **`config_ack_timeout_ms` is added** at `0x000C`: how long the bridge waits
  for a `CONFIG_ACK` before it reports the outcome `unknown` (spec §7.4). The bridge had
  fixed it at 8000 ms with a setter nothing called, which root rule 8 does not allow. The
  default and name follow the bridge's `kConfigAckTimeoutDefaultMs` and
  `config_readback_timeout_ms`.

- **v0.13** — **§4 is built** as `/lib/lran-config/`, host-tested in `native` (BF-32's
  library half). §4 records the four places the implementation differs from its own sketch:
  the protocol's enumerations are reused rather than redeclared, a value is `int32_t`,
  `find()` belongs to a `Table` assembled from blocks, and persistence is injected. The
  store implements spec §7.4's three load-bearing properties and §7.4.1's readback walk.

- **v0.12** — **§4 counts a readback against spec §7.4's budget, which closes W10**
  (**D57**). A `CONFIG_ACK` has 193 bytes for results; GateLink's 25 named rows take 171 of
  them, and the rows its PRD implies but does not name take it to 211. §4 carries the table
  and names all of them. **`config_readback_timeout_ms` is added** at `0x000B`, the bridge's
  wait for an answer split across several `CONFIG_ACK` messages (spec §7.4.1). Two findings
  are recorded against GateLink's own documents rather than fixed here: PRD §5.3.1 still
  lists the PHY as not runtime-configurable after D56 reversed that, and it calls the
  transmit power `tx_conducted_dbm` where §4 calls it `tx_power_dbm`.

- **v0.11** — **§4 declares the PHY parameters** per **D56**: `freq_hz`,
  `spreading_factor`, `bandwidth_khz`, `coding_rate_denominator`, `tx_power_dbm` and
  `phy_trial_s`, held per node and by the bridge for its own radio. They are **read-only
  until BF-33** builds spec §12.4's commit-and-revert, so Home Assistant can read the
  working point before it can change it, and `ParamDef` gains an `access` field to carry
  that. `tx_power_dbm`'s maximum is D33's ceiling and `bandwidth_khz` stays at 125 until an
  envelope decision. GateLink's block is still unwritten; **W10 is closed by D57**, and §4
  carries the count that closed it.

- **v0.10** — **§4 rewritten for D44, D46 and D47, and the bridge's parameter list
  inventoried from the firmware.** The table stays hand-written, but nothing is maintained
  by hand against it any more: discovery and `/docs/gatelink-config.md` are derived by
  code. `ParamDef` gains an `owner`. IDs follow spec v0.13's blocks. The bridge's list
  replaces v0.9's sketch with the eleven values the firmware already has, and the node
  list gains `cad_retries` and `backoff_max_ms`. **Names and ranges are proposals** for
  the operator to review before BF-32 codes them. **The PHY parameters are declared** per
  **D56** — frequency, SF, BW, CR, TX power and `phy_trial_s`, per node — read-only until
  **BF-33** builds spec §12.4's commit-and-revert, and `ParamDef` gains an `access` field
  to say so. §6's 2026-08-30 status paragraph, which
  still read *"P8 is outstanding"* and *"W9 remains"*, is marked superseded, and a blank
  line that split the milestone table before P8 is removed. The binding citation stays at v0.12
  until spec v0.13's sweep.

- **v0.9** — **Protocol specification v0.11 → v0.12; `Counters` gains a field.**
  **`rx_unknown_src`** joins the struct and `kCounterRegistry` (spec §14 stage 9a, §14.1),
  making the registry **22** rows and moving both `kCounterRegistryLen` and the
  `sizeof(Counters)` `static_assert` — which is what makes a half-made addition fail the
  build. It counts into `rx_dropped`. §3.6 records it. Nothing else in the library moves:
  no frame layout, no schema, no authentication scope, and **no vector regenerates**.
  `CommandGate` is unaffected, though §6.3's answer for a `DUPLICATE_CACHED` result —
  the cached `result` travels in `detail` — is what its callers now build against.

- **v0.8** — **§3.10 corrected and P8 built, on D34 as amended 2026-09-11.** The operator
  accepted `LRAN-P8-CommandGate-Brief`'s recommendations in full. `check()` now advances
  the high-water mark, before dispatch, in spec §9.4's order; `Verdict` gains `InFlight`
  and `Status` gains `DuplicateInFlight` beside the planned `DuplicateCached` and
  `RejectedSeq`; `record()` returns `bool`; the atomicity precondition is withdrawn and
  replaced by the in-flight behaviour and one sentence on serializing calls. **The cost
  sentence was wrong for the parameter range it sat beside**: "32 B per peer" was the
  default depth, and static allocation has to size for 32 entries — 128 B. P8's acceptance
  row gains the brief's three tests. Binding specification **v0.10 → v0.11**, which
  answers the window §9.4 had recorded as a silence and changes nothing on the wire. **§6
  records P8 met**, on host and on the Heltec V3.

- **v0.7** — **§3.10 marked under review; nothing else changes.** A handoff review found
  that `CommandGate` as specified can execute a retried command twice: `record()` advances
  the high-water mark after execution, where spec §9.4 advances it before dispatch, and
  the single-threaded-receiver precondition that made the difference moot is contradicted
  by GateLink Impl Plan §5.2. §3.10 carries a banner pointing at
  [`LRAN-P8-CommandGate-Brief`](./LRAN-P8-CommandGate-Brief.md) and is **left as written
  until the operator decides** — correcting it here first would be deciding in the
  document that is supposed to receive the decision.

- **v0.6** — Citation refresh only. Protocol specification **v0.9 → v0.10**. **Nothing in
  this plan or in `/lib/lran-protocol/` changes**: `ver` stays at `2`, no frame layout,
  header field, enumeration value, schema or authentication scope moves, and **no W4 vector
  regenerates**, so §13.2's standing regeneration requirement is not triggered. v0.10 closes
  **D1** — 917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted — which is radio
  configuration, and **radio configuration is deliberately outside this library** (root
  `CLAUDE.md` rule 7: no Arduino header, no `millis()`, time passed in). The one number a
  caller must now carry is §12.3's **`backoff_max_ms` default of 1500**, raised from 500
  because a maximum `PING` at SF9 runs 1107 ms; it belongs to the node's media-access loop,
  not to the codec. **P8 (`CommandGate`, D34) is unaffected and remains the outstanding
  work.**

- **v0.5** — Citation refresh only. Protocol specification **v0.8 → v0.9**. **Nothing in
  this plan or in `/lib/lran-protocol/` changes, and that is the whole entry**: `ver` stays
  at `2`, no frame layout, header field, enumeration value, schema or authentication scope
  moves, and **no W4 vector regenerates**, so §13.2's standing regeneration requirement is
  not triggered. v0.9's content is regulatory and radio-parameter work — §18.2's §15.23
  framing, and §12.1 binding `BW` to the rule section — none of which the codec can see.
  The library remains validated against the wire definitions as they stood at
  specification v0.6, which v0.7, v0.8 and v0.9 have each left untouched. **P8
  (`CommandGate`, D34) is still the only outstanding library work.**
- **v0.4** — Citation refresh only. Protocol specification **v0.7 → v0.8**, which closes **W9** (the full-size and fragmented `PING` bench runs both passed over RF on 2026-09-05) and changes **no frame layout, header field, authentication scope or schema length**; no vector regenerates. **The library needed no change to close W9, which is the result worth recording here.** R9 drove `encode`, `encode_fragment`, `fragment_count`, `Reassembler`, `ping_fill_pattern` and `ping_check_pattern` from outside the library for the first time — every prior exercise of this API was its own test suite — and the P1–P7 surface covered the whole bench with nothing to report back. Fragmented reassembly has now run over the air, not only on host.
- **v0.3** — **`CommandGate` specified and P8 added**, implementing **D34**, which
  closes Protocol Spec **W12**. New **§3.10**. §1's scope gains the gate and, more
  usefully, states the *test* that put it here: not "is it framing" but **"is it
  validation against receiver state, with no allocation, no I/O and an injected clock"**
  — which `Reassembler` already satisfies, and which is what splits steps 4–5 from
  dispatch. §1's "it does not decide anything" is unchanged and now explicitly survives:
  the gate returns a verdict. **§3.6's `Counters` was three revisions stale** — it still
  listed `rx_bad_mac`, `rx_ctx_mismatch` and the unprefixed `reassembly_timeout` /
  `fragment_overflow` that v0.5 renamed and v0.6 renamed again, in a document whose whole
  claim is to be the API's owner. Corrected against §14.1 and `kCounterRegistry`, with
  the `-Werror=switch` and `static_assert` guards written down. §6 records **P1–P7 met,
  P8 outstanding**, and P8 gating simnode B0 alongside P6.
- **v0.2** — Status revision; **the API and the design rules are unchanged**. Binding
  specification moves **v0.3 → v0.6**, which is the version the library was actually
  built and tested against — the v0.4 errata and auth/fragmentation split, the v0.5
  counter registry and HKDF-from-HMAC requirement, and the v0.6 single-frame
  reassembly rule all landed in `/lib/lran-protocol/` while this document still cited
  v0.3. **§6 now records that P1–P7 are met**, so the header no longer reads "ready for
  build" for a library with 107 passing tests and a recorded target footprint, and
  names the two open items (**W12**, **W9**) that are the library's consumers' problem
  rather than the library's. Cross-document links repaired for the `docs/`
  reorganization.
- **v0.1** — Initial release. Created in response to the observation that
  `/lib/lran-protocol/` had three dependent consumers and no owning document, and that an
  API invented while writing the bridge would be inherited unreviewed by the simnode,
  GateLink and the host tooling. Specifies the six design rules and their consequences,
  the full API surface across nine headers, the two-phase decode and why it is split that
  way, `/lib/lran-config/` as hand-written headers per the decision to avoid a generator,
  the W4 vector format with an independent Python implementation, and seven milestones
  gating simnode B0 and bridge B2.
