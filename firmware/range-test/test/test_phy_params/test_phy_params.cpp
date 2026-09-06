// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Host tests for the D33 power clamp and the R3 settings dump.
//
// These run in the `native` environment. The clamp is the only thing in this
// firmware that is wrong in a way that puts illegal power on the air, and it is pure
// arithmetic, so it is checked here rather than on a bench with a spectrum analyser.

#include <unity.h>

#include <cstring>

#include "board_config.h"
#include "phy_params.h"

using namespace rangetest;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// D33 / spec 18.1 ceiling
// ---------------------------------------------------------------------------

// spec 18.1 states the figure outright: "With a 2 dBi antenna the conducted figure
// is roughly -3 dBm". This test is that sentence.
static void test_two_dbi_antenna_gives_minus_three_dbm_conducted() {
  TEST_ASSERT_EQUAL_INT8(-3, conducted_ceiling_dbm(20));
}

static void test_zero_gain_antenna_gives_the_eirp_ceiling_itself() {
  TEST_ASSERT_EQUAL_INT8(-1, conducted_ceiling_dbm(0));
}

// The reason the clamp uses floor and not truncation. -1.0 - 2.5 = -3.5 dBm; C++
// integer division would truncate that to -3, which is 0.5 dB ABOVE the ceiling.
static void test_fractional_ceiling_rounds_down_not_toward_zero() {
  TEST_ASSERT_EQUAL_INT8(-4, conducted_ceiling_dbm(25));
}

static void test_another_fractional_case_rounds_down() {
  // 3.1 dBi -> -1.0 - 3.1 = -4.1 -> floor -5
  TEST_ASSERT_EQUAL_INT8(-5, conducted_ceiling_dbm(31));
}

// ---------------------------------------------------------------------------
// clamp_conducted
// ---------------------------------------------------------------------------

static void test_request_above_ceiling_is_clamped_to_it() {
  PowerPoint p{};
  const ClampResult r = clamp_conducted(kSx1262MaxDbm, 20, &p);
  TEST_ASSERT_EQUAL(static_cast<int>(ClampResult::Clamped), static_cast<int>(r));
  TEST_ASSERT_EQUAL_INT8(-3, p.conducted_dbm);
}

// Task guardrail 3: the sweep starts at the bottom of the SX1262's range and climbs
// only on failure. The bottom must pass through unclamped.
static void test_sweep_start_power_passes_through_unchanged() {
  PowerPoint p{};
  const ClampResult r = clamp_conducted(kSx1262MinDbm, 20, &p);
  TEST_ASSERT_EQUAL(static_cast<int>(ClampResult::Ok), static_cast<int>(r));
  TEST_ASSERT_EQUAL_INT8(kSx1262MinDbm, p.conducted_dbm);
}

static void test_request_at_the_ceiling_is_not_reported_as_clamped() {
  PowerPoint p{};
  const ClampResult r = clamp_conducted(-3, 20, &p);
  TEST_ASSERT_EQUAL(static_cast<int>(ClampResult::Ok), static_cast<int>(r));
  TEST_ASSERT_EQUAL_INT8(-3, p.conducted_dbm);
}

// A 10 dBi antenna puts the conducted ceiling at -11 dBm, below the SX1262's -9 dBm
// floor. The honest answer is that this antenna cannot be used, not a quiet -9.
static void test_ceiling_below_radio_floor_is_reported_not_swallowed() {
  PowerPoint p{};
  const ClampResult r = clamp_conducted(-9, 100, &p);
  TEST_ASSERT_EQUAL(static_cast<int>(ClampResult::BelowRadioFloor),
                    static_cast<int>(r));
}

// D33 standing condition 1: conducted power and antenna gain are recorded
// SEPARATELY, so the EIRP can be audited later. They must not be collapsed.
static void test_antenna_gain_is_recorded_alongside_conducted_power() {
  PowerPoint p{};
  clamp_conducted(kSx1262MaxDbm, 20, &p);
  TEST_ASSERT_EQUAL_INT16(20, p.antenna_gain_dbi10);
  TEST_ASSERT_EQUAL_INT8(-3, p.conducted_dbm);
}

