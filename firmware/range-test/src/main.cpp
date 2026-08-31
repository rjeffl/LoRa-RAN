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

#include "airtime.h"
#include "bench_frame.h"
#include "board_config.h"
#include "phy_params.h"
#include "radio_link.h"
#include "role.h"
#include "sweep.h"
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

RadioLink g_radio;
Ui        g_ui;
Role      g_role = Role::Initiator;

// R4 - the sweep. The plan is data; the state below is where in it we are.
SweepPlan      g_plan;
TestPoint      g_tp;
TestPointStats g_stats;

size_t   g_tp_index    = 0;   // which test point
size_t   g_prev_config = 0;   // radio config of the point just finished
bool     g_have_prev_config = false;
uint16_t g_warmup_left = 0;   // uncounted probes still owed to responder reacquisition
uint16_t g_probe_index = 0;   // which probe within it
uint16_t g_probe_seq   = 0;   // monotonic, identifies an echo with its probe
uint16_t g_position_id = 0;   // R5 will drive this from the responder's PRG button

// Set when a probe is in flight and we are waiting for its echo.
bool     g_awaiting_echo  = false;
uint32_t g_echo_deadline  = 0;

// Responder side. Counts only; its own summary is R6.
uint32_t g_resp_echoes = 0;

// RESPONDER FOLLOWS THE INITIATOR. The initiator retunes for every test point; a
// responder that stays put can only hear the first radio configuration and reports
// every other one as a dead link. Found on hardware - see the engineering log.
//
// It cannot be told to retune out of band, because the only channel is the one whose
// configuration is changing. So it hunts: dwell on a configuration, and if nothing
// arrives within the dwell, step to the next one CYCLICALLY.
//
// Stepping cyclically from the last configuration heard is both the fast path and the
// recovery path, which is why there is no separate "scan mode". In plan order the
// next configuration after the current one is almost always the right guess, so a
// normal sweep advance costs one dwell; a genuinely unreachable configuration just
// keeps the cycle turning until something is heard again.
size_t   g_resp_config  = 0;
uint32_t g_resp_dwell_until = 0;

uint8_t g_buf[kMaxBenchPayload];

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

// RESPONDER: tune to radio configuration `index` and restart the dwell clock.
void resp_tune_to(size_t index) {
  RadioConfigKey cfg{};
  if (!sweep_config_at(g_plan, index, &cfg)) return;

  // Only freq/SF/CR matter for HEARING the initiator. The power set here is a
  // placeholder the echo path immediately overrides with the probe's own test-point
  // power, so it is the sweep floor rather than the ceiling - if the override were
  // ever to fail, erring low keeps the D33 argument intact.
  TestPoint tp{};
  tp.freq_hz     = cfg.freq_hz;
  tp.sf          = cfg.sf;
  tp.cr_denom    = cfg.cr_denom;
  tp.payload_len = 0;
  if (clamp_conducted(kSx1262MinDbm, kAntennaGainDbi10, &tp.power) ==
      ClampResult::BelowRadioFloor) {
    return;
  }

  g_resp_config = index;
  g_radio.apply(tp);
  g_radio.start_receive();
  g_resp_dwell_until = millis() + sweep_config_dwell_ms(g_plan, cfg);

  Serial.print(F("# resp tuned to config "));
  Serial.print(static_cast<unsigned>(index));
  Serial.print(F(" sf=")); Serial.print(cfg.sf);
  Serial.print(F(" cr=4/")); Serial.println(cfg.cr_denom);
}

// R4 - what the operator needs before standing still for a position.
void dump_sweep_plan() {
  const size_t   n       = sweep_point_count(g_plan);
  const uint32_t worst   = sweep_duration_worst_ms(g_plan, kAntennaGainDbi10);
  const uint32_t nominal = sweep_duration_nominal_ms(g_plan, kAntennaGainDbi10);

  Serial.println(F("--- sweep plan (R4) ---"));
  Serial.print(F("test_points=")); Serial.println(static_cast<unsigned>(n));
  Serial.print(F("probes_per_point=")); Serial.println(g_plan.probes_per_point);
  Serial.print(F("total_probes="));
  Serial.println(static_cast<unsigned>(n * g_plan.probes_per_point));

  // Both figures, because the spread is what matters: a dead position runs every
  // probe out to its deadline, and that is exactly where the data is wanted.
  Serial.print(F("duration_nominal_s=")); Serial.println((nominal + 500) / 1000);
  Serial.print(F("duration_worst_s="));   Serial.println((worst + 500) / 1000);
  Serial.println(F("--- end sweep plan ---"));
}

