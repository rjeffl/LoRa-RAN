// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-9 - the OLED page. Impl Plan 10.6 rule 2: an armed fault shows until it disarms itself.
//
// These tests drive the real injector and node, so "the panel shows a fault" is asserted
// against the same armed state the console reports, not against a hand-built snapshot. The
// bounded count and self-disarm themselves are BF-8's and are asserted in test_fault; here
// the question is only whether the page follows them.
//
// WHAT THIS CANNOT COVER: pixels. Whether 21 characters of ArialMT_Plain_10 fit the panel is
// the bridge's measured budget, and whether the inverted bar reads at a glance needs a board.

#include <unity.h>

#include <cstring>

#include "fault.h"
#include "identity.h"
#include "lran/lran.h"
#include "node.h"
#include "oled_page.h"
#include "refimpl_mac.h"
#include "test_key.h"

using namespace simnode;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefKdf g_kdf;
refimpl::RefMac g_mac;

uint32_t g_next_random = 0xB000;
uint32_t counting_random() { return ++g_next_random; }

class NullSink final : public Sink {
 public:
  void line(const char*) override {}
};

struct Board {
  IdentityTable ids;
  Outbox        out;
  NullSink      log;
  Node          node{&ids, &out, &g_mac, &log};
  FaultInjector faults{&ids, &out, &node, &g_mac, &log};
  Board() { ids.init(lran_test::kTestMasterKey, &g_kdf, counting_random); }

  PageLines page(uint32_t now_ms, bool radio_up = true) const {
    return build_page(take_snapshot(ids, faults, node, radio_up, now_ms));
  }

  // Drains the outbox, as the radio would, so the injector has room to fire again.
  void drain() {
    OutFrame f;
    while (out.pop(&f)) {}
  }
};

size_t row_width(const PageRow& r) {
  const size_t right = std::strlen(r.right);
  return std::strlen(r.left) + (right > 0 ? 1 + right : 0);
}

void assert_fits(const PageLines& p) {
  for (size_t i = 0; i < kPageRows; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(row_width(p.rows[i]) <= kRowBudget, p.rows[i].left);
  }
}

}  // namespace

void test_an_idle_board_lists_its_identities_uninverted() {
  Board b;
  b.ids.add(kNodeSim0, Role::Range);
  b.ids.add(kNodeSim2, Role::Health);
  const PageLines p = b.page(0);

  TEST_ASSERT_EQUAL_STRING("rx: nothing yet", p.rows[0].left);
  TEST_ASSERT_EQUAL_STRING("f0 ROLE_RANGE", p.rows[1].left);
  TEST_ASSERT_EQUAL_STRING("f2 ROLE_HEALTH", p.rows[2].left);
  TEST_ASSERT_EQUAL_STRING("", p.rows[3].left);
  for (size_t i = 0; i < kPageRows; ++i) TEST_ASSERT_FALSE(p.rows[i].invert);
}

void test_an_armed_fault_is_inverted_counts_down_and_clears_when_it_disarms() {
  Board b;
  b.ids.add(kNodeSim0, Role::Fault);
  b.ids.add(kNodeSim2, Role::Health);
  FaultRequest req;
  req.count = 3;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultResult::Ok),
                        static_cast<int>(b.faults.arm(kNodeSim0, "bad_crc", req, 0)));

  // The first injection fires on arm, so two are left.
  PageLines p = b.page(0);
  TEST_ASSERT_TRUE(p.rows[1].invert);
  TEST_ASSERT_EQUAL_STRING("f0 bad_crc", p.rows[1].left);
  TEST_ASSERT_EQUAL_STRING("2", p.rows[1].right);
  TEST_ASSERT_FALSE(p.rows[2].invert);  // only the identity it is armed on

  uint32_t now = 0;
  for (int i = 0; i < 10 && b.faults.armed(kNodeSim0) != nullptr; ++i) {
    b.drain();
    now += 10000;
    b.faults.tick(now);
  }
  TEST_ASSERT_NULL(b.faults.armed(kNodeSim0));
  p = b.page(now);
  TEST_ASSERT_FALSE(p.rows[1].invert);
  TEST_ASSERT_EQUAL_STRING("f0 ROLE_FAULT", p.rows[1].left);
  TEST_ASSERT_EQUAL_STRING("", p.rows[1].right);
}

