// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// P6 / W4 - the independent witness. Spec 13.2.
//
// Every other suite in this project checks the codec against itself. This one checks
// it against /tools/vectors/, generated in Python from the specification prose by a
// party that never read this codec. Where the two disagree, the disagreement is the
// finding and is adjudicated against the spec text - see the engineering log.

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <unity.h>

#include <cstdio>
#include <cstring>

#include "lran/lran.h"
#include "vectors_data.h"
#include "refimpl_mac.h"
#include "test_key.h"

using namespace lran;
using namespace lran_vectors;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefMac g_mac;
refimpl::RefKdf g_kdf;

// The vectors name types by their spec 6 name, not their numeric value: the
// name-to-value mapping is itself part of what is being witnessed, so a number in
// the file would hide a disagreement about it.
bool type_from_name(const char* s, MsgType* out) {
  struct Row { const char* name; MsgType type; };
  static const Row kRows[] = {
      {"COMMAND", MsgType::Command},     {"COMMAND_ACK", MsgType::CommandAck},
      {"POLL", MsgType::Poll},           {"STATUS", MsgType::Status},
      {"EVENT", MsgType::Event},         {"ERROR", MsgType::Error},
      {"PING", MsgType::Ping},           {"HEX_REQ", MsgType::HexReq},
      {"HEX_RSP", MsgType::HexRsp},      {"CONFIG", MsgType::Config},
      {"CONFIG_ACK", MsgType::ConfigAck},
  };
  for (const Row& r : kRows) {
    if (strcmp(s, r.name) == 0) {
      *out = r.type;
      return true;
    }
  }
  return false;
}

bool status_from_name(const char* s, Status* out) {
  struct Row { const char* name; Status s; };
  static const Row kRows[] = {
      {"Ok", Status::Ok},
      {"Runt", Status::Runt},
      {"Oversize", Status::Oversize},
      {"BadCrc", Status::BadCrc},
      {"BadVersion", Status::BadVersion},
      {"NotAddressed", Status::NotAddressed},
      {"UnknownHdrExt", Status::UnknownHdrExt},
      {"BadFrag", Status::BadFrag},
      {"UnknownType", Status::UnknownType},
      {"UnknownSchema", Status::UnknownSchema},
      {"BadLength", Status::BadLength},
      {"NotFragmentable", Status::NotFragmentable},
      {"ReassemblyTimeout", Status::ReassemblyTimeout},
      {"ReassemblyAbandoned", Status::ReassemblyAbandoned},
      {"FragmentOverflow", Status::FragmentOverflow},
      {"BadMac", Status::BadMac},
      {"CtxMismatch", Status::CtxMismatch},
  };
  for (const Row& r : kRows) {
    if (strcmp(s, r.name) == 0) {
      *out = r.s;
      return true;
    }
  }
  return false;
}

// Reading a counter by the name the vector file uses. The negative vectors assert
// that exactly ONE counter moved, which is the assertion that keeps two different
// faults from quietly sharing a bucket - the reason v0.4 split rx_oversize out of
// rx_bad_length and rx_reassembly_abandoned out of rx_reassembly_timeout.
void report(const char* group, const char* vec, const char* what);

// The name -> field mapping comes from lran::kCounterRegistry (spec 14.1) rather
// than a copy kept here. A counter renamed in the library then fails these vectors
// by name instead of silently matching a stale local table.
bool assert_only_counter(const Counters& c, const char* want, const char* vec_name,
                         bool exactly_one) {
  bool known = false;
  for (const CounterField& r : kCounterRegistry) {
    if (strcmp(r.name, want) == 0) known = true;
  }
  if (!known) return false;
  for (const CounterField& r : kCounterRegistry) {
    const uint32_t v = c.*(r.field);
    const bool     is_target = (strcmp(r.name, want) == 0);
    if (is_target) {
      // A negative vector is one frame and so exactly one discard. A fragmentation
      // vector delivers a whole set, and a counter may legitimately move once per
      // fragment - there, what matters is that no OTHER counter moved.
      if (exactly_one ? (v != 1) : (v == 0)) {
        char msg[192];
        snprintf(msg, sizeof(msg), "%s should be %s, was %u", r.name,
                 exactly_one ? "1" : "non-zero", v);
        report("counter", vec_name, msg);
      }
    } else if (v != 0) {
      char msg[192];
      snprintf(msg, sizeof(msg), "%s moved too (%u); expected only %s", r.name, v, want);
      report("counter", vec_name, msg);
    }
  }
  return true;
}

// Builds a Header from a vector's declared header.
bool header_from(const HdrSpec& h, Header* out) {
  MsgType type;
  if (!type_from_name(h.type, &type)) return false;
  out->type      = type;
  out->ver       = h.ver;
  out->src       = h.src;
  out->dst       = h.dst;
  out->seq       = h.seq;
  out->ctx_id    = h.ctx_id;
  out->frag      = h.frag;
  out->schema    = h.schema;
  out->hdr_flags = h.hdr_flags;
  return true;
}

