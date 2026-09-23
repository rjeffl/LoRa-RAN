// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-15; see registry_runtime.h.

#include "registry_runtime.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "command.h"
#include "lora_link.h"
#include "mbedtls_mac.h"

namespace bridge {
namespace {

// Static, like everything else in this firmware (root rule 3).
Registry                 g_registry;
lran::esp32::MbedtlsMac  g_mac;
StaticSemaphore_t        g_lock_buf;
SemaphoreHandle_t        g_lock = nullptr;

// An unbounded wait is correct here and would be wrong in lora_task. The sections this
// lock guards are a field update or a struct copy, and no caller does I/O while holding
// it, so a waiter is delayed by microseconds. A mutex rather than a critical section so
// that priority inheritance applies.
class Lock {
 public:
  Lock() { xSemaphoreTake(g_lock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_lock); }
  Lock(const Lock&)            = delete;
  Lock& operator=(const Lock&) = delete;
};

}  // namespace

bool registry_begin(const uint8_t master[lran::kMasterKeyLen]) {
  g_lock = xSemaphoreCreateMutexStatic(&g_lock_buf);
  if (g_lock == nullptr) return false;

  // spec 9.1 - HKDF built from mbedtls_md_hmac, never mbedtls_hkdf, which links on no
  // Arduino-ESP32 build (tools/checks/no_mbedtls_hkdf.py).
  lran::esp32::MbedtlsKdf kdf;
  g_registry.load(master, &kdf);

  lora_set_auth(&g_mac, &g_registry);
  return true;
}

const NodeInfo* registry_find(lran::NodeId id) { return g_registry.find(id); }

size_t registry_size() { return g_registry.size(); }

const NodeInfo& registry_info_at(size_t i) { return g_registry.info_at(i); }

Observed registry_observe(const lran::Header& hdr, int16_t rssi_dbm, int8_t snr_db,
                          uint32_t now_ms) {
  Lock lock;
  return g_registry.observe(hdr, rssi_dbm, snr_db, now_ms);
}

bool registry_note_poll_missed(lran::NodeId id) {
  Lock lock;
  return g_registry.note_poll_missed(id);
}

bool registry_state(lran::NodeId id, NodeState* out) {
  Lock             lock;
  const NodeState* s = g_registry.state(id);
  if (s == nullptr) return false;
  *out = *s;
  return true;
}

bool registry_take_cmd_seq(lran::NodeId id, lran::Seq* out) {
  Lock lock;
  return g_registry.take_cmd_seq(id, out);
}

bool registry_adopt_ctx(lran::NodeId id, lran::CtxId ctx) {
  Lock lock;
  return g_registry.adopt_ctx(id, ctx);
}

bool registry_note_unsupported_version(lran::NodeId id, uint8_t ver) {
  Lock lock;
  return g_registry.note_unsupported_version(id, ver);
}

size_t registry_build_command(lran::NodeId dst, lran::CtxId ctx, lran::Seq seq, uint8_t ver,
                              const lran::msg::Command& cmd, uint8_t* buf, size_t cap) {
  // key_for() is lock-free by construction (registry.h): keys are derived in
  // registry_begin() before start_tasks() and never written again. The lock guards
  // what the bridge has LEARNED, and a key is not that.
  const uint8_t* key = g_registry.key_for(dst);
  if (key == nullptr) return 0;

  lran::EncodeCtx ectx;
  ectx.mac      = &g_mac;
  ectx.node_key = key;
  return build_command_frame(dst, ctx, seq, ver, cmd, ectx, buf, cap);
}

size_t registry_build_config(lran::NodeId dst, lran::CtxId ctx, lran::Seq seq, uint8_t ver,
                             const lran::schema::NodeConfigV1& cfg, uint8_t* buf,
                             size_t cap) {
  const uint8_t* key = g_registry.key_for(dst);
  if (key == nullptr) return 0;

  uint8_t payload[lran::kMaxSchemaPayload];
  size_t  plen = 0;
  if (lran::schema::serialize(cfg, payload, sizeof(payload), &plen) != lran::Status::Ok) {
    return 0;
  }

  lran::Header h;
  h.ver    = ver;  // R-3.1e - the version last heard from this node (BF-22)
  h.type   = lran::MsgType::Config;
  h.src    = lran::kNodeBridge;
  h.dst    = dst;
  h.seq    = seq;
  h.ctx_id = ctx;
  h.schema = lran::kSchemaNodeConfigV1;

  lran::EncodeCtx ectx;
  ectx.mac      = &g_mac;
  ectx.node_key = key;
  size_t len    = 0;
  return lran::encode(h, payload, plen, ectx, buf, cap, &len) == lran::Status::Ok ? len : 0;
}

}  // namespace bridge
