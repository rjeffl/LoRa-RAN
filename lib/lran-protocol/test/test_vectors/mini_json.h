// SPDX-License-Identifier: MIT
// Copyright (c) 2026 <holder>          // D31 open - see LRAN-Decision-Register
//
// A minimal JSON reader for the W4 vector files. TEST CODE ONLY - it is not part of
// the library and nothing in /lib/ or any firmware may include it.
//
// Deliberately not a general JSON library. It parses the subset /tools/vectors/
// emits (objects, arrays, strings, integers, true/false/null) into a fixed node
// arena, so the no-dynamic-allocation rule still holds even here. Unknown keys are
// ignored rather than rejected, so the generator can add fields without breaking the
// consumer - but a MISSING key a test asks for is a hard failure, never a silent
// default. A vector that fails to parse must never look like a vector that passed.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mini_json {

// Sized against the largest vector file (~27 KB, ~700 nodes) with generous room
// to grow. The arena is static and shared, so ONE Doc is live at a time - load a
// file, consume it, then load the next.
inline constexpr int    kMaxNodes = 12000;
inline constexpr size_t kMaxText  = 200000;

enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

struct Node {
  Type   type        = Type::Null;
  int    first_child = -1;
  int    next        = -1;
  size_t key_off     = 0;  // into text; 0 length when the node is not a member
  size_t key_len     = 0;
  size_t val_off     = 0;  // string body (unescaped source) or number text
  size_t val_len     = 0;
  long   num         = 0;
  bool   boolean     = false;
};

class Doc {
 public:
  // Returns false if the file cannot be read or does not parse. Never partially
  // succeeds: a caller that ignores the result gets an empty document, not garbage.
  bool load(const char* path) {
    n_nodes_ = 0;
    text_len_ = 0;
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return false;
    text_len_ = fread(text_, 1, kMaxText - 1, f);
    const bool more = (fgetc(f) != EOF);  // the arena must not silently truncate
    fclose(f);
    if (more) return false;
    text_[text_len_] = '\0';
    pos_ = 0;
    skip_ws();
    const int root = parse_value();
    return root == 0;
  }

  const Node& node(int i) const { return nodes_[i]; }
  const char* text() const { return text_; }

  // Object member lookup by key. Returns -1 when absent.
  int get(int obj, const char* key) const {
    if (obj < 0 || nodes_[obj].type != Type::Object) return -1;
    const size_t klen = strlen(key);
    for (int c = nodes_[obj].first_child; c >= 0; c = nodes_[c].next) {
      if (nodes_[c].key_len == klen &&
          memcmp(text_ + nodes_[c].key_off, key, klen) == 0) {
        return c;
      }
    }
    return -1;
  }

  int count(int arr) const {
    if (arr < 0 || nodes_[arr].type != Type::Array) return 0;
    int n = 0;
    for (int c = nodes_[arr].first_child; c >= 0; c = nodes_[c].next) ++n;
    return n;
  }

  int at(int arr, int index) const {
    if (arr < 0) return -1;
    int i = 0;
    for (int c = nodes_[arr].first_child; c >= 0; c = nodes_[c].next, ++i) {
      if (i == index) return c;
    }
    return -1;
  }

  bool is_null(int i) const { return i >= 0 && nodes_[i].type == Type::Null; }

  // String compare against a literal.
  bool str_is(int i, const char* s) const {
    if (i < 0 || nodes_[i].type != Type::String) return false;
    const size_t len = strlen(s);
    return nodes_[i].val_len == len && memcmp(text_ + nodes_[i].val_off, s, len) == 0;
  }

  // Copies a string value out, NUL terminated. Returns false if absent or too long.
  bool str(int i, char* out, size_t cap) const {
    if (i < 0 || nodes_[i].type != Type::String) return false;
    if (nodes_[i].val_len + 1 > cap) return false;
    memcpy(out, text_ + nodes_[i].val_off, nodes_[i].val_len);
    out[nodes_[i].val_len] = '\0';
    return true;
  }

  long num(int i, long fallback = 0) const {
    return (i >= 0 && nodes_[i].type == Type::Number) ? nodes_[i].num : fallback;
  }

  // Decodes a lowercase-hex string value into bytes. `""` is a valid zero-length
  // result and is NOT the same as an absent key, which returns false.
  bool hex(int i, uint8_t* out, size_t cap, size_t* out_len) const {
    if (i < 0 || nodes_[i].type != Type::String) return false;
    const size_t len = nodes_[i].val_len;
    if ((len & 1u) != 0 || len / 2 > cap) return false;
    const char* p = text_ + nodes_[i].val_off;
    for (size_t b = 0; b < len / 2; ++b) {
      int hi = hexval(p[2 * b]), lo = hexval(p[2 * b + 1]);
      if (hi < 0 || lo < 0) return false;
      out[b] = static_cast<uint8_t>((hi << 4) | lo);
    }
    *out_len = len / 2;
    return true;
  }

  // A "0x.." string byte, as the format uses for frag / schema / hdr_flags.
  bool hex_byte(int i, uint8_t* out) const {
    char buf[16];
    if (!str(i, buf, sizeof(buf))) return false;
    const char* p = buf;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    long v = strtol(p, nullptr, 16);
    if (v < 0 || v > 0xFF) return false;
    *out = static_cast<uint8_t>(v);
    return true;
  }