// Loads test point `g_tp_index` into the radio and clears the statistics for it.
bool begin_test_point() {
  if (!sweep_point_at(g_plan, g_tp_index, kAntennaGainDbi10, &g_tp)) return false;

  const int16_t st = g_radio.apply(g_tp);
  if (st != 0) {
    Serial.print(F("apply() failed at tp="));
    Serial.print(static_cast<unsigned>(g_tp_index));
    Serial.print(F(" code="));
    Serial.println(st);
    return false;
  }

  // If the radio configuration changed, the responder needs a dwell to notice. Those
  // probes are transmitted but not counted - otherwise its reacquisition time is
  // charged to the link as packet loss on the first point of every configuration.
  size_t cfg = 0;
  bool   changed = false;
  if (sweep_config_index_of(g_plan, g_tp_index, &cfg)) {
    changed = g_have_prev_config && (cfg != g_prev_config);
    if (!g_have_prev_config) {
      // First point after boot: the responder starts hunting from configuration 0
      // and has to find us, so treat it as a change from wherever it began.
      changed       = true;
      g_prev_config = 0;
    }
  }
  g_warmup_left = sweep_warmup_probes(g_plan, changed, g_prev_config, g_tp);
  if (g_warmup_left > 0) {
    Serial.print(F("# warmup "));
    Serial.print(g_warmup_left);
    Serial.print(F(" probes into tp "));
    Serial.println(static_cast<unsigned>(g_tp_index));
  }
  g_prev_config       = cfg;
  g_have_prev_config  = true;

  g_stats.reset();
  g_probe_index = 0;
  return true;
}

// One CSV-ish line per completed test point. R6 owns the real CSV; this is the
// minimum that makes R4 observable, and deliberately carries conducted power and
// antenna gain as separate columns (D33 standing condition 1).
void report_test_point() {
  Serial.print(F("TP,"));
  Serial.print(g_position_id);                       Serial.print(',');
  Serial.print(static_cast<unsigned>(g_tp_index));   Serial.print(',');
  Serial.print(g_tp.freq_hz);                        Serial.print(',');
  Serial.print(g_tp.sf);                             Serial.print(',');
  Serial.print(g_tp.cr_denom);                       Serial.print(',');
  Serial.print(g_tp.power.conducted_dbm);            Serial.print(',');
  Serial.print(g_tp.power.antenna_gain_dbi10);       Serial.print(',');
  Serial.print(g_tp.payload_len);                    Serial.print(',');
  Serial.print(g_stats.probes_sent);                 Serial.print(',');
  Serial.print(g_stats.echoes_received);             Serial.print(',');
  Serial.print(g_stats.per_pct100());                Serial.print(',');
  Serial.print(g_stats.init_rssi_dbm10.mean());      Serial.print(',');
  Serial.print(g_stats.init_rssi_dbm10.min);         Serial.print(',');
  Serial.print(g_stats.init_rssi_dbm10.max);         Serial.print(',');
  Serial.print(g_stats.init_snr_db10.mean());        Serial.print(',');
  Serial.print(g_stats.resp_rssi_dbm10.mean());      Serial.print(',');
  Serial.print(g_stats.resp_snr_db10.mean());        Serial.print(',');
  Serial.print(g_stats.phy_crc_errors);              Serial.print(',');
  Serial.print(g_stats.foreign_frames);              Serial.print(',');
  Serial.println(g_stats.filler_mismatch);
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
  g_ui.show_role(g_role, kHeltecV3.short_name);
  Serial.print(F("role="));
  Serial.println(to_string(g_role));

  g_plan = default_plan();

  // Test point 0. Every point's power goes through the D33 clamp inside
  // sweep_point_at(), and point 0 is the bottom of the SX1262's range - the sweep
  // starts there and climbs only on failure (task guardrail 3).
  if (!sweep_point_at(g_plan, 0, kAntennaGainDbi10, &g_tp)) {
    // Either the plan is empty or the D33 ceiling sits below anything this radio can
    // emit on this antenna. The second is not recoverable by turning the power down
    // and is not something to transmit through.
    Serial.println(F("FATAL: no usable test point. Either the sweep plan is empty, "
                     "or the D33 EIRP ceiling is below the SX1262 minimum for this "
                     "antenna gain. Refusing to transmit."));
    g_ui.show_message("NO TEST POINT", "see serial");
    while (true) delay(1000);
  }

  dump_settings();
  dump_sweep_plan();

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

  // Both roles listen. The initiator is waiting for echoes, the responder for probes.
  const int16_t rx = g_radio.start_receive();
  if (rx != 0) {
    Serial.print(F("FATAL: startReceive() failed, code "));
    Serial.println(rx);
    while (true) delay(1000);
  }

  if (g_role == Role::Responder) {
    // Start the hunt at configuration 0 and let the dwell clock carry it forward.
    resp_tune_to(0);
  }

  if (g_role == Role::Initiator) {
    if (!begin_test_point()) {
      Serial.println(F("FATAL: could not load test point 0"));
      while (true) delay(1000);
    }
    // The CSV header. R7 moves this into a committed file under
    // docs/rangetest/data/; here it is what makes the serial log self-describing.
    Serial.println(F("TP,position,tp_index,freq_hz,sf,cr_denom,conducted_dbm,"
                     "antenna_gain_dbi10,payload_len,probes,echoes,per_pct100,"
                     "init_rssi_mean10,init_rssi_min10,init_rssi_max10,"
                     "init_snr_mean10,resp_rssi_mean10,resp_snr_mean10,"
                     "phy_crc_err,foreign,filler_err"));
  }
}

