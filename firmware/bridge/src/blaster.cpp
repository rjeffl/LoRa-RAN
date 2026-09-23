// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// V-B12's WiFi load. blaster.h says what it is for and how it is driven; this file is
// compiled into every heltec image and is empty outside `v_b12_blaster`.

#include "blaster.h"

#if defined(LRAN_V_B12_BLASTER)

#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bridge {
namespace {

// RFC 863 discard. Nothing is expected to listen, and the load is the transmit, not
// the delivery: a host that answers with ICMP port unreachable adds a little receive.
constexpr uint16_t kDiscardPort = 9;

// 1472 fills one 1500-byte Ethernet MTU with no fragmentation, which is the frame size
// an iperf UDP flood sends by default.
constexpr size_t kMaxPayload = 1472;
constexpr size_t kMinPayload = 16;

// A bound against a typo, not a rate cap. The cap for a run is chosen on the bench and
// passed at run time (root rule 8); above what the radio can carry, sends start failing
// and `fail` counts them.
constexpr uint32_t kMaxKbps = 50000;

// The credit the pacer may bank while loop() is not scheduled. Without a ceiling a long
// stall turns into a burst at line rate, which is a different load from the one asked for.
constexpr uint32_t kMaxBankMs = 20;

// Sends per poll, so one poll never holds loop() for long when the stack is keeping up.
constexpr int kMaxSendsPerPoll = 64;

// A raw lwIP socket, not WiFiUDP: WiFiUDP logs every refused send at error level, and at
// a thousand refusals a second that log is itself a load on the serial port and the CPU.
const char* g_host = nullptr;
int g_sock = -1;
sockaddr_in g_dest = {};
IPAddress g_target;
uint8_t g_payload[kMaxPayload];  // zeros; the content does not matter

uint32_t g_kbps = 0;
size_t g_bytes_per_packet = kMaxPayload;
uint32_t g_credit = 0;  // bytes
uint32_t g_last_us = 0;

uint32_t g_started_ms = 0;
uint32_t g_stopped_ms = 0;  // so a status read after a stop still covers the run alone
uint32_t g_sent = 0;
uint32_t g_fail = 0;
uint32_t g_down = 0;   // polls skipped because the link was down; nothing was sent
int g_last_err = 0;   // errno of the most recent refused send
uint64_t g_bytes = 0;

char g_line[64];
size_t g_line_len = 0;

void print_totals(const char* state) {
  const uint32_t end = g_kbps ? millis() : g_stopped_ms;
  const uint32_t ms = g_started_ms ? end - g_started_ms : 0;
  const uint32_t kbps = ms ? static_cast<uint32_t>(g_bytes * 8 / ms) : 0;
  Serial.printf("blast: %s sent=%lu bytes=%llu fail=%lu err=%d down=%lu ms=%lu kbps=%lu\n",
                state, static_cast<unsigned long>(g_sent),
                static_cast<unsigned long long>(g_bytes), static_cast<unsigned long>(g_fail),
                g_last_err, static_cast<unsigned long>(g_down), static_cast<unsigned long>(ms),
                static_cast<unsigned long>(kbps));
}

// The target is resolved at each start rather than at boot, because the network is
// not up at boot (mqtt_task associates on its own backoff).
bool resolve_target() {
  if (g_target.fromString(g_host)) {
    return true;
  }
  return WiFi.hostByName(g_host, g_target) == 1;
}

void start(uint32_t kbps, size_t bytes) {
  if (kbps > kMaxKbps || bytes < kMinPayload || bytes > kMaxPayload) {
    Serial.printf("blast: refused - kbps 1..%lu, bytes %u..%u\n",
                  static_cast<unsigned long>(kMaxKbps), static_cast<unsigned>(kMinPayload),
                  static_cast<unsigned>(kMaxPayload));
    return;
  }
  if (!resolve_target()) {
    Serial.printf("blast: refused - cannot resolve %s\n", g_host);
    return;
  }
  if (g_sock < 0) {
    g_sock = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock < 0) {
      Serial.printf("blast: refused - no socket, errno %d\n", errno);
      return;
    }
  }
  g_dest.sin_family = AF_INET;
  g_dest.sin_port = htons(kDiscardPort);
  g_dest.sin_addr.s_addr = static_cast<uint32_t>(g_target);
  // A rate change mid-run keeps the totals, so the summary still covers the whole run.
  if (g_kbps == 0) {
    g_started_ms = millis();
    g_sent = 0;
    g_fail = 0;
    g_down = 0;
    g_last_err = 0;
    g_bytes = 0;
  }
  g_kbps = kbps;
  g_bytes_per_packet = bytes;
  g_credit = 0;
  g_last_us = micros();
  Serial.printf("blast: on kbps=%lu bytes=%u to %s:%u\n", static_cast<unsigned long>(kbps),
                static_cast<unsigned>(bytes), g_target.toString().c_str(), kDiscardPort);
}