// key_node is the PEER's address (spec 9.1); -1 means the frame carries no MAC.
void key_for(int16_t key_node, uint8_t out[32]) {
  g_kdf.derive_node_key(lran_test::kTestMasterKey, static_cast<NodeId>(key_node), out);
}


// Non-fatal comparison. Unity aborts a test function at the first failed assertion,
// which would report one disagreement and hide every other. The whole product of
// this milestone is the COMPLETE list, so a mismatch is recorded and the suite keeps
// going; the test fails at the end on the tally.
int g_disagreements = 0;

void report(const char* group, const char* vec, const char* what) {
  printf("  DISAGREE [%s] %s: %s\n", group, vec, what);
  ++g_disagreements;
}

bool expect_true(bool cond, const char* group, const char* vec, const char* what) {
  if (!cond) report(group, vec, what);
  return cond;
}

bool expect_u32(uint32_t want, uint32_t got, const char* group, const char* vec,
                const char* what) {
  if (want != got) {
    char msg[192];
    snprintf(msg, sizeof(msg), "%s: expected %u, got %u", what,
             static_cast<unsigned>(want), static_cast<unsigned>(got));
    report(group, vec, msg);
    return false;
  }
  return true;
}

bool expect_status(Status want, Status got, const char* group, const char* vec,
                   const char* what) {
  if (want != got) {
    char msg[192];
    snprintf(msg, sizeof(msg), "%s: expected %s, got %s", what, to_string(want),
             to_string(got));
    report(group, vec, msg);
    return false;
  }
  return true;
}

bool expect_bytes(const uint8_t* want, const uint8_t* got, size_t len,
                  const char* group, const char* vec, const char* what) {
  for (size_t i = 0; i < len; ++i) {
    if (want[i] != got[i]) {
      char msg[192];
      snprintf(msg, sizeof(msg),
               "%s: first diff at byte %u, expected 0x%02x, got 0x%02x", what,
               static_cast<unsigned>(i), want[i], got[i]);
      report(group, vec, msg);
      return false;
    }
  }
  return true;
}

}  // namespace

