// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// ChargeInhibit: low-temperature charge inhibition by the BMS path (PRD R-6.4a) and by
// VE.Direct alone (R-6.4b), and the STATUS and EVENT each change pushes (spec 7.2.6 bit 3,
// 8.7, 8.9). GL5.

#include <unity.h>

#include "charge_inhibit.h"

using gatelink::BmsSnapshot;
using gatelink::ChargeInhibit;
using gatelink::ChargeParams;
using gatelink::ChargeSource;
using gatelink::EventIds;
using gatelink::GateContext;
using gatelink::MpptSnapshot;
using gatelink::Push;
using gatelink::PushList;

void setUp() {}
void tearDown() {}

namespace {

constexpr uint32_t kConfirmMs = 300000;  // charge_inhibit_confirm_s's default

// The MPPT in bulk on a sunny day with the pack taking nothing: R-6.4b's condition.
MpptSnapshot blocked_mppt(uint16_t batt_mv = 13300) {
  MpptSnapshot m;
  m.batt_mv      = batt_mv;
  m.batt_ma      = 0;
  m.pv_w         = 40;
  m.charge_state = 3;  // bulk
  m.mppt_flags   = 0;
  return m;
}

// A fresh BMS read of four sensors, the coldest `coldest_dc`.
BmsSnapshot bms_read(int16_t coldest_dc, int32_t current_ma, uint32_t read_ms) {
  BmsSnapshot b;
  b.have              = true;
  b.read_ms           = read_ms;
  b.data.valid        = true;
  b.data.temp_count   = 4;
  b.data.temp_dc[0]   = 15;
  b.data.temp_dc[1]   = coldest_dc;
  b.data.temp_dc[2]   = 12;
  b.data.temp_dc[3]   = 20;
  b.data.current_ma   = current_ma;
  return b;
}

struct Rig {
  ChargeInhibit c;
  ChargeParams  p;
  GateContext   g;
  EventIds      ids;
  PushList      out;

  bool step(const MpptSnapshot& m, const BmsSnapshot& b, uint32_t now) {
    out = PushList{};
    return c.update(m, b, now, p, g, &ids, &out);
  }
};

}  // namespace

// R-6.4a - a cold pack that is not charging is inhibited, by the BMS path, and the onset
// pushes a STATUS and an EVENT.
void test_bms_cold_and_not_charging_is_inhibited() {
  Rig r;
  TEST_ASSERT_TRUE(r.step(MpptSnapshot{}, bms_read(-25, 0, 1000), 1000));
  TEST_ASSERT_TRUE(r.c.inhibited());
  TEST_ASSERT_EQUAL(ChargeSource::Bms, r.c.source());
  TEST_ASSERT_EQUAL_UINT32(2, r.out.size());
  TEST_ASSERT_EQUAL(Push::Kind::Status, r.out[0].kind);
  TEST_ASSERT_EQUAL(lran::StatusReason::ChargeInhibited, r.out[0].reason);
  TEST_ASSERT_EQUAL(Push::Kind::Event, r.out[1].kind);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lran::EventType::ChargeInhibited),
                          r.out[1].event.event_type);
  TEST_ASSERT_EQUAL_UINT32(1, r.out[1].event.event_id);
}

// The threshold is "at or below", and in whole degrees against 0.1 C.
void test_bms_threshold_is_inclusive() {
  Rig r;
  TEST_ASSERT_TRUE(r.step(MpptSnapshot{}, bms_read(0, 0, 1000), 1000));
  Rig warm;
  TEST_ASSERT_FALSE(warm.step(MpptSnapshot{}, bms_read(1, 0, 1000), 1000));
  warm.p.temp_c = -5;
  TEST_ASSERT_FALSE(warm.step(MpptSnapshot{}, bms_read(-49, 0, 2000), 2000));
  TEST_ASSERT_TRUE(warm.step(MpptSnapshot{}, bms_read(-50, 0, 3000), 3000));
}

// A cold pack that is charging is not inhibited: the BMS let the charge through.
void test_bms_cold_but_charging_is_not_inhibited() {
  Rig r;
  TEST_ASSERT_FALSE(r.step(blocked_mppt(), bms_read(-25, 1800, 1000), 1000));
  TEST_ASSERT_FALSE(r.c.inhibited());
  TEST_ASSERT_EQUAL(ChargeSource::Bms, r.c.path());
}

// Warming clears it, with a STATUS and no EVENT.
void test_bms_warming_clears_with_a_status_alone() {
  Rig r;
  r.step(MpptSnapshot{}, bms_read(-25, 0, 1000), 1000);
  TEST_ASSERT_TRUE(r.step(MpptSnapshot{}, bms_read(35, 0, 301000), 301000));
  TEST_ASSERT_FALSE(r.c.inhibited());
  TEST_ASSERT_EQUAL(ChargeSource::None, r.c.source());
  TEST_ASSERT_EQUAL_UINT32(1, r.out.size());
  TEST_ASSERT_EQUAL(Push::Kind::Status, r.out[0].kind);
}

// A fresh warm read outranks the VE.Direct inference: R-6.4b is for BLE unavailable.
void test_fresh_bms_outranks_vedirect() {
  Rig r;
  for (uint32_t t = 0; t <= kConfirmMs + 1000; t += 1000) {
    r.step(blocked_mppt(), bms_read(150, 0, t), t);
  }
  TEST_ASSERT_FALSE(r.c.inhibited());
  TEST_ASSERT_EQUAL(ChargeSource::Bms, r.c.path());
}

