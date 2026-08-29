// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Verified on target at P7.2 - see mbedtls_mac.h.

#include "mbedtls_mac.h"

#include <mbedtls/md.h>

namespace lran {
namespace esp32 {

void MbedtlsMac::hmac_sha256_trunc(const uint8_t* key, size_t key_len,
                                   const uint8_t* data, size_t data_len,
                                   uint8_t out[kMacLen]) {
  uint8_t full[32] = {};
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_hmac(info, key, key_len, data, data_len, full);
  // spec 9.3 - the FIRST kMacLen bytes.
  for (size_t i = 0; i < kMacLen; ++i) out[i] = full[i];
}

void MbedtlsKdf::derive_node_key(const uint8_t master[32], NodeId id,
                                 uint8_t out[32]) {
  // spec 9.1 - info is "node-" followed by the RAW address byte. `||` is byte
  // concatenation throughout the specification, not string formatting.
  uint8_t info[kKdfInfoLen];
  for (size_t i = 0; i < kKdfInfoPrefixLen; ++i) {
    info[i] = static_cast<uint8_t>(kKdfInfoPrefix[i]);
  }
  info[kKdfInfoPrefixLen] = id;

  const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  // HKDF-SHA256 built from HMAC rather than mbedtls_hkdf(), because
  // MBEDTLS_HKDF_C is NOT enabled in Arduino-ESP32's prebuilt mbedTLS: the header
  // is present and the file compiles, but the symbol is missing at link. Found at
  // P7.2 - it cannot be found on the host, which is why this file was marked
  // UNVERIFIED until it had been built and run on target.
  //
  // Building it here from mbedtls_md_hmac, which IS available, keeps the
  // derivation independent of an optional mbedTLS module and therefore of any
  // future sdkconfig change. RFC 5869 §2.

  // Extract: PRK = HMAC(salt, IKM)
  uint8_t prk[32];
  mbedtls_md_hmac(md, reinterpret_cast<const uint8_t*>(kKdfSalt), kKdfSaltLen, master,
                  kMasterKeyLen, prk);

  // Expand: T(1) = HMAC(PRK, info || 0x01). kNodeKeyLen is exactly one SHA-256
  // block, so a single iteration produces the whole key; the loop is written out
  // anyway so a longer key length cannot silently truncate.
  uint8_t t[32];
  size_t  done = 0;
  uint8_t counter = 1;
  while (done < kNodeKeyLen) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, md, 1);  // 1 = HMAC
    mbedtls_md_hmac_starts(&ctx, prk, sizeof(prk));
    if (done > 0) mbedtls_md_hmac_update(&ctx, t, sizeof(t));  // T(n-1)
    mbedtls_md_hmac_update(&ctx, info, kKdfInfoLen);
    mbedtls_md_hmac_update(&ctx, &counter, 1);
    mbedtls_md_hmac_finish(&ctx, t);
    mbedtls_md_free(&ctx);

    const size_t take = (kNodeKeyLen - done < sizeof(t)) ? kNodeKeyLen - done : sizeof(t);
    for (size_t i = 0; i < take; ++i) out[done + i] = t[i];
    done += take;
    ++counter;
  }
}

}  // namespace esp32
}  // namespace lran
