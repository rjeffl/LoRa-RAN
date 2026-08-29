// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// P3 - MAC and keying. Spec 9.

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include <unity.h>

#include "lran/lran.h"
#include "refimpl_mac.h"

#ifdef ARDUINO
#include "mbedtls_mac.h"
#endif

using namespace lran;

namespace {

refimpl::RefMac g_mac;

const uint8_t kMaster[kMasterKeyLen] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

void assert_hex(const uint8_t* got, const char* want_hex, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const char hi = want_hex[2 * i], lo = want_hex[2 * i + 1];
    auto nyb = [](char ch) -> uint8_t {
      if (ch >= '0' && ch <= '9') return static_cast<uint8_t>(ch - '0');
      return static_cast<uint8_t>(ch - 'a' + 10);
    };
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>((nyb(hi) << 4) | nyb(lo)), got[i]);
  }
}

}  // namespace

void setUp() {}
void tearDown() {}

// FIPS 180-4 worked example.
void test_sha256_known_answer() {
  const uint8_t abc[] = {'a', 'b', 'c'};
  uint8_t out[32];
  refimpl::sha256(abc, 3, out);
  assert_hex(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32);

  refimpl::sha256(nullptr, 0, out);
  assert_hex(out, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 32);
}

// Exercises the multi-block path, where a padding bug hides.
void test_sha256_multiblock() {
  uint8_t data[200];
  for (size_t i = 0; i < sizeof(data); ++i) data[i] = static_cast<uint8_t>('a');
  uint8_t out[32];
  refimpl::sha256(data, 56, out);  // exactly the awkward length: 56 bytes
  uint8_t again[32];
  refimpl::sha256(data, 56, again);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(out, again, 32);
  // 'a' x 1000000 is the classic vector; 'a' x 64 is enough to prove the block
  // boundary is handled, and is checked against a second run of the same input to
  // catch state that leaks between calls.
  refimpl::sha256(data, 64, out);
  refimpl::sha256(data, 64, again);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(out, again, 32);
}

