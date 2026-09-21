// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// What the bridge holds of the configuration, and how a name on a topic resolves to a
// row. Task BF-32; spec 16.7.1; D47, D49.
//
// ARDUINO-FREE, with the nonvolatile store INJECTED. `/lib/lran-config/`'s Store already
// does the hard part - clamping, the effective value in every ACK, an honest
// persist_status - so this file is the bridge's own scoping around it: one Store for the
// global rows and one per node for the per-node rows, plus the name lookup.
//
// THE TOPIC DISAMBIGUATES, AND THAT IS NOT A CONVENIENCE. Three names appear in BOTH
// blocks of the table - `cad_retries`, `backoff_max_ms` and `frag_reassembly_timeout_ms`
// - because the bridge and every node each have their own. Spec 16.7.1 resolves them by
// where the set arrived: `lran/bridge/config/set` sees the global rows alone, and
// `lran/<node>/config/set` sees that node's rows and the bridge's per-node rows for it.
// A lookup that searched both blocks would answer the bridge's `cad_retries` for a set
// aimed at a node, and the write would land on the wrong radio.
//
// A NAME THE TOPIC DOES NOT HOLD IS `unknown_param` (spec 16.7.1) - never a fall-through
// to the other block.

#pragma once

#include <cstddef>
#include <cstdint>

#include "config_json.h"
#include "lran/config/store.h"
#include "lran/config/table.h"
#include "lran/types.h"
#include "registry.h"

namespace bridge {

// Which block of the table a topic exposes (spec 16.7.1).
enum class ConfigScope : uint8_t {
  Bridge,  // lran/bridge/config/set - Owner::BridgeGlobal
  Node,    // lran/<node>/config/set - Owner::Node and Owner::BridgePerNode
};

// The row a name resolves to on a scope, or nullptr. Node scope answers rows of both
// owners, and the caller tells them apart by `owner` to decide which half applies it.
const lran::config::ParamDef* find_param(ConfigScope scope, const char* name);

// One of the two documents' worth of rows, in table order.
size_t scope_rows(ConfigScope scope, const lran::config::ParamDef** out, size_t cap);

// ---------------------------------------------------------------------------
// THE PREMISE THE TABLES ARE BUILT ON, AND THE CHECK THAT WOULD FALSIFY IT.
//
// `Table::add_block` takes a pointer and a count, so the bridge's two blocks are
// SUB-RANGES of kBridgeParams rather than copies of it. That costs no RAM and needs no
// assembly at boot - and it is only correct while each owner's rows sit together in the
// table. A row inserted between them would silently put a per-node row in the global
// block, where it would be set on the wrong topic and applied to every node at once.
//
// The static_asserts below are what fails instead. They are compile-time because the
// table is, and because a boot-time check on this would report a defect to a log nobody
// reads on a bridge that is already answering the wrong topic.
// ---------------------------------------------------------------------------

constexpr size_t owner_first(const lran::config::ParamDef* p, size_t n,
                             lran::config::Owner o) {
  for (size_t i = 0; i < n; ++i) {
    if (p[i].owner == o) return i;
  }
  return n;
}

constexpr size_t owner_count(const lran::config::ParamDef* p, size_t n,
                             lran::config::Owner o) {
  size_t c = 0;
  for (size_t i = 0; i < n; ++i) {
    if (p[i].owner == o) ++c;
  }
  return c;
}

constexpr bool owner_rows_contiguous(const lran::config::ParamDef* p, size_t n,
                                     lran::config::Owner o) {
  const size_t first = owner_first(p, n, o);
  const size_t count = owner_count(p, n, o);
  if (count == 0) return false;
  for (size_t i = first; i < first + count; ++i) {
    if (p[i].owner != o) return false;
  }
  return true;
}

inline constexpr size_t kBridgeGlobalFirst =
    owner_first(lran::config::kBridgeParams, lran::config::kBridgeParamCount,
                lran::config::Owner::BridgeGlobal);
inline constexpr size_t kBridgeGlobalCount =
    owner_count(lran::config::kBridgeParams, lran::config::kBridgeParamCount,
                lran::config::Owner::BridgeGlobal);
inline constexpr size_t kBridgePerNodeFirst =
    owner_first(lran::config::kBridgeParams, lran::config::kBridgeParamCount,
                lran::config::Owner::BridgePerNode);
inline constexpr size_t kBridgePerNodeCount =
    owner_count(lran::config::kBridgeParams, lran::config::kBridgeParamCount,
                lran::config::Owner::BridgePerNode);

static_assert(owner_rows_contiguous(lran::config::kBridgeParams,
                                    lran::config::kBridgeParamCount,
                                    lran::config::Owner::BridgeGlobal),
              "the bridge's global rows must sit together - config_store.h builds a "
              "table block from that range without copying it");
static_assert(owner_rows_contiguous(lran::config::kBridgeParams,
                                    lran::config::kBridgeParamCount,
                                    lran::config::Owner::BridgePerNode),
              "the bridge's per-node rows must sit together - see above");

// ---------------------------------------------------------------------------
// The bridge's holdings.
//
// ONE STORE PER SCOPE THE BRIDGE ITSELF APPLIES. The global rows are one store; each
// node's per-node rows are another, because `poll_interval_s` is one value per node
// (D47) and a single store keyed by id alone would let one node's write move another's.
// ---------------------------------------------------------------------------

class ConfigStore {
 public:
  ConfigStore();

