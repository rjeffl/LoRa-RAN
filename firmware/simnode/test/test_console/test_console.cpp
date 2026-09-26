// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-4 - the serial console (Impl Plan 10.4). Every command answers OK or ERR, so a simctl
// script can tell a refused command from a silent one.

#include <unity.h>

#include <cstring>

#include "console.h"
#include "identity.h"
#include "lran/lran.h"
#include "node.h"
#include "refimpl_mac.h"
#include "test_key.h"

using namespace simnode;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefKdf g_kdf;
refimpl::RefMac g_mac;

uint32_t g_next_random = 0x9000;
uint32_t counting_random() { return ++g_next_random; }

class Transcript final : public Sink {
 public:
  void line(const char* text) override {
    if (count_ < kLines) std::strncpy(lines_[count_++], text, kWidth - 1);
  }
  bool any_starts_with(const char* prefix) const {
    for (int i = 0; i < count_; ++i) {
      if (std::strncmp(lines_[i], prefix, std::strlen(prefix)) == 0) return true;
    }
    return false;
  }
  bool any_contains(const char* s) const {
    for (int i = 0; i < count_; ++i) {
      if (std::strstr(lines_[i], s) != nullptr) return true;
    }
    return false;
  }
  const char* first() const { return count_ > 0 ? lines_[0] : ""; }
  void        clear() {
    count_ = 0;
    std::memset(lines_, 0, sizeof(lines_));
  }

 private:
  // MUST EXCEED kFaultCatalogueLen + 1, because `fault list` prints a header and every
  // row. At 32 it exactly matched the catalogue and BF-21's ctx_reject pushed the last
  // row off the end, which reads as a missing fault rather than as a full buffer.
  static constexpr int kLines = 64;
  static constexpr int kWidth = 200;
  char                 lines_[kLines][kWidth] = {};
  int                  count_                 = 0;
};

struct Rig {
  IdentityTable ids;
  Outbox        out;
  Transcript    t;
  Node          node{&ids, &out, &g_mac, &t};
  FaultInjector faults{&ids, &out, &node, &g_mac, &t};
  Console       console{&node, &ids, &faults, &t};
  Rig() { ids.init(lran_test::kTestMasterKey, &g_kdf, counting_random); }

  void run(const char* line) {
    t.clear();
    for (const char* p = line; *p != '\0'; ++p) console.feed(*p, 1000);
    console.feed('\n', 1000);
  }
};

}  // namespace

void test_id_add_list_and_del() {
  Rig r;
  r.run("id add f1 ROLE_HEALTH");
  TEST_ASSERT_EQUAL_STRING("OK id f1 added as ROLE_HEALTH", r.t.first());
  TEST_ASSERT_NOT_NULL(r.ids.find(0xF1));

  r.run("id add 0xF3 ROLE_RANGE");
  TEST_ASSERT_TRUE(r.t.any_starts_with("OK"));

  r.run("id list");
  TEST_ASSERT_EQUAL_STRING("OK 2 identities", r.t.first());
  TEST_ASSERT_TRUE(r.t.any_contains("id f1 ROLE_HEALTH enabled ver 2"));
  TEST_ASSERT_TRUE(r.t.any_contains("id f3 ROLE_RANGE"));

  r.run("id del f1");
  TEST_ASSERT_EQUAL_STRING("OK id f1 removed", r.t.first());
  TEST_ASSERT_NULL(r.ids.find(0xF1));
}

void test_bad_ids_and_roles_are_refused_by_name() {
  Rig r;
  r.run("id add 01 ROLE_RANGE");
  TEST_ASSERT_TRUE(r.t.any_contains("ERR id add 01: id must be f0-f3"));
  r.run("id add f0 range");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR bad role 'range'"));
  r.run("id add f0");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR usage"));
  r.run("id del f2");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR no identity"));
}

void test_enable_disable_ver_and_ctx() {
  Rig r;
  r.run("id add f0 ROLE_RANGE");
  const Identity* e = r.ids.find(0xF0);

  r.run("disable f0");
  TEST_ASSERT_EQUAL_STRING("OK id f0 disabled", r.t.first());
  TEST_ASSERT_FALSE(e->enabled);
  r.run("enable f0");
  TEST_ASSERT_TRUE(e->enabled);

  r.run("ver f0 1");
  TEST_ASSERT_EQUAL_STRING("OK id f0 announces ver 1", r.t.first());
  TEST_ASSERT_EQUAL_UINT8(1, e->proto_ver);

  const CtxId before = e->ctx_id;
  r.run("ctx f0");
  TEST_ASSERT_EQUAL_UINT32(before, e->ctx_id);
  r.run("ctx f0 new");
  TEST_ASSERT_TRUE(r.t.any_starts_with("OK id f0 ctx 0x"));
  TEST_ASSERT_NOT_EQUAL(before, e->ctx_id);
  r.run("ctx f0 old");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR usage"));
}

