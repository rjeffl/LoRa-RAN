// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
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
#include <Preferences.h>

#include <cstdio>
#include <cstring>

#include "airtime.h"
#include "bench_frame.h"
#include "board_config.h"
#include "csv.h"
#include "pa_config.h"
#include "phy_params.h"
#include "radio_link.h"
#include "resp_log.h"
#include "role.h"
#include "survey.h"
#include "sweep.h"
#include "ui_oled.h"
#include "w9.h"

#include "lran/reassembly.h"

namespace {

using namespace rangetest;

// D33 standing condition 1: conducted power and antenna gain are recorded separately,
// and the gain is an INPUT TO THE CLAMP, not just a CSV column. The ceiling is EIRP.
//
// SET IN platformio.ini, not here. It moved out of this file when the stock 2.0 dBi
// whips were replaced by the 3.0 dBi production antennas: an antenna swap has to be a
// visible one-line decision in the build, because forgetting it is wrong twice over -
// the trace records an antenna that was not fitted, AND the clamp permits an EIRP over
// the ceiling by exactly the error.
//
//   2.0 dBi (stock whip)  -> conducted ceiling -3 dBm
//   3.0 dBi (production)  -> conducted ceiling -4 dBm
//
// No default here on purpose. A default is what silently survives an antenna change.
#ifndef LRAN_ANTENNA_GAIN_DBI10
#error "LRAN_ANTENNA_GAIN_DBI10 must be set in platformio.ini - see the note there."
#endif
constexpr int16_t kAntennaGainDbi10 = LRAN_ANTENNA_GAIN_DBI10;

RadioLink g_radio;
Ui        g_ui(kBoardUi);
Role      g_role = Role::Initiator;

// R4 - the sweep. The plan is data; the state below is where in it we are.
SweepPlan      g_plan;
TestPoint      g_tp;
TestPointStats g_stats;

// R5 - one sweep per position. The initiator sweeps, then ARMS and waits; the
// operator walks, presses PRG on the responder, and the next sweep starts.
//
// The button is on the WALKING end, so its press has to reach the initiator in band -
// there is no other channel. The responder owns `position_id` and stamps it into
// every echo; the initiator learns it from there. Which means the initiator has to
// keep talking while it is idle, or it would never hear that the operator has moved.
enum class InitState : uint8_t { Sweeping, Armed };

// ARMED AT BOOT, NOT SWEEPING.
//
// It used to start sweeping the moment it came up, which loses the beginning of the
// first sweep every time: the field flow is start the capture (which resets the
// initiator), then walk over and boot the responder, so the initiator is always
// several test points ahead of a responder that does not exist yet. The first real
// walk (2026-09-04) shows it exactly - position 0 lost its first 23 probes, 100% PER
// on test points 0 and 1, while position 1 was a clean 192 of 192.
//
// Arming instead means the first sweep starts on the operator's first PRG press, which
// is what R5's "one press, one sweep, one position" already describes. Positions then
// run 1..N and every one of them is clean.
//
// THAT HOLDS FOR ONE RESPONDER PER CAPTURE, AND ONLY THAT. Arming protects the
// INITIATOR's boot; it does nothing about a responder swapped in mid-capture. A fresh
// responder boots g_position_id at 0 (below) while the initiator is still holding the
// last position it swept, and the mismatch below IS the start signal - so the new board
// starts a sweep with no press, at position 0, wherever the operator happens to be
// carrying it. Observed 2026-09-09 on the B1b A/B: sweeps ran 1, 2, 0, 1 in one file,
// with two different locations sharing position 1.
//
// Reset the initiator when you change responders. Until that is enforced here, "a trace
// with a position 0 predates this change" is not a safe reading of a trace.
InitState g_init_state = InitState::Armed;

// Position the CURRENT sweep is being run at, versus the latest the responder has
// reported. A difference between them is the signal to start the next sweep.
uint16_t g_swept_position   = 0;
uint16_t g_heard_position   = 0;
uint16_t g_sweeps_completed = 0;

// While armed the initiator beacons on configuration 0 so the responder's hunt can
// find it and its echoes can carry the position forward. Not counted in anything.
constexpr uint32_t kArmedBeaconMs = 1000;
uint32_t g_next_beacon_ms = 0;

size_t   g_tp_index    = 0;   // which test point
size_t   g_prev_config = 0;   // radio config of the point just finished
bool     g_have_prev_config = false;
uint16_t g_warmup_left = 0;   // uncounted probes still owed to responder reacquisition
uint16_t g_probe_index = 0;   // which probe within it
uint16_t g_probe_seq   = 0;   // monotonic, identifies an echo with its probe
// RESPONDER-OWNED. The walking end increments this on a PRG press and stamps it into
// every echo; the initiator never writes it and learns the value from the echo into
// g_heard_position instead. One writer, so the two ends cannot disagree about where
// the operator is standing.
uint16_t g_position_id = 0;

// Set when a probe is in flight and we are waiting for its echo.
bool     g_awaiting_echo  = false;
uint32_t g_echo_deadline  = 0;

// Responder side.
uint32_t g_resp_echoes = 0;

// R5/R6 - what the WALKING operator needs to know and can only learn over the air:
// the initiator has finished the sweep for this position and is waiting. Set by an
// ArmedBeacon, cleared by any counted probe, so it tracks the far end rather than
// latching on the first one seen.
bool g_resp_far_end_armed = false;

// R6 - what the responder itself heard, per position. Recovers the direction that
// round-trip PER conflates: a probe heard whose echo was lost is invisible to the
// initiator, and this is the only record it ever existed.
PositionLog g_resp_log;
Preferences g_prefs;

// Per-test-point tally, reset when the sweep moves on. Rides back in every echo so
// the initiator's CSV resolves downlink from uplink on its own, without this log
// having to be recovered and merged afterwards.
uint16_t g_resp_tp_index = 0xFFFF;
uint16_t g_resp_tp_heard = 0;

// R6 - last-heard age. Out of range and crashed look identical on a display showing
// only the last reading; on a walk that is the difference between carrying on and
// turning back.
uint32_t g_resp_last_heard_ms = 0;
bool     g_resp_ever_heard    = false;
float    g_resp_last_rssi_dbm = 0.0f;

// Long enough not to flicker between probes at SF12 - where one probe period is over
// 8 s - and short enough to notice on a walk.
constexpr uint32_t kStaleAfterMs = 12000;

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

// R8 / M20 - the ambient survey. Its own state, sharing nothing with the sweep: there
// is no far end here, no position and no test point, and folding it into the sweep's
// state machine would put a transmit path one mistaken branch away from a mode whose
// entire correctness is that it never transmits.
Survey     g_survey;
SurveyPlan g_survey_plan;
size_t     g_survey_bin           = 0;
uint32_t   g_survey_bin_ends_ms   = 0;   // when this bin's dwell expires
uint32_t   g_survey_listen_at_ms  = 0;   // when the settle time is up
uint32_t   g_survey_next_sample   = 0;
bool       g_survey_saved         = false;
// R11 - the cursor AND the hold phase. Advanced by PRG, exactly as the responder's
// position is (R5) - same button, same muscle memory - but two presses per site now:
// one on arrival to start the dwell, one when it is done to store and move on. The
// walk between sites happens in Held and is not measured. See survey.h.
SurveyCampaign g_campaign;
uint32_t   g_survey_next_draw_ms  = 0;

// Progress to the console every so often. Not decoration: a survey run is minutes of
// deliberate silence, and capture.py's idle timeout, the operator and the question
// "is it still alive" are all answered by the same line.
constexpr uint32_t kSurveyProgressPasses = 10;
constexpr uint32_t kSurveyDrawIntervalMs = 500;

// R1 - the selection window. See role.h for why this is a post-boot window rather
// than a hold through reset, and why serial is a second selector alongside PRG.
Role select_role() {
  pinMode(kBoardUi.role_button, INPUT_PULLUP);

  Serial.println(F("role select: tap PRG = RESPONDER, HOLD PRG = SURVEY, or send "
                   "'i'/'r'/'v' (3s, default INITIATOR)"));

  const uint32_t start = millis();
  uint32_t       last_draw = 0;

  while (millis() - start < kRoleSelectWindowMs) {
    if (digitalRead(kBoardUi.role_button) == LOW) {
      // Debounce by confirming the press is still there.
      delay(30);
      if (digitalRead(kBoardUi.role_button) == LOW) {
        // TAP = RESPONDER, HOLD = SURVEY. The board on a power bank has no other way
        // to reach the survey; see role.h for why serial-only was a field-blocking
        // bug rather than a limitation.
        //
        // The display is updated WHILE THE BUTTON IS STILL DOWN, so the operator sees
        // the role cross over to SURVEY and releases on the one they wanted. A hold
        // whose effect is invisible until the radio does or does not start is exactly
        // what R1's selection window was written to avoid.
        const uint32_t pressed_at = millis();
        bool           survey     = false;
        while (digitalRead(kBoardUi.role_button) == LOW) {
          const uint32_t held = millis() - pressed_at;
          if (!survey && held >= kPrgSurveyHoldMs) survey = true;
          g_ui.show_role_hold(held, survey);
          delay(10);
        }
        return survey ? Role::Survey : Role::Responder;
      }
    }

    // The tethered-bench selector. Both boards on one machine cannot both be
    // reached by a thumb.
    while (Serial.available() > 0) {
      const int c = Serial.read();
      if (c == kSerialSelectResponder || c == 'R') return Role::Responder;
      if (c == kSerialSelectInitiator || c == 'I') return Role::Initiator;
      if (c == kSerialSelectSurvey || c == 'V') return Role::Survey;
      if (c == kSerialSelectW9Initiator || c == 'W') return Role::W9Initiator;
      if (c == kSerialSelectW9Responder || c == 'X') return Role::W9Responder;
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
  const size_t n = format_settings(g_tp, kBoard.name, buf, sizeof(buf));
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

// R5 - a debounced falling edge on PRG. Non-blocking: the responder has to keep
// echoing while the operator is pressing it, and a blocking debounce would drop
// probes at exactly the moment a new position starts.
//
// The same GPIO the role selector used at boot (role.h), confirmed on hardware. It is
// re-read here as an ordinary input; nothing about the strapping-pin behaviour
// matters once the application is running.
bool prg_edge() {
  // A press is only a press once the line has been LOW CONTINUOUSLY for
  // kPressStableMs. The previous version rate-limited *changes* - it accepted the
  // first LOW sample it saw and then refused another for 40 ms - which is not a
  // debounce at all: any glitch narrower than the sampling interval still reported a
  // press.
  //
  // That mattered because GPIO 0 is also IO0, driven by the USB bridge's DTR. Opening
  // the port asserted it (fixed host-side in capture.py) and CLOSING it pulses it, and
  // a pulse was indistinguishable from a thumb. In survey mode a press is
  // store-and-advance, so a tethered session ended by storing a bogus run and stepping
  // the campaign cursor - which presents as an erase that "does not stick", because
  // the erase works and the phantom press immediately re-stores site 0.
  //
  // A human press is over 100 ms; a line glitch is far shorter. Requiring the level to
  // hold rejects the glitch without making the button feel slow.
  constexpr uint32_t kPressStableMs = 50;

  static bool     reported   = false;   // this press has already been announced
  static bool     low_seen   = false;   // the line is currently low
  static uint32_t low_since  = 0;

  const bool low = (digitalRead(kBoardUi.role_button) == LOW);

  if (!low) {
    // Released - or the glitch ended before it ever qualified. Either way, re-arm.
    low_seen = false;
    reported = false;
    return false;
  }

  if (!low_seen) {
    low_seen  = true;
    low_since = millis();
    return false;                 // start the clock, report nothing yet
  }

  if (!reported && (millis() - low_since) >= kPressStableMs) {
    reported = true;              // once per press, not once per poll
    return true;
  }
  return false;
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

// R6/R7 - one CSV row per test point per position, via the shared formatter so the
// header and the columns cannot drift apart.
void report_test_point() {
  CsvRow row{};
  row.position_id       = g_swept_position;
  row.tp_index          = static_cast<uint16_t>(g_tp_index);
  row.tp                = g_tp;
  row.stats             = g_stats;
  row.resp_probes_heard = g_stats.resp_heard;

  char line[kCsvMaxLine];
  if (csv_row(row, line, sizeof(line)) == 0) {
    // Never a truncated row - one that still parses and is wrong is worse than a
    // missing one - so say which point was lost instead.
    Serial.print(F("# ERROR: CSV row did not fit for tp "));
    Serial.println(static_cast<unsigned>(g_tp_index));
    return;
  }
  Serial.println(line);
}

// R6 - persistence. The responder is untethered and battery powered; a brown-out on
// the walk back would otherwise take the whole log with it.
//
// NVS holds one opaque blob. All the structure lives in PositionLog::serialize, which
// is host tested - the part that can be silently wrong is the byte layout, not the
// key-value store.
constexpr char kNvsNamespace[] = "lran-rt";
constexpr char kNvsLogKey[]    = "poslog";
// R8 - one key per site, so a run stored at the gate is not overwritten by the run
// stored at the well. Short keys: NVS caps them at 15 characters.
constexpr char kNvsSurveyKeyPrefix[] = "surv";

// R8 - WHICH SITE THE CAMPAIGN IS UP TO, persisted alongside the runs themselves.
//
// The ROLE is deliberately not persisted (R1 - a power cycle re-asks). Campaign
// PROGRESS is a different thing and must be, because this board has no battery and
// every move between the laptop and a power bank is a power cycle. Without it the
// cursor restarted at site 0 after every swap, and the only way forward was to press
// PRG past the sites already done - which STORES an empty run over each one on the
// way. That is the campaign destroying itself to get back to where it was.
constexpr char kNvsSurveySiteKey[] = "survsite";

void survey_nvs_key(size_t site, char* out, size_t cap) {
  std::snprintf(out, cap, "%s%u", kNvsSurveyKeyPrefix,
                static_cast<unsigned>(site));
}

void resp_log_save() {
  uint8_t      blob[PositionLog::kBlobMaxLen];
  const size_t n = g_resp_log.serialize(blob, sizeof(blob));
  if (n == 0) return;
  if (!g_prefs.begin(kNvsNamespace, /* readOnly */ false)) return;
  g_prefs.putBytes(kNvsLogKey, blob, n);
  g_prefs.end();
}

void resp_log_load() {
  if (!g_prefs.begin(kNvsNamespace, /* readOnly */ true)) return;
  uint8_t      blob[PositionLog::kBlobMaxLen];
  const size_t n = g_prefs.getBytesLength(kNvsLogKey);
  if (n > 0 && n <= sizeof(blob)) {
    g_prefs.getBytes(kNvsLogKey, blob, n);
    // A failed load leaves the log cleared rather than half-populated, so a corrupt
    // blob cannot masquerade as short data.
    if (!g_resp_log.deserialize(blob, n)) {
      Serial.println(F("# stored position log was unreadable - discarded"));
    }
  }
  g_prefs.end();
}

// R6 - "dumped over serial on reconnect". The board comes back from the walk with
// this in it and a laptop attached.
void resp_log_dump() {
  Serial.println(F("--- responder position log (R6) ---"));
  if (g_resp_log.overflowed()) {
    Serial.println(F("# WARNING: ring wrapped - earliest positions were displaced"));
  }
  Serial.println(F("RESP,position,probes_heard,echoes_sent,"
                   "rssi_mean10,rssi_min10,rssi_max10,"
                   "snr_mean10,snr_min10,snr_max10"));
  for (size_t i = 0; i < g_resp_log.count(); ++i) {
    const PositionSummary* e = g_resp_log.at(i);
    if (e == nullptr || !e->used) continue;
    Serial.print(F("RESP,"));
    Serial.print(e->position_id);       Serial.print(',');
    Serial.print(e->probes_heard);      Serial.print(',');
    Serial.print(e->echoes_sent);       Serial.print(',');
    Serial.print(e->rssi_dbm10.mean()); Serial.print(',');
    Serial.print(e->rssi_dbm10.empty() ? kI16NotAvailable : e->rssi_dbm10.min);
    Serial.print(',');
    Serial.print(e->rssi_dbm10.empty() ? kI16NotAvailable : e->rssi_dbm10.max);
    Serial.print(',');
    Serial.print(e->snr_db10.mean());   Serial.print(',');
    Serial.print(e->snr_db10.empty() ? kI16NotAvailable : e->snr_db10.min);
    Serial.print(',');
    Serial.println(e->snr_db10.empty() ? kI16NotAvailable : e->snr_db10.max);
  }
  // A '#' marker like the sweep's and the survey's, so capture.py can stop on it.
  // The "--- end ---" banner is for a human reading the console; the tool needs a
  // line it already knows how to recognise.
  Serial.println(F("# responder log complete"));
  Serial.println(F("--- end responder log ---"));
}


// dBm/dB as reported by RadioLib, converted to the tenths this firmware carries
// everywhere. Rounds away from zero so a negative RSSI is never nudged optimistic.
int16_t to_tenths(float v) {
  const float scaled = v * 10.0f;
  return static_cast<int16_t>(scaled >= 0.0f ? (scaled + 0.5f) : (scaled - 0.5f));
}

// ---------------------------------------------------------------------------
// R8 / M20 - the ambient survey.
//
// NOTHING HERE TRANSMITS. The mode exists to answer what spec 12.1 requires before D1
// may fix a frequency, and the only radio calls it makes are retune, receive and read
// RSSI. There is deliberately no path from this code to transmit() or set_power().
// ---------------------------------------------------------------------------

void survey_save_site_cursor() {
  if (!g_prefs.begin(kNvsNamespace, /* readOnly */ false)) return;
  g_prefs.putUShort(kNvsSurveySiteKey, static_cast<uint16_t>(g_campaign.site()));
  g_prefs.end();
}

bool survey_save_site(size_t site) {
  uint8_t      blob[Survey::kBlobMaxLen];
  const size_t n = g_survey.serialize(g_survey_plan, blob, sizeof(blob));
  if (n == 0) {
    Serial.println(F("# survey serialize failed - NOT stored"));
    return false;
  }
  if (!g_prefs.begin(kNvsNamespace, /* readOnly */ false)) {
    Serial.println(F("# NVS open failed - survey NOT stored"));
    return false;
  }
  char key[16];
  survey_nvs_key(site, key, sizeof(key));
  const size_t w = g_prefs.putBytes(key, blob, n);
  g_prefs.end();

  // Reported either way, loudly. A press that silently failed to store is the one
  // outcome that costs a second trip to that site (repo rule 4's reasoning, applied
  // to a button rather than a frame). NVS filling up is a REAL possibility here -
  // seven blobs of 1.6 kB in a 20 kB partition - and it fails by short write.
  if (w == n) {
    Serial.print(F("# survey stored: site "));
    Serial.print(static_cast<unsigned>(site));
    Serial.print(' ');
    Serial.print(survey_site_name(site));
    Serial.print(F(", "));
    Serial.print(g_survey.passes());
    Serial.println(F(" passes"));
    return true;
  }
  Serial.print(F("# NVS WRITE FAILED for site "));
  Serial.print(survey_site_name(site));
  Serial.println(F(" - survey NOT stored. Read it out with 'd' before moving on."));
  return false;
}

// R11 - PRG in survey mode. The same gesture the walk uses to mark a position, so the
// operator learns one button for both jobs, but it now means one of two things:
//
//   HELD    -> start the dwell at this site. Clears the accumulator first, so nothing
//              heard on the walk in is counted.
//   RUNNING -> store the run and advance, returning to HELD for the walk out.
//
// Two presses per site, and the walk between them is not measured. Before R11 the scan
// never stopped and every site carried its inbound transit into a peak hold that never
// forgets; see survey.h and the engineering log, 2026-09-05.
void survey_prg_press() {
  if (g_campaign.classify_press() == SurveyPress::StartDwell) {
    // Cleared HERE, not on the store, so a long hold accumulates nothing: whatever the
    // radio picked up while walking is discarded at the moment the dwell begins.
    g_survey.reset();
    g_survey_saved = false;
    g_campaign.note_started();
    Serial.print(F("# survey RUNNING at site "));
    Serial.print(static_cast<unsigned>(g_campaign.site()));
    Serial.print(' ');
    Serial.println(survey_site_name(g_campaign.site()));
    return;
  }

  g_survey_saved = survey_save_site(g_campaign.site());
  // A failed store leaves the phase RUNNING and the cursor put: the run stays in
  // memory and still accumulating, so the operator can press again or read it out.
  // Advancing over a site that was not written is a site silently lost.
  if (!g_campaign.note_stored(g_survey_saved)) {
    if (g_survey_saved) {
      Serial.println(F("# all sites stored - staying on the last one. 'a' dumps them "
                       "all."));
    }
    return;
  }
  survey_save_site_cursor();   // survives the next power cycle
  Serial.print(F("# survey HELD - walk to site "));
  Serial.print(static_cast<unsigned>(g_campaign.site()));
  Serial.print(' ');
  Serial.print(survey_site_name(g_campaign.site()));
  Serial.println(F(", then press PRG to start the dwell"));
}

// Loads one site's stored run INTO g_survey, replacing whatever is there. True when
// a blob was found and parsed.
bool survey_load_site(size_t site) {
  if (!g_prefs.begin(kNvsNamespace, /* readOnly */ true)) return false;
  uint8_t      blob[Survey::kBlobMaxLen];
  char         key[16];
  survey_nvs_key(site, key, sizeof(key));
  if (!g_prefs.isKey(key)) {          // asking for a missing key logs an ESP-IDF error
    g_prefs.end();
    return false;
  }
  const size_t n = g_prefs.getBytesLength(key);
  bool         loaded = false;
  if (n > 0 && n <= sizeof(blob)) {
    g_prefs.getBytes(key, blob, n);
    SurveyPlan stored{};
    if (g_survey.deserialize(blob, n, &stored)) {
      g_survey_plan.start_hz = stored.start_hz;
      g_survey_plan.step_hz  = stored.step_hz;
      loaded = true;
    } else {
      Serial.print(F("# stored survey for site "));
      Serial.print(survey_site_name(site));
      Serial.println(F(" was unreadable - discarded"));
    }
  }
  g_prefs.end();
  return loaded;
}

void survey_load_site_cursor() {
  if (!g_prefs.begin(kNvsNamespace, /* readOnly */ true)) return;
  if (g_prefs.isKey(kNvsSurveySiteKey)) {
    const uint16_t v = g_prefs.getUShort(kNvsSurveySiteKey, 0);
    // Clamped rather than trusted: a stale cursor from a shorter site list would
    // otherwise index off the end of the name table.
    // Always restored HELD (R11): a power cycle happens between sites, with the
    // board in a bag or on a charger, and resuming a dwell is the operator's call.
    g_campaign.restore_site(v);
  }
  g_prefs.end();
}

void survey_clear_nvs() {
  if (!g_prefs.begin(kNvsNamespace, /* readOnly */ false)) return;
  for (size_t i = 0; i < kSurveySiteCount; ++i) {
    char key[16];
    survey_nvs_key(i, key, sizeof(key));
    g_prefs.remove(key);
  }
  if (g_prefs.isKey(kNvsSurveySiteKey)) g_prefs.remove(kNvsSurveySiteKey);
  g_prefs.end();
  g_campaign.reset();
  Serial.println(F("# all stored surveys erased from NVS, site cursor reset"));
}

// R8 acceptance - the trace. Printed in the same schema whether it came from NVS or
// from the run in progress, because a reader must not have to care which.
// `standalone` prints the CSV header and the completion marker around the rows. A
// campaign dump passes false and emits ONE header for all seven sites: a header
// reprinted per site is indistinguishable, to anything reading the port, from the
// board having rebooted mid-capture - which is exactly how capture.py first read it.
void survey_dump(size_t site, bool standalone) {
  Serial.println(F("--- ambient survey (R8 / M20) ---"));
  Serial.print(F("# site=")); Serial.print(static_cast<unsigned>(site));
  Serial.print(' '); Serial.println(survey_site_name(site));
  Serial.print(F("# passes=")); Serial.println(g_survey.passes());
  // R11 provenance, read from THE BLOB and not assumed from this firmware's version.
  // Pre-R11 runs scanned continuously between sites, so their peak column carries
  // bursts heard in transit; a reader cannot tell the two apart from the numbers.
  // Printing a constant here made a re-dump of the pre-R11 campaign claim a discipline
  // it never had - which is the exact provenance error the line exists to prevent.
  Serial.print(F("# hold_discipline="));
  Serial.println(g_survey.hold_discipline() ? 1 : 0);
  Serial.print(F("# bins_sampled=")); Serial.print(g_survey.bins_sampled());
  Serial.print(F(" of ")); Serial.println(kSurveyBinCount);
  Serial.print(F("# dwell_ms=")); Serial.print(g_survey_plan.dwell_ms);
  Serial.print(F(" settle_ms=")); Serial.print(g_survey_plan.settle_ms);
  Serial.print(F(" sample_interval_ms="));
  Serial.println(g_survey_plan.sample_interval_ms);

  char line[kSurveyCsvMaxLine];
  if (standalone) {
    if (survey_csv_header(line, sizeof(line)) == 0) {
      Serial.println(F("# ERROR: survey header did not fit its buffer"));
      return;
    }
    Serial.println(line);
  }

  for (size_t i = 0; i < kSurveyBinCount; ++i) {
    SurveyCsvRow row{};
    row.site      = site;
    row.bin_index = i;
    row.freq_hz   = survey_bin_freq_hz(i);
    row.passes    = g_survey.passes();
    row.bin       = &g_survey.bin(i);
    if (survey_csv_row(row, line, sizeof(line)) == 0) {
      Serial.print(F("# ERROR: survey row did not fit for bin "));
      Serial.println(static_cast<unsigned>(i));
      continue;
    }
    Serial.println(line);
  }
  // capture.py stops on this marker, the way it stops on "sweep complete". A
  // campaign dump emits it once at the end instead, or a capture asked for one unit
  // of work would stop after the first of seven sites.
  if (standalone) Serial.println(F("# survey dump complete"));
  Serial.println(F("--- end ambient survey ---"));
}

// R8 acceptance - all seven sites in one listing, so the whole campaign lands in one
// committed trace. Destroys the in-memory run, so it reloads it afterwards; the
// operator dumping mid-campaign must not lose the site in progress.
void survey_dump_all(bool restore_current) {
  Survey   in_progress = g_survey;
  uint32_t found = 0;

  char hdr[kSurveyCsvMaxLine];
  bool header_printed = false;

  for (size_t i = 0; i < kSurveySiteCount; ++i) {
    if (!survey_load_site(i)) continue;
    if (!header_printed) {
      if (survey_csv_header(hdr, sizeof(hdr)) == 0) {
        Serial.println(F("# ERROR: survey header did not fit its buffer"));
        break;
      }
      Serial.println(hdr);
      header_printed = true;
    }
    survey_dump(i, /* standalone */ false);
    ++found;
  }

  if (found == 0) {
    Serial.println(F("# no stored surveys in NVS"));
  } else {
    Serial.print(F("# survey campaign complete - "));
    Serial.print(found);
    Serial.println(F(" site(s)"));
  }
  if (restore_current) g_survey = in_progress;
}

// Retunes and re-enters receive for `bin`. Errors are reported and the bin is skipped
// rather than silently contributing samples taken on the previous frequency, which is
// the one failure that would put a real occupant in the wrong bin.
bool survey_enter_bin(size_t bin) {
  const uint32_t hz = survey_bin_freq_hz(bin);
  const int16_t  st = g_radio.set_frequency(hz);
  if (st != 0) {
    Serial.print(F("# survey retune failed at bin "));
    Serial.print(static_cast<unsigned>(bin));
    Serial.print(F(" code ")); Serial.println(st);
    return false;
  }
  const int16_t rx = g_radio.start_receive();
  if (rx != 0) {
    Serial.print(F("# survey startReceive failed at bin "));
    Serial.print(static_cast<unsigned>(bin));
    Serial.print(F(" code ")); Serial.println(rx);
    return false;
  }

  const uint32_t now  = millis();
  g_survey_listen_at_ms = now + g_survey_plan.settle_ms;
  g_survey_next_sample  = g_survey_listen_at_ms;
  g_survey_bin_ends_ms  = now + g_survey_plan.dwell_ms;
  return true;
}

void survey_dump_plan() {
  Serial.println(F("--- survey plan (R8) ---"));
  Serial.print(F("bins=")); Serial.println(static_cast<unsigned>(kSurveyBinCount));
  Serial.print(F("start_hz=")); Serial.println(g_survey_plan.start_hz);
  Serial.print(F("step_hz=")); Serial.println(g_survey_plan.step_hz);
  Serial.print(F("dwell_ms=")); Serial.println(g_survey_plan.dwell_ms);
  Serial.print(F("settle_ms=")); Serial.println(g_survey_plan.settle_ms);
  Serial.print(F("samples_per_dwell="));
  Serial.println(survey_samples_per_dwell(g_survey_plan));
  Serial.print(F("pass_duration_s="));
  Serial.println((survey_pass_duration_ms(g_survey_plan) + 500) / 1000);
  Serial.println(F("# occupancy detection is PROBABILISTIC - see survey.h. Run for "
                   "several minutes; a quiet bin is not a proven empty one."));
  Serial.print(F("sites=")); Serial.println(static_cast<unsigned>(kSurveySiteCount));
  Serial.println(F("# keys: d=dump this site  a=dump ALL stored  s=store here  "
                   "p=PRG press (start dwell, or store+next)"));
  Serial.println(F("#       n=next site (no store)  b=previous site (no store)"));
  Serial.println(F("# R11: the scan is HELD between sites. Two presses per site - one\n"
                   "#      on arrival to start the dwell, one when it is done to store\n"
                   "#      and move on. The walk between them is not measured."));
  Serial.println(F("#       x=clear memory  z=erase all stored  [ ]=dwell -/+ 10ms"));
  Serial.println(F("--- end survey plan ---"));
}

void loop_survey() {
  // PRG stores this site and moves to the next. The sites have no laptop; this is
  // the whole reason the blob exists.
  if (prg_edge()) survey_prg_press();

  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch == 'd' || ch == 'D') {
      survey_dump(g_campaign.site(), /* standalone */ true);  // the run in progress
    } else if (ch == 'a' || ch == 'A') {
      survey_dump_all(/* restore_current */ true);   // everything stored
    } else if (ch == 's' || ch == 'S') {
      g_survey_saved = survey_save_site(g_campaign.site());  // store, do not advance
    } else if (ch == 'p' || ch == 'P') {
      survey_prg_press();                  // same as PRG, for the tethered bench
    } else if (ch == 'x' || ch == 'X') {
      g_survey.reset();
      g_survey_saved = false;
      Serial.println(F("# survey cleared (memory only - NVS untouched)"));
    } else if (ch == 'z' || ch == 'Z') {
      survey_clear_nvs();
    } else if (ch == 'n' || ch == 'N') {
      // Advance WITHOUT storing. The store-and-advance path would write the run in
      // progress over whatever is already in the next slot, so correcting a cursor
      // must not go through it.
      if (g_campaign.next_site()) {
        survey_save_site_cursor();
        g_survey.reset();
        g_survey_saved = false;
        Serial.print(F("# survey site -> "));
        Serial.print(static_cast<unsigned>(g_campaign.site()));
        Serial.print(' ');
        Serial.print(survey_site_name(g_campaign.site()));
        Serial.println(F(" (skipped, nothing stored, HELD)"));
      }
    } else if (ch == 'b' || ch == 'B') {
      if (g_campaign.prev_site()) {
        survey_save_site_cursor();
        g_survey.reset();
        g_survey_saved = false;
        Serial.print(F("# survey site -> "));
        Serial.print(static_cast<unsigned>(g_campaign.site()));
        Serial.print(' ');
        Serial.print(survey_site_name(g_campaign.site()));
        Serial.println(F(" (stepped back, nothing stored, HELD)"));
      }
    } else if (ch == '[' && g_survey_plan.dwell_ms > 10) {
      // Rule 8 - the timing is data, adjustable at runtime, not a compile-time
      // constant. Bounded so the dwell can never fall to or below the settle time,
      // which would leave every bin with no listening window at all.
      g_survey_plan.dwell_ms = static_cast<uint16_t>(g_survey_plan.dwell_ms - 10);
      if (g_survey_plan.dwell_ms <= g_survey_plan.settle_ms) {
        g_survey_plan.dwell_ms = static_cast<uint16_t>(g_survey_plan.settle_ms + 10);
      }
      Serial.print(F("# dwell_ms=")); Serial.println(g_survey_plan.dwell_ms);
    } else if (ch == ']' && g_survey_plan.dwell_ms < 500) {
      g_survey_plan.dwell_ms = static_cast<uint16_t>(g_survey_plan.dwell_ms + 10);
      Serial.print(F("# dwell_ms=")); Serial.println(g_survey_plan.dwell_ms);
    }
  }

  const uint32_t now = millis();

  // R11 - HELD. The scan does not advance and does not sample, so the walk between
  // sites is not folded into the next site's run. The display still refreshes, because
  // a board that has gone blank at the moment the operator arrives is a board they
  // cannot tell from a crashed one.
  //
  // The radio is left in RX and simply not read. Nothing here transmits in either
  // phase, which is the property the whole mode rests on.
  if (g_campaign.held()) {
    if (static_cast<int32_t>(now - g_survey_next_draw_ms) >= 0) {
      g_survey_next_draw_ms = now + kSurveyDrawIntervalMs;
      g_ui.show_survey_held(survey_site_name(g_campaign.site()),
                            g_campaign.site(), kSurveySiteCount, g_survey_saved);
    }
    return;
  }

  if (static_cast<int32_t>(now - g_survey_bin_ends_ms) >= 0) {
    ++g_survey_bin;
    if (g_survey_bin >= kSurveyBinCount) {
      g_survey_bin = 0;
      g_survey.note_pass();
      if (g_survey.passes() % kSurveyProgressPasses == 0) {
        Serial.print(F("# survey pass "));
        Serial.print(g_survey.passes());
        Serial.print(F(", bins sampled "));
        Serial.println(static_cast<unsigned>(g_survey.bins_sampled()));
      }
    }
    if (!survey_enter_bin(g_survey_bin)) {
      // Skipped: give the bin its dwell anyway so one bad retune cannot spin the loop.
      g_survey_bin_ends_ms = now + g_survey_plan.dwell_ms;
      g_survey_listen_at_ms = g_survey_bin_ends_ms;   // no samples from this bin
    }
    return;
  }

  if (static_cast<int32_t>(now - g_survey_listen_at_ms) >= 0 &&
      static_cast<int32_t>(now - g_survey_next_sample) >= 0) {
    g_survey.add_sample(g_survey_bin, to_tenths(g_radio.instant_rssi_dbm()));
    const uint16_t iv = g_survey_plan.sample_interval_ms > 0
                            ? g_survey_plan.sample_interval_ms : 1;
    g_survey_next_sample = now + iv;
  }

  if (static_cast<int32_t>(now - g_survey_next_draw_ms) >= 0) {
    g_survey_next_draw_ms = now + kSurveyDrawIntervalMs;
    const size_t loud = g_survey.loudest_bin();
    g_ui.show_survey(survey_site_name(g_campaign.site()),
                     g_survey.passes(), survey_bin_freq_hz(g_survey_bin),
                     loud < kSurveyBinCount ? survey_bin_freq_hz(loud) : 0,
                     loud < kSurveyBinCount ? g_survey.bin(loud).rssi_dbm10.max
                                            : kI16NotAvailable,
                     g_survey_saved);
  }
}

// R9 - defined with the rest of the W9 bench below, declared here because setup()
// starts the first run. Both blocks are the same anonymous namespace.
void w9_begin_run(W9Run run);

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t serial_wait = millis();
  while (!Serial && millis() - serial_wait < 2000) delay(10);

  Serial.println();
  // The banner is what a capture is correlated against months later, so it names the
  // pass and the binding spec version. Both were stale until the pass 2 bench run
  // printed them next to a v0.8 repo - it still said "pass 1, branch 1 (R1-R3)" and
  // "v0.7" long after R4-R9 landed and the spec moved. A banner nobody updates is worse
  // than no banner: it is a confident wrong answer in every log file it appears in.
  Serial.println(F("LRAN range test firmware - pass 2 (two board profiles)"));
  Serial.println(F("Binding spec: LRAN-Protocol-Specification v0.13 (ver = 2)"));
  Serial.println(F("This firmware never ships. No WiFi, no MQTT, no secrets."));

  if (!g_ui.begin()) {
    // A dark panel on this board is usually Vext, not the driver. Say so once,
    // then carry on: the serial console is the initiator's real instrument and a
    // dead OLED must not stop a bench bring-up.
    Serial.println(F("WARN: OLED did not ACK at 0x3C (check Vext) - continuing"));
  }

  g_role = select_role();
  g_ui.show_role(g_role, kBoard.short_name);
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
  // The sweep plan belongs to R4. In survey mode no sweep runs, and printing its
  // duration and probe counts into a survey trace would describe a measurement that
  // never happened - the exact failure the settings dump exists to prevent.
  // The sweep plan belongs to R4 and describes a measurement none of the other three
  // modes performs. Printing it into a W9 log would describe probes that never ran,
  // which is the same failure the survey exclusion exists to prevent.
  if (g_role != Role::Survey && !w9_role(g_role)) dump_sweep_plan();

  const int16_t st = g_radio.begin(kBoard, g_tp);
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

  // Handoff 6 requirement 7 / M21 findings 7.5 - the PA configuration on the record.
  //
  // PRINTED HERE, AFTER begin(), for two reasons. It is the configuration actually
  // applied rather than the one intended, and it is still before the CSV header, which
  // is what makes capture.py fold these lines into the trace's own header block with
  // the rest of the settings dump (SETTING_RE, `key=value`, no spaces). A trace has
  // never carried this: every committed CSV records the power requested and nothing
  // about the PA configuration that emitted it.
  //
  // Only the boot point's entry is printed, and that is enough. The configuration is a
  // pure function of conducted power, and every CSV row already carries its own
  // conducted power - so with `pa_optimize` and the table version on the record, the
  // entry for any row in the sweep is recoverable. What could not be recovered is the
  // flag, because it is invisible in RadioLib's one-argument overload.
  {
    char pa[192];
    const size_t pn = format_pa_config(g_radio.applied_pa_config(), pa, sizeof(pa));
    if (pn == 0) {
      // Same refusal as the settings dump: a partial record is worse than a named
      // absence, because it reads like a complete one.
      Serial.println(F("ERROR: PA config record did not fit its buffer"));
    } else {
      Serial.print(pa);
    }
  }

  // Both roles listen. The initiator is waiting for echoes, the responder for probes.
  const int16_t rx = g_radio.start_receive();
  if (rx != 0) {
    Serial.print(F("FATAL: startReceive() failed, code "));
    Serial.println(rx);
    while (true) delay(1000);
  }

  if (g_role == Role::Survey) {
    // R8. The board comes back from the far point with a stored survey in it, so it
    // is read out BEFORE the live run resets the accumulator - same reasoning as the
    // responder's log below, and the same trap if the order were reversed.
    // Everything stored, before the live run touches anything. The board comes home
    // from the loop with up to seven sites in it, and reading them out is the whole
    // point of having stored them.
    survey_dump_all(/* restore_current */ false);
    g_survey.reset();
    g_survey_saved = false;
    survey_load_site_cursor();
    Serial.print(F("# resuming campaign at site "));
    Serial.print(static_cast<unsigned>(g_campaign.site()));
    Serial.print(' ');
    Serial.print(survey_site_name(g_campaign.site()));
    // R11 - boot comes up HELD. The operator is not standing at the site when the
    // board boots, and a scan that started itself would charge the walk in to the run.
    Serial.println(F(" - HELD, press PRG to start the dwell"));

    survey_dump_plan();
    char shdr[kSurveyCsvMaxLine];
    if (survey_csv_header(shdr, sizeof(shdr)) > 0) Serial.println(shdr);

    if (!survey_enter_bin(0)) {
      Serial.println(F("FATAL: could not tune the first survey bin"));
      g_ui.show_message("SURVEY FAIL", "see serial");
      while (true) delay(1000);
    }
  }

  if (g_role == Role::Responder) {
    // R6 - the board comes back from the walk with a log in it and a laptop
    // attached, so it is read out at boot before anything overwrites it.
    resp_log_load();
    if (g_resp_log.count() > 0) resp_log_dump();

    // Start the hunt at configuration 0 and let the dwell clock carry it forward.
    resp_tune_to(0);
  }

  if (w9_role(g_role)) {
    // R9. Both W9 ends already listen (start_receive above). The initiator drives;
    // the responder is purely reactive and needs no state beyond its reassembler.
    Serial.println(F("# W9 - spec 6.6 PING over RF, against /lib/lran-protocol/."));
    Serial.print(F("# node ids: initiator 0x"));
    Serial.print(kW9Initiator, HEX);
    Serial.print(F(" responder 0x"));
    Serial.print(kW9Responder, HEX);
    Serial.println(F(" (spec 5.3 bench range - never a production node)"));

    if (g_role == Role::W9Initiator) {
      w9_begin_run(W9Run::MaxFrame);
    } else {
      Serial.println(F("# W9 responder: reassembles, verifies, re-fragments the echo "
                       "(spec 6.6.2). Waiting."));
    }
  }

  if (g_role == Role::Initiator) {
    if (!begin_test_point()) {
      Serial.println(F("FATAL: could not load test point 0"));
      while (true) delay(1000);
    }
    // From the same schema string the rows are built from, so the two cannot drift.
    // R7 moves these traces into committed files under docs/rangetest/data/.
    char hdr[kCsvMaxLine];
    if (csv_header(hdr, sizeof(hdr)) > 0) Serial.println(hdr);

    // Armed from the start, so the first sweep begins on the operator's first PRG
    // press rather than against a responder that has not been booted yet. Beacon
    // immediately: the responder's hunt has to be able to find us.
    g_next_beacon_ms = millis();
    Serial.println(F("# ARMED at boot - press PRG on the responder to start "
                     "position 1. No sweep runs until you do."));
    g_ui.show_armed(g_role, g_swept_position, g_sweeps_completed);
  }
}

namespace {

// INITIATOR: send probe `g_probe_index` of the current test point.
void send_probe() {
  BenchFrame f{};
  // Marked on the wire, not just locally: the responder must exclude these too, or
  // its resp_heard tally exceeds the initiator's probes_sent and the downlink/uplink
  // comparison stops working.
  f.kind        = (g_warmup_left > 0) ? BenchKind::WarmupProbe : BenchKind::Probe;
  f.position_id = g_swept_position;
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
  // Cumulative for this test point, so the latest echo carries the fullest count.
  if (f.resp_heard != kU16NotAvailable) g_stats.resp_heard = f.resp_heard;
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

  ++g_tp_index;
  if (g_tp_index >= sweep_point_count(g_plan)) {
    // R5 - ONE SWEEP PER POSITION. Stop here rather than wrapping. A free-running
    // loop re-measures a position the operator has already left, and the wrap costs
    // the responder a reacquisition it does not need to pay.
    g_tp_index = 0;
    ++g_sweeps_completed;
    g_init_state    = InitState::Armed;
    g_awaiting_echo = false;
    g_next_beacon_ms = millis();

    Serial.print(F("# sweep complete at position "));
    Serial.print(g_swept_position);
    Serial.println(F(" - ARMED, press PRG on the responder for the next position"));
    g_ui.show_armed(g_role, g_swept_position, g_sweeps_completed);
    return;
  }

  if (!begin_test_point()) {
    Serial.println(F("FATAL: could not load next test point"));
    while (true) delay(1000);
  }
}

// INITIATOR: begin a sweep at `position`.
void start_sweep(uint16_t position) {
  g_swept_position = position;
  g_tp_index       = 0;
  g_probe_index    = 0;
  g_awaiting_echo  = false;

  // The next sweep starts on configuration 0 while the responder may be anywhere in
  // its hunt, so the configuration is deliberately treated as changed - that is what
  // buys the warmup probes that keep reacquisition out of the statistics.
  g_have_prev_config = false;

  Serial.print(F("# sweep start, position "));
  Serial.println(position);

  if (!begin_test_point()) {
    Serial.println(F("FATAL: could not load test point 0"));
    while (true) delay(1000);
  }
  g_init_state = InitState::Sweeping;
}

// INITIATOR, ARMED: a cheap probe on configuration 0, purely so the responder's hunt
// can find us and its echo can tell us where the operator now is.
void send_beacon() {
  RadioConfigKey cfg{};
  if (!sweep_config_at(g_plan, 0, &cfg)) return;

  TestPoint tp{};
  tp.freq_hz     = cfg.freq_hz;
  tp.sf          = cfg.sf;
  tp.cr_denom    = cfg.cr_denom;
  tp.payload_len = static_cast<uint8_t>(kBenchHeaderLen);
  if (clamp_conducted(kSx1262MinDbm, kAntennaGainDbi10, &tp.power) ==
      ClampResult::BelowRadioFloor) {
    return;
  }
  g_radio.apply(tp);

  BenchFrame f{};
  // ArmedBeacon, not Probe and no longer WarmupProbe. A beacon is scaffolding: it
  // exists so the responder's hunt can find us and its echo can carry the position
  // forward, and it must not be counted by either end. Sent as a plain Probe it was
  // tallied against real test point 0 - the bench showed resp_heard=9 against
  // probes_sent=8 on the first point of the second position, which is the beacons the
  // responder heard while armed.
  //
  // It has its OWN kind because the walking operator, several hundred feet away, can
  // only see the responder's display: this frame is what tells them the sweep is
  // finished and they may press PRG and move on. As a WarmupProbe it was
  // indistinguishable from the warmups sent DURING a sweep at each configuration
  // change, and the display would have said "done" five times too early.
  f.kind        = BenchKind::ArmedBeacon;
  f.position_id = g_swept_position;
  f.tp_index    = 0;
  f.probe_seq   = g_probe_seq++;

  const size_t n = bench_serialize(f, g_buf, sizeof(g_buf), tp.payload_len);
  if (n > 0) g_radio.transmit(g_buf, n);
  g_radio.start_receive();
}

// RESPONDER: echo a probe back with our own measurement of it attached.
void echo_probe(const BenchFrame& probe_in, size_t rx_len) {
  BenchFrame f{};
  f.kind        = BenchKind::Echo;
  f.position_id = g_position_id;   // R5 - ours, not the probe's
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
  f.resp_heard = g_resp_tp_heard;

  const size_t n = bench_serialize(f, g_buf, sizeof(g_buf), rx_len);
  if (n == 0) return;

  g_radio.transmit(g_buf, n);
  ++g_resp_echoes;
  if (bench_is_counted(probe_in.kind)) g_resp_log.record_echo(g_position_id);
  g_radio.start_receive();
}


// ---------------------------------------------------------------------------
// R9 / W9 - the protocol bench. Spec 6.6, 11.
//
// Real PING frames through the real codec, which is the one thing the sweep does
// not do. Two runs: the 222-byte maximum frame (spec 6.6.1), then the 15-fragment
// set (spec 6.6.2). Both ends use PATTERN_FILL, so a fault comes back as a byte
// OFFSET rather than as "one of them failed" (spec 6.6.3).
// ---------------------------------------------------------------------------

// One reassembler per peer (see lran/reassembly.h). Two boards, one peer each.
lran::Reassembler g_w9_re;

// What the responder inferred about the incoming set's split. Reset per set.
uint16_t g_w9_largest_frag = 0;
uint8_t  g_w9_frag_total   = 1;

W9Plan  g_w9_plan;
W9Stats g_w9_stats;
W9Run   g_w9_run       = W9Run::MaxFrame;
bool    g_w9_done      = false;

lran::Seq g_w9_seq      = 1;
uint16_t  g_w9_sent     = 0;
bool      g_w9_awaiting = false;
uint32_t  g_w9_deadline = 0;

// The payload of the PING currently in flight, kept so the echo can be checked
// against what was actually sent rather than against a rebuild of it.
uint8_t g_w9_payload[lran::kMaxPayloadPlain];
size_t  g_w9_payload_len = 0;

// How long to wait for an echo of a set.
//
// Sized from AIRTIME, not from a guess: the fragmented run puts 15 frames on the air
// in each direction, and a timeout that fitted the single-frame run would score every
// fragmented PING lost. Two legs, every frame of the set, plus the plan's margin.
uint32_t w9_echo_timeout_ms(const W9Plan& plan) {
  LoraParams lp{};
  lp.sf       = g_tp.sf;
  lp.cr_denom = g_tp.cr_denom;

  const size_t chunk = (plan.frag_chunk == 0) ? plan.payload_len : plan.frag_chunk;
  const uint32_t per_frame =
      airtime_ms(lp, static_cast<uint16_t>(lran::frame_len(chunk, false)));

  return 2U * per_frame * plan.expect_frags + g_plan.echo_timeout_margin_ms;
}

// Transmits one PING, fragmenting it when the run calls for it. Returns false if the
// codec refused - which is a finding, not a retry: encode() declining to emit a frame
// means this firmware asked for something the specification does not permit.
bool w9_transmit_set(const lran::Header& hdr, const uint8_t* payload, size_t payload_len,
                     size_t frag_chunk, uint8_t frags) {
  uint8_t frame[lran::kMaxFrame];
  size_t  frame_len = 0;
  lran::EncodeCtx enc;  // spec 9.2 - PING carries no MAC, so no key material here

  for (uint8_t i = 0; i < frags; ++i) {
    const lran::Status st =
        (frag_chunk == 0)
            ? lran::encode(hdr, payload, payload_len, enc, frame, sizeof(frame), &frame_len)
            : lran::encode_fragment(hdr, payload, payload_len, i, frag_chunk, enc, frame,
                                    sizeof(frame), &frame_len);
    if (st != lran::Status::Ok) {
      Serial.print(F("# W9 encode refused, lran::Status "));
      Serial.println(static_cast<int>(st));
      return false;
    }
    if (g_radio.transmit(frame, frame_len) != 0) return false;
    ++g_w9_stats.frames_sent;
  }
  g_radio.start_receive();
  return true;
}

void w9_report_run() {
  const bool passed = w9_run_passed(g_w9_stats);

  Serial.print(F("# W9 run "));
  Serial.print(to_string(g_w9_run));
  Serial.println(passed ? F(" PASSED") : F(" FAILED"));

  Serial.print(F("# pings="));      Serial.print(g_w9_stats.pings_sent);
  Serial.print(F(" frames="));      Serial.print(g_w9_stats.frames_sent);
  Serial.print(F(" echoes_ok="));   Serial.print(g_w9_stats.echoes_ok);
  Serial.print(F(" timeouts="));    Serial.print(g_w9_stats.echo_timeouts);
  Serial.print(F(" pattern="));     Serial.print(g_w9_stats.pattern_faults);
  Serial.print(F(" decode="));      Serial.print(g_w9_stats.decode_faults);
  Serial.print(F(" reasm_fail="));  Serial.print(g_w9_stats.reassembly_fails);
  Serial.print(F(" frag_late="));   Serial.print(g_w9_stats.late_fragments);
  Serial.print(F(" phy_crc="));     Serial.print(g_w9_stats.crc_errors);
  Serial.print(F(" foreign="));     Serial.println(g_w9_stats.foreign_frames);

  // spec 6.6.3 - the offset is the finding. Byte 0 diverging says something very
  // different about the path than byte 168 of a 15-fragment set does.
  if (g_w9_stats.any_pattern_fault) {
    Serial.print(F("# PATTERN FAULT, deepest first-bad offset "));
    Serial.print(static_cast<unsigned long>(g_w9_stats.first_bad_worst));
    Serial.print(F(" on seq "));
    Serial.println(g_w9_stats.first_bad_seq);
  }

  // R9 - the airtime check, asked for by name: 12.3's backoff defaults were chosen
  // against an EMPTY channel, and a 222-byte frame is the case that tests it.
  LoraParams lp{};
  lp.sf       = g_tp.sf;
  lp.cr_denom = g_tp.cr_denom;
  const uint32_t max_frame_ms = airtime_ms(lp, lran::kMaxFrame);
  const W9AirtimeCheck ac = w9_airtime_check(max_frame_ms, kW9SpecBackoffMaxMs);
  Serial.print(F("# spec 15.1 airtime of a 222-byte frame at SF"));
  Serial.print(g_tp.sf);
  Serial.print(F(" = "));
  Serial.print(ac.frame_airtime_ms);
  Serial.print(F(" ms; spec 12.3 max backoff "));
  Serial.print(ac.backoff_max_ms);
  Serial.println(ac.backoff_covers
                     ? F(" ms - window covers one frame")
                     : F(" ms - WINDOW IS SHORTER THAN ONE FRAME (finding)"));
}

void w9_begin_run(W9Run run) {
  g_w9_run  = run;
  g_w9_plan = w9_plan(run);
  w9_stats_reset(&g_w9_stats);
  g_w9_sent     = 0;
  g_w9_awaiting = false;
  g_w9_re.reset();
  g_w9_re.forget_completed();

  Serial.print(F("# W9 run "));
  Serial.print(to_string(run));
  Serial.print(F(": n="));
  Serial.print(g_w9_plan.echo_n);
  Serial.print(F(" payload="));
  Serial.print(g_w9_plan.payload_len);
  Serial.print(F(" frags="));
  Serial.print(g_w9_plan.expect_frags);
  Serial.print(F(" chunk="));
  Serial.print(static_cast<unsigned>(g_w9_plan.frag_chunk));
  Serial.print(F(" pings="));
  Serial.println(g_w9_plan.pings);
}

void w9_send_next_ping() {
  ++g_w9_seq;
  if (g_w9_seq == 0) g_w9_seq = 1;  // seq 0 is reserved for "no reference" (spec 6.5)

  if (w9_build_ping(g_w9_seq, g_w9_plan.echo_n, g_w9_payload, sizeof(g_w9_payload),
                    &g_w9_payload_len) != lran::Status::Ok) {
    Serial.println(F("FATAL: W9 could not build its own PING"));
    g_w9_done = true;
    return;
  }

  const lran::Header hdr = w9_header(g_w9_seq, kW9Initiator, kW9Responder);
  g_w9_re.reset();

  ++g_w9_stats.pings_sent;
  ++g_w9_sent;
  if (!w9_transmit_set(hdr, g_w9_payload, g_w9_payload_len, g_w9_plan.frag_chunk,
                       g_w9_plan.expect_frags)) {
    ++g_w9_stats.decode_faults;
  }

  g_w9_awaiting = true;
  g_w9_deadline = millis() + w9_echo_timeout_ms(g_w9_plan);
}

// Feeds one received frame into the reassembler and reports whether a payload is
// ready. Shared by both W9 ends: the responder reassembles an incoming PING and the
// initiator reassembles the echo, and spec 11 makes no distinction between them.
bool w9_accept_frame(const uint8_t* buf, size_t len, lran::NodeId self, W9Stats* stats) {
  lran::DecodeCtx dec;
  dec.self = self;
  // spec 9.2 - PING is unauthenticated. A production receiver leaving this null
  // accepts forged COMMANDs; this one never sees a COMMAND. See CLAUDE.md.
  dec.mac = nullptr;

  lran::Frame f;
  if (lran::decode_header(buf, len, dec, &f) != lran::Status::Ok) {
    ++stats->foreign_frames;  // not addressed to us, or not an LRAN frame at all
    return false;
  }
  if (lran::decode_payload(buf, len, dec, &f) != lran::Status::Ok) {
    ++stats->decode_faults;
    return false;
  }
  if (f.hdr.type != lran::MsgType::Ping) {
    ++stats->foreign_frames;
    return false;
  }

  const lran::Status st = g_w9_re.accept(f, millis());
  if (st == lran::Status::FragLate) {
    // spec 11.2 - a fragment of a set already completed. R9 asks whether this link
    // produces late fragments at all, so this is a RESULT to count, not an error.
    ++stats->late_fragments;
    return false;
  }
  if (st != lran::Status::Ok) {
    ++stats->reassembly_fails;
    return false;
  }
  return g_w9_re.complete();
}

void loop_w9_initiator() {
  if (g_w9_done) {
    delay(100);
    return;
  }

  uint8_t rx[lran::kMaxFrame];
  size_t  len       = 0;
  bool    crc_error = false;

  if (g_radio.poll(rx, sizeof(rx), &len, &crc_error)) {
    if (crc_error) {
      ++g_w9_stats.crc_errors;  // spec 14 stage 1
    } else if (w9_accept_frame(rx, len, kW9Initiator, &g_w9_stats)) {
      const W9EchoCheck c =
          w9_check_echo(g_w9_seq, g_w9_plan.echo_n, g_w9_re.data(), g_w9_re.len());
      if (c.ok()) {
        ++g_w9_stats.echoes_ok;
      } else if (!c.pattern_ok) {
        ++g_w9_stats.pattern_faults;
        g_w9_stats.any_pattern_fault = true;
        if (c.first_bad >= g_w9_stats.first_bad_worst) {
          g_w9_stats.first_bad_worst = c.first_bad;
          g_w9_stats.first_bad_seq   = g_w9_seq;
        }
      } else {
        ++g_w9_stats.decode_faults;
      }
      g_w9_awaiting = false;
    }
    g_radio.start_receive();
  }

  if (g_w9_awaiting) {
    if (static_cast<int32_t>(millis() - g_w9_deadline) >= 0) {
      ++g_w9_stats.echo_timeouts;
      g_w9_awaiting = false;
    }
    delay(2);
    return;
  }

  if (g_w9_sent >= g_w9_plan.pings) {
    w9_report_run();
    if (g_w9_run == W9Run::MaxFrame) {
      w9_begin_run(W9Run::Fragmented);
    } else {
      Serial.println(F("# W9 complete - both runs reported above. "
                       "Record the result in docs/rangetest/engineering-log.md."));
      g_w9_done = true;
      g_ui.show_message("W9 DONE", "see serial");
    }
    return;
  }

  w9_send_next_ping();
}

// The responder reports on an idle gap, not per set.
//
// WHY IT REPORTS AT ALL: the initiator can only count what happened on the leg coming
// BACK to it. Whether the INBOUND leg saw a late fragment, a PHY CRC error or a
// foreign frame is visible only here, and R9 asks whether this link produces late
// fragments at all - an answer covering one direction is half an answer. Same
// reasoning that put `resp_heard` in the sweep's echo (see bench_frame.h).
//
// An idle gap rather than a count, because the responder is not told where a run ends:
// the initiator moves from run 1 to run 2 without announcing it, and the gap between
// runs is the only edge the responder can see.
uint32_t g_w9_resp_last_rx  = 0;
bool     g_w9_resp_reported = true;

inline constexpr uint32_t kW9RespIdleReportMs = 5000;

void w9_report_responder() {
  Serial.print(F("# W9 responder inbound: sets_echoed="));
  Serial.print(g_w9_stats.pings_sent);
  Serial.print(F(" frames_sent="));  Serial.print(g_w9_stats.frames_sent);
  Serial.print(F(" decode="));       Serial.print(g_w9_stats.decode_faults);
  Serial.print(F(" reasm_fail="));   Serial.print(g_w9_stats.reassembly_fails);
  Serial.print(F(" frag_late="));    Serial.print(g_w9_stats.late_fragments);
  Serial.print(F(" phy_crc="));      Serial.print(g_w9_stats.crc_errors);
  Serial.print(F(" foreign="));      Serial.println(g_w9_stats.foreign_frames);
}

void loop_w9_responder() {
  uint8_t rx[lran::kMaxFrame];
  size_t  len       = 0;
  bool    crc_error = false;

  if (!g_radio.poll(rx, sizeof(rx), &len, &crc_error)) {
    if (!g_w9_resp_reported && g_w9_resp_last_rx != 0 &&
        static_cast<int32_t>(millis() - g_w9_resp_last_rx) >=
            static_cast<int32_t>(kW9RespIdleReportMs)) {
      w9_report_responder();
      g_w9_resp_reported = true;
    }
    delay(2);
    return;
  }

  g_w9_resp_last_rx  = millis();
  g_w9_resp_reported = false;

  if (crc_error) {
    ++g_w9_stats.crc_errors;
    g_radio.start_receive();
    return;
  }

  // The set's shape has to be read BEFORE it is accepted: once the reassembler
  // completes, the individual fragments are gone and the chunk to echo with cannot be
  // recovered. frag_chunk appears nowhere on the wire (spec 6.6.2), so the largest
  // fragment payload is the only evidence of it there is.
  lran::DecodeCtx peek;
  peek.self = kW9Responder;
  peek.mac  = nullptr;
  lran::Frame pf;
  uint16_t frag_payload = 0;
  uint8_t  frag_total   = 1;
  if (lran::decode_header(rx, len, peek, &pf) == lran::Status::Ok &&
      lran::decode_payload(rx, len, peek, &pf) == lran::Status::Ok) {
    frag_payload = static_cast<uint16_t>(pf.payload_len);
    frag_total   = pf.hdr.frag_total();
    if (frag_payload > g_w9_largest_frag) g_w9_largest_frag = frag_payload;
    if (frag_total > g_w9_frag_total) g_w9_frag_total = frag_total;
  }

  if (!w9_accept_frame(rx, len, kW9Responder, &g_w9_stats)) {
    g_radio.start_receive();
    return;
  }

  // spec 6.6 - swap src/dst, PRESERVE seq and ping_flags, echo the bytes verbatim.
  // The echo is built from the reassembled payload rather than rebuilt from `n`,
  // because echoing a regenerated pattern would pass this test no matter what the
  // link did to the bytes on the way in.
  const lran::Seq seq = g_w9_re.seq();
  memcpy(g_w9_payload, g_w9_re.data(), g_w9_re.len());
  g_w9_payload_len = g_w9_re.len();

  const lran::Header hdr = w9_header(seq, kW9Responder, kW9Initiator);
  const size_t chunk = w9_echo_chunk(g_w9_largest_frag, g_w9_frag_total);
  const uint8_t frags =
      (chunk == 0) ? 1 : lran::fragment_count(g_w9_payload_len, chunk);

  ++g_w9_stats.pings_sent;
  w9_transmit_set(hdr, g_w9_payload, g_w9_payload_len, chunk, frags);

  // The next set may be split differently - the initiator moves from run 1 to run 2
  // without telling anyone - so the inference starts clean for each one.
  g_w9_largest_frag = 0;
  g_w9_frag_total   = 1;
  g_w9_re.reset();
}

}  // namespace

void loop() {
  // R8 - the survey shares no state with the sweep and must not fall through into a
  // path that can transmit. It returns before the frame handling below ever runs.
  if (g_role == Role::Survey) {
    loop_survey();
    return;
  }

  // R9 - W9 shares the radio and nothing else. It runs the real codec against real
  // PING frames, so it must not fall through into the sweep's raw-frame handling
  // below, which would parse an LRAN frame as a bench frame and count it foreign.
  if (g_role == Role::W9Initiator) {
    loop_w9_initiator();
    return;
  }
  if (g_role == Role::W9Responder) {
    loop_w9_responder();
    return;
  }

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
      } else if (g_role == Role::Responder && bench_is_probe(f.kind)) {
        // R5 - the RESPONDER owns the position. It is the walking end and the only
        // one that knows it has moved, so the number originates here and rides out
        // in the echo. Taking it from the probe (as the R4 draft did) would have made
        // the initiator authoritative about where the operator was standing.

        // The probe names its test point, so the responder knows exactly where the
        // sweep is. Echo first, then retune if the sweep has moved on - retuning
        // before the echo would send it on a configuration the initiator has already
        // left, and the probe would be scored lost for no reason.
        // R6 - fold it into our own record BEFORE echoing. A probe we heard counts
        // even if the echo never gets out, and that case is precisely the one the
        // initiator cannot see.
        const int16_t rssi10 = to_tenths(g_radio.last_rssi_dbm());
        const int16_t snr10  = to_tenths(g_radio.last_snr_db());

        if (f.tp_index != g_resp_tp_index) {
          g_resp_tp_index = f.tp_index;
          g_resp_tp_heard = 0;
        }
        // Warmup probes are echoed - that is how the responder proves it has found
        // the configuration - but they are measurement scaffolding and are counted
        // by neither end.
        if (bench_is_counted(f.kind)) {
          g_resp_log.record_probe(g_position_id, rssi10, snr10);
          ++g_resp_tp_heard;
        }

        // Tracked, not latched: a counted probe means the far end has started
        // sweeping again, so the "you may move" state has to go away by itself. An
        // operator who walks on while a sweep is running produces a trace whose
        // position column is a lie, and nothing downstream can detect it.
        const bool was_armed = g_resp_far_end_armed;
        g_resp_far_end_armed = bench_says_armed(f.kind);
        if (g_resp_far_end_armed && !was_armed) {
          // PERSIST HERE, NOT ONLY ON THE NEXT PRG PRESS.
          //
          // The log used to be written only when the position ADVANCED, which meant
          // the LAST position of every walk was never saved: the operator walks home
          // and powers the board down without a further press, and the final
          // position's data - often the most distant one, the whole point of the walk -
          // is gone. Seen on the first real walk (2026-09-04): two positions covered,
          // one row in the log.
          //
          // The armed beacon is the right moment. It means the initiator has finished
          // the sweep for this position, so the position's tally is complete; saving
          // on the transition writes once per position rather than once per beacon.
          resp_log_save();

          // Printed on the TRANSITION, not every beacon. The walking end is normally
          // untethered and this line is for the bench and for the log; the display is
          // what the operator in the field actually reads.
          // WORDING MATTERS HERE. This said "sweep complete", which is the exact
          // substring capture.py stops a capture on - so reading the responder's log
          // while the initiator was still beaconing ended the capture on this line
          // instead of on "# responder log complete". Seen on the bench: 2 completion
          // units counted for a 2-row log. The responder does not run sweeps; saying
          // so was wrong as well as ambiguous.
          Serial.print(F("# far end ARMED - position "));
          Serial.print(g_position_id);
          Serial.println(F(" measured and saved, press PRG to move on"));
        }

        g_resp_last_heard_ms = millis();
        g_resp_ever_heard    = true;
        g_resp_last_rssi_dbm = g_radio.last_rssi_dbm();

        echo_probe(f, len);
        if (g_resp_far_end_armed) {
          // THE WALKING OPERATOR'S GO SIGNAL. This is the only indication they get
          // that the sweep for this position is finished, and it is the difference
          // between a timed guess and knowing.
          g_ui.show_sweep_done(g_position_id, g_radio.last_rssi_dbm());
        } else {
          g_ui.show_link(g_role, g_radio.last_rssi_dbm(), g_radio.last_snr_db(),
                         g_position_id, g_resp_echoes, g_resp_log.find(g_position_id)
                             ? g_resp_log.find(g_position_id)->probes_heard
                             : g_resp_echoes);
        }

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
        // R5 - the position travels in every echo, sweeping or armed. Learned
        // unconditionally, because an armed beacon's echo is the ONLY way the
        // initiator finds out the operator has moved.
        g_heard_position = f.position_id;

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
                         g_swept_position, g_stats.echoes_received,
                         g_stats.probes_sent);
          finish_probe();
        }
      }
    }
    g_radio.start_receive();
  }

  if (g_role != Role::Initiator) {
    // R5 - the press that marks a new position. Takes effect on the next echo.
    // Bench aid alongside it: 'p' on the serial console does the same thing, so the
    // walk flow can be exercised with both boards on a desk.
    bool advance = prg_edge();
    while (Serial.available() > 0) {
      const int c = Serial.read();
      if (c == 'p' || c == 'P') advance = true;
      if (c == 'd' || c == 'D') resp_log_dump();     // R6 - dump on demand
      if (c == 'x' || c == 'X') { g_resp_log.clear(); resp_log_save();
                                  Serial.println(F("# position log cleared")); }
    }
    if (advance) {
      // R6 - persist the position we are leaving before starting a new one. Sixteen
      // writes across a walk is nothing to NVS, and it means a brown-out costs at
      // most the position in progress.
      resp_log_save();
      ++g_position_id;
      g_resp_far_end_armed = false;   // the press starts a new sweep; stop saying "go"
      Serial.print(F("# position -> "));
      Serial.println(g_position_id);
      g_ui.show_armed(g_role, g_position_id, 0);

      // Jump straight to configuration 0 rather than waiting out the current dwell.
      //
      // Measured: without this the press-to-sweep-start latency was 15 s, because a
      // sweep ends on the slowest configuration and its dwell is ~17 s. Both ends
      // already know a new sweep begins on configuration 0 and that the armed
      // initiator beacons there, so there is nothing to discover - only a timer to
      // stop waiting on. The operator stands still once per position; fifteen
      // seconds of it is worth removing.
      resp_tune_to(0);
    }

    // R6 - show the silence, and keep the age ticking so the display is visibly
    // alive. Redrawn about once a second; the OLED write is not free and there is
    // nothing to see faster than that.
    {
      static uint32_t last_stale_draw = 0;
      const uint32_t  silent = g_resp_ever_heard
                                   ? (millis() - g_resp_last_heard_ms)
                                   : millis();
      if (silent > kStaleAfterMs && millis() - last_stale_draw >= 1000) {
        last_stale_draw = millis();
        g_ui.show_stale(g_role, g_position_id, silent, g_resp_last_rssi_dbm,
                        g_resp_ever_heard);
      }
    }

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

  // Bench aid: 's' forces the next sweep without waiting for a position change, and
  // 'n' advances the position locally. Both exist so the walk flow can be driven from
  // the build machine; neither is how the field test works.
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if ((c == 's' || c == 'S') && g_init_state == InitState::Armed) {
      start_sweep(g_swept_position);
    } else if (c == 'n' || c == 'N') {
      g_heard_position = static_cast<uint16_t>(g_swept_position + 1);
    }
  }

  if (g_init_state == InitState::Armed) {
    // The operator has moved and the responder has told us so.
    if (g_heard_position != g_swept_position) {
      start_sweep(g_heard_position);
      return;
    }
    if (static_cast<int32_t>(millis() - g_next_beacon_ms) >= 0) {
      g_next_beacon_ms = millis() + kArmedBeaconMs;
      send_beacon();
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
