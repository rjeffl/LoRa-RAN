// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-2, BF-3 - hardware profiles and the identity table (Impl Plan 10.3, 10.8.1).
//
// KEYS ARE CHECKED AGAINST THE W4 VECTORS. The simnode and the bridge derive node keys
// independently; both agreeing with tools/vectors' generator is what makes them agree with
// each other, and with a real node flashed from the same derivation.

#include <unity.h>

#include "identity.h"
#include "lran/link/radio_config.h"
#include "lran/lran.h"
#include "profiles.h"
#include "refimpl_mac.h"
#include "test_key.h"
#include "vectors_data.h"

using namespace simnode;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefKdf g_kdf;

uint32_t g_next_random = 0x1000;
uint32_t counting_random() { return ++g_next_random; }

uint32_t zero_random() { return 0; }

void init(IdentityTable& t) { t.init(lran_test::kTestMasterKey, &g_kdf, counting_random); }

}  // namespace

// ---------------------------------------------------------------------------
// profiles.h - Impl Plan 10.8.1, value for value
// ---------------------------------------------------------------------------

void test_heltec_profile_matches_impl_plan_10_8_1() {
  TEST_ASSERT_EQUAL_INT8(8, kHeltecV3Radio.nss);
  TEST_ASSERT_EQUAL_INT8(12, kHeltecV3Radio.rst);
  TEST_ASSERT_EQUAL_INT8(13, kHeltecV3Radio.busy);
  TEST_ASSERT_EQUAL_INT8(14, kHeltecV3Radio.dio1);
  TEST_ASSERT_EQUAL_INT8(9, kHeltecV3Radio.sck);
  TEST_ASSERT_EQUAL_INT8(11, kHeltecV3Radio.miso);
  TEST_ASSERT_EQUAL_INT8(10, kHeltecV3Radio.mosi);
  TEST_ASSERT_EQUAL_INT8(kPinNone, kHeltecV3Radio.rf_sw);
  TEST_ASSERT_EQUAL_UINT16(1800, kHeltecV3Radio.tcxo_mv);
  TEST_ASSERT_TRUE(kHeltecV3Radio.dio2_as_rf_switch);
}

// The Kit, p-5982 - not the header board. rf_sw is a real pin AND DIO2 drives the switch.
void test_xiao_wio_kit_profile_matches_impl_plan_10_8_1() {
  TEST_ASSERT_EQUAL_INT8(41, kXiaoWioKitRadio.nss);
  TEST_ASSERT_EQUAL_INT8(42, kXiaoWioKitRadio.rst);
  TEST_ASSERT_EQUAL_INT8(40, kXiaoWioKitRadio.busy);
  TEST_ASSERT_EQUAL_INT8(39, kXiaoWioKitRadio.dio1);
  TEST_ASSERT_EQUAL_INT8(7, kXiaoWioKitRadio.sck);
  TEST_ASSERT_EQUAL_INT8(8, kXiaoWioKitRadio.miso);
  TEST_ASSERT_EQUAL_INT8(9, kXiaoWioKitRadio.mosi);
  TEST_ASSERT_EQUAL_INT8(38, kXiaoWioKitRadio.rf_sw);
  TEST_ASSERT_EQUAL_UINT16(1800, kXiaoWioKitRadio.tcxo_mv);
  TEST_ASSERT_TRUE(kXiaoWioKitRadio.dio2_as_rf_switch);
}

// The simnode transmits on the bridge's channel: one kPhy, from lib/lran-link.
void test_the_phy_is_the_d1_working_point_within_d33() {
  TEST_ASSERT_EQUAL_UINT32(917400000u, link::kPhy.freq_hz);
  TEST_ASSERT_EQUAL_UINT8(9, link::kPhy.sf);
  TEST_ASSERT_EQUAL_INT8(-4, link::kPhy.conducted_dbm);
  TEST_ASSERT_TRUE(link::within_eirp_ceiling(link::kPhy));

  link::PhyConfig hot = link::kPhy;
  hot.conducted_dbm   = -3;
  TEST_ASSERT_FALSE(link::within_eirp_ceiling(hot));
}

// ---------------------------------------------------------------------------
// Roles
// ---------------------------------------------------------------------------

void test_role_tokens_are_exact() {
  Role r;
  TEST_ASSERT_TRUE(parse_role("ROLE_RANGE", &r));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Role::Range), static_cast<int>(r));
  TEST_ASSERT_TRUE(parse_role("ROLE_GATELINK", &r));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Role::GateLink), static_cast<int>(r));
  TEST_ASSERT_FALSE(parse_role("role_range", &r));
  TEST_ASSERT_FALSE(parse_role("RANGE", &r));
  TEST_ASSERT_EQUAL_STRING("ROLE_HEALTH", role_name(Role::Health));
  TEST_ASSERT_EQUAL_STRING("ROLE_FAULT", role_name(Role::Fault));
}

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

void test_only_the_four_bench_addresses_are_accepted() {
  IdentityTable t;
  init(t);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AddResult::BadId), static_cast<int>(t.add(0x01, Role::Range)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AddResult::BadId), static_cast<int>(t.add(0xF4, Role::Range)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AddResult::BadId), static_cast<int>(t.add(0x00, Role::Range)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AddResult::Ok), static_cast<int>(t.add(0xF3, Role::Range)));
}

