// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Bridge Node, node 0x00 - boot, banner, network and OTA configuration, and task
// creation. Tasks BF-10 to BF-13; milestone B2.
//
// Init order and task creation. The radio is brought up by lora_task itself
// (lora_link.h, BF-16) and the OLED page by ui_task (BF-14); the task table and the
// queue boundaries are in tasks.h and queues.h, the network in wifi_link.h and
// mqtt_transport.h, OTA in ota.h.
//
// THE ONLY TRANSLATION UNIT THAT INCLUDES secrets.h. Everything else takes what it
// needs as an argument, which keeps the number of files that could log a credential
// at one - and this one prints only the SSID and the broker address.

#include <Arduino.h>

#include "lran/link/chan_monitor.h"
#include "ota.h"
#include "radio_config.h"
#include "registry_runtime.h"
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
  Serial.println(F("Binding spec: LRAN-Protocol-Specification v0.12 (ver = 2)"));

  // D1, closed 2026-09-10 (Decision Register 3.4). Printed because a log with no
  // record of the channel is a log that cannot be compared with another one - the
  // same reason the range test prints its own configuration.
  Serial.println(F("PHY: 917.4 MHz, SF9, BW 125 kHz, CR 4/5, -4 dBm conducted, 3.0 dBi"));

  if (master_key_is_placeholder()) {
    Serial.println(F("*** LRAN_MASTER_KEY IS THE ALL-ZERO PLACEHOLDER ***"));
    Serial.println(F("*** This build cannot authenticate any node. Fill in secrets.h. ***"));
  }

  // BF-13. Which image is running, and whether it still has to prove itself. V-B9's
  // procedure reads these three lines. After an OTA the state MUST read
  // pending_verify: if it reads not_pending on the first boot of a fresh upload, the
  // core has already blessed the image and ota.cpp's verifyRollbackLater() override is
  // not the symbol being called.
  Serial.print(F("Version: "));
  Serial.print(F(LRAN_BRIDGE_VERSION));
  Serial.print(F(" ("));
  Serial.print(F(LRAN_BRIDGE_GIT));
  Serial.println(')');
  Serial.print(F("Slot: "));
  Serial.println(bridge::ota_running_slot());
  Serial.print(F("Image state: "));
  Serial.println(bridge::ota_state_name(bridge::ota_image_state()));

  // M25 - the capture file's header. A six-to-twelve-hour capture is read by a tool
  // months later on a machine that has none of this context, so the file has to say what
  // produced it: which image, which channel, which bucket length and which threshold.
  // Printed at every boot, so a capture that spans a reboot carries two of them and the
  // reboot is visible rather than inferred from a timestamp going backwards.
  {
    char line[160];
    if (lran::link::render_chan_boot(LRAN_BRIDGE_GIT, bridge::kPhy.freq_hz, bridge::kPhy.sf,
                                 bridge::kPhy.bw_khz10, bridge::kLoraMaxWaitMs, line,
                                 sizeof(line)) > 0) {
      Serial.println(line);
    }
  }

#if defined(LRAN_V_B9_BAD_IMAGE) && LRAN_V_B9_BAD_IMAGE == 2
  // V-B9, the bootloader path. Abort before anything else runs; the reset lands in
  // PENDING_VERIFY and the bootloader boots the previous slot. If this line prints
  // twice, rollback is not enabled in the bootloader that is on this board.
  Serial.println(F("*** V-B9 BAD IMAGE (panic) - aborting; expect a rollback ***"));
  Serial.flush();
  abort();
#endif

  bridge::ota_configure(OTA_PASSWORD);

#if defined(LRAN_V_B9_BAD_IMAGE) && LRAN_V_B9_BAD_IMAGE == 1
  // V-B9, this firmware's verdict. The network is never configured, so the broker is
  // never reached, nothing marks the image valid, and ota_task rolls it back at the
  // shortened deadline set for this environment.
  Serial.println(F("*** V-B9 BAD IMAGE (no network) - expect a rollback at the deadline ***"));
#else
  // BF-12. WiFi and the broker client are configured here, from the only place that
  // sees secrets.h, and NEITHER CONNECTS YET: association and the MQTT handshake
  // happen in mqtt_task on its own backoff. Boot does not wait for a network, and a
  // bridge with no AP still receives LoRa (R-3.2b).
  //
  // Nothing below logs a credential. The SSID is printed because it is the one field
  // whose value makes a connection failure diagnosable; the password, the broker
  // credentials, the OTA password and the master key are never printed anywhere in
  // this firmware.
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
#endif

  // BF-15. Before start_tasks(): lora_task reads the keys without a lock, which is safe
  // only because they are complete before it runs. Node addresses are printed, never a
  // key.
  {
    const uint8_t master[] = LRAN_MASTER_KEY;
    static_assert(sizeof(master) == lran::kMasterKeyLen,
                  "secrets.h: LRAN_MASTER_KEY must be 32 bytes (spec 9.1)");
    if (!bridge::registry_begin(master)) {
      Serial.println(F("FATAL: registry lock creation failed. Halting."));
      for (;;) {
        delay(1000);
      }
    }
  }
  Serial.print(F("Registry:"));
  for (size_t i = 0; i < bridge::registry_size(); ++i) {
    const bridge::NodeInfo& n = bridge::registry_info_at(i);
    Serial.printf(" 0x%02X%s", n.id, n.is_bench ? "(bench)" : "");
  }
  Serial.println();

  // BF-32. Before start_tasks(): mqtt_task answers `config/set` out of this store, and a
  // set arriving before it is open would be answered from the defaults and saved nowhere.
  // A store that will not open is not fatal - spec 8.11 makes that APPLIED_NOT_PERSISTED
  // rather than a bridge that refuses to run.
  Serial.printf("Config: %u stored value(s) restored\n",
                static_cast<unsigned>(bridge::config_begin()));

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
  Serial.println(F("BF-15: WiFi, MQTT, OTA, the radio link and the registry. No discovery yet."));
}

void loop() {
  // Nothing runs here by design. The seven tasks own the work (Impl Plan 5.2), and
  // the Arduino loop task sits below all of them; code added here would run at a
  // priority chosen by Arduino rather than by the table.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