// `reboot <hex> [cause]` is host code; `reboot` and `reboot panic` reset the chip, so they go
// to the board hook, and a console without one refuses them rather than rebooting one identity.
void test_reboot_forms() {
  Rig r;
  r.run("id add f1 ROLE_GATELINK");
  const CtxId before = r.ids.find(0xF1)->ctx_id;

  r.run("reboot f1 WATCHDOG");
  TEST_ASSERT_TRUE(r.t.any_starts_with("OK id f1 rebooted, ctx 0x"));
  TEST_ASSERT_TRUE(r.t.any_contains("BOOT WATCHDOG -> 00"));
  TEST_ASSERT_NOT_EQUAL(before, r.ids.find(0xF1)->ctx_id);
  TEST_ASSERT_EQUAL_size_t(2, r.out.size());  // STATUS, then the BOOT event

  r.run("reboot f1");
  TEST_ASSERT_TRUE(r.t.any_contains("BOOT SOFTWARE -> 00"));
  r.run("reboot f1 watchdog");  // exact spec 8.14 token
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR bad reset cause 'watchdog'"));
  r.run("reboot f2");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR reboot f2:"));
  r.run("reboot");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR unknown command 'reboot'"));
  r.run("reboot panic");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR unknown command 'reboot'"));
}

// Impl Plan 10.4's ping, plus `to <hex>`.
void test_ping_forms() {
  Rig r;
  r.run("id add f0 ROLE_RANGE");

  r.run("ping f0 202 pattern");
  TEST_ASSERT_TRUE(r.t.any_starts_with("OK ping f0 -> 00 seq 1, n 202, 1 frame(s)"));
  TEST_ASSERT_EQUAL_size_t(1, r.out.size());
  r.node.tick(1000 + kDefaultPingTimeoutMs);  // clear the pending echo

  r.run("ping f0 40 pattern frag to f2");  // bare frag: spec 6.6.2's 14-byte chunk
  TEST_ASSERT_TRUE(r.t.any_contains("-> f2 seq 2, n 40, 3 frame(s)"));
  r.node.tick(1000 + 2 * kDefaultPingTimeoutMs);

  r.run("ping f0 40 frag 20");
  TEST_ASSERT_TRUE(r.t.any_contains("n 40, 3 frame(s)"));

  r.run("ping f0 40 sideways");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR unexpected 'sideways'"));
  r.run("ping f1 4");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR ping f1: no such identity"));
}

// BF-6's four commands. Tokens are exact: a spec 8.7 or 8.9 name in its own spelling, or ERR.
void test_gatelink_commands() {
  Rig r;
  r.run("push f0");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR push f0: no such identity"));
  r.run("id add f0 ROLE_HEALTH");
  r.run("push f0");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR push f0: needs ROLE_GATELINK"));
  r.run("fault f0 ack_suppress");
  TEST_ASSERT_TRUE(r.t.any_contains("needs ROLE_GATELINK"));

  r.run("id add f1 ROLE_GATELINK");
  Identity* e = r.ids.find(0xF1);
  r.run("push f1");
  TEST_ASSERT_EQUAL_STRING("OK push f1 -> 00 schema 0xFE DEBUG_SYNTHETIC", r.t.first());
  r.run("push f1 GATE_STATE_CHANGE");
  TEST_ASSERT_EQUAL_STRING("OK push f1 -> 00 schema 0xFE GATE_STATE_CHANGE", r.t.first());
  r.run("push f1 boot");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR bad reason 'boot'"));

  r.run("event f1 again");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR event f1: no earlier event to repeat"));
  r.run("event f1 VEHICLE_DETECTED");
  TEST_ASSERT_EQUAL_STRING("OK event f1 -> 00 event_id 1", r.t.first());
  r.run("event f1 again");
  TEST_ASSERT_EQUAL_STRING("OK event f1 -> 00 event_id 1 (repeat)", r.t.first());
  r.run("event f1 follow");
  TEST_ASSERT_EQUAL_STRING("OK event f1 -> 00 event_id 1 (follow-up)", r.t.first());
  r.run("event f1 vehicle");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR bad event type 'vehicle'"));

  r.run("ack f0 delay 5");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR ack f0: needs ROLE_GATELINK"));
  r.run("ack f1 delay 250");
  TEST_ASSERT_TRUE(r.t.any_starts_with("OK ack f1 delay 250 ms"));
  TEST_ASSERT_EQUAL_UINT32(250, e->gl.ack_delay_ms);
  r.run("ack f1 suppress 3");
  TEST_ASSERT_TRUE(r.t.any_starts_with("OK fault f1 ack_suppress"));
  TEST_ASSERT_EQUAL_UINT16(3, e->gl.ack_suppress_left);
  r.run("ack f1 dup");
  TEST_ASSERT_EQUAL_UINT16(1, e->gl.ack_dup_left);
  r.run("id list");
  TEST_ASSERT_TRUE(r.t.any_contains("gatelink ack_delay 250 ms"));
  r.run("ack f1 normal");
  TEST_ASSERT_EQUAL_STRING("OK ack f1 normal", r.t.first());
  TEST_ASSERT_EQUAL_UINT32(0, e->gl.ack_delay_ms);
  TEST_ASSERT_EQUAL_UINT16(0, e->gl.ack_suppress_left);
  TEST_ASSERT_EQUAL_UINT16(0, e->gl.ack_dup_left);
  r.run("ack f1 sideways");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR usage: ack"));

  r.run("field f1 batt_mv 12100");
  TEST_ASSERT_EQUAL_STRING("OK field f1 batt_mv = 12100", r.t.first());
  TEST_ASSERT_EQUAL_UINT16(12100, e->gl.status.batt_mv);
  r.run("field f1 load_ma na");
  TEST_ASSERT_EQUAL_INT16(INT16_MIN, e->gl.status.load_ma);
  r.run("field f1 gate_state na");
  TEST_ASSERT_TRUE(r.t.any_contains("no not-available sentinel"));
  r.run("field f1 batt_mv 70000");
  TEST_ASSERT_TRUE(r.t.any_contains("out of range"));
  r.run("field f1 nope 1");
  TEST_ASSERT_TRUE(r.t.any_contains("unknown field"));
  r.run("field f1 list");
  TEST_ASSERT_TRUE(r.t.any_contains("batt_mv=12100"));
  TEST_ASSERT_TRUE(r.t.any_contains("node_flags="));
  r.run("field f1 reset");
  TEST_ASSERT_EQUAL_UINT16(13300, e->gl.status.batt_mv);

  r.run("frobnicate");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR unknown command 'frobnicate'"));
}