static void test_clamp_tolerates_a_null_out_pointer() {
  const ClampResult r = clamp_conducted(kSx1262MaxDbm, 20, nullptr);
  TEST_ASSERT_EQUAL(static_cast<int>(ClampResult::Clamped), static_cast<int>(r));
}

// ---------------------------------------------------------------------------
// R3 settings dump
// ---------------------------------------------------------------------------

static TestPoint sample_point() {
  TestPoint tp{};
  tp.freq_hz     = kProvisionalFreqHz;
  tp.sf          = kProvisionalSf;
  tp.cr_denom    = kProvisionalCrDenom;
  tp.payload_len = 32;
  clamp_conducted(kSx1262MaxDbm, 20, &tp.power);
  return tp;
}

static void test_settings_dump_records_the_swept_and_fixed_values() {
  char      buf[512];
  const TestPoint tp = sample_point();
  const size_t n = format_settings(tp, kHeltecV3.name, buf, sizeof(buf));

  TEST_ASSERT_GREATER_THAN_size_t(0, n);
  TEST_ASSERT_EQUAL_size_t(std::strlen(buf), n);

  TEST_ASSERT_NOT_NULL(std::strstr(buf, "board=heltec_wifi_lora_32_V3"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "freq_hz=915000000"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "sf=7"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "bw_khz=125.0"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "cr=4/5"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "sync_word=0x1424"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "explicit_header=1"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "crc=1"));
}

// The audit trail D33 asks for: both halves of the EIRP figure, separately, in the
// dump that a CSV is correlated against.
static void test_settings_dump_records_power_and_gain_separately() {
  char      buf[512];
  const TestPoint tp = sample_point();
  TEST_ASSERT_GREATER_THAN_size_t(0, format_settings(tp, kHeltecV3.name, buf,
                                                     sizeof(buf)));

  TEST_ASSERT_NOT_NULL(std::strstr(buf, "conducted_dbm=-3"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "antenna_gain_dbi=2.0"));
  TEST_ASSERT_NOT_NULL(std::strstr(buf, "eirp_ceiling_dbm=-1.0"));
}

// A truncated dump correlates a CSV with a configuration that was not the one used.
// Better to emit nothing and have the caller notice.
static void test_settings_dump_refuses_to_truncate() {
  char      buf[16];
  const TestPoint tp = sample_point();
  TEST_ASSERT_EQUAL_size_t(0, format_settings(tp, kHeltecV3.name, buf, sizeof(buf)));
}

static void test_settings_dump_tolerates_a_null_board_name() {
  char      buf[512];
  const TestPoint tp = sample_point();
  TEST_ASSERT_GREATER_THAN_size_t(0, format_settings(tp, nullptr, buf, sizeof(buf)));
}

// ---------------------------------------------------------------------------
// R2 pin map - the two settings that fail silently, and the DIO1 naming trap.
// ---------------------------------------------------------------------------

// spec 12.2 names 1.8 V for the Heltec. Wrong value presents as a radio that will
// not calibrate rather than as an error, so it is pinned by a test.
static void test_heltec_tcxo_is_1v8() {
  TEST_ASSERT_EQUAL_UINT16(1800, kHeltecV3.tcxo_mv);
}

// Without this the PA is never connected to the antenna and the radio reports a
// successful transmit.
static void test_heltec_switches_rf_from_dio2() {
  TEST_ASSERT_TRUE(kHeltecV3.dio2_as_rf_switch);
  TEST_ASSERT_EQUAL_INT8(kPinNone, kHeltecV3.rf_sw);
}

