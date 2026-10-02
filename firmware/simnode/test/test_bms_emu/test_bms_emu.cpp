// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink task L7 - the emulated TDT BMS (GateLink Impl Plan 8.1, bms-protocol 3-9).
//
// The emulator replays captures and never includes lib/bms-ble/. These tests use that
// library as the client, so each half checks the other: a wrong byte in the emulator fails
// the decode, and a codec defect has no copy of itself on the far end to agree with.

#include <unity.h>

#include <cstring>

#include "bms_ble/bms_data.h"
#include "bms_ble/tdt_protocol.h"
#include "bms_emu.h"

using namespace simnode;

void setUp() {}
void tearDown() {}

namespace {

class FakeLink final : public BmsLink {
 public:
  bool notify(const uint8_t* data, size_t len) override {
    if (refuse) return false;
    if (count < kMax) sizes[count] = len;
    ++count;
    std::memcpy(bytes + total, data, len);
    total += len;
    return true;
  }
  void drop() override { ++drops; }

  static constexpr int kMax = 16;
  uint8_t bytes[1024]  = {0};
  size_t  total        = 0;
  size_t  sizes[kMax]  = {0};
  int     count        = 0;
  int     drops        = 0;
  bool    refuse       = false;
};

class Transcript final : public Sink {
 public:
  void line(const char* text) override {
    std::strncpy(last, text, sizeof(last) - 1);
    if (count == 0) std::strncpy(first, text, sizeof(first) - 1);
    ++count;
  }
  char first[200] = {0};
  char last[200]  = {0};
  int  count      = 0;
};

class FakeControl final : public BmsControl {
 public:
  bool start(const char* adv_name) override {
    std::strncpy(name, adv_name, sizeof(name) - 1);
    on = true;
    return true;
  }
  void        stop() override { on = false; }
  bool        running() const override { return on; }
  const char* default_suffix() const override { return "BC2C"; }
  char        name[32] = {0};
  bool        on       = false;
};

const uint8_t kHiLink[] = {'H', 'i', 'L', 'i', 'n', 'k'};

// A connected, handshaken, subscribed emulator at the MTU the client negotiated.
void ready(BmsEmu& emu, uint16_t mtu) {
  emu.on_connect(0);
  emu.on_mtu(mtu);
  emu.on_write(BmsChar::Handshake, kHiLink, sizeof(kHiLink), 10);
  emu.on_subscribe(true);
}

void request(BmsEmu& emu, uint8_t cmd, uint32_t now = 100) {
  uint8_t req[bms::tdt::kRequestLen];
  TEST_ASSERT_EQUAL(bms::tdt::kRequestLen, bms::tdt::build_request(cmd, req, sizeof(req)));
  emu.on_write(BmsChar::Tx, req, sizeof(req), now);
  emu.tick(now);
}

// Feeds what the link carried through the client's reassembler, in the notification sizes
// the link saw. Returns the last status.
bms::tdt::FrameReassembler::Status reassemble(const FakeLink& link, bms::tdt::FrameReassembler& rx) {
  bms::tdt::FrameReassembler::Status st = bms::tdt::FrameReassembler::kIncomplete;
  size_t sent = 0;
  for (int i = 0; i < link.count && i < FakeLink::kMax; ++i) {
    size_t off = 0;
    while (off < link.sizes[i]) off += rx.feed(link.bytes + sent + off, link.sizes[i] - off, st);
    sent += link.sizes[i];
  }
  return st;
}

char* g_argv[5];
int   split(char* line) {
  int n = 0;
  for (char* t = std::strtok(line, " "); t != nullptr && n < 5; t = std::strtok(nullptr, " ")) g_argv[n++] = t;
  return n;
}

}  // namespace

// --- bms-protocol 9: the captures, decoded by the client ------------------------------

void test_8c_decodes_to_the_captured_values() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  request(emu, bms::tdt::kCmdCellsPack);
  TEST_ASSERT_EQUAL(1, link.count);  // one notification at MTU 512 (bms-protocol 7)
  TEST_ASSERT_EQUAL(43, link.total);

  bms::tdt::FrameReassembler rx;
  TEST_ASSERT_EQUAL(bms::tdt::FrameReassembler::kComplete, reassemble(link, rx));
  bms::BmsData d;
  TEST_ASSERT_TRUE(bms::tdt::decode_cells_and_pack(rx.frame(), d));
  TEST_ASSERT_EQUAL(4, d.cell_count);
  TEST_ASSERT_EQUAL(3465, d.cell_mv[0]);
  TEST_ASSERT_EQUAL(3483, d.cell_mv[3]);
  TEST_ASSERT_EQUAL(13920, d.pack_mv);
  TEST_ASSERT_EQUAL(100, d.soc_pct);
  TEST_ASSERT_EQUAL(1, d.cycles);
  TEST_ASSERT_EQUAL(1, emu.stats().responses);
}

