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
#include <cstdio>

#include "board_ui.h"
#include "command.h"
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

// BF-18. mqtt_task parses a command topic into one of these; sched_task runs it.
uint8_t      g_command_storage[queue_storage_bytes(kCommandQueueDepth,
                                                   sizeof(CommandRequest))];
StaticQueue_t g_command_queue_buf;
QueueHandle_t g_command_queue = nullptr;

// BF-27 - log_task drains lora_link's frame-log ring directly (frame_log.h), which is
// why there is no g_log_queue here. A queue would have cost lora_task a copy into it and
// bought nothing: the ring IS the queue, single-producer and single-consumer, and it
// overwrites where a queue would refuse - which for a diagnostic timeline is the right
// failure. TODO(BF-11a): the LEVELED log still has no queue.

// Records moved per pass. Sixteen is four batches of the four-a-second a `--gap 250`
// burst produces, so the drain outruns the ring by two orders of magnitude and the
// budget exists to bound the pass, not to pace it.
inline constexpr size_t kLogDrainBudget = 16;

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

// The command path (BF-18), under the SAME lock as the scheduler. Both are decided on
// sched_task and reported to from app_task, and both follow the same discipline: the
// lock is held across their own calls and never across a registry call or a queue
// send. One lock rather than two removes any question of ordering between them, and
// there is no contention to win by splitting - each section is a handful of field
// writes at a 1 s tick.
CommandPath g_command;

// Read by lora_task_idle() from ota_task without the lock; written under it.
std::atomic<bool> g_poll_outstanding{false};

class SchedLock {
 public:
  SchedLock() { xSemaphoreTake(g_sched_lock, portMAX_DELAY); }
  ~SchedLock() { xSemaphoreGive(g_sched_lock); }
  SchedLock(const SchedLock&)            = delete;
  SchedLock& operator=(const SchedLock&) = delete;
};

// BF-18. A COMMAND_ACK arrived. Called from app_task; the deserialize happens outside
// the lock, and a payload that is not a well-formed ACK is dropped here rather than
// reaching the state machine.
void cmd_on_ack(const RxMessage& msg) {
  lran::msg::CommandAck ack;
  if (lran::msg::deserialize(msg.payload, msg.payload_len, &ack) != lran::Status::Ok) {
    return;
  }
  SchedLock lock;
  g_command.on_ack(msg.hdr.src, ack, msg.hdr.ctx_id, msg.rx_millis);
}

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

