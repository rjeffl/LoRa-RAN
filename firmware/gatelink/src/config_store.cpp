// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee

#include "config_store.h"

#include <cstdio>
#include <cstring>

namespace gatelink {

using lran::config::ParamDef;
using lran::config::Value;

namespace {

// A cursor over config.json. The grammar is the subset format_config() writes, and an
// operator editing the file on a laptop: one object, string keys without escapes, integer
// values, whitespace anywhere JSON allows it.
class Reader {
 public:
  Reader(const char* text, size_t n) : p_(text), end_(text + n) {}

  void skip_ws() {
    while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\r' || *p_ == '\n')) ++p_;
  }
  bool take(char c) {
    skip_ws();
    if (p_ >= end_ || *p_ != c) return false;
    ++p_;
    return true;
  }
  bool peek(char c) {
    skip_ws();
    return p_ < end_ && *p_ == c;
  }
  bool at_end() {
    skip_ws();
    return p_ >= end_;
  }

  // A key, copied into `out`. False on an escape, a key longer than `cap`, or no quote.
  bool key(char* out, size_t cap) {
    if (!take('"')) return false;
    size_t n = 0;
    while (p_ < end_ && *p_ != '"') {
      if (*p_ == '\\' || n + 1 >= cap) return false;
      out[n++] = *p_++;
    }
    if (p_ >= end_) return false;
    ++p_;
    out[n] = '\0';
    return true;
  }

  // An integer inside int32_t. A fraction, an exponent or an overflow is an error, not a
  // truncation: the file would then say one thing and the node run another.
  bool integer(Value* out) {
    skip_ws();
    bool neg = false;
    if (p_ < end_ && *p_ == '-') {
      neg = true;
      ++p_;
    }
    if (p_ >= end_ || *p_ < '0' || *p_ > '9') return false;
    int64_t v = 0;
    while (p_ < end_ && *p_ >= '0' && *p_ <= '9') {
      v = v * 10 + (*p_++ - '0');
      if (v > static_cast<int64_t>(INT32_MAX) + 1) return false;
    }
    if (p_ < end_ && (*p_ == '.' || *p_ == 'e' || *p_ == 'E')) return false;
    if (neg) v = -v;
    if (v < INT32_MIN || v > INT32_MAX) return false;
    *out = static_cast<Value>(v);
    return true;
  }

 private:
  const char* p_;
  const char* end_;
};

const ParamDef* find_by_name(const lran::config::Table& table, const char* name) {
  for (size_t i = 0; i < table.size(); ++i) {
    const ParamDef* d = table.at(i);
    if (d != nullptr && std::strcmp(d->name, name) == 0) return d;
  }
  return nullptr;
}

}  // namespace

size_t format_config(const ParamDef* const* defs, const Value* values, size_t n, char* out,
                     size_t cap) {
  size_t used = 0;
  // snprintf's return, checked for truncation. False once the buffer is full.
  auto advance = [&](int w) {
    if (w < 0 || static_cast<size_t>(w) >= cap - used) return false;
    used += static_cast<size_t>(w);
    return true;
  };
  if (cap == 0) return 0;
  if (n == 0) return advance(std::snprintf(out, cap, "{}\n")) ? used : 0;
  if (!advance(std::snprintf(out, cap, "{\n"))) return 0;
  for (size_t i = 0; i < n; ++i) {
    if (!advance(std::snprintf(out + used, cap - used, "  \"%s\": %ld%s\n", defs[i]->name,
                               static_cast<long>(values[i]), i + 1 < n ? "," : ""))) {
      return 0;
    }
  }
  return advance(std::snprintf(out + used, cap - used, "}\n")) ? used : 0;
}

ParseResult parse_config(const lran::config::Table& table, const char* text, size_t n,
                         FileEntry* out, size_t cap) {
  ParseResult r;
  Reader      in(text, n);
  if (!in.take('{')) return r;
  if (in.take('}')) {
    r.ok = in.at_end();
    return r;
  }
  for (;;) {
    char  name[48];
    Value v = 0;
    if (!in.key(name, sizeof(name)) || !in.take(':') || !in.integer(&v)) return ParseResult{};
    const ParamDef* d = find_by_name(table, name);
    if (d == nullptr) {
      ++r.unknown;
    } else if (r.entries < cap) {
      out[r.entries++] = FileEntry{d, v};
    }
    if (in.take(',')) continue;
    if (!in.take('}')) return ParseResult{};
    break;
  }
  r.ok = in.at_end();
  if (!r.ok) return ParseResult{};
  return r;
}

void SdPersist::load(lran::config::Store* store) {
  stats_.restored     = 0;
  stats_.refused      = 0;
  stats_.unknown      = 0;
  stats_.file_corrupt = false;
  mounted_            = file_ != nullptr && file_->mount(false);
  dirty_              = false;
  if (!mounted_) {
    ++stats_.mounts_failed;
    return;
  }
  const size_t n = file_->read(text_, sizeof(text_));
  if (n == 0) return;  // no file: every row at its default

  const ParseResult r = parse_config(table_, text_, n, parsed_, lran::config::kMaxTableParams);
  if (!r.ok) {
    // The defaults run, and the next write replaces the file. Leaving it would bring the
    // same unreadable file back at every boot.
    stats_.file_corrupt = true;
    return;
  }
  stats_.unknown = static_cast<uint32_t>(r.unknown);
  for (size_t i = 0; i < r.entries; ++i) {
    // restore() clamps as apply() would and refuses a PHY row (spec 12.4 step 2).
    if (store->restore(parsed_[i].def->id, parsed_[i].value)) {
      ++stats_.restored;
    } else {
      ++stats_.refused;
    }
  }
}

bool SdPersist::refresh() {
  if (usable() || file_ == nullptr) return usable();
  mounted_ = file_->mount(true);
  if (!mounted_) {
    ++stats_.mounts_failed;
    return false;
  }
  return write_all();
}

bool SdPersist::write_all() {
  if (store_ == nullptr || file_ == nullptr || !mounted_) {
    dirty_ = true;
    return false;
  }
  size_t n = 0;
  for (size_t i = 0; i < table_.size(); ++i) {
    const ParamDef* d = table_.at(i);
    // A PHY row is never written here: the Store holds no committed PHY override while
    // spec 12.4.2 is unbuilt, and a trial value must never reach the card (step 2).
    if (d == nullptr || d->access == lran::config::Access::Phy || !store_->is_override(d->id)) {
      continue;
    }
    defs_[n]   = d;
    values_[n] = store_->effective(d->id);
    ++n;
  }
  const size_t len = format_config(defs_, values_, n, text_, sizeof(text_));
  ++stats_.writes;
  const bool ok = len > 0 && file_->write(text_, len);
  if (!ok) ++stats_.write_failures;
  dirty_ = !ok;
  return ok;
}

GateLinkConfig::GateLinkConfig(ConfigFile* file) : persist_(table_, file), store_(table_, &persist_) {
  (void)table_.add_block(lran::config::kNodeCommonParams, lran::config::kNodeCommonParamCount);
  (void)table_.add_block(lran::config::kGateLinkParams, lran::config::kGateLinkParamCount);
  persist_.attach(&store_);
}

bool GateLinkConfig::unpersisted() const {
  return persist_.dirty() || store_.read_persist_status() != lran::PersistStatus::Persisted;
}

}  // namespace gatelink
