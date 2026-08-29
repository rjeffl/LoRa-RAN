// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Injected crypto. Spec 9.1, 9.3.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config.h"
#include "lran/types.h"

namespace lran {

// spec 9.3 - HMAC-SHA256 truncated to the first kMacLen bytes, computed over the
// entire 16-byte header followed by the entire payload, exactly as on the wire.
//
// Injected rather than called directly because mbedTLS is an ESP-IDF component and
// this library must keep building in the native environment (repo rule 7).
class IMac {
 public:
  virtual ~IMac() = default;
  virtual void hmac_sha256_trunc(const uint8_t* key, size_t key_len,
                                 const uint8_t* data, size_t data_len,
                                 uint8_t out[kMacLen]) = 0;
};

// spec 9.1 - node_key = HKDF-SHA256(master_key, salt = kKdfSalt, info = kKdfInfoPrefix || node_id)
class IKdf {
 public:
  virtual ~IKdf() = default;
  virtual void derive_node_key(const uint8_t master[32], NodeId id,
                               uint8_t out[32]) = 0;
};

// spec 9.1 - FIXED across every `ver` bump. This is a key-derivation domain
// separator, not a wire version. Changing it silently invalidates every provisioned
// node in the field and the symptom is "every command is rejected" with nothing
// pointing at the cause. On a fleet with no OTA, recovery is a USB reflash at each
// node.
inline constexpr char    kKdfSalt[]       = "lran-v1";
inline constexpr size_t  kKdfSaltLen      = sizeof(kKdfSalt) - 1;  // 7, no NUL
inline constexpr char    kKdfInfoPrefix[] = "node-";
inline constexpr size_t  kKdfInfoPrefixLen = sizeof(kKdfInfoPrefix) - 1;  // 5, no NUL
inline constexpr size_t  kKdfInfoLen      = kKdfInfoPrefixLen + 1;
inline constexpr size_t  kMasterKeyLen    = 32;
inline constexpr size_t  kNodeKeyLen      = 32;

// spec 9.1 states the HKDF output length explicitly: L = 32. v0.4 never did, and the
// independent W4 generator had to infer it from the width HMAC-SHA256 consumes. It
// inferred correctly, but getting it wrong is undetectable by inspection - the two
// sides derive different keys, every authenticated frame fails its MAC, and no
// counter points at key derivation.
static_assert(kNodeKeyLen == 32, "spec 9.1 - HKDF output length L is 32");
static_assert(kMasterKeyLen == 32, "spec 9.1 - master key is 32 bytes");

// Constant-time equality. spec 9.4 step 3 requires the MAC comparison itself be
// constant time; a short-circuiting memcmp leaks the length of the matching prefix.
bool ct_equal(const uint8_t* a, const uint8_t* b, size_t len);

}  // namespace lran
