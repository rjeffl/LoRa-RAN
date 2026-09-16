// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-15; see registry_runtime.h.

#include "registry_runtime.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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

bool registry_state(lran::NodeId id, NodeState* out) {
  Lock             lock;
  const NodeState* s = g_registry.state(id);
  if (s == nullptr) return false;
  *out = *s;
  return true;
}

}  // namespace bridge
