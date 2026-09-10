// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Bridge Node, node 0x00 - boot and banner. Task BF-10, milestone B2.
//
// This is the skeleton and nothing more. Task creation (BF-11), WiFi and MQTT
// (BF-12), OTA (BF-13) and the OLED page (BF-14) each land in their own files; what
// belongs here when they do is task creation and init order, per Impl Plan 5.3.

#include <Arduino.h>

// spec 9.1 and Impl Plan 11.4 - the master key, WiFi and broker credentials. This is
// the only firmware in the repo that holds any of them.
//
// __has_include rather than a bare #include: a fresh clone has no secrets.h, and
// "no such file: secrets.h" does not tell anyone what to do about it.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h not found. Run `cp secrets.h.example secrets.h` at the repo root and fill it in - see LRAN-Bridge_Node-Implementation-Plan 11.4. Never commit the copy."
#endif

namespace {

// The template ships a 32-byte key of zeros, and it compiles. That is deliberate -
// CI builds this target against the committed template so the firmware job needs no
// secret (root CLAUDE.md, "no secrets are needed or available").
//
// The cost is a build that looks healthy and cannot authenticate anything, so the
// check is at boot rather than at compile time: a placeholder build FLASHED to a
// board says so on every line of its log, in the one place a person is looking.
//
// Not a compile-time check, because the value is a brace-enclosed initializer and
// the preprocessor cannot see into it. Not a build failure either - that would make
// the CI copy pointless.
bool master_key_is_placeholder() {
  const uint8_t key[] = LRAN_MASTER_KEY;
  for (size_t i = 0; i < sizeof(key); ++i) {
    if (key[i] != 0x00) {
      return false;
    }
  }
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);  // USB CDC enumeration on a cold boot, before the first println

  // The banner names the specification version this firmware was built against.
  // tools/checks/spec_citation_version.py reads this line, so it cannot drift from
  // the specification's own header without CI saying so - the range test earned that
  // check by shipping "v0.7" in its banner long after the spec had moved.
  Serial.println(F("LRAN Bridge Node - node 0x00"));
  Serial.println(F("Binding spec: LRAN-Protocol-Specification v0.10 (ver = 2)"));

  // D1, closed 2026-09-10 (Decision Register 3.4). Printed because a log with no
  // record of the channel is a log that cannot be compared with another one - the
  // same reason the range test prints its own configuration.
  Serial.println(F("PHY: 917.4 MHz, SF9, BW 125 kHz, CR 4/5, -4 dBm conducted, 3.0 dBi"));

  if (master_key_is_placeholder()) {
    Serial.println(F("*** LRAN_MASTER_KEY IS THE ALL-ZERO PLACEHOLDER ***"));
    Serial.println(F("*** This build cannot authenticate any node. Fill in secrets.h. ***"));
  }

  Serial.println(F("BF-10 skeleton: no tasks, no radio, no WiFi, no MQTT yet."));
}

void loop() {
  // BF-11 replaces this with task creation; until then the sketch exists to prove
  // the project builds, links the shared codec and boots.
  delay(1000);
}
