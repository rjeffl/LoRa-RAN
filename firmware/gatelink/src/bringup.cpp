// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The carrier bring-up image. GL0; GateLink Impl Plan 8.2; expansion board 11 steps 1-4.
//
// NOT THE NODE. The gatelink-bringup env builds this file in place of main.cpp and
// task_runtime.cpp, so none of the node's task rules apply: this image starts no tasks
// but step 4's three, and it writes Serial from wherever it likes. It holds no key and
// speaks no protocol. Its transmissions are raw test frames that the bridge counts and
// discards.
//
// Every step is a console command, run in order by the operator at a serial monitor:
//   pins  - step 1: the radio's control lines before anything drives them
//   reset - step 2: pulse NRESET and time BUSY's fall
//   begin - step 2: radio.begin() from kCarrierRadio and kPhy, then read the chip back
//   tx    - step 3: one transmit, with the DIO1 edge timed rather than inferred
//   txloop <n> - n transmits back to back, to watch the 3.3 V rail through TX
//   rx <s>     - listen s seconds and print what arrives
//   cad [rx]   - one channel scan, from standby or from receive, with BUSY and DIO1 timed
//   sd    - mount the microSD on the shared bus
//   bus <s>    - step 4: LCD, microSD and radio concurrently, s seconds
//   ved ...    - step 5 and GL4's bench checks: VE.Direct text and HEX; `ved` lists them
//   stat  - the counters

#include <Arduino.h>
#include <M5StamPLC.h>
#include <RadioLib.h>
#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstring>
#include <new>

#include "board_profile.h"
#include "board_stamplc.h"
#include "lran/link/radio_config.h"
#include "vedirect/hex.h"
#include "vedirect/text.h"

