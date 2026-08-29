// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "lran/counters.h"

namespace lran {

// spec 14.1, in the specification's order. The third column is `in_dropped`.
const CounterField kCounterRegistry[kCounterRegistryLen] = {
    {"rx_crc_err",              &Counters::rx_crc_err,              true},
    {"rx_runt",                 &Counters::rx_runt,                 true},
    {"rx_oversize",             &Counters::rx_oversize,             true},
    {"rx_bad_crc",              &Counters::rx_bad_crc,              true},
    {"rx_bad_ver",              &Counters::rx_bad_ver,              true},
    {"rx_not_addressed",        &Counters::rx_not_addressed,        true},
    {"rx_unknown_hdr_ext",      &Counters::rx_unknown_hdr_ext,      true},
    {"rx_bad_frag",             &Counters::rx_bad_frag,             true},
    {"rx_unknown_type",         &Counters::rx_unknown_type,         true},
    {"rx_unknown_schema",       &Counters::rx_unknown_schema,       true},
    {"rx_bad_length",           &Counters::rx_bad_length,           true},
    {"rx_not_fragmentable",     &Counters::rx_not_fragmentable,     true},
    {"rx_rejected_ctx",         &Counters::rx_rejected_ctx,         true},
    {"rx_rejected_mac",         &Counters::rx_rejected_mac,         true},
    {"rx_reassembly_timeout",   &Counters::rx_reassembly_timeout,   true},
    {"rx_fragment_overflow",    &Counters::rx_fragment_overflow,    true},
    {"rx_reassembly_abandoned", &Counters::rx_reassembly_abandoned, true},
    {"rx_rejected_seq",         &Counters::rx_rejected_seq,         true},

    // Counted, deliberately NOT summed into rx_dropped (spec 14.1).
    {"rx_frag_duplicate",       &Counters::rx_frag_duplicate,       false},
    {"rx_frag_late",            &Counters::rx_frag_late,            false},
    {"rx_dup_command",          &Counters::rx_dup_command,          false},
};

// No `default:` label. Adding a Status enumerator must be a -Werror=switch failure
// here, so a new discard reason cannot ship without a counter to report it under.
//
// Not every counter has a Status: rx_frag_duplicate and rx_frag_late are raised by
// the Reassembler directly (they are not discards the codec returns), and
// rx_rejected_seq / rx_dup_command belong to stage 11, outside this library (W12).
void Counters::bump(Status s) {
  switch (s) {
    case Status::Ok:                  return;
    case Status::Runt:                ++rx_runt; return;
    case Status::Oversize:            ++rx_oversize; return;
    case Status::BadCrc:              ++rx_bad_crc; return;
    case Status::BadVersion:          ++rx_bad_ver; return;
    case Status::NotAddressed:        ++rx_not_addressed; return;
    case Status::UnknownHdrExt:       ++rx_unknown_hdr_ext; return;
    case Status::BadFrag:             ++rx_bad_frag; return;
    case Status::UnknownType:         ++rx_unknown_type; return;
    case Status::UnknownSchema:       ++rx_unknown_schema; return;
    case Status::BadLength:           ++rx_bad_length; return;
    case Status::NotFragmentable:     ++rx_not_fragmentable; return;
    case Status::ReassemblyTimeout:   ++rx_reassembly_timeout; return;
    case Status::ReassemblyAbandoned: ++rx_reassembly_abandoned; return;
    case Status::FragmentOverflow:    ++rx_fragment_overflow; return;
    case Status::FragLate:            ++rx_frag_late; return;

    // spec 14 stage 9. The counter names follow spec 14.1 and the COMMAND_ACK results
    // of spec 8.2; the Status names predate both and are the library's own.
    case Status::BadMac:              ++rx_rejected_mac; return;
    case Status::CtxMismatch:         ++rx_rejected_ctx; return;

    // Caller errors, not wire conditions. Nothing on the link caused them and no
    // spec 14 stage owns them, so they are not counted as drops.
    case Status::BufferTooSmall:      return;
    case Status::MissingMac:          return;
    case Status::NotImplemented:      return;
  }
}

uint32_t Counters::total_dropped() const {
  // spec 14.1 - driven by the registry rather than a hand-written sum, so a counter
  // added to the struct and the registry is included automatically and one marked
  // `no` cannot creep in. The three excluded are normal traffic (spec 14.1).
  uint32_t sum = 0;
  for (size_t i = 0; i < kCounterRegistryLen; ++i) {
    if (kCounterRegistry[i].in_dropped) sum += this->*(kCounterRegistry[i].field);
  }
  return sum;
}

void Counters::reset() { *this = Counters{}; }

}  // namespace lran
