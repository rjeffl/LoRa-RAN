# LRAN `config/set` and `/lib/lran-config/` — decision brief

**Document:** `LRAN-Config-Set-Brief`
**Version:** 0.1
**Status:** **Open.** The route is decided; the eight questions in §3 are not. The Decision
Register records nothing from this brief until the operator answers them
**Parent document:** [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md)
**Binding protocol:** [`LRAN-Protocol-Specification`](./LRAN-Protocol-Specification.md) **v0.12** (`ver = 2`)
**Decision status:** [`LRAN-Decision-Register`](./LRAN-Decision-Register.md) — **D42** deferred
`config/set` and `config/ack` until they had a caller; they now have one
**Last updated:** 2026-09-19

> **The operator chose the general `config/set` on 2026-09-19**, with `/lib/lran-config/`
> behind it, over a narrow single-purpose topic and a serial-only lever. The 2026-09-17
> session brief set out the three routes. The general route is what System PRD §9.4 and
> spec §7.4 intended from the start. This brief collects what has to be settled before the
> first line of it is written: five stale passages the specification already carries, and
> eight design questions, each with a recommendation.

---

## 1. The short answer

**Nothing here changes a frame layout.** `CONFIG` and `CONFIG_ACK` are specified in §7.4.
What is missing is everything on the Home Assistant side of the bridge, and a parameter
table for either side to read. `ver` stays `2`.

**D42's reason for waiting no longer holds.** D42 left `config/set` and `config/ack`
undefined because neither had *"an implementation, a configuration library or an inbound
path."* BF-18 built the inbound path. The bridge now carries eight runtime values that its
`TODO(BF-23)` and `TODO(BF-26)` markers say are to be set from HA (§3.7), so the interface has
callers to be designed against.

