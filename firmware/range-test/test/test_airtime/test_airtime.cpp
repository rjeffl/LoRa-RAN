// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Host tests for the LoRa time-on-air formula.
//
// THE POINT OF THIS SUITE is the spec 15.1 table. Every one of its twenty-one figures
// is asserted below. That table is W7's deliverable - "recompute once D1 fixes
// SF/BW/CR" - so an implementation reproducing it exactly is the instrument W7
// regenerates it with. If one of these ever fails, either this formula is wrong or
// the specification's table is, and the answer matters to 12.3's channel-occupancy
// budget as well as to the sweep.

#include <unity.h>

#include "airtime.h"
#include "phy_params.h"

using namespace rangetest;

void setUp() {}
void tearDown() {}

namespace {

// spec 15.1's stated conditions: BW 125 kHz, CR 4/5, explicit header, CRC on,
// 8-symbol preamble.
LoraParams spec_15_1(uint8_t sf) {
  LoraParams p{};
  p.sf               = sf;
  p.bw_khz10         = 1250;
  p.cr_denom         = 5;
  p.preamble_symbols = 8;
  p.explicit_header  = true;
  p.crc              = true;
  return p;
}

void check(uint8_t sf, uint16_t bytes, uint32_t expect_ms) {
  TEST_ASSERT_EQUAL_UINT32(expect_ms, airtime_ms(spec_15_1(sf), bytes));
}

}  // namespace

// | Frame              | Bytes | SF7   | SF8   | SF9    |
// | POLL               |    19 |  51   | 103   |  185   |
// | COMMAND_ACK        |    24 |  62   | 113   |  206   |
// | COMMAND            |    30 |  72   | 123   |  226   |
// | EVENT (0x11)       |    34 |  77   | 134   |  247   |
// | Node health (0xF0) |    38 |  82   | 144   |  267   |
// | STATUS (0x10)      |    96 | 164   | 297   |  534   |
// | PING, maximum      |   222 | 348   | 615   | 1107   |

static void test_spec_15_1_poll_19_bytes()          { check(7, 19,   51); check(8, 19,  103); check(9, 19,  185); }
static void test_spec_15_1_command_ack_24_bytes()   { check(7, 24,   62); check(8, 24,  113); check(9, 24,  206); }
static void test_spec_15_1_command_30_bytes()       { check(7, 30,   72); check(8, 30,  123); check(9, 30,  226); }
static void test_spec_15_1_event_34_bytes()         { check(7, 34,   77); check(8, 34,  134); check(9, 34,  247); }
static void test_spec_15_1_node_health_38_bytes()   { check(7, 38,   82); check(8, 38,  144); check(9, 38,  267); }
static void test_spec_15_1_status_96_bytes()        { check(7, 96,  164); check(8, 96,  297); check(9, 96,  534); }
static void test_spec_15_1_max_ping_222_bytes()     { check(7, 222, 348); check(8, 222, 615); check(9, 222, 1107); }

// spec 15.1's own note: at SF9 a full-size PING occupies the channel for over a
// second, and that is the number to check a CAD/backoff window against (12.3).
// Asserted as an inequality because it is the CLAIM, independent of the exact figure.
static void test_max_ping_at_sf9_exceeds_one_second() {
  TEST_ASSERT_GREATER_THAN_UINT32(1000, airtime_ms(spec_15_1(9), 222));
}

// The 4.25-symbol sync interval. spec 15.1 records that v0.2 omitted it and was ~4%
// low; dropping it here would reintroduce exactly that error, so it is pinned.
static void test_preamble_includes_the_4v25_symbol_sync_interval() {
  LoraParams p = spec_15_1(7);           // T_sym = 1.024 ms
  const uint32_t t_sym_us = 1024;

  // Isolate the preamble term: changing only n_pre must move the total by exactly
  // that many symbols, whatever the payload does.
  p.preamble_symbols = 8;
  const uint32_t at8 = airtime_us(p, 32);
  p.preamble_symbols = 12;
  const uint32_t at12 = airtime_us(p, 32);
  TEST_ASSERT_UINT32_WITHIN(2, 4 * t_sym_us, at12 - at8);

  // Now pin the 4.25 itself. At n_pre = 0 the preamble is the bare 4.25-symbol sync
  // interval, so the total is (4.25 + payload_syms) * T_sym. For a zero-length
  // payload at SF7/CR4:5 with CRC on and an explicit header the Semtech formula
  // still yields one coding block:
  //   num = -4*7 + 28 + 16 = 16, den = 28, ceil(16/28) = 1 block -> 5 symbols
  //   payload symbols = 8 + 5 = 13
  // giving (4.25 + 13) * 1.024 ms = 17.664 ms. Drop the 4.25 and this reads 13.312,
  // which is the ~4% understatement spec 15.1 records v0.2 having shipped.
  p.preamble_symbols = 0;
  TEST_ASSERT_UINT32_WITHIN(4, 17664, airtime_us(p, 0));
}