void test_fault_command_arms_and_disarms() {
  Rig r;
  r.run("id add f0 ROLE_HEALTH");

  r.run("fault list");
  TEST_ASSERT_TRUE(r.t.any_starts_with("OK"));
  TEST_ASSERT_TRUE(r.t.any_contains("single_frame_interleave"));
  TEST_ASSERT_TRUE(r.t.any_contains("bad_phy_crc"));

  r.run("fault f0 runt");  // one-shot, fires immediately
  TEST_ASSERT_TRUE(r.t.any_contains("runt armed"));
  TEST_ASSERT_EQUAL_size_t(1, r.out.size());

  r.run("fault f0 off");
  TEST_ASSERT_TRUE(r.t.any_starts_with("OK fault f0"));

  r.run("fault f0 nonsense");
  TEST_ASSERT_TRUE(r.t.any_contains("unknown fault"));

  r.run("fault f0 bad_phy_crc");
  TEST_ASSERT_TRUE(r.t.any_contains("cannot be injected"));

  r.run("fault f9 runt");
  TEST_ASSERT_TRUE(r.t.any_contains("no such identity"));
}

void test_log_and_stats() {
  Rig r;
  r.run("log debug");
  TEST_ASSERT_EQUAL_STRING("OK log debug", r.t.first());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LogLevel::Debug), static_cast<int>(r.node.log_level()));
  r.run("log loud");
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR"));

  r.run("id add f2 ROLE_HEALTH");
  r.run("stats f2");
  TEST_ASSERT_EQUAL_STRING("OK stats f2", r.t.first());
  TEST_ASSERT_TRUE(r.t.any_contains("rx_not_addressed 0"));
}

void test_blank_lines_crlf_and_overlong_lines() {
  Rig r;
  r.run("");
  TEST_ASSERT_EQUAL_STRING("", r.t.first());

  r.t.clear();
  for (const char* p = "id list\r\n"; *p != '\0'; ++p) r.console.feed(*p, 0);
  TEST_ASSERT_EQUAL_STRING("OK 0 identities", r.t.first());

  char longline[300];
  std::memset(longline, 'x', sizeof(longline) - 1);
  longline[sizeof(longline) - 1] = '\0';
  r.run(longline);
  TEST_ASSERT_TRUE(r.t.any_starts_with("ERR line longer than"));
  r.run("id list");  // and the console recovers
  TEST_ASSERT_TRUE(r.t.any_starts_with("OK"));
}

void test_hex_byte_parsing() {
  uint8_t v = 0;
  TEST_ASSERT_TRUE(parse_hex_byte("f0", &v));
  TEST_ASSERT_EQUAL_UINT8(0xF0, v);
  TEST_ASSERT_TRUE(parse_hex_byte("0xF3", &v));
  TEST_ASSERT_EQUAL_UINT8(0xF3, v);
  TEST_ASSERT_FALSE(parse_hex_byte("100", &v));
  TEST_ASSERT_FALSE(parse_hex_byte("f0z", &v));
  TEST_ASSERT_FALSE(parse_hex_byte("", &v));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_id_add_list_and_del);
  RUN_TEST(test_bad_ids_and_roles_are_refused_by_name);
  RUN_TEST(test_enable_disable_ver_and_ctx);
  RUN_TEST(test_reboot_forms);
  RUN_TEST(test_ping_forms);
  RUN_TEST(test_gatelink_commands);
  RUN_TEST(test_fault_command_arms_and_disarms);
  RUN_TEST(test_log_and_stats);
  RUN_TEST(test_blank_lines_crlf_and_overlong_lines);
  RUN_TEST(test_hex_byte_parsing);
  return UNITY_END();
}