| # | Question | Blocks | Recommendation | § |
|---|---|---|---|---|
| 1 | Is the parameter table generated, or hand-written? The documents disagree | everything | A hand-written C++ table is the one source; everything else is derived from it by code | [§3.1](#31-question-1--generated-or-hand-written) |
| 2 | Two of the three read paths have no reply in the spec, and the simnode filled the gap itself | GateLink, the readback rule | Specify what the simnode does: an unsolicited `CONFIG_ACK` with `op` = `GET_ALL` | [§3.2](#32-question-2--three-ways-to-read-a-configuration) |
| 3 | Is `param_id` one namespace for the fleet, and whose schema is `0x12`? | the table | One namespace, a block per owner; `0x12` becomes the node config schema | [§3.3](#33-question-3--one-param_id-namespace) |
| 4 | Where a parameter lives: the bridge, or the node | `config/set` routing | Each parameter declares its owner; the bridge splits a set and merges the results | [§3.4](#34-question-4--the-bridge-holds-some-of-a-nodes-parameters) |
| 5 | The `config/set`, `config/ack` and `config/state` payloads | BF-23, BF-26 | One JSON topic per node, keyed by parameter name | [§3.5](#35-question-5--the-mqtt-payloads) |
| 6 | What `persist_status` means on a node with no microSD slot | the bridge's parameters | NVS on the bridge; §8.11 says "nonvolatile store" | [§3.6](#36-question-6--persistence-on-the-bridge) |
| 7 | The bridge's parameter names and IDs | HA entity IDs | Inventory from the firmware, not from Library Plan §4's sketch | [§3.7](#37-question-7--the-bridges-parameter-list) |
| 8 | No task owns any of this | scheduling | New **BF-32**; BF-23's lever and BF-26 consume it | [§3.8](#38-question-8--which-task-owns-it) |

---

## 2. Five passages v0.12 left stale

**v0.12 made `CONFIG` and `CONFIG_ACK` single-frame (D38) and corrected §11.4 and §11.5.
Five other passages still describe them as fragmented.** None is a decision. Each is a
sentence that contradicts §11.4, and a reader who starts at the wrong one will build the
wrong thing.

| # | Where | What it says | Why it is wrong |
|---|---|---|---|
| C1 | Spec §7.4, *"A full parameter set does not fit one frame"* | Full-set push and readback are *"the first fragmented frames the system is expected to produce. Both types are fragmentable; see §11.4."* | §11.4 says a sender MUST NOT fragment either type, and a receiver discards one with `ERROR(BAD_LENGTH)` |
| C2 | Spec §7.4, *"A lost `CONFIG_ACK` fragment is recovered by readback"* | The rule is framed around a lost fragment | The rule still holds, for a lost single-frame `CONFIG_ACK`. The framing needs rewording, not the rule |
| C3 | Spec §6.6.2 | *"§11.5 records why that matters now … `CONFIG_ACK` is expected to cross the single-frame boundary on GateLink"* | §11.5 now records the opposite |
| C4 | Spec §9.4 | An unfragmented frame is *"every authenticated frame except a multi-fragment `CONFIG`"* | No multi-fragment `CONFIG` exists in v1. Every authenticated frame is unfragmented |
| C5 | Bridge Impl Plan §10.5, the `single_frame_interleave` explanation | *"a node's periodic `STATUS` destroys that same node's in-progress fragmented `CONFIG_ACK`"* | The test is still right; its example is now an impossible frame. `PING` is the only fragmentable type |

**Dated notes are left alone.** §14's *"Added in v0.6"* note describes the same defect
in `CONFIG_ACK` terms, and it records what was true when it was written.

**Nothing caught these because nothing reads a specification against itself.**
`spec_citation_version.py` checks that documents cite the current version, not that
the current version agrees with itself.

---

## 3. The questions

### 3.1 Question 1 — generated, or hand-written?

**The documents disagree.** System PRD §9.4 and spec §7.4 say `/lib/lran-config/`
declares each parameter once and that firmware defaults, HA discovery payloads and
`/docs/gatelink-config.md` are **generated** from it. Protocol Library Plan §4 (v0.9) says
*"Hand-written C++ headers … No generator, no YAML,"* and accepts that discovery payloads
and the GateLink document are then *"maintained by hand against this header."*

**BF-23 has since dissolved most of the conflict.** Discovery is built by
`discovery.cpp` at runtime, and `tools/ha/dump_discovery.cpp` links that same file to
produce the `/ha/` examples, which `tools/checks/ha_examples.py` diffs in CI. A
hand-written C++ table can therefore feed discovery directly, with no second copy and no
YAML.

**Recommendation.** The C++ table is the one source. Firmware defaults and HA `number`
discovery read it at build and run time. A host tool built the same way as
`dump_discovery.cpp` writes `/docs/gatelink-config.md`, and a check diffs it. Amend
Library Plan §4 to drop *"maintained by hand"*; amend PRD §9.4 and spec §7.4 to say
*derived by code from* rather than *generated from*, so neither implies a generator the
project decided against.

### 3.2 Question 2 — three ways to read a configuration

**The specification has three ways to ask a node for its configuration, and says what
the node sends back for only one of them.**

| Path | Where | Reply defined in the spec? |
|---|---|---|
| `CONFIG` with `op` = `GET` or `GET_ALL` | §7.4, §8.10 | **Yes.** `CONFIG_ACK`, correlated by `seq` |
| `POLL` with `poll_flags` bit 1, *"request config readback"* | §6.4 | **No.** §7.4 relies on it for the outcome-unknown case, but no section says what the node transmits in answer |
| `COMMAND` `REQUEST_CONFIG` (`0x11`) | §8.1 | **No.** It gets a `COMMAND_ACK`, and nothing says what follows. BF-23's discovery already publishes it as a button on GateLink and WellLink |

**The simnode answers both undefined paths the same way**, and Bridge Impl Plan §10.9.2 records
it: *"`REQUEST_STATUS` and `REQUEST_CONFIG` follow their ACK with a status or a
readback."* The readback is a `CONFIG_ACK` with `op` = `GET_ALL`, sent on the node's own
`seq` (`send_config_readback()`, `firmware/simnode/src/gatelink.cpp`). A `POLL` with bit 1
set gets the same `CONFIG_ACK` after its `STATUS`.

**That reply contradicts §9.2.** Its table says `CONFIG_ACK` needs no MAC because it is
*"correlated to an authenticated request by `seq`."* An unsolicited readback answers a
`POLL`, which is not authenticated, or follows a `COMMAND_ACK`, and carries a `seq` that
matches no request. The simnode is filling a specification gap locally, which is the thing
the root `CLAUDE.md` asks to have raised.

**Recommendation: specify what the simnode does.** An unsolicited `CONFIG_ACK` with
`op` = `GET_ALL` is the answer to `POLL` bit 1 and to `REQUEST_CONFIG`, sent on the node's
own `seq`. §9.2's row gains a second clause: *or an unsolicited `GET_ALL` readback, which
is unauthenticated for the same reason `STATUS` is.* A spoofed readback misreports
configuration to HA the way a spoofed `STATUS` misreports state, and §9.5 already accepts
that nuisance. `CONFIG` `GET` and `GET_ALL` stay as the authenticated read.

**The alternative** is to keep only `CONFIG` `GET_ALL`, return `poll_flags` bit 1 to
reserved, and redefine `REQUEST_CONFIG`. That changes working simnode code and an HA
button already in discovery, and costs an authenticated frame for every readback that a
1-byte `POLL` gets today.

### 3.3 Question 3 — one `param_id` namespace

**Schema `0x12` is registered as *"GateLink config v1"*,** yet Library Plan §4 already
puts bridge parameters at `0x0001` and parameters common to every node at `0x0100` in one
table. WellLink will need configuration too, and §7.1 reserves it no config schema.

**Recommendation.** `param_id` is one namespace for the fleet, allocated in blocks:

| Block | Owner |
|---|---|
| `0x0000`–`0x00FF` | the bridge |
| `0x0100`–`0x01FF` | every node (`dedup_cache_depth`, `frag_reassembly_timeout_ms`) |
| `0x1000`–`0x1FFF` | GateLink |
| `0x2000`–`0x2FFF` | WellLink |

`0x12` keeps its value and becomes *"node config v1,"* carried by any node. A node answers
a `param_id` outside its own blocks with `UNKNOWN_PARAM`, as §7.4 already requires. **This
renames a schema without changing a byte**, so it needs no W4 regeneration.

### 3.4 Question 4 — the bridge holds some of a node's parameters

**Some parameters about a node are held by the bridge, not the node.** BG-4 makes each
node's poll interval runtime-configurable, and `registry.h` already keeps
`poll_interval_s` per node on the bridge, and setting it sends nothing over LoRa. Library Plan §4 lists `poll_interval_s` once, as a bridge
parameter, which would give every node one interval.

**Recommendation.** Each parameter in the table declares an owner and a scope:

| Owner | Scope | Example | Applied by |
|---|---|---|---|
| bridge | global | `g_diag_interval_s`, `simnode_diag_enable`, `missed_poll_threshold` | the bridge, on `lran/bridge/config/set` |
| bridge | per node | `poll_interval_s` | the bridge, on `lran/<node>/config/set` |
| node | — | `dedup_cache_depth`, GateLink's timings | the node, via `CONFIG` |

A set on `lran/<node>/config/set` that names both kinds is split by the bridge. It applies
the bridge-held half itself, sends the node-held half as `CONFIG`, and publishes one
`config/ack` once both halves have an outcome. `config/state` shows all of them together,
so HA sees one device with one configuration.

### 3.5 Question 5 — the MQTT payloads

**Two shapes are possible.** A single JSON topic per node, which §16.2 already names; or a
topic per parameter, which is what an HA `number` entity publishes to by default.

**Recommendation: the single topic, keyed by parameter name.** Names, not `param_id`s:
the ID is a wire detail, and the name is already the HA `object_id` (Library Plan §4).
HA's MQTT `number` entity can publish into a shared topic through a `command_template`;
**verify that against the HA documentation before the spec text is written.**

```json
// lran/<node>/config/set                      HA -> bridge, not retained
{"set": {"poll_interval_s": 120}}
{"op": "restore_defaults"}
{"op": "get_all"}

// lran/<node>/config/ack                      bridge -> HA, not retained
{"op": "set", "persist": "applied_not_persisted",
 "results": {"poll_interval_s": {"status": "ok", "value": 120}}}

// lran/<node>/config/state                    bridge -> HA, retained
{"poll_interval_s": {"value": 120, "source": "override"},
 "missed_poll_threshold": {"value": 3, "source": "default"}}
```

Three rules carry over from §7.4 and must survive into the JSON:

- **Per-entry results.** An unknown name gets `"status": "unknown_param"`; the rest apply.
- **Effective values.** `value` is what took effect, and a clamp says `"clamped"`.
- **Honest persistence.** `persist` is one of `persisted`, `applied_not_persisted`,
  `not_applied` or `unknown`. `unknown` is new, for §7.4's case of a `CONFIG` that got no
  `CONFIG_ACK`. The bridge resolves it with a readback (§3.2) and publishes a second
  `config/ack` once it knows.

### 3.6 Question 6 — persistence on the bridge

**§8.11 defines `APPLIED_NOT_PERSISTED` as *"no usable microSD"*.** The bridge has no SD
slot; it has NVS. Spec §16.6 already says bridge parameters carry *"the same per-entry ACK
and `persist_status` semantics as any node parameter."*

**Recommendation.** The bridge persists to NVS. §8.11 reads *"no usable nonvolatile
store"*, which is microSD on GateLink and NVS on the bridge. The bridge reports
`APPLIED_NOT_PERSISTED` only when an NVS write fails.

### 3.7 Question 7 — the bridge's parameter list

**Library Plan §4's `kBridgeParams` is a sketch written before the bridge existed, and it
no longer matches the firmware.** It names `command_ack_timeout_ms` and
`republish_interval_s`, and a single global `poll_interval_s`. The firmware's runtime values are below. All but
`missed_poll_threshold` carry a `TODO(BF-23)` or `TODO(BF-26)` marker, or sit behind
`lora_configure()` or `lora_configure_errors()`, whose comments say only the defaults are
in effect until BF-23:

| Value today | Where | In §4's sketch? |
|---|---|---|
| `poll_interval_s`, per node | `registry.h` | as a global |
| `kPollReplyTimeoutDefaultMs` (10 s) | `scheduler.h` | no |
| `g_diag_interval_s` | `task_runtime.cpp` | no |
| `simnode_diag_enable` | `task_runtime.cpp` | yes |
| `missed_poll_threshold`, one for every node | `node_availability.h` | yes |
| `cad_retries`, `backoff_max_ms` | `lora_link.h`, `lora_configure()` | no |
| `frag_reassembly_timeout_ms` | `lora_link.h` | as a node parameter only |
| `error_min_interval_ms` | `lora_link.h`, `lora_configure_errors()` | no |

**Names are the one thing this interface cannot take back.** A name becomes an HA
`object_id`, and renaming it later orphans the entity — the same argument the 2026-09-17
brief made against the narrow topic. **Recommendation:** fix the bridge's list and names in
this brief's successor, before BF-32 codes them, and leave GateLink's list to its own
milestone under **W10**.

### 3.8 Question 8 — which task owns it

**No `BF-*` row names `/lib/lran-config/` or a `config/set` subscriber.** BF-23's lever
half and BF-26 both wait on it.

**Recommendation.** A new **BF-32** in `LRAN-Bridge-Firmware-Tasks`: the library, the
bridge's table, NVS persistence, the `config/set` subscriber, and `config/ack` and
`config/state` publication. BF-23's lever half then becomes *"`g_diag_interval_s` reads the
table"*, and BF-26 becomes *"`simnode_diag_enable` reads the table."* The library half is
host-tested in the `native` environment, like `/lib/lran-link/`, and needs no board.

---

## 4. Suggested order

1. **The operator answers §3's eight questions.** They become **D43** (the route) and
   **D44** onward in the Decision Register.
2. **Spec v0.13, on its own branch.** The five corrections in §2, the answers to
   Questions 2, 3, 5 and 6, and the citation sweep a version bump carries — about 27
   files at v0.12. `ver` stays `2`; no W4 vector changes.
3. **`b4-lran-config`, stacked on it.** BF-32, then BF-23's lever half and BF-26.

**Expect more of §2.** Each of its five passages was found while reading for something
else. The v0.13 branch should read every section that names `CONFIG`, `CONFIG_ACK`,
`param_id` or `persist_status`, not only the ones listed here.
