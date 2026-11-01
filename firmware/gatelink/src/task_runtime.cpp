// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task creation and the task bodies. Task L6; GateLink Impl Plan 5.2.
//
// io_task does real work from GL1: relay pulses, input debounce, buttons and the sensors,
// and from GL3 the relay sequence of each gate command. lora_task runs lran-node's engine
// from GL3. vedirect_task reads the MPPT and carries its HEX transactions from GL4. log_task
// carries the bench console. Every other body is a stub that counts its passes and waits
// out its period; the milestone that fills it is named at it.
//
// THE TASK WATCHDOG WATCHES app_task ALONE (Impl Plan 5.2), so a stalled application is
// not masked by a healthy io_task. Its timeout is the parameter watchdog_timeout_s
// (decided 2026-10-07; root rule 8), armed in start_tasks() before any task runs. The idle
// task on core 0, which Arduino-ESP32 subscribes at boot, runs to the same timeout.

#include "task_runtime.h"

#include <Arduino.h>
#include <bootloader_random.h>
#include <esp32s3/rom/rtc.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ah_drawn.h"
#include "alarm_watch.h"
#include "ble_interlock.h"
#include "bms_client.h"
#include "bms_link.h"
#include "charge_inhibit.h"
#include "board_profile.h"
#include "board_stamplc.h"
#include "config_store.h"
#include "detect.h"
#include "gate_io.h"
#include "gate_state.h"
#include "gatelink_app.h"
#include "file_dump.h"
#include "inject.h"
#include "mppt_error_watch.h"
#include "roll_entropy.h"
#include "lran/node/engine.h"
#include "lran/node/names.h"
#include "lran/node/phy_trial.h"
#include "mbedtls_mac.h"
#include "nvs_blob.h"
#include "radio.h"
#include "ved_link.h"

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
StackType_t  g_stack_log[6144];

// How often log_task prints the pass counts.
constexpr uint32_t kAliveReportMs = 30000;

// io_task counts milliseconds in ticks.
static_assert(configTICK_RATE_HZ == 1000, "io_task's deadlines assume a 1 ms tick");

// How often io_task looks again at a sequence that is waiting to start its next pulse.
constexpr uint32_t kSequenceTickMs = 10;

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
  bool         inputs_read   = false;  // the first read has landed; the debouncer takes it as stable
  // Bit changes seen since boot, raw and after debounce. A bounce or a tap shorter than
  // input_debounce_samples polls adds to the first and not the second.
  uint32_t     raw_edges     = 0;
  uint32_t     inputs_edges  = 0;
  uint8_t      relays        = 0;
  // Pulses started on K1-K4 since boot, by any path. app_task attributes holds and openings
  // to GateLink's own K1 and K3 by them (Impl Plan 6.2, 6.3).
  uint32_t     relay_starts[4] = {};
  uint8_t      inject_mask   = 0;  // the inputs injection overrides, in input_bits' sense
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
  // GL3 - the end of each gate command's relay sequence, which lora_task's COMMAND_ACK waits
  // for (Impl Plan 5.2). cmd_seq is bumped once per sequence, after its last trailing edge.
  uint32_t     cmd_seq       = 0;
  SequenceEnd  cmd_end       = SequenceEnd::Done;
  bool         cmd_write_failed = false;  // the last sequence lost a write: D78, spec 8.2
  uint32_t     cmd_i2c_failures = 0;  // expander writes a sequence lost, since boot
};

// main.cpp's alone, because version.py stamps LRAN_GATELINK_GIT into that one object.
const char* g_git = "";

portMUX_TYPE  g_io_mux = portMUX_INITIALIZER_UNLOCKED;

// The console's `relayfault`: how many leading edges to skip and report as failed.
std::atomic<uint32_t> g_relay_write_faults{0};

bool take_relay_write_fault() {
  uint32_t left = g_relay_write_faults.load(std::memory_order_relaxed);
  return left != 0 && g_relay_write_faults.compare_exchange_strong(left, left - 1, std::memory_order_relaxed);
}
IoSnapshot    g_io;
StaticQueue_t g_pulse_q_storage;
uint8_t       g_pulse_q_buf[4 * sizeof(PulseRequest)];
QueueHandle_t g_pulse_q = nullptr;

// lora_task's gate commands. Depth 1: the engine runs one command at a time (spec 9.4), so a
// full queue means a second caller, and execute() answers ACTUATOR_BUSY.
StaticQueue_t g_cmd_q_storage;
uint8_t       g_cmd_q_buf[sizeof(RelaySequence)];
QueueHandle_t g_cmd_q = nullptr;

// io_task tells lora_task that a sequence ended. Written before any task starts.
TaskHandle_t g_lora_handle = nullptr;

// GL3 - input injection (PRD R-5.4b). app_task plays the script and stores the result here,
// mask in the high byte and value in the low; io_task applies it below the debounce. The
// console hands app_task a script through the text and the request.
std::atomic<uint16_t> g_inject{0};
constexpr uint8_t     kInjectStart = 1;
constexpr uint8_t     kInjectOff   = 2;
std::atomic<uint8_t>  g_inject_req{0};
char                  g_inject_text[160];
std::atomic<bool>     g_inject_running{false};
std::atomic<uint8_t>  g_inject_step{0};

// GL3 - app_task's derived gate block, for STATUS and EVENT (spec 7.2.1), and what it asks
// lora_task to send. One event_id counter for both tasks (spec 7.3).
EventIds      g_event_ids;
portMUX_TYPE  g_gate_mux = portMUX_INITIALIZER_UNLOCKED;
GateSnapshot  g_gate;
StaticQueue_t g_push_q_storage;
uint8_t       g_push_q_buf[8 * sizeof(Push)];
QueueHandle_t g_push_q = nullptr;
std::atomic<uint32_t> g_push_dropped{0};  // root rule 4: a push the queue had no room for

// GL5 - app_task's charge-inhibited indication (PRD R-6.4a, R-6.4b), for spec 7.2.6 bit 3
// and the console's `chg`. The source and path are ChargeSource values.
std::atomic<bool>    g_charge_inhibited{false};
// app_task's PRD R-6.4c count, spec 7.2.10.
std::atomic<uint16_t> g_ah_drawn_dah{lran::kU16NotAvailable};
std::atomic<uint8_t> g_charge_source{0};
std::atomic<uint8_t> g_charge_path{0};

// The console's `aw`: log_task asks, io_task reads the expander on its next pass (it alone
// owns the internal bus) and bumps the sequence.
std::atomic<bool>     g_aw_req{false};
std::atomic<uint32_t> g_aw_seq{0};
uint8_t               g_aw_regs[kExpanderDumpLen];
bool                  g_aw_ok = false;

// `aw drive <0|1>`, the port-1 probe (engineering log, 2026-10-24). io_task owns the bus,
// so the console asks for the probe the way it asks for the dump.
std::atomic<bool>     g_p1_req{false};
std::atomic<bool>     g_p1_level{false};
std::atomic<bool>     g_p1_redir{false};  // `aw redir`: the direction write alone
std::atomic<uint32_t> g_p1_seq{0};
uint8_t               g_p1_readback = 0;
bool                  g_p1_ok       = false;

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

// ---------------------------------------------------------------------------
// Configuration (GL3, Impl Plan 6.4). start_tasks() reads the card before any task runs;
// lora_task owns the Store from then on, because CONFIG arrives there.
// ---------------------------------------------------------------------------

constexpr const char* kConfigPath = "/config.json";

class SdConfigFile final : public ConfigFile {
 public:
  bool   mount(bool remount) override { return board_sd_mount(remount); }
  size_t read(char* out, size_t cap) override { return board_sd_read(kConfigPath, out, cap); }
  bool   write(const char* text, size_t n) override {
    return board_sd_replace(kConfigPath, text, n);
  }
};

SdConfigFile   g_config_file;
GateLinkConfig g_config(&g_config_file);

// Each row's effective value, for the tasks that read one. lora_task writes them, from
// GateLinkPort::param_changed(); the table they index is fixed once g_config is built.
std::atomic<int32_t> g_live[lran::config::kMaxTableParams];

size_t live_index(uint16_t id) {
  const lran::config::Table& t = g_config.table();
  for (size_t i = 0; i < t.size(); ++i) {
    if (t.at(i)->id == id) return i;
  }
  return lran::config::kMaxTableParams;
}

uint32_t live_param(uint16_t id) {
  const size_t i = live_index(id);
  return i < lran::config::kMaxTableParams
             ? static_cast<uint32_t>(g_live[i].load(std::memory_order_relaxed))
             : param_default(id);
}

uint32_t bms_cap_ms() { return live_param(kParamBmsWindowMaxMs); }

void publish_param(uint16_t id, int32_t v) {
  const size_t i = live_index(id);
  if (i < lran::config::kMaxTableParams) g_live[i].store(v, std::memory_order_relaxed);
}

// R-5.2a - io_task never blocks on anything but its own period, and vTaskDelayUntil keeps
// that period from drifting by the length of a pass. tools/checks/io_task_never_blocks.py
// reads this function.
//
// It wakes at the next poll or at a pulse's trailing edge, whichever is sooner, so the edge
// is late by at most a tick and an I2C write rather than by up to input_poll_ms. A pulse's
// leading edge waits for the next poll, which delays the command but not its width.
//
// Its five parameters are read each pass, so a SET takes effect at the next pass, inside a
// running sequence too.
void io_task(void*) {
  RelayPulser pulser;
  Debouncer   debounce;
  CommandSequencer sequencer;
  uint32_t         seq_i2c_failures = 0;

  TickType_t last        = xTaskGetTickCount();
  uint32_t   next_poll   = now_ms();
  uint32_t   next_sensor = now_ms();

  // IN5-IN8 do not sample until P1's direction register is written, though it already reads
  // back as all inputs (engineering log, 2026-10-24). The same write from board_begin() did
  // not hold: something between it and the first pass leaves the register's value intact
  // while the pins stay inert. Writing it here, from the task that owns the bus, is what
  // made IN5 and IN6 read on the bench.
  bool port1_ready = false;

  for (;;) {
    count(TaskId::Io);
    if (!port1_ready) {
      uint8_t port1 = 0;
      port1_ready   = board_redirect_port1_inputs(&port1);
      if (!port1_ready) ++g_io.i2c_failures;
    }
    const uint32_t now        = now_ms();
    const uint32_t poll_ms    = live_param(kParamInputPollMs);
    const uint32_t width_ms   = live_param(kParamRelayPulseMs);
    const uint32_t spacing_ms = live_param(kParamRelayMinSpacingMs);
    const uint32_t settle_ms  = live_param(kParamUnlockSettleMs);
    const uint8_t  samples    = static_cast<uint8_t>(live_param(kParamInputDebounceSamples));

    // The trailing edge first: it is the one with a deadline.
    if (pulser.update(now)) {
      const bool    ok  = board_write_relays(pulser.mask());
      const int64_t t   = esp_timer_get_time();
      if (!ok && sequencer.running()) ++seq_i2c_failures;
      portENTER_CRITICAL(&g_io_mux);
      g_io.relays       = pulser.mask();
      g_io.pulse_off_us = t;
      if (!ok) ++g_io.i2c_failures;
      ++g_io.pulse_seq;
      portEXIT_CRITICAL(&g_io_mux);
    }

    // GL3 - a gate command's sequence. Taken only between sequences; the engine sends no
    // second one until the first is acknowledged.
    RelaySequence cmd;
    if (!sequencer.running() && xQueueReceive(g_cmd_q, &cmd, 0) == pdTRUE) {
      sequencer.begin(cmd, now);
    }
    if (sequencer.service(pulser, width_ms, spacing_ms, settle_ms, now)) {
      const bool    ok = !take_relay_write_fault() && board_write_relays(pulser.mask());
      const int64_t t  = esp_timer_get_time();
      if (!ok) ++seq_i2c_failures;
      portENTER_CRITICAL(&g_io_mux);
      g_io.relays       = pulser.mask();
      g_io.pulse_relay  = static_cast<uint8_t>(__builtin_ctz(pulser.mask()));
      ++g_io.relay_starts[g_io.pulse_relay & 3];
      g_io.pulse_result = PulseResult::Started;
      g_io.pulse_on_us  = t;
      if (!ok) ++g_io.i2c_failures;
      portEXIT_CRITICAL(&g_io_mux);
    }
    SequenceEnd end;
    if (sequencer.take_end(&end)) {
      portENTER_CRITICAL(&g_io_mux);
      g_io.cmd_end = end;
      g_io.cmd_write_failed = seq_i2c_failures != 0;
      g_io.cmd_i2c_failures += seq_i2c_failures;
      ++g_io.cmd_seq;
      portEXIT_CRITICAL(&g_io_mux);
      seq_i2c_failures = 0;
      if (g_lora_handle != nullptr) xTaskNotifyGive(g_lora_handle);
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
        ++g_io.relay_starts[req.relay & 3];
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
        // `raw` stays the opto's levels for the console's `in`; everything past the
        // debounce sees asserted inputs.
        // R-5.4b - injection replaces the inputs it names, below the debounce.
        const uint16_t inj    = g_inject.load(std::memory_order_relaxed);
        const uint8_t  mask   = static_cast<uint8_t>(inj >> 8);
        const uint8_t  live   = static_cast<uint8_t>((inputs_from_levels(raw) & ~mask) | (inj & mask));
        const uint8_t  inputs = debounce.update(live, samples);
        g_io.inject_mask = mask;
        g_io.raw_edges += static_cast<uint32_t>(__builtin_popcount(raw ^ g_io.inputs_raw));
        g_io.inputs_edges += static_cast<uint32_t>(__builtin_popcount(inputs ^ g_io.inputs));
        g_io.inputs_raw = raw;
        g_io.inputs     = inputs;
        g_io.inputs_read = true;
      } else {
        ++g_io.i2c_failures;
      }
      g_io.buttons |= buttons;
      portEXIT_CRITICAL(&g_io_mux);
    }

    if (g_aw_req.exchange(false)) {
      uint8_t    regs[kExpanderDumpLen] = {};
      const bool ok = board_dump_expander(regs);
      portENTER_CRITICAL(&g_io_mux);
      std::memcpy(g_aw_regs, regs, sizeof(regs));
      g_aw_ok = ok;
      if (!ok) ++g_io.i2c_failures;
      portEXIT_CRITICAL(&g_io_mux);
      g_aw_seq.fetch_add(1);
    }

    if (g_p1_req.exchange(false)) {
      uint8_t    readback = 0;
      // level 2 asks for the direction write alone, without driving the pins.
      const bool ok = g_p1_redir.load() ? board_redirect_port1_inputs(&readback)
                                        : board_probe_port1(g_p1_level.load(), &readback);
      portENTER_CRITICAL(&g_io_mux);
      g_p1_readback = readback;
      g_p1_ok       = ok;
      if (!ok) ++g_io.i2c_failures;
      portEXIT_CRITICAL(&g_io_mux);
      g_p1_seq.fetch_add(1);
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
    // A sequence waiting out spacing or unlock_settle_ms starts within a few ticks of its
    // time rather than at the next poll: an immediate close is two of these.
    if (sequencer.running() && pulser.mask() == 0 && wait > kSequenceTickMs) wait = kSequenceTickMs;
    const uint32_t target = after + wait;
    TickType_t     inc    = static_cast<TickType_t>(target - static_cast<uint32_t>(last));
    if (static_cast<int32_t>(inc) < 1) inc = 1;
    vTaskDelayUntil(&last, inc);
  }
}

