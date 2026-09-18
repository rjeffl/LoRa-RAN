// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The BF-27 frame log's ring and rendering. frame_log.h has the reasoning.

#include "frame_log.h"

#include <cstdio>
#include <cstring>

namespace bridge {
namespace {

uint32_t saturating_add(uint32_t a, uint32_t b) {
  return b > UINT32_MAX - a ? UINT32_MAX : a + b;
}

}  // namespace

void FrameLog::record(FrameLogEntry e) {
  // WAIT-FREE AND BRANCH-LIGHT ON PURPOSE. This runs on lora_task, between the radio
  // read and the queue send. The overwrite accounting is deliberately NOT done here -
  // the reader can work it out exactly from the two indices, so the producer does not
  // pay for it and cannot be delayed by it.
  const uint32_t w = write_.load(std::memory_order_relaxed);
  e.index          = w;

  slots_[w % kFrameLogSlots] = e;

  // Release: the slot's bytes are visible before the index that publishes them.
  write_.store(w + 1, std::memory_order_release);
}

bool FrameLog::read_next(FrameLogEntry* out) {
  // Bounded, so a producer running flat out cannot hold the consumer in this loop. One
  // pass per slot is more than enough: each iteration advances read_ by one, and a ring
  // that laps the reader within kFrameLogSlots attempts is reported as loss, which is
  // exactly what it is.
  for (size_t attempt = 0; attempt < kFrameLogSlots; ++attempt) {
    const uint32_t w = write_.load(std::memory_order_acquire);
    uint32_t       r = read_.load(std::memory_order_relaxed);

    if (r == w) return false;  // empty

    // Unsigned subtraction, correct across the index's own wrap - the same arithmetic
    // rx_deaf.h and lora_link.cpp's elapsed() rely on.
    const uint32_t span = w - r;
    if (span > kFrameLogSlots) {
      // The producer lapped the reader. Everything older than the ring's span is gone
      // and no amount of trying recovers it; count it and resynchronise to the oldest
      // record that still exists.
      lost_.store(saturating_add(lost_.load(std::memory_order_relaxed),
                                 span - kFrameLogSlots),
                  std::memory_order_relaxed);
      r = w - kFrameLogSlots;
    }

    const FrameLogEntry copy = slots_[r % kFrameLogSlots];
    read_.store(r + 1, std::memory_order_relaxed);

    // THE TORN-SLOT CHECK. The producer may have overwritten this slot while it was
    // being copied, in which case the bytes are a mixture of two records and the index
    // is the tell: a slot holding record r answers r, and anything else means the copy
    // is not a record that ever existed. Discard it, count it, move on.
    if (copy.index == r) {
      *out = copy;
      return true;
    }
    lost_.store(saturating_add(lost_.load(std::memory_order_relaxed), 1),
                std::memory_order_relaxed);
  }
  return false;
}

size_t render_line(const FrameLogEntry& e, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;

  // `st` and `rx` stay numeric. lran::Status has no name table and spec 14.1's
  // registry names COUNTERS, which are not one per status; inventing a third
  // vocabulary here is the drift that registry exists to retire.
  char meta[32];
  if (e.has_radio_meta()) {
    std::snprintf(meta, sizeof(meta), "rssi=%d snr=%d", static_cast<int>(e.rssi_dbm),
                  static_cast<int>(e.snr_db));
  } else {
    std::snprintf(meta, sizeof(meta), "rssi=- snr=-");
  }

  const int n = std::snprintf(
      out, cap,
      "FRAME #%lu %s t=%lu peer=0x%02x type=%u schema=%u frag=0x%02x seq=%u %s st=%u "
      "rx=%u deaf=%lu",
      static_cast<unsigned long>(e.index),
      e.dir == static_cast<uint8_t>(FrameDir::Tx) ? "tx" : "rx",
      static_cast<unsigned long>(e.ms), static_cast<unsigned>(e.peer),
      static_cast<unsigned>(e.type), static_cast<unsigned>(e.schema),
      static_cast<unsigned>(e.frag), static_cast<unsigned>(e.seq), meta,
      static_cast<unsigned>(e.status), static_cast<unsigned>(e.rx),
      static_cast<unsigned long>(e.deaf_ms));

  // Refused, never truncated. snprintf reports what it WOULD have written.
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

size_t render_batch(const FrameLogEntry* entries, size_t n, uint32_t lost, char* out,
                    size_t cap, size_t* consumed) {
  if (consumed != nullptr) *consumed = 0;
  if (out == nullptr || cap == 0 || entries == nullptr) return 0;

  size_t    len = 0;
  const int head =
      std::snprintf(out, cap, "{\"lost\":%lu,\"f\":[", static_cast<unsigned long>(lost));
  if (head < 0 || static_cast<size_t>(head) >= cap) {
    out[0] = '\0';
    return 0;
  }
  len = static_cast<size_t>(head);

  size_t fit = 0;
  for (size_t i = 0; i < n; ++i) {
    const FrameLogEntry& e = entries[i];

    char rssi[16];
    char snr[16];
    if (e.has_radio_meta()) {
      std::snprintf(rssi, sizeof(rssi), "%d", static_cast<int>(e.rssi_dbm));
      std::snprintf(snr, sizeof(snr), "%d", static_cast<int>(e.snr_db));
    } else {
      // spec 16.2.1 - an unavailable value is `null`, never a number. A Tx record and a
      // header error both reach here, and a reader must be able to tell either from a
      // reception at 0 dBm.
      std::snprintf(rssi, sizeof(rssi), "null");
      std::snprintf(snr, sizeof(snr), "null");
    }

    // Written into the remaining space, then accepted only if it fit whole. The closing
    // "]}" must fit too, so the budget is cap - 2 - the comma this record may need.
    const size_t budget = cap - len;
    const int    w      = std::snprintf(
        out + len, budget,
        "%s{\"i\":%lu,\"ms\":%lu,\"deaf\":%lu,\"d\":\"%s\",\"peer\":%u,\"type\":%u,"
                "\"schema\":%u,\"frag\":%u,\"seq\":%u,\"rssi\":%s,\"snr\":%s,\"st\":%u,"
                "\"rx\":%u}",
        fit == 0 ? "" : ",", static_cast<unsigned long>(e.index),
        static_cast<unsigned long>(e.ms), static_cast<unsigned long>(e.deaf_ms),
        e.dir == static_cast<uint8_t>(FrameDir::Tx) ? "tx" : "rx",
        static_cast<unsigned>(e.peer), static_cast<unsigned>(e.type),
        static_cast<unsigned>(e.schema), static_cast<unsigned>(e.frag),
        static_cast<unsigned>(e.seq), rssi, snr, static_cast<unsigned>(e.status),
        static_cast<unsigned>(e.rx));

    if (w < 0 || static_cast<size_t>(w) + 2 >= budget) {
      out[len] = '\0';  // this record did not fit; the batch ends before it
      break;
    }
    len += static_cast<size_t>(w);
    ++fit;
  }

  if (fit == 0) {
    out[0] = '\0';
    return 0;
  }

  // The loop reserved room for these two.
  out[len++] = ']';
  out[len++] = '}';
  out[len]   = '\0';

  if (consumed != nullptr) *consumed = fit;
  return len;
}

}  // namespace bridge
