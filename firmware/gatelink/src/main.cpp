// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink, node 0x01. Boot, banner and task start. Task L6; GateLink Impl Plan 5.3, 6.8.
//
// THE ONLY TRANSLATION UNIT THAT INCLUDES secrets.h, and it takes LRAN_GATELINK_NODE_KEY
// and nothing else. GateLink holds its own derived key and never the master (Impl Plan
// 6.8); tools/checks/node_holds_no_master.py fails the build's checks if this firmware
// names the master. The key is never printed.

#include <Arduino.h>
#include <esp_system.h>

#include "board_stamplc.h"
#include "spi_bus.h"
#include "task_runtime.h"
#include "ui_pages.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h not found. Run `cp secrets.h.example secrets.h` at the repo root and fill it in. GateLink reads LRAN_GATELINK_NODE_KEY only. Never commit the copy."
#endif

// Impl Plan 6.8, item 1. A secrets.h made before L5 carries its own copy of the template's
// completeness block, which does not name this field, so GateLink checks for it here.
#if !defined(LRAN_GATELINK_NODE_KEY)
#error "secrets.h: LRAN_GATELINK_NODE_KEY missing. Run `python3 tools/provision/node_key.py` and paste the line it prints into the root secrets.h (secrets.h.example documents the field)."
#endif

namespace {

const uint8_t kNodeKey[] = LRAN_GATELINK_NODE_KEY;
static_assert(sizeof(kNodeKey) == 32, "secrets.h: LRAN_GATELINK_NODE_KEY must be 32 bytes (spec 9.1)");

const char* reset_reason_name(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "power_on";
    case ESP_RST_EXT:       return "external_pin";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:  return "task_watchdog";
    case ESP_RST_WDT:       return "other_watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_SDIO:      return "sdio";
    default:                return "unknown";
  }
}

}  // namespace

void setup() {
  // PRD R-3.5j. Before the banner's 200 ms: a reset in the middle of a pulse leaves the
  // relay energized until this write.
  const gatelink::EarlyOff relays_off = gatelink::board_relays_off_early();

  Serial.begin(115200);
  delay(200);  // USB CDC enumeration on a cold boot, before the first println

  const char* reset = reset_reason_name(esp_reset_reason());

  // The banner names the specification version this firmware is built against.
  // tools/checks/spec_citation_version.py reads that line, as it reads the bridge's.
  Serial.println(F("LRAN GateLink - node 0x01"));
  Serial.println(F("Binding spec: LRAN-Protocol-Specification v0.17 (ver = 2)"));

  // At the gate, this line is how anyone tells which image is running. A -dirty commit
  // matches no commit in the repository.
  Serial.printf("Version: %s (%s)\n", LRAN_GATELINK_VERSION, LRAN_GATELINK_GIT);
  Serial.printf("Reset: %s\n", reset);
  if (relays_off == gatelink::EarlyOff::BusFailed) Serial.println(F("*** Boot-time relay off: internal I2C did not start ***"));
  if (relays_off == gatelink::EarlyOff::NoAck) Serial.println(F("*** Boot-time relay off: expander did not acknowledge ***"));

  if (gatelink::node_key_unprovisioned(kNodeKey, sizeof(kNodeKey))) {
    Serial.println(F("*** LRAN_GATELINK_NODE_KEY IS THE ALL-ZERO PLACEHOLDER ***"));
    Serial.println(F("*** This node cannot authenticate. Run tools/provision/node_key.py. ***"));
  } else {
    Serial.println(F("Node key: provisioned"));
  }

  gatelink::spi_bus_init();
  gatelink::board_begin();
  gatelink::PageText page;
  gatelink::render_boot_page(
      {LRAN_GATELINK_VERSION, LRAN_GATELINK_GIT, reset, kNodeKey, sizeof(kNodeKey)}, &page);
  gatelink::board_show(page);

  const size_t started = gatelink::start_tasks();
  Serial.printf("Tasks: %u of %u started\n", static_cast<unsigned>(started),
                static_cast<unsigned>(gatelink::kTaskCount));
}

// Every job runs in a task (Impl Plan 5.2). The Arduino loopTask has nothing to do.
void loop() { vTaskDelay(portMAX_DELAY); }
