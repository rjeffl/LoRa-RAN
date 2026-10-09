// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GL3 - GateLink's configuration on microSD (Impl Plan 6.4, D49, PRD R-4.2c; spec 7.4,
// 8.11). The card is a fake that can be absent, pulled, or fail a write, because the
// honest answer to each of those is the half of the requirement a bench cannot repeat on
// demand.

#include <unity.h>

#include <cstring>
#include <string>

#include "config_store.h"

using namespace lran;
using namespace lran::config;
using gatelink::ConfigFile;
using gatelink::GateLinkConfig;

void setUp() {}
void tearDown() {}

namespace {

class FakeCard final : public ConfigFile {
 public:
  bool mount(bool remount) override {
    ++mounts;
    if (remount) ++remounts;
    return present;
  }
  size_t read(char* out, size_t cap) override {
    if (!present || !has_file || text.size() > cap) return 0;
    std::memcpy(out, text.data(), text.size());
    return text.size();
  }
  bool write(const char* t, size_t n) override {
    ++writes;
    if (!present || fail_writes) return false;
    text.assign(t, n);
    has_file = true;
    return true;
  }

  bool        present     = true;
  bool        has_file    = false;
  bool        fail_writes = false;
  std::string text;
  int         mounts   = 0;
  int         remounts = 0;
  int         writes   = 0;
};

schema::ConfigEntry set_entry(uint16_t id, PType t, uint32_t raw) {
  schema::ConfigEntry e;
  schema::entry_pack(&e, id, t, raw);
  return e;
}

constexpr uint16_t kRelayPulseMs     = 0x1000;
constexpr uint16_t kWatchdogTimeoutS = 0x1060;
constexpr uint16_t kFreqHz           = 0x0110;

}  // namespace

