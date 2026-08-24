# LRAN Protocol Library Implementation Plan

**Document:** `LRAN-Protocol-Library-Implementation-Plan`
**Version:** 0.1
**Artifact:** `/lib/lran-protocol/` — the shared codec
**Binding specification:** [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) v0.3
**Consumers:** `lran-bridge`, `lran-simnode`, `lran-gatelink`, `/tools/`
**Status:** Ready for build. **This is the first code written in the project.**
**Last updated:** 2026-08-19

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
enumerations, discard counters.

**Out of scope:** radio drivers, MQTT, scheduling, publication policy, node behaviour.
The library moves bytes and validates them. It does not decide anything.

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
was decoded from. The bridge's `lora_task` copies into a queue entry before handing off;
that copy is the task's responsibility, not the library's, and it is stated here so the
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
ladder, and the ordering *is* the specification — §14 exists precisely so that a frame is
rejected at the earliest possible stage and counted there. Two phases also let the bridge
count and log a frame it cannot fully parse, which is what makes a version-skew or
schema-skew problem diagnosable rather than merely visible as a rising discard count.

### 3.6 `lran/counters.h`

```cpp
namespace lran {

struct Counters {
  uint32_t rx_frames, tx_frames;
  uint32_t rx_crc_err, rx_runt, rx_bad_crc, rx_bad_ver, rx_not_addressed;
  uint32_t rx_unknown_hdr_ext, rx_unknown_type, rx_unknown_schema;
  uint32_t rx_bad_length, rx_bad_mac, rx_ctx_mismatch;
  uint32_t reassembly_timeout, fragment_overflow, cad_backoffs;

  void bump(Status);                 // the single mapping point
  uint32_t total_dropped() const;    // feeds schema 0xF0 rx_dropped
};

}  // namespace lran
```

`bump(Status)` being the only place the mapping exists is what guarantees the bridge and
every node report the same thing under the same name. `rx_crc_err` is the PHY CRC and is
bumped by the radio driver, not by the codec — it is the one counter the library cannot
own, and Bridge Impl Plan §10.5 records that it is also the one discard path that cannot
be tested at a desk.

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

---

## 4. Companion library: `/lib/lran-config/`

Hand-written C++ headers, one per node type plus one for the bridge. No generator, no
YAML. **The cost of that choice is that HA discovery payloads and
`/docs/gatelink-config.md` are now maintained by hand against this header**, so the header
must be self-documenting enough that a drift is obvious on inspection.

```cpp
namespace lran::config {

enum class PType : uint8_t { U8=1, U16=2, U32=3, I16=4, I32=5, Bool=6 };

struct ParamDef {
  uint16_t    id;
  const char* name;         // matches the HA entity object_id
  PType       type;
  int32_t     min, max, def;
  const char* unit;         // nullptr if unitless
  const char* doc;          // one line - this IS the documentation
};

// Bridge parameters
inline constexpr ParamDef kBridgeParams[] = {
  {0x0001, "poll_interval_s",        PType::U16,  10, 3600,  60, "s",  "Per-node poll period"},
  {0x0002, "command_ack_timeout_ms", PType::U16, 500, 30000, 3000,"ms", "ACK wait before retry"},
  {0x0003, "cmd_retries",            PType::U8,    0,    10,    3, nullptr, "Retries, SAME seq"},
  {0x0004, "missed_poll_threshold",  PType::U8,    1,    20,    3, nullptr, "Polls before offline"},
  {0x0005, "republish_interval_s",   PType::U16,  60, 86400,  900,"s",  "Heartbeat republish"},
  {0x0006, "mppt_write_arm_timeout_s",PType::U16, 30,  3600,  300,"s",  "HEX write arm expiry"},
  {0x0007, "simnode_diag_enable",    PType::Bool,  0,     1,    0, nullptr, "Publish bench nodes (Bridge Impl 4.2a)"},
};

constexpr const ParamDef* find(uint16_t id);   // constexpr - no runtime table build

}  // namespace lran::config
```

A `static_assert` verifies IDs are unique and ascending at compile time. That is the one
thing a generator would have given for free, and it is cheap to keep.

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

**P6 gates simnode B0. P7 gates bridge B2.**

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

- **v0.1** — Initial release. Created in response to the observation that
  `/lib/lran-protocol/` had three dependent consumers and no owning document, and that an
  API invented while writing the bridge would be inherited unreviewed by the simnode,
  GateLink and the host tooling. Specifies the six design rules and their consequences,
  the full API surface across nine headers, the two-phase decode and why it is split that
  way, `/lib/lran-config/` as hand-written headers per the decision to avoid a generator,
  the W4 vector format with an independent Python implementation, and seven milestones
  gating simnode B0 and bridge B2.
