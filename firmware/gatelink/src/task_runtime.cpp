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
// THE WATCHDOG IS NOT ARMED HERE. Impl Plan 5.2 feeds it from app_task, and its timeout is
// the parameter watchdog_timeout_s (decided 2026-10-07; root rule 8), which a later GL3
// slice adds. Arduino-ESP32's default watchdog still watches the idle task on core 0.

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

#include "board_profile.h"
#include "board_stamplc.h"
#include "gate_io.h"
#include "gatelink_app.h"
#include "lran/node/engine.h"
#include "lran/node/names.h"
#include "mbedtls_mac.h"
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
StackType_t  g_stack_log[4096];

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
  // GL3 - the end of each gate command's relay sequence, which lora_task's COMMAND_ACK waits
  // for (Impl Plan 5.2). cmd_seq is bumped once per sequence, after its last trailing edge.
  uint32_t     cmd_seq       = 0;
  SequenceEnd  cmd_end       = SequenceEnd::Done;
  uint32_t     cmd_i2c_failures = 0;  // expander writes a sequence lost, since boot
};

portMUX_TYPE  g_io_mux = portMUX_INITIALIZER_UNLOCKED;
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
  const uint32_t settle_ms  = param_default(kParamUnlockSettleMs);
  const uint8_t  samples    = static_cast<uint8_t>(param_default(kParamInputDebounceSamples));
  CommandSequencer sequencer;
  uint32_t         seq_i2c_failures = 0;

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
      const bool    ok = board_write_relays(pulser.mask());
      const int64_t t  = esp_timer_get_time();
      if (!ok) ++seq_i2c_failures;
      portENTER_CRITICAL(&g_io_mux);
      g_io.relays       = pulser.mask();
      g_io.pulse_relay  = static_cast<uint8_t>(__builtin_ctz(pulser.mask()));
      g_io.pulse_result = PulseResult::Started;
      g_io.pulse_on_us  = t;
      if (!ok) ++g_io.i2c_failures;
      portEXIT_CRITICAL(&g_io_mux);
    }
    SequenceEnd end;
    if (sequencer.take_end(&end)) {
      portENTER_CRITICAL(&g_io_mux);
      g_io.cmd_end = end;
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

// Waits on UART RX events, and wakes at least every kVedWaitMs for the HEX timers.
void vedirect_task(void*) {
  g_ved_handle = xTaskGetCurrentTaskHandle();
  g_ved_link.set_params(
      VedParams{param_default(kParamHexTimeoutMs), param_default(kParamVedirectStaleS) * 1000});
  // Before begin(), or the core ignores it. A text block is a few hundred bytes; 1 KB holds
  // several, so a late pass loses none (engineering log, 2026-10-08).
  Serial1.setRxBufferSize(1024);
  Serial1.begin(kVedBaud, SERIAL_8N1, kVedUartRx, kVedUartTx);
  Serial1.onReceive([] { xTaskNotifyGive(g_ved_handle); });

  for (;;) {
    count(TaskId::Vedirect);
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kVedWaitMs));
    const uint32_t now = now_ms();
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

uint32_t random_u32() { return esp_random(); }

// spec 8.14. A REBOOT command resets through esp_restart(), which the chip reports as a
// software reset; this word, which survives a software reset, tells the two apart.
constexpr uint32_t        kRebootMarker = 0x5245424Fu;  // "REBO"
RTC_NOINIT_ATTR uint32_t g_reboot_marker;

lran::ResetCause reset_cause() {
  const bool commanded = g_reboot_marker == kRebootMarker;
  g_reboot_marker      = 0;
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

class Node final : public RadioClient, public GateLinkPort {
 public:
  Node() : engine_(&outbox_, &mac_, &sink_), app_(this, random_u32, &sink_) {}

  void begin(const uint8_t* key, size_t key_len) {
    app_.set_hex_timeout_ms(param_default(kParamHexTimeoutMs));
    ctx_.id = lran::kNodeGateLink;
    std::memcpy(ctx_.key, key, key_len < sizeof(ctx_.key) ? key_len : sizeof(ctx_.key));
    lran::node::reset_context(ctx_, random_u32);  // spec 10.1 - a new ctx_id every boot
  }

  // RadioClient
  void on_frame(const uint8_t* buf, size_t len, int16_t rssi_dbm, int16_t snr_db10,
                uint32_t now_ms) override {
    engine_.receive(ctx_, app_, buf, len, rssi_dbm, snr_db10, now_ms);
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

// lora_task waits this long on DIO1 or io_task with nothing moving, and this long while a
// frame is in media access or on the air, whose steps are read from the IRQ register.
constexpr uint32_t kLoraIdleWaitMs   = 100;
constexpr uint32_t kLoraActiveWaitMs = 5;

// GL3 - waits on DIO1 and io_task, through one task notification. GL1's bus test still
// drives the radio from here while it runs.
void lora_task(void*) {
  g_node.begin(g_node_key, g_node_key_len);
  const int16_t st = radio_begin(xTaskGetCurrentTaskHandle());
  g_radio_status.store(st, std::memory_order_relaxed);

  // spec 10.7 - STATUS with BOOT, then the BOOT event with its cause. Queued now; media
  // access sends them once the radio is up.
  const lran::ResetCause cause = reset_cause();
  if (!g_node.engine().announce_boot(g_node.ctx(), g_node.app(), cause, now_ms())) {
    QueueSink().line("boot: announcement not queued");
  }

  uint32_t cmd_seq = io_snapshot().cmd_seq;
  for (;;) {
    count(TaskId::Lora);
    const bool active = radio_tx_active() || g_node.outbox().size() != 0 ||
                        g_bus_run.load(std::memory_order_relaxed);
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(active ? kLoraActiveWaitMs : kLoraIdleWaitMs));
    const uint32_t now = now_ms();

    radio_service(&g_node, &g_node.outbox(), now);

    // Impl Plan 5.2 - the ACK after the last trailing edge. A failed expander write has no
    // AckResult (spec 8.2); it is counted, and the handoff carries the question.
    const IoSnapshot io = io_snapshot();
    if (io.cmd_seq != cmd_seq) {
      cmd_seq = io.cmd_seq;
      g_node.engine().finish_command(g_node.ctx(), g_node.app(), now);
    }
    g_node.app().poll_hex(g_node.engine(), g_node.ctx());
    g_node.engine().tick(g_node.ctx(), now);

    if (g_ack_drop_req.exchange(false, std::memory_order_relaxed)) g_node.app().withhold_next_ack();
    if (g_ctx_new_req.exchange(false, std::memory_order_relaxed)) {
      // A command mid-execution would lose its ACK, as it would in a real reboot; the
      // check wants the resync alone.
      if (g_node.ctx().pending.active) {
        QueueSink().line("ctx: command in flight, not renewed");
      } else {
        lran::node::reset_context(g_node.ctx(), random_u32);
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
    portENTER_CRITICAL(&g_view_mux);
    g_view = v;
    portEXIT_CRITICAL(&g_view_mux);
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
//   lran              the protocol node: context, frames, refusals, commands (GL3)
//   lran ctx new      a new ctx_id, unannounced, for spec 10.3's resync on the bench
//   lran ack drop     withhold the next fresh COMMAND_ACK, for spec 9.4's dedup hit
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
  char line[200];
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
      // Aligned 32-bit words, which lora_task alone writes: a read is never torn.
      const RadioStats& r = radio_stats();
      portENTER_CRITICAL(&g_view_mux);
      const NodeView v = g_view;
      portEXIT_CRITICAL(&g_view_mux);
      n = std::snprintf(line, sizeof(line),
                        "radio: RadioLib status %d - %s; begin fails %lu; rx %lu err %lu; tx %lu "
                        "err %lu timeout %lu forced %lu; cad err %lu deferred %lu; outbox %u%s; last error %s %d",
                        st, st == 0 ? "up" : "FAILED", static_cast<unsigned long>(r.begin_failures),
                        static_cast<unsigned long>(r.rx_frames), static_cast<unsigned long>(r.rx_driver_errors),
                        static_cast<unsigned long>(r.tx_frames), static_cast<unsigned long>(r.tx_errors),
                        static_cast<unsigned long>(r.tx_timeouts), static_cast<unsigned long>(r.tx_forced),
                        static_cast<unsigned long>(r.cad_errors), static_cast<unsigned long>(r.cad_deferred),
                        static_cast<unsigned>(v.outbox), v.tx_active ? ", sending" : "",
                        r.last_error_at[0] != '\0' ? r.last_error_at : "none", r.last_error);
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
  } else if (std::strcmp(cmd, "hang") == 0) {
    Serial.println(F("hang: interrupts off"));
    Serial.flush();
    portDISABLE_INTERRUPTS();
    for (;;) {
    }
  } else {
    n = std::snprintf(line, sizeof(line), "commands: relay <1-4> [ms] | in | sense | sd | beep | radio | lran | ved | bus <s> | restart | hang");
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

size_t start_tasks(const PageText& boot_page, const uint8_t* node_key, size_t node_key_len) {
  g_boot_page    = boot_page;
  g_node_key     = node_key;
  g_node_key_len = node_key_len;
  g_pulse_q = xQueueCreateStatic(4, sizeof(PulseRequest), g_pulse_q_buf, &g_pulse_q_storage);
  g_cmd_q   = xQueueCreateStatic(1, sizeof(RelaySequence), g_cmd_q_buf, &g_cmd_q_storage);
  g_log_q   = xQueueCreateStatic(kLogQueueDepth, sizeof(LogLine), g_log_q_buf, &g_log_q_storage);
  g_hex_q   = xQueueCreateStatic(1, sizeof(HexJob), g_hex_q_buf, &g_hex_q_storage);
  size_t started = 0;
  for (size_t i = 0; i < kTaskCount; ++i) {
    const TaskSpec& spec = task_table()[i];
    if (kSlots[i].stack_bytes != spec.stack_bytes) continue;  // a table row and a stack disagree
    TaskHandle_t h = xTaskCreateStaticPinnedToCore(
        kSlots[i].body, spec.name, spec.stack_bytes, nullptr, spec.priority, kSlots[i].stack,
        &g_tcb[i], spec.core == kAnyCore ? tskNO_AFFINITY : spec.core);
    if (h != nullptr) ++started;
    if (spec.id == TaskId::Lora) g_lora_handle = h;
  }
  return started;
}

uint32_t task_passes(TaskId id) {
  return g_passes[static_cast<size_t>(id)].load(std::memory_order_relaxed);
}

}  // namespace gatelink
