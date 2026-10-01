// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The board's PHY trial is lran-node's since GateLink task L1 (lran/node/phy_trial.h).
// These names keep the simnode's code and tests reading as they did.

#pragma once

#include "lran/node/phy_trial.h"

namespace simnode {

using lran::node::BlobStore;
using lran::node::kPhyGroupSize;
using lran::node::phy_config_from;
using lran::node::phy_state_name;
using lran::node::PhyGroup;
using lran::node::PhyPersist;
using lran::node::PhyState;
using lran::node::PhyTrial;
using lran::node::PhyTrialStats;
using lran::node::RevertCause;

}  // namespace simnode
