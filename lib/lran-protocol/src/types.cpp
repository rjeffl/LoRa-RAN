// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "lran/types.h"

namespace lran {

const char* to_string(Status s) {
  switch (s) {
    case Status::Ok:                return "Ok";
    case Status::Runt:              return "Runt";
    case Status::Oversize:          return "Oversize";
    case Status::BadCrc:            return "BadCrc";
    case Status::BadVersion:        return "BadVersion";
    case Status::NotAddressed:      return "NotAddressed";
    case Status::UnknownHdrExt:     return "UnknownHdrExt";
    case Status::BadFrag:           return "BadFrag";
    case Status::UnknownType:       return "UnknownType";
    case Status::UnknownSchema:     return "UnknownSchema";
    case Status::BadLength:         return "BadLength";
    case Status::ReassemblyTimeout: return "ReassemblyTimeout";
    case Status::ReassemblyAbandoned: return "ReassemblyAbandoned";
    case Status::FragLate:          return "FragLate";
    case Status::NotFragmentable:   return "NotFragmentable";
    case Status::FragmentOverflow:  return "FragmentOverflow";
    case Status::RejectedMac:       return "RejectedMac";
    case Status::RejectedCtx:       return "RejectedCtx";
    case Status::BufferTooSmall:    return "BufferTooSmall";
    case Status::MissingMac:        return "MissingMac";
    case Status::NotImplemented:    return "NotImplemented";
  }
  return "?";  // unreachable for a valid Status; keeps -Wreturn-type quiet
}

}  // namespace lran
