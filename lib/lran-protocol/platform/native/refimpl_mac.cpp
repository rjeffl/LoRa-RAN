// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Public-domain-style SHA-256 (FIPS 180-4) plus HMAC and HKDF, written out here so
// the native environment needs no crypto dependency at all.

#include "refimpl_mac.h"

namespace lran {
namespace refimpl {
namespace {

constexpr uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha256 {
  uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  uint8_t  block[64] = {};
  size_t   used = 0;
  uint64_t total_bits = 0;

  void compress(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(p[4 * i]) << 24) |
             (static_cast<uint32_t>(p[4 * i + 1]) << 16) |
             (static_cast<uint32_t>(p[4 * i + 2]) << 8) |
             static_cast<uint32_t>(p[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = hh + S1 + ch + kK[i] + w[i];
      const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = S0 + maj;
      hh = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }

  void update(const uint8_t* data, size_t len) {
    total_bits += static_cast<uint64_t>(len) * 8;
    for (size_t i = 0; i < len; ++i) {
      block[used++] = data[i];
      if (used == 64) { compress(block); used = 0; }
    }
  }

  void finish(uint8_t out[32]) {
    const uint64_t bits = total_bits;
    block[used++] = 0x80;  // update() compresses at 64, so `used` is < 64 here
    if (used > 56) {
      while (used < 64) block[used++] = 0;
      compress(block);
      used = 0;
    }
    while (used < 56) block[used++] = 0;
    for (int i = 0; i < 8; ++i) {
      block[56 + i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    }
    compress(block);
    for (int i = 0; i < 8; ++i) {
      out[4 * i]     = static_cast<uint8_t>(h[i] >> 24);
      out[4 * i + 1] = static_cast<uint8_t>(h[i] >> 16);
      out[4 * i + 2] = static_cast<uint8_t>(h[i] >> 8);
      out[4 * i + 3] = static_cast<uint8_t>(h[i]);
    }
  }
};

}  // namespace

void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  Sha256 s;
  s.update(data, len);
  s.finish(out);
}

void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* data,
                 size_t data_len, uint8_t out[32]) {
  uint8_t k[64] = {};
  if (key_len > 64) {
    sha256(key, key_len, k);
  } else {
    for (size_t i = 0; i < key_len; ++i) k[i] = key[i];
  }
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; ++i) {
    ipad[i] = static_cast<uint8_t>(k[i] ^ 0x36);
    opad[i] = static_cast<uint8_t>(k[i] ^ 0x5c);
  }
  uint8_t inner[32];
  {
    Sha256 s;
    s.update(ipad, 64);
    s.update(data, data_len);
    s.finish(inner);
  }
  Sha256 s;
  s.update(opad, 64);
  s.update(inner, 32);
  s.finish(out);
}

void hkdf_sha256(const uint8_t* salt, size_t salt_len, const uint8_t* ikm,
                 size_t ikm_len, const uint8_t* info, size_t info_len, uint8_t* out,
                 size_t out_len) {
  uint8_t prk[32];
  hmac_sha256(salt, salt_len, ikm, ikm_len, prk);  // extract

  // expand, one 32-byte block: T(1) = HMAC(prk, info || 0x01)
  uint8_t t_in[256];
  size_t  n = 0;
  for (size_t i = 0; i < info_len && n < sizeof(t_in) - 1; ++i) t_in[n++] = info[i];
  t_in[n++] = 0x01;
  uint8_t t[32];
  hmac_sha256(prk, 32, t_in, n, t);
  for (size_t i = 0; i < out_len && i < 32; ++i) out[i] = t[i];
}

void RefMac::hmac_sha256_trunc(const uint8_t* key, size_t key_len, const uint8_t* data,
                               size_t data_len, uint8_t out[kMacLen]) {
  uint8_t full[32];
  hmac_sha256(key, key_len, data, data_len, full);
  // spec 9.3 - truncate to the FIRST kMacLen bytes.
  for (size_t i = 0; i < kMacLen; ++i) out[i] = full[i];
}

void RefKdf::derive_node_key(const uint8_t master[32], NodeId id, uint8_t out[32]) {
  // spec 9.1 - node_key = HKDF-SHA256(master, salt "lran-v1", info "node-" || node_id)
  //
  // `||` is byte concatenation throughout the specification, so `node_id` is the raw
  // address byte, not its ASCII rendering. The Python side of the W4 vectors must
  // agree: get this wrong and every node is provisioned with a key the bridge cannot
  // reproduce, and the only symptom is that every command is rejected.
  uint8_t info[kKdfInfoLen];
  for (size_t i = 0; i < kKdfInfoPrefixLen; ++i) {
    info[i] = static_cast<uint8_t>(kKdfInfoPrefix[i]);
  }
  info[kKdfInfoPrefixLen] = id;

  hkdf_sha256(reinterpret_cast<const uint8_t*>(kKdfSalt), kKdfSaltLen, master,
              kMasterKeyLen, info, kKdfInfoLen, out, kNodeKeyLen);
}

}  // namespace refimpl
}  // namespace lran
