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
#include <ctime>

#include "board_ui.h"
#include "command.h"
#include "config_json.h"
#include "config_path.h"
#include "config_store.h"
#include "context_roll.h"
#include "diag_json.h"
#include "dummy.h"
#include "discovery.h"
#include "levers.h"
#include "lora_link.h"
#include "mqtt_pubsub.h"
#include "mqtt_transport.h"
#include "net_policy.h"
#include "node_availability.h"
#include "nvs_persist.h"
#include "ota.h"
#include "publish.h"
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

// BF-32. mqtt_task parses a `config/set` into one of these; sched_task runs the half
// that has to cross the radio.
uint8_t      g_config_storage[queue_storage_bytes(kConfigQueueDepth, sizeof(ConfigJob))];
StaticQueue_t g_config_queue_buf;
QueueHandle_t g_config_queue = nullptr;

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
StackType_t g_stack_sched[5120];
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
static_assert(sizeof(g_stack_sched) / sizeof(StackType_t) == 5120, "sched stack");

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

// BF-32's node half, under the SAME lock and for the same reasons: decided on
// sched_task, reported to from app_task, and never holding the lock across a registry
// call or a queue send.
ConfigPath g_config_path;

// BF-34, spec 10.6 - the context roll, under the SAME lock and for the same reasons as the
// command path. It serializes with the command path as well: a roll does not start while a
// command is in flight, and a command is not admitted while a roll is. The roll is a
// COMMAND on the air, and command.h's one-in-flight rule is about a node's seq space,
// which the roll resets.
ContextRoll g_roll;

// The pending bits, for mqtt_task. sched_task is the only writer, under the lock, after
// every roll that resolves. A bit only ever clears after boot, so a reader that sees a
// node clear can act on that without the lock.
std::atomic<uint32_t> g_roll_pending{kAllNodesMask};

bool roll_pending_for(lran::NodeId node) { return (g_roll_pending.load() & node_bit(node)) != 0; }

// The job in flight, kept beside the path rather than inside it. sched_task needs the
// names and the bridge half's results when the transaction resolves, and one is in
// flight across the fleet - so a copy here is simpler than an accessor that would hand
// out a reference into the state machine.
ConfigJob g_config_job;

// The most rows one topic's document can carry: a scope's own rows, or the largest set
// the parser accepts, whichever is larger.
inline constexpr size_t kMaxScopeRows = 32;
static_assert(kMaxScopeRows >= kMaxConfigSetEntries, "a set's answer must fit");
static_assert(kMaxScopeRows >= kBridgeGlobalCount, "the bridge's own block must fit");
static_assert(kMaxScopeRows >= kBridgePerNodeCount + lran::config::kNodeCommonParamCount,
              "a node's block must fit");

ConfigStore g_config;
NvsPersist  g_cfg_global_persist;

// g_config is used from two tasks. mqtt_task writes the stores and reads the mirror;
// sched_task writes the mirror when a node transaction resolves and reads it to publish.
// Neither ConfigStore nor Store takes a lock, so without this one a set arriving while a
// transaction resolves could read a half-written mirror or store.
//
// ITS OWN LOCK, HELD ACROSS g_config CALLS AND NOTHING ELSE. Never across a publish, a
// queue send, a registry call or SchedLock, so it nests with no other lock. A holder can
// wait on an NVS write inside apply(); sched_task ticks at 1 s and can afford that.
// lora_task never takes it. config_begin() runs before the tasks and takes it neither.
StaticSemaphore_t g_config_lock_buf;
SemaphoreHandle_t g_config_lock = nullptr;

// BF-23 - the store's lever values, carried to the tasks that apply them (levers.h).
// Published by config_begin() before the tasks start, and by mqtt_task after every set
// that changes a bridge-held value; read by sched_task and lora_task.
LeverBoard g_levers;
NvsPersist  g_cfg_node_persist[kNodeCount];

// ---------------------------------------------------------------------------
// BF-24 - the publication policy (publish.h), which app_task alone drives.
//
// STATIC, NOT LOCAL, for sched_task's reason: the policy holds a kilobyte document buffer
// and a PublishMessage is larger again, and app_task's stack is 6144.
// ---------------------------------------------------------------------------

PublicationPolicy g_policy;
PublishMessage    g_app_msg;

// Set by mqtt_task on every connect, taken by app_task: the retained documents may be gone.
std::atomic<bool> g_publish_forget{false};

// app_task writes these after each STATUS or EVENT and sched_task reads them for
// lran/bridge/diag/publish/state. Ten independent counters, so a reader that catches one
// updated and the next not yet sees two numbers a frame apart, which is harmless.
struct PublishStatsBoard {
  std::atomic<uint32_t> status_frames{0}, documents{0}, unchanged{0}, heartbeats{0},
      event_frames{0}, events{0}, event_repeats{0}, bench_withheld{0}, queue_refused{0},
      undecodable{0};

  void store(const PublishStats& s) {
    status_frames  = s.status_frames;
    documents      = s.documents;
    unchanged      = s.unchanged;
    heartbeats     = s.heartbeats;
    event_frames   = s.event_frames;
    events         = s.events;
    event_repeats  = s.event_repeats;
    bench_withheld = s.bench_withheld;
    queue_refused  = s.queue_refused;
    undecodable    = s.undecodable;
  }
  PublishStats load() const {
    PublishStats s;
    s.status_frames  = status_frames;
    s.documents      = documents;
    s.unchanged      = unchanged;
    s.heartbeats     = heartbeats;
    s.event_frames   = event_frames;
    s.events         = events;
    s.event_repeats  = event_repeats;
    s.bench_withheld = bench_withheld;
    s.queue_refused  = queue_refused;
    s.undecodable    = undecodable;
    return s;
  }
};
PublishStatsBoard g_publish_stats;

// Every document goes through make_publish(), so spec 16.3's retain rule and spec 16.6's
// bench topic rule are checked on this path as on every other, and the queue counts a drop.
struct QueueSink final : PublishSink {
  bool emit(const char* topic, const char* payload, bool retain, uint8_t qos) override {
    return make_publish(&g_app_msg, topic, payload, retain, qos) &&
           send_publish(g_app_msg);
  }
};

// spec 7.2.9 - the wall clock at reception. time() is SNTP's (mqtt_task starts it); before
// SNTP answers it is near 1970, which publish.cpp refuses as a clock. `rx_millis` is when
// lora_task heard the frame, so the time the frame spent queued is taken off.
UtcSeconds utc_at(uint32_t rx_millis) {
  const time_t now = time(nullptr);
  return static_cast<UtcSeconds>(now) - static_cast<UtcSeconds>((millis() - rx_millis) / 1000u);
}

ConfigSetRequest g_cfg_req;
ConfigResult     g_cfg_results[kMaxScopeRows];
ConfigStateEntry g_cfg_state_rows[kMaxScopeRows];
PublishMessage   g_cfg_msg;
char             g_cfg_doc[kMaxPayloadLen];

struct ConfigInboundStats {
  uint32_t received    = 0;
  uint32_t bad_topic   = 0;
  uint32_t bad_payload = 0;  // refused whole, spec 16.7.2's last paragraph
  uint32_t applied     = 0;  // sets whose bridge half changed something
  uint32_t no_answer   = 0;  // the ack or the state document did not fit, or would not go
  uint32_t refused_roll_pending = 0;  // spec 10.6 bridge step 7, refused whole
};

ConfigInboundStats g_cfg_inbound;

// Read by lora_task_idle() from ota_task without the lock; written under it.
std::atomic<bool> g_poll_outstanding{false};

class SchedLock {
 public:
  SchedLock() { xSemaphoreTake(g_sched_lock, portMAX_DELAY); }
  ~SchedLock() { xSemaphoreGive(g_sched_lock); }
  SchedLock(const SchedLock&)            = delete;
  SchedLock& operator=(const SchedLock&) = delete;
};