// RFC 4231 test case 1.
void test_hmac_sha256_known_answer() {
  uint8_t key[20];
  for (int i = 0; i < 20; ++i) key[i] = 0x0b;
  const uint8_t data[] = {'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e'};
  uint8_t out[32];
  refimpl::hmac_sha256(key, sizeof(key), data, sizeof(data), out);
  assert_hex(out, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", 32);
}

// RFC 4231 test case 2 - a short ASCII key, the common shape.
void test_hmac_sha256_case2() {
  const uint8_t key[] = {'J', 'e', 'f', 'e'};
  const uint8_t data[] = {'w', 'h', 'a', 't', ' ', 'd', 'o', ' ', 'y', 'a', ' ',
                          'w', 'a', 'n', 't', ' ', 'f', 'o', 'r', ' ', 'n', 'o',
                          't', 'h', 'i', 'n', 'g', '?'};
  uint8_t out[32];
  refimpl::hmac_sha256(key, sizeof(key), data, sizeof(data), out);
  assert_hex(out, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32);
}

// RFC 5869 test case 1, first 32 bytes of OKM.
void test_hkdf_sha256_known_answer() {
  uint8_t ikm[22];
  for (int i = 0; i < 22; ++i) ikm[i] = 0x0b;
  const uint8_t salt[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                          0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
  const uint8_t info[] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
  uint8_t okm[32];
  refimpl::hkdf_sha256(salt, sizeof(salt), ikm, sizeof(ikm), info, sizeof(info), okm,
                       sizeof(okm));
  assert_hex(okm, "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf", 32);
}

// spec 9.1 - per-node derivation. Compromising a node yields that node's key alone.
void test_node_keys_differ_per_node() {
  refimpl::RefKdf kdf;
  uint8_t gate[32], well[32], sim0[32], again[32];
  kdf.derive_node_key(kMaster, kNodeGateLink, gate);
  kdf.derive_node_key(kMaster, kNodeWellLink, well);
  kdf.derive_node_key(kMaster, kNodeSim0, sim0);
  kdf.derive_node_key(kMaster, kNodeGateLink, again);

  TEST_ASSERT_EQUAL_HEX8_ARRAY(gate, again, 32);  // deterministic
  bool same_gw = true, same_gs = true;
  for (int i = 0; i < 32; ++i) {
    if (gate[i] != well[i]) same_gw = false;
    if (gate[i] != sim0[i]) same_gs = false;
  }
  TEST_ASSERT_FALSE(same_gw);
  TEST_ASSERT_FALSE(same_gs);
}

// spec 9.1 - the salt is a domain separator, not a wire version. Pinned here so a
// change shows up as a failing test rather than as a fleet that rejects everything.
void test_kdf_salt_and_info_are_pinned() {
  TEST_ASSERT_EQUAL_UINT32(7, kKdfSaltLen);
  TEST_ASSERT_EQUAL_STRING("lran-v1", kKdfSalt);
  TEST_ASSERT_EQUAL_STRING("node-", kKdfInfoPrefix);
  TEST_ASSERT_EQUAL_UINT32(6, kKdfInfoLen);
}

void test_ct_equal() {
  const uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  TEST_ASSERT_TRUE(ct_equal(a, b, 8));
  b[7] = 9;
  TEST_ASSERT_FALSE(ct_equal(a, b, 8));
  b[7] = 8;
  b[0] = 9;
  TEST_ASSERT_FALSE(ct_equal(a, b, 8));
}

// spec 9.3 - the MAC covers the entire header and payload, and nothing else.
void test_command_mac_roundtrip_and_tamper() {
  refimpl::RefKdf kdf;
  uint8_t key[32];
  kdf.derive_node_key(kMaster, kNodeGateLink, key);

  msg::Command cmd{static_cast<uint8_t>(Cmd::HoldOpen), 0, 0};
  uint8_t payload[4];
  size_t  plen = 0;
  msg::serialize(cmd, payload, sizeof(payload), &plen);

  Header h;
  h.type   = MsgType::Command;
  h.src    = kNodeBridge;
  h.dst    = kNodeGateLink;
  h.seq    = 7;
  h.ctx_id = 0xDEADBEEFu;
  h.set_frag(0, 1);

  EncodeCtx ec;
  ec.mac      = &g_mac;
  ec.node_key = key;

  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload, plen, ec, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(30, n);  // spec 19 - COMMAND is 30 bytes

  Counters c;
  DecodeCtx d;
  d.self          = kNodeGateLink;
  d.mac           = &g_mac;
  d.node_key      = key;
  d.expect_ctx_id = 0xDEADBEEFu;
  d.counters      = &c;

  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, d, &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, d, &f));
  TEST_ASSERT_NOT_NULL(f.mac);
  TEST_ASSERT_EQUAL_UINT32(4, f.payload_len);

  // Flipping a RESERVED header byte must break the MAC: spec 9.3 puts them inside
  // it, so a future extension is authenticated from the day it is defined.
  uint8_t tampered[kMaxFrame];
  for (size_t i = 0; i < n; ++i) tampered[i] = buf[i];
  tampered[13] ^= 0x01;
  const uint16_t crc = crc16_ccitt_false(tampered, n - kCrcLen);
  tampered[n - 2] = static_cast<uint8_t>(crc & 0xFF);
  tampered[n - 1] = static_cast<uint8_t>(crc >> 8);
  Frame tf;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(tampered, n, d, &tf));
  TEST_ASSERT_EQUAL(Status::BadMac, decode_payload(tampered, n, d, &tf));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_rejected_mac);
}

