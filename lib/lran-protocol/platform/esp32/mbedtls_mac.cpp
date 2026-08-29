// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// UNVERIFIED until milestone P7. See mbedtls_mac.h.

#include "mbedtls_mac.h"

#include <mbedtls/hkdf.h>
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
  mbedtls_hkdf(md, reinterpret_cast<const uint8_t*>(kKdfSalt), kKdfSaltLen, master,
               kMasterKeyLen, info, kKdfInfoLen, out, kNodeKeyLen);
}

}  // namespace esp32
}  // namespace lran
