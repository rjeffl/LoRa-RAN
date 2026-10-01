// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink task L1; see names.h.

#include "lran/node/names.h"
#include "lran/node/sink.h"

#include <cstring>

namespace lran::node {
namespace {

struct ReasonName {
  const char*        name;
  StatusReason value;
};
constexpr ReasonName kReasons[] = {
    {"POLL_RESPONSE", StatusReason::PollResponse},
    {"GATE_STATE_CHANGE", StatusReason::GateStateChange},
    {"HOLD_STATE_CHANGE", StatusReason::HoldStateChange},
    {"MPPT_ERROR", StatusReason::MpptError},
    {"VEHICLE_DETECTED", StatusReason::VehicleDetected},
    {"BMS_ALARM", StatusReason::BmsAlarm},
    {"HARD_SHUTDOWN", StatusReason::HardShutdown},
    {"FIRE", StatusReason::Fire},
    {"CONFIG_CHANGE", StatusReason::ConfigChange},
    {"BOOT", StatusReason::Boot},
    {"CHARGE_INHIBITED", StatusReason::ChargeInhibited},
    {"DEBUG_SYNTHETIC", StatusReason::DebugSynthetic},
};

struct EventName {
  const char*     name;
  EventType value;
};
constexpr EventName kEvents[] = {
    {"VEHICLE_WHILE_HELD_OPEN", EventType::VehicleWhileHeldOpen},
    {"FIRE_ASSERTED", EventType::FireAsserted},
    {"HARD_SHUTDOWN", EventType::HardShutdown},
    {"VEHICLE_DETECTED", EventType::VehicleDetected},
    {"GATE_STATE_CHANGE", EventType::GateStateChange},
    {"HOLD_STATE_CHANGE", EventType::HoldStateChange},
    {"BMS_ALARM", EventType::BmsAlarm},
    {"MPPT_ERROR", EventType::MpptError},
    {"CHARGE_INHIBITED", EventType::ChargeInhibited},
    {"BOOT", EventType::Boot},
    {"PHY_REVERTED", EventType::PhyReverted},
};

struct ResetCauseName {
  const char*      name;
  ResetCause value;
};
constexpr ResetCauseName kResetCauses[] = {
    {"UNKNOWN", ResetCause::Unknown},
    {"POWER_ON", ResetCause::PowerOn},
    {"REBOOT_COMMAND", ResetCause::RebootCommand},
    {"SOFTWARE", ResetCause::Software},
    {"WATCHDOG", ResetCause::Watchdog},
    {"PANIC", ResetCause::Panic},
    {"BROWNOUT", ResetCause::Brownout},
    {"EXTERNAL", ResetCause::External},
};

}  // namespace

const char* status_reason_name(StatusReason r) {
  for (const ReasonName& n : kReasons) {
    if (n.value == r) return n.name;
  }
  return "?";
}

bool parse_status_reason(const char* token, StatusReason* out) {
  for (const ReasonName& n : kReasons) {
    if (std::strcmp(token, n.name) == 0) {
      *out = n.value;
      return true;
    }
  }
  return false;
}

const char* event_type_name(EventType t) {
  for (const EventName& n : kEvents) {
    if (n.value == t) return n.name;
  }
  return "?";
}

bool parse_event_type(const char* token, EventType* out) {
  for (const EventName& n : kEvents) {
    if (std::strcmp(token, n.name) == 0) {
      *out = n.value;
      return true;
    }
  }
  return false;
}

const char* reset_cause_name(ResetCause c) {
  for (const ResetCauseName& n : kResetCauses) {
    if (n.value == c) return n.name;
  }
  return "?";
}

bool parse_reset_cause(const char* token, ResetCause* out) {
  for (const ResetCauseName& n : kResetCauses) {
    if (std::strcmp(token, n.name) == 0) {
      *out = n.value;
      return true;
    }
  }
  return false;
}

const char* ack_result_name(AckResult r) {
  switch (r) {
    case AckResult::Accepted:             return "ACCEPTED";
    case AckResult::RejectedMac:          return "REJECTED_MAC";
    case AckResult::RejectedSeq:          return "REJECTED_SEQ";
    case AckResult::RejectedCtx:          return "REJECTED_CTX";
    case AckResult::RejectedUnknownCmd:   return "REJECTED_UNKNOWN_CMD";
    case AckResult::RejectedArg:          return "REJECTED_ARG";
    case AckResult::RejectedNotSupported: return "REJECTED_NOT_SUPPORTED";
    case AckResult::DuplicateCached:      return "DUPLICATE_CACHED";
    case AckResult::DryRun:               return "DRY_RUN";
    case AckResult::ActuatorBusy:         return "ACTUATOR_BUSY";
    case AckResult::RejectedUnsafe:       return "REJECTED_UNSAFE";
  }
  return "?";
}

const char* cmd_name(uint8_t cmd) {
  switch (static_cast<Cmd>(cmd)) {
    case Cmd::Nop:            return "NOP";
    case Cmd::Open:           return "OPEN";
    case Cmd::Close:          return "CLOSE";
    case Cmd::HoldOpen:       return "HOLD_OPEN";
    case Cmd::ReleaseHold:    return "RELEASE_HOLD";
    case Cmd::RequestStatus:  return "REQUEST_STATUS";
    case Cmd::RequestConfig:  return "REQUEST_CONFIG";
    case Cmd::RollContext:    return "ROLL_CONTEXT";
    case Cmd::SetDebugMode:   return "SET_DEBUG_MODE";
    case Cmd::SetRelayDryRun: return "SET_RELAY_DRY_RUN";
    case Cmd::SetBmsPolling:  return "SET_BMS_POLLING";
    case Cmd::Reboot:         return "REBOOT";
  }
  return "?";
}

const char* log_level_name(LogLevel l) {
  switch (l) {
    case LogLevel::Quiet: return "quiet";
    case LogLevel::Info:  return "info";
    case LogLevel::Debug: return "debug";
  }
  return "?";
}

bool parse_log_level(const char* token, LogLevel* out) {
  const LogLevel all[] = {LogLevel::Quiet, LogLevel::Info, LogLevel::Debug};
  for (LogLevel l : all) {
    if (std::strcmp(token, log_level_name(l)) == 0) {
      *out = l;
      return true;
    }
  }
  return false;
}

}  // namespace lran::node
