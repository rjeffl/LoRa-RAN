// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Spec 16.3, D67.

#include "boot_count.h"

#include <Preferences.h>

namespace bridge {
namespace {

inline constexpr const char* kBootNamespace = "boot";
inline constexpr const char* kBootKey       = "count";

}  // namespace

uint32_t boot_count_advance() {
  Preferences prefs;
  if (!prefs.begin(kBootNamespace, /*readOnly=*/false)) return 0;
  uint32_t n = prefs.getUInt(kBootKey, 0) + 1;
  // A counter that wrapped to 0 would read as unavailable; 1 is a new key either way.
  if (n == 0) n = 1;
  const bool ok = prefs.putUInt(kBootKey, n) == sizeof(n);
  prefs.end();
  return ok ? n : 0;
}

}  // namespace bridge
