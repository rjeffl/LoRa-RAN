// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// Task BF-32; spec 16.7.

#include "config_json.h"

#include <cstring>

#include "json_writer.h"

namespace bridge {
namespace {

// ---------------------------------------------------------------------------
// A reader for exactly spec 16.7.2's shape.
//
// WHAT IT DOES NOT ACCEPT, deliberately: nested objects below the first level, arrays,
// escapes inside a key, floating point, exponents, and duplicate top-level keys. Each
// of those is either not in 16.7.2 or has no meaning for a parameter, and a parser that
// silently accepted `1.5` for a u8 would apply 1 and report `ok`.
// ---------------------------------------------------------------------------

class Reader {
 public:
  Reader(const char* p, size_t len) : p_(p), end_(p + len) {}

  void skip_space() {
    while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
  }

  bool done() const { return p_ >= end_; }
  char peek() const { return p_ < end_ ? *p_ : '\0'; }

  bool take(char c) {
    skip_space();
    if (p_ < end_ && *p_ == c) {
      ++p_;
      return true;
    }
    return false;
  }

  // A JSON string with no escapes. `config/set`'s keys are parameter names from the
  // table, which are lower-case ASCII and cannot contain one; a key that does is not a
  // name this bridge holds, so refusing it here and reporting `unknown_param` later
  // would be two answers to one question. It is refused as malformed instead.
  bool string(char* out, size_t cap) {
    skip_space();
    if (p_ >= end_ || *p_ != '"') return false;
    ++p_;
    size_t n = 0;
    while (p_ < end_ && *p_ != '"') {
      if (*p_ == '\\') return false;
      if (n + 1 >= cap) return false;
      out[n++] = *p_++;
    }
    if (p_ >= end_) return false;  // unterminated
    ++p_;
    out[n] = '\0';
    return true;
  }

  // Skips one value of any type, so a `set` entry whose value this bridge cannot use is
  // still countable and answerable by name. Returns false only on malformed input.
  bool skip_value() {
    skip_space();
    if (p_ >= end_) return false;
    const char c = *p_;
    if (c == '"') {
      // Walked rather than stored: a string value is never a value this bridge applies,
      // so its length is not an error here - only its termination is.
      ++p_;
      while (p_ < end_ && *p_ != '"') {
        if (*p_ == '\\') ++p_;
        ++p_;
      }
      if (p_ >= end_) return false;
      ++p_;
      return true;
    }
    if (c == '{' || c == '[') {
      const char close = c == '{' ? '}' : ']';
      int        depth = 0;
      while (p_ < end_) {
        if (*p_ == '"') {
          ++p_;
          while (p_ < end_ && *p_ != '"') {
            if (*p_ == '\\') ++p_;
            ++p_;
          }
          if (p_ >= end_) return false;
        } else if (*p_ == c) {
          ++depth;
        } else if (*p_ == close) {
          if (--depth == 0) {
            ++p_;
            return true;
          }
        }
        ++p_;
      }
      return false;
    }
    while (p_ < end_ && *p_ != ',' && *p_ != '}' && *p_ != ']') ++p_;
    return true;
  }

  // An integer, `true` or `false`. Anything else leaves the cursor at the value's start
  // so the caller can skip it and answer `type_mismatch`.
  bool value(int32_t* out) {
    skip_space();
    const char* start = p_;
    if (literal("true")) {
      *out = 1;
      return true;
    }
    if (literal("false")) {
      *out = 0;
      return true;
    }
    bool negative = false;
    if (p_ < end_ && (*p_ == '-' || *p_ == '+')) {
      negative = *p_ == '-';
      ++p_;
    }
    if (p_ >= end_ || *p_ < '0' || *p_ > '9') {
      p_ = start;
      return false;
    }
    // Accumulated in int64_t so a value past the range is REFUSED rather than wrapped
    // into a plausible small number. `type_mismatch` is the honest answer for it: the
    // table's own clamp is for values inside int32_t that are outside a row's range.
    int64_t v = 0;
    while (p_ < end_ && *p_ >= '0' && *p_ <= '9') {
      v = v * 10 + (*p_ - '0');
      if (v > 4294967296LL) {  // past anything int32_t can hold, either sign
        p_ = start;
        return false;
      }
      ++p_;
    }
    if (p_ < end_ && (*p_ == '.' || *p_ == 'e' || *p_ == 'E')) {
      p_ = start;  // a float is not an integer, and 1.5 must not apply as 1
      return false;
    }
    const int64_t signed_v = negative ? -v : v;
    if (signed_v < INT32_MIN || signed_v > INT32_MAX) {
      p_ = start;
      return false;
    }
    *out = static_cast<int32_t>(signed_v);
    return true;
  }