void test_92_decodes_to_the_captured_strings() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  request(emu, bms::tdt::kCmdDeviceInfo);
  bms::tdt::FrameReassembler rx;
  TEST_ASSERT_EQUAL(bms::tdt::FrameReassembler::kComplete, reassemble(link, rx));
  bms::DeviceInfo info;
  TEST_ASSERT_TRUE(bms::tdt::decode_device_info(rx.frame(), info));
  TEST_ASSERT_EQUAL_STRING("WT30_10004SW14_L_01", info.sw_version);
  TEST_ASSERT_EQUAL_STRING("IKKKK0000AII00000000", info.serial_number);
}

void test_8d_is_framed_and_its_crc_verifies() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  request(emu, bms::tdt::kCmdAlarms);
  bms::tdt::FrameReassembler rx;
  TEST_ASSERT_EQUAL(bms::tdt::FrameReassembler::kComplete, reassemble(link, rx));
  TEST_ASSERT_EQUAL_HEX8(0x8D, rx.frame().cmd);
  TEST_ASSERT_EQUAL(0x18, rx.frame().payload_len);
}

void test_mtu_23_splits_into_20_byte_notifications() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 23);
  request(emu, bms::tdt::kCmdCellsPack);
  TEST_ASSERT_EQUAL(3, link.count);  // 20 + 20 + 3
  TEST_ASSERT_EQUAL(20, link.sizes[0]);
  TEST_ASSERT_EQUAL(3, link.sizes[2]);
  bms::tdt::FrameReassembler rx;
  TEST_ASSERT_EQUAL(bms::tdt::FrameReassembler::kComplete, reassemble(link, rx));
}

// --- bms-protocol 3 and 8: the handshake and the link's lifetime ----------------------

void test_fff2_is_ignored_before_the_handshake() {
  FakeLink link;
  BmsEmu   emu(&link);
  emu.on_connect(0);
  emu.on_subscribe(true);
  request(emu, bms::tdt::kCmdCellsPack);
  TEST_ASSERT_EQUAL(0, link.count);
  TEST_ASSERT_EQUAL(1, emu.stats().ignored_before_hs);
  TEST_ASSERT_EQUAL_HEX8(0x00, emu.handshake_value());
}

void test_no_handshake_drops_at_4_s_and_writes_do_not_reset_it() {
  FakeLink link;
  BmsEmu   emu(&link);
  emu.on_connect(1000);
  request(emu, bms::tdt::kCmdCellsPack, 4000);  // an ignored write, late in the window
  emu.tick(4999);
  TEST_ASSERT_EQUAL(0, link.drops);
  emu.tick(5000);
  TEST_ASSERT_EQUAL(1, link.drops);
  TEST_ASSERT_FALSE(emu.connected());
  TEST_ASSERT_EQUAL(1, emu.stats().dropped_no_hs);
}

void test_a_wrong_handshake_is_refused_and_the_drop_still_comes() {
  FakeLink      link;
  BmsEmu        emu(&link);
  const uint8_t wrong[] = {'h', 'i', 'l', 'i', 'n', 'k'};
  emu.on_connect(0);
  emu.on_write(BmsChar::Handshake, wrong, sizeof(wrong), 10);
  TEST_ASSERT_EQUAL_HEX8(0x00, emu.handshake_value());
  TEST_ASSERT_EQUAL(1, emu.stats().handshake_rejected);
  emu.tick(kBmsNoHandshakeDropMs);
  TEST_ASSERT_EQUAL(1, link.drops);
}

void test_after_the_handshake_fffa_reads_01_and_the_link_holds() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  TEST_ASSERT_EQUAL_HEX8(0x01, emu.handshake_value());
  emu.tick(60000);
  TEST_ASSERT_EQUAL(0, link.drops);
  TEST_ASSERT_TRUE(emu.connected());
}

