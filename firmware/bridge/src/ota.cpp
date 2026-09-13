// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// OTA over WiFi, and the verdict on a freshly flashed image. Task BF-13;
// Impl Plan 6.5, R-5.3a-e, BS-7, V-B9.

#include "ota.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <esp_ota_ops.h>

#include <atomic>
#include <cstdio>

#ifndef LRAN_BRIDGE_VERSION
#error "LRAN_BRIDGE_VERSION is set in platformio.ini - build through PlatformIO"
#endif
#ifndef LRAN_BRIDGE_GIT
#define LRAN_BRIDGE_GIT "unknown"
#endif

// ---------------------------------------------------------------------------
// THE OVERRIDE THAT MAKES ROLLBACK REAL.
//
// Arduino-ESP32 2.0.x's initArduino() asks verifyRollbackLater(); its weak default
// returns false, and the core then calls verifyOta() - weak default true - and marks
// the image valid before setup() has run. Returning true here takes that decision
// away from the core and gives it to ota_verdict().
//
// extern "C" IS LOAD-BEARING. The weak default is defined in esp32-hal-misc.c, a C
// file, and no header declares it - so a C++ definition here gets a mangled name,
// does not override anything, links cleanly, and leaves the core blessing every
// image. That failure is silent until V-B9 is run, which is why the boot banner
// prints the image state: after an OTA it must read pending_verify, and `valid`
// there means this function is not the one being called.
// ---------------------------------------------------------------------------
extern "C" bool verifyRollbackLater() { return true; }

namespace bridge {
namespace {

const char*       g_password   = nullptr;
bool              g_listening  = false;
std::atomic<bool> g_in_progress{false};
bool              g_decided    = false;  // the verdict is applied once per boot
std::atomic<bool> g_pending{false};      // for readers on other tasks; see ota.h

OtaImageState read_image_state() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t   st;
  if (running == nullptr || esp_ota_get_state_partition(running, &st) != ESP_OK) {
    // A USB-flashed image lands here: boot_app0.bin resets otadata, so the running
    // slot has no recorded state. Nothing to verify, which is correct - USB is the
    // recovery path and does not need recovering from.
    return OtaImageState::NotPending;
  }
  return st == ESP_OTA_IMG_PENDING_VERIFY ? OtaImageState::PendingVerify
                                          : OtaImageState::NotPending;
}

void start_listener() {
  // Hostname on the LAN, and the mDNS name `pio run -t upload -e heltec_ota` uses.
  ArduinoOTA.setHostname("lran-bridge");
  ArduinoOTA.setPassword(g_password);

  ArduinoOTA.onStart([]() {
    g_in_progress = true;
    Serial.println(F("OTA: upload started"));
  });
  ArduinoOTA.onEnd([]() { Serial.println(F("OTA: upload complete, rebooting")); });
  ArduinoOTA.onError([](ota_error_t err) {
    g_in_progress = false;
    Serial.print(F("OTA: failed, error "));
    Serial.println(static_cast<int>(err));
  });

  ArduinoOTA.begin();
  g_listening = true;
  Serial.println(F("OTA: listening as lran-bridge"));
}

void apply_verdict(OtaVerdict v) {
  switch (v) {
    case OtaVerdict::Nothing:
    case OtaVerdict::Wait:
      return;

    case OtaVerdict::MarkValid:
      if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
        Serial.println(F("OTA: image verified - marked valid, rollback cancelled"));
      } else {
        Serial.println(F("OTA: marking the image valid FAILED - it will roll back on reset"));
      }
      g_decided = true;
      return;

    case OtaVerdict::RollBack:
      // Does not return. The bootloader boots the previous slot, which was valid when
      // this image was flashed over it.
      Serial.println(F("OTA: image did not prove itself in time - ROLLING BACK"));
      Serial.flush();
      esp_ota_mark_app_invalid_rollback_and_reboot();
      g_decided = true;  // reached only if the call failed
      return;
  }
}

}  // namespace

void ota_configure(const char* password) { g_password = password; }

void ota_service(bool wifi_connected, bool lora_idle, bool mqtt_connected,
                 bool tasks_started, uint32_t uptime_ms) {
  // The verdict runs whether or not WiFi is up: an image that NEVER associates is
  // precisely the one that must be rolled back.
  if (!g_decided) {
    OtaHealth h;
    h.uptime_ms      = uptime_ms;
    h.tasks_started  = tasks_started;
    h.mqtt_connected = mqtt_connected;
    const OtaVerdict v = ota_verdict(read_image_state(), h);
    if (v == OtaVerdict::Nothing) {
      g_decided = true;
    } else {
      apply_verdict(v);
    }
    g_pending = !g_decided;
  }

  if (!wifi_connected) {
    return;
  }
  if (!g_listening) {
    start_listener();
  }

  // R-5.3d. handle() is where ArduinoOTA answers an upload invitation, so not
  // calling it while a LoRa transaction is outstanding is what defers the upload.
  // espota retries its invitation for several seconds, and a transaction at SF9 is
  // well under that. An upload already in progress runs inside handle() to
  // completion and is not interrupted by this check.
  if (ota_may_start(wifi_connected, lora_idle) || g_in_progress) {
    ArduinoOTA.handle();
  }
}

bool ota_in_progress() { return g_in_progress; }

bool ota_verify_pending() { return g_pending; }

OtaImageState ota_image_state() { return read_image_state(); }

const char* ota_running_slot() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running != nullptr ? running->label : "unknown";
}

size_t ota_version_json(char* out, size_t cap) {
  if (out == nullptr || cap == 0) {
    return 0;
  }
  // spec 16.2 names lran/bridge/version but not its payload; this document is the
  // bridge's choice and is recorded as a gap in docs/bridge/engineering-log.md. It
  // carries the slot and the image state because those are what V-B9 reads.
  const int n = std::snprintf(
      out, cap, "{\"version\":\"%s\",\"git\":\"%s\",\"slot\":\"%s\",\"ota_state\":\"%s\"}",
      LRAN_BRIDGE_VERSION, LRAN_BRIDGE_GIT, ota_running_slot(),
      g_decided && read_image_state() == OtaImageState::NotPending
          ? "verified_or_usb"
          : ota_state_name(read_image_state()));
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

}  // namespace bridge
