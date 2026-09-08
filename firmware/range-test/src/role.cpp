// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "role.h"

namespace rangetest {

const char* to_string(Role r) {
  switch (r) {
    case Role::Initiator: return "INITIATOR";
    case Role::Responder: return "RESPONDER";
    case Role::Survey:    return "SURVEY";
    case Role::W9Initiator: return "W9-INIT";
    case Role::W9Responder: return "W9-RESP";
  }
  return "?";
}

}  // namespace rangetest