// ---------------------------------------------------------------------------
// GL4 - vedirect_task owns Serial1 and the VedLink on it (Impl Plan 4.2.4). lora_task hands it
// a HEX_REQ through a queue and takes the answer from a slot; every other task reads the
// MPPT through a snapshot copied under a spinlock.
// ---------------------------------------------------------------------------

// Depth 1: the engine holds one HEX transaction at a time (spec 8.13).
StaticQueue_t g_hex_q_storage;
uint8_t       g_hex_q_buf[sizeof(HexJob)];
QueueHandle_t g_hex_q = nullptr;

struct VedView {
  MpptSnapshot mppt;
  bool         busy        = false;  // a job is with the MPPT
  bool         result_ready = false;
  HexResult    result;
  bool         have_block  = false;
  uint32_t     block_age_ms = 0;
  vedirect::TextCounters text;
  VedCounters  link;
};
portMUX_TYPE g_ved_mux = portMUX_INITIALIZER_UNLOCKED;
VedView      g_ved;

// The longest vedirect_task sleeps with nothing received. It bounds how late a retry or a
// TIMEOUT can be, which kHexBackstopMarginMs must exceed.
constexpr uint32_t kVedWaitMs = 20;
static_assert(kVedWaitMs < kHexBackstopMarginMs, "the engine's backstop must outlast vedirect_task's wait");

class SerialWriter final : public VedWriter {
 public:
  bool write_line(const char* s, size_t n) override {
    size_t w = Serial1.write(reinterpret_cast<const uint8_t*>(s), n);
    w += Serial1.write('\n');
    return w == n + 1;
  }
};

// Static, not on the task's stack: the parser holds two 24-field blocks (root rule 3).
SerialWriter g_ved_writer;
VedLink      g_ved_link(&g_ved_writer);
TaskHandle_t g_ved_handle = nullptr;

// Defined with the interlock below. vedirect_task reads it to split its CPU figures.
extern std::atomic<bool> g_ble_holds;

// vedirect_task's CPU share, kept apart for passes with bms_task's BLE window open and
// closed: it wakes about 240 times a second, once per burst of received bytes (engineering
// log, 2026-10-08), and NimBLE shares the chip with it. A pass is timed from wake to done,
// so a pass that io_task or lora_task preempts counts their time too: the share is an
// upper bound. Read with `ved cpu`, under g_ved_mux.
struct VedCpu {
  uint64_t busy_us     = 0;  // wake to done, summed
  uint64_t wall_us     = 0;  // wake to wake, summed
  uint32_t passes      = 0;
  uint32_t max_pass_us = 0;
  uint32_t max_gap_us  = 0;  // wake to wake; much past kVedWaitMs means it waited for a CPU
};
VedCpu            g_ved_cpu[2];  // [0] window closed, [1] window open
std::atomic<bool> g_ved_cpu_reset{false};

// Waits on UART RX events, and wakes at least every kVedWaitMs for the HEX timers.
void vedirect_task(void*) {
  g_ved_handle = xTaskGetCurrentTaskHandle();
  VedParams params{live_param(kParamHexTimeoutMs), live_param(kParamVedirectStaleS) * 1000};
  g_ved_link.set_params(params);
  // Before begin(), or the core ignores it. A text block is a few hundred bytes; 1 KB holds
  // several, so a late pass loses none (engineering log, 2026-10-08).
  Serial1.setRxBufferSize(1024);
  Serial1.begin(kVedBaud, SERIAL_8N1, kVedUartRx, kVedUartTx);
  Serial1.onReceive([] { xTaskNotifyGive(g_ved_handle); });

  int64_t last_wake_us = 0;
  for (;;) {
    count(TaskId::Vedirect);
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kVedWaitMs));
    const int64_t  wake_us = esp_timer_get_time();
    const bool     ble_open = g_ble_holds.load(std::memory_order_relaxed);
    const uint32_t now = now_ms();
    // A SET of either takes effect here; a HEX transaction under way keeps its deadline.
    const VedParams live{live_param(kParamHexTimeoutMs), live_param(kParamVedirectStaleS) * 1000};
    if (live.hex_timeout_ms != params.hex_timeout_ms || live.stale_ms != params.stale_ms) {
      params = live;
      g_ved_link.set_params(params);
    }
    while (Serial1.available() > 0) g_ved_link.feed(static_cast<uint8_t>(Serial1.read()), now);

    HexJob job;
    if (!g_ved_link.busy() && xQueueReceive(g_hex_q, &job, 0) == pdTRUE) g_ved_link.start(job, now);
    g_ved_link.tick(now);

    HexResult  r;
    const bool answered = g_ved_link.take_result(&r);
    portENTER_CRITICAL(&g_ved_mux);
    g_ved.mppt         = g_ved_link.mppt(now);
    g_ved.busy         = g_ved_link.busy();
    if (answered) {
      g_ved.result       = r;
      g_ved.result_ready = true;
    }
    g_ved.have_block   = g_ved_link.has_block();
    g_ved.block_age_ms = now - g_ved_link.last_block_ms();
    g_ved.text         = g_ved_link.parser().counters();
    g_ved.link         = g_ved_link.counters();
    portEXIT_CRITICAL(&g_ved_mux);
    if (answered) xTaskNotifyGive(g_lora_handle);

    const uint32_t busy = static_cast<uint32_t>(esp_timer_get_time() - wake_us);
    const uint32_t gap  = static_cast<uint32_t>(wake_us - last_wake_us);
    portENTER_CRITICAL(&g_ved_mux);
    if (g_ved_cpu_reset.exchange(false)) {
      g_ved_cpu[0] = VedCpu{};
      g_ved_cpu[1] = VedCpu{};
    } else if (last_wake_us != 0) {
      VedCpu& c = g_ved_cpu[ble_open ? 1 : 0];
      c.busy_us += busy;
      c.wall_us += gap;
      c.passes++;
      if (busy > c.max_pass_us) c.max_pass_us = busy;
      if (gap > c.max_gap_us) c.max_gap_us = gap;
    }
    portEXIT_CRITICAL(&g_ved_mux);
    last_wake_us = wake_us;
  }
}

VedView ved_view() {
  portENTER_CRITICAL(&g_ved_mux);
  const VedView copy = g_ved;
  portEXIT_CRITICAL(&g_ved_mux);
  return copy;
}

// ---------------------------------------------------------------------------
// GL3 - the protocol node. lora_task owns the engine, the context and the application, and
// is their only caller, so the engine's lack of a lock is safe (Impl Plan 5.2): check() runs
// in receive(), and finish_command() runs here when io_task reports the end of a sequence.
// ---------------------------------------------------------------------------

// Engine log lines, carried to log_task, the only task that writes to Serial. A full queue
// drops the line and counts it: lora_task never waits on logging.
constexpr size_t kLogLineLen   = 120;
constexpr size_t kLogQueueDepth = 8;
struct LogLine {
  char text[kLogLineLen];
};
// PRD R-5.4e - the fault log's lines, carried to log_task, which owns the card's log files.
constexpr size_t      kFaultQueueDepth = 8;
StaticQueue_t         g_fault_q_storage;
uint8_t               g_fault_q_buf[kFaultQueueDepth * sizeof(LogLine)];
QueueHandle_t         g_fault_q = nullptr;
std::atomic<uint32_t> g_fault_dropped{0};  // root rule 4: a line the queue had no room for
uint32_t              g_fault_failed = 0;  // log_task's: a line the card did not take

void fault_line(const char* text) {
  LogLine l{};
  if (format_fault_line(l.text, sizeof(l.text), static_cast<uint32_t>(esp_timer_get_time() / 1000000), text) == 0 ||
      xQueueSend(g_fault_q, &l, 0) != pdTRUE) {
    g_fault_dropped.fetch_add(1, std::memory_order_relaxed);
  }
}

StaticQueue_t         g_log_q_storage;
uint8_t               g_log_q_buf[kLogQueueDepth * sizeof(LogLine)];
QueueHandle_t         g_log_q = nullptr;
std::atomic<uint32_t> g_log_dropped{0};

class QueueSink final : public lran::node::Sink {
 public:
  void line(const char* text) override {
    LogLine l{};
    std::strncpy(l.text, text, sizeof(l.text) - 1);
    if (xQueueSend(g_log_q, &l, 0) != pdTRUE) g_log_dropped.fetch_add(1, std::memory_order_relaxed);
  }
};

// PRD R-3.5h, spec 10.1. esp_random() is a true entropy source only while the RF subsystem
// runs or bootloader_random_enable() holds the ADC's noise source on. At boot BLE has not
// started, so start_tasks() draws this word with the source enabled, before any task runs;
// the source must be off again before bms_task starts the controller.
uint32_t g_boot_entropy = 0;

// D87 - words for later rolls: start_tasks() fills it, and each BLE window tops it up.
RollEntropy  g_roll_entropy;
portMUX_TYPE g_roll_entropy_mux = portMUX_INITIALIZER_UNLOCKED;

void roll_entropy_put(uint32_t word) {
  portENTER_CRITICAL(&g_roll_entropy_mux);
  g_roll_entropy.put(word);
  portEXIT_CRITICAL(&g_roll_entropy_mux);
}

// bms_task's draw, with the controller up.
void roll_entropy_top_up() { roll_entropy_put(esp_random()); }

// spec 8.14. A REBOOT command resets through esp_restart(), which the chip reports as a
// software reset; this word, which survives a software reset, tells the two apart.
constexpr uint32_t        kRebootMarker = 0x5245424Fu;  // "REBO"
RTC_NOINIT_ATTR uint32_t g_reboot_marker;

lran::ResetCause reset_cause() {
  const bool commanded = g_reboot_marker == kRebootMarker;
  g_reboot_marker      = 0;
  if (usb_chip_reset()) return lran::ResetCause::External;
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return lran::ResetCause::PowerOn;
    case ESP_RST_SW:       return commanded ? lran::ResetCause::RebootCommand : lran::ResetCause::Software;
    case ESP_RST_PANIC:    return lran::ResetCause::Panic;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return lran::ResetCause::Watchdog;
    case ESP_RST_BROWNOUT: return lran::ResetCause::Brownout;
    case ESP_RST_EXT:      return lran::ResetCause::External;
    default:               return lran::ResetCause::Unknown;
  }
}

// spec 10.7, D72 - FIRE and the hard-shutdown latch as spec 7.2.1's input_bits name them,
// IN5 and IN6. An edge after boot is AlarmWatch's, in app_task.
constexpr uint8_t kInputFire         = 1u << 4;
constexpr uint8_t kInputHardShutdown = 1u << 5;

