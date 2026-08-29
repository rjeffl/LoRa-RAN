// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// P4 - schemas. Offsets are asserted against the spec 7 tables FIELD BY FIELD, not
// merely round-tripped: a symmetric encoder and decoder agree on a wrong offset.

#include <unity.h>

#include "lran/lran.h"

using namespace lran;
using namespace lran::schema;

void setUp() {}
void tearDown() {}

namespace {

GateLinkStatusV1 distinct_status() {
  GateLinkStatusV1 s;
  s.gate_state           = 0x11;
  s.input_bits           = 0x22;
  s.hold                 = 0x33;
  s.movement_cause       = 0x44;
  s.last_direction       = 0x55;
  s.detect_flags         = 0x66;
  s.last_traversal_age_s = 0x89ABCDEFu;
  s.batt_mv              = 0x1234;
  s.batt_ma              = -2;
  s.pv_cv                = 0x5678;
  s.pv_w                 = 0x9ABC;
  s.load_ma              = kI16NotAvailable;
  s.yield_today          = 0x1111;
  s.yield_yest           = 0x2222;
  s.pmax_today           = 0x3333;
  s.yield_total          = 0x44556677u;
  s.charge_state         = 0xA1;
  s.mppt_err             = 0xA2;
  s.mppt_tracker         = 0xA3;
  s.mppt_flags           = 0xA4;
  s.mppt_temp_c10        = -100;
  s.bms_soc              = 0xB1;
  s.bms_flags            = 0xB2;
  s.pack_mv              = 0x1357;
  s.pack_ma              = -500;
  s.cell_count           = 4;
  s.bms_rssi_neg         = 80;
  s.cell_mv[0]           = 0xC001;
  s.cell_mv[1]           = 0xC002;
  s.cell_mv[2]           = 0xC003;
  s.cell_mv[3]           = 0xC004;
  s.cell_temp_c[0]       = -1;
  s.cell_temp_c[1]       = -2;
  s.cell_temp_c[2]       = 3;
  s.cell_temp_c[3]       = 4;
  s.bms_cycles           = 0xD00D;
  s.bms_capacity_dah     = 0xE00E;
  s.bms_alarms           = 0xF00F;
  s.bms_age_s            = kU16NotAvailable;
  s.uptime_s             = 0x0BADF00Du;
  s.boot_count           = 0x0102;
  s.node_mv              = 0x0304;
  s.node_ma              = -300;
  s.enclosure_temp_c10   = 250;
  s.node_flags           = 0x5A;
  s.status_reason        = static_cast<uint8_t>(StatusReason::DebugSynthetic);
  return s;
}

void assert_u16_at(const uint8_t* b, size_t off, uint16_t v) {
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(v & 0xFF), b[off]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(v >> 8), b[off + 1]);
}

void assert_u32_at(const uint8_t* b, size_t off, uint32_t v) {
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(v & 0xFF), b[off]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>((v >> 8) & 0xFF), b[off + 1]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>((v >> 16) & 0xFF), b[off + 2]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>((v >> 24) & 0xFF), b[off + 3]);
}

}  // namespace

// spec 7.1 registry - the lengths every other size check depends on.
void test_schema_lengths_match_registry() {
  TEST_ASSERT_EQUAL_UINT32(78, kGateLinkStatusV1Len);
  TEST_ASSERT_EQUAL_UINT32(16, kGateLinkEventV1Len);
  TEST_ASSERT_EQUAL_UINT32(20, kNodeHealthV1Len);
  TEST_ASSERT_EQUAL_UINT32(78, fixed_payload_len(MsgType::Status, kSchemaGateLinkStatusV1));
  TEST_ASSERT_EQUAL_UINT32(78, fixed_payload_len(MsgType::Status, kSchemaSimnodeStatusV1));
  TEST_ASSERT_EQUAL_UINT32(16, fixed_payload_len(MsgType::Event, kSchemaGateLinkEventV1));
  TEST_ASSERT_EQUAL_UINT32(20, fixed_payload_len(MsgType::Status, kSchemaNodeHealthV1));
  // spec 19 - STATUS 0x10 is a 96-byte frame.
  TEST_ASSERT_EQUAL_UINT32(96, frame_len(78, false));
  TEST_ASSERT_EQUAL_UINT32(38, frame_len(20, false));  // HEALTH 0xF0
  TEST_ASSERT_EQUAL_UINT32(34, frame_len(16, false));  // EVENT 0x11
  TEST_ASSERT_EQUAL_UINT32(30, frame_len(4, true));    // COMMAND
  TEST_ASSERT_EQUAL_UINT32(24, frame_len(6, false));   // COMMAND_ACK
  TEST_ASSERT_EQUAL_UINT32(19, frame_len(1, false));   // POLL
  TEST_ASSERT_EQUAL_UINT32(22, frame_len(4, false));   // ERROR
}