  // `global` and `per_node` are the nonvolatile stores, injected (D49): NVS on the
  // target, a fake or nullptr on the host. A null store still applies and still answers,
  // with APPLIED_NOT_PERSISTED - a bridge that cannot save must never become
  // unconfigurable, and HA must never be told a value was saved when it was not.
  void begin(lran::config::Persist* global, lran::config::Persist* const* per_node,
             size_t n);

  // Apply the bridge-held entries of one parsed `config/set`.
  //
  // `node_half`, when not null, receives the entries this bridge does NOT hold - the
  // ones a node applies from a CONFIG. A name neither half holds is answered
  // `unknown_param` here, because no frame should be spent asking a node about a name
  // the table says it does not have.
  //
  // Returns the number of results written, and reports the bridge half's persist
  // outcome through `persist`.
  size_t apply(ConfigScope scope, lran::NodeId node, const ConfigSetRequest& req,
               ConfigResult* results, size_t cap, AckPersist* persist,
               ConfigSetRequest* node_half);

  // D52 - RESTORE_DEFAULTS clears every override in the scope and is answered as
  // GET_ALL is. The node half is the caller's to send.
  size_t restore_defaults(ConfigScope scope, lran::NodeId node, ConfigResult* results,
                          size_t cap, AckPersist* persist);

  // Every bridge-held row of the scope, with its effective value - what GET_ALL answers
  // and what `config/state` publishes.
  size_t read_all(ConfigScope scope, lran::NodeId node, ConfigResult* results,
                  size_t cap, AckPersist* persist) const;
  size_t state(ConfigScope scope, lran::NodeId node, ConfigStateEntry* out,
               size_t cap) const;

  // The runtime levers read their value here rather than from a constant (root rule 8).
  lran::config::Value global_value(uint16_t id) const;
  lran::config::Value node_value(lran::NodeId node, uint16_t id) const;

 private:
  // Indexed by the registry's order, so a node the registry does not carry simply has no
  // store rather than sharing one.
  lran::config::Store*       store_for(lran::NodeId node);
  const lran::config::Store* store_for(lran::NodeId node) const;

  lran::config::Table bridge_table_;
  lran::config::Table node_table_;  // Owner::BridgePerNode rows, the bridge's half

  // Built in place like the per-node stores below, and for the same reason: Store holds
  // its Persist from construction and the bridge's is not known until begin().
  lran::config::Store* bridge_store_ = nullptr;
  alignas(lran::config::Store) unsigned char bridge_storage_[sizeof(lran::config::Store)];

  // ONE STORE PER NODE, BUILT IN PLACE, AND NOT AN ALLOCATION. Root rule 3 forbids
  // allocating in firmware; the storage below is a member array and placement new only
  // runs a constructor in it. It is built this way rather than as six brace-initialized
  // members because each store takes its own Persist, which is not known until begin().
  //
  // IT WASTES ROOM AND THE TRADE IS DELIBERATE. Store carries kMaxTableParams override
  // slots and the per-node block holds one row today, so six stores spend about 3 KB to
  // hold six values. The alternative is a compact array here with its own clamping and
  // its own effective-value rule, which is the drift D44 exists to stop - the library is
  // where spec 7.4's behaviour lives, and it should not be written twice for 3 KB.
  lran::config::Store* node_stores_[kNodeCount] = {};
  alignas(lran::config::Store) unsigned char node_storage_[kNodeCount]
                                                          [sizeof(lran::config::Store)];
};

}  // namespace bridge
