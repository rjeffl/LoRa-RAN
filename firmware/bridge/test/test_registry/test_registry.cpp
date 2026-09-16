// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Robert J. Lee
//
// BF-15 - the per-node registry (Impl Plan 4.2; PRD R-3.1b, R-3.1c).
//
// KEYS ARE CHECKED AGAINST THE W4 VECTORS, not against the library's own KDF run twice.
// tools/vectors/generate.py derives node keys from the specification with the codec off
// limits; a registry that fed the wrong address byte into HKDF would agree with itself
// and disagree with every node (spec 9.1's "undetectable by inspection").
//
// WHAT THIS CANNOT COVER. mbedtls_mac.cpp is the KDF on the board; refimpl_mac.cpp is the
// KDF here. The library's P7 target run proved the two agree. The mutex in
// registry_runtime.cpp needs FreeRTOS and is not exercised.

#include <unity.h>

#include "lran/lran.h"
#include "refimpl_mac.h"
#include "registry.h"
#include "rx_ladder.h"
#include "test_key.h"
#include "vectors_data.h"

using namespace bridge;
using namespace lran;

void setUp() {}
void tearDown() {}

namespace {

refimpl::RefKdf g_kdf;
refimpl::RefMac g_mac;

Registry loaded() {
  Registry r;
  r.load(lran_test::kTestMasterKey, &g_kdf);
  return r;
}

Header hdr_from(NodeId src, CtxId ctx, uint8_t ver = kProtoVer) {
  Header h;
  h.type   = MsgType::Status;
  h.src    = src;
  h.dst    = kNodeBridge;
  h.ctx_id = ctx;
  h.ver    = ver;
  h.schema = kSchemaGateLinkStatusV1;
  return h;
}

}  // namespace

// ---------------------------------------------------------------------------
// The table
// ---------------------------------------------------------------------------

// spec 5.3's addresses, and the four bench identities as ordinary rows.
void test_the_table_is_spec_5_3() {
  TEST_ASSERT_EQUAL_size_t(6, kNodeCount);
  const NodeProvision want[] = {
      {kNodeGateLink, NodeType::GateLink}, {kNodeWellLink, NodeType::WellLink},
      {kNodeSim0, NodeType::Simnode},      {kNodeSim1, NodeType::Simnode},
      {kNodeSim2, NodeType::Simnode},      {kNodeSim3, NodeType::Simnode},
  };
  for (size_t i = 0; i < kNodeCount; ++i) {
    TEST_ASSERT_EQUAL_UINT8(want[i].id, kNodeTable[i].id);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(want[i].type), static_cast<int>(kNodeTable[i].type));
  }
}

// The static_assert beside the table proves nothing unless the check can fail.
void test_table_validation_rejects_duplicates_and_reserved_ids() {
  const NodeProvision dup[]       = {{0x01, NodeType::GateLink}, {0x01, NodeType::WellLink}};
  const NodeProvision bridge_id[] = {{kNodeBridge, NodeType::GateLink}};
  const NodeProvision broadcast[] = {{kNodeBroadcast, NodeType::Simnode}};
  const NodeProvision good[]      = {{0x01, NodeType::GateLink}, {0xF0, NodeType::Simnode}};
  TEST_ASSERT_FALSE(node_table_valid(dup, 2));
  TEST_ASSERT_FALSE(node_table_valid(bridge_id, 1));
  TEST_ASSERT_FALSE(node_table_valid(broadcast, 1));
  TEST_ASSERT_TRUE(node_table_valid(good, 2));
}

// spec 5.3 - is_bench follows the address, and nothing else.
void test_is_bench_is_derived_from_the_address() {
  const Registry r = loaded();
  for (size_t i = 0; i < r.size(); ++i) {
    const NodeInfo& n = r.info_at(i);
    TEST_ASSERT_EQUAL(is_bench_node(n.id), n.is_bench);
  }
  TEST_ASSERT_FALSE(r.find(kNodeGateLink)->is_bench);
  TEST_ASSERT_TRUE(r.find(kNodeSim3)->is_bench);
}