// spec 7.2 - every offset in the table, checked against the serialized bytes.
void test_gatelink_status_offsets() {
  uint8_t buf[kGateLinkStatusV1Len] = {};
  size_t  n = 0;
  const GateLinkStatusV1 s = distinct_status();
  TEST_ASSERT_EQUAL(Status::Ok, serialize(s, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(78, n);

  // Gate block, spec 7.2.1, offsets 0-9
  TEST_ASSERT_EQUAL_HEX8(0x11, buf[0]);   // gate_state
  TEST_ASSERT_EQUAL_HEX8(0x22, buf[1]);   // input_bits
  TEST_ASSERT_EQUAL_HEX8(0x33, buf[2]);   // hold
  TEST_ASSERT_EQUAL_HEX8(0x44, buf[3]);   // movement_cause
  TEST_ASSERT_EQUAL_HEX8(0x55, buf[4]);   // last_direction
  TEST_ASSERT_EQUAL_HEX8(0x66, buf[5]);   // detect_flags
  assert_u32_at(buf, 6, 0x89ABCDEFu);     // last_traversal_age_s

  // MPPT block, spec 7.2.2, offsets 10-35
  assert_u16_at(buf, 10, 0x1234);         // batt_mv
  assert_u16_at(buf, 12, 0xFFFE);         // batt_ma = -2
  assert_u16_at(buf, 14, 0x5678);         // pv_cv
  assert_u16_at(buf, 16, 0x9ABC);         // pv_w
  assert_u16_at(buf, 18, 0x8000);         // load_ma = INT16_MIN sentinel
  assert_u16_at(buf, 20, 0x1111);         // yield_today
  assert_u16_at(buf, 22, 0x2222);         // yield_yest
  assert_u16_at(buf, 24, 0x3333);         // pmax_today
  assert_u32_at(buf, 26, 0x44556677u);    // yield_total
  TEST_ASSERT_EQUAL_HEX8(0xA1, buf[30]);  // charge_state
  TEST_ASSERT_EQUAL_HEX8(0xA2, buf[31]);  // mppt_err
  TEST_ASSERT_EQUAL_HEX8(0xA3, buf[32]);  // mppt_tracker
  TEST_ASSERT_EQUAL_HEX8(0xA4, buf[33]);  // mppt_flags
  assert_u16_at(buf, 34, 0xFF9C);         // mppt_temp_c10 = -100

  // BMS block, spec 7.2.3, offsets 36-63
  TEST_ASSERT_EQUAL_HEX8(0xB1, buf[36]);  // bms_soc
  TEST_ASSERT_EQUAL_HEX8(0xB2, buf[37]);  // bms_flags
  assert_u16_at(buf, 38, 0x1357);         // pack_mv
  assert_u16_at(buf, 40, 0xFE0C);         // pack_ma = -500
  TEST_ASSERT_EQUAL_HEX8(4, buf[42]);     // cell_count
  TEST_ASSERT_EQUAL_HEX8(80, buf[43]);    // bms_rssi_neg
  assert_u16_at(buf, 44, 0xC001);         // cell_mv[0]
  assert_u16_at(buf, 46, 0xC002);         // cell_mv[1]
  assert_u16_at(buf, 48, 0xC003);         // cell_mv[2]
  assert_u16_at(buf, 50, 0xC004);         // cell_mv[3]
  TEST_ASSERT_EQUAL_HEX8(0xFF, buf[52]);  // cell_temp_c[0] = -1
  TEST_ASSERT_EQUAL_HEX8(0xFE, buf[53]);  // cell_temp_c[1] = -2
  TEST_ASSERT_EQUAL_HEX8(0x03, buf[54]);  // cell_temp_c[2]
  TEST_ASSERT_EQUAL_HEX8(0x04, buf[55]);  // cell_temp_c[3]
  assert_u16_at(buf, 56, 0xD00D);         // bms_cycles
  assert_u16_at(buf, 58, 0xE00E);         // bms_capacity_dah
  assert_u16_at(buf, 60, 0xF00F);         // bms_alarms
  assert_u16_at(buf, 62, 0xFFFF);         // bms_age_s sentinel

  // Node block, spec 7.2.4, offsets 64-77
  assert_u32_at(buf, 64, 0x0BADF00Du);    // uptime_s
  assert_u16_at(buf, 68, 0x0102);         // boot_count
  assert_u16_at(buf, 70, 0x0304);         // node_mv
  assert_u16_at(buf, 72, 0xFED4);         // node_ma = -300
  assert_u16_at(buf, 74, 0x00FA);         // enclosure_temp_c10 = 250
  TEST_ASSERT_EQUAL_HEX8(0x5A, buf[76]);  // node_flags
  TEST_ASSERT_EQUAL_HEX8(0xFF, buf[77]);  // status_reason = DEBUG_SYNTHETIC
}

void test_gatelink_status_roundtrip() {
  uint8_t buf[kGateLinkStatusV1Len] = {};
  size_t  n = 0;
  const GateLinkStatusV1 s = distinct_status();
  serialize(s, buf, sizeof(buf), &n);

  GateLinkStatusV1 b;
  TEST_ASSERT_EQUAL(Status::Ok, deserialize(buf, n, &b));
  TEST_ASSERT_EQUAL_HEX8(s.gate_state, b.gate_state);
  TEST_ASSERT_EQUAL_HEX32(s.last_traversal_age_s, b.last_traversal_age_s);
  TEST_ASSERT_EQUAL_INT16(s.batt_ma, b.batt_ma);
  TEST_ASSERT_EQUAL_INT16(s.load_ma, b.load_ma);
  TEST_ASSERT_EQUAL_INT16(s.mppt_temp_c10, b.mppt_temp_c10);
  TEST_ASSERT_EQUAL_HEX32(s.yield_total, b.yield_total);
  TEST_ASSERT_EQUAL_INT16(s.pack_ma, b.pack_ma);
  TEST_ASSERT_EQUAL_INT8(s.cell_temp_c[0], b.cell_temp_c[0]);
  TEST_ASSERT_EQUAL_INT8(s.cell_temp_c[1], b.cell_temp_c[1]);
  TEST_ASSERT_EQUAL_HEX16(s.bms_age_s, b.bms_age_s);
  TEST_ASSERT_EQUAL_HEX32(s.uptime_s, b.uptime_s);
  TEST_ASSERT_EQUAL_INT16(s.node_ma, b.node_ma);
  TEST_ASSERT_EQUAL_INT16(s.enclosure_temp_c10, b.enclosure_temp_c10);
  TEST_ASSERT_EQUAL_HEX8(s.status_reason, b.status_reason);

  TEST_ASSERT_EQUAL(Status::BadLength, deserialize(buf, 77, &b));
  TEST_ASSERT_EQUAL(Status::BadLength, deserialize(buf, 79, &b));
}

// spec 4.6 - a consumer must be able to tell "0 A" from "no reading". The defaults
// are the sentinels, not zero.
void test_status_defaults_are_sentinels() {
  GateLinkStatusV1 s;
  TEST_ASSERT_EQUAL_HEX32(UINT32_MAX, s.last_traversal_age_s);
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, s.load_ma);
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, s.mppt_temp_c10);
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, s.enclosure_temp_c10);
  TEST_ASSERT_EQUAL_HEX16(UINT16_MAX, s.bms_age_s);
  TEST_ASSERT_EQUAL_HEX8(0xFF, s.bms_soc);
}