// Transcribed from the vendor variant pins_arduino.h; see board_config.h for the
// path. GPIO 14 is the variant's `DIO0` (an SX127x-era label) and is the SX1262's
// DIO1 - the number is right and the name is legacy.
// ---------------------------------------------------------------------------
// Pass 2 (task X7) - the XIAO+Wio Kit map, and the collision invariant.
// ---------------------------------------------------------------------------

// Transcribed from meshtastic/firmware variants/esp32s3/seeed_xiao_s3/variant.h - see
// board_config.h for the full provenance. These five are the ones that differ from the
// header-board product, and getting the wrong product's map is the failure this whole
// entry is named to prevent.
static void test_xiao_wio_kit_pin_map_is_the_b2b_variant() {
  TEST_ASSERT_EQUAL_INT8(41, kXiaoWioKit.nss);
  TEST_ASSERT_EQUAL_INT8(42, kXiaoWioKit.rst);
  TEST_ASSERT_EQUAL_INT8(40, kXiaoWioKit.busy);
  TEST_ASSERT_EQUAL_INT8(39, kXiaoWioKit.dio1);
  TEST_ASSERT_EQUAL_INT8(38, kXiaoWioKit.rf_sw);
}

// The three SPI nets are the ONLY thing the two products share, and they are also the
// XIAO's own D8/D9/D10 in the vendor variant. Pinned separately from the block above
// because they are the values that would still be right if the wrong module map were
// pasted in - so an all-in-one test would go green on half a mistake.
static void test_xiao_wio_kit_spi_matches_the_vendor_variant() {
  TEST_ASSERT_EQUAL_INT8(7, kXiaoWioKit.sck);
  TEST_ASSERT_EQUAL_INT8(8, kXiaoWioKit.miso);
  TEST_ASSERT_EQUAL_INT8(9, kXiaoWioKit.mosi);
}

// The third silent failure (radio_link.cpp begin()). Seeed does not tie DIO2 to the RF
// switch internally, so this board needs BOTH mechanisms - unlike the Heltec, which
// asserts kPinNone for rf_sw two tests above. Bridge Impl Plan 2.3.1 finding 1.
static void test_xiao_wio_kit_needs_both_rf_switch_mechanisms() {
  TEST_ASSERT_TRUE(kXiaoWioKit.dio2_as_rf_switch);
  TEST_ASSERT_NOT_EQUAL_INT8(kPinNone, kXiaoWioKit.rf_sw);
}

// Same part, same 1.8 V, but per-profile rather than shared - a wrong value here
// presents as a radio that will not calibrate rather than as an error.
static void test_xiao_wio_kit_tcxo_is_1v8() {
  TEST_ASSERT_EQUAL_UINT16(1800, kXiaoWioKit.tcxo_mv);
}

// The XIAO expansion board's panel has neither a Vext rail nor a reset line, and
// ui_oled.cpp branches on exactly these two sentinels. If either became a real GPIO by
// accident the bring-up would drive a pin belonging to something else.
static void test_xiao_ui_has_no_vext_and_no_panel_reset() {
  TEST_ASSERT_EQUAL_INT8(kPinNone, kXiaoWioKitUi.vext);
  TEST_ASSERT_EQUAL_INT8(kPinNone, kXiaoWioKitUi.rst);
}

// TRUE for the enclosure, not for the panel - the XIAO expansion board's display is not
// mounted rotated, but the box holds the stack inverted. Pinned because it is invisible
// to every other check: a wrong flip still ACKs at 0x3C, still draws, and is only ever
// caught by a person looking at it.
static void test_xiao_display_is_flipped_for_the_enclosure() {
  TEST_ASSERT_TRUE(kXiaoWioKitUi.flip_vertically);
}