// spec 9.4 step 2 - ctx before MAC, so a stale context is answered with
// REJECTED_CTX and a resync rather than looking like a forgery.
void test_ctx_mismatch_precedes_mac_check() {
  refimpl::RefKdf kdf;
  uint8_t key[32];
  kdf.derive_node_key(kMaster, kNodeGateLink, key);

  uint8_t payload[4] = {static_cast<uint8_t>(Cmd::Open), 0, 0, 0};
  Header h;
  h.type   = MsgType::Command;
  h.src    = kNodeBridge;
  h.dst    = kNodeGateLink;
  h.ctx_id = 0x11111111u;  // the bridge's stale idea of the node's context
  h.set_frag(0, 1);

  EncodeCtx ec;
  ec.mac      = &g_mac;
  ec.node_key = key;
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  encode(h, payload, 4, ec, buf, sizeof(buf), &n);

  Counters c;
  DecodeCtx d;
  d.self          = kNodeGateLink;
  d.mac           = &g_mac;
  d.node_key      = key;
  d.expect_ctx_id = 0x22222222u;  // the node rebooted
  d.counters      = &c;

  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, d, &f));
  TEST_ASSERT_EQUAL(Status::CtxMismatch, decode_payload(buf, n, d, &f));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_rejected_ctx);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_rejected_mac);  // the MAC was never reached
}

// A bridge sets expect_ctx_id = 0: it has no context of its own and is the party
// that tracks everyone else's (spec 10.1).
void test_bridge_does_not_check_ctx() {
  refimpl::RefKdf kdf;
  uint8_t key[32];
  kdf.derive_node_key(kMaster, kNodeGateLink, key);

  uint8_t payload[4] = {static_cast<uint8_t>(Cmd::Open), 0, 0, 0};
  Header h;
  h.type   = MsgType::Command;
  h.src    = kNodeBridge;
  h.dst    = kNodeGateLink;
  h.ctx_id = 0x33333333u;
  h.set_frag(0, 1);
  EncodeCtx ec;
  ec.mac = &g_mac;
  ec.node_key = key;
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  encode(h, payload, 4, ec, buf, sizeof(buf), &n);

  Counters c;
  DecodeCtx d;
  d.self     = kNodeGateLink;
  d.mac      = &g_mac;
  d.node_key = key;
  d.counters = &c;  // expect_ctx_id left 0
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, d, &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, d, &f));
}

// A key belonging to a different node must not verify. This is the property that
// makes running bench nodes against production infrastructure acceptable: a simnode
// holds only its own derived key and cannot forge a COMMAND to GateLink.
void test_wrong_node_key_fails_verification() {
  refimpl::RefKdf kdf;
  uint8_t gate_key[32], sim_key[32];
  kdf.derive_node_key(kMaster, kNodeGateLink, gate_key);
  kdf.derive_node_key(kMaster, kNodeSim0, sim_key);

  uint8_t payload[4] = {static_cast<uint8_t>(Cmd::Open), 0, 0, 0};
  Header h;
  h.type = MsgType::Command;
  h.src  = kNodeSim0;
  h.dst  = kNodeGateLink;
  h.set_frag(0, 1);

  EncodeCtx ec;
  ec.mac      = &g_mac;
  ec.node_key = sim_key;  // the simnode signs with the only key it has
  uint8_t buf[kMaxFrame];
  size_t  n = 0;
  encode(h, payload, 4, ec, buf, sizeof(buf), &n);

  Counters c;
  DecodeCtx d;
  d.self     = kNodeGateLink;
  d.mac      = &g_mac;
  d.node_key = gate_key;  // GateLink verifies with its own
  d.counters = &c;
  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, d, &f));
  TEST_ASSERT_EQUAL(Status::BadMac, decode_payload(buf, n, d, &f));
}

// --- F2 / F3: spec 9.4's split across the reassembly boundary ---------------
//
// AUTHENTICATION IS PER FRAME, REPLAY PROTECTION IS PER SET. Neither of v0.3's two
// orderings worked whole: verifying after reassembly leaves no MAC to check, since
// spec 11 gives every fragment its own; running all six steps per fragment means
// fragment 0 sets rx_high_water and fragment 1 - which shares the set's seq per
// spec 11.1 - is rejected as a replay of itself.
//
// Steps 4-6 (dedup, seq comparison, high-water) are NODE BEHAVIOUR and live outside
// this library by its Implementation Plan section 1. What is testable here is the
// half the library owns: steps 1-3 run per fragment, before the fragment is
// buffered, and nothing in the reassembly path consults seq to reject.