// spec 7.3 - offsets 0..15.
void test_gatelink_event_offsets() {
  GateLinkEventV1 e;
  e.event_type  = static_cast<uint8_t>(EventType::VehicleWhileHeldOpen);
  e.event_flags = kEventFlagFollowUp;
  e.hold_source = static_cast<uint8_t>(HoldSource::Manual);
  e.direction   = static_cast<uint8_t>(Direction::Entry);
  e.gate_state  = static_cast<uint8_t>(GateState::OpenHeld);
  e.input_bits  = 0x2D;
  e.detail      = 0xBEEF;
  e.event_id    = 0x01020304u;
  e.uptime_s    = 0x0A0B0C0Du;

  uint8_t buf[kGateLinkEventV1Len] = {};
  size_t  n = 0;
  TEST_ASSERT_EQUAL(Status::Ok, serialize(e, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(16, n);

  TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);  // event_type
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);  // event_flags
  TEST_ASSERT_EQUAL_HEX8(0x02, buf[2]);  // hold_source
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[3]);  // direction
  TEST_ASSERT_EQUAL_HEX8(0x04, buf[4]);  // gate_state
  TEST_ASSERT_EQUAL_HEX8(0x2D, buf[5]);  // input_bits
  assert_u16_at(buf, 6, 0xBEEF);         // detail
  assert_u32_at(buf, 8, 0x01020304u);    // event_id
  assert_u32_at(buf, 12, 0x0A0B0C0Du);   // uptime_s

  GateLinkEventV1 b;
  TEST_ASSERT_EQUAL(Status::Ok, deserialize(buf, n, &b));
  TEST_ASSERT_EQUAL_HEX32(e.event_id, b.event_id);
  TEST_ASSERT_EQUAL_HEX16(e.detail, b.detail);
}

