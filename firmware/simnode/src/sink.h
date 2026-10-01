// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Where the simnode's console output goes. The class is lran-node's since GateLink task L1
// (lran/node/sink.h), because the engine logs through it too.

#pragma once

#include "lran/node/sink.h"

namespace simnode {

using lran::node::Sink;
using lran::node::sink_printf;

}  // namespace simnode