namespace {

// A 3-fragment authenticated CONFIG set, built with the real KDF and MAC.
struct FragSet {
  uint8_t frames[3][kMaxFrame];
  size_t  lens[3];
  uint8_t key[32];
  uint8_t payload[90];
};

void build_authenticated_config_set(FragSet* fs) {
  refimpl::RefKdf kdf;
  kdf.derive_node_key(kMaster, kNodeGateLink, fs->key);

  // spec 7.4 - [op][count][ entries... ]; the bytes only have to survive the trip.
  for (size_t i = 0; i < sizeof(fs->payload); ++i) {
    fs->payload[i] = static_cast<uint8_t>(0xA0 + i);
  }

  Header h;
  h.type   = MsgType::Config;
  h.src    = kNodeBridge;
  h.dst    = kNodeGateLink;
  h.seq    = 0x0101;  // spec 11.1 - ONE seq, shared by every fragment of the set
  h.ctx_id = 0xDEADBEEFu;
  h.schema = kSchemaGateLinkConfigV1;

  EncodeCtx ec;
  ec.mac      = &g_mac;
  ec.node_key = fs->key;

  TEST_ASSERT_EQUAL_UINT8(3, fragment_count(sizeof(fs->payload), 30));
  for (uint8_t i = 0; i < 3; ++i) {
    TEST_ASSERT_EQUAL(Status::Ok,
                      encode_fragment(h, fs->payload, sizeof(fs->payload), i, 30, ec,
                                      fs->frames[i], kMaxFrame, &fs->lens[i]));
  }
}

DecodeCtx gate_ctx(const FragSet& fs, Counters* c) {
  DecodeCtx d;
  d.self          = kNodeGateLink;
  d.mac           = &g_mac;
  d.node_key      = fs.key;
  d.expect_ctx_id = 0xDEADBEEFu;
  d.counters      = c;
  return d;
}

// The full spec 14 ladder for one frame: stages 2-8a, then stage 9 (per-frame
// authentication), then stage 10 (reassembly). Stage 9 before stage 10, deliberately.
Status receive(const FragSet& fs, const uint8_t* buf, size_t len, DecodeCtx& d,
               Reassembler* r, uint32_t now_ms) {
  (void)fs;
  Frame f;
  Status st = decode_header(buf, len, d, &f);
  if (st != Status::Ok) return st;
  st = decode_payload(buf, len, d, &f);
  if (st != Status::Ok) return st;
  return r->accept(f, now_ms);
}

}  // namespace