// spec 7.5 - offsets 0..19.
void test_node_health_offsets() {
  NodeHealthV1 h;
  h.uptime_s      = 0x11223344u;
  h.boot_count    = 0x5566;
  h.rx_frames     = 0x7788;
  h.tx_frames     = 0x99AA;
  h.rx_dropped    = 0xBBCC;
  h.cad_backoffs  = 0xDDEE;
  h.last_rssi_dbm = -95;
  h.last_snr_db10 = -75;
  h.proto_ver     = kProtoVer;
  h.health_flags  = kHealthFlagDebugActive;

  uint8_t buf[kNodeHealthV1Len] = {};
  size_t  n = 0;
  TEST_ASSERT_EQUAL(Status::Ok, serialize(h, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(20, n);

  assert_u32_at(buf, 0, 0x11223344u);
  assert_u16_at(buf, 4, 0x5566);
  assert_u16_at(buf, 6, 0x7788);
  assert_u16_at(buf, 8, 0x99AA);
  assert_u16_at(buf, 10, 0xBBCC);
  assert_u16_at(buf, 12, 0xDDEE);
  assert_u16_at(buf, 14, 0xFFA1);  // -95
  assert_u16_at(buf, 16, 0xFFB5);  // -75
  TEST_ASSERT_EQUAL_HEX8(kProtoVer, buf[18]);
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[19]);

  NodeHealthV1 b;
  TEST_ASSERT_EQUAL(Status::Ok, deserialize(buf, n, &b));
  TEST_ASSERT_EQUAL_INT16(-95, b.last_rssi_dbm);
  TEST_ASSERT_EQUAL_INT16(-75, b.last_snr_db10);
}

// spec 7.4 - CONFIG entry layout, offsets within an entry.
void test_config_entry_offsets() {
  GateLinkConfigV1 cfg;
  cfg.op    = ConfigOp::Set;
  cfg.count = 1;
  TEST_ASSERT_TRUE(entry_pack(&cfg.entries[0], 0x0102, PType::U32, 0xAABBCCDDu));

  uint8_t buf[kMaxSchemaPayload] = {};
  size_t  n = 0;
  TEST_ASSERT_EQUAL(Status::Ok, serialize(cfg, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(2 + 4 + 4, n);

  TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);  // op = SET
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);  // count
  assert_u16_at(buf, 2, 0x0102);         // entry param_id
  TEST_ASSERT_EQUAL_HEX8(0x03, buf[4]);  // ptype = U32
  TEST_ASSERT_EQUAL_HEX8(0x04, buf[5]);  // len
  assert_u32_at(buf, 6, 0xAABBCCDDu);    // value, little-endian
}

// spec 7.4 - the ACK carries the effective value and a per-entry status.
void test_config_ack_entry_offsets() {
  GateLinkConfigAckV1 ack;
  ack.op             = ConfigOp::Set;
  ack.persist_status = PersistStatus::AppliedNotPersisted;
  ack.count          = 1;
  TEST_ASSERT_TRUE(entry_pack(&ack.entries[0], 0x0002, ParamStatus::Clamped,
                              PType::U16, 30000));

  uint8_t buf[kMaxSchemaPayload] = {};
  size_t  n = 0;
  TEST_ASSERT_EQUAL(Status::Ok, serialize(ack, buf, sizeof(buf), &n));
  TEST_ASSERT_EQUAL_UINT32(3 + 5 + 2, n);

  TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);  // op
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]);  // persist_status = APPLIED_NOT_PERSISTED
  TEST_ASSERT_EQUAL_HEX8(0x01, buf[2]);  // count
  assert_u16_at(buf, 3, 0x0002);         // param_id
  TEST_ASSERT_EQUAL_HEX8(0x02, buf[5]);  // status = CLAMPED
  TEST_ASSERT_EQUAL_HEX8(0x02, buf[6]);  // ptype = U16
  TEST_ASSERT_EQUAL_HEX8(0x02, buf[7]);  // len
  assert_u16_at(buf, 8, 30000);          // effective value

  GateLinkConfigAckV1 b;
  TEST_ASSERT_EQUAL(Status::Ok, deserialize(buf, n, &b));
  TEST_ASSERT_EQUAL(PersistStatus::AppliedNotPersisted, b.persist_status);
  TEST_ASSERT_EQUAL(ParamStatus::Clamped, b.entries[0].status);
  TEST_ASSERT_EQUAL_UINT32(30000, entry_raw(b.entries[0].value, b.entries[0].len));
}

