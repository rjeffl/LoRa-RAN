// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// lran-simnode - bench nodes 0xF0-0xF3. Boot, banner, default identities, and the loop
// that drives the console, the protocol engine and the radio. Task BF-2; Impl Plan 10.
//
// THE ONLY TRANSLATION UNIT THAT INCLUDES secrets.h, and it takes LRAN_MASTER_KEY and
// nothing else. The key is never printed.

#include <Arduino.h>
#include <bootloader_random.h>
#include <esp_attr.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <soc/reset_reasons.h>

#include <cstring>

#include "console.h"
#include "gatelink.h"
#include "fault.h"
#include "identity.h"
#include "lran/link/radio_config.h"
#include "mbedtls_mac.h"
#include "node.h"
#include "nvs_blob.h"
#include "oled_page.h"
#include "phy_trial.h"
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

// spec 12.4.2 - the board's PHY group, held in NVS. Declared here because the `phy` command
// below reads it.
simnode::NvsBlob    g_phy_blob;
simnode::PhyPersist g_phy_persist(&g_phy_blob);
simnode::PhyTrial   g_phy(&g_phy_persist);

// `phy [reset]` - the group, the trial and the store. `reset` puts D1's group back and
// erases the blob, so a board left on a group the bridge does not share recovers here.
bool phy_command(char** argv, int argc, simnode::Sink* out) {
  if (argc == 2 && std::strcmp(argv[1], "reset") == 0) {
    if (!g_phy.reset_to_defaults()) {
      simnode::sink_printf(out, "ERR phy: no usable store");
      return true;
    }
    simnode::sink_printf(out, "OK phy reset - D1's group, store erased, retuning");
    return true;
  }
  if (argc != 1) {
    simnode::sink_printf(out, "ERR usage: phy [reset]");
    return true;
  }
  const simnode::PhyGroup    g = g_phy.group();
  const simnode::PhyGroup    c = g_phy.committed();
  const simnode::PhyTrialStats& s = g_phy.stats();
  simnode::sink_printf(out, "OK phy %s, store %s, window %lu ms, accepted 0x%02x", 
                       simnode::phy_state_name(g_phy.state()),
                       g_phy.writable() ? "nvs" : "NONE (PHY rows READ_ONLY)",
                       static_cast<unsigned long>(g_phy.window_left_ms(millis())),
                       static_cast<unsigned>(g_phy.accepted_mask()));
  simnode::sink_printf(out, "  effective %ld Hz SF%ld BW %ld CR 4/%ld %ld dBm trial %ld s",
                       static_cast<long>(g.v[0]), static_cast<long>(g.v[1]),
                       static_cast<long>(g.v[2]), static_cast<long>(g.v[3]),
                       static_cast<long>(g.v[4]), static_cast<long>(g.v[5]));
  simnode::sink_printf(out, "  committed %ld Hz SF%ld BW %ld CR 4/%ld %ld dBm trial %ld s",
                       static_cast<long>(c.v[0]), static_cast<long>(c.v[1]),
                       static_cast<long>(c.v[2]), static_cast<long>(c.v[3]),
                       static_cast<long>(c.v[4]), static_cast<long>(c.v[5]));
  simnode::sink_printf(out, "  trials %lu committed %lu reverted %lu abandoned %lu commit_failed %lu",
                       static_cast<unsigned long>(s.trials), static_cast<unsigned long>(s.committed),
                       static_cast<unsigned long>(s.reverted), static_cast<unsigned long>(s.abandoned),
                       static_cast<unsigned long>(s.commit_failed));
  return true;
}

// spec 8.14 - a REBOOT the node accepted and any other software restart both read
// ESP_RST_SW, so the node records its intent where a software reset leaves it. RTC_NOINIT
// holds garbage after a power-on, which is why the value is a pattern and not a flag, and
// why it is trusted only under ESP_RST_SW.
RTC_NOINIT_ATTR uint32_t g_reboot_marker;
constexpr uint32_t       kRebootMarker = 0x5245424Fu;  // "REBO"

