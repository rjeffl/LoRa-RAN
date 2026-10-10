// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// bms_link: the BMS block of spec 7.2.3 from a decoded read, the frames that cut a BLE
// window short, and the interlock's decisions (Impl Plan 5.2, PRD R-4.3h). GL5.
//
// kRsp8C is bms-protocol 9's capture of the gate's pack, as lib/bms-ble's suite holds it.

#include <climits>
#include <cstring>

#include <unity.h>

#include "bms_ble/tdt_protocol.h"
#include "bms_link.h"
#include "lran/config.h"
#include "lran/schema/gatelink_status_v1.h"
#include "lran/types.h"

using gatelink::BmsSnapshot;
using gatelink::TxGate;
using lran::schema::GateLinkStatusV1;

void setUp() {}
void tearDown() {}

namespace {

const uint8_t kRsp8C[] = {
    0x7e, 0x00, 0x01, 0x03, 0x00, 0x8c, 0x00, 0x20, 0x04, 0x0d, 0x89, 0x0d,
    0xa1, 0x0d, 0x9c, 0x0d, 0x9b, 0x04, 0x0b, 0x82, 0x0b, 0x9d, 0x0b, 0x7f,
    0x0b, 0x7e, 0x40, 0x00, 0x05, 0x70, 0x03, 0xe7, 0x03, 0xe8, 0x00, 0x01,
    0x03, 0xe8, 0x00, 0x64, 0x55, 0xa3, 0x0d,
};

BmsSnapshot captured(uint32_t read_ms) {
  bms::tdt::FrameReassembler rx;
  bms::tdt::FrameReassembler::Status st = bms::tdt::FrameReassembler::kIncomplete;
  rx.feed(kRsp8C, sizeof(kRsp8C), st);
  TEST_ASSERT_EQUAL(bms::tdt::FrameReassembler::kComplete, st);
  BmsSnapshot b;
  TEST_ASSERT_TRUE(bms::tdt::decode_cells_and_pack(rx.frame(), b.data));
  b.have     = true;
  b.rssi_dbm = -63;
  b.read_ms  = read_ms;
  return b;
}

// A header with `type` and `schema`, and a payload long enough to reach status_reason.
size_t frame(uint8_t* out, lran::MsgType type, uint8_t schema, uint8_t reason) {
  std::memset(out, 0, 128);
  out[0]  = 2;
  out[1]  = static_cast<uint8_t>(type);
  out[11] = schema;
  out[lran::kHdrLen + 77] = reason;
  return lran::kHdrLen + 78 + 10;
}

}  // namespace

void test_no_read_sends_sentinels() {
  GateLinkStatusV1 s;
  gatelink::fill_bms_block(BmsSnapshot{}, 5000, &s);
  TEST_ASSERT_EQUAL_HEX8(lran::kSocNotAvailable, s.bms_soc);
  TEST_ASSERT_EQUAL_HEX8(0xC0, s.bms_flags);  // spec 7.2.7: no read, soc_source unknown
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, s.pack_mv);
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, s.pack_ma);
  TEST_ASSERT_EQUAL_UINT8(0, s.cell_count);
  TEST_ASSERT_EQUAL_UINT8(0, s.bms_rssi_neg);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, s.cell_mv[0]);
  TEST_ASSERT_EQUAL_INT8(INT8_MIN, s.cell_temp_c[3]);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, s.bms_cycles);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, s.bms_capacity_dah);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, s.bms_alarms);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, s.bms_age_s);
}

void test_captured_read_fills_the_block() {
  GateLinkStatusV1 s;
  gatelink::fill_bms_block(captured(1000), 31999, &s);
  TEST_ASSERT_EQUAL_UINT8(100, s.bms_soc);
  TEST_ASSERT_EQUAL_HEX8(0x01, s.bms_flags);  // valid, soc_source bms_ble
  TEST_ASSERT_EQUAL_UINT16(13920, s.pack_mv);
  TEST_ASSERT_EQUAL_INT16(0, s.pack_ma);
  TEST_ASSERT_EQUAL_UINT8(4, s.cell_count);
  TEST_ASSERT_EQUAL_UINT8(63, s.bms_rssi_neg);
  TEST_ASSERT_EQUAL_UINT16(3465, s.cell_mv[0]);
  TEST_ASSERT_EQUAL_UINT16(3483, s.cell_mv[3]);
  TEST_ASSERT_EQUAL_INT8(22, s.cell_temp_c[0]);  // 21.5 C rounds up
  TEST_ASSERT_EQUAL_INT8(24, s.cell_temp_c[1]);  // 24.2 C
  TEST_ASSERT_EQUAL_INT8(21, s.cell_temp_c[3]);  // 21.1 C
  TEST_ASSERT_EQUAL_UINT16(1, s.bms_cycles);
  TEST_ASSERT_EQUAL_UINT16(1000, s.bms_capacity_dah);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, s.bms_alarms);  // 0x8D is not decoded
  TEST_ASSERT_EQUAL_UINT16(30, s.bms_age_s);
}