namespace {

using gatelink::kCarrierRadio;
using lran::link::kPhy;

// readRegister() is protected in RadioLib 7.7.1. The version string and the sync word are
// the cheapest proof that MISO carries the radio's bytes and not the bus's idle level.
class ProbeRadio : public SX1262 {
 public:
  using SX1262::SX1262;
  using SX126x::readRegister;
};

// Static storage, as the simnode's radio.cpp keeps it (root rule 3). The HAL takes the
// global SPI object: it is the one M5StamPLC's SD.begin() uses, and two SPIClass objects
// on one host would each reset it at begin().
alignas(ArduinoHal) uint8_t g_hal_storage[sizeof(ArduinoHal)];
alignas(Module) uint8_t     g_module_storage[sizeof(Module)];
alignas(ProbeRadio) uint8_t g_radio_storage[sizeof(ProbeRadio)];
ProbeRadio*                 g_radio = nullptr;
bool                        g_radio_up = false;
bool                        g_sd_up    = false;

// Step 3 times the edge itself. RadioLib's own flag would say only that one came.
volatile uint32_t g_dio1_edges   = 0;
volatile uint32_t g_dio1_last_us = 0;
void IRAM_ATTR on_dio1() {
  g_dio1_last_us = micros();
  g_dio1_edges   = g_dio1_edges + 1;
}

// Plan 5.2's one lock, in miniature. Step 4 asks whether it is enough; the libraries' own
// per-transaction lock is not, because a radio command and the BUSY wait before it, or an
// SD sector and its CS, span more than one transaction.
SemaphoreHandle_t g_bus = nullptr;
StaticSemaphore_t g_bus_storage;

struct BusStats {
  uint32_t lcd_frames = 0;
  uint32_t sd_ok      = 0;
  uint32_t sd_bad     = 0;
  uint32_t radio_ok   = 0;
  uint32_t radio_bad  = 0;
  // Longest hold of the bus lock, per task. A fill that slows from ~15 ms names a clock
  // that another device's transaction left behind.
  uint32_t lcd_max_us   = 0;
  uint32_t sd_max_us    = 0;
  uint32_t radio_max_us = 0;
};

void note_hold(uint32_t& max_us, uint32_t t0) {
  const uint32_t dt = micros() - t0;
  if (dt > max_us) max_us = dt;
}
BusStats          g_bus_stats;
volatile bool     g_bus_run = false;
volatile uint32_t g_bus_done = 0;

const char* level(int pin) { return digitalRead(pin) ? "high" : "low"; }

// ---------------------------------------------------------------------------
// Step 1 - nothing driven yet. R3 should hold NSS high and R4 should hold NRESET low,
// which holds the radio in reset with BUSY high. DIO1 reads low on its pull-down.
// ---------------------------------------------------------------------------
void cmd_pins() {
  const auto& p = kCarrierRadio;
  pinMode(p.nss, INPUT);
  pinMode(p.rst, INPUT);
  pinMode(p.busy, INPUT);
  pinMode(p.dio1, INPUT_PULLDOWN);
  delay(2);
  Serial.printf("pins: NSS G%d %s (expect high, R3)\n", p.nss, level(p.nss));
  Serial.printf("pins: NRESET G%d %s (expect low, R4)\n", p.rst, level(p.rst));
  Serial.printf("pins: BUSY G%d %s (expect high while in reset)\n", p.busy, level(p.busy));
  Serial.printf("pins: DIO1 G%d %s (expect low, pull-down)\n", p.dio1, level(p.dio1));
}

// ---------------------------------------------------------------------------
// Step 2a - out of reset, BUSY should fall within a few ms. A BUSY that never falls is a
// dead or unpowered part, or BUSY not on Bus 14 (expansion board 11, step 2).
// ---------------------------------------------------------------------------
void cmd_reset() {
  const auto& p = kCarrierRadio;
  pinMode(p.busy, INPUT);
  pinMode(p.rst, OUTPUT);
  digitalWrite(p.rst, LOW);
  delay(2);
  const bool busy_in_reset = digitalRead(p.busy);
  const uint32_t t0 = micros();
  digitalWrite(p.rst, HIGH);
  while (digitalRead(p.busy) && micros() - t0 < 100000) {
  }
  const uint32_t dt = micros() - t0;
  Serial.printf("reset: BUSY in reset %s; after release %s in %lu us\n",
                busy_in_reset ? "high" : "low", digitalRead(p.busy) ? "STILL HIGH" : "low",
                static_cast<unsigned long>(dt));
}

void print_version() {
  uint8_t ver[17] = {};
  uint8_t sync[2] = {};
  xSemaphoreTake(g_bus, portMAX_DELAY);
  const int16_t a = g_radio->readRegister(RADIOLIB_SX126X_REG_VERSION_STRING, ver, 16);
  const int16_t b = g_radio->readRegister(RADIOLIB_SX126X_REG_LORA_SYNC_WORD_MSB, sync, 2);
  xSemaphoreGive(g_bus);
  Serial.printf("radio: version \"%s\" (status %d), sync word 0x%02X%02X (status %d, expect 0x1424)\n",
                reinterpret_cast<const char*>(ver), a, sync[0], sync[1], b);
}

// ---------------------------------------------------------------------------
// Step 2b - spec 12.1 and 12.2, as the simnode's radio_begin() does it.
// ---------------------------------------------------------------------------
int16_t radio_configure() {
  const auto& p = kCarrierRadio;
  int16_t st = g_radio->begin(static_cast<float>(kPhy.freq_hz) / 1000000.0f,
                              static_cast<float>(kPhy.bw_khz10) / 10.0f, kPhy.sf, kPhy.cr_denom,
                              kPhy.sync_word, kPhy.conducted_dbm, kPhy.preamble_symbols,
                              static_cast<float>(p.tcxo_mv) / 1000.0f, false);
  if (st != RADIOLIB_ERR_NONE) return st;
  st = g_radio->setOutputPower(kPhy.conducted_dbm, true);
  if (st != RADIOLIB_ERR_NONE) return st;
  st = g_radio->setDio2AsRfSwitch(p.dio2_as_rf_switch);
  if (st != RADIOLIB_ERR_NONE) return st;
  g_radio->setRfSwitchPins(static_cast<uint32_t>(p.rf_sw), RADIOLIB_NC);  // returns void
  st = g_radio->explicitHeader();
  if (st != RADIOLIB_ERR_NONE) return st;
  st = g_radio->setCRC(2);
  if (st != RADIOLIB_ERR_NONE) return st;

  // begin() sets DIO1 to plain INPUT (SX126x.cpp, modSetup), which clears a pull-down set
  // before it. Expansion board 7.1.1 asks for the pull-down before begin(); it has to be
  // set again after, or an open DIO1 conductor floats (engineering log, 2026-10-05).
  pinMode(p.dio1, INPUT_PULLDOWN);
  g_radio->setDio1Action(on_dio1);
  return RADIOLIB_ERR_NONE;
}

void cmd_begin() {
  xSemaphoreTake(g_bus, portMAX_DELAY);
  const int16_t st = radio_configure();
  xSemaphoreGive(g_bus);
  g_radio_up = (st == RADIOLIB_ERR_NONE);
  Serial.printf("begin: RadioLib status %d - %s\n", st, g_radio_up ? "radio up" : "FAILED");
  if (!g_radio_up) {
    // -2 is CHIP_NOT_FOUND, -705 SPI_CMD_TIMEOUT (BUSY never fell), -707 SPI_CMD_FAILED.
    Serial.println(F("begin: suspect TCXO voltage, then NSS on G41 and BUSY on G11 (expansion board 11)"));
    return;
  }
  Serial.printf("begin: %lu Hz, SF%u, BW %u kHz, CR 4/%u, %d dBm conducted, %u.%u dBi antenna\n",
                static_cast<unsigned long>(kPhy.freq_hz), kPhy.sf, kPhy.bw_khz10 / 10,
                kPhy.cr_denom, kPhy.conducted_dbm, kPhy.antenna_gain_dbi10 / 10,
                kPhy.antenna_gain_dbi10 % 10);
  print_version();
}

// ---------------------------------------------------------------------------
// Step 3 - one transmit. Expansion board 7.1.1: if the status says TX_DONE but no edge
// arrived, the fault is the DIO1 conductor and nothing else.
// ---------------------------------------------------------------------------
bool transmit_once(bool verbose) {
  static uint8_t frame[16] = {'G', 'L', '0', ' ', 'b', 'r', 'i', 'n', 'g', '-', 'u', 'p'};
  static uint32_t n = 0;
  ++n;
  std::memcpy(frame + 12, &n, sizeof(n));  // a counter, not a wire field

  xSemaphoreTake(g_bus, portMAX_DELAY);
  const uint32_t toa_us  = g_radio->getTimeOnAir(sizeof(frame));
  const uint32_t edges0  = g_dio1_edges;
  const uint32_t t0      = micros();
  const int16_t  st      = g_radio->startTransmit(frame, sizeof(frame));
  xSemaphoreGive(g_bus);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("tx: startTransmit status %d\n", st);
    return false;
  }

  const uint32_t limit_us = 5000 + toa_us * 5;  // RadioLib's own transmit() allowance
  while (g_dio1_edges == edges0 && micros() - t0 < limit_us) delay(1);
  const bool     edge    = g_dio1_edges != edges0;
  const uint32_t edge_us = g_dio1_last_us - t0;

  xSemaphoreTake(g_bus, portMAX_DELAY);
  const uint32_t irq = g_radio->getIrqFlags();
  (void)g_radio->finishTransmit();
  xSemaphoreGive(g_bus);
  const bool tx_done = (irq & RADIOLIB_SX126X_IRQ_TX_DONE) != 0;

