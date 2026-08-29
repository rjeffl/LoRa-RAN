// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// The fixed W4 test master key. Task list P6.4.
//
// ############################################################################
// #  TEST-ONLY. THIS KEY IS PUBLIC. IT IS COMMITTED IN THE CLEAR ON PURPOSE.  #
// #  IT MUST NEVER REACH HARDWARE.                                           #
// ############################################################################
//
// This is NOT secrets.h. It is a test fixture: the W4 vectors are worthless without
// a fixed master key both implementations derive from, and a key that is not in the
// repo cannot be one. The 32 bytes spell "LRAN-TEST-ONLY-MASTER-KEY-DO-NOT" in
// ASCII, so it announces itself in a hex dump or a logic-analyser capture instead of
// looking like real key material.
//
// The authoritative copy is /tools/vectors/test_master_key.json; this header must
// agree with it, and kTestMasterKeyMatchesFixture below is asserted against the
// vector files at runtime.

#pragma once

// A target build reaching this header means the test fixture has been pulled into
// firmware. Provisioning a node with this key means anyone who can read the repo can
// forge a COMMAND, which for GateLink is a relay pulse at the gate. Recovery is a USB
// reflash of every node.
//
// PlatformIO defines UNIT_TEST for `pio test` builds only, so this fires the moment
// the header is included from a firmware target.
#if !defined(UNIT_TEST) && !defined(LRAN_ALLOW_TEST_KEY)
#error "TEST-ONLY master key included in a non-test build. This key must never reach hardware - see /tools/vectors/README.md and task P6.4."
#endif

#include <cstdint>
#include <cstddef>

namespace lran_test {

// "LRAN-TEST-ONLY-MASTER-KEY-DO-NOT"
inline constexpr uint8_t kTestMasterKey[32] = {
    0x4c, 0x52, 0x41, 0x4e, 0x2d, 0x54, 0x45, 0x53,  // L R A N - T E S
    0x54, 0x2d, 0x4f, 0x4e, 0x4c, 0x59, 0x2d, 0x4d,  // T - O N L Y - M
    0x41, 0x53, 0x54, 0x45, 0x52, 0x2d, 0x4b, 0x45,  // A S T E R - K E
    0x59, 0x2d, 0x44, 0x4f, 0x2d, 0x4e, 0x4f, 0x54,  // Y - D O - N O T
};

inline constexpr size_t kTestMasterKeyLen = sizeof(kTestMasterKey);

}  // namespace lran_test