namespace {

// dBm/dB as reported by RadioLib, converted to the tenths this firmware carries
// everywhere. Rounds away from zero so a negative RSSI is never nudged optimistic.
int16_t to_tenths(float v) {
  const float scaled = v * 10.0f;
  return static_cast<int16_t>(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
}

// INITIATOR: send probe `g_probe_index` of the current test point.
void send_probe() {
  BenchFrame f{};
  f.kind        = BenchKind::Probe;
  f.position_id = g_position_id;
  f.tp_index    = static_cast<uint16_t>(g_tp_index);
  f.probe_seq   = g_probe_seq;
  // resp_* stay kI16NotAvailable: a probe has no responder measurement in it yet.

  const size_t n = bench_serialize(f, g_buf, sizeof(g_buf), g_tp.payload_len);
  if (n == 0) {
    Serial.println(F("probe serialize failed - payload_len out of range"));
    return;
  }

  const int16_t st = g_radio.transmit(g_buf, n);
  if (st != 0) {
    Serial.print(F("tx error "));
    Serial.println(st);
    // Still counted as sent. A probe the radio refused is a probe the far end never
    // had a chance to echo, and hiding it would flatter the PER.
  }
  if (g_warmup_left == 0) ++g_stats.probes_sent;

  g_awaiting_echo = true;
  g_echo_deadline = millis() + echo_timeout_ms(g_plan, g_tp);
  g_radio.start_receive();
}

// INITIATOR: fold a returned echo into the statistics.
void record_echo(const BenchFrame& f) {
  // A warmup probe's echo proves the responder has found us, which is all it is for.
  // Folding it into the statistics would defeat the point of not counting the probe.
  if (g_warmup_left > 0) return;

  g_stats.init_rssi_dbm10.add(to_tenths(g_radio.last_rssi_dbm()));
  g_stats.init_snr_db10.add(to_tenths(g_radio.last_snr_db()));

  // The reverse-direction data R4 otherwise gives up. Only folded in when the
  // responder actually supplied it - the sentinel is not a measurement.
  if (f.resp_rssi_dbm10 != kI16NotAvailable) {
    g_stats.resp_rssi_dbm10.add(f.resp_rssi_dbm10);
  }
  if (f.resp_snr_db10 != kI16NotAvailable) {
    g_stats.resp_snr_db10.add(f.resp_snr_db10);
  }
  ++g_stats.echoes_received;
}

// INITIATOR: this probe is done, one way or the other. Advance.
void finish_probe() {
  g_awaiting_echo = false;
  ++g_probe_seq;

  if (g_warmup_left > 0) {
    --g_warmup_left;
    return;   // does not advance the counted probe index
  }
  ++g_probe_index;

  if (g_probe_index < g_plan.probes_per_point) return;

  report_test_point();

  g_tp_index = (g_tp_index + 1) % sweep_point_count(g_plan);
  if (g_tp_index == 0) Serial.println(F("# sweep complete, restarting"));

  if (!begin_test_point()) {
    Serial.println(F("FATAL: could not load next test point"));
    while (true) delay(1000);
  }
}

// RESPONDER: echo a probe back with our own measurement of it attached.
void echo_probe(const BenchFrame& probe_in, size_t rx_len) {
  BenchFrame f{};
  f.kind        = BenchKind::Echo;
  f.position_id = probe_in.position_id;
  f.tp_index    = probe_in.tp_index;
  f.probe_seq   = probe_in.probe_seq;

  // The measurement the initiator cannot make. This is the whole reason the sweep
  // uses a bench frame rather than PING, whose responder echoes the payload
  // unchanged (spec 6.6).
  f.resp_rssi_dbm10 = to_tenths(g_radio.last_rssi_dbm());
  f.resp_snr_db10   = to_tenths(g_radio.last_snr_db());

  // Echo at the PROBE'S power, not at the responder's own ceiling.
  //
  // The probe names its test point, so the power it was sent at is knowable. Without
  // this the return leg runs at the D33 ceiling while the outbound leg runs wherever
  // the sweep put it - a 6 dB asymmetry at the low-power points, measured on the
  // bench - and round-trip PER stops being a measurement of the link. Still routed
  // through the clamp inside sweep_point_at(); nothing here bypasses D33.
  TestPoint probe_tp{};
  if (sweep_point_at(g_plan, probe_in.tp_index, kAntennaGainDbi10, &probe_tp)) {
    g_radio.set_power(probe_tp.power.conducted_dbm);
  }

  // Echo at the same length so both legs are the same shape and the round-trip
  // timeout computed from one airtime is right for both.
  const size_t n = bench_serialize(f, g_buf, sizeof(g_buf), rx_len);
  if (n == 0) return;

  g_radio.transmit(g_buf, n);
  ++g_resp_echoes;
  g_radio.start_receive();
}

}  // namespace

void loop() {
  uint8_t rx[kMaxBenchPayload];
  size_t  len       = 0;
  bool    crc_error = false;

  if (g_radio.poll(rx, sizeof(rx), &len, &crc_error)) {
    if (crc_error) {
      // spec 14 stage 1 - the one discard path that cannot be produced at a desk.
      // Counted against the current test point, never silently dropped.
      ++g_stats.phy_crc_errors;
    } else {
      BenchFrame f{};
      if (!bench_parse(rx, len, &f)) {
        // Not ours. The site has known 915 MHz occupants (D1 notes), and counting a
        // foreign frame as an echo would flatter the link.
        ++g_stats.foreign_frames;
      } else if (g_role == Role::Responder && f.kind == BenchKind::Probe) {
        g_position_id = f.position_id;   // R5 will drive this locally instead

        // The probe names its test point, so the responder knows exactly where the
        // sweep is. Echo first, then retune if the sweep has moved on - retuning
        // before the echo would send it on a configuration the initiator has already
        // left, and the probe would be scored lost for no reason.
        echo_probe(f, len);
        g_ui.show_link(g_role, g_radio.last_rssi_dbm(), g_radio.last_snr_db(),
                       f.position_id, g_resp_echoes, g_resp_echoes);

        size_t want = g_resp_config;
        if (sweep_config_index_of(g_plan, f.tp_index, &want) &&
            want != g_resp_config) {
          resp_tune_to(want);
        } else {
          // Same configuration, still in contact: hold here and restart the clock.
          RadioConfigKey cfg{};
          if (sweep_config_at(g_plan, g_resp_config, &cfg)) {
            g_resp_dwell_until = millis() + sweep_config_dwell_ms(g_plan, cfg);
          }
        }

      } else if (g_role == Role::Initiator && f.kind == BenchKind::Echo) {
        // Match the echo to the probe in flight. A stale echo from an earlier probe
        // must not be credited to this one, or PER reads better than the link is.
        if (g_awaiting_echo && f.probe_seq == g_probe_seq) {
          size_t bad = 0;
          if (!bench_check_filler(rx, len, f.probe_seq, &bad)) {
            // Passed the PHY CRC but the bytes are wrong - a buffer or indexing
            // fault, not an RF one. Counted apart from a lost echo because the two
            // have different causes and different fixes.
            ++g_stats.filler_mismatch;
          }
          record_echo(f);
          g_ui.show_link(g_role, g_radio.last_rssi_dbm(), g_radio.last_snr_db(),
                         g_position_id, g_stats.echoes_received,
                         g_stats.probes_sent);
          finish_probe();
        }
      }
    }
    g_radio.start_receive();
  }

  if (g_role != Role::Initiator) {
    // Dwell expired with nothing heard: step to the next configuration. In plan order
    // that is almost always where the initiator just went, so a normal sweep advance
    // costs one dwell rather than a search.
    if (static_cast<int32_t>(millis() - g_resp_dwell_until) >= 0) {
      const size_t n = sweep_config_count(g_plan);
      if (n > 0) resp_tune_to((g_resp_config + 1) % n);
    }
    delay(2);
    return;
  }

  if (g_awaiting_echo) {
    if (static_cast<int32_t>(millis() - g_echo_deadline) >= 0) {
      finish_probe();   // scored lost: probes_sent already counted it
    }
  } else {
    delay(g_plan.inter_probe_gap_ms);
    send_probe();
  }

  delay(2);
}