// ---------------------------------------------------------------------------
// Keys - spec 9.1, R-3.1c
// ---------------------------------------------------------------------------

// Every row's key equals the independently generated W4 vector for its address.
void test_every_key_matches_its_w4_vector() {
  const Registry r = loaded();
  size_t         matched = 0;
  for (size_t v = 0; v < lran_vectors::kKdfCount; ++v) {
    const lran_vectors::KdfVec& vec = lran_vectors::kKdf[v];
    TEST_ASSERT_EQUAL_MEMORY(lran_test::kTestMasterKey, vec.master, kMasterKeyLen);
    const uint8_t* key = r.key_for(vec.node_id);
    if (key == nullptr) continue;
    TEST_ASSERT_EQUAL_UINT16(kNodeKeyLen, vec.node_key_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(vec.node_key, key, kNodeKeyLen, vec.name);
    ++matched;
  }
  // Every provisioned node has a vector. A row added without one fails here.
  TEST_ASSERT_EQUAL_size_t(kNodeCount, matched);
}

// Before load() nothing is registered and no key exists, so the ladder refuses everything.
void test_nothing_is_registered_before_load() {
  const Registry r;
  TEST_ASSERT_NULL(r.key_for(kNodeGateLink));
  TEST_ASSERT_FALSE(r.is_registered(kNodeGateLink));
  TEST_ASSERT_NULL(r.find(kNodeGateLink));
  TEST_ASSERT_NULL(r.state(kNodeGateLink));
}

void test_an_unregistered_address_has_no_key() {
  const Registry r = loaded();
  const NodeId   unregistered[] = {kNodeBridge, 0x03, 0xEF, 0xF4, 0xFE, kNodeBroadcast};
  for (NodeId id : unregistered) {
    TEST_ASSERT_NULL(r.key_for(id));
    TEST_ASSERT_FALSE(r.is_registered(id));
    TEST_ASSERT_NULL(r.find(id));
  }
}

// ---------------------------------------------------------------------------
// What the bridge learns - spec 10.1, 10.2
// ---------------------------------------------------------------------------

// Root rule 6 - a node never heard reads as never heard, not as 0 dBm on version 0.
void test_an_unheard_node_reads_as_sentinels() {
  const Registry   r = loaded();
  const NodeState* s = r.state(kNodeGateLink);
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_FALSE(s->heard);
  TEST_ASSERT_EQUAL_UINT32(0, s->ctx_id);
  TEST_ASSERT_EQUAL_UINT16(1, s->cmd_seq);
  TEST_ASSERT_EQUAL_INT16(kRssiUnknown, s->rssi_dbm);
  TEST_ASSERT_EQUAL_INT8(kSnrUnknown, s->snr_db);
  TEST_ASSERT_EQUAL_UINT8(kVerUnknown, s->proto_ver);
  TEST_ASSERT_EQUAL_UINT16(0, s->missed_polls);
  TEST_ASSERT_EQUAL_UINT16(kPollIntervalDefaultS, s->poll_interval_s);
}

void test_a_reception_records_last_seen_radio_and_version() {
  Registry r = loaded();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Observed::NewContext),
                        static_cast<int>(r.observe(hdr_from(kNodeGateLink, 0xAABBCCDDu, 1),
                                                   -97, -8, 12345)));
  const NodeState* s = r.state(kNodeGateLink);
  TEST_ASSERT_TRUE(s->heard);
  TEST_ASSERT_EQUAL_UINT32(12345, s->last_seen_ms);
  TEST_ASSERT_EQUAL_INT16(-97, s->rssi_dbm);
  TEST_ASSERT_EQUAL_INT8(-8, s->snr_db);
  TEST_ASSERT_EQUAL_UINT8(1, s->proto_ver);
}

