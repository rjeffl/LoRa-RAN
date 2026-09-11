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
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "mqtt_pubsub.h"
#include "mqtt_transport.h"
#include "net_policy.h"
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

QueueAccounting g_accounting;

// Task stacks and control blocks, one pair per row of the table.
StackType_t g_stack_lora[4096];
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

// The declared stack_words must match the array actually reserved. A mismatch is a
// stack overflow that presents as a corrupted neighbour, which is among the worst
// things to debug on a board at the far end of a property.
static_assert(sizeof(g_stack_lora) / sizeof(StackType_t) == 4096, "lora stack");
static_assert(sizeof(g_stack_mqtt) / sizeof(StackType_t) == 6144, "mqtt stack");

// ---------------------------------------------------------------------------
// Task bodies.
// ---------------------------------------------------------------------------

// Highest priority, and it never blocks on the network. Its only outputs are the
// non-blocking queue sends below.
void lora_task(void*) {
  // TODO(BF-16): RadioLib init from the injected RadioPins (spec 12.2, Impl Plan
  // 10.8.1), the RX path through the spec 14 ladder, and the TX drain of g_tx_queue.
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// 1 s tick. Also feeds the hardware watchdog (Impl Plan 5.2).
void sched_task(void*) {
  const TickType_t period = pdMS_TO_TICKS(task_spec(TaskId::Sched).period_ms);
  TickType_t       last   = xTaskGetTickCount();
  for (;;) {
    // TODO(BF-17): per-node poll scheduling with fleet-wide serialization (6.1).
    // TODO(BF-20): the availability watchdog (3.4).
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

  // TODO(BF-23): discovery configs, republished here - "on boot AND on every broker
  // reconnect" (R-3.3c). They are generated in this task and published directly
  // rather than through the queue, which is why the queue is sized for state.
  // TODO(BF-20): per-node availability, which is a different thing from this one.
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
    // TODO(BF-15): resolve the node from the registry and decode per schema.
    // TODO(BF-19): wire the spec 14 discard ladder counters through.
    // TODO(BF-24): the publication policy, into the publish queue.
    (void)msg;
  }
}

void ota_task(void*) {
  for (;;) {
    // TODO(BF-13): ArduinoOTA or esp_https_ota against the A/B partition table,
    // and the rollback V-B9 requires. It defers until lora_task_idle() - the seam
    // exists now so BF-13 does not invent one.
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void ui_task(void*) {
  const TickType_t period = pdMS_TO_TICKS(task_spec(TaskId::Ui).period_ms);
  TickType_t       last   = xTaskGetTickCount();
  for (;;) {
    // TODO(BF-14): the OLED status page - Vext first, then the ThingPulse driver.
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
        body_for(spec.id), spec.name, spec.stack_words, nullptr, spec.priority,
        stack_for(spec.id), &g_tcb[i],
        spec.core == kAnyCore ? tskNO_AFFINITY : spec.core);
    if (h == nullptr) {
      return false;
    }
  }
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

bool lora_task_idle() {
  return true;  // TODO(BF-16)
}

const QueueAccounting& queue_accounting() { return g_accounting; }

}  // namespace bridge