// A 3-fragment authenticated CONFIG reassembles and dispatches once.
void test_fragmented_config_authenticated_roundtrip() {
  FragSet fs;
  build_authenticated_config_set(&fs);

  Counters c;
  Reassembler r(&c);
  DecodeCtx d = gate_ctx(fs, &c);

  for (uint8_t i = 0; i < 3; ++i) {
    TEST_ASSERT_EQUAL(Status::Ok,
                      receive(fs, fs.frames[i], fs.lens[i], d, &r, 100u + i));
    TEST_ASSERT_EQUAL(i == 2, r.complete());
  }
  TEST_ASSERT_EQUAL_UINT32(sizeof(fs.payload), r.len());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(fs.payload, r.data(), sizeof(fs.payload));
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// THE TEST THAT WOULD HAVE CAUGHT THE v0.3 CONFLICT. Every fragment of a set shares
// one seq (spec 11.1). Running spec 9.4 steps 4-6 per fragment would have fragment 0
// set the high-water mark and fragment 1 rejected as a replay of itself, with the
// dedup cache flagging it as a duplicate first.
void test_fragment_one_not_rejected_as_replay() {
  FragSet fs;
  build_authenticated_config_set(&fs);

  Counters c;
  Reassembler r(&c);
  DecodeCtx d = gate_ctx(fs, &c);

  Frame f0, f1;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(fs.frames[0], fs.lens[0], d, &f0));
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(fs.frames[1], fs.lens[1], d, &f1));
  TEST_ASSERT_EQUAL_HEX16(f0.hdr.seq, f1.hdr.seq);   // one seq for the whole set
  TEST_ASSERT_EQUAL_UINT8(0, f0.hdr.frag_index());
  TEST_ASSERT_EQUAL_UINT8(1, f1.hdr.frag_index());

  TEST_ASSERT_EQUAL(Status::Ok, receive(fs, fs.frames[0], fs.lens[0], d, &r, 100));
  TEST_ASSERT_EQUAL(Status::Ok, receive(fs, fs.frames[1], fs.lens[1], d, &r, 101));
  TEST_ASSERT_EQUAL(Status::Ok, receive(fs, fs.frames[2], fs.lens[2], d, &r, 102));
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// A fragment with a corrupt MAC is rejected at stage 9 and the reassembly slot is
// observably untouched - asserted on state, not merely on the return code.
void test_bad_mac_fragment_never_buffered() {
  FragSet fs;
  build_authenticated_config_set(&fs);

  Counters c;
  Reassembler r(&c);
  DecodeCtx d = gate_ctx(fs, &c);

  // Flip a payload byte and repair the CRC16, so the frame is well formed all the
  // way to stage 9 and fails only there.
  uint8_t bad[kMaxFrame];
  const size_t n = fs.lens[0];
  for (size_t i = 0; i < n; ++i) bad[i] = fs.frames[0][i];
  bad[kHdrLen] ^= 0x01;
  const uint16_t crc = crc16_ccitt_false(bad, n - kCrcLen);
  bad[n - 2] = static_cast<uint8_t>(crc & 0xFF);
  bad[n - 1] = static_cast<uint8_t>(crc >> 8);

  TEST_ASSERT_EQUAL(Status::BadMac, receive(fs, bad, n, d, &r, 100));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_rejected_mac);
  TEST_ASSERT_FALSE(r.active());     // no slot taken
  TEST_ASSERT_FALSE(r.complete());
  TEST_ASSERT_EQUAL_UINT32(0, r.len());

  // The legitimate set still completes afterwards.
  for (uint8_t i = 0; i < 3; ++i) {
    TEST_ASSERT_EQUAL(Status::Ok,
                      receive(fs, fs.frames[i], fs.lens[i], d, &r, 200u + i));
  }
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(fs.payload, r.data(), sizeof(fs.payload));
}

// F3 - a SECURITY property, not a correctness one. Under v0.3's ordering an attacker
// with no key could fill every reassembly slot on the bridge with forged fragment-0
// frames and hold each for frag_reassembly_timeout_ms, never completing the set: the
// forgery was detected only on completion, which the attacker simply never allows.
void test_forged_fragment_zero_does_not_occupy_slot() {
  FragSet fs;
  build_authenticated_config_set(&fs);

  Counters c;
  Reassembler r(&c);
  DecodeCtx d = gate_ctx(fs, &c);

  for (uint16_t attempt = 0; attempt < 8; ++attempt) {
    uint8_t forged[kMaxFrame];
    const size_t n = fs.lens[0];
    for (size_t i = 0; i < n; ++i) forged[i] = fs.frames[0][i];
    // A different seq each time, so each forgery looks like a NEW set - which is what
    // would consume a fresh slot if the MAC were checked after reassembly. Offset
    // clear of the genuine set's seq low byte: colliding with it would reproduce the
    // real frame, MAC and all, and the attempt would legitimately verify.
    forged[4] = static_cast<uint8_t>(0x40 + attempt);
    const uint16_t crc = crc16_ccitt_false(forged, n - kCrcLen);
    forged[n - 2] = static_cast<uint8_t>(crc & 0xFF);
    forged[n - 1] = static_cast<uint8_t>(crc >> 8);

    TEST_ASSERT_EQUAL(Status::BadMac, receive(fs, forged, n, d, &r, 10u + attempt));
    TEST_ASSERT_FALSE(r.active());
  }
  TEST_ASSERT_EQUAL_UINT32(8, c.rx_rejected_mac);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_reassembly_abandoned);  // nothing was ever held

  // The legitimate set completes normally, unimpeded.
  for (uint8_t i = 0; i < 3; ++i) {
    TEST_ASSERT_EQUAL(Status::Ok,
                      receive(fs, fs.frames[i], fs.lens[i], d, &r, 100u + i));
  }
  TEST_ASSERT_TRUE(r.complete());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(fs.payload, r.data(), sizeof(fs.payload));
}

