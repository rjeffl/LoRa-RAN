// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Why service_receive ran, and what the IRQ register made of it. The receive path's
// 1 s knee (engineering log, 2026-09-17); root rule 4.
//
// ARDUINO-FREE AND HEADER-ONLY, for the reason queues.h gives: this is accounting, and
// accounting is where a silent loss becomes visible or stays invisible. lora_link.cpp
// cannot be built at a desk - it reaches RadioLib and the ESP32 - so the two decisions
// worth getting right are lifted out of it and tested in test_lora. They are pure
// functions of their arguments and hold no state.
//
// WHY THIS EXISTS AT ALL. DIO1 is a LEVEL output from the SX1262: it stays asserted for
// as long as a masked interrupt is set, and drops only when clearIrqStatus() runs.
// RadioLib attaches the handler on the RISING edge (SX126x_config.cpp, 7.7.1:
// GpioInterruptRising). So a second RX_DONE raised while the first is still pending
// produces NO EDGE - the line never went low - and the ISR never runs for it. The frame
// is in the radio's buffer with nothing to announce it.
//
// lora_link's timed read (kIrqReadMs) is what recovers that frame. That makes the timed
// read the belt-and-braces its comment calls it, and it also makes the recovery interval
// the thing that decides whether a frame arriving too soon after another survives. The
// two counters below separate those cases, so the question is read at the broker rather
// than inferred from a loss curve.
//
// ONE THING THE TIMED READ IS NOT OPTIONAL FOR. The DIO1 mask is RX_DONE alone
// (RADIOLIB_IRQ_RX_DEFAULT_MASK), so HEADER_ERR - spec 14 stage 1's header half - can
// only ever be found by the timed read. A "fix" that removes the timed read stops
// counting those discards entirely, which root rule 4 forbids.

#pragma once

#include <cstdint>

namespace bridge {

// Why service_receive is touching the radio this pass.
enum class RxWake : uint8_t {
  Skip,       // neither trigger fired; leave the radio alone
  Interrupt,  // the ISR recorded a DIO1 edge
  Timed,      // no edge, and the read interval has elapsed
};

// Unsigned subtraction, correct across a millis() wrap - the same arithmetic
// lora_link.cpp's elapsed() does, restated here so this header needs nothing from it.
constexpr RxWake rx_wake(bool dio1, uint32_t now_ms, uint32_t last_read_ms,
                         uint32_t irq_read_ms) {
  return dio1 ? RxWake::Interrupt
              : ((now_ms - last_read_ms) >= irq_read_ms ? RxWake::Timed : RxWake::Skip);
}

// What the register held, read together with why the pass happened.
enum class RxPass : uint8_t {
  Nothing,      // a timed read over an empty register: the normal quiet case
  Packet,       // RX_DONE, announced by its own interrupt
  Orphan,       // RX_DONE found by the timed read - NO INTERRUPT EVER ARRIVED FOR IT
  HeaderError,  // spec 14 stage 1, the header half
  WakeEmpty,    // an edge arrived and the register held neither
};

// `rx_done` and `header_err` are the two IRQ bits, already masked out by the caller so
// that RadioLib's names stay in the one file that includes RadioLib.
//
// RX_DONE WINS OVER HEADER_ERR. readData() reports a header error on a completed
// reception through its own CRC-mismatch status (SX126x.cpp), which is where
// rx_ladder's stage 1 already handles it; the HeaderError case here is the reception
// that never completed and so raised no RX_DONE at all.
//
// A HEADER ERROR IS NEVER AN ORPHAN. HEADER_ERR is not in the DIO1 mask, so the timed
// read is the only thing that can find one - by design, not by a missed edge. Counting
// it as a missed interrupt would bury the signal this whole seam exists to produce.
constexpr RxPass rx_pass(RxWake wake, bool rx_done, bool header_err) {
  return wake == RxWake::Skip          ? RxPass::Nothing
         : rx_done                     ? (wake == RxWake::Timed ? RxPass::Orphan
                                                                : RxPass::Packet)
         : header_err                  ? RxPass::HeaderError
         : wake == RxWake::Interrupt   ? RxPass::WakeEmpty
                                       : RxPass::Nothing;
}

}  // namespace bridge
