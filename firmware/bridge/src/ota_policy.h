// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// When a freshly flashed image is kept, and when it is rolled back. Task BF-13;
// R-5.3c, R-5.3d, BS-7, V-B9.
//
// ARDUINO-FREE, because this is the decision V-B9 exists to test, and a decision
// that can only be exercised by flashing a board is one that gets exercised once.
//
// THE PROBLEM THIS SOLVES IS ARDUINO'S DEFAULT. With rollback enabled in the
// bootloader, a new image boots in PENDING_VERIFY and the bootloader rolls it back
// if it resets before being marked valid. Arduino-ESP32 2.0.x marks it valid itself,
// in initArduino(), BEFORE setup() runs - its weak verifyOta() returns true. So out
// of the box, any image that reaches initArduino() is blessed, including one that
// never finds the network again. That is the one failure that matters on this node:
// a bridge that cannot reach the LAN cannot be OTA'd back, and recovery is a USB
// cable. ota.cpp overrides verifyRollbackLater() so the verdict below is made by
// this firmware instead.

#pragma once

#include <cstdint>

namespace bridge {

// What esp_ota_get_state_partition() says about the running image, reduced to the
// three cases the verdict distinguishes.
enum class OtaImageState : uint8_t {
  // Nothing to decide: a USB-flashed image (otadata reset by boot_app0.bin), a state
  // the query could not read, or an image already verified on an earlier boot.
  NotPending = 0,
  PendingVerify,  // first boot after an OTA; the bootloader will roll back on reset
};

struct OtaHealth {
  uint32_t uptime_ms      = 0;
  bool     tasks_started  = false;
  bool     mqtt_connected = false;

  // TODO(BF-16): radio_ok - SX1262 init succeeded against the injected RadioPins.
  // An image that reaches the broker with a dead radio is reachable, which is what
  // makes it RECOVERABLE by another OTA; that is why reachability is the verdict
  // today. Once the radio exists, an image that cannot hear the fleet is a bad image
  // too, and it belongs in the verdict.
};

enum class OtaVerdict : uint8_t {
  Nothing = 0,  // not pending; nothing to decide
  Wait,         // pending, and not yet proven either way
  MarkValid,    // keep this image
  RollBack,     // mark invalid and reboot into the previous image
};

// The two numbers, and why.
//
// MINIMUM UPTIME, 120 s. An image that connects and then falls over - a stack
// overflow on the first discovery publish, a panic on the first poll - must not be
// blessed on the strength of its first few seconds. Two minutes covers connect,
// discovery and the fleet's first poll cycle at its fastest.
//
// DEADLINE, 600 s. Long enough that a slow AP and a broker on its 30 s reconnect
// backoff do not roll back a good image. When it DOES fire on a good image - the
// broker was down for ten minutes during the update - the result is the previous
// image, which was known good, and a repeat of the update later. That false
// rollback is the safe direction, and it is chosen over its opposite.
//
// Overridable at build time ONLY so V-B9 does not take ten minutes per case
// (platformio.ini, [env:v_b9_bad_image]). The bridge has OTA (D16), so root rule 8's
// runtime-configurability requirement - written for nodes that cannot be reflashed
// without a walk - does not bind these.
#ifndef LRAN_OTA_MIN_UPTIME_MS
#define LRAN_OTA_MIN_UPTIME_MS 120000u
#endif
#ifndef LRAN_OTA_VERIFY_DEADLINE_MS
#define LRAN_OTA_VERIFY_DEADLINE_MS 600000u
#endif

inline constexpr uint32_t kOtaMinUptimeMs      = LRAN_OTA_MIN_UPTIME_MS;
inline constexpr uint32_t kOtaVerifyDeadlineMs = LRAN_OTA_VERIFY_DEADLINE_MS;

static_assert(kOtaVerifyDeadlineMs > kOtaMinUptimeMs,
              "an image must have time to prove itself before the deadline");

OtaVerdict ota_verdict(OtaImageState state, const OtaHealth& health);

// R-5.3d - an OTA does not START while a LoRa transaction is outstanding. An upload
// already running is not interrupted; the rule is about when to accept one.
bool ota_may_start(bool wifi_connected, bool lora_idle);

// Short names for the version topic and the boot log. Stable tokens - V-B9's
// procedure reads them.
const char* ota_state_name(OtaImageState state);
const char* ota_verdict_name(OtaVerdict verdict);

}  // namespace bridge