  if (verbose || !edge || !tx_done) {
    Serial.printf("tx: #%lu time on air %lu us; DIO1 edge %s", static_cast<unsigned long>(n),
                  static_cast<unsigned long>(toa_us), edge ? "SEEN" : "NOT SEEN");
    if (edge) Serial.printf(" at %lu us", static_cast<unsigned long>(edge_us));
    Serial.printf("; IRQ 0x%04lX, TX_DONE %s\n", static_cast<unsigned long>(irq),
                  tx_done ? "set" : "clear");
    if (tx_done && !edge) Serial.println(F("tx: TX_DONE without an edge - the DIO1 conductor (PORT.A white)"));
  }
  return edge && tx_done;
}

void cmd_tx(uint32_t count) {
  if (!g_radio_up) {
    Serial.println(F("tx: run begin first"));
    return;
  }
  uint32_t ok = 0;
  for (uint32_t i = 0; i < count; ++i) {
    if (transmit_once(count == 1)) ++ok;
  }
  Serial.printf("tx: %lu of %lu with edge and TX_DONE\n", static_cast<unsigned long>(ok),
                static_cast<unsigned long>(count));
}

void cmd_rx(uint32_t seconds) {
  if (!g_radio_up) {
    Serial.println(F("rx: run begin first"));
    return;
  }
  uint8_t buf[256];
  xSemaphoreTake(g_bus, portMAX_DELAY);
  g_radio->startReceive();
  xSemaphoreGive(g_bus);
  uint32_t edges = g_dio1_edges;
  uint32_t heard = 0;
  const uint32_t end = millis() + seconds * 1000;
  while (static_cast<int32_t>(end - millis()) > 0) {
    if (g_dio1_edges == edges) {
      delay(5);
      continue;
    }
    edges = g_dio1_edges;
    xSemaphoreTake(g_bus, portMAX_DELAY);
    size_t len = g_radio->getPacketLength();
    if (len > sizeof(buf)) len = sizeof(buf);
    const int16_t st   = g_radio->readData(buf, len);
    const float   rssi = g_radio->getRSSI();
    const float   snr  = g_radio->getSNR();
    g_radio->startReceive();
    xSemaphoreGive(g_bus);
    ++heard;
    Serial.printf("rx: %u bytes, status %d, RSSI %.0f dBm, SNR %.1f dB, first bytes", len, st,
                  rssi, snr);
    for (size_t i = 0; i < len && i < 8; ++i) Serial.printf(" %02X", buf[i]);
    Serial.println();
  }
  xSemaphoreTake(g_bus, portMAX_DELAY);
  g_radio->standby();
  xSemaphoreGive(g_bus);
  Serial.printf("rx: %lu frames in %lu s\n", static_cast<unsigned long>(heard),
                static_cast<unsigned long>(seconds));
}

// GL3 - one CAD, the step lran-link's media access takes before every transmit. The node
// image's first CAD left BUSY high until a reset (engineering log, 2026-10-07). From
// receive is what the node does; from standby is what GL0 never tried.
void cmd_cad(bool from_rx) {
  if (!g_radio_up) {
    Serial.println(F("cad: run begin first"));
    return;
  }
  const int busy_pin = kCarrierRadio.busy;
  xSemaphoreTake(g_bus, portMAX_DELAY);
  int16_t st = from_rx ? g_radio->startReceive() : g_radio->standby();
  xSemaphoreGive(g_bus);
  Serial.printf("cad: %s status %d, BUSY %s\n", from_rx ? "startReceive" : "standby", st,
                digitalRead(busy_pin) ? "high" : "low");
  if (from_rx) delay(50);

  const uint32_t edges0 = g_dio1_edges;
  xSemaphoreTake(g_bus, portMAX_DELAY);
  const uint32_t t0 = micros();
  st                = g_radio->startChannelScan();
  xSemaphoreGive(g_bus);
  Serial.printf("cad: startChannelScan status %d\n", st);

  // BUSY sampled every 100 us for 500 ms: when it fell, and whether it stayed down.
  uint32_t busy_low_us = 0;
  bool     fell        = false;
  while (micros() - t0 < 500000) {
    if (!fell && digitalRead(busy_pin) == LOW) {
      fell        = true;
      busy_low_us = micros() - t0;
    }
    if (g_dio1_edges != edges0) break;
    delayMicroseconds(100);
  }
  const bool     edge    = g_dio1_edges != edges0;
  const uint32_t edge_us = g_dio1_last_us - t0;
  Serial.printf("cad: BUSY %s", fell ? "fell" : "NEVER FELL");
  if (fell) Serial.printf(" at %lu us", static_cast<unsigned long>(busy_low_us));
  Serial.printf("; DIO1 edge %s", edge ? "seen" : "NOT SEEN");
  if (edge) Serial.printf(" at %lu us", static_cast<unsigned long>(edge_us));
  Serial.printf("; BUSY now %s\n", digitalRead(busy_pin) ? "high" : "low");

  xSemaphoreTake(g_bus, portMAX_DELAY);
  const uint32_t irq = g_radio->getIrqFlags();
  st                 = g_radio->standby();
  xSemaphoreGive(g_bus);
  Serial.printf("cad: IRQ 0x%04lX (CAD_DONE %s, DETECTED %s); standby status %d\n",
                static_cast<unsigned long>(irq),
                (irq & RADIOLIB_SX126X_IRQ_CAD_DONE) ? "set" : "clear",
                (irq & RADIOLIB_SX126X_IRQ_CAD_DETECTED) ? "set" : "clear", st);
}