 private:
  bool literal(const char* word) {
    const size_t n = std::strlen(word);
    if (static_cast<size_t>(end_ - p_) < n) return false;
    if (std::strncmp(p_, word, n) != 0) return false;
    p_ += n;
    return true;
  }

  const char* p_;
  const char* end_;
};

bool equals(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

}  // namespace

bool parse_config_set(const char* payload, size_t len, ConfigSetRequest* out,
                      const char** error) {
  static const char* kNotObject = "payload is not a JSON object";
  static const char* kNoKey     = "object carries neither `set` nor `op`";
  static const char* kBothKeys  = "object carries both `set` and `op`";
  static const char* kBadOp     = "`op` is not `get_all` or `restore_defaults`";
  static const char* kBadSet    = "`set` is not an object of name/value pairs";
  static const char* kTooMany   = "`set` names more parameters than the bridge accepts";
  static const char* kEmptySet  = "`set` names no parameters";
  static const char* kDuplicate = "object carries the same key twice";

  const char* unused = nullptr;
  if (error == nullptr) error = &unused;
  *error = kNotObject;
  if (payload == nullptr || out == nullptr || len == 0) return false;

  *out = ConfigSetRequest{};
  Reader r(payload, len);
  if (!r.take('{')) return false;

  bool have_set = false;
  bool have_op  = false;

  if (!r.take('}')) {
    for (;;) {
      char key[kMaxParamNameLen];
      if (!r.string(key, sizeof(key))) return false;
      if (!r.take(':')) return false;

      if (equals(key, "op")) {
        if (have_op) {
          *error = kDuplicate;
          return false;
        }
        if (have_set) {
          *error = kBothKeys;
          return false;
        }
        have_op = true;
        char word[kMaxParamNameLen];
        if (!r.string(word, sizeof(word))) {
          *error = kBadOp;
          return false;
        }
        if (equals(word, "get_all")) {
          out->op = lran::ConfigOp::GetAll;
        } else if (equals(word, "restore_defaults")) {
          out->op = lran::ConfigOp::RestoreDefaults;
        } else {
          *error = kBadOp;
          return false;
        }
      } else if (equals(key, "set")) {
        if (have_set) {
          *error = kDuplicate;
          return false;
        }
        if (have_op) {
          *error = kBothKeys;
          return false;
        }
        have_set = true;
        out->op  = lran::ConfigOp::Set;
        if (!r.take('{')) {
          *error = kBadSet;
          return false;
        }
        if (!r.take('}')) {
          for (;;) {
            if (out->count >= kMaxConfigSetEntries) {
              *error = kTooMany;
              return false;
            }
            ConfigSetEntry& e = out->entries[out->count];
            if (!r.string(e.name, sizeof(e.name))) {
              *error = kBadSet;
              return false;
            }
            if (!r.take(':')) {
              *error = kBadSet;
              return false;
            }
            // A value this parser cannot read is KEPT, not dropped: spec 16.7.2 answers
            // it `type_mismatch` by name, and an entry that vanished here would be
            // reported `unknown_param` or not at all.
            e.value_readable = r.value(&e.value);
            if (!e.value_readable && !r.skip_value()) {
              *error = kBadSet;
              return false;
            }
            ++out->count;
            if (r.take(',')) continue;
            if (r.take('}')) break;
            *error = kBadSet;
            return false;
          }
        }
        if (out->count == 0) {
          *error = kEmptySet;
          return false;
        }
      } else {
        // A key spec 16.7.2 does not define. Refused whole rather than ignored: a
        // payload carrying `sett` applies nothing and must say so, not succeed silently.
        *error = kNoKey;
        return false;
      }

      if (r.take(',')) continue;
      if (r.take('}')) break;
      *error = kNotObject;
      return false;
    }
  }

  r.skip_space();
  if (!r.done()) {
    *error = kNotObject;  // trailing content after the object
    return false;
  }
  if (have_set == have_op) {
    *error = have_set ? kBothKeys : kNoKey;
    return false;
  }
  *error = nullptr;
  return true;
}

// ---------------------------------------------------------------------------
// The two documents the bridge writes.
// ---------------------------------------------------------------------------

ResultStatus result_status_of(lran::ParamStatus s) {
  switch (s) {
    case lran::ParamStatus::Ok: return ResultStatus::Ok;
    case lran::ParamStatus::UnknownParam: return ResultStatus::UnknownParam;
    case lran::ParamStatus::Clamped: return ResultStatus::Clamped;
    case lran::ParamStatus::TypeMismatch: return ResultStatus::TypeMismatch;
    case lran::ParamStatus::ReadOnly: return ResultStatus::ReadOnly;
    case lran::ParamStatus::InvalidValue: return ResultStatus::InvalidValue;
  }
  return ResultStatus::Unknown;
}

const char* result_status_name(ResultStatus s) {
  switch (s) {
    case ResultStatus::Ok: return "ok";
    case ResultStatus::UnknownParam: return "unknown_param";
    case ResultStatus::Clamped: return "clamped";
    case ResultStatus::TypeMismatch: return "type_mismatch";
    case ResultStatus::ReadOnly: return "read_only";
    case ResultStatus::InvalidValue: return "invalid_value";
    case ResultStatus::Unknown: return "unknown";
    case ResultStatus::Reverted: return "reverted";
  }
  return "unknown";
}

AckPersist ack_persist_of(lran::PersistStatus s) {
  switch (s) {
    case lran::PersistStatus::Persisted: return AckPersist::Persisted;
    case lran::PersistStatus::AppliedNotPersisted: return AckPersist::AppliedNotPersisted;
    case lran::PersistStatus::NotApplied: return AckPersist::NotApplied;
  }
  return AckPersist::Unknown;
}

const char* ack_persist_name(AckPersist p) {
  switch (p) {
    case AckPersist::Persisted: return "persisted";
    case AckPersist::AppliedNotPersisted: return "applied_not_persisted";
    case AckPersist::NotApplied: return "not_applied";
    case AckPersist::Unknown: return "unknown";
  }
  return "unknown";
}

AckPersist combine_persist(AckPersist a, AckPersist b) {
  if (a == AckPersist::Unknown || b == AckPersist::Unknown) return AckPersist::Unknown;
  // spec 16.7.3's order, least persisted first. Written as a rank rather than a chain of
  // comparisons so adding a value cannot leave one pair unordered.
  auto rank = [](AckPersist p) -> int {
    switch (p) {
      case AckPersist::NotApplied: return 0;
      case AckPersist::AppliedNotPersisted: return 1;
      case AckPersist::Persisted: return 2;
      case AckPersist::Unknown: return 3;
    }
    return 3;
  };
  return rank(a) <= rank(b) ? a : b;
}

bool config_set_changed(lran::ConfigOp op, AckPersist persist) {
  switch (op) {
    case lran::ConfigOp::RestoreDefaults: return true;
    case lran::ConfigOp::Get:
    case lran::ConfigOp::GetAll: return false;
    default: break;
  }
  return persist == AckPersist::Persisted || persist == AckPersist::AppliedNotPersisted;
}

const char* config_op_name(lran::ConfigOp op) {
  switch (op) {
    case lran::ConfigOp::Set: return "set";
    case lran::ConfigOp::Get: return "get";
    case lran::ConfigOp::GetAll: return "get_all";
    case lran::ConfigOp::RestoreDefaults: return "restore_defaults";
  }
  return "set";
}

size_t build_config_ack(lran::ConfigOp op, AckPersist persist, const ConfigResult* results,
                        size_t n, const char* error, char* out, size_t cap) {
  JsonObject doc(out, cap);
  doc.str("op", config_op_name(op));
  doc.str("persist", ack_persist_name(persist));

  // Spec 16.7.3 - `error` is present only when the payload was rejected whole, and
  // `results` is then absent rather than empty. An empty object would read as "every
  // parameter was considered and none had an outcome", which is a different claim.
  if (error != nullptr) {
    doc.str("error", error);
    return doc.finish();
  }

  doc.begin_object("results");
  for (size_t i = 0; i < n; ++i) {
    const ConfigResult& r = results[i];
    doc.begin_object(r.name);
    doc.str("status", result_status_name(r.status));
    if (r.has_value) {
      doc.i32("value", r.value);
    } else {
      doc.null("value");
    }
    doc.end_object();
  }
  doc.end_object();
  return doc.finish();
}

size_t build_config_state(const ConfigStateEntry* entries, size_t n, char* out, size_t cap) {
  JsonObject doc(out, cap);
  for (size_t i = 0; i < n; ++i) {
    const ConfigStateEntry& e = entries[i];
    doc.begin_object(e.name);
    if (e.has_value) {
      doc.i32("value", e.value);
    } else {
      doc.null("value");
    }
    // Spec 16.7.4 - inferred for a node-held row, because CONFIG_ACK carries no override
    // flag. W15 tracks that gap; an override equal to its default reads `default` here.
    doc.str("source", e.is_override ? "override" : "default");
    doc.end_object();
  }
  return doc.finish();
}

size_t build_phy_reverted(uint32_t boot, uint32_t event_id, const char* reason,
                          const char* node, char* out, size_t cap) {
  JsonObject doc(out, cap);
  if (boot == 0) doc.null("boot"); else doc.u32("boot", boot);
  doc.u32("event_id", event_id);
  doc.str("reason", reason);
  if (node == nullptr) doc.null("node"); else doc.str("node", node);
  return doc.finish();
}

}  // namespace bridge