lran::ResetCause reset_cause_at_boot() {
  const bool directed = g_reboot_marker == kRebootMarker;
  g_reboot_marker     = 0;
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return lran::ResetCause::PowerOn;
    case ESP_RST_EXT:      return lran::ResetCause::External;
    case ESP_RST_SW:       return directed ? lran::ResetCause::RebootCommand
                                           : lran::ResetCause::Software;
    case ESP_RST_PANIC:    return lran::ResetCause::Panic;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return lran::ResetCause::Watchdog;
    case ESP_RST_BROWNOUT: return lran::ResetCause::Brownout;
    default:               break;
  }
  // ESP-IDF 4.4 has no ESP_RST_USB and reports a reset from the USB peripheral as
  // ESP_RST_UNKNOWN. Both boards are ESP32-S3, and a host opening the port resets them this
  // way (0x15 on the bench, 2026-09-26). Spec 8.14 names it EXTERNAL.
  switch (esp_rom_get_reset_reason(0)) {
    case RESET_REASON_CORE_USB_UART:
    case RESET_REASON_CORE_USB_JTAG: return lran::ResetCause::External;
    default:                         return lran::ResetCause::Unknown;
  }
}

// `reboot` and `reboot panic` - the whole board, through the chip. The console handles
// `reboot <hex>`, one identity simulated, and passes these two here.
bool reboot_command(char** argv, int argc, simnode::Sink* out) {
  if (argc == 2 && std::strcmp(argv[1], "panic") == 0) {
    simnode::sink_printf(out, "OK reboot panic - abort(), expect PANIC");
    Serial.flush();
    abort();
  }
  if (argc != 1) {
    simnode::sink_printf(out, "ERR usage: reboot [<hex> [cause]] | reboot panic");
    return true;
  }
  simnode::sink_printf(out, "OK reboot - esp_restart(), expect SOFTWARE");
  Serial.flush();
  esp_restart();
}