void stop() {
  if (g_kbps == 0) {
    print_totals("off");
    return;
  }
  g_kbps = 0;
  g_stopped_ms = millis();
  print_totals("off");
}

void handle_line(char* line) {
  char* argv[4] = {};
  int argc = 0;
  for (char* tok = strtok(line, " \t"); tok && argc < 4; tok = strtok(nullptr, " \t")) {
    argv[argc++] = tok;
  }
  if (argc == 0 || strcmp(argv[0], "blast") != 0) {
    if (argc > 0) {
      Serial.printf("blast: unknown command '%s'\n", argv[0]);
    }
    return;
  }
  if (argc == 1) {
    print_totals(g_kbps ? "on" : "off");
    return;
  }
  const uint32_t kbps = strtoul(argv[1], nullptr, 10);
  if (kbps == 0) {
    stop();
    return;
  }
  const size_t bytes = argc >= 3 ? strtoul(argv[2], nullptr, 10) : kMaxPayload;
  start(kbps, bytes);
}

void read_console() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      g_line[g_line_len] = '\0';
      handle_line(g_line);
      g_line_len = 0;
      continue;
    }
    // An overlong line is truncated rather than wrapped into a second command.
    if (g_line_len < sizeof(g_line) - 1) {
      g_line[g_line_len++] = static_cast<char>(c);
    }
  }
}

void pace() {
  if (g_kbps == 0) {
    return;
  }
  const uint32_t now = micros();
  const uint32_t elapsed_us = now - g_last_us;
  g_last_us = now;

  // kbps * 1000 / 8 bytes per second, so kbps / 8 bytes per millisecond.
  const uint64_t earned = static_cast<uint64_t>(g_kbps) * elapsed_us / 8000;
  const uint64_t ceiling = static_cast<uint64_t>(g_kbps) * kMaxBankMs / 8 + g_bytes_per_packet;
  uint64_t credit = g_credit + earned;
  if (credit > ceiling) {
    credit = ceiling;
  }

  // A link that is down is not a load. Counted apart from `fail`, so a run that lost its
  // association reads as that and not as a stack out of buffers.
  if (WiFi.status() != WL_CONNECTED) {
    ++g_down;
    g_credit = 0;
    return;
  }

  for (int i = 0; i < kMaxSendsPerPoll && credit >= g_bytes_per_packet; ++i) {
    credit -= g_bytes_per_packet;
    const int n = lwip_sendto(g_sock, g_payload, g_bytes_per_packet, MSG_DONTWAIT,
                              reinterpret_cast<const sockaddr*>(&g_dest), sizeof(g_dest));
    if (n != static_cast<int>(g_bytes_per_packet)) {
      // Usually ENOMEM: the stack is out of buffers. Give the tick back rather than
      // spin, and let the next poll try again.
      ++g_fail;
      g_last_err = errno;
      break;
    }
    ++g_sent;
    g_bytes += g_bytes_per_packet;
  }
  g_credit = static_cast<uint32_t>(credit);
}

}  // namespace

void blaster_begin(const char* host) {
  g_host = host;
  Serial.println(F("*** V-B12 BENCH IMAGE - `blast <kbps> [bytes]` loads WiFi. Never deploy ***"));
}

void blaster_poll() {
  read_console();
  pace();
}

}  // namespace bridge

#endif  // LRAN_V_B12_BLASTER
