// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "ui_oled.h"

#include "role.h"
#include "sentinels.h"

#include <Arduino.h>
#include <Wire.h>

#include <cstdio>

namespace rangetest {
namespace {

constexpr int16_t kWidth = 128;

// ArialMT_Plain_10 advances 13 px, _16 about 19, _24 about 28.
constexpr int16_t kBadgeW = 30;
constexpr int16_t kBadgeH = 13;

constexpr int16_t kRowTop  = 0;
constexpr int16_t kRowBig  = 14;
constexpr int16_t kRowFoot = 51;

}  // namespace

Ui::Ui() : display_(kOledAddr, kPinOledSda, kPinOledScl) {}

void Ui::draw_role_badge(Role r) {
  // Four characters so the tag is a fixed width and the eye finds it in the same
  // place on both boards.
  const char* tag = "RESP";
  if (r == Role::Initiator) tag = "INIT";
  else if (r == Role::Survey) tag = "SURV";

  display_.setFont(ArialMT_Plain_10);
  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.setColor(WHITE);
  display_.fillRect(0, 0, kBadgeW, kBadgeH);
  display_.setColor(BLACK);
  display_.drawString(3, 0, tag);
  display_.setColor(WHITE);  // restore, or everything drawn after this is invisible
}

bool Ui::begin() {
  // Order matters. The OLED is powered THROUGH Vext, not directly - skip this and
  // the panel stays dark and the I2C scan finds nothing, which looks exactly like a
  // dead panel or a wrong address. Confirmed on this board in wattcycle-reader.
  pinMode(kPinVext, OUTPUT);
  digitalWrite(kPinVext, LOW);  // LOW = Vext ON
  delay(100);

  pinMode(kPinOledRst, OUTPUT);
  digitalWrite(kPinOledRst, LOW);
  delay(20);
  digitalWrite(kPinOledRst, HIGH);
  delay(50);

  Wire.begin(kPinOledSda, kPinOledScl);

  // Probe before init so a missing panel is reported rather than guessed at.
  Wire.beginTransmission(kOledAddr);
  if (Wire.endTransmission() != 0) return false;

  display_.init();
  display_.flipScreenVertically();  // required on the V3 - verified on hardware
  display_.setContrast(255);
  ok_ = true;
  return true;
}

void Ui::show_message(const char* line1, const char* line2) {
  if (!ok_) return;
  display_.clear();
  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.setFont(ArialMT_Plain_10);
  display_.drawString(0, kRowTop, line1);
  display_.setFont(ArialMT_Plain_16);
  display_.drawString(0, kRowBig, line2);
  display_.display();
}

void Ui::show_role_prompt(uint32_t ms_remaining) {
  if (!ok_) return;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "tap=RESP hold=SURV %lus",
                static_cast<unsigned long>((ms_remaining + 999) / 1000));

  display_.clear();
  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.setFont(ArialMT_Plain_10);
  display_.drawString(0, kRowTop, "LRAN range test");
  display_.setFont(ArialMT_Plain_16);
  display_.drawString(0, kRowBig, "Select role");
  display_.setFont(ArialMT_Plain_10);
  display_.drawString(0, kRowFoot, buf);
  display_.display();
}

void Ui::show_role_hold(uint32_t held_ms, bool survey) {
  if (!ok_) return;

  char foot[32];
  if (survey) {
    std::snprintf(foot, sizeof(foot), "release for SURVEY");
  } else {
    const uint32_t left = (kPrgSurveyHoldMs > held_ms)
                              ? (kPrgSurveyHoldMs - held_ms) : 0;
    std::snprintf(foot, sizeof(foot), "hold %lu.%lus for SURVEY",
                  static_cast<unsigned long>(left / 1000),
                  static_cast<unsigned long>((left % 1000) / 100));
  }

  display_.clear();
  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.setFont(ArialMT_Plain_10);
  display_.drawString(0, kRowTop, "LRAN range test");

  // The role name is the big element, and it CHANGES under the thumb. That change is
  // the whole signal - the operator is watching for the word to flip, not counting.
  display_.setFont(ArialMT_Plain_16);
  display_.drawString(0, kRowBig, survey ? "SURVEY" : "RESPONDER");

  display_.setFont(ArialMT_Plain_10);
  display_.drawString(0, kRowFoot, foot);
  display_.display();
}

void Ui::show_role(Role r, const char* board_name) {
  if (!ok_) return;
  display_.clear();
  draw_role_badge(r);
  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.setFont(ArialMT_Plain_10);
  display_.drawString(kBadgeW + 4, kRowTop, "LRAN range test");

  // ArialMT_Plain_16, NOT _24. Both role words are nine characters and at _24 the
  // last one runs off a 128 px panel - observed on hardware as "RESPONDE". A role
  // label that silently drops a character is worse than a smaller one, and the
  // inverted badge above already carries the at-a-glance version.
  display_.setFont(ArialMT_Plain_16);
  display_.drawString(0, kRowBig, to_string(r));

  display_.setFont(ArialMT_Plain_10);
  display_.drawString(0, kRowFoot, (board_name != nullptr) ? board_name : "");
  display_.display();
}