void test_a_reconnect_needs_a_new_handshake() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  emu.on_disconnect();
  emu.on_connect(100);
  TEST_ASSERT_FALSE(emu.handshaken());
  TEST_ASSERT_EQUAL(23, emu.mtu());
}

void test_unknown_and_unsubscribed_requests_are_counted() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  uint8_t req7e[bms::tdt::kRequestLen];
  bms::tdt::build_request(bms::tdt::kCmdCellsPack, req7e, sizeof(req7e));
  req7e[0] = 0x7E;  // bms-protocol 4 - this unit never answers the 0x7E head
  emu.on_write(BmsChar::Tx, req7e, sizeof(req7e), 100);
  emu.tick(100);
  TEST_ASSERT_EQUAL(1, emu.stats().unknown_requests);
  emu.on_subscribe(false);
  request(emu, bms::tdt::kCmdCellsPack);
  TEST_ASSERT_EQUAL(0, link.count);
  TEST_ASSERT_EQUAL(1, emu.stats().unsubscribed);
}

// --- Faults ----------------------------------------------------------------------------

void test_bad_crc_is_caught_and_disarms_after_one() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  emu.arm(BmsFault::BadCrc, 1);
  request(emu, bms::tdt::kCmdCellsPack);
  bms::tdt::FrameReassembler rx;
  TEST_ASSERT_EQUAL(bms::tdt::FrameReassembler::kCrcError, reassemble(link, rx));
  TEST_ASSERT_EQUAL(BmsFault::None, emu.fault());

  FakeLink link2;
  BmsEmu   emu2(&link2);
  ready(emu2, 512);
  request(emu2, bms::tdt::kCmdCellsPack);  // and the capture itself was not touched
  bms::tdt::FrameReassembler rx2;
  TEST_ASSERT_EQUAL(bms::tdt::FrameReassembler::kComplete, reassemble(link2, rx2));
}

void test_bad_terminator_is_caught() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  emu.arm(BmsFault::BadTerm, 1);
  request(emu, bms::tdt::kCmdDeviceInfo);
  bms::tdt::FrameReassembler rx;
  TEST_ASSERT_EQUAL(bms::tdt::FrameReassembler::kBadTerminator, reassemble(link, rx));
}

void test_split_forces_20_byte_chunks_at_mtu_512() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  emu.arm(BmsFault::Split, 2);
  request(emu, bms::tdt::kCmdDeviceInfo);
  TEST_ASSERT_EQUAL(4, link.count);  // 71 bytes = 20 + 20 + 20 + 11
  bms::tdt::FrameReassembler rx;
  TEST_ASSERT_EQUAL(bms::tdt::FrameReassembler::kComplete, reassemble(link, rx));
  TEST_ASSERT_EQUAL(1, emu.fault_left());
}

void test_no_response_sends_nothing() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  emu.arm(BmsFault::NoResponse, 1);
  request(emu, bms::tdt::kCmdCellsPack);
  TEST_ASSERT_EQUAL(0, link.count);
  TEST_ASSERT_EQUAL(1, emu.stats().faults_applied);
  request(emu, bms::tdt::kCmdCellsPack);
  TEST_ASSERT_EQUAL(1, link.count);
}

void test_drop_mid_sends_20_bytes_then_drops() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  emu.arm(BmsFault::DropMid, 1);
  request(emu, bms::tdt::kCmdCellsPack);
  TEST_ASSERT_EQUAL(1, link.count);
  TEST_ASSERT_EQUAL(20, link.total);
  TEST_ASSERT_EQUAL(1, link.drops);
  TEST_ASSERT_FALSE(emu.connected());
  bms::tdt::FrameReassembler rx;
  TEST_ASSERT_EQUAL(bms::tdt::FrameReassembler::kIncomplete, reassemble(link, rx));
}

void test_a_refused_notification_is_counted() {
  FakeLink link;
  BmsEmu   emu(&link);
  ready(emu, 512);
  link.refuse = true;
  request(emu, bms::tdt::kCmdCellsPack);
  TEST_ASSERT_EQUAL(1, emu.stats().notify_failures);
  TEST_ASSERT_EQUAL(0, emu.stats().responses);
}

// --- The console command ---------------------------------------------------------------

