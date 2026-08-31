// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// firmware/range-test/ - LRAN range test, pass 1. Branch 1 (R1-R3).
//
// Tasks: docs/rangetest/LRAN-Range-Test-Firmware-Pass1-Tasks.md
//
// THIS FIRMWARE NEVER SHIPS and is not the seed of bridge firmware (task guardrail
// 1). What is here is deliberately modular so R4-R9, and any later bench work, can
// reuse the pieces rather than reinvent them - but the reuse direction is outward
// from lib/ and this directory, never from this main into node firmware.
//
// R1-R3 only. The bench probe below is R2's acceptance criterion - a single
// hardcoded packet crossing with plausible RSSI at 1 m - and NOT R4's sweep. The
// sweep, position marking, CSV, ambient survey and the W9 PING runs are branches 2
// to 4 and are not started here.

#include <Arduino.h>

#include <cstdio>

#include "board_config.h"
#include "phy_params.h"
#include "radio_link.h"
#include "role.h"
#include "ui_oled.h"

namespace {

using namespace rangetest;

// D33 standing condition 1, and task R10: "use the antenna supplied with the
// module", recorded with its gain. 2.0 dBi is spec 18.1's worked figure and the
// stock whip's nameplate value.
//
// It is a CONSTANT AND NOT A GUESS BURIED IN THE CLAMP: change the antenna and this
// line changes with it, and the settings dump reports the new conducted ceiling.
// M21 (confirm the modules' own FCC grant conditions - antenna type and gain) is
// what turns this from a nameplate figure into an audited one, and it is open.
constexpr int16_t kAntennaGainDbi10 = 20;  // 2.0 dBi

// R2 acceptance - the bench probe. Not a protocol frame: R4 keeps the sweep on raw
// RadioLib frames and R9 introduces real PING frames against the codec. A hardcoded
// ASCII payload is the least that proves the PA reaches the antenna.
constexpr char     kProbeText[] = "LRAN-RANGETEST-R2";
constexpr uint32_t kProbePeriodMs = 2000;

RadioLink g_radio;
Ui        g_ui;
Role      g_role = Role::Initiator;
TestPoint g_tp;

uint32_t g_tx_count = 0;
uint32_t g_rx_count = 0;
uint32_t g_crc_errors = 0;
uint32_t g_next_tx_ms = 0;

// R1 - the selection window. See role.h for why this is a post-boot window rather
// than a hold through reset, and why serial is a second selector alongside PRG.
Role select_role() {
  pinMode(kPinPrgButton, INPUT_PULLUP);

  Serial.println(F("role select: press PRG for RESPONDER, or send 'i'/'r' "
                   "(3s, default INITIATOR)"));

  const uint32_t start = millis();
  uint32_t       last_draw = 0;

  while (millis() - start < kRoleSelectWindowMs) {
    if (digitalRead(kPinPrgButton) == LOW) {
      // Debounce by confirming the press is still there.
      delay(30);
      if (digitalRead(kPinPrgButton) == LOW) {
        while (digitalRead(kPinPrgButton) == LOW) delay(10);  // wait for release
        return Role::Responder;
      }
    }

    // The tethered-bench selector. Both boards on one machine cannot both be
    // reached by a thumb.
    while (Serial.available() > 0) {
      const int c = Serial.read();
      if (c == kSerialSelectResponder || c == 'R') return Role::Responder;
      if (c == kSerialSelectInitiator || c == 'I') return Role::Initiator;
    }

    const uint32_t elapsed = millis() - start;
    if (elapsed - last_draw >= 200) {
      last_draw = elapsed;
      g_ui.show_role_prompt(kRoleSelectWindowMs - elapsed);
    }
    delay(10);
  }
  return Role::Initiator;
}

// R3 acceptance - the settings dump on the serial console at boot, so a CSV can be
// correlated with the configuration that produced it.
void dump_settings() {
  char buf[512];
  const size_t n = format_settings(g_tp, kHeltecV3.name, buf, sizeof(buf));
  Serial.println(F("--- settings (R3) ---"));
  if (n == 0) {
    // format_settings refuses to truncate rather than emit a partial dump that would
    // correlate a CSV with a configuration that was not the one used.
    Serial.println(F("ERROR: settings dump did not fit its buffer"));
  } else {
    Serial.print(buf);
  }
  Serial.print(F("role="));
  Serial.println(to_string(g_role));
  Serial.println(F("--- end settings ---"));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t serial_wait = millis();
  while (!Serial && millis() - serial_wait < 2000) delay(10);

  Serial.println();
  Serial.println(F("LRAN range test firmware - pass 1, branch 1 (R1-R3)"));
  Serial.println(F("Binding spec: LRAN-Protocol-Specification v0.7 (ver = 2)"));
  Serial.println(F("This firmware never ships. No WiFi, no MQTT, no secrets."));

  if (!g_ui.begin()) {
    // A dark panel on this board is usually Vext, not the driver. Say so once,
    // then carry on: the serial console is the initiator's real instrument and a
    // dead OLED must not stop a bench bring-up.
    Serial.println(F("WARN: OLED did not ACK at 0x3C (check Vext) - continuing"));
  }

  g_role = select_role();
  g_ui.show_role(g_role, kHeltecV3.name);
  Serial.print(F("role="));
  Serial.println(to_string(g_role));

  // The test point. Provisional, and labelled as such in phy_params.h: D1 is open
  // and its frequency waits on M20's ambient survey (task R8), which has not run.
  g_tp.freq_hz     = kProvisionalFreqHz;
  g_tp.sf          = kProvisionalSf;
  g_tp.cr_denom    = kProvisionalCrDenom;
  g_tp.payload_len = sizeof(kProbeText) - 1;

  // Task guardrail 3 - the sweep STARTS AT THE BOTTOM of the SX1262's range and
  // climbs only on failure. A working point chosen at an unusable power is a result
  // thrown away, so the bottom is where bring-up starts too.
  const ClampResult clamp =
      clamp_conducted(kSx1262MinDbm, kAntennaGainDbi10, &g_tp.power);

  if (clamp == ClampResult::BelowRadioFloor) {
    // The D33 ceiling sits below anything this radio can emit on this antenna. Not
    // recoverable by turning the power down, and not something to transmit through.
    Serial.println(F("FATAL: D33 EIRP ceiling is below the SX1262 minimum for this "
                     "antenna gain. Refusing to transmit."));
    g_ui.show_message("D33 CEILING", "below radio min");
    while (true) delay(1000);
  }
  if (clamp == ClampResult::Clamped) {
    Serial.println(F("note: requested power clamped to the D33 ceiling"));
  }

  dump_settings();

  const int16_t st = g_radio.begin(kHeltecV3, g_tp);
  if (st != 0) {
    // Task R2 spends half its text on the two settings that fail SILENTLY on this
    // board. This is the other case - a failure the driver does report - and it is
    // printed with its number rather than reduced to "radio failed".
    Serial.print(F("FATAL: SX1262 begin() failed, RadioLib code "));
    Serial.println(st);
    g_ui.show_message("RADIO FAIL", "see serial");
    while (true) delay(1000);
  }
  Serial.println(F("SX1262 up."));

  if (g_role == Role::Responder) {
    const int16_t rx = g_radio.start_receive();
    if (rx != 0) {
      Serial.print(F("FATAL: startReceive() failed, code "));
      Serial.println(rx);
      while (true) delay(1000);
    }
  }
  g_next_tx_ms = millis();
}

void loop() {
  // R2 acceptance: one hardcoded packet across the bench with plausible RSSI at 1 m.
  //
  // The initiator transmits and then listens for the responder's echo; the responder
  // listens and echoes. That is the smallest thing that proves BOTH directions,
  // which is what R4's round-trip PER will be built on.
  if (g_role == Role::Initiator && static_cast<int32_t>(millis() - g_next_tx_ms) >= 0) {
    g_next_tx_ms = millis() + kProbePeriodMs;

    const int16_t st = g_radio.transmit(
        reinterpret_cast<const uint8_t*>(kProbeText), sizeof(kProbeText) - 1);
    if (st != 0) {
      Serial.print(F("tx error "));
      Serial.println(st);
    } else {
      ++g_tx_count;
    }
    g_radio.start_receive();
  }

  uint8_t buf[64];
  size_t  len = 0;
  bool    crc_error = false;

  if (g_radio.poll(buf, sizeof(buf), &len, &crc_error)) {
    if (crc_error) {
      // spec 14 stage 1 - the one discard path that cannot be produced at a desk.
      // Counted and printed, never silently dropped; the engineering log asks for it
      // to be observed at the far edge of the walk.
      ++g_crc_errors;
      Serial.print(F("phy_crc_error rssi="));
      Serial.print(g_radio.last_rssi_dbm());
      Serial.print(F(" snr="));
      Serial.println(g_radio.last_snr_db());
    } else {
      ++g_rx_count;
      Serial.print(F("rx len="));
      Serial.print(static_cast<unsigned>(len));
      Serial.print(F(" rssi="));
      Serial.print(g_radio.last_rssi_dbm());
      Serial.print(F(" snr="));
      Serial.println(g_radio.last_snr_db());

      if (g_role == Role::Responder) {
        // Echo it straight back. Not a PING responder - that is R9, against the real
        // codec. This only closes the loop so the initiator sees a round trip.
        g_radio.transmit(buf, len);
      }
    }

    g_ui.show_link(g_role, g_radio.last_rssi_dbm(), g_radio.last_snr_db(),
                   /* position_id */ 0, g_rx_count,
                   (g_role == Role::Initiator) ? g_tx_count : g_rx_count);
    g_radio.start_receive();
  }

  delay(5);
}