void Ui::show_armed(Role r, uint16_t position_id, uint16_t sweeps_done) {
  if (!ok_) return;

  char pos[16];
  char foot[32];
  std::snprintf(pos, sizeof(pos), "P%u", static_cast<unsigned>(position_id));
  std::snprintf(foot, sizeof(foot), "%u sweep%s done",
                static_cast<unsigned>(sweeps_done), sweeps_done == 1 ? "" : "s");

  display_.clear();
  draw_role_badge(r);

  display_.setFont(ArialMT_Plain_10);
  display_.setTextAlignment(TEXT_ALIGN_RIGHT);
  display_.drawString(kWidth, kRowTop, "PRG = next");

  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.setFont(ArialMT_Plain_24);
  display_.drawString(0, kRowBig, pos);

  display_.setFont(ArialMT_Plain_10);
  display_.drawString(0, kRowFoot, foot);
  display_.display();
}

void Ui::show_survey(const char* site_name, uint32_t passes, uint32_t freq_hz_now,
                     uint32_t loudest_freq_hz, int16_t loudest_peak_dbm10,
                     bool saved) {
  if (!ok_) return;

  char big[20];
  char top[24];
  char foot[32];

  // MHz to one decimal, assembled from integer kHz. Same reasoning as the CSV: the
  // firmware carries no floats through a measurement path, and 902.0 printed from an
  // integer cannot drift.
  const uint32_t khz = freq_hz_now / 1000UL;
  std::snprintf(top, sizeof(top), "%lu.%lu", 
                static_cast<unsigned long>(khz / 1000UL),
                static_cast<unsigned long>((khz % 1000UL) / 100UL));

  // The pass count is the big number because it is the one that proves the board is
  // alive from arm's length. A frozen scan and a quiet band look identical otherwise.
  std::snprintf(big, sizeof(big), "%lu", static_cast<unsigned long>(passes));

  if (loudest_peak_dbm10 == kI16NotAvailable) {
    std::snprintf(foot, sizeof(foot), "%s", saved ? "SAVED - no data" : "scanning...");
  } else {
    const uint32_t lkhz = loudest_freq_hz / 1000UL;
    std::snprintf(foot, sizeof(foot), "%s%lu.%lu %d dBm",
                  saved ? "SAVED " : "pk ",
                  static_cast<unsigned long>(lkhz / 1000UL),
                  static_cast<unsigned long>((lkhz % 1000UL) / 100UL),
                  static_cast<int>(loudest_peak_dbm10 / 10));
  }

  display_.clear();
  draw_role_badge(Role::Survey);

  display_.setFont(ArialMT_Plain_10);
  display_.setTextAlignment(TEXT_ALIGN_RIGHT);
  display_.drawString(kWidth, kRowTop, top);

  // The site the run will be FILED UNDER. Wrong site is the one error that survives
  // the walk home intact - right numbers, wrong place on the property - so it is on
  // the display the whole time and not only at the moment of the press.
  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.drawString(kBadgeW + 4, kRowTop, site_name);

  display_.setFont(ArialMT_Plain_24);
  display_.drawString(0, kRowBig, big);

  display_.setFont(ArialMT_Plain_10);
  display_.drawString(0, kRowFoot, foot);
  display_.display();
}

void Ui::show_survey_held(const char* site_name, size_t site_index, size_t site_count,
                          bool saved) {
  if (!ok_) return;

  char top[24];
  char foot[32];
  std::snprintf(top, sizeof(top), "site %u/%u",
                static_cast<unsigned>(site_index + 1),
                static_cast<unsigned>(site_count));
  // The press is the only thing the operator has to do, so it is the footer. `saved`
  // distinguishes "held after storing the last site" from "held, nothing stored yet",
  // which is the difference between a campaign in progress and one that lost a run.
  std::snprintf(foot, sizeof(foot), "%sPRG = start dwell",
                saved ? "saved. " : "");

  display_.clear();
  draw_role_badge(Role::Survey);

  display_.setColor(WHITE);
  display_.fillRect(0, kRowBig - 2, kWidth, 32);
  display_.setColor(BLACK);
  display_.setFont(ArialMT_Plain_24);
  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.drawString(2, kRowBig, "HELD");
  display_.setFont(ArialMT_Plain_10);
  display_.setTextAlignment(TEXT_ALIGN_RIGHT);
  // Inside the bar: the site the next dwell will be filed under. Wrong site is the
  // error that survives the walk home intact.
  display_.drawString(kWidth - 2, kRowBig + 12, site_name);
  display_.setColor(WHITE);   // restore, or everything after this is invisible

  display_.setFont(ArialMT_Plain_10);
  display_.setTextAlignment(TEXT_ALIGN_RIGHT);
  display_.drawString(kWidth, kRowTop, top);

  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.drawString(0, kRowFoot, foot);
  display_.display();
}