// spec 10.1 learns ctx_id from any frame; spec 10.2 resets cmd_seq to 1 on a new one.
void test_a_new_context_is_learned_and_an_old_one_is_not_relearned() {
  Registry r = loaded();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Observed::NewContext),
                        static_cast<int>(r.observe(hdr_from(kNodeSim1, 0x1111u), 0, 0, 1)));
  TEST_ASSERT_EQUAL_UINT32(0x1111u, r.state(kNodeSim1)->ctx_id);
  TEST_ASSERT_EQUAL_UINT16(1, r.state(kNodeSim1)->cmd_seq);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Observed::Heard),
                        static_cast<int>(r.observe(hdr_from(kNodeSim1, 0x1111u), 0, 0, 2)));

  // The node rebooted.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Observed::NewContext),
                        static_cast<int>(r.observe(hdr_from(kNodeSim1, 0x2222u), 0, 0, 3)));
  TEST_ASSERT_EQUAL_UINT32(0x2222u, r.state(kNodeSim1)->ctx_id);
  TEST_ASSERT_EQUAL_UINT16(1, r.state(kNodeSim1)->cmd_seq);
}

// A zero ctx_id is not a context, and adopting one would unlearn the node's real one.
void test_a_zero_context_is_not_adopted() {
  Registry r = loaded();
  r.observe(hdr_from(kNodeGateLink, 0x5555u), 0, 0, 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Observed::Heard),
                        static_cast<int>(r.observe(hdr_from(kNodeGateLink, 0), 0, 0, 2)));
  TEST_ASSERT_EQUAL_UINT32(0x5555u, r.state(kNodeGateLink)->ctx_id);
}

// Learning about one node leaves every other entry alone.
void test_an_observation_touches_only_its_own_entry() {
  Registry r = loaded();
  r.observe(hdr_from(kNodeSim0, 0xABCDu), -50, 5, 99);
  TEST_ASSERT_FALSE(r.state(kNodeSim1)->heard);
  TEST_ASSERT_EQUAL_UINT32(0, r.state(kNodeSim1)->ctx_id);
  TEST_ASSERT_FALSE(r.state(kNodeGateLink)->heard);
}

void test_an_unregistered_node_changes_nothing() {
  Registry r = loaded();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Observed::UnregisteredNode),
                        static_cast<int>(r.observe(hdr_from(0x03, 0x1234u), 0, 0, 1)));
  for (size_t i = 0; i < r.size(); ++i) {
    TEST_ASSERT_FALSE(r.state(r.info_at(i).id)->heard);
  }
}

// Impl Plan 4.2 - "the bench IDs being ordinary entries is itself the test". A bench
// identity and GateLink, given the same frame, end in the same state.
void test_a_bench_identity_is_handled_exactly_like_gatelink() {
  Registry r = loaded();
  const Observed gate  = r.observe(hdr_from(kNodeGateLink, 0x77u), -80, 3, 500);
  const Observed bench = r.observe(hdr_from(kNodeSim2, 0x77u), -80, 3, 500);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(gate), static_cast<int>(bench));

  const NodeState* g = r.state(kNodeGateLink);
  const NodeState* b = r.state(kNodeSim2);
  TEST_ASSERT_EQUAL(g->heard, b->heard);
  TEST_ASSERT_EQUAL_UINT32(g->ctx_id, b->ctx_id);
  TEST_ASSERT_EQUAL_UINT16(g->cmd_seq, b->cmd_seq);
  TEST_ASSERT_EQUAL_UINT32(g->last_seen_ms, b->last_seen_ms);
  TEST_ASSERT_EQUAL_INT16(g->rssi_dbm, b->rssi_dbm);
  TEST_ASSERT_EQUAL_INT8(g->snr_db, b->snr_db);
  TEST_ASSERT_EQUAL_UINT8(g->proto_ver, b->proto_ver);
  TEST_ASSERT_EQUAL_UINT16(g->poll_interval_s, b->poll_interval_s);
}

