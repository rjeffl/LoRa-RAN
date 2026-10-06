// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The one SPI lock. GL1; GateLink Impl Plan 5.2.

#include "spi_bus.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace gatelink {
namespace {

// Static, as every other kernel object here (root rule 3).
StaticSemaphore_t g_storage;
SemaphoreHandle_t g_bus = nullptr;

}  // namespace

void spi_bus_init() {
  if (g_bus == nullptr) g_bus = xSemaphoreCreateMutexStatic(&g_storage);
}

SpiLock::SpiLock() { xSemaphoreTake(g_bus, portMAX_DELAY); }

SpiLock::~SpiLock() { xSemaphoreGive(g_bus); }

}  // namespace gatelink