void test_disarming_from_the_console_clears_the_row() {
  Board b;
  b.ids.add(kNodeSim0, Role::Fault);
  FaultRequest req;
  req.count = 50;
  b.faults.arm(kNodeSim0, "flood", req, 0);
  TEST_ASSERT_TRUE(b.page(0).rows[1].invert);
  TEST_ASSERT_TRUE(b.faults.disarm(kNodeSim0));
  TEST_ASSERT_FALSE(b.page(0).rows[1].invert);
}

void test_silent_shows_the_answers_it_will_still_withhold() {
  Board b;
  b.ids.add(kNodeSim0, Role::Range);
  FaultRequest req;
  req.count = 4;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultResult::Ok),
                        static_cast<int>(b.faults.arm(kNodeSim0, "silent", req, 0)));
  const PageLines p = b.page(0);
  TEST_ASSERT_TRUE(p.rows[1].invert);
  TEST_ASSERT_EQUAL_STRING("f0 silent", p.rows[1].left);
  TEST_ASSERT_EQUAL_STRING("4", p.rows[1].right);
}

void test_the_longest_fault_name_is_cut_visibly_and_the_count_survives() {
  Board b;
  b.ids.add(kNodeSim3, Role::Fault);
  FaultRequest req;
  req.count = 999;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(FaultResult::Ok),
                        static_cast<int>(b.faults.arm(kNodeSim3, "single_frame_interleave", req, 0)));
  const PageLines p = b.page(0);
  TEST_ASSERT_TRUE(p.rows[1].invert);
  TEST_ASSERT_EQUAL_STRING("998", p.rows[1].right);
  TEST_ASSERT_EQUAL_UINT(kRowBudget, row_width(p.rows[1]));
  TEST_ASSERT_EQUAL_CHAR('~', p.rows[1].left[std::strlen(p.rows[1].left) - 1]);
  TEST_ASSERT_EQUAL_INT(0, std::strncmp("f3 single_frame", p.rows[1].left, 15));
}

void test_every_catalogue_name_at_the_largest_count_fits() {
  PageSnapshot s;
  s.radio_up = true;
  for (size_t i = 0; i < kFaultCatalogueLen; ++i) {
    s.ids[0].used       = true;
    s.ids[0].id         = kNodeSim3;
    s.ids[0].fault      = kFaultCatalogue[i].name;
    s.ids[0].fault_left = 0xFFFF;
    assert_fits(build_page(s));
  }
}

void test_a_disabled_identity_says_off_and_every_role_fits() {
  PageSnapshot s;
  s.radio_up = true;
  const Role roles[] = {Role::Range, Role::Health, Role::GateLink, Role::Fault};
  for (size_t i = 0; i < kMaxIdentities; ++i) {
    s.ids[i].used    = true;
    s.ids[i].id      = static_cast<NodeId>(kNodeSim0 + i);
    s.ids[i].role    = roles[i];
    s.ids[i].enabled = false;
  }
  const PageLines p = build_page(s);
  TEST_ASSERT_EQUAL_STRING("f2 ROLE_GATELINK", p.rows[3].left);
  TEST_ASSERT_EQUAL_STRING("off", p.rows[3].right);
  assert_fits(p);
}

void test_a_received_ping_shows_on_the_top_row_with_its_age() {
  Board a;  // sender
  Board b;  // this board
  a.ids.add(kNodeSim1, Role::Range);
  b.ids.add(kNodeSim0, Role::Range);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PingResult::Ok),
                        static_cast<int>(a.node.ping(kNodeSim1, 8, true, 0, kNodeSim0, 0)));
  OutFrame f;
  TEST_ASSERT_TRUE(a.out.pop(&f));
  b.node.on_rx(f.bytes, f.len, -42, 75, 1000);

  const PageLines p = b.page(13500);
  TEST_ASSERT_EQUAL_STRING("f1>f0 PING -42", p.rows[0].left);
  TEST_ASSERT_EQUAL_STRING("12s", p.rows[0].right);
}