// `sd format` lets FatFs make a FAT32 volume, but only on a card with no FAT volume: the
// core builds FatFs without exFAT (FF_FS_EXFAT 0), so a card over 32 GB arrives unreadable.
void cmd_sd(bool format) {
  xSemaphoreTake(g_bus, portMAX_DELAY);
  g_sd_up = SD.begin(gatelink::kSdCs, SPI, 4000000, "/sd", 5, format);
  const uint64_t mb = g_sd_up ? SD.cardSize() / (1024 * 1024) : 0;
  xSemaphoreGive(g_bus);
  Serial.printf("sd: %s", g_sd_up ? "mounted" : "NOT MOUNTED (card present?)");
  if (g_sd_up) Serial.printf(", %llu MB", static_cast<unsigned long long>(mb));
  Serial.println();
}

// ---------------------------------------------------------------------------
// Step 4 - three tasks on the one bus, or two when no card is mounted. Each task checks its
// own data, so a transaction that leaks into another shows up as a count, not as a hang to
// diagnose later.
// ---------------------------------------------------------------------------
void bus_lcd(void*) {
  uint32_t i = 0;
  while (g_bus_run) {
    xSemaphoreTake(g_bus, portMAX_DELAY);
    const uint32_t t0 = micros();
    auto& d = M5StamPLC.Display;
    d.fillRect(0, 0, 240, 135, (i & 1) ? TFT_NAVY : TFT_BLACK);
    d.setTextColor(TFT_WHITE);
    d.setTextSize(2);
    d.setCursor(6, 56);
    d.printf("bus test %lu", static_cast<unsigned long>(i));
    note_hold(g_bus_stats.lcd_max_us, t0);
    xSemaphoreGive(g_bus);
    ++g_bus_stats.lcd_frames;
    ++i;
    vTaskDelay(1);
  }
  ++g_bus_done;
  vTaskDelete(nullptr);
}

void bus_sd(void*) {
  uint8_t out[512];
  uint8_t in[512];
  uint32_t i = 0;
  while (g_bus_run) {
    for (size_t k = 0; k < sizeof(out); ++k) out[k] = static_cast<uint8_t>(k * 7 + i);
    xSemaphoreTake(g_bus, portMAX_DELAY);
    const uint32_t t0 = micros();
    bool ok = false;
    File f  = SD.open("/gl0bus.bin", FILE_WRITE, true);
    if (f) {
      ok = f.write(out, sizeof(out)) == sizeof(out);
      f.close();
    }
    f = SD.open("/gl0bus.bin", FILE_READ);
    if (f) {
      ok = ok && f.read(in, sizeof(in)) == static_cast<int>(sizeof(in)) &&
           std::memcmp(in, out, sizeof(in)) == 0;
      f.close();
    } else {
      ok = false;
    }
    note_hold(g_bus_stats.sd_max_us, t0);
    xSemaphoreGive(g_bus);
    if (ok) {
      ++g_bus_stats.sd_ok;
    } else {
      ++g_bus_stats.sd_bad;
    }
    ++i;
    vTaskDelay(1);
  }
  ++g_bus_done;
  vTaskDelete(nullptr);
}

void bus_radio(void*) {
  while (g_bus_run) {
    uint8_t sync[2] = {};
    xSemaphoreTake(g_bus, portMAX_DELAY);
    const uint32_t t0 = micros();
    const int16_t st = g_radio->readRegister(RADIOLIB_SX126X_REG_LORA_SYNC_WORD_MSB, sync, 2);
    note_hold(g_bus_stats.radio_max_us, t0);
    xSemaphoreGive(g_bus);
    if (st == RADIOLIB_ERR_NONE && sync[0] == 0x14 && sync[1] == 0x24) {
      ++g_bus_stats.radio_ok;
    } else {
      ++g_bus_stats.radio_bad;
    }
    vTaskDelay(1);
  }
  ++g_bus_done;
  vTaskDelete(nullptr);
}

