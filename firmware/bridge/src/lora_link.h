// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The SX1262, and the work lora_task does with it. Task BF-16; spec 12, 14; Impl Plan
// 5.2, 5.3, 10.8.1; R-4.1b, R-5.3d.
//
// THE ONLY FILE IN THE BRIDGE THAT INCLUDES RADIOLIB. The two decisions lora_task makes
// live elsewhere and are host-tested - rx_ladder.h (spec 14 stages 2 to 10) and
// lib/lran-link's media_access.h (spec 12.3), which the simnode shares. This file moves
// bytes and interrupts between them and the radio.
//
// NOTHING HERE WAITS ON THE RADIO EITHER. RadioLib's transmit() and scanChannel() loop
// until DIO1 rises, and scanChannel()'s loop has no timeout (SX126x.cpp, 7.7.1): a radio
// that never raised CAD_DONE would hang the highest-priority task in the bridge for good.
// So a CAD and a transmission are STARTED here, and their completion is read from the IRQ
// register on later passes, each against a deadline, while lora_task keeps its loop.
// lora_task's one wait is lora_wait(): bounded, and on the radio's own interrupt.

#pragma once

#include <cstdint>

#include "error_reply.h"
#include "frame_log.h"
#include "lran/counters.h"
#include "lran/link/media_access.h"
#include "lran/mac.h"
#include "lora_stats.h"
#include "radio_config.h"
#include "rx_ladder.h"

namespace bridge {

using lran::link::CadResult;
using lran::link::MediaAccess;
using lran::link::MediaAccessConfig;
using lran::link::TxStep;

// Records the calling task as the one DIO1 wakes, then brings the radio up against the
// injected pin map (spec 12.2) and the fixed PHY (spec 12.1). Call once, at the top of
// lora_task. A failure is logged and retried from lora_service() every 10 s.
void lora_start(const RadioPins& pins, const PhyConfig& phy);

// One pass of lora_task's work: read a received frame, expire reassembly sets, move the
// waiting frame through media access, and take the next one from the TX queue.
void lora_service(uint32_t now_ms);

// Waits for DIO1 or `max_wait_ms`, whichever comes first.
void lora_wait(uint32_t max_wait_ms);

// Root rule 8 - spec 12.3's cad_retries and backoff_max_ms, and spec 11.2's
// frag_reassembly_timeout_ms, are runtime-configurable. This is where they enter.
// TODO(BF-23): called from the HA-visible configuration, through lora_task rather than
// across it; until then only the defaults are ever in effect.
void lora_configure(const MediaAccessConfig& access, uint32_t frag_timeout_ms);

// Spec 14.2's floor between two ERRORs to one peer (BF-19a). Root rule 8, same path and
// same caveat as lora_configure(): only the default is ever in effect until BF-23.
void lora_configure_errors(uint32_t min_interval_ms);

// BF-19a. ERRORs the rate limit withheld, for the diagnostic publication. Safe from any
// task, and up to a second old like the counters beside it.
uint32_t lora_errors_suppressed();

// R-3.1f (BF-22). Takes the last (src, ver) refused at spec 14 stage 4 and clears it,
// so a caller sees each one once. False when nothing is pending. Call from sched_task:
// the record exists because lora_task cannot reach the registry.
bool lora_take_bad_version(lran::NodeId* src, uint8_t* ver);

// The registry's keys and the platform HMAC. Until it is called nothing is registered, so
// every frame is refused. registry_begin() calls it before start_tasks(), so lora_task
// never races the assignment.
void lora_set_auth(lran::IMac* mac, const PeerKeys* keys);

// Safe from any task.
bool lora_radio_ready();

// R-5.3d. True when no frame is waiting or on the air, no reassembly set is incomplete,
// and the radio is receiving - or is down, when there is nothing an OTA could interrupt.
bool lora_idle();

// BF-27 - Impl Plan 6.6. Takes the oldest record the raw frame log still holds, or false
// when it is empty. FOR log_task AND NO OTHER CALLER: frame_log.h's ring is
// single-consumer, and a second reader would take records the first never sees.
bool lora_take_frame_log(FrameLogEntry* out);

// Records the ring overwrote before log_task drained them. Cumulative; the index gaps in
// the drained records say where they fell.
uint32_t lora_frame_log_lost();

// lora_task's counters, as one consistent view, for the diagnostic publication (BF-19).
// Safe from any task. lora_task copies them under a spinlock once a second, so the view is
// up to a second old; rx_dropped computed from it always agrees with the counters beside it.
//
// Since BF-15a a frame from an unregistered source is `rx_unknown_src` inside `counters`
// - spec 14 stage 9a, summed into rx_dropped. It was a separate output of this call while
// spec 14 had no stage to map it to.
void lora_diag_snapshot(lran::Counters* counters, LoraStats* stats);

}  // namespace bridge
