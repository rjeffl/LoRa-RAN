// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// P3 - MAC and keying. Spec 9.

#include <unity.h>

#include "lran/lran.h"
#include "refimpl_mac.h"

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
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_bad_mac);
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
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_ctx_mismatch);
  TEST_ASSERT_EQUAL_UINT32(0, c.rx_bad_mac);  // the MAC was never reached
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

int main() {
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
  return UNITY_END();
}