// The Heltec keeps both, and keeps the flip. This is the "X4 was a no-op" assertion:
// the display refactor must not have changed the board that already worked.
static void test_heltec_ui_is_unchanged_by_the_refactor() {
  TEST_ASSERT_EQUAL_INT8(17, kHeltecV3Ui.sda);
  TEST_ASSERT_EQUAL_INT8(18, kHeltecV3Ui.scl);
  TEST_ASSERT_EQUAL_INT8(21, kHeltecV3Ui.rst);
  TEST_ASSERT_EQUAL_INT8(36, kHeltecV3Ui.vext);
  TEST_ASSERT_EQUAL_INT8(0,  kHeltecV3Ui.role_button);
  TEST_ASSERT_EQUAL_UINT8(0x3c, kHeltecV3Ui.addr);
  TEST_ASSERT_TRUE(kHeltecV3Ui.flip_vertically);
}

// GPIO 21 reaches the button across the B2B connector - it is not one of the XIAO's
// D-pads. Deliberately NOT GPIO 0: that is the ESP32-S3 BOOT strapping pin and the R1
// gesture is a press held across a reset (role.h).
static void test_xiao_role_button_is_not_the_boot_strap() {
  TEST_ASSERT_EQUAL_INT8(21, kXiaoWioKitUi.role_button);
  TEST_ASSERT_NOT_EQUAL_INT8(0, kXiaoWioKitUi.role_button);
}

// Both shipping profiles, checked the way the static_asserts in board_config.h check
// them. Duplicated as a runtime test on purpose: a static_assert that someone deletes
// to make a build go green leaves no trace, and this does.
static void test_no_shipping_profile_has_a_pin_collision() {
  TEST_ASSERT_FALSE(has_pin_conflict(kHeltecV3, kHeltecV3Ui));
  TEST_ASSERT_FALSE(has_pin_conflict(kXiaoWioKit, kXiaoWioKitUi));
}

// THE TEST THAT EARNS THE CHECKER. The header-board product (p-6379) puts NSS on GPIO 5
// and RF_SW on GPIO 6 - directly on top of the expansion board's I2C bus. Had that
// product arrived instead, this stack would not have worked, and the symptom would have
// been attributed to the radio or to the panel rather than to the pairing.
//
// Built as a local literal rather than a constant in board_config.h: this map must NOT
// be selectable, it exists only to prove the checker catches the case it was written
// for. If this test ever goes green-by-passing, the checker has stopped working.
//
// The real header-board map is owned by docs/gatelink/gatelink-expansion-board.md 6.1 -
// it is GateLink's module, not this firmware's. Do not promote this literal into a board
// profile; if this firmware ever needs that product, take the numbers from there.
static void test_the_checker_catches_the_header_board_collision() {
  const BoardRadioConfig header_board = {
      "wio_sx1262_header_board", "Wio hdr",
      /* nss */ 5, /* rst */ 3, /* busy */ 4, /* dio1 */ 2,
      /* sck */ 7, /* miso */ 8, /* mosi */ 9,
      /* rf_sw */ 6, /* tcxo_mv */ 1800, /* dio2_as_rf_switch */ true};
  TEST_ASSERT_TRUE(has_pin_conflict(header_board, kXiaoWioKitUi));
}

// kPinNone is "not connected" and must never collide with itself - the Heltec carries
// two of them (rf_sw and, on other boards, vext/rst), so a checker that compared
// sentinels would reject every valid board.
static void test_unconnected_pins_do_not_collide_with_each_other() {
  const BoardUiConfig bare = {/* sda */ 5,  /* scl */ 6,
                              /* rst */ kPinNone, /* vext */ kPinNone,
                              /* role_button */ 21, /* addr */ 0x3c,
                              /* flip */ false};
  TEST_ASSERT_FALSE(has_pin_conflict(kXiaoWioKit, bare));
}

static void test_heltec_pin_map_matches_the_vendor_variant() {
  TEST_ASSERT_EQUAL_INT8(8,  kHeltecV3.nss);
  TEST_ASSERT_EQUAL_INT8(9,  kHeltecV3.sck);
  TEST_ASSERT_EQUAL_INT8(11, kHeltecV3.miso);
  TEST_ASSERT_EQUAL_INT8(10, kHeltecV3.mosi);
  TEST_ASSERT_EQUAL_INT8(12, kHeltecV3.rst);
  TEST_ASSERT_EQUAL_INT8(13, kHeltecV3.busy);
  TEST_ASSERT_EQUAL_INT8(14, kHeltecV3.dio1);
}

