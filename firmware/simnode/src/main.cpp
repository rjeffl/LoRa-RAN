// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// lran-simnode - bench nodes 0xF0-0xF3. Boot, banner, default identities, and the loop
// that drives the console, the protocol engine and the radio. Task BF-2; Impl Plan 10.
//
// THE ONLY TRANSLATION UNIT THAT INCLUDES secrets.h, and it takes LRAN_MASTER_KEY and
// nothing else. The key is never printed.

#include <Arduino.h>
#include <esp_system.h>

#include <cstring>

#include "console.h"
#include "fault.h"
#include "identity.h"
#include "lran/link/radio_config.h"
#include "mbedtls_mac.h"
#include "node.h"
#include "oled_page.h"
#include "profiles.h"
#include "radio.h"
#include "ui.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h not found. Run `cp secrets.h.example secrets.h` at the repo root and fill it in. The simnode reads LRAN_MASTER_KEY only. Never commit the copy."
#endif

#if !defined(LRAN_PROFILE_HELTEC) && !defined(LRAN_PROFILE_XIAO_WIO_KIT)
#error "No hardware profile - build env simnode-heltec or simnode-xiao-wio (Impl Plan 10.8)"
#endif

namespace {

class SerialSink final : public simnode::Sink {
 public:
  void line(const char* text) override { Serial.println(text); }
};

// `radio` - what the driver saw. `stats` counts frames an identity queued; this counts
// TX_DONE, which is the only evidence on this board that a frame reached the air.
bool radio_command(char** argv, int argc, simnode::Sink* out) {
  if (std::strcmp(argv[0], "radio") != 0) return false;
  if (argc != 1) {
    simnode::sink_printf(out, "ERR usage: radio");
    return true;
  }
  const simnode::RadioStats& r = simnode::radio_stats();
  simnode::sink_printf(out, "OK radio %s", simnode::radio_ready() ? "up" : "DOWN");
  simnode::sink_printf(out, "  tx_frames %lu tx_errors %lu tx_timeouts %lu tx_forced %lu",
                       static_cast<unsigned long>(r.tx_frames), static_cast<unsigned long>(r.tx_errors),
                       static_cast<unsigned long>(r.tx_timeouts),
                       static_cast<unsigned long>(r.tx_forced));
  simnode::sink_printf(out, "  cad_errors %lu cad_deferred %lu rx_driver_errors %lu begin_failures %lu",
                       static_cast<unsigned long>(r.cad_errors),
                       static_cast<unsigned long>(r.cad_deferred),
                       static_cast<unsigned long>(r.rx_driver_errors),
                       static_cast<unsigned long>(r.begin_failures));
  return true;
}

SerialSink               g_sink;
lran::esp32::MbedtlsMac  g_mac;
lran::esp32::MbedtlsKdf  g_kdf;
simnode::IdentityTable   g_ids;
simnode::Outbox          g_outbox;
simnode::Node            g_node(&g_ids, &g_outbox, &g_mac, &g_sink);
simnode::FaultInjector   g_faults(&g_ids, &g_outbox, &g_node, &g_mac, &g_sink);
simnode::Console         g_console(&g_node, &g_ids, &g_faults, &g_sink, radio_command);

uint32_t random_u32() { return esp_random(); }

// The page is rebuilt this often and redrawn only when its text changed. A redraw is about
// 1 KB over I2C, tens of milliseconds, taken in loop(): well inside radio.cpp's 500 ms CAD
// deadline, and the DIO1 flag is latched by its ISR, so a frame arriving meanwhile waits
// rather than being lost. Ages move once a second, so in practice the panel redraws at 1 Hz.
constexpr uint32_t kUiPollMs = 200;

simnode::PageLines g_page_shown;
uint32_t           g_ui_last_ms = 0;

void ui_service(uint32_t now) {
  if (now - g_ui_last_ms < kUiPollMs) return;
  g_ui_last_ms = now;
  const simnode::PageLines page = simnode::build_page(
      simnode::take_snapshot(g_ids, g_faults, g_node, simnode::radio_ready(), now));
  // Byte comparison of a local, fixed-layout struct - it never leaves this board.
  if (std::memcmp(&page, &g_page_shown, sizeof(page)) == 0) return;
  g_page_shown = page;
  simnode::ui_render(page);
}

// The template's key is 32 zero bytes and builds, so CI needs no secret. A board flashed
// with it derives keys no bridge holding a real key shares, and says so.
bool master_key_is_placeholder(const uint8_t* key) {
  for (size_t i = 0; i < lran::kMasterKeyLen; ++i) {
    if (key[i] != 0) return false;
  }
  return true;
}

void add_default(uint8_t id, simnode::Role role) {
  if (g_ids.add(id, role) == simnode::AddResult::Ok) {
    const simnode::Identity* e = g_ids.find(id);
    Serial.printf("id %02x %s ctx 0x%08lx\n", id, simnode::role_name(role),
                  static_cast<unsigned long>(e->ctx_id));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);  // the XIAO's USB CDC enumerates late; the banner is the useful part of a log

  // tools/checks/spec_citation_version.py reads the next line.
  Serial.println(F("LRAN simnode - bench nodes 0xF0-0xF3"));
  Serial.println(F("Binding spec: LRAN-Protocol-Specification v0.11 (ver = 2)"));
  Serial.print(F("Board: "));
  Serial.println(simnode::kBoardName);

  {
    const uint8_t master[] = LRAN_MASTER_KEY;
    static_assert(sizeof(master) == lran::kMasterKeyLen,
                  "secrets.h: LRAN_MASTER_KEY must be 32 bytes (spec 9.1)");
    if (master_key_is_placeholder(master)) {
      Serial.println(F("*** LRAN_MASTER_KEY IS THE ALL-ZERO PLACEHOLDER ***"));
      Serial.println(F("*** Keys will not match a provisioned bridge. Fill in secrets.h. ***"));
    }
    g_ids.init(master, &g_kdf, random_u32);
  }

  // Impl Plan 10.8.1's assignment, with ROLE_RANGE standing in until ROLE_FAULT (BF-8) and
  // ROLE_GATELINK (BF-6) exist. Nothing persists: a reboot is a new context for every
  // identity, which is what a node reboot is.
#if defined(LRAN_PROFILE_HELTEC)
  add_default(lran::kNodeSim0, simnode::Role::Range);
  add_default(lran::kNodeSim2, simnode::Role::Health);
#else
  add_default(lran::kNodeSim1, simnode::Role::Range);
#endif

  if (simnode::ui_begin(simnode::kPanel)) {
    Serial.println(F("OLED: up"));
  } else {
    Serial.println(F("OLED: no ACK - check Vext on a Heltec, the expansion board on a XIAO; continuing without it"));
  }

  simnode::radio_start(simnode::kRadio, lran::link::kPhy, &g_sink);
  Serial.println(F("B0: identities, console, faults, OLED; ROLE_RANGE and ROLE_HEALTH. Type 'help'."));
}

void loop() {
  const uint32_t now = millis();
  while (Serial.available() > 0) {
    g_console.feed(static_cast<char>(Serial.read()), now);
  }
  g_node.tick(now);
  g_faults.tick(now);
  simnode::radio_service(&g_node, &g_outbox, now);
  ui_service(now);
  delay(1);  // spec 12.3 backoffs are milliseconds; nothing here needs a finer loop
}
