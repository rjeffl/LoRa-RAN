// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register

#include "lran/wire.h"

namespace lran {

bool type_is_known(uint8_t type) {
  switch (type) {
    case static_cast<uint8_t>(MsgType::Command):
    case static_cast<uint8_t>(MsgType::CommandAck):
    case static_cast<uint8_t>(MsgType::Poll):
    case static_cast<uint8_t>(MsgType::Status):
    case static_cast<uint8_t>(MsgType::Event):
    case static_cast<uint8_t>(MsgType::Error):
    case static_cast<uint8_t>(MsgType::Ping):
    case static_cast<uint8_t>(MsgType::HexReq):
    case static_cast<uint8_t>(MsgType::HexRsp):
    case static_cast<uint8_t>(MsgType::Config):
    case static_cast<uint8_t>(MsgType::ConfigAck):
      return true;
    default:
      // spec 6 - 0x00 and 0x0C..0xFF are reserved. spec 13.2: adding a type needs no
      // `ver` bump precisely because an older receiver lands here and discards
      // loudly with ERROR(UNKNOWN_TYPE).
      return false;
  }
}

bool type_carries_schema(MsgType type) {
  switch (type) {
    case MsgType::Status:
    case MsgType::Event:
    case MsgType::Config:
    case MsgType::ConfigAck:
      return true;
    case MsgType::Command:
    case MsgType::CommandAck:
    case MsgType::Poll:
    case MsgType::Error:
    case MsgType::Ping:
    case MsgType::HexReq:
    case MsgType::HexRsp:
      return false;
  }
  return false;
}

bool schema_is_known(MsgType type, SchemaId schema) {
  if (!type_carries_schema(type)) {
    // spec 5.7 - written 0x00 and ignored. Not validated as zero: spec 4.3 forbids
    // that, and a receiver that rejected a non-zero schema here would break the
    // forward compatibility the ignore rule exists to provide.
    return true;
  }
  switch (type) {
    case MsgType::Status:
      return schema == kSchemaGateLinkStatusV1 || schema == kSchemaNodeHealthV1 ||
             schema == kSchemaSimnodeStatusV1;
    case MsgType::Event:
      return schema == kSchemaGateLinkEventV1;
    case MsgType::Config:
    case MsgType::ConfigAck:
      return schema == kSchemaGateLinkConfigV1;
    default:
      return false;
  }
}

size_t fixed_payload_len(MsgType type, SchemaId schema) {
  switch (type) {
    case MsgType::Command:    return 4;  // spec 6.2
    case MsgType::CommandAck: return 6;  // spec 6.3
    case MsgType::Poll:       return 1;  // spec 6.4
    case MsgType::Error:      return 4;  // spec 6.5

    // spec 4.4 - explicitly variable, carrying their own length fields.
    case MsgType::Ping:
    case MsgType::HexReq:
    case MsgType::HexRsp:
    case MsgType::Config:
    case MsgType::ConfigAck:
      return kVariableLen;

    case MsgType::Status:
    case MsgType::Event:
      switch (schema) {
        case kSchemaGateLinkStatusV1: return 78;  // spec 7.2
        case kSchemaSimnodeStatusV1:  return 78;  // spec 7.1 - mirrors 0x10
        case kSchemaGateLinkEventV1:  return 16;  // spec 7.3
        case kSchemaNodeHealthV1:     return 20;  // spec 7.5
        default:                      return kVariableLen;
      }
  }
  return kVariableLen;
}

bool type_requires_mac(MsgType type) {
  switch (type) {
    case MsgType::Command:  return true;  // spec 9.2 - moves the gate
    case MsgType::Config:   return true;  // spec 9.2 - a command by any other name
    case MsgType::CommandAck:
    case MsgType::Poll:
    case MsgType::Status:
    case MsgType::Event:
    case MsgType::Error:
    case MsgType::Ping:
    case MsgType::HexReq:   // conditional - hex_req_is_write_class
    case MsgType::HexRsp:
    case MsgType::ConfigAck:
      return false;
  }
  return false;
}

bool hex_req_is_write_class(const uint8_t* payload, size_t payload_len) {
  // [flags][n][hex:n]; the nibble is the character after the leading ':'.
  //
  // A malformed request - no colon, no nibble, a truncated string - returns false
  // and so needs no MAC. That is safe rather than fail-open: the node answers
  // HEX_RSP(MALFORMED_REQUEST) (spec 8.13) and never forwards it to the MPPT, so
  // there is no write to authenticate. Only a well-formed Set or Restart reaches the
  // UART, and only that shape returns true.
  if (payload == nullptr || payload_len < 4) return false;
  const uint8_t n = payload[1];
  if (n < 2 || payload_len < static_cast<size_t>(2) + n) return false;
  if (payload[2] != ':') return false;

  const uint8_t c = payload[3];
  uint8_t nibble;
  if (c >= '0' && c <= '9')      nibble = static_cast<uint8_t>(c - '0');
  else if (c >= 'A' && c <= 'F') nibble = static_cast<uint8_t>(c - 'A' + 10);
  else if (c >= 'a' && c <= 'f') nibble = static_cast<uint8_t>(c - 'a' + 10);
  else return false;

  return nibble == 0x8 || nibble == 0x6;  // spec 7.6 - Set, Restart
}

}  // namespace lran
