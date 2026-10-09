// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// GateLink's configuration: lran-config's Store over the node-common and GateLink blocks,
// with microSD as its Persist (Impl Plan 6.4, D49, PRD R-4.2c). GL3.
//
// ARDUINO-FREE. The card is reached through ConfigFile, which the board implements and
// the native suite fakes, so the honest-reporting cases can be tested without a card.
//
// THE FILE IS config.json, ONE FLAT OBJECT OF OVERRIDES BY NAME: {"relay_pulse_ms": 700}.
// Names, not ids, because the file is what an operator reads with the card in a laptop,
// and a name is as permanent as an id once Home Assistant publishes it (spec 16.7). A row
// the file does not name runs at its default. Each write replaces the whole file, so the
// card holds exactly the overrides in RAM or the write failed.
//
// HONEST WHEN THE CARD IS NOT (spec 7.4, 8.11). A write that fails marks the store dirty:
// the card no longer holds what RAM does, so usable() reads false and every answer says
// APPLIED_NOT_PERSISTED until refresh() rewrites the whole file. The card can be pulled at
// any time and nothing tells the firmware, so a failed write is how removal is found.
//
// EVERY WRITE IS MADE FROM THE STORE, never from the entry that prompted it. A SET applied
// while the card was absent reaches RAM and not the card, and the Store never calls save()
// for it; the next write still carries it, because it walks the Store's overrides.

#pragma once

#include <cstddef>
#include <cstdint>

#include "lran/config/store.h"
#include "lran/config/table.h"

namespace gatelink {

// The card, as the store needs it. One file; the board decides how to replace it safely.
class ConfigFile {
 public:
  virtual ~ConfigFile() = default;
  // Mounts the card, unmounting first when `remount` is set, which is how a card pulled
  // and put back is found again. True when the card answered.
  virtual bool mount(bool remount) = 0;
  // The file's bytes, or 0 when there is no file or it does not fit `cap`.
  virtual size_t read(char* out, size_t cap) = 0;
  // Replaces the file with `n` bytes. False when the write fell short.
  virtual bool write(const char* text, size_t n) = 0;
};

// The longest config.json: every row overridden, each line its name, a colon and an i32.
inline constexpr size_t kConfigFileCap = 3072;

// One override in the file. The Value is the file's, before the Store clamps it.
struct FileEntry {
  const lran::config::ParamDef* def   = nullptr;
  lran::config::Value           value = 0;
};

// What a parse found. A syntax error abandons the whole file: half a file is a
// configuration nobody chose.
struct ParseResult {
  bool   ok       = false;
  size_t entries  = 0;
  size_t unknown  = 0;  // names the table has no row for; skipped, and counted
};

// Writes `n` overrides as config.json. Bytes written, or 0 when `cap` is too small.
size_t format_config(const lran::config::ParamDef* const* defs, const lran::config::Value* values,
                     size_t n, char* out, size_t cap);

// Reads config.json into `out`, at most `cap` entries.
ParseResult parse_config(const lran::config::Table& table, const char* text, size_t n,
                         FileEntry* out, size_t cap);

struct PersistStats {
  uint32_t writes         = 0;
  uint32_t write_failures = 0;
  uint32_t mounts_failed  = 0;
  uint32_t restored       = 0;  // values the last load() put back
  uint32_t refused        = 0;  // values in the file the Store would not take back
  uint32_t unknown        = 0;  // names in the file with no row
  bool     file_corrupt   = false;
};

class SdPersist final : public lran::config::Persist {
 public:
  SdPersist(const lran::config::Table& table, ConfigFile* file) : table_(table), file_(file) {}

  // The Store whose overrides each write carries. Set once, before load().
  void attach(const lran::config::Store* store) { store_ = store; }

  // Boot: mounts the card and puts every value in config.json back into `store`. With no
  // card, or no file, the defaults apply (Impl Plan 6.4's absent-card behaviour).
  void load(lran::config::Store* store);

  // Brings an absent or dirty card back: remounts, and rewrites the whole file from the
  // Store. True when the card now holds what RAM does. A no-op while usable().
  bool refresh();

  // lran-config's Persist. usable() is false while the card is absent or dirty, so the
  // Store reports APPLIED_NOT_PERSISTED rather than a saved value it cannot vouch for.
  bool usable() const override { return mounted_ && !dirty_; }
  bool save(uint16_t, lran::config::Value) override { return write_all(); }
  bool clear_all() override { return write_all(); }
  // The PHY group is not persisted here: spec 12.4.2 is not built, so the Store answers
  // those rows READ_ONLY and never calls this.
  bool save_group(const uint16_t*, const lran::config::Value*, size_t) override { return false; }

  bool mounted() const { return mounted_; }
  // RAM holds overrides the card does not.
  bool dirty() const { return dirty_; }
  const PersistStats& stats() const { return stats_; }

 private:
  bool write_all();

  const lran::config::Table& table_;
  ConfigFile*                file_;
  const lran::config::Store* store_   = nullptr;
  bool                       mounted_ = false;
  bool                       dirty_   = false;

  // Root rule 3: fixed, bounded by the table.
  const lran::config::ParamDef* defs_[lran::config::kMaxTableParams]   = {};
  lran::config::Value           values_[lran::config::kMaxTableParams] = {};
  FileEntry                     parsed_[lran::config::kMaxTableParams] = {};
  char                          text_[kConfigFileCap]                  = {};
  PersistStats stats_;
};

// The Table, the Persist and the Store, built in the order each needs the last.
class GateLinkConfig {
 public:
  explicit GateLinkConfig(ConfigFile* file);

  GateLinkConfig(const GateLinkConfig&)            = delete;
  GateLinkConfig& operator=(const GateLinkConfig&) = delete;

  const lran::config::Table& table() const { return table_; }
  lran::config::Store&       store() { return store_; }
  const lran::config::Store& store() const { return store_; }
  SdPersist&                 persist() { return persist_; }
  const SdPersist&           persist() const { return persist_; }

  // spec 7.2.8 bit 0 and spec 7.4's D53 - true when an override is running that the card
  // does not hold.
  bool unpersisted() const;

 private:
  lran::config::Table table_;
  SdPersist           persist_;
  lran::config::Store store_;
};

}  // namespace gatelink