// The file is what an operator reads with the card in a laptop: names, not ids.
void test_format_writes_names_and_values() {
  const ParamDef* defs[2] = {&kGateLinkParams[0], &kGateLinkParams[1]};
  const Value     vals[2] = {700, -3};
  char            buf[128];
  const size_t    n = gatelink::format_config(defs, vals, 2, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("{\n  \"relay_pulse_ms\": 700,\n  \"relay_min_spacing_ms\": -3\n}\n",
                           std::string(buf, n).c_str());
  TEST_ASSERT_EQUAL_STRING("{}\n", std::string(buf, gatelink::format_config(defs, vals, 0, buf,
                                                                           sizeof(buf)))
                                       .c_str());
  // Too small is 0, never a truncated file.
  TEST_ASSERT_EQUAL_size_t(0, gatelink::format_config(defs, vals, 2, buf, 20));
}

void test_parse_reads_back_what_format_wrote() {
  GateLinkConfig        cfg(nullptr);
  gatelink::FileEntry   out[4];
  const char*           text = "{ \"relay_pulse_ms\" :700 ,\n\"watchdog_timeout_s\": 20 }\n";
  gatelink::ParseResult r = gatelink::parse_config(cfg.table(), text, std::strlen(text), out, 4);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_size_t(2, r.entries);
  TEST_ASSERT_EQUAL_UINT16(kRelayPulseMs, out[0].def->id);
  TEST_ASSERT_EQUAL_INT32(700, out[0].value);
  TEST_ASSERT_EQUAL_UINT16(kWatchdogTimeoutS, out[1].def->id);
}

// A name the table lacks is skipped and counted, so a file written by a later firmware
// still loads on this one.
void test_parse_skips_an_unknown_name() {
  GateLinkConfig        cfg(nullptr);
  gatelink::FileEntry   out[4];
  const char*           text = "{\"no_such_row\": 1, \"relay_pulse_ms\": 700}";
  gatelink::ParseResult r = gatelink::parse_config(cfg.table(), text, std::strlen(text), out, 4);
  TEST_ASSERT_TRUE(r.ok);
  TEST_ASSERT_EQUAL_size_t(1, r.entries);
  TEST_ASSERT_EQUAL_size_t(1, r.unknown);
}

// Half a file is a configuration nobody chose: any syntax error abandons all of it.
void test_parse_rejects_a_damaged_file() {
  GateLinkConfig      cfg(nullptr);
  gatelink::FileEntry out[4];
  const char* bad[] = {"",
                       "{\"relay_pulse_ms\": 700",
                       "{\"relay_pulse_ms\": 7.5}",
                       "{\"relay_pulse_ms\": 99999999999}",
                       "{\"relay_pulse_ms\": 700,}",
                       "{\"relay_pulse_ms\": 700} x",
                       "{\"relay\\u_ms\": 1}"};
  for (const char* t : bad) {
    TEST_ASSERT_FALSE_MESSAGE(gatelink::parse_config(cfg.table(), t, std::strlen(t), out, 4).ok, t);
  }
}

// Impl Plan 6.4 - a value on the card is the value after a reboot, clamped as a SET is.
void test_load_restores_the_card() {
  FakeCard card;
  card.has_file = true;
  card.text     = "{\"relay_pulse_ms\": 700, \"watchdog_timeout_s\": 1}";
  GateLinkConfig cfg(&card);
  cfg.persist().load(&cfg.store());
  TEST_ASSERT_EQUAL_INT32(700, cfg.store().effective(kRelayPulseMs));
  TEST_ASSERT_TRUE(cfg.store().is_override(kRelayPulseMs));
  // Impl Plan 5.2 - the floor holds even for a value typed into the file by hand.
  TEST_ASSERT_EQUAL_INT32(5, cfg.store().effective(kWatchdogTimeoutS));
  TEST_ASSERT_EQUAL_UINT32(2, cfg.persist().stats().restored);
  TEST_ASSERT_TRUE(cfg.persist().usable());
  TEST_ASSERT_FALSE(cfg.unpersisted());
}

// spec 12.4 step 2 - a PHY value in the file is not put back while 12.4.2 is unbuilt.
void test_load_refuses_a_phy_row() {
  FakeCard card;
  card.has_file = true;
  card.text     = "{\"freq_hz\": 915000000}";
  GateLinkConfig cfg(&card);
  cfg.persist().load(&cfg.store());
  TEST_ASSERT_EQUAL_INT32(917400000, cfg.store().effective(kFreqHz));
  TEST_ASSERT_EQUAL_UINT32(1, cfg.persist().stats().refused);
}

void test_load_with_a_damaged_file_runs_the_defaults() {
  FakeCard card;
  card.has_file = true;
  card.text     = "{\"relay_pulse_ms\": 700";
  GateLinkConfig cfg(&card);
  cfg.persist().load(&cfg.store());
  TEST_ASSERT_TRUE(cfg.persist().stats().file_corrupt);
  TEST_ASSERT_FALSE(cfg.store().is_override(kRelayPulseMs));
}

// A SET with a card writes the whole file, and the answer is PERSISTED.
void test_set_with_a_card_is_persisted() {
  FakeCard       card;
  GateLinkConfig cfg(&card);
  cfg.persist().load(&cfg.store());
  bool applied = false, persisted = false;
  (void)cfg.store().apply(set_entry(kRelayPulseMs, PType::U16, 700), &applied, &persisted);
  TEST_ASSERT_TRUE(applied);
  TEST_ASSERT_TRUE(persisted);
  TEST_ASSERT_EQUAL_STRING("{\n  \"relay_pulse_ms\": 700\n}\n", card.text.c_str());
  TEST_ASSERT_FALSE(cfg.unpersisted());
}

// Impl Plan 6.4's absent-card behaviour: defaults, the change applied to RAM, and an
// answer that says it was not saved.
void test_set_with_no_card_is_applied_not_persisted() {
  FakeCard card;
  card.present = false;
  GateLinkConfig cfg(&card);
  cfg.persist().load(&cfg.store());
  TEST_ASSERT_FALSE(cfg.persist().usable());
  TEST_ASSERT_FALSE(cfg.unpersisted());  // nothing overridden yet, so nothing unsaved
  bool applied = false, persisted = true;
  (void)cfg.store().apply(set_entry(kRelayPulseMs, PType::U16, 700), &applied, &persisted);
  TEST_ASSERT_TRUE(applied);
  TEST_ASSERT_FALSE(persisted);
  TEST_ASSERT_EQUAL_INT32(700, cfg.store().effective(kRelayPulseMs));
  TEST_ASSERT_TRUE(cfg.unpersisted());
  TEST_ASSERT_EQUAL(PersistStatus::AppliedNotPersisted, cfg.store().read_persist_status());
}

// A card pulled after boot is found by the write that fails. From then on nothing claims
// PERSISTED, including a later SET that would have written.
void test_a_failed_write_marks_the_store_dirty() {
  FakeCard       card;
  GateLinkConfig cfg(&card);
  cfg.persist().load(&cfg.store());
  card.present = false;  // pulled; nothing tells the firmware
  bool persisted = true;
  (void)cfg.store().apply(set_entry(kRelayPulseMs, PType::U16, 700), nullptr, &persisted);
  TEST_ASSERT_FALSE(persisted);
  TEST_ASSERT_TRUE(cfg.persist().dirty());
  TEST_ASSERT_TRUE(cfg.unpersisted());
  TEST_ASSERT_EQUAL_UINT32(1, cfg.persist().stats().write_failures);
}

// refresh() brings the card back with every override in RAM, including one set while
// it was absent, which the Store never offered to save().
void test_refresh_writes_what_ram_holds() {
  FakeCard card;
  card.present = false;
  GateLinkConfig cfg(&card);
  cfg.persist().load(&cfg.store());
  (void)cfg.store().apply(set_entry(kRelayPulseMs, PType::U16, 700), nullptr, nullptr);
  TEST_ASSERT_FALSE(cfg.persist().refresh());  // still absent

  card.present = true;
  TEST_ASSERT_TRUE(cfg.persist().refresh());
  TEST_ASSERT_EQUAL_STRING("{\n  \"relay_pulse_ms\": 700\n}\n", card.text.c_str());
  TEST_ASSERT_TRUE(cfg.persist().usable());
  TEST_ASSERT_FALSE(cfg.unpersisted());
  TEST_ASSERT_EQUAL(PersistStatus::Persisted, cfg.store().read_persist_status());

  // And it is a no-op while the card holds what RAM does.
  const int writes = card.writes;
  TEST_ASSERT_TRUE(cfg.persist().refresh());
  TEST_ASSERT_EQUAL_INT(writes, card.writes);
}

// D52 - RESTORE_DEFAULTS empties the file.
void test_restore_defaults_empties_the_file() {
  FakeCard       card;
  GateLinkConfig cfg(&card);
  cfg.persist().load(&cfg.store());
  (void)cfg.store().apply(set_entry(kRelayPulseMs, PType::U16, 700), nullptr, nullptr);
  TEST_ASSERT_TRUE(cfg.store().restore_defaults());
  TEST_ASSERT_EQUAL_STRING("{}\n", card.text.c_str());
  TEST_ASSERT_FALSE(cfg.unpersisted());
}

// The longest file fits the buffer: every non-PHY row overridden at its widest value.
void test_every_row_fits_the_file() {
  FakeCard       card;
  GateLinkConfig cfg(&card);
  cfg.persist().load(&cfg.store());
  const Table& t = cfg.table();
  for (size_t i = 0; i < t.size(); ++i) {
    const ParamDef* d = t.at(i);
    if (d->access != Access::ReadWrite) continue;
    const uint32_t raw = d->min < 0 ? static_cast<uint32_t>(d->min) : static_cast<uint32_t>(d->max);
    (void)cfg.store().apply(set_entry(d->id, d->type, raw), nullptr, nullptr);
  }
  TEST_ASSERT_FALSE(cfg.persist().dirty());
  TEST_ASSERT_TRUE(card.text.size() < gatelink::kConfigFileCap / 2);

  GateLinkConfig again(&card);
  again.persist().load(&again.store());
  TEST_ASSERT_EQUAL_UINT32(0, again.persist().stats().refused);
  for (size_t i = 0; i < t.size(); ++i) {
    const ParamDef* d = t.at(i);
    TEST_ASSERT_EQUAL_INT32(cfg.store().effective(d->id), again.store().effective(d->id));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_format_writes_names_and_values);
  RUN_TEST(test_parse_reads_back_what_format_wrote);
  RUN_TEST(test_parse_skips_an_unknown_name);
  RUN_TEST(test_parse_rejects_a_damaged_file);
  RUN_TEST(test_load_restores_the_card);
  RUN_TEST(test_load_refuses_a_phy_row);
  RUN_TEST(test_load_with_a_damaged_file_runs_the_defaults);
  RUN_TEST(test_set_with_a_card_is_persisted);
  RUN_TEST(test_set_with_no_card_is_applied_not_persisted);
  RUN_TEST(test_a_failed_write_marks_the_store_dirty);
  RUN_TEST(test_refresh_writes_what_ram_holds);
  RUN_TEST(test_restore_defaults_empties_the_file);
  RUN_TEST(test_every_row_fits_the_file);
  return UNITY_END();
}