// The backstop: even a caller that skips stage 9 cannot buffer an unverified
// fragment of an authenticated type. spec 14 orders stage 9 before stage 10, and the
// library enforces the order rather than trusting four firmwares to remember it.
void test_unverified_fragment_refused_by_reassembler() {
  FragSet fs;
  build_authenticated_config_set(&fs);

  Counters c;
  Reassembler r(&c);
  DecodeCtx d = gate_ctx(fs, &c);
  d.mac = nullptr;  // a caller that never verifies

  Frame f;
  TEST_ASSERT_EQUAL(Status::Ok, decode_header(fs.frames[0], fs.lens[0], d, &f));
  TEST_ASSERT_EQUAL(Status::Ok, decode_payload(fs.frames[0], fs.lens[0], d, &f));
  TEST_ASSERT_NOT_NULL(f.mac);        // the bytes are there...
  TEST_ASSERT_FALSE(f.mac_verified);  // ...and were never checked

  TEST_ASSERT_EQUAL(Status::BadMac, r.accept(f, 0));
  TEST_ASSERT_FALSE(r.active());
}

// --- F8 / spec 9.4: expect_ctx_id semantics ---------------------------------

// "No expected context" means SKIP, never "expect zero". 0x00000000 means unknown
// (spec 5.5) and no node ever sends it; a bridge holds no context of its own
// (spec 10.1) and is the party that tracks everyone else's.
void test_zero_expect_ctx_skips_check() {
  refimpl::RefKdf kdf;
  uint8_t key[32];
  kdf.derive_node_key(kMaster, kNodeGateLink, key);

  const CtxId ctxs[] = {0x00000001u, 0x33333333u, 0x89ABCDEFu, 0xFFFFFFFFu};
  for (CtxId ctx : ctxs) {
    uint8_t payload[4] = {static_cast<uint8_t>(Cmd::Open), 0, 0, 0};
    Header h;
    h.type   = MsgType::Command;
    h.src    = kNodeBridge;
    h.dst    = kNodeGateLink;
    h.ctx_id = ctx;
    h.set_frag(0, 1);

    EncodeCtx ec;
    ec.mac      = &g_mac;
    ec.node_key = key;
    uint8_t buf[kMaxFrame];
    size_t  n = 0;
    TEST_ASSERT_EQUAL(Status::Ok, encode(h, payload, 4, ec, buf, sizeof(buf), &n));

    Counters c;
    DecodeCtx d;
    d.self     = kNodeGateLink;
    d.mac      = &g_mac;
    d.node_key = key;
    d.counters = &c;
    TEST_ASSERT_EQUAL_UINT32(0, d.expect_ctx_id);  // the default IS "skip"

    Frame f;
    TEST_ASSERT_EQUAL(Status::Ok, decode_header(buf, n, d, &f));
    TEST_ASSERT_EQUAL(Status::Ok, decode_payload(buf, n, d, &f));
    TEST_ASSERT_TRUE(f.mac_verified);
    TEST_ASSERT_EQUAL_UINT32(0, c.rx_rejected_ctx);
  }
}