 private:
  static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  void skip_ws() {
    while (pos_ < text_len_ && (text_[pos_] == ' ' || text_[pos_] == '\n' ||
                                text_[pos_] == '\r' || text_[pos_] == '\t')) {
      ++pos_;
    }
  }

  int alloc() {
    if (n_nodes_ >= kMaxNodes) return -1;
    nodes_[n_nodes_] = Node{};
    return n_nodes_++;
  }

  // Returns the node index, or -1 on any malformed input.
  int parse_value() {
    skip_ws();
    if (pos_ >= text_len_) return -1;
    const char c = text_[pos_];
    if (c == '{') return parse_object();
    if (c == '[') return parse_array();
    if (c == '"') return parse_string();
    if (c == 't' || c == 'f') return parse_bool();
    if (c == 'n') return parse_null();
    return parse_number();
  }

  int parse_string() {
    const int id = alloc();
    if (id < 0) return -1;
    ++pos_;  // opening quote
    const size_t start = pos_;
    while (pos_ < text_len_ && text_[pos_] != '"') {
      // The generator emits \uXXXX for the section sign in spec_ref. Those fields are
      // never compared byte-for-byte, so escapes are spanned, not decoded.
      if (text_[pos_] == '\\' && pos_ + 1 < text_len_) ++pos_;
      ++pos_;
    }
    if (pos_ >= text_len_) return -1;
    nodes_[id].type    = Type::String;
    nodes_[id].val_off = start;
    nodes_[id].val_len = pos_ - start;
    ++pos_;  // closing quote
    return id;
  }

  int parse_number() {
    const int id = alloc();
    if (id < 0) return -1;
    const size_t start = pos_;
    while (pos_ < text_len_ && (text_[pos_] == '-' || text_[pos_] == '+' ||
                                text_[pos_] == '.' || text_[pos_] == 'e' ||
                                text_[pos_] == 'E' ||
                                (text_[pos_] >= '0' && text_[pos_] <= '9'))) {
      ++pos_;
    }
    if (pos_ == start) return -1;
    nodes_[id].type    = Type::Number;
    nodes_[id].val_off = start;
    nodes_[id].val_len = pos_ - start;
    nodes_[id].num     = strtol(text_ + start, nullptr, 10);
    return id;
  }

  int parse_bool() {
    const int id = alloc();
    if (id < 0) return -1;
    nodes_[id].type = Type::Bool;
    if (strncmp(text_ + pos_, "true", 4) == 0) {
      nodes_[id].boolean = true;
      pos_ += 4;
    } else if (strncmp(text_ + pos_, "false", 5) == 0) {
      nodes_[id].boolean = false;
      pos_ += 5;
    } else {
      return -1;
    }
    return id;
  }

  int parse_null() {
    const int id = alloc();
    if (id < 0) return -1;
    if (strncmp(text_ + pos_, "null", 4) != 0) return -1;
    nodes_[id].type = Type::Null;
    pos_ += 4;
    return id;
  }

  int parse_array() {
    const int id = alloc();
    if (id < 0) return -1;
    nodes_[id].type = Type::Array;
    ++pos_;  // '['
    int last = -1;
    skip_ws();
    if (pos_ < text_len_ && text_[pos_] == ']') {
      ++pos_;
      return id;
    }
    for (;;) {
      const int child = parse_value();
      if (child < 0) return -1;
      if (last < 0) nodes_[id].first_child = child;
      else          nodes_[last].next = child;
      last = child;
      skip_ws();
      if (pos_ >= text_len_) return -1;
      if (text_[pos_] == ',') { ++pos_; continue; }
      if (text_[pos_] == ']') { ++pos_; return id; }
      return -1;
    }
  }

  int parse_object() {
    const int id = alloc();
    if (id < 0) return -1;
    nodes_[id].type = Type::Object;
    ++pos_;  // '{'
    int last = -1;
    skip_ws();
    if (pos_ < text_len_ && text_[pos_] == '}') {
      ++pos_;
      return id;
    }
    for (;;) {
      skip_ws();
      if (pos_ >= text_len_ || text_[pos_] != '"') return -1;
      const int keynode = parse_string();
      if (keynode < 0) return -1;
      const size_t koff = nodes_[keynode].val_off;
      const size_t klen = nodes_[keynode].val_len;
      --n_nodes_;  // the key is metadata on the value, not a node of its own
      skip_ws();
      if (pos_ >= text_len_ || text_[pos_] != ':') return -1;
      ++pos_;
      const int child = parse_value();
      if (child < 0) return -1;
      nodes_[child].key_off = koff;
      nodes_[child].key_len = klen;
      if (last < 0) nodes_[id].first_child = child;
      else          nodes_[last].next = child;
      last = child;
      skip_ws();
      if (pos_ >= text_len_) return -1;
      if (text_[pos_] == ',') { ++pos_; continue; }
      if (text_[pos_] == '}') { ++pos_; return id; }
      return -1;
    }
  }

  static char   text_[kMaxText];
  static Node   nodes_[kMaxNodes];
  size_t        text_len_ = 0;
  size_t        pos_      = 0;
  int           n_nodes_  = 0;
};

inline char Doc::text_[kMaxText];
inline Node Doc::nodes_[kMaxNodes];

}  // namespace mini_json
