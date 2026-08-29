// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// P6 / W4 - the independent witness. Spec 13.2.
//
// Every other suite in this project checks the codec against itself. This one checks
// it against /tools/vectors/, generated in Python from the specification prose by a
// party that never read this codec. Where the two disagree, the disagreement is the
// finding and is adjudicated against the spec text - see the engineering log.

#include <unity.h>

#include <cstring>

#include "lran/lran.h"
#include "mini_json.h"
#include "refimpl_mac.h"
#include "test_key.h"

using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefMac g_mac;
refimpl::RefKdf g_kdf;

// `pio test` runs with the project directory as CWD, but the suite is also useful
// from the repo root. Try both rather than making the caller care.
const char* vector_path(const char* name) {
  static char buf[256];
  static const char* const prefixes[] = {"../../tools/vectors/", "tools/vectors/"};
  for (const char* prefix : prefixes) {
    snprintf(buf, sizeof(buf), "%s%s", prefix, name);
    FILE* f = fopen(buf, "rb");
    if (f != nullptr) {
      fclose(f);
      return buf;
    }
  }
  snprintf(buf, sizeof(buf), "../../tools/vectors/%s", name);
  return buf;  // let the caller fail loudly with a path in the message
}

void load_or_fail(mini_json::Doc* doc, const char* name) {
  const char* path = vector_path(name);
  if (!doc->load(path)) {
    // A vector file that fails to load must never look like a suite that passed.
    TEST_FAIL_MESSAGE(path);
  }
}

// The vectors name types by their spec 6 name, not their numeric value: the
// name-to-value mapping is itself part of what is being witnessed, so a number in
// the file would hide a disagreement about it.
bool type_from_name(const mini_json::Doc& d, int node, MsgType* out) {
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
    if (d.str_is(node, r.name)) {
      *out = r.type;
      return true;
    }
  }
  return false;
}

