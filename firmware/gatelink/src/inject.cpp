// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "inject.h"

#include <cstdlib>
#include <cstring>

#include "gate_state.h"

namespace gatelink {

namespace {

uint8_t input_bit(char c) {
  switch (c) {
    case 'O': return kInOpen;
    case 'M': return kInMoving;
    case 'S': return kInSafety;
    case 'E': return kInExit;
    case 'F': return kInFire;
    case 'A': return kInAlarm;
    default:  return 0;
  }
}

struct Canned {
  const char* name;
  const char* script;
};

// Times are the PRD's: a departing vehicle waits 10-30 s at the wand for the gate (PRD
// 3.2.2 property 1), and a vehicle parked on the loops outlasts detect_sequence_window_ms.
// The gate scripts take about 15 s of travel.
constexpr Canned kCanned[] = {
    {"exit", "+E @30000 +S @2000 -E @4000 -S"},      // 30 s gap, then EXIT
    {"entry", "+S @3000 +E @1000 -S @1000 -E"},      // SAFETY, then EXIT
    {"both", "+S,+E @2000 -S,-E"},                   // one read: UNDETERMINED
    {"wand-back", "+E @5000 -E"},                    // partial: resolves after the idle time
    {"loop-stop", "+S @70000 -S"},                   // partial: resolves at the window
    {"cycle", "+M @15000 +O @20000 -O @15000 -M"},   // momentary open, countdown, close
    {"hold", "+M @15000 +O,-M"},                     // opens to a hold
    {"release", "+M @2000 -O @15000 -M"},            // a held gate counts down and closes
    {"closed", "-O,-M,-S,-E"},                       // a closed, idle gate
};

}  // namespace

bool InjectScript::parse(const char* text, const char** bad) {
  n_       = 0;
  next_    = 0;
  running_ = false;
  Step     cur;
  bool     have    = false;
  bool     timed   = false;
  uint32_t wait_ms = 0;
  const char* p = text;
  while (*p != '\0') {
    while (*p == ' ') ++p;
    if (*p == '\0') break;
    const char* tok = p;
    if (*p == '@') {
      char*               end = nullptr;
      const unsigned long ms  = std::strtoul(p + 1, &end, 10);
      if (end == p + 1 || (*end != ' ' && *end != '\0')) {
        *bad = tok;
        n_   = 0;
        return false;
      }
      timed   = true;
      wait_ms = static_cast<uint32_t>(ms);
      p       = end;
      continue;
    }
    // One step: changes joined by commas.
    cur = Step{};
    for (;;) {
      const uint8_t bit = input_bit(p[1]);
      if ((p[0] != '+' && p[0] != '-') || bit == 0 || n_ == kMaxSteps) {
        *bad = tok;
        n_   = 0;
        return false;
      }
      (p[0] == '+' ? cur.set : cur.clear) |= bit;
      p += 2;
      if (*p != ',') break;
      ++p;
    }
    if (*p != ' ' && *p != '\0') {
      *bad = tok;
      n_   = 0;
      return false;
    }
    cur.timed     = timed;
    cur.wait_ms   = wait_ms;
    steps_[n_++]  = cur;
    have          = true;
    timed         = false;
    wait_ms       = 0;
  }
  if (!have) *bad = text;
  return have;
}

void InjectScript::start(uint32_t now_ms) {
  next_    = 0;
  running_ = n_ != 0;
  last_ms_ = now_ms;
}

bool InjectScript::service(uint32_t now_ms, uint32_t spacing_ms, uint8_t* mask, uint8_t* value) {
  while (running_) {
    const Step&    s    = steps_[next_];
    const uint32_t wait = next_ == 0 ? 0 : (s.timed ? s.wait_ms : spacing_ms);
    if (now_ms - last_ms_ < wait) break;
    *mask |= s.set | s.clear;
    *value = static_cast<uint8_t>((*value | s.set) & ~s.clear);
    last_ms_ += wait;
    if (++next_ == n_) running_ = false;
  }
  return running_;
}

const char* inject_canned(const char* name) {
  for (const Canned& c : kCanned) {
    if (std::strcmp(c.name, name) == 0) return c.script;
  }
  return nullptr;
}

const char* inject_canned_names() {
  return "exit entry both wand-back loop-stop cycle hold release closed";
}

}  // namespace gatelink