// The OLED is 128 px and ArialMT_Plain_10 averages a little over 5 px per character,
// so a name past ~16 characters starts clipping. The full board name did exactly that
// on hardware ("heltec_wifi_lora_32_3"), which is why short_name exists.
//
// A character budget is a crude proxy for a rendered width, and it is the only one a
// host test can check. Its job is to stop pass 2 pasting another 22-character name
// into the XIAO entry and rediscovering this on a bench.
static void test_short_board_name_fits_the_panel() {
  TEST_ASSERT_NOT_NULL(kHeltecV3.short_name);
  TEST_ASSERT_LESS_OR_EQUAL_size_t(16u, std::strlen(kHeltecV3.short_name));
  TEST_ASSERT_GREATER_THAN_size_t(0u, std::strlen(kHeltecV3.short_name));
}

// The full name is what the R3 dump prints and what a CSV is correlated against, so
// it must stay the unambiguous one - not quietly replaced by the short form.
static void test_full_board_name_is_still_the_unambiguous_one() {
  TEST_ASSERT_EQUAL_STRING("heltec_wifi_lora_32_V3", kHeltecV3.name);
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_two_dbi_antenna_gives_minus_three_dbm_conducted);
  RUN_TEST(test_zero_gain_antenna_gives_the_eirp_ceiling_itself);
  RUN_TEST(test_fractional_ceiling_rounds_down_not_toward_zero);
  RUN_TEST(test_another_fractional_case_rounds_down);

  RUN_TEST(test_request_above_ceiling_is_clamped_to_it);
  RUN_TEST(test_sweep_start_power_passes_through_unchanged);
  RUN_TEST(test_request_at_the_ceiling_is_not_reported_as_clamped);
  RUN_TEST(test_ceiling_below_radio_floor_is_reported_not_swallowed);
  RUN_TEST(test_antenna_gain_is_recorded_alongside_conducted_power);
  RUN_TEST(test_clamp_tolerates_a_null_out_pointer);

  RUN_TEST(test_settings_dump_records_the_swept_and_fixed_values);
  RUN_TEST(test_settings_dump_records_power_and_gain_separately);
  RUN_TEST(test_settings_dump_refuses_to_truncate);
  RUN_TEST(test_settings_dump_tolerates_a_null_board_name);

  RUN_TEST(test_heltec_tcxo_is_1v8);
  RUN_TEST(test_heltec_switches_rf_from_dio2);
  RUN_TEST(test_heltec_pin_map_matches_the_vendor_variant);

  // Pass 2 (task X7)
  RUN_TEST(test_xiao_wio_kit_pin_map_is_the_b2b_variant);
  RUN_TEST(test_xiao_wio_kit_spi_matches_the_vendor_variant);
  RUN_TEST(test_xiao_wio_kit_needs_both_rf_switch_mechanisms);
  RUN_TEST(test_xiao_wio_kit_tcxo_is_1v8);
  RUN_TEST(test_xiao_ui_has_no_vext_and_no_panel_reset);
  RUN_TEST(test_xiao_display_is_flipped_for_the_enclosure);
  RUN_TEST(test_heltec_ui_is_unchanged_by_the_refactor);
  RUN_TEST(test_xiao_role_button_is_not_the_boot_strap);
  RUN_TEST(test_no_shipping_profile_has_a_pin_collision);
  RUN_TEST(test_the_checker_catches_the_header_board_collision);
  RUN_TEST(test_unconnected_pins_do_not_collide_with_each_other);
  RUN_TEST(test_short_board_name_fits_the_panel);
  RUN_TEST(test_full_board_name_is_still_the_unambiguous_one);

  return UNITY_END();
}
