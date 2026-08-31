// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "ui_oled.h"

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
  const char* tag = (r == Role::Initiator) ? "INIT" : "RESP";

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
  std::snprintf(buf, sizeof(buf), "PRG = RESPONDER  %lus",
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

void Ui::show_link(Role r, float rssi_dbm, float snr_db, uint16_t position_id,
                   uint32_t ok_count, uint32_t total_count) {
  if (!ok_) return;

  char rssi[16];
  char snr[24];
  char foot[32];

  // RSSI large: it is the number being read at arm's length on a fence post (R6).
  std::snprintf(rssi, sizeof(rssi), "%d", static_cast<int>(rssi_dbm));
  std::snprintf(snr, sizeof(snr), "SNR %.1f", static_cast<double>(snr_db));
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