void test_bms_on_uses_the_default_suffix_or_the_given_one() {
  FakeLink    link;
  BmsEmu      emu(&link);
  FakeControl ctl;
  Transcript  out;
  char        line[] = "bms on";
  TEST_ASSERT_TRUE(bms_command(g_argv, split(line), &emu, &ctl, &out));
  TEST_ASSERT_EQUAL_STRING("XDZN_001_BC2C", ctl.name);
  TEST_ASSERT_EQUAL_STRING("OK bms on, advertising XDZN_001_BC2C", out.last);

  char again[] = "bms on 49A1";
  bms_command(g_argv, split(again), &emu, &ctl, &out);
  TEST_ASSERT_EQUAL(0, std::strncmp(out.last, "ERR", 3));  // already on

  char off[] = "bms off";
  bms_command(g_argv, split(off), &emu, &ctl, &out);
  TEST_ASSERT_FALSE(ctl.on);
  char named[] = "bms on 49A1";
  bms_command(g_argv, split(named), &emu, &ctl, &out);
  TEST_ASSERT_EQUAL_STRING("XDZN_001_49A1", ctl.name);

  char ctl_off[] = "bms off";
  bms_command(g_argv, split(ctl_off), &emu, &ctl, &out);
  char too_long[] = "bms on 49A1X";
  bms_command(g_argv, split(too_long), &emu, &ctl, &out);
  TEST_ASSERT_EQUAL(0, std::strncmp(out.last, "ERR", 3));
  TEST_ASSERT_FALSE(ctl.on);
}

void test_bms_fault_arms_and_refuses_unknown_names() {
  FakeLink    link;
  BmsEmu      emu(&link);
  FakeControl ctl;
  Transcript  out;
  char        arm[] = "bms fault split 3";
  bms_command(g_argv, split(arm), &emu, &ctl, &out);
  TEST_ASSERT_EQUAL(BmsFault::Split, emu.fault());
  TEST_ASSERT_EQUAL(3, emu.fault_left());
  char bad[] = "bms fault frobnicate";
  bms_command(g_argv, split(bad), &emu, &ctl, &out);
  TEST_ASSERT_EQUAL(0, std::strncmp(out.last, "ERR", 3));
  char off[] = "bms fault off";
  bms_command(g_argv, split(off), &emu, &ctl, &out);
  TEST_ASSERT_EQUAL(BmsFault::None, emu.fault());
}

void test_bms_status_and_other_commands() {
  FakeLink    link;
  BmsEmu      emu(&link);
  FakeControl ctl;
  Transcript  out;
  char        st[] = "bms status";
  TEST_ASSERT_TRUE(bms_command(g_argv, split(st), &emu, &ctl, &out));
  TEST_ASSERT_EQUAL(0, std::strncmp(out.first, "OK bms off, no link", 19));
  TEST_ASSERT_EQUAL(3, out.count);
  char other[] = "radio";
  TEST_ASSERT_FALSE(bms_command(g_argv, split(other), &emu, &ctl, &out));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_8c_decodes_to_the_captured_values);
  RUN_TEST(test_92_decodes_to_the_captured_strings);
  RUN_TEST(test_8d_is_framed_and_its_crc_verifies);
  RUN_TEST(test_mtu_23_splits_into_20_byte_notifications);
  RUN_TEST(test_fff2_is_ignored_before_the_handshake);
  RUN_TEST(test_no_handshake_drops_at_4_s_and_writes_do_not_reset_it);
  RUN_TEST(test_a_wrong_handshake_is_refused_and_the_drop_still_comes);
  RUN_TEST(test_after_the_handshake_fffa_reads_01_and_the_link_holds);
  RUN_TEST(test_a_reconnect_needs_a_new_handshake);
  RUN_TEST(test_unknown_and_unsubscribed_requests_are_counted);
  RUN_TEST(test_bad_crc_is_caught_and_disarms_after_one);
  RUN_TEST(test_bad_terminator_is_caught);
  RUN_TEST(test_split_forces_20_byte_chunks_at_mtu_512);
  RUN_TEST(test_no_response_sends_nothing);
  RUN_TEST(test_drop_mid_sends_20_bytes_then_drops);
  RUN_TEST(test_a_refused_notification_is_counted);
  RUN_TEST(test_bms_on_uses_the_default_suffix_or_the_given_one);
  RUN_TEST(test_bms_fault_arms_and_refuses_unknown_names);
  RUN_TEST(test_bms_status_and_other_commands);
  return UNITY_END();
}