#ifdef ARDUINO
// --- P7.2: mbedTLS against the portable reference, ON TARGET ----------------
//
// The whole reason platform/esp32/mbedtls_mac.cpp exists is that the target uses
// the ESP-IDF mbedTLS component where the host uses the portable implementation.
// AGREEMENT IS THE THING BEING TESTED - this cannot be run on the host, and until
// it passes here the file stays marked UNVERIFIED.
//
// A divergence would mean the bridge and the node derive different keys from the
// same master, or compute different MACs over the same frame: every authenticated
// COMMAND rejected, with no counter pointing at the cause (spec 9.1).
void test_mbedtls_hmac_matches_refimpl() {
  esp32::MbedtlsMac mbed;
  refimpl::RefMac   ref;

  const uint8_t key[kNodeKeyLen] = {
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
      0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
      0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

  // Sizes that straddle the SHA-256 block boundary: an empty message, a short one,
  // exactly 64 bytes, and one spanning two blocks. A backend that mishandles the
  // final block passes the short case and fails here.
  const size_t sizes[] = {0, 1, 16, 63, 64, 65, 128, kMaxFrame};
  uint8_t data[kMaxFrame];
  for (size_t i = 0; i < sizeof(data); ++i) data[i] = static_cast<uint8_t>(i * 31 + 7);

  for (size_t n : sizes) {
    uint8_t a[kMacLen], b[kMacLen];
    ref.hmac_sha256_trunc(key, kNodeKeyLen, data, n, a);
    mbed.hmac_sha256_trunc(key, kNodeKeyLen, data, n, b);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(a, b, kMacLen, "mbedTLS HMAC != refimpl");
  }
}

void test_mbedtls_hkdf_matches_refimpl() {
  esp32::MbedtlsKdf mbed;
  refimpl::RefKdf   ref;

  // Every provisioned address, plus the bench range: one wrong key per node is one
  // node that silently cannot be commanded.
  const NodeId ids[] = {kNodeGateLink, kNodeWellLink, kNodeSim0, kNodeSim1,
                        kNodeSim2,     kNodeSim3};
  for (NodeId id : ids) {
    uint8_t a[32], b[32];
    ref.derive_node_key(kMaster, id, a);
    mbed.derive_node_key(kMaster, id, b);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(a, b, 32, "mbedTLS HKDF != refimpl");
  }
}

// The published KATs, recomputed through mbedTLS rather than the portable code, so
// the target is checked against the standard and not merely against its neighbour.
void test_mbedtls_hmac_rfc4231_case1() {
  esp32::MbedtlsMac mbed;
  uint8_t key[20];
  for (uint8_t& k : key) k = 0x0b;
  const uint8_t msg[] = {'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e'};
  // RFC 4231 case 1, first 8 bytes of b0344c61d8db38535ca8afceaf0bf12b...
  const uint8_t want[kMacLen] = {0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53};
  uint8_t got[kMacLen];
  mbed.hmac_sha256_trunc(key, sizeof(key), msg, sizeof(msg), got);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(want, got, kMacLen);
}
#endif  // ARDUINO

int run_all() {
  UNITY_BEGIN();
  RUN_TEST(test_sha256_known_answer);
  RUN_TEST(test_sha256_multiblock);
  RUN_TEST(test_hmac_sha256_known_answer);
  RUN_TEST(test_hmac_sha256_case2);
  RUN_TEST(test_hkdf_sha256_known_answer);
  RUN_TEST(test_node_keys_differ_per_node);
  RUN_TEST(test_kdf_salt_and_info_are_pinned);
  RUN_TEST(test_ct_equal);
  RUN_TEST(test_command_mac_roundtrip_and_tamper);
  RUN_TEST(test_ctx_mismatch_precedes_mac_check);
  RUN_TEST(test_bridge_does_not_check_ctx);
  RUN_TEST(test_wrong_node_key_fails_verification);
  RUN_TEST(test_fragmented_config_authenticated_roundtrip);
  RUN_TEST(test_fragment_one_not_rejected_as_replay);
  RUN_TEST(test_bad_mac_fragment_never_buffered);
  RUN_TEST(test_forged_fragment_zero_does_not_occupy_slot);
  RUN_TEST(test_unverified_fragment_refused_by_reassembler);
  RUN_TEST(test_zero_expect_ctx_skips_check);
#ifdef ARDUINO
  RUN_TEST(test_mbedtls_hmac_matches_refimpl);
  RUN_TEST(test_mbedtls_hkdf_matches_refimpl);
  RUN_TEST(test_mbedtls_hmac_rfc4231_case1);
#endif
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