void cmd_bus(uint32_t seconds) {
  if (!g_radio_up) {
    Serial.println(F("bus: run begin first"));
    return;
  }
  const uint32_t tasks = g_sd_up ? 3 : 2;
  g_bus_stats = BusStats{};
  g_bus_done  = 0;
  g_bus_run   = true;
  // Every driver here busy-waits on its SPI transfer, and the lock passes straight from one
  // task to the next, so the bus never idles. Unpinned at priority 2, the tasks kept IDLE0 off
  // CPU 0 until the task watchdog fired (engineering log, 2026-10-06). On core 1 at the loop
  // task's priority, CPU 0 idles and the reporter below still gets time slices.
  xTaskCreatePinnedToCore(bus_lcd, "bus_lcd", 4096, nullptr, 1, nullptr, 1);
  if (g_sd_up) xTaskCreatePinnedToCore(bus_sd, "bus_sd", 6144, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(bus_radio, "bus_radio", 4096, nullptr, 1, nullptr, 1);
  // A line a second, so a crash mid-run still leaves the trend in the log.
  for (uint32_t t = 1; t <= seconds; ++t) {
    delay(1000);
    const auto& s = g_bus_stats;
    Serial.printf("bus: t %lu; LCD %lu, max %lu us; SD %lu ok %lu bad, max %lu us; radio %lu, max %lu us\n",
                  static_cast<unsigned long>(t), static_cast<unsigned long>(s.lcd_frames),
                  static_cast<unsigned long>(s.lcd_max_us), static_cast<unsigned long>(s.sd_ok),
                  static_cast<unsigned long>(s.sd_bad), static_cast<unsigned long>(s.sd_max_us),
                  static_cast<unsigned long>(s.radio_ok), static_cast<unsigned long>(s.radio_max_us));
  }
  g_bus_run = false;
  while (g_bus_done < tasks) delay(10);

  // A transmit at the end proves the radio still works after the bus was shared.
  const bool tx_after = transmit_once(false);
  const auto& s = g_bus_stats;
  Serial.printf("bus: %lu s; LCD %lu frames; SD %s %lu ok %lu bad; radio %lu ok %lu bad; tx after %s\n",
                static_cast<unsigned long>(seconds), static_cast<unsigned long>(s.lcd_frames),
                g_sd_up ? "on" : "off", static_cast<unsigned long>(s.sd_ok),
                static_cast<unsigned long>(s.sd_bad),
                static_cast<unsigned long>(s.radio_ok), static_cast<unsigned long>(s.radio_bad),
                tx_after ? "ok" : "FAILED");
}

// ---------------------------------------------------------------------------
// Step 5 (expansion board 11) and GL4's bench checks: VE.Direct on Serial1.
//
// THE MPPT ON THE BENCH IS THE GATE'S, with its settings saved from VictronConnect, and
// the carrier speaks HEX to it. So this console sends only requests that read: Ping,
// AppVersion, ProductId and Get. A Set, a Restart or anything else is refused here, before
// it reaches the UART. Write rejection is the node's behaviour (plan 4.2.4), tested there.
//
// M4 BY DIVISION. Plan 4.2.2 asks whether the MPPT's TX drives low hard enough to pull the
// converter's 10 kOhm pull-up below the BSS138's threshold. If it cannot, the failure is
// silent: no framing errors, no bytes. `ved raw` receiving checksummed blocks answers that
// for this carrier; receiving nothing, with the cable metered, points at D25 first.

bool                 g_ved_up = false;
vedirect::TextParser g_text;
// `ved swap` receives on G5 with TX off: a cable with J4 pins 2 and 3 crossed can then be
// read before it is re-crimped, and nothing drives the MPPT. HEX refuses while it is set.
bool                 g_ved_swapped = false;

void ved_begin() {
  if (g_ved_up) return;
  // Before begin(), or the core ignores it. A text block is a few hundred bytes, and a
  // console print can hold this loop for longer than the default 256 take at 19200 baud.
  Serial1.setRxBufferSize(1024);
  const int8_t rx = g_ved_swapped ? gatelink::kVedUartTx : gatelink::kVedUartRx;
  const int8_t tx = g_ved_swapped ? -1 : gatelink::kVedUartTx;
  Serial1.begin(gatelink::kVedBaud, SERIAL_8N1, rx, tx);
  g_ved_up = true;
  Serial.printf("ved: Serial1 at %lu baud, RX G%d, TX %s%d\n",
                static_cast<unsigned long>(gatelink::kVedBaud), rx, tx < 0 ? "off " : "G",
                tx < 0 ? 0 : tx);
}

// One byte as it would be written in a C string literal, so a capture can be pasted into
// lib/vedirect's tests unchanged. The checksum byte can be anything, so nothing is assumed
// printable. A newline in the data also ends the console line.
void print_escaped(uint8_t b) {
  if (b == '\n') {
    Serial.print("\\n\n");
  } else if (b == '\r') {
    Serial.print("\\r");
  } else if (b == '\t') {
    Serial.print("\\t");
  } else if (b == '\\' || b == '"') {
    Serial.printf("\\%c", b);
  } else if (b >= 0x20 && b < 0x7F) {
    Serial.write(b);
  } else {
    // Closing the literal here stops a following hex digit from joining the escape.
    Serial.printf("\\x%02X\"\"", b);
  }
}

// With `reg`, a Get of it goes out every kRawGetMs, unanswered or not, so the capture shows
// where the MPPT puts a HEX answer against its text blocks (L3; handoff Open).
constexpr uint32_t kRawGetMs = 50;

void cmd_ved_raw(uint32_t seconds, int32_t reg) {
  ved_begin();
  while (Serial1.available() > 0) Serial1.read();
  Serial.printf("ved raw: %lu s, bytes as a C string literal", static_cast<unsigned long>(seconds));
  if (reg >= 0) Serial.printf("; a Get of 0x%04lX every %lu ms", static_cast<unsigned long>(reg),
                              static_cast<unsigned long>(kRawGetMs));
  Serial.println();
  char         get[vedirect::kMaxChars];
  const size_t get_n = reg >= 0 ? vedirect::encode_get(static_cast<uint16_t>(reg), get, sizeof(get)) : 0;
  uint32_t       bytes = 0;
  uint32_t       first = 0;
  uint32_t       gets  = 0;
  const uint32_t t0    = millis();
  uint32_t       next_get = t0;
  while (millis() - t0 < seconds * 1000UL) {
    if (get_n != 0 && static_cast<int32_t>(millis() - next_get) >= 0) {
      Serial1.write(reinterpret_cast<const uint8_t*>(get), get_n);
      Serial1.write('\n');
      ++gets;
      next_get += kRawGetMs;
    }
    while (Serial1.available() > 0) {
      if (bytes == 0) first = millis() - t0;
      print_escaped(static_cast<uint8_t>(Serial1.read()));
      ++bytes;
    }
    delay(1);
  }
  if (bytes == 0) {
    Serial.println(F("\nved raw: NO BYTES. Meter the cable first (plan 4.2.1), then D25 (plan 4.2.2)"));
  } else {
    Serial.printf("\nved raw: %lu bytes, the first %lu ms in; %lu Gets sent\n", static_cast<unsigned long>(bytes),
                  static_cast<unsigned long>(first), static_cast<unsigned long>(gets));
  }
}

void print_counters() {
  const auto& c = g_text.counters();
  Serial.printf("ved counters: blocks %lu, bad_checksum %lu, unsynced %lu, interrupted %lu, "
                "overflow %lu, hex_lines %lu, hex_too_long %lu\n",
                static_cast<unsigned long>(c.blocks), static_cast<unsigned long>(c.bad_checksum),
                static_cast<unsigned long>(c.unsynced), static_cast<unsigned long>(c.interrupted),
                static_cast<unsigned long>(c.overflow), static_cast<unsigned long>(c.hex_lines),
                static_cast<unsigned long>(c.hex_too_long));
}

void print_block(uint32_t gap_ms) {
  const auto& b = g_text.block();
  Serial.printf("block: %u fields, %lu ms after the last\n", static_cast<unsigned>(b.count),
                static_cast<unsigned long>(gap_ms));
  for (size_t i = 0; i < b.count; ++i) {
    // The Checksum record's value is one raw byte, not text.
    if (std::strcmp(b.fields[i].label, "Checksum") == 0) continue;
    Serial.printf("  %-9s %s\n", b.fields[i].label, b.fields[i].value);
  }
  vedirect::MpptText m;
  const size_t       bad = vedirect::decode_mppt(b, &m);
  // Sentinels print as they are (root rule 6): a field that reads 4294967295 is missing.
  Serial.printf("  decoded: pid 0x%04X, batt %lu mV %ld mA, pv %lu mV %lu W, load %ld mA %s, "
                "cs %u, mppt %u, err %u, h19 %lu, h20 %lu, h21 %lu, h22 %lu, hsds %u; "
                "%u unparsed\n",
                m.pid, static_cast<unsigned long>(m.batt_mv), static_cast<long>(m.batt_ma),
                static_cast<unsigned long>(m.pv_mv), static_cast<unsigned long>(m.pv_w),
                static_cast<long>(m.load_ma),
                m.load == vedirect::LoadState::On    ? "ON"
                : m.load == vedirect::LoadState::Off ? "OFF"
                                                     : "n/a",
                m.charge_state, m.tracker, m.err, static_cast<unsigned long>(m.yield_total),
                static_cast<unsigned long>(m.yield_today), static_cast<unsigned long>(m.pmax_today),
                static_cast<unsigned long>(m.yield_yest), m.day_seq, static_cast<unsigned>(bad));
}

// Feeds one byte to the parser and reports what it completed. Returns the event so a HEX
// transaction can look for its reply among the text.
vedirect::TextEvent ved_feed(uint8_t b, uint32_t& last_block_ms, bool print_blocks) {
  const vedirect::TextEvent ev = g_text.feed(b);
  if (ev == vedirect::TextEvent::Block) {
    const uint32_t now = millis();
    if (print_blocks) print_block(last_block_ms == 0 ? 0 : now - last_block_ms);
    last_block_ms = now;
  } else if (ev == vedirect::TextEvent::Dropped) {
    Serial.printf("dropped: %s\n", vedirect::drop_name(g_text.last_drop()));
  }
  return ev;
}

void cmd_ved_text(uint32_t seconds) {
  ved_begin();
  g_text.reset();
  Serial.printf("ved text: %lu s\n", static_cast<unsigned long>(seconds));
  uint32_t       last = 0;
  const uint32_t t0   = millis();
  while (millis() - t0 < seconds * 1000UL) {
    while (Serial1.available() > 0) {
      if (ved_feed(static_cast<uint8_t>(Serial1.read()), last, true) ==
          vedirect::TextEvent::HexLine) {
        Serial.printf("hex (unsolicited): %s\n", g_text.hex_line());
      }
    }
    delay(1);
  }
  print_counters();
}

// Plan 4.2.4's hex_timeout_ms default. The node reads it from lran-config.
constexpr uint32_t kHexTimeoutMs = 1000;

// The response command that answers `req`, per hex.h's tables. Unknown and Error answer
// any request.
bool answers(uint8_t req, int32_t reg, const vedirect::Frame& f) {
  using vedirect::HexRsp;
  if (f.cmd == static_cast<uint8_t>(HexRsp::Unknown) || f.cmd == static_cast<uint8_t>(HexRsp::Error)) {
    return true;
  }
  switch (static_cast<vedirect::HexCmd>(req)) {
    case vedirect::HexCmd::Ping:       return f.cmd == static_cast<uint8_t>(HexRsp::Ping);
    case vedirect::HexCmd::AppVersion: return f.cmd == static_cast<uint8_t>(HexRsp::Done);
    case vedirect::HexCmd::ProductId:  return f.cmd == static_cast<uint8_t>(HexRsp::Done);
    case vedirect::HexCmd::Get: {
      vedirect::RegReply r;
      return vedirect::reg_reply(f, &r) && r.cmd == HexRsp::Get && r.reg == reg;
    }
    default: return false;
  }
}

// One transaction: one request out, its reply matched, everything else on the line
// reported. Prints the result and returns whether a reply came.
bool ved_hex(const char* req, size_t n, int32_t reg) {
  vedirect::Frame q;
  const vedirect::Parse qp = vedirect::decode(req, n, &q);
  if (qp != vedirect::Parse::Ok) {
    Serial.printf("hex: request refused, %s\n", vedirect::parse_name(qp));
    return false;
  }
  using vedirect::HexCmd;
  const HexCmd c = static_cast<HexCmd>(q.cmd);
  if (c != HexCmd::Ping && c != HexCmd::AppVersion && c != HexCmd::ProductId && c != HexCmd::Get) {
    Serial.printf("hex: command %X refused; this console only reads\n", q.cmd);
    return false;
  }
  if (g_ved_swapped) {
    Serial.println(F("hex: refused while `ved swap` is on; there is no TX"));
    return false;
  }
  ved_begin();
  Serial1.write(reinterpret_cast<const uint8_t*>(req), n);
  Serial1.write('\n');
  Serial.printf("hex > %.*s\n", static_cast<int>(n), req);

  uint32_t       last = 0;
  const uint32_t t0   = millis();
  while (millis() - t0 < kHexTimeoutMs) {
    while (Serial1.available() > 0) {
      if (ved_feed(static_cast<uint8_t>(Serial1.read()), last, false) !=
          vedirect::TextEvent::HexLine) {
        continue;
      }
      vedirect::Frame       f;
      const vedirect::Parse p = vedirect::decode(g_text.hex_line(), g_text.hex_len(), &f);
      if (p != vedirect::Parse::Ok) {
        Serial.printf("hex < %s  (%s)\n", g_text.hex_line(), vedirect::parse_name(p));
        continue;
      }
      if (!answers(q.cmd, reg, f)) {
        Serial.printf("hex < %s  (unmatched, discarded)\n", g_text.hex_line());
        continue;
      }
      const uint32_t dt = millis() - t0;
      Serial.printf("hex < %s  (%lu ms)\n", g_text.hex_line(), static_cast<unsigned long>(dt));
      vedirect::RegReply r;
      if (vedirect::reg_reply(f, &r)) {
        Serial.printf("      reg 0x%04X flags 0x%02X value %lu (0x%lX), %u bytes%s\n", r.reg,
                      r.flags, static_cast<unsigned long>(r.value),
                      static_cast<unsigned long>(r.value), r.width,
                      r.flags & vedirect::kFlagUnknownId ? ", UNKNOWN ID" : "");
      } else {
        // Ping and AppVersion carry the firmware version; ProductId the PID.
        Serial.print(F("      data"));
        for (size_t i = 0; i < f.len; ++i) Serial.printf(" %02X", f.data[i]);
        Serial.println();
      }
      return true;
    }
    delay(1);
  }
  Serial.printf("hex: TIMEOUT after %lu ms\n", static_cast<unsigned long>(kHexTimeoutMs));
  return false;
}

bool ved_simple(vedirect::HexCmd c) {
  char         out[vedirect::kMaxChars];
  const size_t n = vedirect::encode(static_cast<uint8_t>(c), nullptr, 0, out, sizeof(out));
  return ved_hex(out, n, -1);
}

bool ved_get(uint16_t reg) {
  char         out[vedirect::kMaxChars];
  const size_t n = vedirect::encode_get(reg, out, sizeof(out));
  return ved_hex(out, n, reg);
}

// The registers the bridge reads back for BF-30 (firmware/bridge/src/charge_readback.h),
// then product ID and device state. Compare them with VictronConnect's.
constexpr uint16_t kScanRegs[] = {0x0100, 0x0201, 0xEDF7, 0xEDF6, 0xEDF4, 0xEDFD, 0xEDF2,
                                  0xEDF1, 0xEDF0, 0xEDFB, 0xEDEA, 0xEDE0};

void cmd_ved_scan() {
  unsigned ok = 0;
  for (uint16_t reg : kScanRegs) {
    if (ved_get(reg)) ++ok;
  }
  Serial.printf("ved scan: %u of %u answered\n", ok,
                static_cast<unsigned>(sizeof(kScanRegs) / sizeof(kScanRegs[0])));
}

// G4 and G5 sampled as GPIO inputs, with Serial1 released. It tells apart the ways `ved
// raw` gets nothing: a line stuck high (D25, or the wrong J4 pin), a line stuck low, and
// edges that arrive but do not frame at 19200 baud. The BSS138 channels pass a low both
// ways, so a swapped cable shows the MPPT's TX on G5. Neither pin is driven here, so a
// swap cannot set up the contention expansion board 7.4 describes. A bit is 52 us; the
// shortest low seen should be near that or a multiple of it.
struct EdgeStats {
  int      pin;
  int      start;
  int      prev;
  uint32_t falls   = 0;
  uint32_t fell    = 0;
  uint32_t low_us  = 0;
  uint32_t min_low = UINT32_MAX;
  uint32_t max_low = 0;
};

void sample_edge(EdgeStats& e, uint32_t now) {
  const int v = digitalRead(e.pin);
  if (v == e.prev) return;
  if (v == 0) {
    ++e.falls;
    e.fell = now;
  } else if (e.falls > 0) {
    const uint32_t w = now - e.fell;
    e.low_us += w;
    if (w < e.min_low) e.min_low = w;
    if (w > e.max_low) e.max_low = w;
  }
  e.prev = v;
}

void cmd_ved_edges(uint32_t seconds) {
  if (g_ved_up) {
    Serial1.end();
    g_ved_up = false;
  }
  EdgeStats e[2];
  e[0].pin = gatelink::kVedUartRx;
  e[1].pin = gatelink::kVedUartTx;
  for (auto& x : e) {
    pinMode(x.pin, INPUT);
    x.start = x.prev = digitalRead(x.pin);
  }
  const uint32_t t0 = millis();
  while (millis() - t0 < seconds * 1000UL) {
    // 100 ms of tight polling, then a tick for the scheduler.
    const uint32_t c0 = micros();
    uint32_t       now;
    while ((now = micros()) - c0 < 100000UL) {
      sample_edge(e[0], now);
      sample_edge(e[1], now);
    }
    delay(1);
  }
  for (const auto& x : e) {
    Serial.printf("ved edges: G%d %s at start, %s at end; %lu falling edges in %lu s; low "
                  "%lu us in all, shortest %lu us, longest %lu us\n",
                  x.pin, x.start ? "high" : "low", x.prev ? "high" : "low",
                  static_cast<unsigned long>(x.falls), static_cast<unsigned long>(seconds),
                  static_cast<unsigned long>(x.low_us),
                  static_cast<unsigned long>(x.min_low == UINT32_MAX ? 0 : x.min_low),
                  static_cast<unsigned long>(x.max_low));
  }
}

void ved_help() {
  Serial.println(F("ved: raw [s] [get_reg] | edges [s] | text [s] | ping | ver | pid | get <reg> | scan | send <:frame> | swap | stat"));
}

void cmd_ved(char* arg) {
  char* sub = arg;
  char* rest = sub == nullptr ? nullptr : std::strchr(sub, ' ');
  if (rest != nullptr) *rest++ = '\0';
  const unsigned long n = rest == nullptr ? 0 : std::strtoul(rest, nullptr, 0);
  if (sub == nullptr) {
    ved_help();
  } else if (std::strcmp(sub, "raw") == 0) {
    char* reg_s = nullptr;
    if (rest != nullptr) (void)std::strtoul(rest, &reg_s, 0);
    const bool has_reg = reg_s != nullptr && std::strspn(reg_s, " ") < std::strlen(reg_s);
    cmd_ved_raw(n == 0 ? 5 : n, has_reg ? static_cast<int32_t>(std::strtoul(reg_s, nullptr, 16)) : -1);
  } else if (std::strcmp(sub, "edges") == 0) {
    cmd_ved_edges(n == 0 ? 3 : n);
  } else if (std::strcmp(sub, "text") == 0) {
    cmd_ved_text(n == 0 ? 10 : n);
  } else if (std::strcmp(sub, "ping") == 0) {
    ved_simple(vedirect::HexCmd::Ping);
  } else if (std::strcmp(sub, "ver") == 0) {
    ved_simple(vedirect::HexCmd::AppVersion);
  } else if (std::strcmp(sub, "pid") == 0) {
    ved_simple(vedirect::HexCmd::ProductId);
  } else if (std::strcmp(sub, "get") == 0 && rest != nullptr) {
    ved_get(static_cast<uint16_t>(std::strtoul(rest, nullptr, 16)));
  } else if (std::strcmp(sub, "scan") == 0) {
    cmd_ved_scan();
  } else if (std::strcmp(sub, "send") == 0 && rest != nullptr) {
    ved_hex(rest, std::strlen(rest), -1);
  } else if (std::strcmp(sub, "swap") == 0) {
    if (g_ved_up) Serial1.end();
    g_ved_up      = false;
    g_ved_swapped = !g_ved_swapped;
    Serial.printf("ved swap: %s\n", g_ved_swapped ? "on, RX on G5, TX off" : "off");
  } else if (std::strcmp(sub, "stat") == 0) {
    print_counters();
  } else {
    ved_help();
  }
}

void cmd_stat() {
  Serial.printf("stat: radio %s, sd %s, DIO1 edges %lu, DIO1 now %s\n", g_radio_up ? "up" : "down",
                g_sd_up ? "mounted" : "not mounted", static_cast<unsigned long>(g_dio1_edges),
                level(kCarrierRadio.dio1));
  if (g_radio_up) print_version();
}

void help() {
  Serial.println(F("commands: pins | reset | begin | tx | txloop <n> | rx <s> | cad [rx] | sd [format] | bus <s> | ved | stat"));
}

void dispatch(char* line) {
  char*          arg = std::strchr(line, ' ');
  unsigned long  n   = 0;
  if (arg != nullptr) {
    *arg++ = '\0';
    n      = std::strtoul(arg, nullptr, 10);
  }
  if (std::strcmp(line, "pins") == 0) {
    cmd_pins();
  } else if (std::strcmp(line, "reset") == 0) {
    cmd_reset();
  } else if (std::strcmp(line, "begin") == 0) {
    cmd_begin();
  } else if (std::strcmp(line, "tx") == 0) {
    cmd_tx(1);
  } else if (std::strcmp(line, "txloop") == 0) {
    cmd_tx(n == 0 ? 20 : n);
  } else if (std::strcmp(line, "rx") == 0) {
    cmd_rx(n == 0 ? 10 : n);
  } else if (std::strcmp(line, "cad") == 0) {
    cmd_cad(arg != nullptr && std::strcmp(arg, "rx") == 0);
  } else if (std::strcmp(line, "sd") == 0) {
    cmd_sd(arg != nullptr && std::strcmp(arg, "format") == 0);
  } else if (std::strcmp(line, "bus") == 0) {
    cmd_bus(n == 0 ? 30 : n);
  } else if (std::strcmp(line, "ved") == 0) {
    cmd_ved(arg);
  } else if (std::strcmp(line, "stat") == 0) {
    cmd_stat();
  } else if (line[0] != '\0') {
    help();
  }
}

}  // namespace

