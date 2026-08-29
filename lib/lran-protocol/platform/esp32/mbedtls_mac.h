// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// ESP32 IMac / IKdf backed by the ESP-IDF mbedTLS component. Spec 9.1, 9.3.
//
// NOT part of the library proper - include/lran/ must stay free of any ESP-IDF
// header so the native environment keeps building (repo rule 7). A firmware project
// adds this file to its own build.
//
// UNVERIFIED: this file has not been compiled. It is built and checked at milestone
// P7 (target build), which is out of scope for the session that wrote it.

#pragma once

#include "lran/mac.h"

namespace lran {
namespace esp32 {

class MbedtlsMac final : public IMac {
 public:
  void hmac_sha256_trunc(const uint8_t* key, size_t key_len, const uint8_t* data,
                         size_t data_len, uint8_t out[kMacLen]) override;
};

class MbedtlsKdf final : public IKdf {
 public:
  void derive_node_key(const uint8_t master[32], NodeId id, uint8_t out[32]) override;
};

}  // namespace esp32
}  // namespace lran