void test_config_signed_values_sign_extend() {
  ConfigEntry e;
  TEST_ASSERT_TRUE(entry_pack(&e, 0x0010, PType::I16, static_cast<uint32_t>(-40)));
  TEST_ASSERT_EQUAL_UINT8(2, e.len);
  TEST_ASSERT_EQUAL_INT32(-40, entry_signed(e.value, e.len, PType::I16));
  TEST_ASSERT_EQUAL_UINT32(0xFFD8u, entry_raw(e.value, e.len));

  TEST_ASSERT_TRUE(entry_pack(&e, 0x0011, PType::I32, static_cast<uint32_t>(-70000)));
  TEST_ASSERT_EQUAL_UINT8(4, e.len);
  TEST_ASSERT_EQUAL_INT32(-70000, entry_signed(e.value, e.len, PType::I32));
}

// Caps derive from kMaxSchemaPayload, so they cannot drift out of agreement with it.
void test_config_entry_caps_are_derived() {
  TEST_ASSERT_EQUAL_UINT32(38, kMaxConfigEntries);
  TEST_ASSERT_EQUAL_UINT32(32, kMaxConfigAckEntries);
  TEST_ASSERT_TRUE(kConfigHdrLen + kMaxConfigEntries * (kConfigEntryHdrLen + 1) <=
                   kMaxSchemaPayload);
  TEST_ASSERT_TRUE(kConfigAckHdrLen + kMaxConfigAckEntries * (kConfigAckEntryHdrLen + 1) <=
                   kMaxSchemaPayload);
}

void test_schema_serialize_rejects_small_buffer() {
  const GateLinkStatusV1 s = distinct_status();
  uint8_t small[77];
  size_t  n = 0;
  TEST_ASSERT_EQUAL(Status::BufferTooSmall, serialize(s, small, sizeof(small), &n));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_schema_lengths_match_registry);
  RUN_TEST(test_gatelink_status_offsets);
  RUN_TEST(test_gatelink_status_roundtrip);
  RUN_TEST(test_status_defaults_are_sentinels);
  RUN_TEST(test_gatelink_event_offsets);
  RUN_TEST(test_node_health_offsets);
  RUN_TEST(test_config_entry_offsets);
  RUN_TEST(test_config_ack_entry_offsets);
  RUN_TEST(test_config_signed_values_sign_extend);
  RUN_TEST(test_config_entry_caps_are_derived);
  RUN_TEST(test_schema_serialize_rejects_small_buffer);
  return UNITY_END();
}
