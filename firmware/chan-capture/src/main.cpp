// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// lran-chan-capture - a receiver that samples one channel's RSSI and never transmits.
// D1 frequency change brief 5; M25; the bridge engineering log, 2026-09-18.
//
// WHY IT EXISTS. The captures that compared 917.2 MHz with 917.4 MHz shared no hour, and
// occupancy moved five-fold between hours, so frequency and time of day could not be told
// apart. Several of these, one per candidate frequency, over the same night make the
// comparison same-hour by construction.
//
// WHAT IT KEEPS FROM THE BRIDGE. The sampler is lib/lran-link's ChanMonitor, the one the
// bridge runs, so the CHAN, CHANSUM and CHAN-BOOT lines are the bridge's byte for byte and
// tools/simctl/rssi_capture.py and rssi_report.py read them unchanged. The PHY is kPhy's
// in every field but the frequency.
//
// WHAT IT CHANGES, and a reader comparing its files with the bridge's has to know:
//
//   - It never transmits, so there are no FRAME lines, and rssi_report.py has no own
//     transmissions to correlate a band against.
//   - It samples on a fixed 10 ms period. The bridge samples once per lora_task wake, which
//     is at most 10 ms apart and sometimes sooner, about 99.3 samples a second over M25.
//     Occupancy is a ratio of samples, so the rate moves the catch rate of a short burst and
//     not the occupancy figure.
//   - It never skips for a reception of its own, because it has none. `own_rx` in its CHAN
//     lines counts LRAN-PHY frames it HEARD, and the samples during them are counted as
//     channel, which is what they are to a receiver that did not send them.

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "capture_config.h"
#include "lran/link/chan_monitor.h"
#include "lran/link/radio_config.h"
#include "profiles.h"
#include "receiver.h"

#if !defined(LRAN_PROFILE_HELTEC) && !defined(LRAN_PROFILE_XIAO_WIO_KIT)
#error "Build with -e heltec or -e xiao-wio: no board profile is selected"
#endif

#ifndef LRAN_CAPTURE_GIT
#define LRAN_CAPTURE_GIT "unknown"
#endif

namespace {

using lran::link::ChanBucket;
using lran::link::ChanMonitor;
using lran::link::ChanRollup;
using lran::link::ChanRollupper;

// The bridge's kLoraMaxWaitMs, so both samplers take ~100 readings a second. Compile-time,
// as chan_monitor.h's bucket length is: root rule 8 is about a node that cannot be
// reflashed without a walk to the gate, and this image lives on the bench.
constexpr uint32_t kSampleMs = 10;

// How long setup() waits for a host on native USB before printing the header. The XIAO's
// USB CDC drops what is written before the port is open, and a capture that loses its
// CHAN-BOOT line cannot say which frequency it measured. The Heltec's UART never waits.
constexpr uint32_t kHostWaitMs = 5000;

constexpr const char* kPrefsNamespace = "chancap";
constexpr const char* kPrefsFreqKey   = "freq_hz";

ChanMonitor              g_chan;
ChanRollupper            g_rollup;
chancap::FreqChoice      g_freq{};
Preferences              g_prefs;

// profiles.h says why. On the Heltec, Vext high unpowers the panel. On the XIAO the panel is
// powered directly, so it gets DISPLAYOFF (0xAE) and CHARGEPUMP disable (0x8D, 0x10), the
// SSD1306's own commands, sent once. No I2C traffic follows, so the bus is quiet for the run.
void panel_off() {
  const chancap::PanelPins& p = chancap::kPanel;
  if (p.vext != chancap::kPinNone) {
    pinMode(p.vext, OUTPUT);
    digitalWrite(p.vext, HIGH);
    return;
  }
  Wire.begin(p.sda, p.scl);
  Wire.beginTransmission(p.addr);
  Wire.write(0x00);  // control byte: a command stream follows
  Wire.write(0xAE);
  Wire.write(0x8D);
  Wire.write(0x10);
  const uint8_t st = Wire.endTransmission();
  Wire.end();
  if (st != 0) Serial.printf("panel: no answer at 0x%02x (I2C status %u)\n", p.addr, st);
}

// The sampler. Highest priority of the two tasks so a serial write can never delay a
// reading; the ring between them is ChanMonitor's.
void sample_task(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    const uint32_t now_ms = millis();
    if (chancap::receiver_service(now_ms)) g_chan.note_own_rx();
    if (chancap::receiver_ready()) {
      g_chan.sample(chancap::receiver_rssi_dbm10(), now_ms);
    } else {
      g_chan.skip(now_ms);
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(kSampleMs));
  }
}

void print_mac() {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.printf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0], mac[1], mac[2], mac[3], mac[4],
                mac[5]);
}

void print_freq() {
  Serial.printf("capture: sampling %lu Hz (%s)\n", static_cast<unsigned long>(g_freq.hz),
                chancap::freq_source_name(g_freq.source));
}

