// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "role.h"

namespace rangetest {

const char* to_string(Role r) {
  switch (r) {
    case Role::Initiator: return "INITIATOR";
    case Role::Responder: return "RESPONDER";
    case Role::Survey:    return "SURVEY";
  }
  return "?";
}

}  // namespace rangetest