// ---------------------------------------------------------------------------
// The LoRa/BLE interlock and bms_task's results (GL5; PRD R-3.4a-R-3.4f, R-4.3h; Impl Plan
// 5.2). bms_task holds the mutex from controller start to de-init. lora_task tries it,
// never waiting, before each frame's media access.
// ---------------------------------------------------------------------------


StaticSemaphore_t     g_ble_mutex_storage;
SemaphoreHandle_t     g_ble_mutex = nullptr;
std::atomic<bool>     g_ble_holds{false};       // bms_task holds the interlock
std::atomic<uint32_t> g_ble_start_ms{0};        // when it took it
std::atomic<uint32_t> g_ble_abort_ms{0};        // when lora_task asked for it back; 0 = not
bool                  g_radio_holds = false;    // lora_task's alone

// Counted on the console's `bms` line (root rule 4).
std::atomic<uint32_t> g_ble_tx_waits{0};     // passes a frame waited for a window
std::atomic<uint32_t> g_ble_aborts_asked{0}; // windows lora_task cut short
std::atomic<uint32_t> g_ble_overruns{0};     // frames sent past the cap, without the lock

}  // namespace

bool ble_interlock_tx_take(const uint8_t* frame, size_t len, uint32_t now_ms) {
  if (g_radio_holds) return true;
  if (g_ble_mutex != nullptr && xSemaphoreTake(g_ble_mutex, 0) == pdTRUE) {
    g_radio_holds = true;
    return true;
  }
  const bool     holds  = g_ble_holds.load();
  const uint32_t window = now_ms - g_ble_start_ms.load();
  switch (tx_gate(holds, window, bms_cap_ms(), reply_awaited(frame, len))) {
    case TxGate::Go:
      // bms_task is between the mutex and its flag; the next pass takes the mutex.
      return false;
    case TxGate::Wait:
      g_ble_tx_waits.fetch_add(1, std::memory_order_relaxed);
      return false;
    case TxGate::WaitAbort: {
      uint32_t none = 0;
      if (g_ble_abort_ms.compare_exchange_strong(none, now_ms == 0 ? 1 : now_ms)) {
        g_ble_aborts_asked.fetch_add(1, std::memory_order_relaxed);
      }
      g_ble_tx_waits.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    case TxGate::Overrun:
      g_ble_overruns.fetch_add(1, std::memory_order_relaxed);
      return true;
  }
  return false;
}

void ble_interlock_tx_release() {
  if (!g_radio_holds) return;
  g_radio_holds = false;
  xSemaphoreGive(g_ble_mutex);
}

namespace {

// spec 10.1 - a roll's ctx_id. Held words run out only after four rolls with no window, as
// with polling suspended. The source is then enabled for the draw, which needs BLE kept
// from starting, so the interlock is held across it. Failing to take the interlock means
// bms_task has it, and the controller is up or about to be.
uint32_t roll_random_u32() {
  portENTER_CRITICAL(&g_roll_entropy_mux);
  const uint32_t held = g_roll_entropy.take();
  portEXIT_CRITICAL(&g_roll_entropy_mux);
  if (held != 0) return held;

  const bool mine = !g_radio_holds && xSemaphoreTake(g_ble_mutex, 0) == pdTRUE;
  if (!mine && !g_radio_holds) return esp_random();
  bootloader_random_enable();
  const uint32_t v = esp_random();
  bootloader_random_disable();
  if (mine) xSemaphoreGive(g_ble_mutex);
  return v;
}

uint32_t boot_random_u32() {
  const uint32_t v = g_boot_entropy;
  g_boot_entropy   = 0;
  return v != 0 ? v : roll_random_u32();
}

// What bms_task publishes, and the console's `bms` line reads.
struct BmsView {
  BmsSnapshot snap;
  BmsTiming   timing;
  BmsEnd      last_end       = BmsEnd::Count;  // Count = no window yet
  uint32_t    window_ms      = 0;              // the last window, interlock to release
  uint32_t    window_max_ms  = 0;
  uint32_t    abort_ms       = 0;              // the last abort's latency, request to release
  uint32_t    abort_max_ms   = 0;
  uint32_t    ends[static_cast<size_t>(BmsEnd::Count)] = {};
  uint32_t    busy           = 0;  // polls skipped because lora_task held the interlock
  uint32_t    suspended      = 0;  // polls skipped by SET_BMS_POLLING
  uint32_t    card_logged    = 0;  // windows appended to /bms.log
  uint32_t    card_failed    = 0;  // windows whose /bms.log line was not written
};
portMUX_TYPE g_bms_mux = portMUX_INITIALIZER_UNLOCKED;
BmsView      g_bms;

// One line per BLE window, for a soak with no console attached (format_bms_log_line()).
constexpr const char* kBmsLogPath = "/bms.log";

// PRD R-3.4e - the HA switch, carried from lora_task's application.
std::atomic<bool> g_bms_polling{true};

// The console's `bms now [abort_ms]`: a window at once, optionally asked to end at
// abort_ms into it, which stands in for a reply queued at that moment.
TaskHandle_t          g_bms_handle = nullptr;
std::atomic<bool>     g_bms_now{false};
std::atomic<uint32_t> g_bms_bench_abort_ms{0};  // 0 = none

void bms_log(const char* line) { QueueSink().line(line); }

bool ble_stop_requested() {
  const uint32_t at = g_bms_bench_abort_ms.load();
  if (at != 0) {
    const uint32_t start = g_ble_start_ms.load();
    if (now_ms() - start >= at) {
      uint32_t none = 0;
      (void)g_ble_abort_ms.compare_exchange_strong(none, start + at);
    }
  }
  return g_ble_abort_ms.load() != 0;
}

BmsSnapshot bms_snapshot() {
  portENTER_CRITICAL(&g_bms_mux);
  const BmsSnapshot b = g_bms.snap;
  portEXIT_CRITICAL(&g_bms_mux);
  return b;
}

// spec 12.4.2 - the committed PHY group in NVS (nvs_blob.h says why not the card), and the
// trial that changes it. lora_task owns all three once it starts.
NvsBlob                g_phy_blob;
lran::node::PhyPersist g_phy_persist(&g_phy_blob);
lran::node::PhyTrial   g_phy(&g_phy_persist);

class Node final : public RadioClient, public GateLinkPort {
 public:
  Node() : engine_(&outbox_, &mac_, &sink_), app_(this, roll_random_u32, &sink_) {}

  void begin(const uint8_t* key, size_t key_len) {
    ctx_.id = lran::kNodeGateLink;
    std::memcpy(ctx_.key, key, key_len < sizeof(ctx_.key) ? key_len : sizeof(ctx_.key));
    lran::node::reset_context(ctx_, boot_random_u32);  // spec 10.1 - a new ctx_id every boot
    // Impl Plan 6.4 - the card's values, read by start_tasks(), applied to every task.
    app_.set_config(&g_config);
    app_.set_event_ids(&g_event_ids);
    app_.apply_all(ctx_);
  }

  // RadioClient
  void on_frame(const uint8_t* buf, size_t len, int16_t rssi_dbm, int16_t snr_db10,
                uint32_t now_ms) override {
    engine_.receive(ctx_, app_, buf, len, rssi_dbm, snr_db10, now_ms);
    // Impl Plan 6.4 - a CONFIG met an absent or dirty card: try it again now, after the
    // answer is queued. A card put back then holds RAM's overrides, and the next answer
    // says PERSISTED. With no card the attempt holds the SPI bus for about 1 s (bench,
    // 2026-10-08), which only configuration traffic pays.
    if (app_.take_card_retry()) {
      QueueSink().line(g_config.persist().refresh() ? "cfg: card back, config.json rewritten"
                                                    : "cfg: card still unusable");
    }
  }
  void            on_phy_crc_error(uint32_t) override { lran::node::Engine::on_phy_crc_error(ctx_); }
  lran::Counters* counters() override { return &ctx_.counters; }

  // GateLinkPort
  bool dispatch(const RelaySequence& seq) override { return xQueueSend(g_cmd_q, &seq, 0) == pdTRUE; }
  NodeSnapshot snapshot() const override {
    const IoSnapshot io = io_snapshot();
    NodeSnapshot     n;
    n.inputs             = io.inputs;
    n.node_mv            = io.sensors.bus_mv;
    n.enclosure_temp_c10 = io.sensors.temp_c10;
    n.uptime_s           = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
    return n;
  }
  GateSnapshot gate() const override {
    portENTER_CRITICAL(&g_gate_mux);
    const GateSnapshot g = g_gate;
    portEXIT_CRITICAL(&g_gate_mux);
    return g;
  }
  BmsSnapshot bms() const override { return bms_snapshot(); }
  bool charge_inhibited() const override { return g_charge_inhibited.load(std::memory_order_relaxed); }
  uint16_t ah_drawn_dah() const override { return g_ah_drawn_dah.load(std::memory_order_relaxed); }
  void bms_polling_changed(bool on) override { g_bms_polling.store(on); }
  MpptSnapshot mppt() const override {
    portENTER_CRITICAL(&g_ved_mux);
    const MpptSnapshot m = g_ved.mppt;
    portEXIT_CRITICAL(&g_ved_mux);
    return m;
  }
  bool hex_submit(const HexJob& job) override {
    // A job still with the MPPT is one the engine has given up on; the next waits for it.
    portENTER_CRITICAL(&g_ved_mux);
    const bool busy = g_ved.busy;
    portEXIT_CRITICAL(&g_ved_mux);
    if (busy) return false;
    const bool queued = xQueueSend(g_hex_q, &job, 0) == pdTRUE;
    if (queued) xTaskNotifyGive(g_ved_handle);
    return queued;
  }
  // Every row reaches g_live; the rows owned by lora_task's own objects are applied here.
  void param_changed(uint16_t id, int32_t v) override {
    publish_param(id, v);
    switch (id) {
      case kParamCadRetries:
      case kParamBackoffMaxMs:
        radio_set_media_access(static_cast<uint8_t>(live_param(kParamCadRetries)),
                               live_param(kParamBackoffMaxMs));
        break;
      case kParamWatchdogTimeoutS:
        // Impl Plan 5.2 - applied when set. start_tasks() armed it at this value already,
        // so boot's apply_all() does not re-arm it.
        if (static_cast<uint32_t>(v) != watchdog_timeout_s() &&
            !apply_watchdog_timeout(static_cast<uint32_t>(v))) {
          QueueSink().line("wdt: new timeout NOT applied");
        }
        break;
      default:
        break;
    }
  }
  bool hex_take(HexResult* out) override {
    portENTER_CRITICAL(&g_ved_mux);
    const bool ready = g_ved.result_ready;
    if (ready) *out = g_ved.result;
    g_ved.result_ready = false;
    portEXIT_CRITICAL(&g_ved_mux);
    return ready;
  }

  lran::node::Engine&       engine() { return engine_; }
  lran::node::Context&      ctx() { return ctx_; }
  GateLinkApp&              app() { return app_; }
  lran::node::Outbox&       outbox() { return outbox_; }

 private:
  lran::node::Outbox     outbox_;
  lran::esp32::MbedtlsMac mac_;
  QueueSink              sink_;
  lran::node::Engine     engine_;
  GateLinkApp            app_;
  lran::node::Context    ctx_;
};

// Static, not on lora_task's stack: the outbox alone is 4 KB, and the engine holds a full
// CONFIG and CONFIG_ACK (root rule 3).
Node g_node;

const uint8_t* g_node_key     = nullptr;
size_t         g_node_key_len = 0;

// What the console's `lran` line reads. Copied by lora_task once a pass; one writer.
struct NodeView {
  uint32_t ctx_id      = 0;
  uint16_t tx_seq      = 0;
  bool     pending     = false;
  uint32_t rx_frames   = 0;
  uint32_t tx_frames   = 0;
  uint32_t rejected    = 0;  // ctx, MAC and seq refusals
  uint32_t dup_command = 0;
  uint32_t executions  = 0;
  uint32_t dispatched  = 0;
  uint32_t busy        = 0;
  uint32_t dropped     = 0;  // answers the outbox had no room for
  uint32_t cmd_i2c     = 0;
  uint8_t  outbox      = 0;  // frames waiting for media access
  bool     tx_active   = false;
  // The configuration's store (Impl Plan 6.4), for the console's `cfg`.
  bool         card_mounted = false;
  bool         card_dirty   = false;
  bool         unpersisted  = false;
  PersistStats card;
  // spec 12.4.2, for the console's `phy`.
  lran::node::PhyState      phy_state     = lran::node::PhyState::Idle;
  bool                      phy_writable  = false;
  uint32_t                  phy_window_ms = 0;
  lran::node::PhyGroup      phy_effective{};
  lran::node::PhyGroup      phy_committed{};
  lran::node::PhyTrialStats phy_stats;
};
portMUX_TYPE g_view_mux = portMUX_INITIALIZER_UNLOCKED;
NodeView     g_view;

// The console's `lran ctx new`: a new ctx_id that the bridge does not hear, as a reboot
// whose BOOT frames were lost would leave it. The bridge's next command then meets spec
// 10.3's resync, which GL3 checks on the bench. lora_task owns the context, so the
// console only asks.
std::atomic<bool> g_ctx_new_req{false};
// The console's `lran ack drop`, carried to lora_task, which owns the application.
std::atomic<bool> g_ack_drop_req{false};
// The console's `phy reset`, carried to lora_task, which owns the trial.
std::atomic<bool> g_phy_reset_req{false};

// spec 12.4.2 steps 6 and 8 - a revert, logged and owed to the bridge as PHY_REVERTED.
void note_phy_revert(lran::node::RevertCause cause) {
  lran::node::Engine::note_phy_revert(g_node.ctx(), cause);
  QueueSink().line(cause == lran::node::RevertCause::Reboot
                       ? "phy: REVERTED to the committed group - a reboot ended the trial"
                       : "phy: REVERTED to the committed group - the window expired");
}

// spec 12.4.2 step 3 - hands an owed retune to the radio, and opens the window once the
// radio reports it applied. radio_service() waits for an empty outbox first, so the
// CONFIG_ACK that caused the retune goes out on the old settings. The simnode's
// phy_service() is the same.
bool g_retune_requested = false;

void phy_service(uint32_t now) {
  const lran::node::RevertCause reverted = g_phy.tick(now);
  if (reverted != lran::node::RevertCause::None) note_phy_revert(reverted);
  if (g_phy.retune_due() && !g_retune_requested) {
    lran::link::PhyConfig next = radio_phy();
    if (!lran::node::phy_config_from(g_phy.group(), radio_phy(), &next)) {
      // Unreachable while the table caps tx_power_dbm at D33's ceiling. The radio keeps its
      // settings; the window still opens, and silence reverts it.
      QueueSink().line("phy: group breaks D33's EIRP ceiling - radio NOT retuned");
    }
    radio_request_phy(next);
    g_retune_requested = true;
  }
  if (g_retune_requested && radio_take_retuned()) {
    g_retune_requested = false;
    g_phy.on_retuned(now);
    const lran::link::PhyConfig& p = radio_phy();
    char text[112];
    std::snprintf(text, sizeof(text), "phy: retuned to %lu Hz SF%u BW %u kHz CR 4/%u %d dBm, board %s",
                  static_cast<unsigned long>(p.freq_hz), static_cast<unsigned>(p.sf),
                  static_cast<unsigned>(p.bw_khz10 / 10), static_cast<unsigned>(p.cr_denom),
                  static_cast<int>(p.conducted_dbm), lran::node::phy_state_name(g_phy.state()));
    QueueSink().line(text);
  }
}

// lora_task waits this long on DIO1 or io_task with nothing moving, and this long while a
// frame is in media access or on the air, whose steps are read from the IRQ register.
constexpr uint32_t kLoraIdleWaitMs   = 100;
constexpr uint32_t kLoraActiveWaitMs = 5;

void send_boot_alarm(lran::EventType type, uint32_t now) {
  uint8_t      payload[lran::kMaxSchemaPayload];
  uint8_t      schema = lran::kSchemaNone;
  const size_t n      = g_node.app().build_event(g_node.ctx(), type, 0, now, payload, sizeof(payload), &schema);
  const bool   queued = n > 0 && g_node.engine().send_event(g_node.ctx(), lran::kNodeBridge, payload, n, schema);
  if (!queued) g_node.engine().count_dropped();
  char text[64];
  std::snprintf(text, sizeof(text), "boot: %s present at boot, %s", lran::node::event_type_name(type),
                queued ? "sent again" : "NOT QUEUED");
  QueueSink().line(text);
}

// GL3 - waits on DIO1 and io_task, through one task notification. GL1's bus test still
// drives the radio from here while it runs.
void lora_task(void*) {
  g_node.begin(g_node_key, g_node_key_len);

  // spec 12.4.2 step 7 - the radio boots on the committed group, which is D1's until a
  // change commits. A trial the reboot ended is owed to the bridge as PHY_REVERTED.
  if (!g_phy_blob.begin()) QueueSink().line("phy: NVS refused - no store, every PHY row answers READ_ONLY");
  const lran::node::RevertCause boot_revert = g_phy.begin();
  g_node.engine().set_phy(&g_phy);
  if (boot_revert != lran::node::RevertCause::None) note_phy_revert(boot_revert);
  lran::link::PhyConfig boot_phy = lran::link::kPhy;
  if (!lran::node::phy_config_from(g_phy.group(), lran::link::kPhy, &boot_phy)) {
    QueueSink().line("phy: stored group breaks D33's EIRP ceiling - booting on kPhy");
  }
  const int16_t st = radio_begin(xTaskGetCurrentTaskHandle(), boot_phy);
  g_radio_status.store(st, std::memory_order_relaxed);

  // spec 10.7 - STATUS with BOOT, then the BOOT event with its cause. Queued now; media
  // access sends them once the radio is up.
  const lran::ResetCause cause = reset_cause();
  {
    char text[64];
    std::snprintf(text, sizeof(text), "boot cause=%u ver=%s git=%s", static_cast<unsigned>(cause),
                  LRAN_GATELINK_VERSION, g_git);
    fault_line(text);
  }
  if (!g_node.engine().announce_boot(g_node.ctx(), g_node.app(), cause, now_ms())) {
    QueueSink().line("boot: announcement not queued");
  }
  {
    const RadioStats& r = radio_stats();
    char              text[96];
    std::snprintf(text, sizeof(text), "boot: %s, ctx_id %08lx; radio RST check %s, BUSY fell in %lu us",
                  lran::node::reset_cause_name(cause), static_cast<unsigned long>(g_node.ctx().ctx_id),
                  rst_check_name(r.rst_check), static_cast<unsigned long>(r.rst_busy_fall_us));
    QueueSink().line(text);
  }
  // spec 10.7, D72 - an alarm present at boot is sent again after the BOOT status, once
  // io_task's first read has landed. Before it, `inputs` reads 0 and would hide a fire.
  bool boot_alarms_owed = true;

  {
    const PersistStats& k = g_config.persist().stats();
    char                text[112];
    std::snprintf(text, sizeof(text), "cfg: card %s; %lu restored, %lu refused, %lu unknown%s",
                  g_config.persist().mounted() ? "mounted" : "NOT MOUNTED - defaults",
                  static_cast<unsigned long>(k.restored), static_cast<unsigned long>(k.refused),
                  static_cast<unsigned long>(k.unknown),
                  k.file_corrupt ? "; config.json UNREADABLE, defaults" : "");
    QueueSink().line(text);
  }
  uint32_t cmd_seq = io_snapshot().cmd_seq;
  for (;;) {
    count(TaskId::Lora);
    const bool active = radio_tx_active() || g_node.outbox().size() != 0 || g_retune_requested ||
                        g_bus_run.load(std::memory_order_relaxed);
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(active ? kLoraActiveWaitMs : kLoraIdleWaitMs));
    const uint32_t now = now_ms();

    radio_service(&g_node, &g_node.outbox(), now);

    // Impl Plan 5.2 - the ACK after the last trailing edge, so it can say whether every
    // write in the sequence landed (spec 8.2, D78).
    const IoSnapshot io = io_snapshot();
    if (io.cmd_seq != cmd_seq) {
      cmd_seq = io.cmd_seq;
      if (io.cmd_write_failed) {
        fault_line("actuator_fault");
        g_node.engine().finish_command(g_node.ctx(), g_node.app(), now, lran::AckResult::ActuatorFault);
      } else {
        g_node.engine().finish_command(g_node.ctx(), g_node.app(), now);
      }
    }
    if (boot_alarms_owed && io.inputs_read) {
      boot_alarms_owed = false;
      // The inputs the decision rests on, so a missed alarm shows its cause on the console.
      char text[56];
      std::snprintf(text, sizeof(text), "boot: first input read raw 0x%02x debounced 0x%02x",
                    io.inputs_raw, io.inputs);
      QueueSink().line(text);
      if ((io.inputs & kInputFire) != 0) send_boot_alarm(lran::EventType::FireAsserted, now);
      if ((io.inputs & kInputHardShutdown) != 0) send_boot_alarm(lran::EventType::HardShutdown, now);
    }
    // GL3 - app_task's pushes: STATUS for a gate, hold or detection change, EVENT for a
    // detection (spec 8.7, 8.9). The outbox orders them behind anything already queued.
    Push push;
    while (xQueueReceive(g_push_q, &push, 0) == pdTRUE) {
      bool queued = false;
      if (push.kind == Push::Kind::Status) {
        queued = g_node.engine().send_status(g_node.ctx(), g_node.app(), lran::kNodeBridge,
                                             push.reason, now);
      } else {
        uint8_t      payload[lran::kMaxSchemaPayload];
        uint8_t      schema = lran::kSchemaNone;
        const size_t n = g_node.app().serialize_event(push.event, payload, sizeof(payload), &schema);
        queued = n > 0 && g_node.engine().send_event(g_node.ctx(), lran::kNodeBridge, payload, n, schema);
        char text[64];
        std::snprintf(text, sizeof(text), "event type=%u detail=%u id=%lu gate=%u inputs=0x%02x",
                      push.event.event_type, push.event.detail,
                      static_cast<unsigned long>(push.event.event_id), push.event.gate_state,
                      push.event.input_bits);
        fault_line(text);
      }
      if (!queued) g_node.engine().count_dropped();
    }
    g_node.app().poll_hex(g_node.engine(), g_node.ctx());
    g_node.engine().tick(g_node.ctx(), now);
    if (g_phy_reset_req.exchange(false, std::memory_order_relaxed)) {
      QueueSink().line(g_phy.reset_to_defaults() ? "phy: reset - D1's group, store erased, retuning"
                                                 : "phy: reset refused - no usable store");
    }
    phy_service(now);

    if (g_ack_drop_req.exchange(false, std::memory_order_relaxed)) g_node.app().withhold_next_ack();
    if (g_ctx_new_req.exchange(false, std::memory_order_relaxed)) {
      // A command mid-execution would lose its ACK, as it would in a real reboot; the
      // check wants the resync alone.
      if (g_node.ctx().pending.active) {
        QueueSink().line("ctx: command in flight, not renewed");
      } else {
        lran::node::reset_context(g_node.ctx(), roll_random_u32);
        char text[48];
        std::snprintf(text, sizeof(text), "ctx: renewed, now %08lx",
                      static_cast<unsigned long>(g_node.ctx().ctx_id));
        QueueSink().line(text);
      }
    }

    // spec 8.1 - a REBOOT resets once its ACK is on the air.
    if (g_node.engine().restart_owed() && g_node.outbox().size() == 0 && radio_tx_idle()) {
      g_reboot_marker = kRebootMarker;
      esp_restart();
    }

    if (g_bus_run.load(std::memory_order_relaxed)) {
      const int64_t t0 = esp_timer_get_time();
      const bool    ok = radio_probe();
      note_max(g_bus.radio_max_us, t0);
      (ok ? g_bus.radio_ok : g_bus.radio_bad).fetch_add(1, std::memory_order_relaxed);
    }

    const lran::Counters& k = g_node.ctx().counters;
    NodeView              v;
    v.ctx_id      = g_node.ctx().ctx_id;
    v.tx_seq      = g_node.ctx().tx_seq;
    v.pending     = g_node.ctx().pending.active;
    v.rx_frames   = radio_stats().rx_frames;
    v.tx_frames   = radio_stats().tx_frames;
    v.rejected    = k.rx_rejected_ctx + k.rx_rejected_mac + k.rx_rejected_seq;
    v.dup_command = k.rx_dup_command;
    v.executions  = g_node.ctx().executions;
    v.dispatched  = g_node.app().dispatched();
    v.busy        = g_node.app().dispatch_refused();
    v.dropped     = g_node.engine().answers_dropped();
    v.cmd_i2c     = io.cmd_i2c_failures;
    v.outbox      = static_cast<uint8_t>(g_node.outbox().size());
    v.tx_active   = radio_tx_active();
    v.card_mounted = g_config.persist().mounted();
    v.card_dirty   = g_config.persist().dirty();
    v.unpersisted  = g_config.unpersisted();
    v.card         = g_config.persist().stats();
    v.phy_state     = g_phy.state();
    v.phy_writable  = g_phy.writable();
    v.phy_window_ms = g_phy.window_left_ms(now);
    v.phy_effective = g_phy.group();
    v.phy_committed = g_phy.committed();
    v.phy_stats     = g_phy.stats();
    portENTER_CRITICAL(&g_view_mux);
    g_view = v;
    portEXIT_CRITICAL(&g_view_mux);
  }
}

// The timeout in force, or 0 before the watchdog arms. Written by start_tasks() and by
// the CONFIG path; read by the console.
std::atomic<uint32_t> g_wdt_timeout_s{0};

// Set by the console's `wdt stall`: app_task stops feeding and parks, which is the
// application stall the watchdog exists for. The bench reads the next boot's reset cause.
std::atomic<bool> g_wdt_stall{false};

const char* gate_state_name(lran::GateState g) {
  switch (g) {
    case lran::GateState::Unknown:       return "UNKNOWN";
    case lran::GateState::Closed:        return "CLOSED";
    case lran::GateState::Moving:        return "MOVING";
    case lran::GateState::OpenCountdown: return "OPEN_COUNTDOWN";
    case lran::GateState::OpenHeld:      return "OPEN_HELD";
    case lran::GateState::Fault:         return "FAULT";
  }
  return "?";
}

const char* hold_source_name(uint8_t h) {
  static const char* const kNames[] = {"NONE", "LRAN", "MANUAL", "KEYPAD_FIRE", "UNKNOWN"};
  return h < 5 ? kNames[h] : "?";
}

const char* cause_name(uint8_t c) {
  static const char* const kNames[] = {"UNKNOWN", "EXIT_WAND", "LRAN_COMMAND", "KEYPAD_FIRE",
                                       "MANUAL_HOLD", "EXTERNAL_MOMENTARY"};
  return c < 6 ? kNames[c] : "?";
}

const char* direction_name(uint8_t d) {
  static const char* const kNames[] = {"NONE", "ENTRY", "EXIT", "UNDETERMINED"};
  return d < 4 ? kNames[d] : "?";
}

// GL3 - a script from the console, played a step at a time; io_task applies the result.
void service_injection(InjectScript& script, uint8_t* mask, uint8_t* value, uint32_t now) {
  const uint8_t req = g_inject_req.load(std::memory_order_acquire);
  if (req == kInjectStart) {
    const char* bad = nullptr;
    if (script.parse(g_inject_text, &bad)) {
      script.start(now);
    } else {
      char text[64];
      std::snprintf(text, sizeof(text), "inject: not understood at '%.24s'", bad);
      QueueSink().line(text);
    }
  } else if (req == kInjectOff) {
    script.stop();
    *mask  = 0;
    *value = 0;
  }
  if (req != 0) g_inject_req.store(0, std::memory_order_release);
  if (script.running()) script.service(now, live_param(kParamInjectSpacingMs), mask, value);
  g_inject.store(static_cast<uint16_t>((*mask << 8) | *value), std::memory_order_relaxed);
  g_inject_running.store(script.running(), std::memory_order_relaxed);
  g_inject_step.store(static_cast<uint8_t>(script.next_step()), std::memory_order_relaxed);
}

// GL3 - state derivation, detection and the pushes they cause (PRD R-3.1.4, 3.2; Impl Plan
// 5.2, 6.2, 6.3), and the one task that feeds the watchdog. A task that cannot subscribe
// logs it and runs unwatched: a gate controller that stops for want of a watchdog protects
// nothing.
void app_task(void*) {
  const bool watched = g_wdt_timeout_s.load() != 0 && esp_task_wdt_add(nullptr) == ESP_OK;
  if (!watched) QueueSink{}.line("wdt: app_task not subscribed - runs unwatched");
  GateTracker  tracker;
  Detector     detector(&g_event_ids);
  ChargeInhibit charge;
  AlarmWatch   alarms;
  MpptErrorWatch mppt_errors;
  AhDrawn      ah_drawn;
  InjectScript script;
  uint8_t      inj_mask  = 0;
  uint8_t      inj_value = 0;
  uint32_t     starts_seen[4] = {};
  TickType_t   last = xTaskGetTickCount();
  for (;;) {
    count(TaskId::App);
    if (g_wdt_stall.load(std::memory_order_relaxed)) {
      for (;;) vTaskDelay(portMAX_DELAY);
    }
    if (watched) (void)esp_task_wdt_reset();
    const uint32_t now = now_ms();
    service_injection(script, &inj_mask, &inj_value, now);

    const IoSnapshot io = io_snapshot();
    if (io.inputs_read) {
      for (uint8_t k = 0; k < 4; ++k) {
        if (io.relay_starts[k] != starts_seen[k]) {
          starts_seen[k] = io.relay_starts[k];
          tracker.note_pulse(k, now);
        }
      }
      GateParams gp;
      gp.hold_confirm_ms = live_param(kParamHoldConfirmMs);
      gp.cause_window_ms = live_param(kParamCauseWindowMs);
      const uint8_t changed = tracker.update(io.inputs, now, gp);

      // spec 8.7 - one STATUS per change; a change of hold is the one HA alerts on.
      PushList out;
      if (changed != 0) {
        Push p;
        p.reason = (changed & kChangedHold) != 0 ? lran::StatusReason::HoldStateChange
                                                 : lran::StatusReason::GateStateChange;
        out.add(p);
      }
      GateContext gc;
      gc.held        = tracker.held();
      gc.hold_source = tracker.hold_source();
      gc.gate_state  = tracker.state();
      gc.input_bits  = io.inputs;
      gc.uptime_s    = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
      DetectParams dp;
      dp.window_ms      = live_param(kParamDetectWindowMs);
      dp.idle_ms        = live_param(kParamDetectIdleMs);
      dp.alert_repeat_s = live_param(kParamAlertRepeatS);
      detector.update(io.inputs, now, gc, dp, &out);

      // D83, PRD G-9 and G-10 - FIRE and the hard-shutdown latch on their edges. Its own
      // list, because a detection can fill `out`, and queued at once: FIRE_ASSERTED is the
      // highest-priority alert (spec 8.9). Its STATUS reads input_bits from io_task's
      // snapshot, so it does not wait for the gate snapshot below.
      PushList alarm_out;
      if (alarms.update(io.inputs, gc, &g_event_ids, &alarm_out)) {
        char text[64];
        std::snprintf(text, sizeof(text), "alarm: inputs 0x%02x - FIRE %s, shutdown %s", io.inputs,
                      (io.inputs & kInFire) != 0 ? "ASSERTED" : "clear",
                      (io.inputs & kInAlarm) != 0 ? "ASSERTED" : "clear");
        QueueSink().line(text);
      }
      for (size_t i = 0; i < alarm_out.size(); ++i) {
        if (xQueueSend(g_push_q, &alarm_out[i], 0) != pdTRUE) {
          g_push_dropped.fetch_add(1, std::memory_order_relaxed);
        }
      }

      // GL5 - charge inhibition from the MPPT and the BMS (PRD R-6.4a, R-6.4b). Its own
      // list, because a detection can fill `out`.
      ChargeParams cp;
      cp.confirm_ms = live_param(kParamChargeConfirmS) * 1000;
      cp.temp_c     = static_cast<int16_t>(static_cast<int32_t>(live_param(kParamChargeTempC)));
      cp.band_ma    = live_param(kParamChargeBandMa);
      cp.rise_mv    = live_param(kParamChargeRiseMv);
      cp.bms_poll_s = live_param(kParamBmsPollS);
      portENTER_CRITICAL(&g_ved_mux);
      const MpptSnapshot mppt = g_ved.mppt;
      portEXIT_CRITICAL(&g_ved_mux);
      const BmsSnapshot bms_now = bms_snapshot();
      g_ah_drawn_dah.store(ah_drawn.update(mppt, bms_now, now, cp.bms_poll_s), std::memory_order_relaxed);
      PushList charge_out;
      const bool charge_changed =
          charge.update(mppt, bms_now, now, cp, gc, &g_event_ids, &charge_out);
      // Spec 8.9, D86. A STATUS and an EVENT at most, so it fits beside charge inhibition's.
      mppt_errors.update(mppt, gc, &g_event_ids, &charge_out);
      // The flag first, so the STATUS it causes carries it.
      g_charge_inhibited.store(charge.inhibited(), std::memory_order_relaxed);
      g_charge_source.store(static_cast<uint8_t>(charge.source()), std::memory_order_relaxed);
      g_charge_path.store(static_cast<uint8_t>(charge.path()), std::memory_order_relaxed);
      if (charge_changed) {
        char text[64];
        std::snprintf(text, sizeof(text), "charge: %s, by %s",
                      charge.inhibited() ? "INHIBITED" : "no longer inhibited",
                      charge_source_name(charge.inhibited() ? charge.source() : charge.path()));
        QueueSink().line(text);
      }
      for (size_t i = 0; i < charge_out.size(); ++i) {
        if (xQueueSend(g_push_q, &charge_out[i], 0) != pdTRUE) {
          g_push_dropped.fetch_add(1, std::memory_order_relaxed);
        }
      }

      // The snapshot first, so each STATUS carries the change that caused it.
      GateSnapshot g;
      g.gate_state           = static_cast<uint8_t>(tracker.state());
      g.hold                 = tracker.hold_byte();
      g.movement_cause       = static_cast<uint8_t>(tracker.cause());
      g.last_direction       = static_cast<uint8_t>(detector.last_direction());
      g.detect_flags         = detector.detect_flags(io.inputs);
      g.last_traversal_age_s = detector.last_traversal_age_s(now);
      portENTER_CRITICAL(&g_gate_mux);
      g_gate = g;
      portEXIT_CRITICAL(&g_gate_mux);

      if (changed != 0) {
        char text[96];
        std::snprintf(text, sizeof(text), "gate: %s, hold %s, cause %s, inputs 0x%02x",
                      gate_state_name(tracker.state()), hold_source_name(g.hold >> 1),
                      cause_name(g.movement_cause), io.inputs);
        QueueSink().line(text);
      }
      for (size_t i = 0; i < out.size(); ++i) {
        const Push& p = out[i];
        if (p.kind == Push::Kind::Event) {
          char text[96];
          std::snprintf(text, sizeof(text), "detect: %s%s id %lu, direction %s, hold %s",
                        lran::node::event_type_name(static_cast<lran::EventType>(p.event.event_type)),
                        (p.event.event_flags & 1) != 0 ? " follow-up" : "",
                        static_cast<unsigned long>(p.event.event_id),
                        direction_name(p.event.direction), hold_source_name(p.event.hold_source));
          QueueSink().line(text);
        }
        if (xQueueSend(g_push_q, &p, 0) != pdTRUE) g_push_dropped.fetch_add(1, std::memory_order_relaxed);
      }
      if ((out.size() != 0 || charge_out.size() != 0 || alarm_out.size() != 0) &&
          g_lora_handle != nullptr) {
        xTaskNotifyGive(g_lora_handle);
      }
    }
    vTaskDelayUntil(&last, period_ticks(TaskId::App));
  }
}

// GL5 - one BMS read per bms_poll_s, under the LoRa/BLE interlock (PRD R-3.4a, R-4.3h).
// R-3.4d: a failed window costs this task alone, and the next poll tries again. A window a
// reply aborted is retried sooner, once (bms_retry_next()).
void bms_task(void*) {
  bool retry = false;  // the next window is the one retry of an aborted window
  for (;;) {
    count(TaskId::Bms);
    const bool     is_retry = retry;
    const uint32_t period_ms =
        live_param(is_retry ? kParamBmsRetryS : kParamBmsPollS) * 1000;
    retry = false;
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(period_ms));
    const bool forced = g_bms_now.exchange(false);
    if (!forced && !g_bms_polling.load()) {
      portENTER_CRITICAL(&g_bms_mux);
      ++g_bms.suspended;
      portEXIT_CRITICAL(&g_bms_mux);
      continue;
    }
    const uint32_t cap = bms_cap_ms();
    // lora_task holds the lock for one frame's media access, at most a few backoffs.
    if (xSemaphoreTake(g_ble_mutex, pdMS_TO_TICKS(cap)) != pdTRUE) {
      portENTER_CRITICAL(&g_bms_mux);
      ++g_bms.busy;
      portEXIT_CRITICAL(&g_bms_mux);
      continue;
    }
    const uint32_t start = now_ms();
    g_ble_abort_ms.store(0);
    g_ble_start_ms.store(start);
    g_ble_holds.store(true);

    BmsWindowArgs args;
    args.name     = kBmsNamePrefix;
    args.entropy  = roll_entropy_top_up;
    args.start_ms = start;
    args.cap_ms   = cap;
    args.stop     = ble_stop_requested;
    args.log      = bms_log;
    BmsSnapshot snap;
    BmsTiming   t;
    const BmsEnd end = bms_window(args, &snap, &t);
    retry            = bms_retry_next(end, is_retry);

    const uint32_t done     = now_ms();
    const uint32_t asked_at = g_ble_abort_ms.load();
    g_ble_holds.store(false);
    xSemaphoreGive(g_ble_mutex);
    g_bms_bench_abort_ms.store(0);

    portENTER_CRITICAL(&g_bms_mux);
    if (end == BmsEnd::Read) g_bms.snap = snap;
    g_bms.timing    = t;
    g_bms.last_end  = end;
    g_bms.window_ms = done - start;
    if (g_bms.window_ms > g_bms.window_max_ms) g_bms.window_max_ms = g_bms.window_ms;
    if (asked_at != 0) {
      g_bms.abort_ms = done - asked_at;
      if (g_bms.abort_ms > g_bms.abort_max_ms) g_bms.abort_max_ms = g_bms.abort_ms;
    }
    ++g_bms.ends[static_cast<size_t>(end)];
    portEXIT_CRITICAL(&g_bms_mux);

    char text[120];
    std::snprintf(text, sizeof(text),
                  "bms: %s in %lu ms (init %lu scan %lu conn %lu hs %lu ans %lu down %lu)%s",
                  bms_end_name(end), static_cast<unsigned long>(done - start),
                  static_cast<unsigned long>(t.init_ms), static_cast<unsigned long>(t.scan_ms),
                  static_cast<unsigned long>(t.connect_ms), static_cast<unsigned long>(t.handshake_ms),
                  static_cast<unsigned long>(t.answer_ms), static_cast<unsigned long>(t.teardown_ms),
                  asked_at != 0 ? "; abort asked" : "");
    QueueSink().line(text);

    // After the interlock is released, so the card never lengthens a window. The append
    // holds the SPI lock for up to 59 ms, and a frame waits behind it as behind a
    // config.json write. No card, or a removed one, costs a count and not a remount: a
    // remount with no card holds the bus for about 1 s (Impl Plan 6.4).
    static char card[200];  // static: FATFS's open and write run on this 6 KB stack too
    const size_t len = format_bms_log_line(
        card, sizeof(card), static_cast<uint32_t>(esp_timer_get_time() / 1000000), end,
        done - start, t, asked_at != 0, bms_rx_counters(), bms_drain().timeouts, bms_drain().max_ms);
    const bool logged = len > 0 && board_sd_append(kBmsLogPath, card);
    static char name_seen[kBmsNameLen];
    char        name[kBmsNameLen];
    bms_matched_name(name);
    if (name[0] != '\0' && std::strcmp(name, name_seen) != 0) {
      std::memcpy(name_seen, name, sizeof(name_seen));
      char text[48];
      std::snprintf(text, sizeof(text), "bms_name %s", name);
      fault_line(text);
    }
    portENTER_CRITICAL(&g_bms_mux);
    ++(logged ? g_bms.card_logged : g_bms.card_failed);
    portEXIT_CRITICAL(&g_bms_mux);
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
//   gate              the derived gate block: state, hold and source, cause, direction (GL3)
//   inject [<script>|<name>|list|off]
//                     synthetic inputs below the debounce (PRD R-5.4b); inject.h has the
//                     script syntax, `inject list` the named scripts
//   sense             INA226, LM75 and RTC
//   sd                mount the card and append a line to /gl1.txt
//   beep              the buzzer
//   radio             radio_begin()'s status
//   lran              the protocol node: context, frames, refusals, commands (GL3)
//   lran ctx new      a new ctx_id, unannounced, for spec 10.3's resync on the bench
//   lran ack drop     withhold the next fresh COMMAND_ACK, for spec 9.4's dedup hit
//   bus <s>           GL1's bus test for s seconds, 1-600; see BusStats
//   wdt               the task watchdog's timeout
//   chg               charge inhibition: the indication, its source and the path deciding (GL5)
//   cfg               the configuration's card: mounted, dirty, writes and what boot read
//   wdt stall         park app_task unfed, so the task watchdog resets the chip (GL3)
//   restart           a software reset
//   panic             abort(), for spec 8.14's PANIC
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

void file_command(char* arg);

void console_command(char* cmd) {
  char line[360];
  size_t n = 0;
  char* arg = std::strchr(cmd, ' ');
  if (arg != nullptr) *arg++ = '\0';
  if (std::strcmp(cmd, "file") == 0) {
    file_command(arg);
    return;
  }
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
  } else if (std::strcmp(cmd, "relayfault") == 0) {
    // D78's bench check: an expander write cannot be made to fail on demand. USB only.
    g_relay_write_faults.store(1, std::memory_order_relaxed);
    n = std::snprintf(line, sizeof(line), "relayfault: the next sequence's next leading edge fails");
  } else if (std::strcmp(cmd, "in") == 0) {
    const IoSnapshot s = io_snapshot();
    static const char* const kEarlyOff[] = {"ok", "bus failed", "no ack"};
    n = std::snprintf(line, sizeof(line),
                      "in: raw 0x%02X debounced 0x%02X edges %lu/%lu relays 0x%02X i2c failures %lu; boot relay-off %s",
                      s.inputs_raw, s.inputs, static_cast<unsigned long>(s.raw_edges),
                      static_cast<unsigned long>(s.inputs_edges), s.relays,
                      static_cast<unsigned long>(s.i2c_failures),
                      kEarlyOff[static_cast<size_t>(board_early_off_result())]);
  } else if (std::strcmp(cmd, "gate") == 0) {
    GateSnapshot g;
    portENTER_CRITICAL(&g_gate_mux);
    g = g_gate;
    portEXIT_CRITICAL(&g_gate_mux);
    const IoSnapshot s = io_snapshot();
    char age[16] = "none";
    if (g.last_traversal_age_s != UINT32_MAX) {
      std::snprintf(age, sizeof(age), "%lus", static_cast<unsigned long>(g.last_traversal_age_s));
    }
    n = std::snprintf(line, sizeof(line),
                      "gate: %s held %u source %s cause %s; last direction %s, traversal %s; "
                      "detect_flags 0x%02x inputs 0x%02x injected 0x%02x; pushes dropped %lu",
                      gate_state_name(static_cast<lran::GateState>(g.gate_state)), g.hold & 1u,
                      hold_source_name(g.hold >> 1), cause_name(g.movement_cause),
                      direction_name(g.last_direction), age, g.detect_flags, s.inputs,
                      s.inject_mask, static_cast<unsigned long>(g_push_dropped.load()));
  } else if (std::strcmp(cmd, "inject") == 0) {
    const uint16_t inj = g_inject.load();
    if (arg == nullptr) {
      n = std::snprintf(line, sizeof(line), "inject: mask 0x%02x value 0x%02x, %s at step %u",
                        inj >> 8, inj & 0xFF, g_inject_running.load() ? "running" : "idle",
                        g_inject_step.load());
    } else if (std::strcmp(arg, "list") == 0) {
      n = std::snprintf(line, sizeof(line), "inject: %s", inject_canned_names());
    } else if (g_inject_req.load(std::memory_order_acquire) != 0) {
      n = std::snprintf(line, sizeof(line), "inject: busy, try again");
    } else if (std::strcmp(arg, "off") == 0) {
      g_inject_req.store(kInjectOff, std::memory_order_release);
      n = std::snprintf(line, sizeof(line), "inject: off; every input back to the hardware");
    } else {
      const char* canned = inject_canned(arg);
      std::snprintf(g_inject_text, sizeof(g_inject_text), "%s", canned != nullptr ? canned : arg);
      g_inject_req.store(kInjectStart, std::memory_order_release);
      n = std::snprintf(line, sizeof(line), "inject: %s", g_inject_text);
    }
  } else if (std::strcmp(cmd, "aw") == 0 && arg != nullptr &&
             std::strncmp(arg, "redir", 5) == 0) {
    const uint32_t seq = g_p1_seq.load();
    g_p1_redir.store(true);
    g_p1_req.store(true);
    for (int i = 0; i < 50 && g_p1_seq.load() == seq; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    portENTER_CRITICAL(&g_io_mux);
    const uint8_t readback = g_p1_readback;
    const bool    ok       = g_p1_ok;
    portEXIT_CRITICAL(&g_io_mux);
    g_p1_redir.store(false);
    if (g_p1_seq.load() == seq) {
      n = std::snprintf(line, sizeof(line), "aw redir: no answer from io_task");
    } else if (!ok) {
      n = std::snprintf(line, sizeof(line), "aw redir: I2C failure");
    } else {
      n = std::snprintf(line, sizeof(line),
                        "aw redir: P1_4-P1_7 set input again, input reg %02x, those bits %02x"
                        " - IN5-IN8 %s",
                        readback, static_cast<uint8_t>(readback & 0xF0),
                        (readback & 0xF0) != 0 ? "read" : "still 0");
    }
  } else if (std::strcmp(cmd, "aw") == 0 && arg != nullptr &&
             std::strncmp(arg, "drive", 5) == 0) {
    const char* lvl   = std::strchr(arg, ' ');
    const bool  level = lvl != nullptr && std::strtoul(lvl + 1, nullptr, 10) != 0;
    const uint32_t seq = g_p1_seq.load();
    g_p1_level.store(level);
    g_p1_req.store(true);
    for (int i = 0; i < 50 && g_p1_seq.load() == seq; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    portENTER_CRITICAL(&g_io_mux);
    const uint8_t readback = g_p1_readback;
    const bool    ok       = g_p1_ok;
    portEXIT_CRITICAL(&g_io_mux);
    if (g_p1_seq.load() == seq) {
      n = std::snprintf(line, sizeof(line), "aw drive: no answer from io_task");
    } else if (!ok) {
      n = std::snprintf(line, sizeof(line), "aw drive: I2C failure");
    } else {
      // Only the upper nibble was driven; the lower one is P1_0-P1_3, unconnected here.
      const uint8_t want = level ? 0xF0 : 0x00;
      n = std::snprintf(line, sizeof(line),
                        "aw drive: P1_4-P1_7 driven %u, input reg %02x, those bits %02x, "
                        "expected %02x - %s",
                        level ? 1u : 0u, readback,
                        static_cast<uint8_t>(readback & 0xF0), want,
                        (readback & 0xF0) == want ? "pins follow the drive"
                                                  : "pins do not follow the drive");
    }
  } else if (std::strcmp(cmd, "aw") == 0) {
    const uint32_t seq = g_aw_seq.load();
    g_aw_req.store(true);
    for (int i = 0; i < 50 && g_aw_seq.load() == seq; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    uint8_t r[kExpanderDumpLen];
    portENTER_CRITICAL(&g_io_mux);
    std::memcpy(r, g_aw_regs, sizeof(r));
    const bool ok = g_aw_ok;
    portEXIT_CRITICAL(&g_io_mux);
    if (g_aw_seq.load() == seq) {
      n = std::snprintf(line, sizeof(line), "aw: no answer from io_task");
    } else if (!ok) {
      n = std::snprintf(line, sizeof(line), "aw: I2C failure");
    } else {
      n = std::snprintf(line, sizeof(line),
                        "aw: input %02x %02x output %02x %02x dir %02x %02x int %02x %02x "
                        "id %02x gcr %02x ledmode %02x %02x",
                        r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11]);
    }
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
      // Aligned 32-bit words, which lora_task alone writes: a read is never torn.
      const RadioStats& r = radio_stats();
      portENTER_CRITICAL(&g_view_mux);
      const NodeView v = g_view;
      portEXIT_CRITICAL(&g_view_mux);
      n = std::snprintf(line, sizeof(line),
                        "radio: RadioLib status %d - %s; begin fails %lu; rx %lu err %lu; tx %lu "
                        "err %lu timeout %lu forced %lu; cad err %lu deferred %lu; outbox %u%s; last error %s %d; "
                        "RST check %s",
                        st, st == 0 ? "up" : "FAILED", static_cast<unsigned long>(r.begin_failures),
                        static_cast<unsigned long>(r.rx_frames), static_cast<unsigned long>(r.rx_driver_errors),
                        static_cast<unsigned long>(r.tx_frames), static_cast<unsigned long>(r.tx_errors),
                        static_cast<unsigned long>(r.tx_timeouts), static_cast<unsigned long>(r.tx_forced),
                        static_cast<unsigned long>(r.cad_errors), static_cast<unsigned long>(r.cad_deferred),
                        static_cast<unsigned>(v.outbox), v.tx_active ? ", sending" : "",
                        r.last_error_at[0] != '\0' ? r.last_error_at : "none", r.last_error,
                        rst_check_name(r.rst_check));
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
  } else if (std::strcmp(cmd, "bms") == 0 && arg != nullptr) {
    char* rest = nullptr;
    if (std::strncmp(arg, "now", 3) == 0) {
      // `bms now [abort_ms]` - a window at once; with abort_ms, asked to end that far in.
      const unsigned long at = std::strtoul(arg + 3, &rest, 10);
      g_bms_bench_abort_ms.store(static_cast<uint32_t>(at));
      g_bms_now.store(true);
      if (g_bms_handle != nullptr) xTaskNotifyGive(g_bms_handle);
      n = at != 0 ? std::snprintf(line, sizeof(line), "bms: window now, abort at %lu ms", at)
                  : std::snprintf(line, sizeof(line), "bms: window now");
    } else if (std::strcmp(arg, "data") == 0) {
      // The decoded fields, laid out as wattcycle-reader prints them, to check one against
      // the other.
      const BmsSnapshot b = bms_snapshot();
      if (!b.have) {
        n = std::snprintf(line, sizeof(line), "bms: no read yet");
      } else {
        const bms::BmsData& d = b.data;
        n = std::snprintf(line, sizeof(line),
                          "bms: cells %u: %u %u %u %u mV (delta %u); temps %u: %d %d %d %d (0.1 C); "
                          "pack %lu mV %ld mA (%s, raw 0x%04X); SOC %u%%; %u/%u (0.1 Ah); cycles %u; SOH %u; rssi %d",
                          d.cell_count, d.cell_mv[0], d.cell_mv[1], d.cell_mv[2], d.cell_mv[3],
                          d.delta_cell_mv(), d.temp_count, d.temp_dc[0], d.temp_dc[1], d.temp_dc[2],
                          d.temp_dc[3], static_cast<unsigned long>(d.pack_mv),
                          static_cast<long>(d.current_ma),
                          d.discharging ? "discharge flag" : "charge flag", d.current_raw, d.soc_pct,
                          d.remaining_dAh, d.nominal_dAh, d.cycles, d.soh_dpct, b.rssi_dbm);
      }
    } else {
      n = std::snprintf(line, sizeof(line), "bms: no arguments, now [abort_ms], or data");
    }
  } else if (std::strcmp(cmd, "bms") == 0) {
    portENTER_CRITICAL(&g_bms_mux);
    const BmsView v = g_bms;
    portEXIT_CRITICAL(&g_bms_mux);
    const BmsRxCounters k = bms_rx_counters();
    const BmsHeap       h = bms_heap();
    const BmsDrain      d = bms_drain();
    size_t ends = 0;
    char   list[96]  = "";
    for (size_t i = 0; i < static_cast<size_t>(BmsEnd::Count); ++i) {
      if (v.ends[i] == 0) continue;
      ends += std::snprintf(list + ends, sizeof(list) - ends, " %s %lu",
                            bms_end_name(static_cast<BmsEnd>(i)), static_cast<unsigned long>(v.ends[i]));
      if (ends >= sizeof(list)) break;
    }
    n = std::snprintf(line, sizeof(line),
                      "bms: %s; polling %s; window %lu max %lu ms; abort %lu max %lu ms; cap %lu; "
                      "ends%s; busy %lu off %lu; tx waits %lu asked %lu overrun %lu; rx %lu crc %lu "
                      "term %lu other %lu; heap %lu/%lu low %lu; drain max %lu ms timeouts %lu; "
                      "%s %lu failed %lu; stack free %lu",
                      v.last_end == BmsEnd::Count ? "no window yet" : bms_end_name(v.last_end),
                      g_bms_polling.load() ? "on" : "SUSPENDED",
                      static_cast<unsigned long>(v.window_ms), static_cast<unsigned long>(v.window_max_ms),
                      static_cast<unsigned long>(v.abort_ms), static_cast<unsigned long>(v.abort_max_ms),
                      static_cast<unsigned long>(bms_cap_ms()), list,
                      static_cast<unsigned long>(v.busy), static_cast<unsigned long>(v.suspended),
                      static_cast<unsigned long>(g_ble_tx_waits.load()),
                      static_cast<unsigned long>(g_ble_aborts_asked.load()),
                      static_cast<unsigned long>(g_ble_overruns.load()),
                      static_cast<unsigned long>(k.frames), static_cast<unsigned long>(k.crc_errors),
                      static_cast<unsigned long>(k.bad_term), static_cast<unsigned long>(k.undecoded),
                      static_cast<unsigned long>(h.before), static_cast<unsigned long>(h.after),
                      static_cast<unsigned long>(h.low), static_cast<unsigned long>(d.max_ms),
                      static_cast<unsigned long>(d.timeouts), kBmsLogPath,
                      static_cast<unsigned long>(v.card_logged),
                      static_cast<unsigned long>(v.card_failed),
                      static_cast<unsigned long>(
                          g_bms_handle ? uxTaskGetStackHighWaterMark(g_bms_handle) : 0));
  } else if (std::strcmp(cmd, "lran") == 0 && arg != nullptr) {
    if (std::strcmp(arg, "ctx new") == 0) {
      g_ctx_new_req.store(true, std::memory_order_relaxed);
      if (g_lora_handle != nullptr) xTaskNotifyGive(g_lora_handle);
      n = std::snprintf(line, sizeof(line), "lran: new context requested");
    } else if (std::strcmp(arg, "ack drop") == 0) {
      g_ack_drop_req.store(true, std::memory_order_relaxed);
      n = std::snprintf(line, sizeof(line), "lran: next COMMAND_ACK will be withheld");
    } else {
      n = std::snprintf(line, sizeof(line), "lran: no arguments, ctx new, or ack drop");
    }
  } else if (std::strcmp(cmd, "lran") == 0) {
    portENTER_CRITICAL(&g_view_mux);
    const NodeView v = g_view;
    portEXIT_CRITICAL(&g_view_mux);
    n = std::snprintf(line, sizeof(line),
                      "lran: ctx %08lx seq %u%s; rx %lu tx %lu; refused %lu dup %lu; exec %lu, "
                      "pulsed %lu busy %lu, i2c lost %lu; dropped %lu log %lu",
                      static_cast<unsigned long>(v.ctx_id), static_cast<unsigned>(v.tx_seq),
                      v.pending ? ", command in flight" : "", static_cast<unsigned long>(v.rx_frames),
                      static_cast<unsigned long>(v.tx_frames), static_cast<unsigned long>(v.rejected),
                      static_cast<unsigned long>(v.dup_command), static_cast<unsigned long>(v.executions),
                      static_cast<unsigned long>(v.dispatched), static_cast<unsigned long>(v.busy),
                      static_cast<unsigned long>(v.cmd_i2c), static_cast<unsigned long>(v.dropped),
                      static_cast<unsigned long>(g_log_dropped.load(std::memory_order_relaxed)));
  } else if (std::strcmp(cmd, "phy") == 0 && arg != nullptr) {
    if (std::strcmp(arg, "reset") == 0) {
      g_phy_reset_req.store(true, std::memory_order_relaxed);
      if (g_lora_handle != nullptr) xTaskNotifyGive(g_lora_handle);
      n = std::snprintf(line, sizeof(line), "phy: reset requested");
    } else {
      n = std::snprintf(line, sizeof(line), "phy: no arguments, or reset");
    }
  } else if (std::strcmp(cmd, "phy") == 0) {
    // spec 12.4.2 - the group on the air, the one committed, and the trial between them.
    portENTER_CRITICAL(&g_view_mux);
    const NodeView v = g_view;
    portEXIT_CRITICAL(&g_view_mux);
    const auto ld = [](uint32_t x) { return static_cast<unsigned long>(x); };
    const auto& e = v.phy_effective.v;
    const auto& c = v.phy_committed.v;
    const auto& k = v.phy_stats;
    n = std::snprintf(line, sizeof(line),
                      "phy: %s, store %s, window %lu ms; effective %ld Hz SF%ld BW %ld CR 4/%ld %ld dBm "
                      "trial %ld s; committed %ld Hz SF%ld BW %ld CR 4/%ld %ld dBm trial %ld s; "
                      "trials %lu committed %lu reverted %lu abandoned %lu commit failed %lu refused %lu",
                      lran::node::phy_state_name(v.phy_state),
                      v.phy_writable ? "nvs" : "NONE (PHY rows READ_ONLY)", ld(v.phy_window_ms),
                      static_cast<long>(e[0]), static_cast<long>(e[1]), static_cast<long>(e[2]),
                      static_cast<long>(e[3]), static_cast<long>(e[4]), static_cast<long>(e[5]),
                      static_cast<long>(c[0]), static_cast<long>(c[1]), static_cast<long>(c[2]),
                      static_cast<long>(c[3]), static_cast<long>(c[4]), static_cast<long>(c[5]),
                      ld(k.trials), ld(k.committed), ld(k.reverted), ld(k.abandoned),
                      ld(k.commit_failed), ld(k.refused_in_trial));
  } else if (std::strcmp(cmd, "cfg") == 0) {
    portENTER_CRITICAL(&g_view_mux);
    const NodeView v = g_view;
    portEXIT_CRITICAL(&g_view_mux);
    const auto ld = [](uint32_t x) { return static_cast<unsigned long>(x); };
    n = std::snprintf(line, sizeof(line),
                      "cfg: card %s%s, overrides %s; writes %lu failed %lu, mounts failed %lu; "
                      "boot read %lu, refused %lu, unknown %lu%s",
                      v.card_mounted ? "mounted" : "NOT MOUNTED", v.card_dirty ? " DIRTY" : "",
                      v.unpersisted ? "NOT PERSISTED" : "persisted", ld(v.card.writes),
                      ld(v.card.write_failures), ld(v.card.mounts_failed), ld(v.card.restored),
                      ld(v.card.refused), ld(v.card.unknown),
                      v.card.file_corrupt ? ", file unreadable" : "");
  } else if (std::strcmp(cmd, "ved") == 0 && arg != nullptr && std::strcmp(arg, "cpu") == 0) {
    VedCpu c[2];
    portENTER_CRITICAL(&g_ved_mux);
    c[0] = g_ved_cpu[0];
    c[1] = g_ved_cpu[1];
    portEXIT_CRITICAL(&g_ved_mux);
    g_ved_cpu_reset.store(true);
    for (size_t i = 0; i < 2; ++i) {
      const uint64_t w = c[i].wall_us == 0 ? 1 : c[i].wall_us;
      n = std::snprintf(line, sizeof(line),
                        "ved cpu: ble %s; %lu ms, %lu passes, %lu/s; busy %lu.%02lu%%, max pass %lu us, max gap %lu us",
                        i == 1 ? "open" : "shut", static_cast<unsigned long>(c[i].wall_us / 1000),
                        static_cast<unsigned long>(c[i].passes),
                        static_cast<unsigned long>(uint64_t{c[i].passes} * 1000000u / w),
                        static_cast<unsigned long>(c[i].busy_us * 100u / w),
                        static_cast<unsigned long>(c[i].busy_us * 10000u / w % 100u),
                        static_cast<unsigned long>(c[i].max_pass_us), static_cast<unsigned long>(c[i].max_gap_us));
      if (i == 0) write_line(line, n, sizeof(line));
    }
  } else if (std::strcmp(cmd, "chg") == 0) {
    const auto src = [](const std::atomic<uint8_t>& a) {
      return charge_source_name(static_cast<ChargeSource>(a.load(std::memory_order_relaxed)));
    };
    n = std::snprintf(line, sizeof(line), "chg: %s, source %s, deciding path %s; temp <= %ld C, band %lu mA, rise %lu mV, confirm %lu s",
                      g_charge_inhibited.load() ? "INHIBITED" : "not inhibited", src(g_charge_source),
                      src(g_charge_path), static_cast<long>(static_cast<int32_t>(live_param(kParamChargeTempC))),
                      static_cast<unsigned long>(live_param(kParamChargeBandMa)),
                      static_cast<unsigned long>(live_param(kParamChargeRiseMv)),
                      static_cast<unsigned long>(live_param(kParamChargeConfirmS)));
    write_line(line, n, sizeof(line));
  } else if (std::strcmp(cmd, "ved") == 0) {
    const VedView      v = ved_view();
    const MpptSnapshot& m = v.mppt;
    // Short lines: one near 200 bytes lost its middle on the USB console (bench, 2026-10-08).
    n = std::snprintf(line, sizeof(line), "ved: %s, flags 0x%02X; batt %u mV %d mA, pv %u cV %u W, load %d mA",
                      v.have_block ? "block" : "NO BLOCK YET", m.mppt_flags, m.batt_mv, m.batt_ma, m.pv_cv,
                      m.pv_w, m.load_ma);
    write_line(line, n, sizeof(line));
    n = std::snprintf(line, sizeof(line), "ved: cs %u err %u mppt %u; h19 %lu h20 %u h21 %u h22 %u",
                      m.charge_state, m.mppt_err, m.mppt_tracker, static_cast<unsigned long>(m.yield_total),
                      m.yield_today, m.pmax_today, m.yield_yest);
    write_line(line, n, sizeof(line));
    const auto ld = [](uint32_t x) { return static_cast<unsigned long>(x); };
    n = std::snprintf(line, sizeof(line),
                      "ved: last block %lu ms ago; text %lu ok, %lu bad, %lu unsynced, %lu interrupted, %lu overflow, "
                      "%lu unparsed",
                      v.have_block ? ld(v.block_age_ms) : 0, ld(v.text.blocks), ld(v.text.bad_checksum),
                      ld(v.text.unsynced), ld(v.text.interrupted), ld(v.text.overflow), ld(v.link.blocks_unparsed));
    write_line(line, n, sizeof(line));
    n = std::snprintf(line, sizeof(line),
                      "ved: hex sent %lu, retried %lu, answered %lu, timeouts %lu, uart errors %lu; async %lu, "
                      "unmatched %lu, bad %lu, too long %lu%s",
                      ld(v.link.hex_sent), ld(v.link.hex_retries), ld(v.link.hex_answered), ld(v.link.hex_timeouts),
                      ld(v.link.hex_uart_errors), ld(v.link.hex_async), ld(v.link.hex_unmatched),
                      ld(v.link.hex_bad), ld(v.text.hex_too_long), v.busy ? "; one outstanding" : "");
  } else if (std::strcmp(cmd, "beep") == 0) {
    board_beep(2000, 200);
    n = std::snprintf(line, sizeof(line), "beep");
  } else if (std::strcmp(cmd, "restart") == 0) {
    Serial.println(F("restart"));
    Serial.flush();
    esp_restart();
  } else if (std::strcmp(cmd, "wdt") == 0) {
    if (arg != nullptr && std::strcmp(arg, "stall") == 0) {
      g_wdt_stall.store(true);
      n = std::snprintf(line, sizeof(line), "wdt: app_task parked, not feeding; reset due in %lu s",
                        static_cast<unsigned long>(watchdog_timeout_s()));
    } else {
      n = std::snprintf(line, sizeof(line), "wdt: timeout %lu s%s",
                        static_cast<unsigned long>(watchdog_timeout_s()),
                        watchdog_timeout_s() == 0 ? " - NOT ARMED" : "");
    }
  } else if (std::strcmp(cmd, "panic") == 0) {
    // GL3 - spec 8.14's PANIC, which no other bench command produces.
    Serial.println(F("panic: abort()"));
    Serial.flush();
    abort();
  } else if (std::strcmp(cmd, "hang") == 0) {
    Serial.println(F("hang: interrupts off"));
    Serial.flush();
    portDISABLE_INTERRUPTS();
    for (;;) {
    }
  } else {
    n = std::snprintf(line, sizeof(line), "commands: file | relay <1-4> [ms] | in | sense | sd | beep | radio | lran | ved | bus <s> | wdt [stall] | restart | panic | hang");
  }
  write_line(line, n, sizeof(line));
}

// PRD R-5.3g, R-5.4e. log_task alone writes the fault log. The queue's lines, then the
// counters every 12 bms_poll_s, about hourly at the default and so a parameter's multiple.
// The counters clear at any reset, and a console attach is a reset (engineering log,
// 2026-10-27), so the card is what a visit can still read.
void fault_log_service(uint32_t* next_ms) {
  LogLine l;
  bool    wrote = false;
  while (xQueueReceive(g_fault_q, &l, 0) == pdTRUE) {
    if (!wrote && board_sd_size(kFaultLogPath) >= kFaultLogCapBytes) {
      board_sd_rotate(kFaultLogPath, kFaultLogOldPath);
    }
    wrote = true;
    if (!board_sd_append(kFaultLogPath, l.text)) ++g_fault_failed;
  }
  const uint32_t now = now_ms();
  if (*next_ms == 0) *next_ms = now + 12 * live_param(kParamBmsPollS) * 1000;
  if (!reached(now, *next_ms)) return;
  *next_ms = now + 12 * live_param(kParamBmsPollS) * 1000;

  const RadioStats& r = radio_stats();
  const IoSnapshot  s = io_snapshot();
  const BmsDrain    d = bms_drain();
  const auto        ld = [](uint32_t x) { return static_cast<unsigned long>(x); };
  // Two lines, because one would not fit a LogLine beside its uptime.
  char text[kLogLineLen - 24];
  std::snprintf(text, sizeof(text), "radio rx=%lu tx=%lu tx_err=%lu tx_to=%lu", ld(r.rx_frames),
                ld(r.tx_frames), ld(r.tx_errors), ld(r.tx_timeouts));
  fault_line(text);
  std::snprintf(text, sizeof(text), "counts i2c=%lu cmd_i2c=%lu push=%lu drain=%lu drop=%lu fail=%lu",
                ld(s.i2c_failures), ld(s.cmd_i2c_failures), ld(g_push_dropped.load()), ld(d.timeouts),
                ld(g_fault_dropped.load()), ld(g_fault_failed));
  fault_line(text);
}

// Streams `bytes` of a file from `start`, then its CRC (file_dump.h). Ctrl-C stops it, and
// any other pending input waits for its own turn, as a second command.
void file_stream(const char* path, uint32_t start, uint32_t bytes) {
  static uint8_t buf[512];
  // HWCDC discards what it is given once a write has waited out its TX timeout, 100 ms by
  // default. Longer here, because log_task alone waits on it, and back after, so a console
  // with no host stays cheap.
  Serial.setTxTimeoutMs(2000);
  Crc32    crc;
  uint32_t sent = 0;
  bool     stopped = false;
  bool     failed  = false;
  while (sent < bytes) {
    if (Serial.peek() == 0x03) {
      Serial.read();
      stopped = true;
      break;
    }
    const uint32_t left = bytes - sent;
    const size_t   want = left < sizeof(buf) ? left : sizeof(buf);
    // One read per lock, so the radio waits on one chunk and not the file.
    const size_t got = board_sd_read_at(path, start + sent, buf, want);
    if (got == 0) {
      failed = true;
      break;
    }
    // Short writes with a pause between. 512 B a tick lost about 0.1 % of a 180 KB file, and
    // no pause lost 15 %; this lost none in five dumps (engineering log, 2026-11-01).
    constexpr size_t kUsbStep = 64;
    for (size_t at = 0; at < got; at += kUsbStep) {
      Serial.write(buf + at, got - at < kUsbStep ? got - at : kUsbStep);
      vTaskDelay(pdMS_TO_TICKS(4));
    }
    crc.update(buf, got);
    sent += static_cast<uint32_t>(got);
    vTaskDelay(1);
  }
  Serial.setTxTimeoutMs(100);
  char line[96];
  const size_t n = std::snprintf(line, sizeof(line), "\nfile: %s %s, %lu bytes from %lu, crc32 %08lx", path,
                                 stopped ? "STOPPED" : failed ? "READ FAILED" : "end",
                                 static_cast<unsigned long>(sent), static_cast<unsigned long>(start),
                                 static_cast<unsigned long>(crc.value()));
  write_line(line, n, sizeof(line));
}

void file_list_entry(const char* name, uint32_t size, bool dir) {
  char line[96];
  const size_t n = std::snprintf(line, sizeof(line), "file: %s%s %lu", name, dir ? "/" : "",
                                 static_cast<unsigned long>(size));
  write_line(line, n, sizeof(line));
}

// file ls [dir] | file cat <path> [offset] | file tail <path> [lines]. USB console only, and
// it reads: nothing here writes to the card (PRD R-5.4e; operator, 2026-11-01).
void file_command(char* arg) {
  char line[96];
  size_t n = 0;
  char* verb = arg;
  char* path = verb != nullptr ? std::strchr(verb, ' ') : nullptr;
  if (path != nullptr) *path++ = '\0';
  char* num = path != nullptr ? std::strchr(path, ' ') : nullptr;
  if (num != nullptr) *num++ = '\0';

  if (verb != nullptr && std::strcmp(verb, "ls") == 0) {
    if (!board_sd_list(path != nullptr ? path : "/", file_list_entry)) {
      n = std::snprintf(line, sizeof(line), "file: no card, or no such directory");
      write_line(line, n, sizeof(line));
    }
    return;
  }
  const bool cat  = verb != nullptr && std::strcmp(verb, "cat") == 0;
  const bool tail = verb != nullptr && std::strcmp(verb, "tail") == 0;
  if ((!cat && !tail) || path == nullptr || path[0] != '/') {
    n = std::snprintf(line, sizeof(line), "file: ls [dir] | cat </path> [offset] | tail </path> [lines]");
    write_line(line, n, sizeof(line));
    return;
  }
  const int64_t size = board_sd_size(path);
  if (size < 0) {
    n = std::snprintf(line, sizeof(line), "file: %.60s: no card, or no such file", path);
    write_line(line, n, sizeof(line));
    return;
  }
  const uint32_t total = static_cast<uint32_t>(size);
  uint32_t       start = 0;
  if (cat) {
    start = num != nullptr ? static_cast<uint32_t>(std::strtoul(num, nullptr, 10)) : 0;
    if (start > total) start = total;
  } else {
    const unsigned long lines = num != nullptr ? std::strtoul(num, nullptr, 10) : 20;
    static uint8_t buf[512];
    TailScan scan(total, lines != 0 ? static_cast<uint32_t>(lines) : 20);
    uint32_t offset = 0;
    size_t   want;
    while ((want = scan.next_chunk(sizeof(buf), &offset)) != 0) {
      if (board_sd_read_at(path, offset, buf, want) != want) break;
      if (scan.feed(buf, want)) break;
      vTaskDelay(1);
    }
    start = scan.start();
  }
  file_stream(path, start, total - start);
}

// GL1 - a queue of leveled lines and the microSD log arrive here. Today it runs the bench
// console and prints the pass counts. It is the only task that writes to Serial: a full USB
// CDC buffer blocks the writer, and no other task may be that writer. The first report
// waits a period, so it cannot interleave with setup()'s last banner line.
void log_task(void*) {
  uint32_t fault_next = 0;
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
    fault_log_service(&fault_next);
    LogLine engine_line;
    while (xQueueReceive(g_log_q, &engine_line, 0) == pdTRUE) {
      write_line(engine_line.text, std::strlen(engine_line.text), sizeof(engine_line.text));
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

size_t start_tasks(const PageText& boot_page, const uint8_t* node_key, size_t node_key_len,
                   const char* git) {
  g_git          = git;
  g_boot_page    = boot_page;
  g_node_key     = node_key;
  g_node_key_len = node_key_len;
  g_pulse_q = xQueueCreateStatic(4, sizeof(PulseRequest), g_pulse_q_buf, &g_pulse_q_storage);
  g_cmd_q   = xQueueCreateStatic(1, sizeof(RelaySequence), g_cmd_q_buf, &g_cmd_q_storage);
  g_push_q  = xQueueCreateStatic(8, sizeof(Push), g_push_q_buf, &g_push_q_storage);
  g_log_q   = xQueueCreateStatic(kLogQueueDepth, sizeof(LogLine), g_log_q_buf, &g_log_q_storage);
  g_fault_q = xQueueCreateStatic(kFaultQueueDepth, sizeof(LogLine), g_fault_q_buf, &g_fault_q_storage);
  g_hex_q   = xQueueCreateStatic(1, sizeof(HexJob), g_hex_q_buf, &g_hex_q_storage);
  g_ble_mutex = xSemaphoreCreateMutexStatic(&g_ble_mutex_storage);
  // Impl Plan 6.4 - the card before any task runs, so every task starts on its values.
  // lora_task applies them and logs what was read.
  g_config.persist().load(&g_config.store());
  const lran::config::Table& t = g_config.table();
  for (size_t i = 0; i < t.size(); ++i) {
    g_live[i].store(g_config.store().effective(t.at(i)->id), std::memory_order_relaxed);
  }
  // PRD R-3.5h - the boot's ctx_id, drawn while nothing else uses the ADC or the radio.
  bootloader_random_enable();
  g_boot_entropy = esp_random();
  while (g_roll_entropy.size() < RollEntropy::kCap) g_roll_entropy.put(esp_random());
  bootloader_random_disable();
  // Armed before any task starts, so app_task subscribes at the configured timeout. A
  // watchdog that fails to arm leaves app_task unwatched; app_task logs that.
  (void)apply_watchdog_timeout(live_param(kParamWatchdogTimeoutS));
  size_t started = 0;
  for (size_t i = 0; i < kTaskCount; ++i) {
    const TaskSpec& spec = task_table()[i];
    if (kSlots[i].stack_bytes != spec.stack_bytes) continue;  // a table row and a stack disagree
    TaskHandle_t h = xTaskCreateStaticPinnedToCore(
        kSlots[i].body, spec.name, spec.stack_bytes, nullptr, spec.priority, kSlots[i].stack,
        &g_tcb[i], spec.core == kAnyCore ? tskNO_AFFINITY : spec.core);
    if (h != nullptr) ++started;
    if (spec.id == TaskId::Lora) g_lora_handle = h;
    if (spec.id == TaskId::Bms) g_bms_handle = h;
  }
  return started;
}

bool usb_chip_reset() {
  // ESP-IDF 4.4 has no ESP_RST_USB, and esp_reset_reason() answers ESP_RST_UNKNOWN for a
  // reset through the USB serial/JTAG peripheral, which is what an upload or the host's
  // DTR toggle does. The ROM's reason names it.
  const RESET_REASON r = rtc_get_reset_reason(0);
  return r == USB_UART_CHIP_RESET || r == USB_JTAG_CHIP_RESET;
}

bool apply_watchdog_timeout(uint32_t seconds) {
  const uint32_t s = watchdog_timeout_in_range(seconds);
  if (esp_task_wdt_init(s, /*panic=*/true) != ESP_OK) return false;
  g_wdt_timeout_s.store(s);
  return true;
}

uint32_t watchdog_timeout_s() { return g_wdt_timeout_s.load(); }

uint32_t task_passes(TaskId id) {
  return g_passes[static_cast<size_t>(id)].load(std::memory_order_relaxed);
}

}  // namespace gatelink
