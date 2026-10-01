// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink task L1; see context.h. Moved from the simnode's identity.cpp.

#include "lran/node/context.h"

namespace lran::node {

CtxId new_ctx_id(RandomFn random) {
  for (int i = 0; i < 16; ++i) {
    const CtxId v = random != nullptr ? random() : 0;
    if (v != 0) return v;
  }
  return 1;
}

void reset_context(Context& c, RandomFn random) {
  c.ctx_id = new_ctx_id(random);
  c.gate.reset_context(c.ctx_id);
  c.tx_seq = 1;
  c.reassembler.reset();
  c.reassembler.forget_completed();
  c.rx_chunk    = 0;
  c.pending     = PendingCommand{};
  c.hex_pending = HexPending{};
}

void roll_context(Context& c, RandomFn random) {
  // spec 10.6 node step 2 - DIFFERENT from the current one, not merely random. A roll that
  // drew the same value would leave a replayed ROLL_CONTEXT valid at spec 9.4 step 2, and
  // the replay bound in spec 10.6 rests on the ctx_id changing.
  CtxId next = new_ctx_id(random);
  for (int i = 0; i < 16 && next == c.ctx_id; ++i) next = new_ctx_id(random);
  if (next == c.ctx_id) next = c.ctx_id == 0xFFFFFFFFu ? 1u : c.ctx_id + 1u;
  c.ctx_id = next;
  c.gate.reset_context(next);
  c.tx_seq = 1;
}

}  // namespace lran::node