// The header a capture file opens with. CHAN-BOOT is the line the tools read; the lines
// above it are for a person.
void print_header() {
  const auto& phy = lran::link::kPhy;
  Serial.println();
  Serial.println(F("LRAN chan-capture - listen-only channel sampler, never transmits (" LRAN_CAPTURE_GIT ")"));
  Serial.printf("Board: %s\n", chancap::kBoardName);
  print_mac();
  print_freq();
  Serial.printf("PHY: %lu.%lu MHz, SF%u, BW %u kHz, CR 4/%u, receive only\n",
                static_cast<unsigned long>(g_freq.hz / 1000000u),
                static_cast<unsigned long>((g_freq.hz / 100000u) % 10u),
                static_cast<unsigned>(phy.sf), static_cast<unsigned>(phy.bw_khz10 / 10),
                static_cast<unsigned>(phy.cr_denom));

  char line[160];
  if (lran::link::render_chan_boot(LRAN_CAPTURE_GIT, g_freq.hz, phy.sf, phy.bw_khz10, kSampleMs,
                                   line, sizeof(line)) > 0) {
    Serial.println(line);
  }
}

void print_help() {
  Serial.println(F("capture: commands"));
  Serial.println(F("  freq         the frequency this boot samples"));
  Serial.println(F("  freq <hz>    store <hz> for the next boot, then reboot"));
  Serial.printf("               whole hertz, %lu to %lu\n",
                static_cast<unsigned long>(chancap::kFreqMinHz),
                static_cast<unsigned long>(chancap::kFreqMaxHz));
}

void handle_line(const char* line) {
  const chancap::Command c = chancap::parse_command(line);
  switch (c.kind) {
    case chancap::CommandKind::Empty:
      return;
    case chancap::CommandKind::ShowFreq:
      print_freq();
      return;
    case chancap::CommandKind::Help:
      print_help();
      return;
    case chancap::CommandKind::BadFreq:
      Serial.println(F("capture: freq refused - give whole hertz inside the band; try help"));
      return;
    case chancap::CommandKind::Unknown:
      Serial.println(F("capture: unknown command; try help"));
      return;
    case chancap::CommandKind::SetFreq:
      // Stored, then a reboot, so the new frequency arrives under its own CHAN-BOOT line
      // and never inside a segment headed by the old one (capture_config.h).
      g_prefs.putUInt(kPrefsFreqKey, c.hz);
      Serial.printf("capture: freq %lu Hz stored - rebooting\n", static_cast<unsigned long>(c.hz));
      Serial.flush();
      delay(100);
      ESP.restart();
      return;
  }
}

void poll_console() {
  static char   buf[64];
  static size_t n        = 0;
  static bool   overflow = false;

  while (Serial.available() > 0) {
    const int ch = Serial.read();
    if (ch < 0) return;
    if (ch == '\n' || ch == '\r') {
      if (overflow) {
        Serial.println(F("capture: line too long; ignored"));
      } else {
        buf[n] = '\0';
        handle_line(buf);
      }
      n        = 0;
      overflow = false;
      continue;
    }
    if (n + 1 < sizeof(buf)) {
      buf[n++] = static_cast<char>(ch);
    } else {
      overflow = true;
    }
  }
}

// Every closed bucket goes into the rollup; a bucket that saw something also gets its own
// line. chan_monitor.h has the reasoning, and this is the bridge's drain_chan() in shape.
size_t drain_chan() {
  size_t     n = 0;
  ChanBucket b;
  char       line[192];
  while (n < 16 && g_chan.take(&b)) {
    if (lran::link::chan_notable(b) && lran::link::render_chan(b, line, sizeof(line)) > 0) {
      Serial.println(line);
    }
    g_rollup.add(b);
    if (g_rollup.due()) {
      ChanRollup r;
      if (g_rollup.take(&r) && lran::link::render_chan_rollup(r, line, sizeof(line)) > 0) {
        Serial.println(line);
      }
    }
    ++n;
  }
  return n;
}

// Said once per change, not per bucket: a receiver that went down shows as blind buckets in
// CHANSUM, and this line says why.
void report_state() {
  static bool     was_ready = false;
  static bool     first     = true;
  static uint32_t lost_seen = 0;

  const bool ready = chancap::receiver_ready();
  if (first || ready != was_ready) {
    if (ready) {
      Serial.println(F("radio: up, receiving"));
    } else {
      Serial.printf("radio: down, RadioLib status %d - retrying every 10 s\n",
                    static_cast<int>(chancap::receiver_stats().last_begin_status));
    }
    was_ready = ready;
    first     = false;
  }

  const uint32_t lost = g_chan.lost();
  if (lost != lost_seen) {
    Serial.printf("capture: the ring overwrote %lu bucket(s) in all\n",
                  static_cast<unsigned long>(lost));
    lost_seen = lost;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
#if defined(LRAN_PROFILE_XIAO_WIO_KIT)
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < kHostWaitMs) delay(10);
#endif

  g_prefs.begin(kPrefsNamespace, false);
  const bool has = g_prefs.isKey(kPrefsFreqKey);
  g_freq = chancap::choose_freq(has, has ? g_prefs.getUInt(kPrefsFreqKey, 0) : 0,
                                lran::link::kPhy.freq_hz);

  panel_off();
  print_header();

  chancap::receiver_start(chancap::kRadio, lran::link::kPhy, g_freq.hz, millis());

  xTaskCreatePinnedToCore(sample_task, "sample", 4096, nullptr, 3, nullptr, 1);
}

// The writer and the console, at the Arduino loop's priority, below the sampler.
void loop() {
  report_state();
  poll_console();
  if (drain_chan() == 0) delay(20);
}
