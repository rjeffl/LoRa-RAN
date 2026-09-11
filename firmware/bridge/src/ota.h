// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// OTA over WiFi, and the verdict on a freshly flashed image. Task BF-13;
// Impl Plan 6.5, R-5.3a-e, BS-7, V-B9.

#pragma once

#include <cstddef>

#include "ota_policy.h"

namespace bridge {

// The OTA password is passed in from main.cpp, like every other credential. It is
// never logged. ArduinoOTA authenticates with an MD5 challenge-response, so the
// password does not cross the network in the clear - but MD5 is weak, and this is
// an authenticated endpoint on a LAN (R-5.3b), not a hardened one on the internet.
void ota_configure(const char* password);

// Called from ota_task's tick. Starts the listener on the first WiFi association,
// services it when R-5.3d allows, and applies the image verdict.
void ota_service(bool wifi_connected, bool lora_idle, bool mqtt_connected,
                 bool tasks_started, uint32_t uptime_ms);

// True from an upload's first byte to its reboot. BF-17's scheduler consults this
// so a poll is not started into a bridge about to restart.
bool ota_in_progress();

// What the running image is, for the version topic and the boot banner. V-B9's
// procedure reads these: after a rollback, the slot changes and git changes.
OtaImageState ota_image_state();
const char*   ota_running_slot();  // "app0" / "app1" - the partition label

// The version document published to lran/bridge/version (spec 16.2). Returns the
// length written, 0 if `cap` was too small.
size_t ota_version_json(char* out, size_t cap);

}  // namespace bridge