void Ui::show_sweep_done(uint16_t position_id, float last_rssi_dbm) {
  if (!ok_) return;

  char foot[32];
  char pos[16];
  std::snprintf(pos, sizeof(pos), "P%u", static_cast<unsigned>(position_id));
  std::snprintf(foot, sizeof(foot), "PRG = next  %d dBm",
                static_cast<int>(last_rssi_dbm));

  display_.clear();
  draw_role_badge(Role::Responder);

  // INVERTED BAR, not just different text. The operator is glancing at a hand-shaded
  // panel in sunlight, at arm's length, having stood still for seven minutes - the
  // difference between "still sweeping" and "you may move" has to survive a glance
  // that does not read any words at all.
  display_.setColor(WHITE);
  display_.fillRect(0, kRowBig - 2, kWidth, 32);
  display_.setColor(BLACK);
  display_.setFont(ArialMT_Plain_24);
  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.drawString(2, kRowBig, "DONE");
  display_.setFont(ArialMT_Plain_16);
  display_.setTextAlignment(TEXT_ALIGN_RIGHT);
  display_.drawString(kWidth - 2, kRowBig + 6, pos);
  display_.setColor(WHITE);   // restore, or everything after this is invisible

  display_.setFont(ArialMT_Plain_10);
  display_.setTextAlignment(TEXT_ALIGN_RIGHT);
  display_.drawString(kWidth, kRowTop, "sweep complete");

  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.drawString(0, kRowFoot, foot);
  display_.display();
}

void Ui::show_stale(Role r, uint16_t position_id, uint32_t silent_ms,
                    float last_rssi_dbm, bool ever_heard) {
  if (!ok_) return;

  char age[20];
  char foot[32];
  const uint32_t s_whole = silent_ms / 1000;
  if (s_whole < 60) {
    std::snprintf(age, sizeof(age), "%lus", static_cast<unsigned long>(s_whole));
  } else {
    std::snprintf(age, sizeof(age), "%lum%02lus",
                  static_cast<unsigned long>(s_whole / 60),
                  static_cast<unsigned long>(s_whole % 60));
  }

  if (ever_heard) {
    std::snprintf(foot, sizeof(foot), "P%u  last %d dBm",
                  static_cast<unsigned>(position_id),
                  static_cast<int>(last_rssi_dbm));
  } else {
    // Never heard anything at all - a different situation from having lost contact,
    // and worth saying so rather than showing a last reading that does not exist.
    std::snprintf(foot, sizeof(foot), "P%u  no contact yet",
                  static_cast<unsigned>(position_id));
  }

  display_.clear();
  draw_role_badge(r);

  display_.setFont(ArialMT_Plain_10);
  display_.setTextAlignment(TEXT_ALIGN_RIGHT);
  display_.drawString(kWidth, kRowTop, "SILENT");

  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.setFont(ArialMT_Plain_24);
  display_.drawString(0, kRowBig, age);

  display_.setFont(ArialMT_Plain_10);
  display_.drawString(0, kRowFoot, foot);
  display_.display();
}

void Ui::show_link(Role r, float rssi_dbm, float snr_db, uint16_t position_id,
                   uint32_t ok_count, uint32_t total_count) {
  if (!ok_) return;

  char rssi[16];
  char snr[24];
  char foot[32];

  // RSSI large: it is the number being read at arm's length on a fence post (R6).
  std::snprintf(rssi, sizeof(rssi), "%d", static_cast<int>(rssi_dbm));
  // Units spelled out. RSSI carries a "dBm" label beside it, and an SNR figure with
  // no unit next to one that has one reads as an oversight - which is what it was.
  //
  // Width, since this panel has already truncated two strings: right-aligned at 128
  // px, "SNR -20.5 dB" is the widest realistic case at roughly 66 px, so it starts
  // near x=62 and clears the 30 px role badge with room to spare.
  std::snprintf(snr, sizeof(snr), "SNR %.1f dB", static_cast<double>(snr_db));
  std::snprintf(foot, sizeof(foot), "P%u  %lu/%lu",
                static_cast<unsigned>(position_id),
                static_cast<unsigned long>(ok_count),
                static_cast<unsigned long>(total_count));

  display_.clear();
  draw_role_badge(r);

  display_.setFont(ArialMT_Plain_10);
  display_.setTextAlignment(TEXT_ALIGN_RIGHT);
  display_.drawString(kWidth, kRowTop, snr);

  display_.setTextAlignment(TEXT_ALIGN_LEFT);
  display_.setFont(ArialMT_Plain_24);
  display_.drawString(0, kRowBig, rssi);
  display_.setFont(ArialMT_Plain_10);
  display_.drawString(58, kRowBig + 12, "dBm");

  display_.drawString(0, kRowFoot, foot);
  display_.display();
}

}  // namespace rangetest