// --- spec 9.1: key derivation, checked before anything downstream -----------
//
// First deliberately. Getting this wrong is undetectable by inspection: the two
// sides derive different keys, every authenticated frame fails its MAC, and no
// counter points at key derivation.
void test_vectors_kdf() {
  const int before = g_disagreements;
  TEST_ASSERT_GREATER_THAN_UINT32(0, kKdfCount);

  for (size_t i = 0; i < kKdfCount; ++i) {
    const KdfVec& v = kKdf[i];

    // The vector's master key must be the fixture this build embeds, or the
    // comparison below is meaningless.
    expect_u32(static_cast<uint32_t>(lran_test::kTestMasterKeyLen), v.master_len,
               "kdf", v.name, "master_key length");
    expect_bytes(lran_test::kTestMasterKey, v.master, v.master_len, "kdf", v.name,
                 "master_key is not the committed TEST-ONLY fixture");

    // spec 9.1 - info is "node-" || the RAW address byte. Six bytes: not "node-1",
    // not "node-01", not "node-0x01".
    if (expect_u32(static_cast<uint32_t>(kKdfInfoLen), v.info_len, "kdf", v.name,
                   "info length")) {
      expect_bytes(reinterpret_cast<const uint8_t*>(kKdfInfoPrefix), v.info,
                   kKdfInfoPrefixLen, "kdf", v.name, "info prefix");
      expect_u32(v.node_id, v.info[kKdfInfoPrefixLen], "kdf", v.name,
                 "info address byte");
    }

    // spec 9.1 - the salt is FIXED across ver bumps. Changing it silently
    // invalidates every provisioned node in the field.
    if (expect_u32(static_cast<uint32_t>(kKdfSaltLen), v.salt_len, "kdf", v.name,
                   "salt length")) {
      expect_bytes(reinterpret_cast<const uint8_t*>(kKdfSalt), v.salt, v.salt_len,
                   "kdf", v.name, "salt");
    }

    if (!expect_u32(static_cast<uint32_t>(kNodeKeyLen), v.node_key_len, "kdf", v.name,
                    "node_key length")) continue;

    uint8_t got[32];
    g_kdf.derive_node_key(lran_test::kTestMasterKey, v.node_id, got);
    expect_bytes(v.node_key, got, kNodeKeyLen, "kdf", v.name, "derived node_key");
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(before, g_disagreements, "kdf vectors disagree");
}

// --- spec 6, 7, 19: one complete frame, byte for byte -----------------------
void test_vectors_single_frames() {
  const int before = g_disagreements;
  TEST_ASSERT_GREATER_THAN_UINT32(0, kSingleCount);

  for (size_t i = 0; i < kSingleCount; ++i) {
    const SingleVec& v = kSingle[i];

    Header h;
    if (!expect_true(header_from(v.hdr, &h), "single", v.name, "unknown type name"))
      continue;

    uint8_t   key[32];
    EncodeCtx ec;
    if (v.key_node >= 0) {
      key_for(v.key_node, key);
      ec.mac      = &g_mac;
      ec.node_key = key;
    }

    // spec 19 - the declared length is redundant against the bytes on purpose: an
    // off-by-one then fails as a length mismatch, not an opaque byte diff.
    expect_u32(v.declared_len, v.frame_len, "single", v.name, "frame_len vs frame");

    // A decode_only vector is one a conforming ENCODER would never emit - reserved
    // bits set, say (spec 5.8 requires they be written 0) - but that a receiver must
    // still accept. Encoding it is not a meaningful comparison.
    if (!v.decode_only) {
      uint8_t got[kMaxFrame];
      size_t  got_len = 0;
      const Status st =
          encode(h, v.payload, v.payload_len, ec, got, sizeof(got), &got_len);
      if (expect_status(Status::Ok, st, "single", v.name, "encode") &&
          expect_u32(v.frame_len, static_cast<uint32_t>(got_len), "single", v.name,
                     "encoded length")) {
        expect_bytes(v.frame, got, v.frame_len, "single", v.name, "encoded frame");
      }
    }

    // ...and the decode side agrees about what those bytes mean.
    Counters  c;
    DecodeCtx dc;
    dc.self          = v.self;
    dc.expect_ctx_id = v.expect_ctx;
    dc.counters      = &c;
    if (v.key_node >= 0) {
      dc.mac      = &g_mac;
      dc.node_key = key;
    }

    Status want_st = Status::Ok;
    if (!expect_true(status_from_name(v.status, &want_st), "single", v.name,
                     "unknown status name")) continue;

    Frame  f;
    Status ds = decode_header(v.frame, v.frame_len, dc, &f);
    if (ds == Status::Ok) ds = decode_payload(v.frame, v.frame_len, dc, &f);
    if (!expect_status(want_st, ds, "single", v.name, "decode")) continue;

    if (want_st == Status::Ok) {
      if (expect_u32(v.dec_payload_len, static_cast<uint32_t>(f.payload_len), "single",
                     v.name, "decoded payload length")) {
        expect_bytes(v.dec_payload, f.payload, v.dec_payload_len, "single", v.name,
                     "decoded payload");
      }
      expect_u32(v.mac_present ? 1u : 0u, f.mac != nullptr ? 1u : 0u, "single", v.name,
                 "mac_present");
      expect_u32(0, c.total_dropped(), "single", v.name,
                 "counters moved on a clean frame");
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(before, g_disagreements,
                                "single-frame vectors disagree");
}

// --- spec 11: fragmentation, both directions --------------------------------
void test_vectors_fragmentation() {
  const int before = g_disagreements;
  TEST_ASSERT_GREATER_THAN_UINT32(0, kFragCount);

  for (size_t i = 0; i < kFragCount; ++i) {
    const FragVec& v = kFrag[i];

    Header h;
    if (!expect_true(header_from(v.hdr, &h), "frag", v.name, "unknown type name"))
      continue;

    uint8_t   key[32];
    EncodeCtx ec;
    const bool authed = (v.key_node >= 0);
    if (authed) {
      key_for(v.key_node, key);
      ec.mac      = &g_mac;
      ec.node_key = key;
    }

    expect_u32(v.total, fragment_count(v.payload_len, v.frag_chunk), "frag", v.name,
               "fragment count");

    // spec 11.1 - the sender side, byte for byte.
    for (uint8_t fi = 0; fi < v.total; ++fi) {
      uint8_t got[kMaxFrame];
      size_t  got_len = 0;
      const Status st = encode_fragment(h, v.payload, v.payload_len, fi, v.frag_chunk,
                                        ec, got, sizeof(got), &got_len);
      if (!expect_status(Status::Ok, st, "frag", v.name, "encode_fragment")) continue;
      if (!expect_u32(v.frame_lens[fi], static_cast<uint32_t>(got_len), "frag", v.name,
                      "fragment length")) continue;
      expect_bytes(v.frames[fi], got, v.frame_lens[fi], "frag", v.name,
                   "fragment bytes");

      // spec 11.1 - uniform except the last, pinned explicitly.
      const size_t actual = got_len - kHdrLen - kCrcLen - (authed ? kMacLen : 0);
      expect_u32(v.fragment_lens[fi], static_cast<uint32_t>(actual), "frag", v.name,
                 "declared fragment_lens entry");
    }

    // spec 11.2 - the receiver side, in the vector's declared arrival order.
    Counters  c;
    DecodeCtx dc;
    dc.self          = v.self;
    dc.expect_ctx_id = v.expect_ctx;
    dc.counters      = &c;
    if (authed) {
      dc.mac      = &g_mac;
      dc.node_key = key;
    }

    Reassembler r(&c);
    uint32_t    now = 1000;
    bool        delivery_ok = true;
    for (uint8_t s = 0; s < v.order_len; ++s) {
      const uint8_t fi = v.order[s];
      Frame  f;
      Status ds = decode_header(v.frames[fi], v.frame_lens[fi], dc, &f);
      if (ds == Status::Ok) ds = decode_payload(v.frames[fi], v.frame_lens[fi], dc, &f);
      if (!expect_status(Status::Ok, ds, "frag", v.name, "fragment decode")) {
        delivery_ok = false;
        break;
      }
      // Reassembler::accept requires a MONOTONIC now_ms - a decreasing clock
      // underflows the unsigned age arithmetic and expires the set on arrival.
      now += 10;
      if (!expect_status(Status::Ok, r.accept(f, now), "frag", v.name, "accept")) {
        delivery_ok = false;
        break;
      }
    }
    if (!delivery_ok) continue;
    if (!expect_true(r.complete(), "frag", v.name, "set did not complete")) continue;

    if (expect_u32(v.reassembled_len, static_cast<uint32_t>(r.len()), "frag", v.name,
                   "reassembled length")) {
      expect_bytes(v.reassembled, r.data(), v.reassembled_len, "frag", v.name,
                   "reassembled bytes");
    }

    // Reassembly must reproduce the pre-fragmentation payload EXACTLY. Its absence in
    // v0.3 would have produced payloads that reassemble into the wrong bytes with a
    // valid CRC on every frame, between two conformant implementations.
    if (expect_u32(v.payload_len, static_cast<uint32_t>(r.len()), "frag", v.name,
                   "round-trip length")) {
      expect_bytes(v.payload, r.data(), v.payload_len, "frag", v.name,
                   "round-trip bytes");
    }

    if (v.counter != nullptr && !assert_only_counter(c, v.counter, v.name, false)) {
      report("frag", v.name, "vector names a counter this build has no field for");
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(before, g_disagreements,
                                "fragmentation vectors disagree");
}

// --- spec 14: every discard stage, and the counter that must move -----------
void test_vectors_negative() {
  const int before = g_disagreements;
  TEST_ASSERT_GREATER_THAN_UINT32(0, kNegativeCount);

  for (size_t i = 0; i < kNegativeCount; ++i) {
    const NegVec& v = kNegative[i];

    Status want_st = Status::Ok;
    if (!expect_true(status_from_name(v.status, &want_st), "negative", v.name,
                     "unknown status name")) continue;

    Counters  c;
    DecodeCtx dc;
    dc.self          = v.self;
    dc.expect_ctx_id = v.expect_ctx;
    dc.counters      = &c;
    dc.mac           = &g_mac;

    // Most negative vectors are rejected before stage 9. Those that are not name a
    // key; the rest get GateLink's so the MAC path is still reachable.
    uint8_t key[32];
    key_for(v.key_node >= 0 ? v.key_node : kNodeGateLink, key);
    dc.node_key = key;

    Frame  f;
    Status ds = decode_header(v.frame, v.frame_len, dc, &f);
    if (ds == Status::Ok) ds = decode_payload(v.frame, v.frame_len, dc, &f);
    expect_status(want_st, ds, "negative", v.name, "decode");

    if (v.counter != nullptr && !assert_only_counter(c, v.counter, v.name, true)) {
      report("negative", v.name, "vector names a counter this build has no field for");
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(before, g_disagreements, "negative vectors disagree");
}

int run_all() {
  UNITY_BEGIN();
  RUN_TEST(test_vectors_kdf);
  RUN_TEST(test_vectors_single_frames);
  RUN_TEST(test_vectors_fragmentation);
  RUN_TEST(test_vectors_negative);
  if (g_disagreements > 0) {
    printf("\n%d generator-vs-codec disagreement(s) - see the list above.\n",
           g_disagreements);
  }
  return UNITY_END();
}

// PlatformIO runs the same suites on the host and on the ESP32-S3. The host entry
// point is main(); Arduino's is setup()/loop(). Unity's own setUp/tearDown are
// distinct names and do not collide.
#ifdef ARDUINO
void setup() {
  // The USB-serial link needs a moment before the first report, or the opening
  // lines are lost and a passing run looks like a hang.
  delay(2000);
  run_all();
}
void loop() {}
#else
int main() { return run_all(); }
#endif
