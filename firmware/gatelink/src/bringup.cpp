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
//   sd    - mount the microSD on the shared bus
//   bus <s>    - step 4: LCD, microSD and radio concurrently, s seconds
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
};
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
    // -2 is CHIP_NOT_FOUND, -707 SPI_CMD_TIMEOUT (BUSY never fell).
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
// Step 4 - three tasks on the one bus, or two without a card: whether GateLink keeps the
// microSD card is undecided (engineering log, 2026-10-06). Each task checks its own data, so a
// transaction that leaks into another shows up as a count, not as a hang to diagnose later.
// ---------------------------------------------------------------------------
void bus_lcd(void*) {
  uint32_t i = 0;
  while (g_bus_run) {
    xSemaphoreTake(g_bus, portMAX_DELAY);
    auto& d = M5StamPLC.Display;
    d.fillRect(0, 0, 240, 135, (i & 1) ? TFT_NAVY : TFT_BLACK);
    d.setTextColor(TFT_WHITE);
    d.setTextSize(2);
    d.setCursor(6, 56);
    d.printf("bus test %lu", static_cast<unsigned long>(i));
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
    const int16_t st = g_radio->readRegister(RADIOLIB_SX126X_REG_LORA_SYNC_WORD_MSB, sync, 2);
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
  xTaskCreate(bus_lcd, "bus_lcd", 4096, nullptr, 2, nullptr);
  if (g_sd_up) xTaskCreate(bus_sd, "bus_sd", 6144, nullptr, 2, nullptr);
  xTaskCreate(bus_radio, "bus_radio", 4096, nullptr, 2, nullptr);
  delay(seconds * 1000);
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

void cmd_stat() {
  Serial.printf("stat: radio %s, sd %s, DIO1 edges %lu, DIO1 now %s\n", g_radio_up ? "up" : "down",
                g_sd_up ? "mounted" : "not mounted", static_cast<unsigned long>(g_dio1_edges),
                level(kCarrierRadio.dio1));
  if (g_radio_up) print_version();
}

void help() {
  Serial.println(F("commands: pins | reset | begin | tx | txloop <n> | rx <s> | sd [format] | bus <s> | stat"));
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
  } else if (std::strcmp(line, "sd") == 0) {
    cmd_sd(arg != nullptr && std::strcmp(arg, "format") == 0);
  } else if (std::strcmp(line, "bus") == 0) {
    cmd_bus(n == 0 ? 30 : n);
  } else if (std::strcmp(line, "stat") == 0) {
    cmd_stat();
  } else if (line[0] != '\0') {
    help();
  }
}

}  // namespace

void setup() {
  // Before anything else: an open DIO1 conductor then reads a steady low (expansion
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
