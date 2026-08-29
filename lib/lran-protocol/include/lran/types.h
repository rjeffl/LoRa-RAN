// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// Identifiers, status, and every enumeration in spec 8. Wire values only - this
// header assigns no behaviour.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config.h"

namespace lran {

using NodeId   = uint8_t;
using SchemaId = uint8_t;
using Seq      = uint16_t;
using CtxId    = uint32_t;

// spec 5.3
inline constexpr NodeId kNodeBridge    = 0x00;
inline constexpr NodeId kNodeGateLink  = 0x01;
inline constexpr NodeId kNodeWellLink  = 0x02;
inline constexpr NodeId kNodeSim0      = 0xF0;
inline constexpr NodeId kNodeSim1      = 0xF1;
inline constexpr NodeId kNodeSim2      = 0xF2;
inline constexpr NodeId kNodeSim3      = 0xF3;
inline constexpr NodeId kNodeBroadcast = 0xFF;

// spec 5.3 - the bench range. 0xF0..0xF3 are provisioned; 0xF4..0xFE are reserved
// and unassigned, but a receiver still recognizes them as bench so they can never be
// mistaken for a production node (spec 16.6).
constexpr bool is_bench_node(NodeId id) { return id >= 0xF0 && id <= 0xFE; }

// spec 6
enum class MsgType : uint8_t {
  Command    = 0x01,
  CommandAck = 0x02,
  Poll       = 0x03,
  Status     = 0x04,
  Event      = 0x05,
  Error      = 0x06,
  Ping       = 0x07,
  HexReq     = 0x08,
  HexRsp     = 0x09,
  Config     = 0x0A,
  ConfigAck  = 0x0B,
};

// spec 7.1
inline constexpr SchemaId kSchemaNone             = 0x00;
inline constexpr SchemaId kSchemaGateLinkStatusV1 = 0x10;  // 78 B, spec 7.2
inline constexpr SchemaId kSchemaGateLinkEventV1  = 0x11;  // 16 B, spec 7.3
inline constexpr SchemaId kSchemaGateLinkConfigV1 = 0x12;  // variable, spec 7.4
inline constexpr SchemaId kSchemaNodeHealthV1     = 0xF0;  // 20 B, spec 7.5
inline constexpr SchemaId kSchemaSimnodeStatusV1  = 0xFE;  // 78 B, mirrors 0x10

// spec 5.8 - the one header bit that is validated rather than ignored.
inline constexpr uint8_t kHdrFlagCriticalExt = 0x80;

// spec 6.4
inline constexpr uint8_t kPollFlagFullStatus     = 0x01;
inline constexpr uint8_t kPollFlagConfigReadback = 0x02;

// spec 6.6 / 6.6.3
inline constexpr uint8_t kPingFlagPatternFill = 0x01;

// spec 7.6
inline constexpr uint8_t kHexReqFlagWriteClass = 0x01;

// One value per failure. Maps 1:1 onto the spec 14 stages and the counter set, so a
// discard always has a name (repo rule 4). Adding an enumerator makes every switch
// over it a -Werror diagnostic until the new case is handled - including
// Counters::bump and the bridge's diagnostic publication. That is the mechanism that
// keeps "discard silently" from ever being the default.
enum class Status : uint8_t {
  Ok = 0,
  Runt,               // spec 14 stage 2
  Oversize,           // stage 2a - spec 3, longer than kMaxFrame
  BadCrc,             // stage 3
  BadVersion,         // stage 4
  NotAddressed,       // stage 5
  UnknownHdrExt,      // stage 5a - spec 5.8
  BadFrag,            // stage 5b - spec 5.6, a `frag` total of 0
  UnknownType,        // stage 6
  UnknownSchema,      // stage 7
  BadLength,          // stage 8
  ReassemblyTimeout,  // stage 10, spec 11.2 - the set's window expired
  ReassemblyAbandoned,  // spec 11.3 - a new set displaced a live one
  FragmentOverflow,   // stage 10, spec 11.2

  // spec 11.4 / 14 stage 8a - a type v1 rules out was fragmented. Serves BOTH
  // directions: the sender's refusal to emit one (a spec violation the encoder
  // declined to commit) and the receiver's discard of one.
  //
  // The WIRE answer stays ERROR(BAD_LENGTH), which is what spec 11.4 and stage 8a
  // require; the COUNTER is rx_not_fragmentable. The two do not have to agree - the
  // same split spec 5.6 makes for BadFrag - and the counter is the diagnosis. "A peer
  // fragmented a type that may not be fragmented" and "a peer's encoder got a length
  // wrong" have different fixes, and folding them together would repeat exactly what
  // v0.4 split apart in stage 2a and spec 11.3.
  //
  // encode() never touches Counters, so the sender's use of this value costs nothing.
  NotFragmentable,
  BadMac,             // spec 9.4 step 3
  CtxMismatch,        // spec 10.1, checked at spec 9.4 step 2
  BufferTooSmall,     // caller error, not a wire condition

  // spec 9.2 - an authenticated type was encoded with no IMac or no key. A
  // MISCONFIGURATION: the caller wired the library up wrong or shipped without key
  // material. Held apart from NotImplemented because a field log cannot tell a
  // library gap from a build that is about to be unable to command the gate, and
  // this is the more serious of the two and the less visible.
  MissingMac,