// `radio` - what the driver saw. `stats` counts frames an identity queued; this counts
// TX_DONE, which is the only evidence on this board that a frame reached the air. Also
// dispatches `phy` and `reboot`, because the console takes one board hook.
bool radio_command(char** argv, int argc, simnode::Sink* out) {
  if (std::strcmp(argv[0], "phy") == 0) return phy_command(argv, argc, out);
  if (std::strcmp(argv[0], "reboot") == 0) return reboot_command(argv, argc, out);
  if (std::strcmp(argv[0], "radio") != 0) return false;
  if (argc != 1) {
    simnode::sink_printf(out, "ERR usage: radio");
    return true;
  }
  const simnode::RadioStats& r = simnode::radio_stats();
  const lran::link::PhyConfig& p = simnode::radio_phy();
  simnode::sink_printf(out, "OK radio %s, %lu Hz SF%u CR 4/%u %d dBm",
                       simnode::radio_ready() ? "up" : "DOWN", static_cast<unsigned long>(p.freq_hz),
                       static_cast<unsigned>(p.sf), static_cast<unsigned>(p.cr_denom),
                       static_cast<int>(p.conducted_dbm));
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

// spec 12.4.2 step 3 - hands an owed retune to the radio, and opens the window once the
// radio reports it applied. radio.cpp waits for its outbox to drain first, so the
// CONFIG_ACK that caused the retune goes out on the old settings.
bool g_retune_requested = false;

void phy_service(uint32_t now) {
  if (g_phy.retune_due() && !g_retune_requested) {
    lran::link::PhyConfig next = simnode::radio_phy();
    if (!simnode::phy_config_from(g_phy.group(), simnode::radio_phy(), &next)) {
      // Unreachable while the table caps tx_power_dbm at D33's ceiling. The radio keeps
      // its settings; the window still opens, and silence reverts it.
      g_sink.line("phy: group breaks D33's EIRP ceiling - radio NOT retuned");
    }
    simnode::radio_request_phy(next);
    g_retune_requested = true;
  }
  if (g_retune_requested && simnode::radio_take_retuned()) {
    g_retune_requested = false;
    g_phy.on_retuned(now);
    simnode::sink_printf(&g_sink, "phy: retuned, board %s", simnode::phy_state_name(g_phy.state()));
  }
}

// spec 8.1 - REBOOT acknowledges, then resets, and only once the ACK is on the air: an empty
// outbox and an idle radio. A radio that is down will send nothing, so it does not hold the
// reset. The marker tells the next boot this was the REBOOT and not another restart.
void restart_service() {
  if (!g_node.restart_owed()) return;
  if (simnode::radio_ready() && (g_outbox.size() != 0 || !simnode::radio_tx_idle())) return;
  Serial.println(F("REBOOT: ACK on the air, restarting"));
  Serial.flush();
  g_reboot_marker = kRebootMarker;
  esp_restart();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);  // the XIAO's USB CDC enumerates late; the banner is the useful part of a log

  // tools/checks/spec_citation_version.py reads the next line.
  Serial.println(F("LRAN simnode - bench nodes 0xF0-0xF3"));
  Serial.println(F("Binding spec: LRAN-Protocol-Specification v0.16 (ver = 2)"));
  Serial.print(F("Board: "));
  Serial.println(simnode::kBoardName);

  const lran::ResetCause cause = reset_cause_at_boot();
  const uint16_t         boots = simnode::nvs_count_boot();
  Serial.printf("Reset cause: %s (esp_reset_reason %d), boot_count %u\n",
                simnode::reset_cause_name(cause), static_cast<int>(esp_reset_reason()),
                static_cast<unsigned>(boots));

  // spec 10.1 - ctx_id from a true entropy source. The simnode runs neither WiFi nor
  // Bluetooth, so esp_random() is pseudo-random until this turns the SAR ADC noise source on.
  // It stays on: nothing here uses the ADC, and a roll or `id add` draws a ctx_id later.
  bootloader_random_enable();

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

  // Impl Plan 10.8.1's assignment: the XIAO carries the target radio, so it is 0xF1
  // ROLE_GATELINK (BF-6). The Heltec's 0xF0 stays ROLE_RANGE, because faults arm on any
  // identity and a range peer is what a first bring-up wants. Identities do not persist: a
  // reboot is a new context for every identity, which is what a node reboot is.
#if defined(LRAN_PROFILE_HELTEC)
  add_default(lran::kNodeSim0, simnode::Role::Range);
  add_default(lran::kNodeSim2, simnode::Role::Health);
#else
  add_default(lran::kNodeSim1, simnode::Role::GateLink);
#endif

  if (simnode::ui_begin(simnode::kPanel)) {
    Serial.println(F("OLED: up"));
  } else {
    Serial.println(F("OLED: no ACK - check Vext on a Heltec, the expansion board on a XIAO; continuing without it"));
  }

  // spec 12.4.2 step 7 - the radio boots on the committed group, which is D1's until a
  // change commits. A trial the reboot ended is owed to the bridge as PHY_REVERTED.
  if (!g_phy_blob.begin()) {
    Serial.println(F("PHY: NVS refused - no store, every PHY row answers READ_ONLY"));
  }
  const simnode::RevertCause boot_revert = g_phy.begin();
  g_node.set_phy(&g_phy);
  if (boot_revert != simnode::RevertCause::None) g_node.on_phy_revert(boot_revert);
  lran::link::PhyConfig boot_phy = lran::link::kPhy;
  if (!simnode::phy_config_from(g_phy.group(), lran::link::kPhy, &boot_phy)) {
    Serial.println(F("PHY: stored group breaks D33's EIRP ceiling - booting on kPhy"));
  }
  simnode::radio_start(simnode::kRadio, boot_phy, &g_sink);
  // spec 10.7 - the BOOT status and event, queued behind nothing: they are the first frames.
  g_node.on_boot(cause, boots, millis());
  Serial.println(F("B0: identities, console, faults, OLED; all four roles. Type 'help'."));
}

void loop() {
  const uint32_t now = millis();
  while (Serial.available() > 0) {
    g_console.feed(static_cast<char>(Serial.read()), now);
  }
  g_node.tick(now);
  phy_service(now);
  g_faults.tick(now);
  simnode::radio_service(&g_node, &g_outbox, now);
  restart_service();
  ui_service(now);
  delay(1);  // spec 12.3 backoffs are milliseconds; nothing here needs a finer loop
}
