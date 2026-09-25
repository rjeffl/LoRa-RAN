// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The three `config/*` documents, read and written. Task BF-32; spec 16.7; D43, D48.
//
// ARDUINO-FREE AND HOST-TESTED, like diag_json.cpp and discovery.cpp beside it. What
// Home Assistant sends and what it reads back is exactly the kind of thing to check at
// a desk rather than at the broker, and one of the three documents here is retained -
// a malformed retained document survives every restart until something overwrites it.
//
// THE PARSER IS HAND-WRITTEN AND STRICT, for the same reason json_writer.h is. There is
// no JSON library in this firmware, and adding one to read an object of at most sixteen
// integer values would be the larger change. It accepts spec 16.7.2's shape and refuses
// everything else with a reason rather than guessing.
//
// TWO KINDS OF REFUSAL, AND SPEC 16.7 SEPARATES THEM DELIBERATELY. A payload that is not
// an object carrying exactly one of `set` and `op` is refused WHOLE: `persist` is
// `not_applied`, there are no results, and an `error` string says why. A value inside a
// well-formed `set` that is not an integer or a bool is refused PER ENTRY, as
// `type_mismatch`, and every other entry in the same set still applies. Collapsing the
// second into the first would let one mistyped value silently discard a whole set.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/types.h"

namespace bridge {

// The longest name in the table today is `frag_reassembly_timeout_ms` and
// `config_readback_timeout_ms`, both 26. The cap is the table's business, not the
// wire's, so it is generous rather than tight.
inline constexpr size_t kMaxParamNameLen = 40;

// A `set` naming more parameters than this is refused whole, with a reason.
//
// EIGHT IS NOT A ROUND NUMBER - IT IS WHAT THE ANSWER CAN CARRY. Every name in a `set`
// gets a result in the one `config/ack` that answers it, and that document is capped at
// kMaxPayloadLen like every other publication. The worst case is a set of names the
// table does not hold: each costs its own length plus about 43 bytes of
// `{"status":"unknown_param","value":null}`, and at the longest name this parser accepts
// that is ~82 bytes a row against ~700 bytes of room. Nine rows would not fit, and a
// document that does not fit is DROPPED - the operator would see nothing at all rather
// than an answer.
//
// A Home Assistant `number` entity sends one name, so this bound is far from the way of
// anything the fleet does. test_config.cpp builds the largest document each table can
// produce and fails here rather than at the broker.
inline constexpr size_t kMaxConfigSetEntries = 8;

// One parameter a `config/set` named.
struct ConfigSetEntry {
  char name[kMaxParamNameLen] = {0};
  // False when the JSON value was neither an integer nor a bool. The entry is kept so
  // it can be answered `type_mismatch` by name (spec 16.7.2) rather than disappearing.
  bool    value_readable = false;
  int32_t value          = 0;
};

struct ConfigSetRequest {
  lran::ConfigOp op    = lran::ConfigOp::Set;
  uint8_t        count = 0;
  ConfigSetEntry entries[kMaxConfigSetEntries] = {};
};

// Spec 16.7.2's payload. On false, `*error` points at a static string fit to publish as
// `config/ack`'s `error`, and nothing is applied.
bool parse_config_set(const char* payload, size_t len, ConfigSetRequest* out,
                      const char** error);

// ---------------------------------------------------------------------------
// What the bridge answers with.
// ---------------------------------------------------------------------------

// Spec 8.12's five, plus the one outcome spec 16.7.3 adds and 8.12 has no room for:
// a `CONFIG` that drew no `CONFIG_ACK` inside its timeout. Spec 7.4 requires that be
// treated as neither success nor failure, so it cannot be folded into `not_applied`.
enum class ResultStatus : uint8_t {
  Ok,
  UnknownParam,
  Clamped,
  TypeMismatch,
  ReadOnly,
  InvalidValue,  // D64
  Unknown,
  // spec 16.7.3 - a PHY change the bridge abandoned or reverted. The PHY group's alone,
  // and like `unknown` it has no spec 8.12 counterpart.
  Reverted,
};

ResultStatus result_status_of(lran::ParamStatus s);
const char*  result_status_name(ResultStatus s);

// Spec 8.11's three, plus 16.7.3's `unknown` for the same reason.
enum class AckPersist : uint8_t {
  Persisted,
  AppliedNotPersisted,
  NotApplied,
  Unknown,
};

AckPersist  ack_persist_of(lran::PersistStatus s);
const char* ack_persist_name(AckPersist p);

// Spec 16.7.3's combination rule for a set with two halves: `unknown` if either half is
// unknown, otherwise the LESS persisted of the two, ordered `not_applied`,
// `applied_not_persisted`, `persisted`. Written once here because getting it wrong
// reports a value as saved when half of it was not.
AckPersist combine_persist(AckPersist a, AckPersist b);

// True when the bridge's half of a `config/set` changed a stored value, which is what
// earns a lever publish and a `config/state` republish (spec 16.7.4). `persist` alone
// cannot say so: a GET_ALL reports the store's persist status, `persisted` on a healthy
// board, and changes nothing. Counting it republished an unchanged retained document on
// the bench, 2026-09-23.
bool config_set_changed(lran::ConfigOp op, AckPersist persist);

const char* config_op_name(lran::ConfigOp op);

struct ConfigResult {
  char         name[kMaxParamNameLen] = {0};
  ResultStatus status                 = ResultStatus::Ok;
  // Spec 16.7.3 - `value` is the EFFECTIVE value, and null where nothing was applied or
  // the outcome is not known.
  bool    has_value = false;
  int32_t value     = 0;
};

// `config/ack`, spec 16.7.3. `error` is written only when the payload was refused whole,
// and `results` is then empty. Returns the length written, or 0 when the document did
// not fit - the caller drops the publication rather than sending half an object.
size_t build_config_ack(lran::ConfigOp op, AckPersist persist, const ConfigResult* results,
                        size_t n, const char* error, char* out, size_t cap);

struct ConfigStateEntry {
  char name[kMaxParamNameLen] = {0};
  // Spec 16.7.4 - a value the bridge has never read back is null, which is a different
  // statement from a value that equals its default.
  bool    has_value   = false;
  int32_t value       = 0;
  bool    is_override = false;
};

// `config/state`, spec 16.7.4, retained. Returns the length written, or 0.
size_t build_config_state(const ConfigStateEntry* entries, size_t n, char* out, size_t cap);

}  // namespace bridge
