// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Host-side reference IMac / IKdf. Spec 9.1, 9.3.
//
// NOT part of the library proper: it lives under platform/ so that nothing in
// include/lran/ ever depends on a crypto implementation. The ESP32 build links
// platform/esp32/mbedtls_mac.cpp instead; this one exists for the native tests and
// the W4 vector generator.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/mac.h"

namespace lran {
namespace refimpl {

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* data,
                 size_t data_len, uint8_t out[32]);
// RFC 5869 extract-then-expand, SHA-256, for output lengths up to 32 bytes - which
// is all spec 9.1 asks for (a 32-byte node key).
void hkdf_sha256(const uint8_t* salt, size_t salt_len, const uint8_t* ikm,
                 size_t ikm_len, const uint8_t* info, size_t info_len, uint8_t* out,
                 size_t out_len);

class RefMac final : public IMac {
 public:
  void hmac_sha256_trunc(const uint8_t* key, size_t key_len, const uint8_t* data,
                         size_t data_len, uint8_t out[kMacLen]) override;
};

class RefKdf final : public IKdf {
 public:
  void derive_node_key(const uint8_t master[32], NodeId id, uint8_t out[32]) override;
};

}  // namespace refimpl
}  // namespace lran