// R-6.4b - with no BMS read, the condition held for charge_inhibit_confirm_s infers it.
void test_vedirect_infers_after_confirm() {
  Rig r;
  const BmsSnapshot none;
  uint32_t t = 0;
  for (; t < kConfirmMs; t += 1000) {
    TEST_ASSERT_FALSE(r.step(blocked_mppt(), none, t));
  }
  TEST_ASSERT_EQUAL(ChargeSource::VeDirect, r.c.path());
  TEST_ASSERT_TRUE(r.step(blocked_mppt(), none, t));
  TEST_ASSERT_TRUE(r.c.inhibited());
  TEST_ASSERT_EQUAL(ChargeSource::VeDirect, r.c.source());
  TEST_ASSERT_EQUAL_UINT32(2, r.out.size());
}

// A BMS read older than three polls hands the decision to VE.Direct.
void test_stale_bms_hands_over_to_vedirect() {
  Rig r;
  const BmsSnapshot old = bms_read(150, 0, 0);
  r.step(blocked_mppt(), old, 3 * 300000);
  TEST_ASSERT_EQUAL(ChargeSource::Bms, r.c.path());
  r.step(blocked_mppt(), old, 3 * 300000 + 1);
  TEST_ASSERT_EQUAL(ChargeSource::VeDirect, r.c.path());
}

// Each part of R-6.4b's condition is needed.
void test_vedirect_needs_every_part() {
  const BmsSnapshot none;
  MpptSnapshot m[5] = {blocked_mppt(), blocked_mppt(), blocked_mppt(), blocked_mppt(),
                       blocked_mppt()};
  m[0].pv_w         = 0;                         // no PV
  m[1].charge_state = 5;                         // float
  m[2].batt_ma      = 150;                       // charging past the band
  m[3].batt_ma      = lran::kI16NotAvailable;    // no current reading
  m[4].mppt_flags   = gatelink::kMpptFlagStale;  // a dead link
  for (const MpptSnapshot& one : m) {
    Rig r;
    for (uint32_t t = 0; t <= kConfirmMs + 5000; t += 1000) r.step(one, none, t);
    TEST_ASSERT_FALSE(r.c.inhibited());
  }
}

// A voltage rising past kChargeInhibitRiseMv is charge going in, so the clock restarts.
void test_rising_voltage_restarts_the_clock() {
  Rig r;
  const BmsSnapshot none;
  uint32_t t = 0;
  for (; t < kConfirmMs - 1000; t += 1000) r.step(blocked_mppt(13300), none, t);
  r.step(blocked_mppt(13321), none, t);  // 21 mV over, past the 20 mV default
  // The pass after the rise starts the condition again, from the new voltage.
  t += 1000;
  for (uint32_t end = t + kConfirmMs; t < end; t += 1000) {
    r.step(blocked_mppt(13321), none, t);
  }
  TEST_ASSERT_FALSE(r.c.inhibited());
  r.step(blocked_mppt(13321), none, t);
  TEST_ASSERT_TRUE(r.c.inhibited());
}

// A rise within the step does not restart it.
void test_a_small_rise_keeps_the_clock() {
  Rig r;
  const BmsSnapshot none;
  uint32_t t = 0;
  for (; t <= kConfirmMs; t += 1000) r.step(blocked_mppt(t < 1000 ? 13300 : 13320), none, t);
  TEST_ASSERT_TRUE(r.c.inhibited());
}

// The inference clears as it sets: a minute of cloud does not flap it.
void test_vedirect_clears_after_confirm() {
  Rig r;
  const BmsSnapshot none;
  uint32_t t = 0;
  for (; t <= kConfirmMs; t += 1000) r.step(blocked_mppt(), none, t);
  TEST_ASSERT_TRUE(r.c.inhibited());

  MpptSnapshot cloud = blocked_mppt();
  cloud.pv_w = 0;
  for (uint32_t end = t + 60000; t < end; t += 1000) r.step(cloud, none, t);
  r.step(blocked_mppt(), none, t);
  TEST_ASSERT_TRUE(r.c.inhibited());

  MpptSnapshot charging = blocked_mppt();
  charging.batt_ma = 2000;
  t += 1000;
  const uint32_t began = t;
  bool cleared = false;
  for (; !cleared && t <= began + kConfirmMs; t += 1000) cleared = r.step(charging, none, t);
  TEST_ASSERT_TRUE(cleared);
  TEST_ASSERT_EQUAL_UINT32(began + kConfirmMs, t - 1000);
  TEST_ASSERT_EQUAL_UINT32(1, r.out.size());
}

// With neither the MPPT nor the BMS, nothing decides and nothing is claimed.
void test_no_source_claims_nothing() {
  Rig r;
  MpptSnapshot stale;
  for (uint32_t t = 0; t <= kConfirmMs + 1000; t += 1000) r.step(stale, BmsSnapshot{}, t);
  TEST_ASSERT_FALSE(r.c.inhibited());
  TEST_ASSERT_EQUAL(ChargeSource::None, r.c.path());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bms_cold_and_not_charging_is_inhibited);
  RUN_TEST(test_bms_threshold_is_inclusive);
  RUN_TEST(test_bms_cold_but_charging_is_not_inhibited);
  RUN_TEST(test_bms_warming_clears_with_a_status_alone);
  RUN_TEST(test_fresh_bms_outranks_vedirect);
  RUN_TEST(test_vedirect_infers_after_confirm);
  RUN_TEST(test_stale_bms_hands_over_to_vedirect);
  RUN_TEST(test_vedirect_needs_every_part);
  RUN_TEST(test_rising_voltage_restarts_the_clock);
  RUN_TEST(test_a_small_rise_keeps_the_clock);
  RUN_TEST(test_vedirect_clears_after_confirm);
  RUN_TEST(test_no_source_claims_nothing);
  return UNITY_END();
}