void test_duplicates_are_refused_and_the_table_holds_four() {
  IdentityTable t;
  init(t);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AddResult::Ok), static_cast<int>(t.add(0xF0, Role::Range)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AddResult::Exists), static_cast<int>(t.add(0xF0, Role::Health)));
  t.add(0xF1, Role::Range);
  t.add(0xF2, Role::Range);
  t.add(0xF3, Role::Range);
  TEST_ASSERT_EQUAL_size_t(4, t.count());

  TEST_ASSERT_TRUE(t.remove(0xF1));
  TEST_ASSERT_NULL(t.find(0xF1));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AddResult::Ok), static_cast<int>(t.add(0xF1, Role::Health)));
}

void test_an_uninitialised_table_refuses_to_add() {
  IdentityTable t;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(AddResult::NotReady), static_cast<int>(t.add(0xF0, Role::Range)));
}

// Every identity's key equals the independently generated W4 vector for its address.
void test_each_identity_key_matches_its_w4_vector() {
  IdentityTable t;
  init(t);
  for (NodeId id = kNodeSim0; id <= kNodeSim3; ++id) t.add(id, Role::Range);

  size_t matched = 0;
  for (size_t v = 0; v < lran_vectors::kKdfCount; ++v) {
    const lran_vectors::KdfVec& vec = lran_vectors::kKdf[v];
    const Identity*             e   = t.find(vec.node_id);
    if (e == nullptr) continue;
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(vec.node_key, e->key, kNodeKeyLen, vec.name);
    ++matched;
  }
  TEST_ASSERT_EQUAL_size_t(4, matched);
}

// Independent contexts and sequence spaces - the property B3 checks from the bridge's side.
void test_identities_have_independent_state() {
  IdentityTable t;
  init(t);
  t.add(0xF0, Role::Range);
  t.add(0xF1, Role::Range);
  Identity* a = t.find(0xF0);
  Identity* b = t.find(0xF1);

  TEST_ASSERT_NOT_EQUAL(0, a->ctx_id);
  TEST_ASSERT_NOT_EQUAL(0, b->ctx_id);
  TEST_ASSERT_NOT_EQUAL(a->ctx_id, b->ctx_id);
  TEST_ASSERT_EQUAL_UINT16(1, a->tx_seq);

  a->tx_seq = 40;
  ++a->counters.rx_frames;
  TEST_ASSERT_EQUAL_UINT16(1, b->tx_seq);
  TEST_ASSERT_EQUAL_UINT32(0, b->counters.rx_frames);
  TEST_ASSERT_EQUAL_UINT32(a->ctx_id, a->gate.ctx_id());
}

// spec 10.1 / 10.2 / 10.3 - a new context is a simulated reboot of that identity alone.
void test_a_new_context_resets_seq_and_gate_for_that_identity_only() {
  IdentityTable t;
  init(t);
  t.add(0xF0, Role::Range);
  t.add(0xF1, Role::Range);
  Identity* a = t.find(0xF0);
  Identity* b = t.find(0xF1);
  a->tx_seq = 99;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Verdict::Execute),
                        static_cast<int>(a->gate.check(5).verdict));
  const CtxId old_a = a->ctx_id;
  const CtxId old_b = b->ctx_id;

  TEST_ASSERT_TRUE(t.new_context(0xF0));
  TEST_ASSERT_NOT_EQUAL(old_a, a->ctx_id);
  TEST_ASSERT_NOT_EQUAL(0, a->ctx_id);
  TEST_ASSERT_EQUAL_UINT16(1, a->tx_seq);
  TEST_ASSERT_EQUAL_UINT16(0, a->gate.high_water());
  TEST_ASSERT_EQUAL_UINT32(a->ctx_id, a->gate.ctx_id());
  TEST_ASSERT_EQUAL_UINT32(old_b, b->ctx_id);
}

// spec 10.1 - never zero, even from an RNG that returns nothing but zero.
void test_a_context_is_never_zero() {
  IdentityTable t;
  t.init(lran_test::kTestMasterKey, &g_kdf, zero_random);
  t.add(0xF0, Role::Range);
  TEST_ASSERT_NOT_EQUAL(0, t.find(0xF0)->ctx_id);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_heltec_profile_matches_impl_plan_10_8_1);
  RUN_TEST(test_xiao_wio_kit_profile_matches_impl_plan_10_8_1);
  RUN_TEST(test_the_phy_is_the_d1_working_point_within_d33);
  RUN_TEST(test_role_tokens_are_exact);
  RUN_TEST(test_only_the_four_bench_addresses_are_accepted);
  RUN_TEST(test_duplicates_are_refused_and_the_table_holds_four);
  RUN_TEST(test_an_uninitialised_table_refuses_to_add);
  RUN_TEST(test_each_identity_key_matches_its_w4_vector);
  RUN_TEST(test_identities_have_independent_state);
  RUN_TEST(test_a_new_context_resets_seq_and_gate_for_that_identity_only);
  RUN_TEST(test_a_context_is_never_zero);
  return UNITY_END();
}