// The same zero-length case stated as a whole, because the decomposition above is
// easy to get subtly wrong and this is the number the formula actually returns.
static void test_zero_length_payload_still_costs_one_coding_block() {
  // 12.25 preamble symbols + 13 payload symbols = 25.25 * 1.024 ms.
  TEST_ASSERT_UINT32_WITHIN(4, 25856, airtime_us(spec_15_1(7), 0));
}

// LDRO must agree with what the radio actually does, or every SF11/SF12 figure is
// wrong - and those are the ones long enough for it to matter.
static void test_ldro_engages_at_sf11_and_sf12_only() {
  TEST_ASSERT_FALSE(low_data_rate_optimize(spec_15_1(9)));
  TEST_ASSERT_FALSE(low_data_rate_optimize(spec_15_1(10)));
  TEST_ASSERT_TRUE(low_data_rate_optimize(spec_15_1(11)));
  TEST_ASSERT_TRUE(low_data_rate_optimize(spec_15_1(12)));
}

// Monotonicity. Cheap, and it catches a transposed term that happens to land on one
// tabulated value.
static void test_airtime_rises_with_sf_and_with_payload() {
  for (uint8_t sf = 7; sf < 12; ++sf) {
    TEST_ASSERT_GREATER_THAN_UINT32(airtime_us(spec_15_1(sf), 32),
                                    airtime_us(spec_15_1(sf + 1), 32));
  }
  for (uint16_t n = 8; n < 200; n += 16) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(airtime_us(spec_15_1(9), n),
                                        airtime_us(spec_15_1(9), n + 16));
  }
}

// A weaker coding rate is more redundancy and therefore more airtime.
static void test_weaker_coding_rate_costs_airtime() {
  LoraParams a = spec_15_1(9); a.cr_denom = 5;
  LoraParams b = spec_15_1(9); b.cr_denom = 8;
  TEST_ASSERT_GREATER_THAN_UINT32(airtime_us(a, 64), airtime_us(b, 64));
}

// Nonsense in, zero out - not a plausible-looking number computed from nonsense.
static void test_out_of_range_parameters_return_zero() {
  LoraParams bad_sf = spec_15_1(9); bad_sf.sf = 13;
  TEST_ASSERT_EQUAL_UINT32(0, airtime_us(bad_sf, 32));

  LoraParams bad_cr = spec_15_1(9); bad_cr.cr_denom = 9;
  TEST_ASSERT_EQUAL_UINT32(0, airtime_us(bad_cr, 32));

  LoraParams bad_bw = spec_15_1(9); bad_bw.bw_khz10 = 0;
  TEST_ASSERT_EQUAL_UINT32(0, airtime_us(bad_bw, 32));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_spec_15_1_poll_19_bytes);
  RUN_TEST(test_spec_15_1_command_ack_24_bytes);
  RUN_TEST(test_spec_15_1_command_30_bytes);
  RUN_TEST(test_spec_15_1_event_34_bytes);
  RUN_TEST(test_spec_15_1_node_health_38_bytes);
  RUN_TEST(test_spec_15_1_status_96_bytes);
  RUN_TEST(test_spec_15_1_max_ping_222_bytes);
  RUN_TEST(test_max_ping_at_sf9_exceeds_one_second);
  RUN_TEST(test_preamble_includes_the_4v25_symbol_sync_interval);
  RUN_TEST(test_zero_length_payload_still_costs_one_coding_block);
  RUN_TEST(test_ldro_engages_at_sf11_and_sf12_only);
  RUN_TEST(test_airtime_rises_with_sf_and_with_payload);
  RUN_TEST(test_weaker_coding_rate_costs_airtime);
  RUN_TEST(test_out_of_range_parameters_return_zero);
  return UNITY_END();
}
