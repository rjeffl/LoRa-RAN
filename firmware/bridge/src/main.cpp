// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Bridge Node, node 0x00 - boot, banner, network configuration and task creation.
// Tasks BF-10, BF-11 and BF-12; milestone B2.
//
// Init order and task creation. The radio (BF-16), OTA (BF-13) and the OLED page
// (BF-14) each land in their own files; the task table and the queue boundaries are
// in tasks.h and queues.h, and the network in wifi_link.h and mqtt_transport.h.
//
// THE ONLY TRANSLATION UNIT THAT INCLUDES secrets.h. Everything else takes what it
// needs as an argument, which keeps the number of files that could log a credential
// at one - and this one prints only the SSID and the broker address.

#include <Arduino.h>

#include "task_runtime.h"
#include "tasks.h"

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

  // BF-12. WiFi and the broker client are configured here, from the only place that
  // sees secrets.h, and NEITHER CONNECTS YET: association and the MQTT handshake
  // happen in mqtt_task on its own backoff. Boot does not wait for a network, and a
  // bridge with no AP still receives LoRa (R-3.2b).
  //
  // Nothing below logs a credential. The SSID is printed because it is the one field
  // whose value makes a connection failure diagnosable; the password, the broker
  // credentials and the master key are never printed anywhere in this firmware.
  if (!bridge::net_begin(WIFI_SSID, WIFI_PASSWORD, MQTT_HOST, MQTT_PORT, MQTT_USER,
                         MQTT_PASSWORD)) {
    Serial.println(F("FATAL: network configuration rejected. Halting."));
    for (;;) {
      delay(1000);
    }
  }
  Serial.print(F("WiFi SSID: "));
  Serial.println(WIFI_SSID);
  Serial.print(F("MQTT broker: "));
  Serial.print(MQTT_HOST);
  Serial.print(':');
  Serial.println(MQTT_PORT);

  // BF-11. Task creation is the last thing setup() does: everything a task might
  // touch is initialized above it, and after this line the Arduino loop is the
  // lowest-value thing running.
  if (!bridge::start_tasks()) {
    // Static allocation, so a failure here is a table defect - a depth, a stack
    // array that does not match its declared size - and not a runtime condition
    // that might clear. Halting beats running a fleet with one task missing.
    Serial.println(F("FATAL: task or queue creation failed. Halting."));
    for (;;) {
      delay(1000);
    }
  }

  Serial.print(F("Tasks started: "));
  Serial.println(static_cast<unsigned>(bridge::kTaskCount));
  Serial.println(F("BF-12: WiFi and MQTT up. No radio, no discovery, no OTA yet."));
}

void loop() {
  // Nothing runs here by design. The seven tasks own the work (Impl Plan 5.2), and
  // the Arduino loop task sits below all of them; code added here would run at a
  // priority chosen by Arduino rather than by the table.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
