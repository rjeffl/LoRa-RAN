// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// The JSON object writer. Written for BF-19's diagnostic documents and lifted out of
// diag_json.cpp by BF-23, when discovery became its second user.
//
// ARDUINO-FREE AND HEADER-ONLY, like the accounting headers beside it. Both callers are
// host-tested, and a document that has to be read by Home Assistant is exactly the kind
// of thing that should be checked at a desk rather than at the broker.
//
// IT REFUSES RATHER THAN TRUNCATES, and that is the whole reason it exists instead of
// snprintf at each call site. Truncated JSON is worse than absent: Home Assistant logs a
// parse error against a topic that looks alive while the entity keeps a stale value
// (firmware/bridge/CLAUDE.md). A document that did not fit reports 0 and leaves an empty
// string, and the caller drops the publication.

#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace bridge {

class JsonObject {
 public:
  JsonObject(char* out, size_t cap) : out_(out), cap_(cap) {
    if (out_ == nullptr || cap_ == 0) {
      ok_ = false;
      return;
    }
    append("{");
  }

  void u32(const char* key, uint32_t v) { field(key, "%lu", static_cast<unsigned long>(v)); }
  void i32(const char* key, int32_t v) { field(key, "%ld", static_cast<long>(v)); }
  void null(const char* key) { field(key, "null"); }

  // A fixed-point value written as a decimal: `scaled` is in units of 10^-decimals, so
  // (-5, 1) writes -0.5. Added for BF-24, whose wire fields are 0.1 C and 10 Wh. Integer
  // arithmetic, because a float round trip turns 0.3 into 0.30000000000000004 in a
  // document Home Assistant keeps in its history.
  void decimal(const char* key, int64_t scaled, unsigned decimals) {
    int64_t div = 1;
    for (unsigned i = 0; i < decimals; ++i) div *= 10;
    const bool     neg = scaled < 0;
    const uint64_t mag = neg ? static_cast<uint64_t>(-(scaled + 1)) + 1u
                             : static_cast<uint64_t>(scaled);
    const uint64_t whole = mag / static_cast<uint64_t>(div);
    const uint64_t frac  = mag % static_cast<uint64_t>(div);
    if (decimals == 0) {
      field(key, "%s%llu", neg ? "-" : "", static_cast<unsigned long long>(whole));
    } else {
      field(key, "%s%llu.%0*llu", neg ? "-" : "", static_cast<unsigned long long>(whole),
            static_cast<int>(decimals), static_cast<unsigned long long>(frac));
    }
  }
  void boolean(const char* key, bool v) { field(key, v ? "true" : "false"); }

  // A string value, quoted and escaped. A null pointer writes nothing at all - not
  // `null`, not an empty string - so a table row that leaves a field unset simply omits
  // the key. Home Assistant treats an absent discovery key as "use the default", where
  // an explicit null is a value.
  void str(const char* key, const char* v) {
    if (v == nullptr) return;
    open_key(key);
    append("\"");
    escape(v);
    append("\"");
  }

  // Already-formatted JSON - an array or a nested object the caller built. Nothing here
  // validates it, so the caller owns its shape.
  void raw(const char* key, const char* json) {
    if (json == nullptr) return;
    open_key(key);
    append("%s", json);
  }

  // A nested object, written in place rather than built in a second buffer. Added for
  // BF-32, whose `config/ack` is an object of objects two levels deep and whose largest
  // document already sits close to kMaxPayloadLen - a scratch buffer per level would
  // have cost that much stack again on a task that has other work to do.
  //
  // THE CALLER OWNS THE BALANCE, as it does for raw(). An end_object() that never comes
  // produces a document this class still calls ok, because tracking depth to refuse one
  // would be a second kind of correctness check in a class whose whole job is the first.
  // No state is saved across the nesting: after a nested object closes, its parent has
  // at least one member by construction, so the next sibling always takes a comma.
  void begin_object(const char* key) {
    open_key(key);
    append("{");
    first_ = true;
  }

  void end_object() {
    append("}");
    first_ = false;
  }

  size_t finish() {
    append("}");
    if (!ok_) {
      if (out_ != nullptr && cap_ > 0) out_[0] = '\0';
      return 0;
    }
    return len_;
  }

 private:
  void open_key(const char* key) {
    append(first_ ? "\"%s\":" : ",\"%s\":", key);
    first_ = false;
  }

  void field(const char* key, const char* fmt, ...) {
    open_key(key);
    va_list ap;
    va_start(ap, fmt);
    vappend(fmt, ap);
    va_end(ap);
  }

  // RFC 8259's two mandatory escapes plus the control range. Everything the bridge puts
  // in a string today is ASCII from a table, so this guards a future caller rather than
  // a present one - but a stray quote in a value would produce a document Home Assistant
  // cannot parse, which is the failure this class exists to prevent.
  void escape(const char* v) {
    for (const char* p = v; *p != '\0' && ok_; ++p) {
      const unsigned char c = static_cast<unsigned char>(*p);
      if (c == '"' || c == '\\') {
        append("\\%c", *p);
      } else if (c < 0x20) {
        append("\\u%04x", static_cast<unsigned>(c));
      } else {
        append("%c", *p);
      }
    }
  }

  void append(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vappend(fmt, ap);
    va_end(ap);
  }

  void vappend(const char* fmt, va_list ap) {
    if (!ok_) return;
    const int n = std::vsnprintf(out_ + len_, cap_ - len_, fmt, ap);
    if (n < 0 || static_cast<size_t>(n) >= cap_ - len_) {
      ok_ = false;
      return;
    }
    len_ += static_cast<size_t>(n);
  }

  char*  out_;
  size_t cap_;
  size_t len_   = 0;
  bool   ok_    = true;
  bool   first_ = true;
};

}  // namespace bridge