class ConfigLock {
 public:
  ConfigLock() { xSemaphoreTake(g_config_lock, portMAX_DELAY); }
  ~ConfigLock() { xSemaphoreGive(g_config_lock); }
  ConfigLock(const ConfigLock&)            = delete;
  ConfigLock& operator=(const ConfigLock&) = delete;
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
  // The roll claims its own ACK first. An ACK neither claims reaches the command path,
  // which is the one that counts it ignored.
  if (g_roll.on_ack(msg.hdr.src, ack, msg.hdr.ctx_id, msg.rx_millis)) return;
  g_command.on_ack(msg.hdr.src, ack, msg.hdr.ctx_id, msg.rx_millis);
}

// BF-32. A CONFIG_ACK arrived. Called from app_task; the deserialize happens outside the
// lock, and a payload that is not a well-formed ACK is dropped here rather than reaching
// the state machine.
void config_on_ack(const RxMessage& msg) {
  lran::schema::NodeConfigAckV1 ack;
  if (lran::schema::deserialize(msg.payload, msg.payload_len, &ack) != lran::Status::Ok) {
    return;
  }
  SchedLock lock;
  g_config_path.on_config_ack(msg.hdr.src, ack, msg.hdr.seq, msg.rx_millis);
}

void sched_on_heard(lran::NodeId src, uint32_t now_ms) {
  uint32_t answer_ms = PollScheduler::kNotAnAnswer;
  uint32_t window_ms = 0;
  {
    SchedLock lock;
    answer_ms          = g_scheduler.on_heard(src, now_ms);
    window_ms          = g_scheduler.reply_timeout_ms();
    g_poll_outstanding = g_scheduler.outstanding();
    // spec 10.6 bridge step 2 - a pending node is rolled when it is first heard. Its
    // ctx_id is already in the registry: app_task observed the frame before this.
    g_roll.on_heard(src);
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
  // Sized for this document alone, not kMaxPayloadLen: this runs on sched_task, the
  // deepest task in the firmware. Five short fields; a longer one is refused below.
  char payload[128];
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

// spec 10.6 bridge step 7 - a request for a node whose roll is pending, refused on
// `cmd/ack` rather than held. A held OPEN could reach the gate long after it was pressed.
// The outcome token is this bridge's, like the rest of the document (spec 16.2 fixes the
// topic, not the payload), and matches config/ack's `context_roll_pending`.
void publish_cmd_refused_roll_pending(lran::NodeId dst) {
  char node[32];
  if (node_topic_name(dst, node, sizeof(node)) == 0) return;
  char topic[kMaxTopicLen];
  if (std::snprintf(topic, sizeof(topic), "lran/%s/cmd/ack", node) <= 0) return;
  if (make_publish(&g_sched_msg, topic, "{\"outcome\":\"context_roll_pending\"}",
                   /*retain=*/false, /*qos=*/0)) {
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
    idle = !g_command.busy() && !g_roll.busy();
  }
  if (idle) {
    CommandRequest req;
    if (g_command_queue != nullptr && xQueueReceive(g_command_queue, &req, 0) == pdTRUE) {
      bool refused = false;
      {
        SchedLock lock;
        refused = g_roll.pending(req.dst);
        if (refused) g_roll.note_cmd_refused();
      }
      if (refused) {
        Serial.printf("cmd: %02x refused, context roll pending\n", static_cast<unsigned>(req.dst));
        publish_cmd_refused_roll_pending(req.dst);
        return;
      }
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
// The context roll (BF-34) - spec 10.6, D58, PRD R-3.1h.
//
// THE BOOT POLL IS THE SCHEDULER'S. Spec 10.6 bridge step 1 asks for a POLL to each
// registered node at boot, and the poll scheduler already sends one to every production
// row on its first ticks. A bench row keeps the 2026-09-14 rule and is polled only once
// heard, which the operator confirmed on 2026-09-23: it rolls when first heard (step 2),
// and a simnode not on the bench costs no airtime.
// ---------------------------------------------------------------------------

void sched_roll(uint32_t now_ms) {
  lran::NodeId node  = 0;
  bool         start = false;
  {
    SchedLock lock;
    start = !g_roll.busy() && !g_command.busy() && g_roll.next_due(&node);
  }
  if (start) {
    // spec 10.6 bridge step 2 - under the ctx_id the node's frame carried, with the next
    // command seq. A context still 0 is one never learned, and the node would refuse it.
    NodeState ns;
    lran::Seq seq = 0;
    if (registry_state(node, &ns) && ns.ctx_id != 0 && registry_take_cmd_seq(node, &seq)) {
      SchedLock lock;
      (void)g_roll.submit(node, ns.ctx_id, seq, now_ms);
    }
  }

  for (int step = 0; step < 2; ++step) {  // at most a Send, then a Resolve
    RollStep st;
    uint32_t pending = 0;
    {
      SchedLock lock;
      st      = g_roll.next(now_ms);
      pending = g_roll.pending_mask();
    }
    switch (st.action) {
      case RollAction::None:
        return;

      case RollAction::Resolve:
        if (st.outcome == RollOutcome::Rolled) {
          // spec 10.6 bridge step 3 - adopt the new context and restart the seq space at
          // 1, which is spec 10.3 step 2's write. After this the pending bit clears, and
          // not before: a command admitted in between would carry the old context.
          (void)registry_adopt_ctx(st.dst, st.ctx_id);
          Serial.printf("roll: %02x rolled to ctx 0x%08lx after %u attempt(s)\n",
                        static_cast<unsigned>(st.dst), static_cast<unsigned long>(st.ctx_id),
                        static_cast<unsigned>(st.attempt) + 1u);
        } else if (st.no_ack) {
          Serial.printf("roll: %02x FAILED, no answer to %u attempt(s); still pending\n",
                        static_cast<unsigned>(st.dst), static_cast<unsigned>(st.attempt) + 1u);
        } else {
          Serial.printf("roll: %02x FAILED, answered result %u; still pending\n",
                        static_cast<unsigned>(st.dst), static_cast<unsigned>(st.result));
        }
        g_roll_pending = pending;
        continue;

      case RollAction::Send:
        break;
    }

    const lran::msg::Command cmd{static_cast<uint8_t>(lran::Cmd::RollContext),
                                 lran::kRollContextGuard, 0};
    TxMessage                tx;
    tx.dst = st.dst;
    NodeState cns;
    const uint8_t ver = registry_state(st.dst, &cns) ? node_tx_ver(cns) : lran::kProtoVer;
    tx.len = registry_build_command(st.dst, st.ctx_id, st.seq, ver, cmd, tx.bytes,
                                    sizeof(tx.bytes));
    if (tx.len == 0 || !send_tx(tx)) return;

    SchedLock lock;
    g_roll.on_sent(now_ms);
    return;
  }
}

// ---------------------------------------------------------------------------
// BF-32's node half on sched_task. Spec 7.4, 7.4.1, 16.7.
//
// THE ANSWER IS ONE PUBLICATION FOR BOTH HALVES (spec 16.7.1). The bridge's half was
// applied on mqtt_task and rides in the job; this is where it meets the node's and the
// two become one `config/ack`.
// ---------------------------------------------------------------------------

// Static for the reason g_sched_msg is: sched_task's stack is 3072 and these are large.
ConfigResult     g_sched_cfg_results[kMaxScopeRows];
ConfigStateEntry g_sched_cfg_state[kMaxScopeRows];
char             g_sched_cfg_doc[kMaxPayloadLen];

void publish_config_resolution(const ConfigStep& step) {
  // sched_task's deepest path, reported the way lora_task reports its own. This node's
  // CLAUDE.md asks for a stack size corrected FROM A MEASUREMENT rather than doubled
  // after a crash, and this is the measurement.
  Serial.printf("config: %02x outcome %d, sched stack high-water %u bytes free\n",
                static_cast<unsigned>(step.dst), static_cast<int>(step.op_outcome),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

  char token[kMaxTopicLen];
  if (node_topic_name(step.dst, token, sizeof(token)) == 0) return;

  // The bridge's half first, in the order Home Assistant asked for it.
  size_t n = 0;
  for (size_t i = 0; i < g_config_job.bridge_result_count && n < kMaxScopeRows; ++i) {
    g_sched_cfg_results[n++] = g_config_job.bridge_results[i];
  }

  // Then the node's, by name. A readback carries ids the request never named, which is
  // why the way back is the table rather than the job's own list.
  for (size_t i = 0; i < step.result_count && n < kMaxScopeRows; ++i) {
    const lran::schema::ConfigAckEntry& e = step.results[i];
    const lran::config::ParamDef* d = find_param_by_id(ConfigScope::Node, e.param_id);
    if (d == nullptr) continue;  // a row this bridge's table does not carry
    ConfigResult r;
    std::snprintf(r.name, sizeof(r.name), "%s", d->name);
    r.status = result_status_of(e.status);
    if (e.len > 0) {
      r.has_value = true;
      r.value     = lran::schema::entry_signed(e.value, e.len, e.ptype);
    }
    g_sched_cfg_results[n++] = r;
  }

  // An outcome nobody knows still names the parameters it concerns (spec 16.7.3), so an
  // operator sees which entries are in doubt rather than an empty answer.
  if (step.op_outcome == ConfigOutcome::Unknown ||
      step.op_outcome == ConfigOutcome::Abandoned) {
    for (size_t i = 0; i < g_config_job.name_count && n < kMaxScopeRows; ++i) {
      ConfigResult r;
      std::snprintf(r.name, sizeof(r.name), "%s", g_config_job.names[i]);
      r.status                 = ResultStatus::Unknown;
      r.has_value              = false;
      g_sched_cfg_results[n++] = r;
    }
  }

  const AckPersist persist = g_config_job.bridge_result_count > 0
                                 ? combine_persist(g_config_job.bridge_persist, step.persist)
                                 : step.persist;

  char topic[kMaxTopicLen];
  if (topic_config(token, "ack", topic, sizeof(topic)) > 0 &&
      build_config_ack(step.op, persist, g_sched_cfg_results, n, nullptr, g_sched_cfg_doc,
                       sizeof(g_sched_cfg_doc)) > 0 &&
      make_publish(&g_sched_msg, topic, g_sched_cfg_doc, /*retain=*/false, /*qos=*/0)) {
    (void)send_publish(g_sched_msg);
  } else {
    g_accounting.record_dropped(QueueId::Publish);
  }

  // Spec 16.7.4 - the retained state is published from a COMPLETE answer only, which is
  // what `updates_state` means. An `unknown` or an abandoned readback changes nothing
  // here on purpose: the last values published stay, rather than being replaced by a
  // document half of which is this answer and half of which is the previous one.
  if (!step.updates_state && !g_config_job.bridge_changed) return;
  size_t rows = 0;
  {
    ConfigLock lock;
    if (step.updates_state) {
      // A readback describes the whole table and replaces the mirror; a SET's ACK
      // describes only what it set and merges into it. config_store.h says what each one
      // costs if they are confused.
      if (step.op == lran::ConfigOp::GetAll || step.op == lran::ConfigOp::RestoreDefaults) {
        g_config.note_readback(step.dst, step.results, step.result_count);
      } else {
        g_config.note_set_results(step.dst, step.results, step.result_count);
      }
    }
    rows = g_config.state(ConfigScope::Node, step.dst, g_sched_cfg_state, kMaxScopeRows);
  }
  if (topic_config(token, "state", topic, sizeof(topic)) > 0 &&
      build_config_state(g_sched_cfg_state, rows, g_sched_cfg_doc,
                         sizeof(g_sched_cfg_doc)) > 0 &&
      make_publish(&g_sched_msg, topic, g_sched_cfg_doc, /*retain=*/true, /*qos=*/0)) {
    (void)send_publish(g_sched_msg);
  } else {
    g_accounting.record_dropped(QueueId::Publish);
  }
}

// One tick. Admits a queued job when nothing is in flight, then acts on what the path
// asks for - the same shape as sched_commands() and for the same reasons.
void sched_config(uint32_t now_ms) {
  {
    bool busy = false;
    {
      SchedLock lock;
      busy = g_config_path.busy();
    }
    if (!busy) {
      // RECEIVED STRAIGHT INTO THE STATIC, NOT ONTO THE STACK. A ConfigJob is about a
      // kilobyte - the CONFIG payload, the names and the bridge half's results - and
      // sched_task's stack is 3072. A local here overflowed it and the board panicked
      // with "Stack canary watchpoint triggered (sched)" on the first set aimed at a
      // node, which is how this comment came to be written.
      if (g_config_queue != nullptr &&
          xQueueReceive(g_config_queue, &g_config_job, 0) == pdTRUE) {
        lran::Seq seq = 0;
        NodeState ns;
        if (registry_take_cmd_seq(g_config_job.dst, &seq) &&
            registry_state(g_config_job.dst, &ns)) {
          SchedLock lock;
          (void)g_config_path.submit(g_config_job, ns.ctx_id, seq, now_ms);
        }
      }
    }
  }

  for (int guard = 0; guard < 4; ++guard) {
    ConfigStep step;
    {
      SchedLock lock;
      step = g_config_path.next(now_ms);
    }
    switch (step.action) {
      case ConfigAction::None:
        return;

      case ConfigAction::Resolve:
        publish_config_resolution(step);
        continue;

      case ConfigAction::RequestReadback: {
        // spec 7.4 - a readback request, NOT a retransmission of the CONFIG. `POLL` is
        // unauthenticated (spec 9.2) and takes a poll seq, not the command space's.
        TxMessage tx;
        tx.dst = step.dst;
        NodeState ns;
        if (!registry_state(step.dst, &ns)) return;
        lran::Seq poll_seq = 0;
        {
          SchedLock lock;
          poll_seq = g_scheduler.take_poll_seq();
        }
        tx.len = build_poll_frame(step.dst, ns.ctx_id, poll_seq, node_tx_ver(ns), tx.bytes,
                                  sizeof(tx.bytes),
                                  lran::kPollFlagFullStatus | lran::kPollFlagConfigReadback);
        if (tx.len == 0 || !send_tx(tx)) return;
        SchedLock lock;
        g_config_path.on_sent(now_ms);
        return;
      }

      case ConfigAction::SendConfig: {
        TxMessage tx;
        tx.dst = step.dst;
        NodeState ns;
        const uint8_t ver = registry_state(step.dst, &ns) ? node_tx_ver(ns) : lran::kProtoVer;
        tx.len = registry_build_config(step.dst, step.ctx_id, step.seq, ver, step.payload,
                                       tx.bytes, sizeof(tx.bytes));
        if (tx.len == 0 || !send_tx(tx)) return;
        SchedLock lock;
        g_config_path.on_sent(now_ms);
        return;
      }
    }
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

// spec 16.6 - bench publication, off by default. sched_levers() applies it from the board,
// so sched_task alone reads it and it needs no lock. A bench node is judged and logged
// whichever way it is set; the flag decides only what reaches its topics.
bool g_simnode_diag_enable = false;

// Set for every known bench row on the tick the flag clears, and cleared once that row's
// `offline` is queued. A queue that refuses it leaves the row set for the next tick.
bool g_bench_clearing[kNodeCount] = {};

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
    if (!g_availability.pending(i) && !g_bench_clearing[i]) continue;
    // Spec 16.6 - with the flag clear a bench node publishes nothing, except `offline` on
    // the tick that clears it (sched_levers()).
    const char* payload = availability_publication(info, g_availability.state(i),
                                                   g_simnode_diag_enable, g_bench_clearing[i]);
    if (payload == nullptr) {
      g_availability.clear_pending(i);
      g_bench_clearing[i] = false;
      continue;
    }

    char topic[kMaxTopicLen];
    // R-3.4c - retained. A queue that refuses it leaves the row pending for the next tick.
    if (topic_availability(name, topic, sizeof(topic)) > 0 && sched_publish(topic, payload)) {
      g_availability.clear_pending(i);
      g_bench_clearing[i] = false;
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

// `diag_interval_s`, applied by sched_levers() below (BF-23). An atomic because ui_task
// may read it one day; today sched_task alone does.
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

  // BF-18 and BF-34. A copy under the lock, then formatted outside it - the lock is never held
  // across a queue send (BF-17's rule, and sched_publish() is one).
  CommandStats cs;
  RollStats    rs;
  {
    SchedLock lock;
    cs = g_command.stats();
    rs = g_roll.stats();
  }

  char topic[kMaxTopicLen];
  if (topic_diag("bridge", nullptr, topic, sizeof(topic)) > 0 &&
      diag_rx_json(c, rs, g_sched_json, sizeof(g_sched_json)) > 0) {
    (void)sched_publish(topic, g_sched_json);
  }
  if (topic_diag("bridge", "radio", topic, sizeof(topic)) > 0 &&
      diag_radio_json(r, g_sched_json, sizeof(g_sched_json)) > 0) {
    (void)sched_publish(topic, g_sched_json);
  }

  if (topic_diag("bridge", "cmd", topic, sizeof(topic)) > 0 &&
      diag_command_json(cs, rs, g_sched_json, sizeof(g_sched_json)) > 0) {
    (void)sched_publish(topic, g_sched_json);
  }

  // BF-24 - what the publication policy chose, beside the command path's accounting.
  if (topic_diag("bridge", "publish", topic, sizeof(topic)) > 0 &&
      publish_stats_json(g_publish_stats.load(), g_sched_json, sizeof(g_sched_json)) > 0) {
    (void)sched_publish(topic, g_sched_json);
  }

  for (size_t i = 0; i < registry_size(); ++i) {
    const NodeInfo& info = registry_info_at(i);
    // The nodes the scheduler polls, and spec 16.6's bench gate.
    if (!g_availability.watched(i) || !bench_publication_allowed(info, g_simnode_diag_enable)) {
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
// The runtime levers (BF-23, root rule 8). sched_task applies every lever it owns when the
// board carries a new generation: on its first tick after boot, and on the tick after a
// set that changed one. lora_task applies its own three in its loop, because
// lora_configure() is called through lora_task, never across it (lora_link.h).
//
// STATIC, NOT LOCAL. sched_task has the deepest stack in this firmware (tasks.cpp), and
// two Levers here are what a comparison of old against new costs.
// ---------------------------------------------------------------------------

uint32_t g_sched_levers_seen = 0;
bool     g_sched_levers_have = false;
Levers   g_sched_levers_applied;
Levers   g_sched_levers_next;

void sched_levers() {
  if (!g_levers.take_if_changed(&g_sched_levers_seen, &g_sched_levers_next)) return;
  const Levers& v = g_sched_levers_next;

  {
    SchedLock lock;
    g_scheduler.set_reply_timeout_ms(v.poll_reply_timeout_ms);
    g_command.set_ack_timeout_ms(v.command_ack_timeout_ms);
    // A retry COUNT, and only that. A command in flight keeps the seq its first attempt
    // took (root rule 2); a lower count ends its retries sooner, and nothing else moves.
    g_command.set_retries(v.cmd_retries);
    // The roll is a COMMAND on the air and waits on the same two levers (BF-34).
    g_roll.set_ack_timeout_ms(v.command_ack_timeout_ms);
    g_roll.set_retries(v.cmd_retries);
    g_config_path.set_readback_timeout_ms(v.config_readback_timeout_ms);
    g_config_path.set_ack_timeout_ms(v.config_ack_timeout_ms);
  }
  g_availability.set_threshold(v.missed_poll_threshold);
  g_diag_interval_s = v.diag_interval_s;

  // BF-26, spec 16.6. Switched on, every known node's availability and the bench nodes'
  // diagnostics go out on this tick rather than a diag_interval_s later. Switched off,
  // each bench node HA has seen gets one `offline`. Discovery is mqtt_task's, and
  // handle_config_set() restarts it.
  if (g_sched_levers_have && v.simnode_diag_enable != g_simnode_diag_enable) {
    if (v.simnode_diag_enable) {
      g_availability.mark_known_pending();
      g_diag_republish = true;
    } else {
      for (size_t i = 0; i < kNodeCount; ++i) {
        g_bench_clearing[i] = registry_info_at(i).is_bench;
      }
    }
  }
  g_simnode_diag_enable = v.simnode_diag_enable;

  // Only the nodes whose interval moved, so a set of some other lever does not retime a
  // schedule it has nothing to do with. The registry call comes first and outside the
  // scheduler's lock, which is never held across one (BF-17).
  for (size_t i = 0; i < kNodeCount; ++i) {
    const uint16_t s = v.poll_interval_s[i];
    if (g_sched_levers_have && g_sched_levers_applied.poll_interval_s[i] == s) continue;
    (void)registry_set_poll_interval(kNodeTable[i].id, s);
    SchedLock lock;
    g_scheduler.retime(kNodeTable[i].id, s);
  }

  g_sched_levers_applied = v;
  g_sched_levers_have    = true;

  // The bench record that a set reached its consumer, not just the store.
  Serial.printf("levers: gen %u - diag %u s, poll reply %u ms, missed %u, cmd ack %u ms x%u, "
                "config ack %u ms, readback %u ms, simnode diag %s\n",
                static_cast<unsigned>(g_sched_levers_seen),
                static_cast<unsigned>(v.diag_interval_s),
                static_cast<unsigned>(v.poll_reply_timeout_ms),
                static_cast<unsigned>(v.missed_poll_threshold),
                static_cast<unsigned>(v.command_ack_timeout_ms),
                static_cast<unsigned>(v.cmd_retries),
                static_cast<unsigned>(v.config_ack_timeout_ms),
                static_cast<unsigned>(v.config_readback_timeout_ms),
                v.simnode_diag_enable ? "on" : "off");
}

// ---------------------------------------------------------------------------
// Task bodies.
// ---------------------------------------------------------------------------


// Highest priority, and it never blocks on the network or on a queue. Its outputs are
// the zero-tick queue sends; its one wait is lora_wait(), bounded and on the radio's
// own interrupt (lora_link.h). BF-16.
void lora_task(void*) {
  lora_start(kHeltecV3Radio, kPhy);
  // BF-23 - this task's own three levers. The board is lock-free, so reading it here
  // waits on nothing; a publish caught mid-copy is taken on the next pass.
  uint32_t levers_seen = 0;
  Levers   levers;
  for (;;) {
    if (g_levers.take_if_changed(&levers_seen, &levers)) {
      MediaAccessConfig access;
      access.cad_retries    = levers.cad_retries;
      access.backoff_max_ms = levers.backoff_max_ms;
      lora_configure(access, levers.frag_reassembly_timeout_ms);
      lora_configure_errors(levers.error_min_interval_ms);
    }
    lora_service(millis());
    lora_wait(kLoraMaxWaitMs);
  }
}

// 1 s tick. Also feeds the hardware watchdog (Impl Plan 5.2).
void sched_task(void*) {
  const TickType_t period = pdMS_TO_TICKS(task_spec(TaskId::Sched).period_ms);
  TickType_t       last   = xTaskGetTickCount();
  for (;;) {
    sched_levers();            // BF-23 - root rule 8
    sched_versions();          // BF-22 - R-3.1f, spec 13.1
    sched_polls(millis());     // BF-17 - Impl Plan 6.1, R-3.1d
    sched_roll(millis());      // BF-34 - spec 10.6, R-3.1h; before any command
    sched_commands(millis());  // BF-18 - Impl Plan 6.2, BS-3
    sched_config(millis());    // BF-32 - spec 7.4, 7.4.1, 16.7
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
// A STATE MESSAGE IS LOST ON A FAILED PUBLISH. It has already been dequeued, and
// re-queueing it would reorder it behind newer state for the same entity. Counted,
// not silent - see queues.h on root rule 4. The node's next frame publishes it afresh.
//
// AN EVENT IS HELD AND TRIED FIRST ON THE NEXT PASS (BF-25). Nothing publishes it afresh:
// the policy has already recorded it as published, so the node's retransmission, if one
// comes, is withheld. Newer state cannot overtake it, because the pass stops at the
// failure, and a second event cannot fail while one is held for the same reason. This is
// what QoS 1 would give, and PubSubClient cannot (mqtt_pubsub.cpp).
PublishMessage g_event_held;
bool           g_event_is_held = false;

void drain_publish_queue() {
  if (g_event_is_held) {
    if (!g_mqtt.publish(g_event_held)) return;
    g_event_is_held = false;
  }
  PublishMessage msg;
  while (xQueueReceive(g_publish_queue, &msg, 0) == pdTRUE) {
    if (!g_mqtt.publish(msg)) {
      if (is_event_topic(msg.topic)) {
        g_event_held    = msg;
        g_event_is_held = true;
      } else {
        g_accounting.record_dropped(QueueId::Publish);
      }
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

// ---------------------------------------------------------------------------
// BF-32 - the configuration path's bridge half. Spec 16.7.
//
// IT RUNS ON mqtt_task, WHERE THE COMMAND PATH DOES NOT, and the difference is that a
// bridge-held parameter reaches no radio. `simnode_diag_enable` and `diag_interval_s`
// are the bridge's own; applying one is a write to a store and two publications, none
// of which needs the scheduler's lock or a transmission. The node-held half does need
// both, and it is not here yet.
//
// THE DOCUMENTS ARE STATIC, for the reason BF-19 gives for sched_task's PublishMessage.
// A PublishMessage is ~880 bytes and a config/ack is up to 768 more; mqtt_task's stack
// is 6144 and PubSubClient's callback already sits inside it. mqtt_task is the only task
// that touches any of these.
// ---------------------------------------------------------------------------


// `bridge`, or the node's token. Returns false for an address spec 16.1 gives no token.
bool config_node_token(bool is_bridge, lran::NodeId node, char* out, size_t cap) {
  if (is_bridge) {
    return std::snprintf(out, cap, "%s", kTopicBridgeToken) > 0;
  }
  return node_topic_name(node, out, cap) > 0;
}

// Retained (spec 16.7.4). Published after a set that changed a value, and on every
// broker connect.
void publish_config_state(bool is_bridge, lran::NodeId node) {
  char token[kMaxTopicLen];
  char topic[kMaxTopicLen];
  if (!config_node_token(is_bridge, node, token, sizeof(token)) ||
      topic_config(token, "state", topic, sizeof(topic)) == 0) {
    ++g_cfg_inbound.no_answer;
    return;
  }

  const ConfigScope scope = is_bridge ? ConfigScope::Bridge : ConfigScope::Node;
  size_t            n     = 0;
  {
    ConfigLock lock;
    n = g_config.state(scope, node, g_cfg_state_rows, kMaxScopeRows);
  }
  if (build_config_state(g_cfg_state_rows, n, g_cfg_doc, sizeof(g_cfg_doc)) == 0) {
    // Refused rather than truncated. A truncated retained document is the worst of the
    // three: it survives every restart until something overwrites it.
    ++g_cfg_inbound.no_answer;
    g_accounting.record_dropped(QueueId::Publish);
    return;
  }
  if (!make_publish(&g_cfg_msg, topic, g_cfg_doc, /*retain=*/true, /*qos=*/0) ||
      !g_mqtt.publish(g_cfg_msg)) {
    ++g_cfg_inbound.no_answer;
    g_accounting.record_dropped(QueueId::Publish);
  }
}

void publish_config_ack(bool is_bridge, lran::NodeId node, lran::ConfigOp op,
                        AckPersist persist, const ConfigResult* results, size_t n,
                        const char* error) {
  char token[kMaxTopicLen];
  char topic[kMaxTopicLen];
  if (!config_node_token(is_bridge, node, token, sizeof(token)) ||
      topic_config(token, "ack", topic, sizeof(topic)) == 0) {
    ++g_cfg_inbound.no_answer;
    return;
  }
  if (build_config_ack(op, persist, results, n, error, g_cfg_doc, sizeof(g_cfg_doc)) == 0) {
    ++g_cfg_inbound.no_answer;
    g_accounting.record_dropped(QueueId::Publish);
    return;
  }
  // Not retained (spec 16.7.3). An answer replayed on every HA restart would report an
  // outcome for a set nobody had just made.
  if (!make_publish(&g_cfg_msg, topic, g_cfg_doc, /*retain=*/false, /*qos=*/0) ||
      !g_mqtt.publish(g_cfg_msg)) {
    ++g_cfg_inbound.no_answer;
    g_accounting.record_dropped(QueueId::Publish);
  }
}

// True when the job reached sched_task. False leaves the caller to answer here.
bool queue_config_job(const ConfigJob& job) {
  if (g_config_queue == nullptr || xQueueSend(g_config_queue, &job, 0) != pdTRUE) {
    g_accounting.record_dropped(QueueId::Config);
    return false;
  }
  g_accounting.record_sent(QueueId::Config,
                           static_cast<size_t>(uxQueueMessagesWaiting(g_config_queue)));
  return true;
}

// Fills the CONFIG payload from the names a set named, resolving each to its row. A name
// that does not resolve never gets here - ConfigStore::apply answered it already.
void fill_node_config(ConfigJob* job, const ConfigSetRequest& node_half) {
  job->config    = lran::schema::NodeConfigV1{};
  job->config.op = lran::ConfigOp::Set;
  job->name_count = 0;

  for (size_t i = 0; i < node_half.count; ++i) {
    const lran::config::ParamDef* d = find_param(ConfigScope::Node, node_half.entries[i].name);
    if (d == nullptr) continue;
    if (job->config.count >= lran::schema::kMaxConfigEntries) break;

    uint32_t raw = 0;
    switch (lran::config::ptype_width(d->type)) {
      case 1: raw = static_cast<uint32_t>(node_half.entries[i].value) & 0xFFu; break;
      case 2: raw = static_cast<uint32_t>(node_half.entries[i].value) & 0xFFFFu; break;
      default: raw = static_cast<uint32_t>(node_half.entries[i].value); break;
    }
    lran::schema::entry_pack(&job->config.entries[job->config.count], d->id, d->type, raw);
    ++job->config.count;

    if (job->name_count < kMaxConfigSetEntries) {
      std::snprintf(job->names[job->name_count], kMaxParamNameLen, "%s", d->name);
      ++job->name_count;
    }
  }
}

// BF-26 - discovery follows simnode_diag_enable. Defined with the discovery drain below.
void discovery_take_simnode_diag(bool on);

// One `config/set`, from the topic it arrived on. Spec 16.7.2.
void handle_config_set(const ConfigTopic& target, const InboundMessage& msg) {
  const bool         is_bridge = target.is_bridge;
  const lran::NodeId node      = target.node_id;
  const ConfigScope  scope     = is_bridge ? ConfigScope::Bridge : ConfigScope::Node;

  const char* error = nullptr;
  if (!parse_config_set(msg.payload, msg.payload_len, &g_cfg_req, &error)) {
    // spec 16.7.2 - refused whole: `not_applied`, no results, and a reason. The reason
    // is what tells an operator their template is wrong rather than their broker.
    ++g_cfg_inbound.bad_payload;
    publish_config_ack(is_bridge, node, lran::ConfigOp::Set, AckPersist::NotApplied,
                       nullptr, 0, error);
    return;
  }

  // spec 10.6 bridge step 7 - refused WHOLE while the node's roll is pending, before
  // either half applies. Applying the bridge's half and refusing the node's would leave a
  // set half-done, which spec 16.7.1's one-answer rule has no way to say.
  if (!is_bridge && roll_pending_for(node) && config_set_reaches_node(scope, g_cfg_req)) {
    ++g_cfg_inbound.refused_roll_pending;
    publish_config_ack(is_bridge, node, g_cfg_req.op, AckPersist::NotApplied, nullptr, 0,
                       "context_roll_pending");
    return;
  }

  AckPersist       persist = AckPersist::NotApplied;
  size_t           n       = 0;
  ConfigSetRequest node_half;
  bool             want_node = false;

  // One section from the store write to the lever read, so the levers published are the
  // values this set left, not a mix with a resolution landing in between.
  // LeverBoard::publish() is atomic stores only, so it is safe to call under the lock.
  bool changed = false;
  {
    ConfigLock lock;
    switch (g_cfg_req.op) {
      case lran::ConfigOp::RestoreDefaults:
        n = g_config.restore_defaults(scope, node, g_cfg_results, kMaxScopeRows, &persist);
        ++g_cfg_inbound.applied;
        want_node = !is_bridge;  // the node clears its own overrides (D52)
        break;
      case lran::ConfigOp::GetAll:
        n         = g_config.read_all(scope, node, g_cfg_results, kMaxScopeRows, &persist);
        want_node = !is_bridge;  // the node's own values come from the node
        break;
      default:
        n = g_config.apply(scope, node, g_cfg_req, g_cfg_results, kMaxScopeRows, &persist,
                           &node_half);
        if (persist == AckPersist::Persisted || persist == AckPersist::AppliedNotPersisted) {
          ++g_cfg_inbound.applied;
        }
        want_node = node_half.count > 0;
        break;
    }

    // BF-23 - HERE, as soon as the store holds the value, and not beside the ack below. A
    // set with a node half returns before that point once its job is queued, and a
    // `poll_interval_s` riding with a node's own rows would reach the store and never its
    // consumer. A GET_ALL changes nothing and publishes nothing.
    changed = config_set_changed(g_cfg_req.op, persist);
    if (changed) {
      const Levers v = levers_from(g_config);
      g_levers.publish(v);
      discovery_take_simnode_diag(v.simnode_diag_enable);
    }
  }

  // SPEC 16.7.1 - ONE ack, published when EVERY half has an outcome. When a half is on
  // the air, the bridge's results travel with the job and sched_task publishes both
  // together. Answering here as well would give Home Assistant two answers to one set.
  if (want_node) {
    ConfigJob job;
    job.dst                 = node;
    job.op                  = g_cfg_req.op;
    job.bridge_persist      = persist;
    job.bridge_changed      = changed;
    job.bridge_result_count = static_cast<uint8_t>(n < kMaxConfigSetEntries ? n
                                                                            : kMaxConfigSetEntries);
    for (size_t i = 0; i < job.bridge_result_count; ++i) job.bridge_results[i] = g_cfg_results[i];

    if (g_cfg_req.op == lran::ConfigOp::Set) {
      fill_node_config(&job, node_half);
    } else {
      job.config       = lran::schema::NodeConfigV1{};
      job.config.op    = g_cfg_req.op;  // GET_ALL or RESTORE_DEFAULTS, no entries
      job.config.count = 0;
    }

    if (queue_config_job(job)) return;

    // The queue refused it. Said so rather than left silent: the bridge's half may
    // already have applied, and an operator is entitled to know the other half did not
    // even leave.
    ++g_cfg_inbound.no_answer;
    for (size_t i = 0; i < node_half.count && n < kMaxScopeRows; ++i) {
      ConfigResult r;
      std::snprintf(r.name, sizeof(r.name), "%s", node_half.entries[i].name);
      r.status         = ResultStatus::Unknown;
      r.has_value      = false;
      g_cfg_results[n++] = r;
    }
    persist = AckPersist::Unknown;
  }

  publish_config_ack(is_bridge, node, g_cfg_req.op, persist, g_cfg_results, n, nullptr);

  // Spec 16.7.4 - republished after every ack THAT CHANGED A VALUE. A GET_ALL changes
  // nothing and neither does a set every entry of which was refused, so neither
  // republishes: a retained document rewritten with its own contents is a new message to
  // every subscriber for no news.
  if (changed) {
    publish_config_state(is_bridge, node);
  }
}

// ---------------------------------------------------------------------------
// One sink, two topic families. MqttTransport::set_inbound takes a single sink
// (mqtt_transport.h's one concession to PubSubClient), so the routing is here rather
// than in the transport.
// ---------------------------------------------------------------------------

class InboundRouter final : public MqttInbound {
 public:
  void on_message(const InboundMessage& msg) override {
    ConfigTopic config_target;
    if (parse_config_topic(msg.topic, &config_target)) {
      ++g_cfg_inbound.received;
      // A node the registry does not carry has no store and no key. Answered nowhere,
      // because the topic names a node this bridge does not speak to.
      if (!config_target.is_bridge && registry_find(config_target.node_id) == nullptr) {
        ++g_cfg_inbound.bad_topic;
        return;
      }
      handle_config_set(config_target, msg);
      return;
    }
    commands_.on_message(msg);
  }

 private:
  CommandInbound commands_;
};

InboundRouter g_inbound_router;

// The retained `config/state` set, republished on every broker connect (spec 16.7.4) and
// drained a row at a time like discovery's. Row 0 is the bridge; the rest are the
// registry's nodes in order.
size_t g_config_state_cursor  = 0;
bool   g_config_state_pending = false;

void drain_config_state(size_t budget) {
  if (!g_config_state_pending) return;
  for (size_t i = 0; i < budget; ++i) {
    if (g_config_state_cursor > kNodeCount) {
      g_config_state_pending = false;
      return;
    }
    if (g_config_state_cursor == 0) {
      publish_config_state(/*is_bridge=*/true, 0);
    } else {
      publish_config_state(/*is_bridge=*/false, kNodeTable[g_config_state_cursor - 1].id);
    }
    ++g_config_state_cursor;
  }
}

// ---------------------------------------------------------------------------
// BF-23 - the discovery configs. R-3.3b: on boot AND on every broker reconnect.
//
// THERE IS NO "FIRST TIME" FLAG, deliberately. A boot and a reconnect take the same
// path: on_mqtt_connected() restarts the cursor and this drains it. The reconnect path
// is the one that gets skipped in development and the one that runs unattended (Impl
// Plan 4.4), so it is not a branch that can be got wrong - it is the only branch.
//
// DRAINED A FEW AT A TIME RATHER THAN IN ONE BURST. The whole set is a few dozen
// documents of ~450 bytes; publishing them back to back would hold mqtt_task inside
// PubSubClient without a loop() between them, on a socket that has just reconnected.
// At this task's 100 ms period the set is out within about a second either way.
//
// PUBLISHED DIRECTLY, NOT THROUGH g_publish_queue. The queue is sized for state
// (Impl Plan 4.3.2) and a reconnect would otherwise push a few dozen configs in front
// of every node's current reading, which is the wrong thing to make anyone wait for.
// ---------------------------------------------------------------------------

DiscoveryCursor g_discovery_cursor;
bool            g_discovery_pending = false;

// spec 16.6 - the flag as discovery last took it. mqtt_task alone writes and reads it,
// after setup() has written it once.
bool g_discovery_simnode_diag = false;

// Switched on, the set restarts from the top so the bench nodes' configs go out now rather
// than at the next broker connect. The rest of the set is published again with them, which
// costs a second or so once per switch and keeps one path. Switched off, nothing is
// withdrawn: the entities stay and sched_task marks them `offline` (spec 16.6).
void discovery_take_simnode_diag(bool on) {
  if (on && !g_discovery_simnode_diag) {
    g_discovery_cursor  = DiscoveryCursor{};
    g_discovery_pending = true;
  }
  g_discovery_simnode_diag = on;
}

// A PublishMessage is ~872 bytes and this is called from mqtt_task, which already
// carries one on its stack. Static for the reason BF-19 gives for sched_task's.
PublishMessage g_discovery_msg;

// registry_info_at() is lock-free and what a node IS never changes after load(), so
// this is filled once and reused. discovery_next() takes a plain array, which is what
// keeps it host-testable against a fabricated fleet.
const NodeInfo* discovery_fleet(size_t* count) {
  static NodeInfo fleet[kNodeCount];
  static bool     filled = false;
  if (!filled) {
    for (size_t i = 0; i < kNodeCount && i < registry_size(); ++i) {
      fleet[i] = registry_info_at(i);
    }
    filled = true;
  }
  *count = kNodeCount;
  return fleet;
}

// Up to `budget` configs. Clears the pending flag when the set is exhausted.
void drain_discovery(size_t budget) {
  if (!g_discovery_pending) return;

  size_t          count = 0;
  const NodeInfo* fleet = discovery_fleet(&count);

  for (size_t i = 0; i < budget; ++i) {
    DiscoveryItem item;
    // Spec 16.6 - bench nodes only while the flag is set, so their entities do not enter
    // HA's registry to sit at `unknown` on a bridge that never publishes them.
    if (!discovery_next(&g_discovery_cursor, fleet, count, g_discovery_simnode_diag, &item)) {
      g_discovery_pending = false;
      return;
    }

    char topic[kMaxTopicLen];
    char config[kMaxDiscoveryPayload];
    if (discovery_topic(item, topic, sizeof(topic)) == 0 ||
        discovery_config_json(item, config, sizeof(config)) == 0) {
      // Refused rather than truncated, and counted where every other refusal is. An
      // entity missing from Home Assistant is the symptom; this is the record of why.
      g_accounting.record_dropped(QueueId::Publish);
      continue;
    }
    if (!make_publish(&g_discovery_msg, topic, config, /*retain=*/true, /*qos=*/0) ||
        !g_mqtt.publish(g_discovery_msg)) {
      g_accounting.record_dropped(QueueId::Publish);
      return;  // the socket is unhappy; the next connect restarts the whole set anyway
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
  // Static, and mqtt_task is its only user: beside `msg` it would put two payloads on
  // this task's stack at once, 3 KB since kMaxPayloadLen rose to 1536 for BF-33.
  static char version[kMaxPayloadLen];
  if (topic_bridge_version(topic, sizeof(topic)) > 0 &&
      ota_version_json(version, sizeof(version)) > 0 &&
      make_publish(&msg, topic, version, /*retain=*/true, /*qos=*/0)) {
    (void)g_mqtt.publish(msg);
  }

  // BF-18 - the command subscription, renewed on every connect. A broker restart
  // drops subscriptions, and a bridge that subscribed only once would go on looking
  // healthy while every button in Home Assistant did nothing.
  (void)g_mqtt.subscribe(kTopicCmdFilter, /*qos=*/1);

  // BF-32 - the configuration subscription, renewed for the same reason. Spec 16.7.2's
  // `config/set` is not retained, so a set published during an outage is gone; what a
  // reconnect has to put back is the retained `config/state` below, not the request.
  (void)g_mqtt.subscribe(kTopicConfigFilter, /*qos=*/1);

  // BF-23 - the discovery configs, republished from the top on every connect
  // (R-3.3b). The cursor is restarted here and drained by mqtt_task's loop; a
  // reconnect part-way through a previous drain therefore starts again rather than
  // resuming into a set HA has already forgotten.
  g_discovery_cursor  = DiscoveryCursor{};
  g_discovery_pending = true;

  // BF-32, spec 16.7.4 - the retained configuration state, put back the same way and for
  // the same reason. The cursor counts the bridge itself as row 0 and the registry's
  // nodes after it.
  g_config_state_cursor  = 0;
  g_config_state_pending = true;

  // Per-node availability (BF-20), which is a different thing from this one. sched_task
  // owns the watchdog and publishes through the queue on its next tick.
  g_availability_republish = true;
  // Diagnostics are retained too (spec 16.2), so they are put back the same way (BF-19).
  g_diag_republish = true;
  // BF-24 - and so are the decoded documents. app_task republishes each on its node's next
  // frame; nothing is republished from a cache (R-5.2b).
  g_publish_forget = true;
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

    // BF-24, spec 7.2.9 - SNTP, started once the first time WiFi is up. lwIP keeps it
    // running and re-syncs by itself from then on; nothing here waits for an answer.
    static bool sntp_started = false;
    if (!sntp_started && wifi_connected()) {
      configTime(0, 0, kNtpServer);
      sntp_started = true;
    }

    if (wifi_connected()) {
      if (g_mqtt.connected()) {
        mqtt_attempt = 0;
        g_mqtt.loop();
        drain_publish_queue();
        // After the queue: a node's current reading matters more than a config HA has
        // already got, and the set is republished on the next connect regardless.
        drain_discovery(4);
        // After discovery: an entity must exist before its state means anything.
        drain_config_state(2);
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
  QueueSink sink;
  uint32_t  levers_seen = 0;
  Levers    levers;
  for (;;) {
    if (xQueueReceive(g_rx_queue, &msg, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    // BF-24 - the policy's three levers, from the board every other task reads.
    if (g_levers.take_if_changed(&levers_seen, &levers)) {
      g_policy.set_levers(PublishLevers{levers.republish_interval_s, levers.bms_stale_s,
                                        levers.cell_mv_deadband});
    }
    if (g_publish_forget.exchange(false)) g_policy.forget_published();
    // BF-27. A dummy frame goes to the policy and nowhere else: no node sent it, so it must
    // not teach the registry a ctx_id, answer a poll or move availability (queues.h). Its
    // event is marked here because an EVENT has no status_reason to carry the mark (spec
    // 7.3); its STATUS is marked by its own DEBUG_SYNTHETIC.
    if (msg.dummy) {
      const NodeInfo* info = registry_find(msg.hdr.src);
      if (info != nullptr) {
        if (msg.hdr.type == lran::MsgType::Event) {
          g_policy.on_event(*info, msg.hdr, msg.payload, msg.payload_len,
                            /*synthetic=*/true, sink);
        } else {
          g_policy.on_status(*info, msg.hdr, msg.payload, msg.payload_len, msg.rx_millis,
                             utc_at(msg.rx_millis), sink);
        }
        g_publish_stats.store(g_policy.stats());
      }
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
    // BF-32. A CONFIG_ACK ends a configuration transaction (spec 7.4), and an unsolicited
    // one answers the readback a POLL bit 1 asked for (D45). Both arrive here.
    if (msg.hdr.type == lran::MsgType::ConfigAck) {
      config_on_ack(msg);
    }
    // Discard counters are lora_task's; sched_task publishes them (BF-19).
    // BF-24 - a STATUS becomes its documents (Impl Plan 6.3). The registry refused an
    // unknown source at the ladder, so find() answers for every message here.
    // BF-25 - an EVENT is published once, not retained (Impl Plan 6.3.2).
    if (msg.hdr.type == lran::MsgType::Event) {
      const NodeInfo* info = registry_find(msg.hdr.src);
      if (info != nullptr) {
        g_policy.on_event(*info, msg.hdr, msg.payload, msg.payload_len,
                          /*synthetic=*/false, sink);
        g_publish_stats.store(g_policy.stats());
      }
    }
    if (msg.hdr.type == lran::MsgType::Status) {
      const NodeInfo* info = registry_find(msg.hdr.src);
      if (info != nullptr) {
        g_policy.on_status(*info, msg.hdr, msg.payload, msg.payload_len, msg.rx_millis,
                           utc_at(msg.rx_millis), sink);
        g_publish_stats.store(g_policy.stats());
      }
    }
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

  // NOT RETAINED, and spec 16.2's table says a `/state` leaf is. THE DEVIATION IS
  // DELIBERATE AND IS RAISED, not assumed: this topic carries a rolling window of
  // arrivals, and a retained one replays a burst that finished days ago as though it
  // were arriving now - spec 16.3's own argument, reaching a topic 16.3 does not cover.
  // Impl Plan 6.6.1 has it, and it is a spec finding rather than a local decision left
  // in a comment.
  if (topic_diag("bridge", "rxlog", g_log_msg.topic, kMaxTopicLen) == 0) return n;
  g_log_msg.retain = false;
  g_log_msg.qos    = 0;

  size_t sent = 0;
  while (sent < n) {
    // RENDERED STRAIGHT INTO THE MESSAGE rather than into a local and then through
    // make_publish(), which is what every other publisher here does. The reason is the
    // stack: log_task has 3072 bytes and a payload buffer is 768 of them, so the copy
    // make_publish() exists to do would be paid twice on the one task that has no room
    // for it. What make_publish() enforces is kept - render_batch refuses rather than
    // truncating, topic_diag above refuses an oversized topic, and the retain flag is
    // set once, away from the loop, where spec 16.3's rule is readable.
    size_t       consumed = 0;
    const size_t len      = render_batch(&g_log_batch[sent], n - sent, lost,
                                         g_log_msg.payload, kMaxPayloadLen, &consumed);
    if (len == 0 || consumed == 0) break;  // cannot happen for kMaxPayloadLen; not a loop

    g_log_msg.payload_len = len;
    (void)send_publish(g_log_msg);

    sent += consumed;
  }
  return n;
}

// M25 - the channel buckets, to serial only. NOT TO MQTT, unlike the frame log: a
// six-to-twelve-hour capture is 43 200 buckets, and a retained-or-not topic carrying a
// message a second for half a day is a different kind of object from a diagnostic. The
// serial line is the deliverable and tools/simctl/rssi_capture.py is what reads it.
ChanRollupper g_chan_rollup;

size_t drain_chan() {
  size_t     n = 0;
  ChanBucket b;
  char       line[192];

  while (n < kLogDrainBudget && lora_take_chan(&b)) {
    // A bucket that saw something gets its own line; every bucket, loud or quiet, goes
    // into the rollup. chan_monitor.h has the reasoning - dropping the quiet ones
    // outright would take the denominator with them.
    if (chan_notable(b) && render_chan(b, line, sizeof(line)) > 0) Serial.println(line);

    g_chan_rollup.add(b);
    if (g_chan_rollup.due()) {
      ChanRollup r;
      if (g_chan_rollup.take(&r) && render_chan_rollup(r, line, sizeof(line)) > 0) {
        Serial.println(line);
      }
    }
    ++n;
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
    const size_t moved = drain_frame_log() + drain_chan();
    if (moved < kLogDrainBudget) vTaskDelay(pdMS_TO_TICKS(100));
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
  g_sched_lock  = xSemaphoreCreateMutexStatic(&g_sched_lock_buf);
  g_config_lock = xSemaphoreCreateMutexStatic(&g_config_lock_buf);
  if (g_sched_lock == nullptr || g_config_lock == nullptr) {
    return false;
  }
  g_rx_queue = xQueueCreateStatic(kRxQueueDepth, sizeof(RxMessage), g_rx_storage,
                                  &g_rx_queue_buf);
  g_tx_queue = xQueueCreateStatic(kTxQueueDepth, sizeof(TxMessage), g_tx_storage,
                                  &g_tx_queue_buf);
  g_publish_queue = xQueueCreateStatic(kPublishQueueDepth, sizeof(PublishMessage),
                                       g_publish_storage, &g_publish_queue_buf);
  g_config_queue = xQueueCreateStatic(kConfigQueueDepth, sizeof(ConfigJob),
                                      g_config_storage, &g_config_queue_buf);
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

// BF-27. The generator and its per-boot context live here, touched only by loop().
namespace {
DummyPublisher g_dummy;
lran::CtxId    g_dummy_ctx = 0;
RxMessage      g_dummy_msg;  // static: an RxMessage is ~240 bytes and loop()'s stack is small
}  // namespace

void console_line(const char* line) {
  // A fresh context per boot, so spec 7.3's (ctx_id, event_id) does not repeat when the
  // dummy's event_id restarts. Zero is the value a node has before it is heard (spec 10.1).
  while (g_dummy_ctx == 0) g_dummy_ctx = esp_random();
  static char        reply[1024];  // `dummy show` is ~800 bytes; static for loop()'s stack
  const DummyOutcome o =
      g_dummy.handle(line, g_dummy_ctx, millis(), &g_dummy_msg, reply,
                     sizeof(reply));
  if (o == DummyOutcome::NotMine) return;
  if (o == DummyOutcome::Inject) {
    // R-5.2d's other half. A node heard this boot is real, and synthetic history
    // interleaved with its own is what the marking exists to prevent. A registry
    // question, so it is asked here rather than in dummy.cpp (dummy.h).
    NodeState st;
    if (registry_state(g_dummy_msg.hdr.src, &st) && st.frames_heard > 0) {
      Serial.printf("dummy: refused - node %02x has been heard this boot\n",
                    static_cast<unsigned>(g_dummy_msg.hdr.src));
      return;
    }
    if (!send_rx(g_dummy_msg)) {
      Serial.println(F("dummy: refused - the RX queue is full (counted)"));
      return;
    }
  }
  Serial.println(reply);
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

// BF-32 - the configuration store, before the tasks and before the network. Returns how
// many stored values it put back, and reports nothing as a failure: a bridge whose NVS
// refuses to open still applies every set and answers APPLIED_NOT_PERSISTED, which is
// spec 8.11's whole point.
size_t config_begin() {
  const bool global_ok = g_cfg_global_persist.begin(/*global=*/true, 0);
  lran::config::Persist* per_node[kNodeCount];
  for (size_t i = 0; i < kNodeCount; ++i) {
    (void)g_cfg_node_persist[i].begin(/*global=*/false, kNodeTable[i].id);
    per_node[i] = &g_cfg_node_persist[i];
  }
  g_config.begin(global_ok ? &g_cfg_global_persist : nullptr, per_node, kNodeCount);

  size_t restored = nvs_restore(g_config, ConfigScope::Bridge, 0, g_cfg_global_persist);
  for (size_t i = 0; i < kNodeCount; ++i) {
    restored += nvs_restore(g_config, ConfigScope::Node, kNodeTable[i].id,
                            g_cfg_node_persist[i]);
  }
  // BF-23 - AFTER the restore, so each task's first pass applies what NVS held. Published
  // before a restore, the levers would run their defaults until the first set, and a
  // reboot would quietly undo every saved value.
  const Levers v = levers_from(g_config);
  g_levers.publish(v);
  discovery_take_simnode_diag(v.simnode_diag_enable);
  return restored;
}

bool net_begin(const char* ssid, const char* wifi_password, const char* mqtt_host,
               uint16_t mqtt_port, const char* mqtt_user, const char* mqtt_password) {
  wifi_begin(ssid, wifi_password);

  // BF-18 - the sink before the first connect. A subscription made while no sink is
  // attached delivers to nothing, and the broker will not send a retained command
  // again to make up for it (mqtt_transport.h).
  g_mqtt.set_inbound(&g_inbound_router);

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
