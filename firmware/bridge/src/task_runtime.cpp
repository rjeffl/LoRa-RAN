// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task and queue creation, and the seven task bodies. Task BF-11; Impl Plan 5.2.
//
// WHAT IS HERE AND WHAT IS NOT. The structure is here: priorities, cores, static
// storage, queue boundaries and the never-block rule. The work is not - each body
// is a loop with a TODO naming the task that fills it. That division is BF-11's
// point: task structure is the thing that cannot be retrofitted cheaply, and the
// bodies can be written in any order afterwards.

#include "task_runtime.h"

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>

#include "board_ui.h"
#include "diag_json.h"
#include "lora_link.h"
#include "mqtt_pubsub.h"
#include "mqtt_transport.h"
#include "net_policy.h"
#include "node_availability.h"
#include "ota.h"
#include "radio_config.h"
#include "registry_runtime.h"
#include "scheduler.h"
#include "status_page.h"
#include "ui.h"
#include "wifi_link.h"

namespace bridge {
namespace {

// ---------------------------------------------------------------------------
// Static storage. Root rule 3 - no dynamic allocation in node firmware - so every
// queue and every task stack is a fixed array here rather than a heap block inside
// xQueueCreate/xTaskCreate.
//
// The failure this avoids is not fragmentation. It is a queue creation that returns
// NULL under memory pressure at boot and a firmware that runs anyway with one task
// silently missing.
// ---------------------------------------------------------------------------

uint8_t      g_rx_storage[queue_storage_bytes(kRxQueueDepth, sizeof(RxMessage))];
StaticQueue_t g_rx_queue_buf;
QueueHandle_t g_rx_queue = nullptr;

uint8_t      g_tx_storage[queue_storage_bytes(kTxQueueDepth, sizeof(TxMessage))];
StaticQueue_t g_tx_queue_buf;
QueueHandle_t g_tx_queue = nullptr;

uint8_t      g_publish_storage[queue_storage_bytes(kPublishQueueDepth,
                                                  sizeof(PublishMessage))];
StaticQueue_t g_publish_queue_buf;
QueueHandle_t g_publish_queue = nullptr;

// TODO(BF-11a): g_log_queue - log_task drains it; until then log_task ticks idle.

// The transport, static like everything else here (root rule 3). The seam is
// MqttTransport; this is the only line in the firmware that names PubSubClient's
// implementation, which is what makes D5's designated fallback a one-line swap.
PubSubTransport g_mqtt;

// Health, as ota_task sees it. Atomics rather than calls into the objects: PubSubClient
// is not thread-safe, and ota_task asking g_mqtt.connected() directly would be a read
// racing mqtt_task's writes. mqtt_task publishes the answer once per tick instead.
std::atomic<bool> g_mqtt_up{false};
std::atomic<bool> g_tasks_started{false};

QueueAccounting g_accounting;

// Task stacks and control blocks, one pair per row of the table.
StackType_t g_stack_lora[8192];
StackType_t g_stack_sched[3072];
StackType_t g_stack_mqtt[6144];
StackType_t g_stack_app[6144];
StackType_t g_stack_ota[4096];
StackType_t g_stack_ui[3072];
StackType_t g_stack_log[3072];

StaticTask_t g_tcb[kTaskCount];

StackType_t* stack_for(TaskId id) {
  switch (id) {
    case TaskId::Lora:  return g_stack_lora;
    case TaskId::Sched: return g_stack_sched;
    case TaskId::Mqtt:  return g_stack_mqtt;
    case TaskId::App:   return g_stack_app;
    case TaskId::Ota:   return g_stack_ota;
    case TaskId::Ui:    return g_stack_ui;
    case TaskId::Log:   return g_stack_log;
    case TaskId::kCount: break;
  }
  return nullptr;  // unreachable; the switch is exhaustive and -Werror keeps it so
}

// The declared stack_bytes must match the array actually reserved. A mismatch is a
// stack overflow that presents as a corrupted neighbour, which is among the worst
// things to debug on a board at the far end of a property. StackType_t is one byte
// on this core, so element count and bytes agree; the first assert makes that a
// checked premise rather than an assumed one.
static_assert(sizeof(StackType_t) == 1, "ESP-IDF stack depth is in bytes");
static_assert(sizeof(g_stack_lora) / sizeof(StackType_t) == 8192, "lora stack");
static_assert(sizeof(g_stack_mqtt) / sizeof(StackType_t) == 6144, "mqtt stack");

// ---------------------------------------------------------------------------
// The poll scheduler (BF-17). sched_task decides and sends, app_task reports what it heard,
// ota_task asks whether a poll is outstanding. The mutex is held only across the scheduler's
// own calls - never across a registry call or a queue send - so it never nests with the
// registry's.
// ---------------------------------------------------------------------------

PollScheduler     g_scheduler;
StaticSemaphore_t g_sched_lock_buf;
SemaphoreHandle_t g_sched_lock = nullptr;

// Read by lora_task_idle() from ota_task without the lock; written under it.
std::atomic<bool> g_poll_outstanding{false};

class SchedLock {
 public:
  SchedLock() { xSemaphoreTake(g_sched_lock, portMAX_DELAY); }
  ~SchedLock() { xSemaphoreGive(g_sched_lock); }
  SchedLock(const SchedLock&)            = delete;
  SchedLock& operator=(const SchedLock&) = delete;
};

void sched_on_heard(lran::NodeId src, uint32_t now_ms) {
  uint32_t answer_ms = PollScheduler::kNotAnAnswer;
  uint32_t window_ms = 0;
  {
    SchedLock lock;
    answer_ms          = g_scheduler.on_heard(src, now_ms);
    window_ms          = g_scheduler.reply_timeout_ms();
    g_poll_outstanding = g_scheduler.outstanding();
  }
  // B3a's poll-to-answer record (Impl Plan 6.1.1). Printed after the lock is released, so a
  // slow serial write never holds up sched_task.
  if (answer_ms != PollScheduler::kNotAnAnswer) {
    Serial.printf("poll: %02x answered in %u ms (window %u ms)\n", static_cast<unsigned>(src),
                  static_cast<unsigned>(answer_ms), static_cast<unsigned>(window_ms));
  }
}

// One tick: close an expired reply window, then start at most one poll. A POLL the TX queue
// refuses is counted by send_tx() and not reported to the scheduler, so the node stays due
// and the next tick tries again.
void sched_polls(uint32_t now_ms) {
  for (int step = 0; step < 2; ++step) {  // at most a Missed, then a Poll
    PollStep  st;
    lran::Seq seq = 0;
    {
      SchedLock lock;
      st = g_scheduler.next(now_ms, !ota_in_progress());
      if (st.action == PollAction::Poll) seq = g_scheduler.take_poll_seq();
    }
    switch (st.action) {
      case PollAction::None:
        return;
      case PollAction::Missed:
        g_poll_outstanding = false;
        registry_note_poll_missed(st.node);
        continue;
      case PollAction::Poll:
        break;
    }

    NodeState ns;
    if (!registry_state(st.node, &ns)) return;
    TxMessage tx;
    tx.dst = st.node;
    tx.len = build_poll_frame(st.node, ns.ctx_id, seq, tx.bytes, sizeof(tx.bytes));
    if (tx.len == 0 || !send_tx(tx)) return;

    SchedLock lock;
    g_scheduler.on_sent(st.node, ns.poll_interval_s, now_ms);
    g_poll_outstanding = true;
    return;
  }
}

// ---------------------------------------------------------------------------
// sched_task's publications. One static message and one JSON buffer, not locals: at ~872
// and 768 bytes they would take over half of sched_task's 3072-byte stack. Only sched_task
// touches them.
// ---------------------------------------------------------------------------

PublishMessage g_sched_msg;
char           g_sched_json[kMaxPayloadLen];

// Retained, QoS 0, like the bridge's own availability. False when the queue refused it,
// which send_publish() has already counted.
bool sched_publish(const char* topic, const char* payload) {
  return make_publish(&g_sched_msg, topic, payload, /*retain=*/true, /*qos=*/0) &&
         send_publish(g_sched_msg);
}

// ---------------------------------------------------------------------------
// The availability watchdog (BF-20). sched_task owns it outright - it is written and read in
// no other task - so it needs no lock. mqtt_task asks for a republish through an atomic, and
// ui_task reads the counts through two more.
// ---------------------------------------------------------------------------

AvailabilityWatchdog g_availability;
std::atomic<bool>    g_availability_republish{false};
std::atomic<uint8_t> g_nodes_online{kNodesUnknown};
std::atomic<uint8_t> g_nodes_watched{kNodesUnknown};

// spec 16.6 - bench publication is off by default. TODO(BF-26): simnode_diag_enable, settable
// from lran/bridge/config/set, and a mark_known_pending() when it is switched on. Until then
// a simnode's availability is judged and logged, and not published.
constexpr bool kSimnodeDiagEnable = false;

void sched_availability() {
  if (g_availability_republish.exchange(false)) g_availability.mark_known_pending();

  for (size_t i = 0; i < registry_size(); ++i) {
    const NodeInfo& info = registry_info_at(i);
    NodeState       ns;
    if (!registry_state(info.id, &ns)) continue;

    const AvailabilityChange c = g_availability.evaluate(i, info, ns);
    char                     name[16];
    if (node_topic_name(info.id, name, sizeof(name)) == 0) {
      g_availability.clear_pending(i);  // no spec 16.1 token, so no topic
      continue;
    }
    if (c.changed) {
      // The bench record of V-B3 while bench publication is off.
      Serial.printf("availability: %s %s (missed_polls %u, threshold %u)\n", name,
                    availability_payload(c.to), static_cast<unsigned>(ns.missed_polls),
                    static_cast<unsigned>(g_availability.threshold()));
    }
    if (!g_availability.pending(i)) continue;
    if (!bench_publication_allowed(info, kSimnodeDiagEnable)) {
      g_availability.clear_pending(i);
      continue;
    }

    char topic[kMaxTopicLen];
    // R-3.4c - retained. A queue that refuses it leaves the row pending for the next tick.
    if (topic_availability(name, topic, sizeof(topic)) > 0 &&
        sched_publish(topic, availability_payload(g_availability.state(i)))) {
      g_availability.clear_pending(i);
    }
  }

  g_nodes_online  = g_availability.online_count();
  g_nodes_watched = g_availability.watched_count();
}

// ---------------------------------------------------------------------------
// Diagnostics (BF-19). Every g_diag_interval_s, and on the tick after a broker connect: the
// bridge's discard counters, its radio and queues, and each watched node's link. diag_json.h
// says why the discard counters are the bridge's and not a node's.
//
// A publication the queue refuses is not retried: the next one carries newer numbers.
// ---------------------------------------------------------------------------

// TODO(BF-23): set from Home Assistant.
std::atomic<uint16_t> g_diag_interval_s{kDiagPublishIntervalDefaultS};
std::atomic<bool>     g_diag_republish{false};
bool                  g_diag_published = false;
uint32_t              g_diag_last_ms   = 0;

void sched_diag(uint32_t now_ms) {
  const bool     reconnect = g_diag_republish.exchange(false);
  const uint32_t interval  = (g_diag_interval_s == 0 ? 1u : g_diag_interval_s.load()) * 1000u;
  if (!reconnect && g_diag_published && now_ms - g_diag_last_ms < interval) return;
  g_diag_published = true;
  g_diag_last_ms   = now_ms;

  lran::Counters c;
  RadioDiag      r;
  uint32_t       unregistered = 0;
  lora_diag_snapshot(&c, &r.stats, &unregistered);
  r.tx_frames    = c.tx_frames;
  r.cad_backoffs = c.cad_backoffs;
  for (size_t q = 0; q < kQueueCount; ++q) r.queues[q] = g_accounting.stat(static_cast<QueueId>(q));

  char topic[kMaxTopicLen];
  if (topic_diag("bridge", nullptr, topic, sizeof(topic)) > 0 &&
      diag_rx_json(c, unregistered, g_sched_json, sizeof(g_sched_json)) > 0) {
    (void)sched_publish(topic, g_sched_json);
  }
  if (topic_diag("bridge", "radio", topic, sizeof(topic)) > 0 &&
      diag_radio_json(r, g_sched_json, sizeof(g_sched_json)) > 0) {
    (void)sched_publish(topic, g_sched_json);
  }

  for (size_t i = 0; i < registry_size(); ++i) {
    const NodeInfo& info = registry_info_at(i);
    // The nodes the scheduler polls, and spec 16.6's bench gate.
    if (!g_availability.watched(i) || !bench_publication_allowed(info, kSimnodeDiagEnable)) {
      continue;
    }
    char      name[16];
    NodeState ns;
    if (node_topic_name(info.id, name, sizeof(name)) == 0 || !registry_state(info.id, &ns)) {
      continue;
    }
    if (topic_diag(name, nullptr, topic, sizeof(topic)) > 0 &&
        diag_node_json(ns, now_ms, g_sched_json, sizeof(g_sched_json)) > 0) {
      (void)sched_publish(topic, g_sched_json);
    }
  }
}

// ---------------------------------------------------------------------------
// Task bodies.
// ---------------------------------------------------------------------------

// How long lora_task waits for DIO1 before its next pass. It bounds how long a frame
// queued by another task waits to be picked up, and it is the resolution of a spec 12.3
// backoff; a reception wakes the task at once regardless.
constexpr uint32_t kLoraMaxWaitMs = 10;

// Highest priority, and it never blocks on the network or on a queue. Its outputs are
// the zero-tick queue sends; its one wait is lora_wait(), bounded and on the radio's
// own interrupt (lora_link.h). BF-16.
void lora_task(void*) {
  lora_start(kHeltecV3Radio, kPhy);
  for (;;) {
    lora_service(millis());
    lora_wait(kLoraMaxWaitMs);
  }
}

// 1 s tick. Also feeds the hardware watchdog (Impl Plan 5.2).
void sched_task(void*) {
  const TickType_t period = pdMS_TO_TICKS(task_spec(TaskId::Sched).period_ms);
  TickType_t       last   = xTaskGetTickCount();
  for (;;) {
    sched_polls(millis());  // BF-17 - Impl Plan 6.1, R-3.1d
    sched_availability();   // BF-20 - PRD 3.4, spec 16.5
    sched_diag(millis());   // BF-19 - spec 14.1, 16.2
    // TODO(BF-11b): feed the hardware watchdog from here, once one is enabled.
    vTaskDelayUntil(&last, period);
  }
}

// Everything queued, in one pass, while the broker holds. Stops on the first failed
// publish and leaves the rest queued: a broker that refused one message is about to
// refuse the next, and draining into a dead socket turns a reconnect into a data loss.
//
// THE MESSAGE IS LOST ON A FAILED PUBLISH. It has already been dequeued, and
// re-queueing it would reorder it behind newer state for the same entity. Counted,
// not silent - see queues.h on root rule 4.
void drain_publish_queue() {
  PublishMessage msg;
  while (xQueueReceive(g_publish_queue, &msg, 0) == pdTRUE) {
    if (!g_mqtt.publish(msg)) {
      g_accounting.record_dropped(QueueId::Publish);
      return;
    }
  }
}

// Published on every broker connect, not only the first. A broker restart loses
// retained state unless it was persisted, and the bridge is the only thing that can
// put its own availability back (spec 16.5).
void on_mqtt_connected() {
  char topic[kMaxTopicLen];
  if (topic_availability("bridge", topic, sizeof(topic)) == 0) {
    return;
  }
  PublishMessage msg;
  if (make_publish(&msg, topic, kPayloadOnline, /*retain=*/true, /*qos=*/0)) {
    (void)g_mqtt.publish(msg);
  }

  // R-5.3e - the version, retained, on every connect. It carries the slot and the
  // image state as well, which is how V-B9 is read from Home Assistant rather than
  // from a serial cable: after a rollback, `slot` and `git` both change.
  char version[kMaxPayloadLen];
  if (topic_bridge_version(topic, sizeof(topic)) > 0 &&
      ota_version_json(version, sizeof(version)) > 0 &&
      make_publish(&msg, topic, version, /*retain=*/true, /*qos=*/0)) {
    (void)g_mqtt.publish(msg);
  }

  // TODO(BF-23): discovery configs, republished here - "on boot AND on every broker
  // reconnect" (R-3.3c). They are generated in this task and published directly
  // rather than through the queue, which is why the queue is sized for state.
  // Per-node availability (BF-20), which is a different thing from this one. sched_task
  // owns the watchdog and publishes through the queue on its next tick.
  g_availability_republish = true;
  // Diagnostics are retained too (spec 16.2), so they are put back the same way (BF-19).
  g_diag_republish = true;
}

// Normal priority, core 0, alongside the WiFi stack it talks to.
//
// THIS TASK MAY BLOCK. A publish on a reconnecting broker can occupy it for the
// socket timeout, and that is exactly what the queue in front of it buys: lora_task
// keeps receiving throughout (R-3.2b, PRD 1.3 property 2).
void mqtt_task(void*) {
  const TickType_t period = pdMS_TO_TICKS(task_spec(TaskId::Mqtt).period_ms);
  TickType_t       last   = xTaskGetTickCount();
  uint32_t         mqtt_attempt = 0;
  uint32_t         mqtt_next_ms = 0;

  for (;;) {
    const uint32_t now = millis();

    // WiFi first: there is no point attempting a broker connection without a link,
    // and each has its own backoff so a flapping AP does not also spend the broker's.
    wifi_service(now);
    g_mqtt_up = wifi_connected() && g_mqtt.connected();

    if (wifi_connected()) {
      if (g_mqtt.connected()) {
        mqtt_attempt = 0;
        g_mqtt.loop();
        drain_publish_queue();
      } else if (mqtt_next_ms == 0 || static_cast<int32_t>(now - mqtt_next_ms) >= 0) {
        // Same unsigned-wrap-safe comparison as wifi_link.cpp: millis() wraps at
        // ~49.7 days and this node is expected to run for years.
        if (g_mqtt.connect_once()) {
          mqtt_attempt = 0;
          on_mqtt_connected();
        } else {
          ++mqtt_attempt;
          mqtt_next_ms = now + reconnect_delay_ms(mqtt_attempt);
        }
      }
    }

    vTaskDelayUntil(&last, period);
  }
}

// Queue-driven: everything that turns a received frame into a publication.
void app_task(void*) {
  RxMessage msg;
  for (;;) {
    if (xQueueReceive(g_rx_queue, &msg, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    // A blocking receive is correct HERE and wrong in lora_task: app_task waiting
    // costs nothing, and it is the consumer rather than the producer.
    //
    // BF-15. The ladder refuses a source the registry does not know, so every message
    // here names a registered node. This learns its ctx_id (spec 10.1) and resets its
    // command seq on a new one (spec 10.2).
    (void)registry_observe(msg.hdr, msg.rssi_dbm, msg.snr_db, msg.rx_millis);
    // BF-17. Answers an outstanding poll to this node, and enrols a bench node in the
    // schedule the first time it is heard.
    sched_on_heard(msg.hdr.src, msg.rx_millis);
    // Discard counters are lora_task's; sched_task publishes them (BF-19).
    // TODO(BF-24): decode per schema (Impl Plan 5.3's decode/), then the publication
    // policy, into the publish queue.
  }
}

// Low priority. Two jobs: decide whether a freshly flashed image is kept (the
// verdict runs whether or not WiFi is up - an image that never associates is the
// one that must go), and service ArduinoOTA when R-5.3d allows an upload to start.
//
// An upload runs INSIDE ArduinoOTA.handle(), so this task blocks for its duration.
// That is correct at this priority. What it costs is flash writes, which stall both
// cores briefly while the cache is disabled; lora_task can miss a frame during an
// upload, and R-5.3d's deferral is what keeps an upload from starting mid-transaction.
void ota_task(void*) {
  for (;;) {
    ota_service(wifi_connected(), lora_task_idle(), g_mqtt_up, g_tasks_started,
                lora_radio_ready(), millis());
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// 500 ms tick, low priority. R-4.1c's glanceable page.
//
// A panel that fails to come up is logged once and then ignored: the display is
// MAY-level, and nothing else in the bridge waits on it. This task keeps ticking so a
// later BF can retry without restructuring it.
void ui_task(void*) {
  const TickType_t period = pdMS_TO_TICKS(task_spec(TaskId::Ui).period_ms);
  TickType_t       last   = xTaskGetTickCount();

  const bool have_panel = ui_begin(kHeltecV3Ui);
  if (!have_panel) {
    Serial.println(F("OLED: no ACK at 0x3C - check Vext. Continuing without a display."));
  }

  for (;;) {
    if (have_panel) {
      StatusSnapshot s;
      // esp_timer, not millis(): millis() wraps at ~49.7 days, and a displayed uptime
      // that returns to zero every seven weeks reads as a reboot that did not happen.
      s.uptime_s        = static_cast<uint32_t>(esp_timer_get_time() / 1000000LL);
      s.wifi_connected  = wifi_connected();
      s.wifi_rssi_dbm   = wifi_rssi_dbm();
      s.mqtt_connected  = g_mqtt_up;
      // BF-20 - online out of watched. kNodesUnknown, rendered `--`, until sched_task's
      // first tick.
      s.nodes_online    = g_nodes_online;
      s.nodes_total     = g_nodes_watched;
      s.any_dropped     = g_accounting.any_dropped();
      s.ota_in_progress = ota_in_progress();
      s.ota_pending     = ota_verify_pending();
      s.slot            = ota_running_slot();
      s.version         = LRAN_BRIDGE_VERSION;
      ui_render(build_status_lines(s), pixel_shift_x(s.uptime_s));
    }
    vTaskDelayUntil(&last, period);
  }
}

void log_task(void*) {
  for (;;) {
    // TODO(BF-11a): drain the log queue. Lowest priority on purpose - a log that
    // can preempt the radio changes what it measures.
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

TaskFunction_t body_for(TaskId id) {
  switch (id) {
    case TaskId::Lora:  return lora_task;
    case TaskId::Sched: return sched_task;
    case TaskId::Mqtt:  return mqtt_task;
    case TaskId::App:   return app_task;
    case TaskId::Ota:   return ota_task;
    case TaskId::Ui:    return ui_task;
    case TaskId::Log:   return log_task;
    case TaskId::kCount: break;
  }
  return nullptr;
}

}  // namespace

bool start_tasks() {
  g_sched_lock = xSemaphoreCreateMutexStatic(&g_sched_lock_buf);
  if (g_sched_lock == nullptr) {
    return false;
  }
  g_rx_queue = xQueueCreateStatic(kRxQueueDepth, sizeof(RxMessage), g_rx_storage,
                                  &g_rx_queue_buf);
  g_tx_queue = xQueueCreateStatic(kTxQueueDepth, sizeof(TxMessage), g_tx_storage,
                                  &g_tx_queue_buf);
  g_publish_queue = xQueueCreateStatic(kPublishQueueDepth, sizeof(PublishMessage),
                                       g_publish_storage, &g_publish_queue_buf);
  if (g_rx_queue == nullptr || g_tx_queue == nullptr || g_publish_queue == nullptr) {
    return false;
  }

  for (size_t i = 0; i < kTaskCount; ++i) {
    const TaskSpec& spec = task_table()[i];
    TaskHandle_t    h    = xTaskCreateStaticPinnedToCore(
        body_for(spec.id), spec.name, spec.stack_bytes, nullptr, spec.priority,
        stack_for(spec.id), &g_tcb[i],
        spec.core == kAnyCore ? tskNO_AFFINITY : spec.core);
    if (h == nullptr) {
      return false;
    }
  }
  // One of the verdict's two health inputs (ota_policy.h). Set only once every row
  // started, so an image with a missing task cannot be marked valid.
  g_tasks_started = true;
  return true;
}

// ---------------------------------------------------------------------------
// The sends. Zero ticks, every one of them.
//
// `0` rather than a named short timeout, because a short timeout is a blocking send
// that is hard to notice in review - and this is the file where that distinction is
// the whole point.
// ---------------------------------------------------------------------------

bool send_rx(const RxMessage& msg) {
  if (g_rx_queue == nullptr || xQueueSend(g_rx_queue, &msg, 0) != pdTRUE) {
    g_accounting.record_dropped(QueueId::Rx);
    return false;
  }
  g_accounting.record_sent(QueueId::Rx,
                           static_cast<size_t>(uxQueueMessagesWaiting(g_rx_queue)));
  return true;
}

bool send_tx(const TxMessage& msg) {
  if (g_tx_queue == nullptr || xQueueSend(g_tx_queue, &msg, 0) != pdTRUE) {
    g_accounting.record_dropped(QueueId::Tx);
    return false;
  }
  g_accounting.record_sent(QueueId::Tx,
                           static_cast<size_t>(uxQueueMessagesWaiting(g_tx_queue)));
  return true;
}

bool take_tx(TxMessage* out) {
  return g_tx_queue != nullptr && xQueueReceive(g_tx_queue, out, 0) == pdTRUE;
}

bool send_publish(const PublishMessage& msg) {
  if (g_publish_queue == nullptr || xQueueSend(g_publish_queue, &msg, 0) != pdTRUE) {
    g_accounting.record_dropped(QueueId::Publish);
    return false;
  }
  g_accounting.record_sent(
      QueueId::Publish, static_cast<size_t>(uxQueueMessagesWaiting(g_publish_queue)));
  return true;
}

bool net_begin(const char* ssid, const char* wifi_password, const char* mqtt_host,
               uint16_t mqtt_port, const char* mqtt_user, const char* mqtt_password) {
  wifi_begin(ssid, wifi_password);

  // The LWT topic and payload are static storage, not stack: PubSubClient keeps the
  // pointers it is given and uses them on every reconnect, so a stack buffer here
  // would publish whatever later occupied those bytes.
  static char will_topic[kMaxTopicLen];
  if (topic_availability("bridge", will_topic, sizeof(will_topic)) == 0) {
    return false;
  }

  MqttConfig cfg;
  cfg.host      = mqtt_host;
  cfg.port      = mqtt_port;
  cfg.user      = mqtt_user;
  cfg.password  = mqtt_password;
  cfg.client_id = "lran-bridge";  // spec 16.1 - the node's own name on the wire

  // spec 16.5 - the broker says this for us if the bridge stops saying anything.
  // Retained, so a Home Assistant that restarts during an outage learns the bridge
  // is down rather than waiting for a message that is not coming.
  cfg.will_topic   = will_topic;
  cfg.will_payload = kPayloadOffline;
  cfg.will_retain  = true;

  return g_mqtt.begin(cfg);
}

MqttTransport& mqtt() { return g_mqtt; }

bool lora_task_idle() { return lora_idle() && !g_poll_outstanding.load(); }

const QueueAccounting& queue_accounting() { return g_accounting; }

}  // namespace bridge