  // A genuine gap in this library, not a caller error and not a wire condition.
  NotImplemented,
};

// For logs. The ONLY string-producing function in the library.
const char* to_string(Status s);

// ---------------------------------------------------------------------------
// spec 8 enumerations. Receivers treat an unknown value as UNKNOWN rather than
// discarding the frame (spec 13.2) - these are for constructing and interpreting,
// not for validating.
// ---------------------------------------------------------------------------

// spec 8.1. 0x00..0x0F are actuation commands and the only values that reach a
// physical output; 0x10+ are node-local.
enum class Cmd : uint8_t {
  Nop            = 0x00,
  Open           = 0x01,
  Close          = 0x02,
  HoldOpen       = 0x03,
  ReleaseHold    = 0x04,
  RequestStatus  = 0x10,
  RequestConfig  = 0x11,
  SetDebugMode   = 0x20,
  SetRelayDryRun = 0x21,
  SetBmsPolling  = 0x22,
  Reboot         = 0x7F,
};

// spec 8.1 - REBOOT requires this in `arg` as a confirmation guard.
inline constexpr uint8_t kRebootGuard = 0xA5;

constexpr bool is_actuation_cmd(uint8_t cmd) { return cmd <= 0x0F; }

// spec 8.2
enum class AckResult : uint8_t {
  Accepted             = 0x00,
  RejectedMac          = 0x01,
  RejectedSeq          = 0x02,
  RejectedCtx          = 0x03,
  RejectedUnknownCmd   = 0x04,
  RejectedArg          = 0x05,
  RejectedNotSupported = 0x06,
  DuplicateCached      = 0x07,
  DryRun               = 0x10,
  ActuatorBusy         = 0x11,
  RejectedUnsafe       = 0x12,
};

// spec 8.3
enum class GateState : uint8_t {
  Unknown       = 0x00,
  Closed        = 0x01,
  Moving        = 0x02,
  OpenCountdown = 0x03,
  OpenHeld      = 0x04,
  Fault         = 0x08,
};

// spec 8.4
enum class HoldSource : uint8_t {
  None       = 0x00,
  Lran       = 0x01,
  Manual     = 0x02,
  KeypadFire = 0x03,
  Unknown    = 0x04,
};

// spec 8.5
enum class MovementCause : uint8_t {
  Unknown           = 0x00,
  ExitWand          = 0x01,
  LranCommand       = 0x02,
  KeypadFire        = 0x03,
  ManualHold        = 0x04,
  ExternalMomentary = 0x05,
};

// spec 8.6
enum class Direction : uint8_t {
  None         = 0x00,
  Entry        = 0x01,
  Exit         = 0x02,
  Undetermined = 0x03,
};

// spec 8.7
enum class StatusReason : uint8_t {
  PollResponse    = 0x00,
  GateStateChange = 0x01,
  HoldStateChange = 0x02,
  MpptError       = 0x03,
  VehicleDetected = 0x04,
  BmsAlarm        = 0x05,
  HardShutdown    = 0x06,
  Fire            = 0x07,
  ConfigChange    = 0x08,
  Boot            = 0x09,
  ChargeInhibited = 0x0A,
  DebugSynthetic  = 0xFF,
};

// spec 8.8
enum class ErrCode : uint8_t {
  BadVersion        = 0x01,
  BadLength         = 0x02,
  UnknownType       = 0x03,
  CtxMismatch       = 0x04,
  NotAddressed      = 0x05,
  BadCrc            = 0x06,
  UnknownSchema     = 0x07,
  ReassemblyTimeout = 0x08,
  FragmentOverflow  = 0x09,
  UnknownHdrExt     = 0x0A,
};

// spec 8.9
enum class EventType : uint8_t {
  VehicleWhileHeldOpen = 0x01,
  FireAsserted         = 0x02,
  HardShutdown         = 0x03,
  VehicleDetected      = 0x04,
  GateStateChange      = 0x05,
  HoldStateChange      = 0x06,
  BmsAlarm             = 0x07,
  MpptError            = 0x08,
  ChargeInhibited      = 0x09,
  Boot                 = 0x0A,
};

// spec 8.10
enum class ConfigOp : uint8_t {
  Set             = 0x01,
  Get             = 0x02,
  GetAll          = 0x03,
  RestoreDefaults = 0x04,
};

// spec 8.11
enum class PersistStatus : uint8_t {
  Persisted           = 0x00,
  AppliedNotPersisted = 0x01,
  NotApplied          = 0x02,
};

// spec 8.12
enum class ParamStatus : uint8_t {
  Ok           = 0x00,
  UnknownParam = 0x01,
  Clamped      = 0x02,
  TypeMismatch = 0x03,
  ReadOnly     = 0x04,
};

// spec 8.13
enum class HexStatus : uint8_t {
  Ok                      = 0x00,
  Timeout                 = 0x01,
  RejectedUnauthenticated = 0x02,
  Busy                    = 0x03,
  UartError               = 0x04,
  MalformedRequest        = 0x05,
};

// spec 7.4 - `ptype` on the wire. /lib/lran-config/ describes the same set for the
// parameter table; this is the transport encoding of it.
enum class PType : uint8_t {
  U8   = 0x01,
  U16  = 0x02,
  U32  = 0x03,
  I16  = 0x04,
  I32  = 0x05,
  Bool = 0x06,
};

// spec 4.6 - sentinels, never zero, for "not available". A consumer must be able to
// tell "0 A" from "no reading".
inline constexpr int16_t  kI16NotAvailable = INT16_MIN;
inline constexpr uint16_t kU16NotAvailable = UINT16_MAX;
inline constexpr uint32_t kU32NotAvailable = UINT32_MAX;
inline constexpr uint8_t  kSocNotAvailable = 0xFF;  // spec 7.2.3

}  // namespace lran