void test_a_frame_no_identity_accepts_is_a_drop_with_its_length() {
  Board a;
  Board b;
  a.ids.add(kNodeSim1, Role::Range);
  b.ids.add(kNodeSim0, Role::Range);
  a.node.ping(kNodeSim1, 8, true, 0, kNodeSim2, 0);  // for an identity not on board b
  OutFrame f;
  TEST_ASSERT_TRUE(a.out.pop(&f));
  b.node.on_rx(f.bytes, f.len, -120, -50, 0);

  char expect[24];
  std::snprintf(expect, sizeof(expect), "%uB -120 drop", static_cast<unsigned>(f.len));
  TEST_ASSERT_EQUAL_STRING(expect, b.page(0).rows[0].left);
}

void test_a_phy_crc_error_has_no_rssi_to_show() {
  Board b;
  b.ids.add(kNodeSim0, Role::Range);
  b.node.on_phy_crc_error(0);
  TEST_ASSERT_EQUAL_STRING("phy crc error", b.page(61000).rows[0].left);
  TEST_ASSERT_EQUAL_STRING("1m", b.page(61000).rows[0].right);
}

void test_radio_down_outranks_the_last_frame() {
  Board b;
  b.ids.add(kNodeSim0, Role::Range);
  b.node.on_phy_crc_error(0);
  const PageLines p = b.page(0, false);
  TEST_ASSERT_EQUAL_STRING("RADIO DOWN", p.rows[0].left);
  TEST_ASSERT_TRUE(p.rows[0].invert);
}

void test_a_board_with_no_identities_says_so() {
  Board b;
  TEST_ASSERT_EQUAL_STRING("no identities", b.page(0).rows[1].left);
}

void test_ages_step_through_their_units() {
  char out[8];
  format_age(59999, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("59s", out);
  format_age(60000, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("1m", out);
  format_age(3600000UL, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("1h", out);
  format_age(99UL * 3600000UL + 3599999UL, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("99h", out);
  format_age(100UL * 3600000UL, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(">99h", out);
  format_age(UINT32_MAX, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(">99h", out);
}

void test_the_top_row_fits_at_its_widest() {
  PageSnapshot s;
  s.radio_up      = true;
  s.now_ms        = UINT32_MAX;
  s.last.kind     = RxKind::Frame;
  s.last.rssi_dbm = -32767;
  s.last.src      = 0xFF;
  s.last.dst      = 0xFF;
  s.last.type     = MsgType::ConfigAck;
  assert_fits(build_page(s));
  TEST_ASSERT_EQUAL_STRING("ff>ff CACK ?", build_page(s).rows[0].left);
  s.last.rssi_dbm = -199;  // the widest value shown as a number
  assert_fits(build_page(s));
  s.last.kind = RxKind::HeaderDiscard;
  s.last.len  = 255;
  assert_fits(build_page(s));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_an_idle_board_lists_its_identities_uninverted);
  RUN_TEST(test_an_armed_fault_is_inverted_counts_down_and_clears_when_it_disarms);
  RUN_TEST(test_disarming_from_the_console_clears_the_row);
  RUN_TEST(test_silent_shows_the_answers_it_will_still_withhold);
  RUN_TEST(test_the_longest_fault_name_is_cut_visibly_and_the_count_survives);
  RUN_TEST(test_every_catalogue_name_at_the_largest_count_fits);
  RUN_TEST(test_a_disabled_identity_says_off_and_every_role_fits);
  RUN_TEST(test_a_received_ping_shows_on_the_top_row_with_its_age);
  RUN_TEST(test_a_frame_no_identity_accepts_is_a_drop_with_its_length);
  RUN_TEST(test_a_phy_crc_error_has_no_rssi_to_show);
  RUN_TEST(test_radio_down_outranks_the_last_frame);
  RUN_TEST(test_a_board_with_no_identities_says_so);
  RUN_TEST(test_ages_step_through_their_units);
  RUN_TEST(test_the_top_row_fits_at_its_widest);
  return UNITY_END();
}