// R-3.1f (BF-22). Collects what lora_task refused at spec 14 stage 4 and records it
// against the node, so an unsupported version is a NAMED condition rather than a node
// that mysteriously went quiet. It still goes offline - the availability topic keeps
// spec 16.5's two tokens, which Home Assistant depends on - but its diagnostics say why.
void sched_versions() {
  lran::NodeId src = 0;
  uint8_t      ver = 0;
  while (lora_take_bad_version(&src, &ver)) {
    if (registry_note_unsupported_version(src, ver)) {
      Serial.printf("ver: %02x speaks v%u, this bridge accepts %u-%u\n",
                    static_cast<unsigned>(src), static_cast<unsigned>(ver),
                    static_cast<unsigned>(lran::kProtoVer - 1),
                    static_cast<unsigned>(lran::kProtoVer));
    }
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
    tx.len = build_poll_frame(st.node, ns.ctx_id, seq, node_tx_ver(ns), tx.bytes,
                              sizeof(tx.bytes));
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
// The command path (BF-18) - Impl Plan 6.2.
// ---------------------------------------------------------------------------

bool send_command(const CommandRequest& req) {
  if (g_command_queue == nullptr || xQueueSend(g_command_queue, &req, 0) != pdTRUE) {
    g_accounting.record_dropped(QueueId::Command);
    return false;
  }
  g_accounting.record_sent(QueueId::Command,
                           static_cast<size_t>(uxQueueMessagesWaiting(g_command_queue)));
  return true;
}

// `lran/<node>/cmd/ack` - NOT retained (spec 16.2). A retained command outcome replays
// on every HA restart and reads as a gate that just moved.
void publish_cmd_ack(const CmdStep& st) {
  char node[32];
  if (node_topic_name(st.dst, node, sizeof(node)) == 0) return;
  char topic[kMaxTopicLen];
  if (std::snprintf(topic, sizeof(topic), "lran/%s/cmd/ack", node) <= 0) return;

  // outcome, then the node's own words. `detail` carries the CACHED result when
  // `result` is DUPLICATE_CACHED (spec 6.3), which is why it is published rather than
  // folded into a single verdict.
  const char* outcome = "unknown";
  switch (st.outcome) {
    case CmdOutcome::Acked:        outcome = "acked"; break;
    case CmdOutcome::NoAck:        outcome = "no_ack"; break;
    case CmdOutcome::ResyncFailed: outcome = "resync_failed"; break;
    case CmdOutcome::Pending:      break;
  }
  char payload[kMaxPayloadLen];
  const int n = std::snprintf(
      payload, sizeof(payload),
      "{\"outcome\":\"%s\",\"seq\":%u,\"attempts\":%u,\"result\":%u,\"detail\":%u}",
      outcome, static_cast<unsigned>(st.seq), static_cast<unsigned>(st.attempt) + 1u,
      static_cast<unsigned>(st.result), static_cast<unsigned>(st.detail));
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(payload)) return;

  if (make_publish(&g_sched_msg, topic, payload, /*retain=*/false, /*qos=*/0)) {
    (void)send_publish(g_sched_msg);
  }
}

// One tick: admit a queued request if nothing is in flight, then act on whatever the
// command path asks for. A COMMAND the TX queue refuses is not reported with on_sent(),
// so its window never opens and the next tick transmits it again - the same bargain
// sched_polls() makes, and the reason neither needs a retry path of its own.
void sched_commands(uint32_t now_ms) {
  // Admit at most one per tick. The path runs one command at a time (command.h), so
  // draining the queue here would only move the wait from the queue into the path.
  bool idle = false;
  {
    SchedLock lock;
    idle = !g_command.busy();
  }
  if (idle) {
    CommandRequest req;
    if (g_command_queue != nullptr && xQueueReceive(g_command_queue, &req, 0) == pdTRUE) {
      // spec 10.2 - the seq comes from the registry and is taken ONCE per command.
      // Every retry reuses it (root rule 2, BS-3).
      NodeState ns;
      lran::Seq seq = 0;
      if (registry_state(req.dst, &ns) && registry_take_cmd_seq(req.dst, &seq)) {
        SchedLock lock;
        (void)g_command.submit(req, ns.ctx_id, seq, now_ms);
      }
    }
  }

  for (int step = 0; step < 2; ++step) {  // at most a Send, then a Resolve
    CmdStep st;
    {
      SchedLock lock;
      st = g_command.next(now_ms);
    }
    switch (st.action) {
      case CmdAction::None:
        return;

      case CmdAction::Resolve:
        Serial.printf("cmd: %02x seq %u -> outcome %d result %u detail %u\n",
                      static_cast<unsigned>(st.dst), static_cast<unsigned>(st.seq),
                      static_cast<int>(st.outcome), static_cast<unsigned>(st.result),
                      static_cast<unsigned>(st.detail));
        publish_cmd_ack(st);
        continue;

      case CmdAction::Send:
        break;
    }

    // spec 10.3 step 2 - the resync's new context and seq reach the registry here, so
    // the NEXT command starts where this one ended rather than repeating the resync.
    if (st.ctx_adopted) {
      (void)registry_adopt_ctx(st.dst, st.ctx_id);
    }

    const lran::msg::Command cmd{st.cmd, st.arg, st.arg2};
    TxMessage                tx;
    tx.dst = st.dst;
    NodeState cns;
    const uint8_t ver = registry_state(st.dst, &cns) ? node_tx_ver(cns) : lran::kProtoVer;
    tx.len = registry_build_command(st.dst, st.ctx_id, st.seq, ver, cmd, tx.bytes,
                                    sizeof(tx.bytes));
    if (tx.len == 0 || !send_tx(tx)) return;

    SchedLock lock;
    g_command.on_sent(now_ms);
    return;
  }
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
  lora_diag_snapshot(&c, &r.stats);
  r.tx_frames         = c.tx_frames;
  r.cad_backoffs      = c.cad_backoffs;
  r.errors_suppressed = lora_errors_suppressed();
  for (size_t q = 0; q < kQueueCount; ++q) r.queues[q] = g_accounting.stat(static_cast<QueueId>(q));

  char topic[kMaxTopicLen];
  if (topic_diag("bridge", nullptr, topic, sizeof(topic)) > 0 &&
      diag_rx_json(c, g_sched_json, sizeof(g_sched_json)) > 0) {
    (void)sched_publish(topic, g_sched_json);
  }
  if (topic_diag("bridge", "radio", topic, sizeof(topic)) > 0 &&
      diag_radio_json(r, g_sched_json, sizeof(g_sched_json)) > 0) {
    (void)sched_publish(topic, g_sched_json);
  }

  // BF-18. A copy under the lock, then formatted outside it - the lock is never held
  // across a queue send (BF-17's rule, and sched_publish() is one).
  CommandStats cs;
  {
    SchedLock lock;
    cs = g_command.stats();
  }
  if (topic_diag("bridge", "cmd", topic, sizeof(topic)) > 0 &&
      diag_command_json(cs, g_sched_json, sizeof(g_sched_json)) > 0) {
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
    sched_versions();          // BF-22 - R-3.1f, spec 13.1
    sched_polls(millis());     // BF-17 - Impl Plan 6.1, R-3.1d
    sched_commands(millis());  // BF-18 - Impl Plan 6.2, BS-3
    sched_availability();      // BF-20 - PRD 3.4, spec 16.5
    sched_diag(millis());      // BF-19 - spec 14.1, 16.2
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

// ---------------------------------------------------------------------------
// The inbound command sink (BF-18) - spec 16.2's `lran/<node>/cmd/<action>/set`.
//
// THIS RUNS INSIDE PubSubClient's CALLBACK, on mqtt_task, from g_mqtt.loop(). It
// parses and queues; it does not transmit, take the scheduler's lock, or touch the
// registry's learned state. Everything that decides is sched_task's.
//
// EVERY REFUSAL IS COUNTED. A command that goes nowhere must not be silent - the
// operator pressed a button and is entitled to know the bridge declined it, and
// these are the counters that say which of four reasons it was.
// ---------------------------------------------------------------------------

struct CommandInboundStats {
  uint32_t received    = 0;
  uint32_t bad_topic   = 0;  // not the grammar, or a node/action token we do not name
  uint32_t bad_payload = 0;
  uint32_t not_allowed = 0;  // spec 8.1's cmd is not in this node type's capability set
};

CommandInboundStats g_cmd_inbound;

class CommandInbound final : public MqttInbound {
 public:
  void on_message(const InboundMessage& msg) override {
    ++g_cmd_inbound.received;

    CmdTopic topic;
    if (!parse_cmd_topic(msg.topic, &topic)) {
      ++g_cmd_inbound.bad_topic;
      return;
    }
    // A node the registry does not carry has no key, so no command can reach it.
    const NodeInfo* info = registry_find(topic.node_id);
    if (info == nullptr) {
      ++g_cmd_inbound.bad_topic;
      return;
    }
    // Impl Plan 6.2 step 1 - checked here so a solar node is not woken to refuse it.
    if (!command_allowed(info->type, topic.cmd)) {
      ++g_cmd_inbound.not_allowed;
      return;
    }

    CommandRequest req;
    req.dst = topic.node_id;
    req.cmd = topic.cmd;
    if (!parse_cmd_payload(msg.payload, msg.payload_len, &req.arg, &req.arg2)) {
      ++g_cmd_inbound.bad_payload;
      return;
    }
    (void)send_command(req);  // send_command() counts a refusal of its own
  }
};

CommandInbound g_cmd_inbound_sink;

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

  // BF-18 - the command subscription, renewed on every connect. A broker restart
  // drops subscriptions, and a bridge that subscribed only once would go on looking
  // healthy while every button in Home Assistant did nothing.
  (void)g_mqtt.subscribe(kTopicCmdFilter, /*qos=*/1);

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
    // BF-18. A COMMAND_ACK is what ends a command (spec 6.2), and app_task is where
    // received frames arrive - so the ACK reaches sched_task's command path from here.
    // registry_observe() above has already learned any new ctx_id this frame carried;
    // the path is told the ACK's own ctx_id regardless, because spec 10.3's resync
    // adopts what the REJECTED_CTX carried and may not wait on another task's ordering.
    if (msg.hdr.type == lran::MsgType::CommandAck) {
      cmd_on_ack(msg);
    }
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

// BF-27 - Impl Plan 6.6. Statics rather than locals for the reason sched_task's
// g_sched_msg gives: a PublishMessage is ~872 bytes and this task's stack is 3072.
// log_task is the only writer of both.
PublishMessage g_log_msg;
FrameLogEntry  g_log_batch[kLogDrainBudget];

// One pass of the raw frame log's drain. Returns how many records it moved, so the
// caller can tell a busy tick from an idle one.
size_t drain_frame_log() {
  size_t n = 0;
  while (n < kLogDrainBudget && lora_take_frame_log(&g_log_batch[n])) {
    // Serial first, because it survives a broker that is down - which is exactly the
    // condition a receive-path investigation must not also lose its log to.
    char line[160];
    if (render_line(g_log_batch[n], line, sizeof(line)) > 0) Serial.println(line);
    ++n;
  }
  if (n == 0) return 0;

  // `lost` travels WITH the records rather than on a counter topic of its own: a reader
  // that has to go and find it somewhere else will read a gap in `i` as a lost frame on
  // the air, which is the one conclusion this whole instrument exists to get right.
  const uint32_t lost = lora_frame_log_lost();

  size_t sent = 0;
  while (sent < n) {
    size_t       consumed = 0;
    const size_t len      = render_batch(&g_log_batch[sent], n - sent, lost,
                                         g_log_msg.payload, kMaxPayloadLen, &consumed);
    if (len == 0 || consumed == 0) break;  // cannot happen for kMaxPayloadLen; not a loop

    // NOT RETAINED, and spec 16.2's table says a `/state` leaf is. THE DEVIATION IS
    // DELIBERATE AND IS RAISED, not assumed: this topic carries a rolling window of
    // arrivals, and a retained one replays a burst that finished days ago as though it
    // were arriving now - spec 16.3's own argument, reaching a topic 16.3 does not
    // cover. docs/bridge/engineering-log.md has it, and it is a spec finding, not a
    // local decision to leave in a comment.
    if (topic_diag("bridge", "rxlog", g_log_msg.topic, kMaxTopicLen) == 0) break;
    g_log_msg.payload_len = len;
    g_log_msg.retain      = false;
    g_log_msg.qos         = 0;
    (void)send_publish(g_log_msg);

    sent += consumed;
  }
  return n;
}

void log_task(void*) {
  for (;;) {
    // LOWEST PRIORITY ON PURPOSE - a log that can preempt the radio changes what it
    // measures, and BF-27's whole subject is what the radio was doing.
    //
    // A busy tick comes straight back rather than sleeping: the ring is 64 records and
    // a drain that always sleeps 100 ms between budgets falls behind a burst and starts
    // overwriting, which is loss this task invented rather than found.
    if (drain_frame_log() < kLogDrainBudget) vTaskDelay(pdMS_TO_TICKS(100));
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
  g_command_queue = xQueueCreateStatic(kCommandQueueDepth, sizeof(CommandRequest),
                                       g_command_storage, &g_command_queue_buf);
  if (g_rx_queue == nullptr || g_tx_queue == nullptr || g_publish_queue == nullptr ||
      g_command_queue == nullptr) {
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

  // BF-18 - the sink before the first connect. A subscription made while no sink is
  // attached delivers to nothing, and the broker will not send a retained command
  // again to make up for it (mqtt_transport.h).
  g_mqtt.set_inbound(&g_cmd_inbound_sink);

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
