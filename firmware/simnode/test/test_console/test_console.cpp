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
  static constexpr int kLines = 32;
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

void test_unbuilt_commands_say_which_task_brings_them() {
  Rig r;
  // The command-path commands still wait for ROLE_GATELINK.
  r.run("push f0 boot");
  TEST_ASSERT_TRUE(r.t.any_contains("BF-6"));
  r.run("ack f0 suppress");
  TEST_ASSERT_TRUE(r.t.any_contains("BF-6"));
  // A command-path fault names its task through the injector, not the parser.
  r.run("id add f0 ROLE_HEALTH");
  r.run("fault f0 ack_suppress");
  TEST_ASSERT_TRUE(r.t.any_contains("BF-6"));
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
  RUN_TEST(test_ping_forms);
  RUN_TEST(test_unbuilt_commands_say_which_task_brings_them);
  RUN_TEST(test_fault_command_arms_and_disarms);
  RUN_TEST(test_log_and_stats);
  RUN_TEST(test_blank_lines_crlf_and_overlong_lines);
  RUN_TEST(test_hex_byte_parsing);
  return UNITY_END();
}