void test_block_round_trips_the_wire() {
  GateLinkStatusV1 s;
  gatelink::fill_bms_block(captured(0), 0, &s);
  uint8_t buf[lran::schema::kGateLinkStatusV1Len];
  size_t  n = 0;
  TEST_ASSERT_EQUAL(lran::Status::Ok, lran::schema::serialize(s, buf, sizeof(buf), &n));
  GateLinkStatusV1 back;
  TEST_ASSERT_EQUAL(lran::Status::Ok, lran::schema::deserialize(buf, n, &back));
  TEST_ASSERT_EQUAL_UINT16(13920, back.pack_mv);
  TEST_ASSERT_EQUAL_UINT16(3489, back.cell_mv[1]);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, back.bms_alarms);
}

void test_age_saturates() {
  GateLinkStatusV1 s;
  gatelink::fill_bms_block(captured(0), 70000u * 1000u, &s);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, s.bms_age_s);
}

void test_current_clamps_and_keeps_the_sentinel() {
  BmsSnapshot b = captured(0);
  GateLinkStatusV1 s;
  b.data.current_ma = 40000;
  gatelink::fill_bms_block(b, 0, &s);
  TEST_ASSERT_EQUAL_INT16(INT16_MAX, s.pack_ma);
  b.data.current_ma = -40000;
  gatelink::fill_bms_block(b, 0, &s);
  TEST_ASSERT_EQUAL_INT16(INT16_MIN + 1, s.pack_ma);
}

void test_fewer_cells_leave_sentinels() {
  BmsSnapshot b = captured(0);
  b.data.cell_count = 3;
  b.data.temp_count = 2;
  GateLinkStatusV1 s;
  gatelink::fill_bms_block(b, 0, &s);
  TEST_ASSERT_EQUAL_UINT8(3, s.cell_count);
  TEST_ASSERT_EQUAL_UINT16(UINT16_MAX, s.cell_mv[3]);
  TEST_ASSERT_EQUAL_INT8(INT8_MIN, s.cell_temp_c[2]);
}

void test_unmeasured_rssi_reads_no_link() {
  BmsSnapshot b = captured(0);
  b.rssi_dbm    = 0;
  GateLinkStatusV1 s;
  gatelink::fill_bms_block(b, 0, &s);
  TEST_ASSERT_EQUAL_UINT8(0, s.bms_rssi_neg);
}

void test_awaited_replies() {
  uint8_t f[128];
  using lran::MsgType;
  const uint8_t poll = static_cast<uint8_t>(lran::StatusReason::PollResponse);
  const uint8_t gate = static_cast<uint8_t>(lran::StatusReason::GateStateChange);
  TEST_ASSERT_TRUE(gatelink::reply_awaited(f, frame(f, MsgType::CommandAck, 0, 0)));
  TEST_ASSERT_TRUE(gatelink::reply_awaited(f, frame(f, MsgType::ConfigAck, 0x12, 0)));
  TEST_ASSERT_TRUE(gatelink::reply_awaited(f, frame(f, MsgType::HexRsp, 0, 0)));
  TEST_ASSERT_TRUE(gatelink::reply_awaited(f, frame(f, MsgType::Status, 0x10, poll)));
  TEST_ASSERT_TRUE(gatelink::reply_awaited(f, frame(f, MsgType::Status, 0xF0, gate)));
  TEST_ASSERT_FALSE(gatelink::reply_awaited(f, frame(f, MsgType::Status, 0x10, gate)));
  TEST_ASSERT_FALSE(gatelink::reply_awaited(f, frame(f, MsgType::Event, 0x11, 0)));
  TEST_ASSERT_FALSE(gatelink::reply_awaited(f, frame(f, MsgType::Ping, 0, 0)));
  TEST_ASSERT_FALSE(gatelink::reply_awaited(f, 8));  // shorter than a header
  TEST_ASSERT_FALSE(gatelink::reply_awaited(nullptr, 64));
}

void test_tx_gate() {
  TEST_ASSERT_EQUAL(TxGate::Go, gatelink::tx_gate(false, 0, 1000, true));
  TEST_ASSERT_EQUAL(TxGate::Wait, gatelink::tx_gate(true, 999, 1000, false));
  TEST_ASSERT_EQUAL(TxGate::WaitAbort, gatelink::tx_gate(true, 10, 1000, true));
  TEST_ASSERT_EQUAL(TxGate::Overrun, gatelink::tx_gate(true, 1000, 1000, false));
  TEST_ASSERT_EQUAL(TxGate::Overrun, gatelink::tx_gate(true, 5000, 1000, true));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_read_sends_sentinels);
  RUN_TEST(test_captured_read_fills_the_block);
  RUN_TEST(test_block_round_trips_the_wire);
  RUN_TEST(test_age_saturates);
  RUN_TEST(test_current_clamps_and_keeps_the_sentinel);
  RUN_TEST(test_fewer_cells_leave_sentinels);
  RUN_TEST(test_unmeasured_rssi_reads_no_link);
  RUN_TEST(test_awaited_replies);
  RUN_TEST(test_tx_gate);
  return UNITY_END();
}