bool status_from_name(const mini_json::Doc& d, int node, Status* out) {
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
    if (d.str_is(node, r.name)) {
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

struct CounterRef { const char* name; uint32_t Counters::* field; };

const CounterRef kCounters[] = {
    {"rx_runt", &Counters::rx_runt},
    {"rx_oversize", &Counters::rx_oversize},
    {"rx_bad_crc", &Counters::rx_bad_crc},
    {"rx_bad_ver", &Counters::rx_bad_ver},
    {"rx_not_addressed", &Counters::rx_not_addressed},
    {"rx_unknown_hdr_ext", &Counters::rx_unknown_hdr_ext},
    {"rx_bad_frag", &Counters::rx_bad_frag},
    {"rx_unknown_type", &Counters::rx_unknown_type},
    {"rx_unknown_schema", &Counters::rx_unknown_schema},
    {"rx_bad_length", &Counters::rx_bad_length},
    {"rx_not_fragmentable", &Counters::rx_not_fragmentable},
    {"rx_bad_mac", &Counters::rx_bad_mac},
    {"rx_ctx_mismatch", &Counters::rx_ctx_mismatch},
    {"rx_reassembly_timeout", &Counters::rx_reassembly_timeout},
    {"rx_reassembly_abandoned", &Counters::rx_reassembly_abandoned},
    {"rx_fragment_overflow", &Counters::rx_fragment_overflow},
    {"rx_frag_duplicate", &Counters::rx_frag_duplicate},
};

// Asserts `want` is the only counter that moved. Returns false if the name is one
// this build does not have - a naming disagreement, reported by the caller rather
// than silently passing.
bool assert_only_counter(const Counters& c, const char* want, const char* vec_name,
                         bool exactly_one) {
  bool known = false;
  for (const CounterRef& r : kCounters) {
    if (strcmp(r.name, want) == 0) known = true;
  }
  if (!known) return false;
  for (const CounterRef& r : kCounters) {
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

// Builds a Header from a vector's "header" object.
bool header_from(const mini_json::Doc& d, int hn, Header* out) {
  MsgType type;
  if (!type_from_name(d, d.get(hn, "type"), &type)) return false;
  out->type   = type;
  out->ver    = static_cast<uint8_t>(d.num(d.get(hn, "ver"), kProtoVer));
  out->src    = static_cast<NodeId>(d.num(d.get(hn, "src")));
  out->dst    = static_cast<NodeId>(d.num(d.get(hn, "dst")));
  out->seq    = static_cast<Seq>(d.num(d.get(hn, "seq")));
  out->ctx_id = static_cast<CtxId>(
      static_cast<unsigned long>(d.num(d.get(hn, "ctx_id"))));
  return d.hex_byte(d.get(hn, "frag"), &out->frag) &&
         d.hex_byte(d.get(hn, "schema"), &out->schema) &&
         d.hex_byte(d.get(hn, "hdr_flags"), &out->hdr_flags);
}

// "node:0x01" -> the key derived for node 0x01 from the fixed TEST-ONLY master key.
bool key_for(const mini_json::Doc& d, int kn, uint8_t out[32]) {
  char buf[32];
  if (!d.str(kn, buf, sizeof(buf))) return false;
  if (strncmp(buf, "node:", 5) != 0) return false;
  const long id = strtol(buf + 5, nullptr, 16);
  g_kdf.derive_node_key(lran_test::kTestMasterKey, static_cast<NodeId>(id), out);
  return true;
}


// Non-fatal comparison. Unity aborts a test function at the first failed assertion,
// which would report one disagreement and hide every other. The whole product of
// this milestone is the COMPLETE list, so a mismatch here is recorded and the suite
// keeps going; the test fails at the end on the tally.
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
    snprintf(msg, sizeof(msg), "%s: expected %u, got %u", what, want, got);
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
      snprintf(msg, sizeof(msg), "%s: first diff at byte %zu, expected 0x%02x, got 0x%02x",
               what, i, want[i], got[i]);
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
  mini_json::Doc d;
  load_or_fail(&d, "vectors_kdf.json");
  const int vs = d.get(0, "vectors");
  const int n  = d.count(vs);
  TEST_ASSERT_GREATER_THAN_INT(0, n);
  const int before = g_disagreements;

  for (int i = 0; i < n; ++i) {
    const int v = d.at(vs, i);
    char name[128] = "?";
    d.str(d.get(v, "name"), name, sizeof(name));

    uint8_t node_id = 0;
    if (!expect_true(d.hex_byte(d.get(v, "node_id"), &node_id), "kdf", name,
                     "node_id unreadable")) continue;

    // The master key in the vector must be the fixture this build embeds, or the
    // comparison below is meaningless.
    uint8_t master[64];
    size_t  master_len = 0;
    if (!expect_true(d.hex(d.get(v, "master_key"), master, sizeof(master), &master_len),
                     "kdf", name, "master_key unreadable")) continue;
    expect_u32(static_cast<uint32_t>(lran_test::kTestMasterKeyLen),
               static_cast<uint32_t>(master_len), "kdf", name, "master_key length");
    expect_bytes(lran_test::kTestMasterKey, master, master_len, "kdf", name,
                 "master_key is not the committed TEST-ONLY fixture");

    // spec 9.1 - info is "node-" || the RAW address byte. Six bytes, not "node-1",
    // not "node-01", not "node-0x01".
    uint8_t info[32];
    size_t  info_len = 0;
    if (expect_true(d.hex(d.get(v, "info"), info, sizeof(info), &info_len), "kdf",
                    name, "info unreadable")) {
      expect_u32(static_cast<uint32_t>(kKdfInfoLen), static_cast<uint32_t>(info_len),
                 "kdf", name, "info length");
      if (info_len == kKdfInfoLen) {
        expect_bytes(reinterpret_cast<const uint8_t*>(kKdfInfoPrefix), info,
                     kKdfInfoPrefixLen, "kdf", name, "info prefix");
        expect_u32(node_id, info[kKdfInfoPrefixLen], "kdf", name, "info address byte");
      }
    }

    // spec 9.1 - the salt is FIXED across ver bumps. Changing it silently
    // invalidates every provisioned node.
    uint8_t salt[32];
    size_t  salt_len = 0;
    if (expect_true(d.hex(d.get(v, "salt"), salt, sizeof(salt), &salt_len), "kdf",
                    name, "salt unreadable")) {
      expect_u32(static_cast<uint32_t>(kKdfSaltLen), static_cast<uint32_t>(salt_len),
                 "kdf", name, "salt length");
      if (salt_len == kKdfSaltLen) {
        expect_bytes(reinterpret_cast<const uint8_t*>(kKdfSalt), salt, salt_len, "kdf",
                     name, "salt");
      }
    }

    uint8_t want[64];
    size_t  want_len = 0;
    if (!expect_true(d.hex(d.get(v, "node_key"), want, sizeof(want), &want_len), "kdf",
                     name, "node_key unreadable")) continue;
    if (!expect_u32(static_cast<uint32_t>(kNodeKeyLen), static_cast<uint32_t>(want_len),
                    "kdf", name, "node_key length")) continue;

    uint8_t got[32];
    g_kdf.derive_node_key(lran_test::kTestMasterKey, node_id, got);
    expect_bytes(want, got, kNodeKeyLen, "kdf", name, "derived node_key");
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(before, g_disagreements, "kdf vectors disagree");
}

// --- spec 6, 7, 19: one complete frame, byte for byte -----------------------
void test_vectors_single_frames() {
  mini_json::Doc d;
  load_or_fail(&d, "vectors_single.json");
  const int vs = d.get(0, "vectors");
  const int n  = d.count(vs);
  TEST_ASSERT_GREATER_THAN_INT(0, n);
  const int before = g_disagreements;

  for (int i = 0; i < n; ++i) {
    const int v = d.at(vs, i);
    char name[128] = "?";
    d.str(d.get(v, "name"), name, sizeof(name));

    Header h;
    if (!expect_true(header_from(d, d.get(v, "header"), &h), "single", name,
                     "header unreadable")) continue;

    uint8_t payload[kMaxFrame];
    size_t  payload_len = 0;
    if (!expect_true(d.hex(d.get(v, "payload"), payload, sizeof(payload), &payload_len),
                     "single", name, "payload unreadable")) continue;

    uint8_t key[32];
    EncodeCtx ec;
    const int kn = d.get(v, "key");
    const bool authed = (kn >= 0 && !d.is_null(kn));
    if (authed) {
      if (!expect_true(key_for(d, kn, key), "single", name, "key id unreadable"))
        continue;
      ec.mac      = &g_mac;
      ec.node_key = key;
    }

    uint8_t want[kMaxFrame];
    size_t  want_len = 0;
    if (!expect_true(d.hex(d.get(v, "frame"), want, sizeof(want), &want_len), "single",
                     name, "frame unreadable")) continue;

    // spec 19 - the declared length is redundant against the bytes on purpose: an
    // off-by-one then fails as a length mismatch, not an opaque byte diff.
    expect_u32(static_cast<uint32_t>(d.num(d.get(v, "frame_len"))),
               static_cast<uint32_t>(want_len), "single", name, "frame_len vs frame");

    // A decode_only vector is one a conforming ENCODER would never emit - reserved
    // bits set, for instance (spec 5.8 says write 0) - but that a receiver must
    // still accept. Encoding it is not a meaningful comparison.
    const int donly = d.get(v, "decode_only");
    const bool decode_only = (donly >= 0 && d.node(donly).boolean);

    if (!decode_only) {
      uint8_t got[kMaxFrame];
      size_t  got_len = 0;
      const Status st = encode(h, payload, payload_len, ec, got, sizeof(got), &got_len);
      if (expect_status(Status::Ok, st, "single", name, "encode")) {
        if (expect_u32(static_cast<uint32_t>(want_len), static_cast<uint32_t>(got_len),
                       "single", name, "encoded length")) {
          expect_bytes(want, got, want_len, "single", name, "encoded frame");
        }
      }
    }

    // ...and the decode side agrees about what those bytes mean.
    const int dn = d.get(v, "decode");
    Counters  c;
    DecodeCtx dc;
    dc.self          = static_cast<NodeId>(d.num(d.get(dn, "self")));
    dc.expect_ctx_id = static_cast<CtxId>(
        static_cast<unsigned long>(d.num(d.get(dn, "expect_ctx_id"))));
    dc.counters = &c;
    if (authed) {
      dc.mac      = &g_mac;
      dc.node_key = key;
    }

    Status want_st = Status::Ok;
    if (!expect_true(status_from_name(d, d.get(dn, "status"), &want_st), "single", name,
                     "unknown status name")) continue;

    Frame f;
    Status ds = decode_header(want, want_len, dc, &f);
    if (ds == Status::Ok) ds = decode_payload(want, want_len, dc, &f);
    if (!expect_status(want_st, ds, "single", name, "decode")) continue;

    if (want_st == Status::Ok) {
      uint8_t want_payload[kMaxFrame];
      size_t  want_payload_len = 0;
      if (d.hex(d.get(dn, "payload"), want_payload, sizeof(want_payload),
                &want_payload_len)) {
        if (expect_u32(static_cast<uint32_t>(want_payload_len),
                       static_cast<uint32_t>(f.payload_len), "single", name,
                       "decoded payload length")) {
          expect_bytes(want_payload, f.payload, want_payload_len, "single", name,
                       "decoded payload");
        }
      }
      const int mp = d.get(dn, "mac_present");
      if (mp >= 0) {
        expect_u32(d.node(mp).boolean ? 1u : 0u, f.mac != nullptr ? 1u : 0u, "single",
                   name, "mac_present");
      }
      expect_u32(0, c.total_dropped(), "single", name, "counters moved on a clean frame");
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(before, g_disagreements, "single-frame vectors disagree");
}

// --- spec 11: fragmentation, both directions --------------------------------
void test_vectors_fragmentation() {
  mini_json::Doc d;
  load_or_fail(&d, "vectors_frag.json");
  const int vs = d.get(0, "vectors");
  const int n  = d.count(vs);
  TEST_ASSERT_GREATER_THAN_INT(0, n);
  const int before = g_disagreements;

  for (int i = 0; i < n; ++i) {
    const int v = d.at(vs, i);
    char name[128] = "?";
    d.str(d.get(v, "name"), name, sizeof(name));

    Header h;
    if (!expect_true(header_from(d, d.get(v, "header"), &h), "frag", name,
                     "header unreadable")) continue;

    uint8_t payload[kMaxFrame];
    size_t  payload_len = 0;
    if (!expect_true(d.hex(d.get(v, "payload"), payload, sizeof(payload), &payload_len),
                     "frag", name, "payload unreadable")) continue;

    const size_t chunk = static_cast<size_t>(d.num(d.get(v, "frag_chunk")));

    uint8_t key[32];
    EncodeCtx ec;
    const int kn = d.get(v, "key");
    const bool authed = (kn >= 0 && !d.is_null(kn));
    if (authed) {
      if (!expect_true(key_for(d, kn, key), "frag", name, "key id unreadable")) continue;
      ec.mac      = &g_mac;
      ec.node_key = key;
    }

    const int frames = d.get(v, "frames");
    const int total  = d.count(frames);
    expect_u32(static_cast<uint32_t>(total),
               static_cast<uint32_t>(fragment_count(payload_len, chunk)), "frag", name,
               "fragment count");

    // spec 11.1 - the sender side, byte for byte.
    const int lens = d.get(v, "fragment_lens");
    for (int fi = 0; fi < total; ++fi) {
      uint8_t want[kMaxFrame];
      size_t  want_len = 0;
      if (!expect_true(d.hex(d.at(frames, fi), want, sizeof(want), &want_len), "frag",
                       name, "fragment unreadable")) continue;

      uint8_t got[kMaxFrame];
      size_t  got_len = 0;
      const Status st = encode_fragment(h, payload, payload_len,
                                        static_cast<uint8_t>(fi), chunk, ec, got,
                                        sizeof(got), &got_len);
      if (!expect_status(Status::Ok, st, "frag", name, "encode_fragment")) continue;
      if (!expect_u32(static_cast<uint32_t>(want_len), static_cast<uint32_t>(got_len),
                      "frag", name, "fragment length")) continue;
      expect_bytes(want, got, want_len, "frag", name, "fragment bytes");

      // spec 11.1 - uniform except the last, pinned explicitly.
      if (lens >= 0) {
        const size_t declared = static_cast<size_t>(d.num(d.at(lens, fi)));
        const size_t actual   = got_len - kHdrLen - kCrcLen - (authed ? kMacLen : 0);
        expect_u32(static_cast<uint32_t>(declared), static_cast<uint32_t>(actual),
                   "frag", name, "declared fragment_lens entry");
      }
    }

    // spec 11.2 - the receiver side, in the vector's declared arrival order.
    const int dn = d.get(v, "decode");
    Counters  c;
    DecodeCtx dc;
    dc.self          = static_cast<NodeId>(d.num(d.get(dn, "self")));
    dc.expect_ctx_id = static_cast<CtxId>(
        static_cast<unsigned long>(d.num(d.get(dn, "expect_ctx_id"))));
    dc.counters = &c;
    if (authed) {
      dc.mac      = &g_mac;
      dc.node_key = key;
    }

    Reassembler r(&c);
    const int order = d.get(dn, "delivery_order");
    const int steps = d.count(order);
    if (!expect_true(steps > 0, "frag", name, "empty delivery_order")) continue;

    uint32_t now = 1000;
    bool     delivery_ok = true;
    for (int s = 0; s < steps; ++s) {
      const int fi = static_cast<int>(d.num(d.at(order, s)));
      uint8_t buf[kMaxFrame];
      size_t  blen = 0;
      if (!d.hex(d.at(frames, fi), buf, sizeof(buf), &blen)) { delivery_ok = false; break; }

      Frame f;
      Status ds = decode_header(buf, blen, dc, &f);
      if (ds == Status::Ok) ds = decode_payload(buf, blen, dc, &f);
      if (!expect_status(Status::Ok, ds, "frag", name, "fragment decode")) {
        delivery_ok = false;
        break;
      }
      // Reassembler::accept requires a MONOTONIC now_ms - a decreasing clock
      // underflows the unsigned age arithmetic and expires the set on arrival.
      now += 10;
      if (!expect_status(Status::Ok, r.accept(f, now), "frag", name, "accept")) {
        delivery_ok = false;
        break;
      }
    }
    if (!delivery_ok) continue;

    if (!expect_true(r.complete(), "frag", name, "set did not complete")) continue;

    uint8_t want_re[kMaxFrame];
    size_t  want_re_len = 0;
    if (d.hex(d.get(dn, "reassembled"), want_re, sizeof(want_re), &want_re_len)) {
      if (expect_u32(static_cast<uint32_t>(want_re_len), static_cast<uint32_t>(r.len()),
                     "frag", name, "reassembled length")) {
        expect_bytes(want_re, r.data(), want_re_len, "frag", name, "reassembled bytes");
      }
    }

    // Reassembly must reproduce the pre-fragmentation payload EXACTLY. Its absence in
    // v0.3 would have produced payloads that reassemble into the wrong bytes with a
    // valid CRC on every frame, between two conformant implementations.
    if (expect_u32(static_cast<uint32_t>(payload_len), static_cast<uint32_t>(r.len()),
                   "frag", name, "round-trip length")) {
      expect_bytes(payload, r.data(), payload_len, "frag", name, "round-trip bytes");
    }

    char want_counter[64];
    if (d.str(d.get(dn, "counter"), want_counter, sizeof(want_counter))) {
      if (!assert_only_counter(c, want_counter, name, false)) {
        report("frag", name, "vector names a counter this build has no field for");
      }
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(before, g_disagreements, "fragmentation vectors disagree");
}

// --- spec 14: every discard stage, and the counter that must move -----------
void test_vectors_negative() {
  mini_json::Doc d;
  load_or_fail(&d, "vectors_negative.json");
  const int vs = d.get(0, "vectors");
  const int n  = d.count(vs);
  TEST_ASSERT_GREATER_THAN_INT(0, n);
  const int before = g_disagreements;

  for (int i = 0; i < n; ++i) {
    const int v = d.at(vs, i);
    char name[128] = "?";
    d.str(d.get(v, "name"), name, sizeof(name));

    uint8_t frame[512];  // the oversize vectors exceed kMaxFrame on purpose
    size_t  flen = 0;
    if (!expect_true(d.hex(d.get(v, "frame"), frame, sizeof(frame), &flen), "negative",
                     name, "frame unreadable")) continue;

    const int dn = d.get(v, "decode");
    Status want_st = Status::Ok;
    if (!expect_true(status_from_name(d, d.get(dn, "status"), &want_st), "negative",
                     name, "unknown status name")) continue;

    Counters  c;
    DecodeCtx dc;
    dc.self          = static_cast<NodeId>(d.num(d.get(dn, "self")));
    dc.expect_ctx_id = static_cast<CtxId>(
        static_cast<unsigned long>(d.num(d.get(dn, "expect_ctx_id"))));
    dc.counters = &c;
    dc.mac      = &g_mac;

    uint8_t key[32];
    const int kn = d.get(v, "key");
    if (kn >= 0 && !d.is_null(kn) && key_for(d, kn, key)) {
      dc.node_key = key;
    } else {
      // Most negative vectors are rejected before stage 9. The rest get GateLink's
      // key so the MAC path is still reachable.
      g_kdf.derive_node_key(lran_test::kTestMasterKey, kNodeGateLink, key);
      dc.node_key = key;
    }

    Frame f;
    Status ds = decode_header(frame, flen, dc, &f);
    if (ds == Status::Ok) ds = decode_payload(frame, flen, dc, &f);
    expect_status(want_st, ds, "negative", name, "decode");

    char want_counter[64];
    if (d.str(d.get(dn, "counter"), want_counter, sizeof(want_counter))) {
      if (!assert_only_counter(c, want_counter, name, true)) {
        report("negative", name, "vector names a counter this build has no field for");
      }
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(before, g_disagreements, "negative vectors disagree");
}

int main() {
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
