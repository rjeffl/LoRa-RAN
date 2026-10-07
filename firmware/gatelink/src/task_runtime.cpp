// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task creation and the task bodies. Task L6; GateLink Impl Plan 5.2.
//
// io_task does real work from GL1: relay pulses, input debounce, buttons and the sensors.
// log_task carries GL1's bench console. Every other body is a stub that counts its passes
// and waits out its period; the milestone that fills it is named at it.
//
// THE WATCHDOG IS NOT ARMED HERE. Impl Plan 5.2 feeds it from app_task, and the timeout is a
// timing constant on a node with no OTA (root rule 8). The bridge fixed its own at 10 s on
// the strength of having OTA (bridge tasks.h), an argument GateLink cannot borrow. Arming it
// waits on that decision, at GL3. Arduino-ESP32's default watchdog still watches the idle
// task on core 0.

#include "task_runtime.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board_stamplc.h"
#include "gate_io.h"
#include "radio.h"

namespace gatelink {
namespace {

std::atomic<uint32_t> g_passes[kTaskCount];

StaticTask_t g_tcb[kTaskCount];
StackType_t  g_stack_io[4096];
StackType_t  g_stack_vedirect[4096];
StackType_t  g_stack_lora[8192];
StackType_t  g_stack_app[6144];
StackType_t  g_stack_bms[6144];
StackType_t  g_stack_ui[6144];
StackType_t  g_stack_log[4096];

// How often log_task prints the pass counts.
constexpr uint32_t kAliveReportMs = 30000;

// io_task counts milliseconds in ticks.
static_assert(configTICK_RATE_HZ == 1000, "io_task's deadlines assume a 1 ms tick");

// How often io_task reads the INA226, LM75 and RTC. A display and log rate, not a control
// loop's, so it is not a parameter.
constexpr uint32_t kSensorPeriodMs = 1000;

// ---------------------------------------------------------------------------
// What io_task shares. Other tasks ask for a pulse through the queue and read the rest
// under a spinlock held for a struct copy; io_task never waits on either (R-5.2a).
// ---------------------------------------------------------------------------
struct PulseRequest {
  uint8_t  relay;     // 0-3 for K1-K4
  uint16_t width_ms;  // 0 for relay_pulse_ms
};

struct IoSnapshot {
  uint8_t      inputs_raw    = 0;
  uint8_t      inputs        = 0;  // debounced
  // Bit changes seen since boot, raw and after debounce. A bounce or a tap shorter than
  // input_debounce_samples polls adds to the first and not the second.
  uint32_t     raw_edges     = 0;
  uint32_t     inputs_edges  = 0;
  uint8_t      relays        = 0;
  uint32_t     i2c_failures  = 0;  // every failed expander read or write (root rule 4)
  uint8_t      buttons       = 0;  // presses not yet taken by ui_task, bit 0 = A
  BoardSensors sensors;
  std::tm      rtc{};
  bool         rtc_ok        = false;
  // The last pulse, timed from the expander's acknowledgements: the moment the relay
  // driver was told, not the contact. A logic analyzer on the contact read 0.6-1.7 ms
  // shorter (bench, 2026-10-07).
  uint32_t     pulse_seq     = 0;  // bumped when a pulse ends or is refused
  uint8_t      pulse_relay   = 0;
  PulseResult  pulse_result  = PulseResult::Started;
  int64_t      pulse_on_us   = 0;
  int64_t      pulse_off_us  = 0;
};

portMUX_TYPE  g_io_mux = portMUX_INITIALIZER_UNLOCKED;
IoSnapshot    g_io;
StaticQueue_t g_pulse_q_storage;
uint8_t       g_pulse_q_buf[4 * sizeof(PulseRequest)];
QueueHandle_t g_pulse_q = nullptr;

IoSnapshot io_snapshot() {
  portENTER_CRITICAL(&g_io_mux);
  const IoSnapshot copy = g_io;
  portEXIT_CRITICAL(&g_io_mux);
  return copy;
}

// ui_task's alone: a press is taken once.
uint8_t take_buttons() {
  portENTER_CRITICAL(&g_io_mux);
  const uint8_t b = g_io.buttons;
  g_io.buttons    = 0;
  portEXIT_CRITICAL(&g_io_mux);
  return b;
}

// The page main.cpp drew at boot, which ui_task draws again after a bus test.
PageText g_boot_page{};

// radio_begin()'s status, written by lora_task. INT16_MIN until it has run.
std::atomic<int16_t> g_radio_status{INT16_MIN};

// ---------------------------------------------------------------------------
// GL1's bus test (Impl Plan 8.2): the LCD, the microSD and the radio, each driven by the
// task that owns it in Impl Plan 5.2, at that task's priority, all under the SpiLock. Each
// device checks its own transfer, so a transaction that leaks into another shows up as a
// count. The bring-up image's `bus` test proved the drivers from tasks of its own; this
// one proves the node's tasks.
//
// Each figure has one writer. The max times are of the whole call, so they include the
// wait for the lock.
// ---------------------------------------------------------------------------
struct BusStats {
  std::atomic<uint32_t> lcd_frames{0};
  std::atomic<uint32_t> lcd_max_us{0};
  std::atomic<uint32_t> sd_ok{0};
  std::atomic<uint32_t> sd_bad{0};
  std::atomic<uint32_t> sd_max_us{0};
  std::atomic<uint32_t> radio_ok{0};
  std::atomic<uint32_t> radio_bad{0};
  std::atomic<uint32_t> radio_max_us{0};
};
BusStats          g_bus;
std::atomic<bool> g_bus_run{false};

// lora_task reads the sync word this often during a test: far more SPI traffic than GL3's
// receive loop makes, short of starving the tasks below it.
constexpr uint32_t kBusRadioPeriodMs = 5;

void bus_reset() {
  for (auto* a : {&g_bus.lcd_frames, &g_bus.lcd_max_us, &g_bus.sd_ok, &g_bus.sd_bad, &g_bus.sd_max_us,
                  &g_bus.radio_ok, &g_bus.radio_bad, &g_bus.radio_max_us}) {
    a->store(0, std::memory_order_relaxed);
  }
}

// Microamps as mA with `places` decimals (1-3), sign included: -1234 uA, 2 places, is
// "-1.23".
void format_ma(int32_t ua, int places, char* out, size_t cap) {
  const int64_t mag  = ua < 0 ? -static_cast<int64_t>(ua) : ua;
  const int64_t div  = places == 1 ? 100 : places == 2 ? 10 : 1;
  const int64_t frac = places == 1 ? 10 : places == 2 ? 100 : 1000;
  const int64_t q    = mag / div;
  std::snprintf(out, cap, "%s%lld.%0*lld", ua < 0 ? "-" : "", static_cast<long long>(q / frac), places,
                static_cast<long long>(q % frac));
}

void note_max(std::atomic<uint32_t>& max_us, int64_t t0) {
  const uint32_t dt = static_cast<uint32_t>(esp_timer_get_time() - t0);
  if (dt > max_us.load(std::memory_order_relaxed)) max_us.store(dt, std::memory_order_relaxed);
}

uint32_t now_ms() { return static_cast<uint32_t>(xTaskGetTickCount()); }

bool reached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

void count(TaskId id) { g_passes[static_cast<size_t>(id)].fetch_add(1, std::memory_order_relaxed); }

TickType_t period_ticks(TaskId id) {
  return pdMS_TO_TICKS(default_period_ms(task_spec(id)));
}

// R-5.2a - io_task never blocks on anything but its own period, and vTaskDelayUntil keeps
// that period from drifting by the length of a pass. tools/checks/io_task_never_blocks.py
// reads this function.
//
// It wakes at the next poll or at a pulse's trailing edge, whichever is sooner, so the edge
// is late by at most a tick and an I2C write rather than by up to input_poll_ms. A pulse's
// leading edge waits for the next poll, which delays the command but not its width.
//
// The Store supplies the four parameters from GL3; until then they are lran-config's
// defaults.
void io_task(void*) {
  RelayPulser pulser;
  Debouncer   debounce;
  const uint32_t poll_ms    = param_default(kParamInputPollMs);
  const uint32_t width_ms   = param_default(kParamRelayPulseMs);
  const uint32_t spacing_ms = param_default(kParamRelayMinSpacingMs);
  const uint8_t  samples    = static_cast<uint8_t>(param_default(kParamInputDebounceSamples));

  TickType_t last        = xTaskGetTickCount();
  uint32_t   next_poll   = now_ms();
  uint32_t   next_sensor = now_ms();
  for (;;) {
    count(TaskId::Io);
    const uint32_t now = now_ms();

    // The trailing edge first: it is the one with a deadline.
    if (pulser.update(now)) {
      const bool    ok  = board_write_relays(pulser.mask());
      const int64_t t   = esp_timer_get_time();
      portENTER_CRITICAL(&g_io_mux);
      g_io.relays       = pulser.mask();
      g_io.pulse_off_us = t;
      if (!ok) ++g_io.i2c_failures;
      ++g_io.pulse_seq;
      portEXIT_CRITICAL(&g_io_mux);
    }

    PulseRequest req;
    if (xQueueReceive(g_pulse_q, &req, 0) == pdTRUE) {
      const uint32_t    w  = req.width_ms != 0 ? req.width_ms : width_ms;
      const PulseResult r  = pulser.start(req.relay, w, spacing_ms, now);
      bool              ok = true;
      int64_t           t  = 0;
      if (r == PulseResult::Started) {
        ok = board_write_relays(pulser.mask());
        t  = esp_timer_get_time();
      }
      portENTER_CRITICAL(&g_io_mux);
      g_io.pulse_relay  = req.relay;
      g_io.pulse_result = r;
      if (r == PulseResult::Started) {
        g_io.relays      = pulser.mask();
        g_io.pulse_on_us = t;
      } else {
        ++g_io.pulse_seq;  // refused; nothing to time
      }
      if (!ok) ++g_io.i2c_failures;
      portEXIT_CRITICAL(&g_io_mux);
    }

    if (reached(now, next_poll)) {
      next_poll += poll_ms;
      uint8_t raw = 0;
      const bool    ok      = board_read_inputs(&raw);
      const uint8_t buttons = board_poll_buttons();
      portENTER_CRITICAL(&g_io_mux);
      if (ok) {
        const uint8_t inputs = debounce.update(raw, samples);
        g_io.raw_edges += static_cast<uint32_t>(__builtin_popcount(raw ^ g_io.inputs_raw));
        g_io.inputs_edges += static_cast<uint32_t>(__builtin_popcount(inputs ^ g_io.inputs));
        g_io.inputs_raw = raw;
        g_io.inputs     = inputs;
      } else {
        ++g_io.i2c_failures;
      }
      g_io.buttons |= buttons;
      portEXIT_CRITICAL(&g_io_mux);
    }

    if (reached(now, next_sensor)) {
      next_sensor += kSensorPeriodMs;
      const BoardSensors s = board_read_sensors();
      std::tm            tm{};
      const bool         rtc_ok = board_rtc_get(&tm);
      portENTER_CRITICAL(&g_io_mux);
      g_io.sensors = s;
      g_io.rtc     = tm;
      g_io.rtc_ok  = rtc_ok;
      portEXIT_CRITICAL(&g_io_mux);
    }

    // Wake at the sooner of the next poll and the trailing edge. vTaskDelayUntil counts
    // from the last wake, so the target is absolute and a slow pass does not push it out.
    const uint32_t after = now_ms();
    uint32_t       wait  = reached(after, next_poll) ? 0 : next_poll - after;
    const uint32_t edge  = pulser.ms_to_edge(after);
    if (edge < wait) wait = edge;
    const uint32_t target = after + wait;
    TickType_t     inc    = static_cast<TickType_t>(target - static_cast<uint32_t>(last));
    if (static_cast<int32_t>(inc) < 1) inc = 1;
    vTaskDelayUntil(&last, inc);
  }
}

// GL4 - waits on UART RX events instead of a second.
void vedirect_task(void*) {
  for (;;) {
    count(TaskId::Vedirect);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// GL3 - waits on the DIO1 notification and the TX queue instead of a second. GL1 brings
// the radio up and drives its half of the bus test.
void lora_task(void*) {
  const int16_t st = radio_begin();
  g_radio_status.store(st, std::memory_order_relaxed);
  for (;;) {
    count(TaskId::Lora);
    if (st == 0 && g_bus_run.load(std::memory_order_relaxed)) {
      const int64_t t0 = esp_timer_get_time();
      const bool    ok = radio_probe();
      note_max(g_bus.radio_max_us, t0);
      (ok ? g_bus.radio_ok : g_bus.radio_bad).fetch_add(1, std::memory_order_relaxed);
      vTaskDelay(pdMS_TO_TICKS(kBusRadioPeriodMs));
    } else {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

// GL3 - state derivation, triggers and status; feeds the watchdog once it is armed.
void app_task(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    count(TaskId::App);
    vTaskDelayUntil(&last, period_ticks(TaskId::App));
  }
}

// GL5 - one BMS read per bms_poll_s, under the LoRa/BLE interlock (R-4.3h).
void bms_task(void*) {
  for (;;) {
    count(TaskId::Bms);
    vTaskDelay(period_ticks(TaskId::Bms));
  }
}

// GL1 - the buttons, for now as a beep each, and the INA226 and LM75 on the panel's last
// line once a second. That line is how measurement M12 is read with USB unplugged (Impl
// Plan 3.4). The panel pages follow; until then the boot page stays above it. During a bus
// test the panel is redrawn whole on every tick instead.
void ui_task(void*) {
  TickType_t last     = xTaskGetTickCount();
  uint32_t   ticks    = 0;
  uint32_t   frame    = 0;
  bool       was_test = false;
  for (;;) {
    count(TaskId::Ui);
    if (take_buttons() != 0) board_beep(2000, 60);
    const bool test = g_bus_run.load(std::memory_order_relaxed);
    if (test) {
      PageText page{};
      page.count = 1;
      std::snprintf(page.line[0], sizeof(page.line[0]), "bus test %lu", static_cast<unsigned long>(frame++));
      const int64_t t0 = esp_timer_get_time();
      board_show(page);
      note_max(g_bus.lcd_max_us, t0);
      g_bus.lcd_frames.fetch_add(1, std::memory_order_relaxed);
    } else {
      if (was_test) board_show(g_boot_page);
      if (was_test || ticks % 10 == 0) {
        const BoardSensors s = io_snapshot().sensors;
        // Fits 19 characters in normal use: "11870mV 2.40mA 29C". A sentinel current prints
        // as "--"; anything longer is cut at the panel's width, never wrapped.
        char text[48];
        if (s.shunt_ua == INT32_MIN) {
          std::snprintf(text, sizeof(text), "%umV --mA %dC", s.bus_mv, s.temp_c10 / 10);
        } else {
          char ma[16];
          format_ma(s.shunt_ua, 2, ma, sizeof(ma));
          std::snprintf(text, sizeof(text), "%umV %smA %dC", s.bus_mv, ma, s.temp_c10 / 10);
        }
        text[kPageCols] = '\0';
        board_show_line(kPageLines - 1, text);
      }
    }
    was_test = test;
    ++ticks;
    vTaskDelayUntil(&last, period_ticks(TaskId::Ui));
  }
}

// The line is built whole and written once. Arduino-ESP32 2.0.17's USB-serial driver lost
// bytes from every line written as eight printf calls (bench, 2026-10-02).
void write_line(char* line, size_t n, size_t cap) {
  if (n > cap - 2) n = cap - 2;  // a cut line still ends in a newline
  line[n++] = '\n';
  Serial.write(reinterpret_cast<const uint8_t*>(line), n);
}

const char* pulse_result_name(PulseResult r) {
  switch (r) {
    case PulseResult::Started:  return "started";
    case PulseResult::BadRelay: return "bad relay";
    case PulseResult::BadWidth: return "bad width";
    case PulseResult::Busy:     return "busy";
    case PulseResult::TooSoon:  return "too soon after the last pulse";
  }
  return "?";
}

// GL1's bench console. Plan 6.6's debug tooling replaces it; until then it is how the
// bench drives the board layer:
//   relay <1-4> [ms]  pulse K1-K4, relay_pulse_ms by default, and report the width
//   in                inputs, raw and debounced, and their edge counts since boot
//   sense             INA226, LM75 and RTC
//   sd                mount the card and append a line to /gl1.txt
//   beep              the buzzer
//   radio             radio_begin()'s status
//   bus <s>           GL1's bus test for s seconds, 1-600; see BusStats
//   restart           a software reset
//   hang              interrupts off until the interrupt watchdog resets the chip. With
//                     `relay <k> 2000` first, it is R-3.5j's reset in the middle of a pulse
uint32_t g_bus_seconds = 0;
uint32_t g_bus_end_ms  = 0;

size_t bus_line(char* line, size_t cap, const char* head) {
  const auto ld = [](const std::atomic<uint32_t>& a) {
    return static_cast<unsigned long>(a.load(std::memory_order_relaxed));
  };
  return static_cast<size_t>(std::snprintf(
      line, cap, "bus: %s; LCD %lu, max %lu us; SD %lu ok %lu bad, max %lu us; radio %lu ok %lu bad, max %lu us",
      head, ld(g_bus.lcd_frames), ld(g_bus.lcd_max_us), ld(g_bus.sd_ok), ld(g_bus.sd_bad),
      ld(g_bus.sd_max_us), ld(g_bus.radio_ok), ld(g_bus.radio_bad), ld(g_bus.radio_max_us)));
}

void console_command(char* cmd) {
  char line[160];
  size_t n = 0;
  char* arg = std::strchr(cmd, ' ');
  if (arg != nullptr) *arg++ = '\0';
  if (std::strcmp(cmd, "relay") == 0 && arg != nullptr) {
    char* rest = nullptr;
    const unsigned long k  = std::strtoul(arg, &rest, 10);
    const unsigned long ms = std::strtoul(rest, nullptr, 10);
    // relay_pulse_ms's range, 100-2000 (Impl Plan 4.4), and 0 for its default.
    if (k < 1 || k > kRelayCount || (ms != 0 && (ms < 100 || ms > 2000))) {
      n = std::snprintf(line, sizeof(line), "relay: K1-K4, width 100-2000 ms or none");
    } else {
      const PulseRequest req{static_cast<uint8_t>(k - 1), static_cast<uint16_t>(ms)};
      const bool queued = xQueueSend(g_pulse_q, &req, 0) == pdTRUE;
      n = std::snprintf(line, sizeof(line), "relay: K%lu %s", k, queued ? "queued" : "QUEUE FULL");
    }
  } else if (std::strcmp(cmd, "in") == 0) {
    const IoSnapshot s = io_snapshot();
    static const char* const kEarlyOff[] = {"ok", "bus failed", "no ack"};
    n = std::snprintf(line, sizeof(line),
                      "in: raw 0x%02X debounced 0x%02X edges %lu/%lu relays 0x%02X i2c failures %lu; boot relay-off %s",
                      s.inputs_raw, s.inputs, static_cast<unsigned long>(s.raw_edges),
                      static_cast<unsigned long>(s.inputs_edges), s.relays,
                      static_cast<unsigned long>(s.i2c_failures),
                      kEarlyOff[static_cast<size_t>(board_early_off_result())]);
  } else if (std::strcmp(cmd, "sense") == 0) {
    const IoSnapshot s = io_snapshot();
    char ma[16] = "--";
    if (s.sensors.shunt_ua != INT32_MIN) format_ma(s.sensors.shunt_ua, 3, ma, sizeof(ma));
    n = std::snprintf(line, sizeof(line),
                      "sense: LM75 %d (0.1 C) INA226 %u mV %s mA; RTC %s %04d-%02d-%02d %02d:%02d:%02d",
                      s.sensors.temp_c10, s.sensors.bus_mv, ma,
                      s.rtc_ok ? "ok" : "unset", s.rtc.tm_year + 1900, s.rtc.tm_mon + 1, s.rtc.tm_mday,
                      s.rtc.tm_hour, s.rtc.tm_min, s.rtc.tm_sec);
  } else if (std::strcmp(cmd, "sd") == 0) {
    const bool up = board_sd_begin();
    const bool ok = up && board_sd_append("/gl1.txt", "GL1 board layer");
    n = std::snprintf(line, sizeof(line), "sd: %s, append %s", up ? "mounted" : "NOT MOUNTED",
                      ok ? "ok" : "FAILED");
  } else if (std::strcmp(cmd, "radio") == 0) {
    const int16_t st = g_radio_status.load(std::memory_order_relaxed);
    if (st == INT16_MIN) {
      n = std::snprintf(line, sizeof(line), "radio: not started");
    } else {
      n = std::snprintf(line, sizeof(line), "radio: RadioLib status %d - %s", st, st == 0 ? "up" : "FAILED");
    }
  } else if (std::strcmp(cmd, "bus") == 0) {
    const unsigned long secs = arg != nullptr ? std::strtoul(arg, nullptr, 10) : 0;
    if (secs < 1 || secs > 600) {
      n = std::snprintf(line, sizeof(line), "bus: seconds 1-600");
    } else if (g_bus_run.load(std::memory_order_relaxed)) {
      n = std::snprintf(line, sizeof(line), "bus: already running");
    } else if (g_radio_status.load(std::memory_order_relaxed) != 0) {
      n = std::snprintf(line, sizeof(line), "bus: radio not up; send radio");
    } else if (!board_sd_begin()) {
      n = std::snprintf(line, sizeof(line), "bus: microSD NOT MOUNTED");
    } else {
      bus_reset();
      g_bus_seconds = static_cast<uint32_t>(secs);
      g_bus_end_ms  = now_ms() + g_bus_seconds * 1000;
      g_bus_run.store(true, std::memory_order_relaxed);
      n = std::snprintf(line, sizeof(line), "bus: %lu s started", secs);
    }
  } else if (std::strcmp(cmd, "beep") == 0) {
    board_beep(2000, 200);
    n = std::snprintf(line, sizeof(line), "beep");
  } else if (std::strcmp(cmd, "restart") == 0) {
    Serial.println(F("restart"));
    Serial.flush();
    esp_restart();
  } else if (std::strcmp(cmd, "hang") == 0) {
    Serial.println(F("hang: interrupts off"));
    Serial.flush();
    portDISABLE_INTERRUPTS();
    for (;;) {
    }
  } else {
    n = std::snprintf(line, sizeof(line), "commands: relay <1-4> [ms] | in | sense | sd | beep | radio | bus <s> | restart | hang");
  }
  write_line(line, n, sizeof(line));
}

// GL1 - a queue of leveled lines and the microSD log arrive here. Today it runs the bench
// console and prints the pass counts. It is the only task that writes to Serial: a full USB
// CDC buffer blocks the writer, and no other task may be that writer. The first report
// waits a period, so it cannot interleave with setup()'s last banner line.
void log_task(void*) {
  char     line[160];
  char     cmd[48];
  size_t   cmd_len    = 0;
  uint32_t last_alive = now_ms();
  uint32_t pulse_seq  = 0;
  uint32_t bus_next   = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(20));
    if (g_bus_run.load(std::memory_order_relaxed)) {
      // log_task's half of the bus test: one microSD append a pass, as the microSD log will.
      const int64_t t0 = esp_timer_get_time();
      const bool    ok = board_sd_append("/gl1bus.txt", "GL1 bus test");
      note_max(g_bus.sd_max_us, t0);
      (ok ? g_bus.sd_ok : g_bus.sd_bad).fetch_add(1, std::memory_order_relaxed);
      const uint32_t now = now_ms();
      if (bus_next == 0) bus_next = now + 1000;
      if (reached(now, g_bus_end_ms)) {
        g_bus_run.store(false, std::memory_order_relaxed);
        bus_next = 0;
        char head[16];
        std::snprintf(head, sizeof(head), "%lu s done", static_cast<unsigned long>(g_bus_seconds));
        write_line(line, bus_line(line, sizeof(line), head), sizeof(line));
      } else if (reached(now, bus_next)) {
        // A line a second, so a reset in the middle still leaves the trend in the log.
        char head[16];
        std::snprintf(head, sizeof(head), "t %lu", static_cast<unsigned long>(g_bus_seconds - (g_bus_end_ms - now) / 1000));
        bus_next += 1000;
        write_line(line, bus_line(line, sizeof(line), head), sizeof(line));
      }
    }
    while (Serial.available() > 0) {
      const int c = Serial.read();
      if (c == '\r' || c == '\n') {
        cmd[cmd_len] = '\0';
        if (cmd_len > 0) console_command(cmd);
        cmd_len = 0;
      } else if (cmd_len < sizeof(cmd) - 1) {
        cmd[cmd_len++] = static_cast<char>(c);
      }
    }

    const IoSnapshot s = io_snapshot();
    if (s.pulse_seq != pulse_seq) {
      pulse_seq = s.pulse_seq;
      size_t n;
      if (s.pulse_result == PulseResult::Started) {
        const int64_t us = s.pulse_off_us - s.pulse_on_us;
        n = std::snprintf(line, sizeof(line), "relay: K%u on for %ld.%01ld ms (expander writes)",
                          s.pulse_relay + 1, static_cast<long>(us / 1000), static_cast<long>((us % 1000) / 100));
      } else {
        n = std::snprintf(line, sizeof(line), "relay: K%u refused, %s", s.pulse_relay + 1,
                          pulse_result_name(s.pulse_result));
      }
      write_line(line, n, sizeof(line));
    }

    if (!reached(now_ms(), last_alive + kAliveReportMs)) continue;
    last_alive += kAliveReportMs;
    count(TaskId::Log);
    size_t n = static_cast<size_t>(std::snprintf(line, sizeof(line), "alive:"));
    for (size_t i = 0; i < kTaskCount && n < sizeof(line); ++i) {
      n += static_cast<size_t>(std::snprintf(
          line + n, sizeof(line) - n, " %s=%lu", task_table()[i].name,
          static_cast<unsigned long>(g_passes[i].load(std::memory_order_relaxed))));
    }
    write_line(line, n, sizeof(line));
  }
}

struct Slot {
  TaskFunction_t body;
  StackType_t*   stack;
  size_t         stack_bytes;
};

// Indexed by TaskId. start_tasks() refuses a row whose stack here differs from the table's
// size, and the banner's task count then shows the shortfall.
const Slot kSlots[kTaskCount] = {
    {io_task, g_stack_io, sizeof(g_stack_io)},
    {vedirect_task, g_stack_vedirect, sizeof(g_stack_vedirect)},
    {lora_task, g_stack_lora, sizeof(g_stack_lora)},
    {app_task, g_stack_app, sizeof(g_stack_app)},
    {bms_task, g_stack_bms, sizeof(g_stack_bms)},
    {ui_task, g_stack_ui, sizeof(g_stack_ui)},
    {log_task, g_stack_log, sizeof(g_stack_log)},
};

static_assert(sizeof(StackType_t) == 1, "ESP-IDF counts stack depth in bytes (tasks.h)");

}  // namespace

size_t start_tasks(const PageText& boot_page) {
  g_boot_page = boot_page;
  g_pulse_q = xQueueCreateStatic(4, sizeof(PulseRequest), g_pulse_q_buf, &g_pulse_q_storage);
  size_t started = 0;
  for (size_t i = 0; i < kTaskCount; ++i) {
    const TaskSpec& spec = task_table()[i];
    if (kSlots[i].stack_bytes != spec.stack_bytes) continue;  // a table row and a stack disagree
    TaskHandle_t h = xTaskCreateStaticPinnedToCore(
        kSlots[i].body, spec.name, spec.stack_bytes, nullptr, spec.priority, kSlots[i].stack,
        &g_tcb[i], spec.core == kAnyCore ? tskNO_AFFINITY : spec.core);
    if (h != nullptr) ++started;
  }
  return started;
}

uint32_t task_passes(TaskId id) {
  return g_passes[static_cast<size_t>(id)].load(std::memory_order_relaxed);
}

}  // namespace gatelink