// ---------------------------------------------------------------------------
// The registry as the ladder's PeerKeys
// ---------------------------------------------------------------------------

// A COMMAND encoded under the W4 key for GateLink verifies through the registry and the
// reference HMAC - the key the registry derived is the key a node would hold.
void test_the_ladder_verifies_a_mac_with_a_registry_key() {
  const Registry r = loaded();
  Counters       c;
  RxLadder       ladder(&c);
  ladder.set_auth(&g_mac, &r);

  Header h;
  h.type   = MsgType::Command;
  h.src    = kNodeGateLink;
  h.dst    = kNodeBridge;
  h.seq    = 1;
  h.ctx_id = 0x01020304u;

  uint8_t payload[8] = {0};
  payload[0]         = static_cast<uint8_t>(Cmd::Open);
  EncodeCtx ectx;
  ectx.mac      = &g_mac;
  ectx.node_key = lran_vectors::kKdf[0].node_key;  // node_key_gatelink
  TEST_ASSERT_EQUAL_UINT8(kNodeGateLink, lran_vectors::kKdf[0].node_id);

  uint8_t buf[kMaxFrame];
  size_t  len = 0;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(encode(h, payload,
                                                fixed_payload_len(MsgType::Command, kSchemaNone),
                                                ectx, buf, kMaxFrame, &len)));
  RxDelivery d;
  TEST_ASSERT_TRUE(ladder.accept(buf, len, 0, &d));
  TEST_ASSERT_TRUE(d.mac_verified);
  TEST_ASSERT_EQUAL_UINT32(0, c.total_dropped());
}

// A provisioned-looking address the table does not carry is refused as unregistered.
void test_the_ladder_refuses_a_source_outside_the_table() {
  const Registry r = loaded();
  Counters       c;
  RxLadder       ladder(&c);
  ladder.set_auth(&g_mac, &r);

  uint8_t   buf[kMaxFrame];
  size_t    len = 0;
  uint8_t   payload[kMaxPayloadPlain] = {0};
  const size_t n = fixed_payload_len(MsgType::Status, kSchemaGateLinkStatusV1);
  RxDelivery d;

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(encode(hdr_from(kNodeSim0, 1), payload, n, EncodeCtx{},
                                                buf, kMaxFrame, &len)));
  TEST_ASSERT_TRUE(ladder.accept(buf, len, 0, &d));

  // 0xF4 is in spec 5.3's bench range but unassigned, so no row provisions it.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Status::Ok),
                        static_cast<int>(encode(hdr_from(0xF4, 1), payload, n, EncodeCtx{}, buf,
                                                kMaxFrame, &len)));
  TEST_ASSERT_FALSE(ladder.accept(buf, len, 1, &d));
  TEST_ASSERT_EQUAL_UINT32(1, c.rx_unknown_src);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_the_table_is_spec_5_3);
  RUN_TEST(test_table_validation_rejects_duplicates_and_reserved_ids);
  RUN_TEST(test_is_bench_is_derived_from_the_address);

  RUN_TEST(test_every_key_matches_its_w4_vector);
  RUN_TEST(test_nothing_is_registered_before_load);
  RUN_TEST(test_an_unregistered_address_has_no_key);

  RUN_TEST(test_an_unheard_node_reads_as_sentinels);
  RUN_TEST(test_a_reception_records_last_seen_radio_and_version);
  RUN_TEST(test_a_new_context_is_learned_and_an_old_one_is_not_relearned);
  RUN_TEST(test_a_zero_context_is_not_adopted);
  RUN_TEST(test_an_observation_touches_only_its_own_entry);
  RUN_TEST(test_an_unregistered_node_changes_nothing);
  RUN_TEST(test_a_bench_identity_is_handled_exactly_like_gatelink);

  RUN_TEST(test_the_ladder_verifies_a_mac_with_a_registry_key);
  RUN_TEST(test_the_ladder_refuses_a_source_outside_the_table);
  return UNITY_END();
}
