// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The OTA image verdict. Task BF-13; R-5.3c, R-5.3d, V-B9.

#include "ota_policy.h"

namespace bridge {

OtaVerdict ota_verdict(OtaImageState state, const OtaHealth& health) {
  if (state != OtaImageState::PendingVerify) {
    return OtaVerdict::Nothing;
  }

  const bool healthy = health.tasks_started && health.mqtt_connected && health.radio_ok;

  // Proven: healthy AND old enough. Health alone is not enough - an image that
  // connects and then falls over must not be blessed in its first few seconds.
  if (healthy && health.uptime_ms >= kOtaMinUptimeMs) {
    return OtaVerdict::MarkValid;
  }

  // Out of time. Checked after MarkValid so an image that becomes healthy on the
  // same tick as the deadline is kept rather than thrown away on a technicality.
  if (health.uptime_ms >= kOtaVerifyDeadlineMs) {
    return OtaVerdict::RollBack;
  }

  return OtaVerdict::Wait;
}

bool ota_may_start(bool wifi_connected, bool lora_idle) {
  return wifi_connected && lora_idle;
}

const char* ota_state_name(OtaImageState state) {
  switch (state) {
    case OtaImageState::NotPending:    return "not_pending";
    case OtaImageState::PendingVerify: return "pending_verify";
  }
  return "unknown";
}

const char* ota_verdict_name(OtaVerdict verdict) {
  switch (verdict) {
    case OtaVerdict::Nothing:   return "nothing";
    case OtaVerdict::Wait:      return "wait";
    case OtaVerdict::MarkValid: return "mark_valid";
    case OtaVerdict::RollBack:  return "roll_back";
  }
  return "unknown";
}

}  // namespace bridge