void setup() {
  // PRD R-3.5j: a reset in the middle of a pulse leaves the relay energized until this.
  gatelink::board_relays_off_early();

  // Then the radio's lines: an open DIO1 conductor reads a steady low (expansion
  // board 7.1.1), and the radio stays deselected and in reset on R3 and R4 alone.
  pinMode(kCarrierRadio.dio1, INPUT_PULLDOWN);

  Serial.begin(115200);
  delay(200);
  Serial.println(F("LRAN GateLink - carrier bring-up image (GL0), not the node"));
  Serial.printf("Version: %s (%s)\n", LRAN_GATELINK_VERSION, LRAN_GATELINK_GIT);

  g_bus = xSemaphoreCreateMutexStatic(&g_bus_storage);

  // The global SPI gets the bus's pins before M5StamPLC.begin(). SD.begin() would
  // otherwise start it on the board definition's defaults, which on esp32-s3-devkitc-1 are
  // G11-G13: BUSY and the internal I2C's SDA.
  const auto& p = kCarrierRadio;
  SPI.begin(p.sck, p.miso, p.mosi, -1);
  gatelink::board_begin();

  ArduinoHal* hal    = new (g_hal_storage) ArduinoHal(SPI);
  Module*     module = new (g_module_storage)
      Module(hal, static_cast<uint32_t>(p.nss), static_cast<uint32_t>(p.dio1),
             static_cast<uint32_t>(p.rst), static_cast<uint32_t>(p.busy));
  g_radio = new (g_radio_storage) ProbeRadio(module);

  help();
}

void loop() {
  static char   line[64];
  static size_t len = 0;
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == '\r' || c == '\n') {
      line[len] = '\0';
      dispatch(line);
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = static_cast<char>(c);
    }
  }
  delay(5);
}
