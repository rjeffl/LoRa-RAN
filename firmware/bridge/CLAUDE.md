# `lran-bridge` — node `0x00`

**Subordinate to `/CLAUDE.md`.** Everything there applies. This file adds only what is
specific to the bridge.

**Primary documents:** `docs/bridge/LRAN-Bridge_Node-PRD` v0.2 (requirements,
`R-*`/`BG-*`/`BS-*`/`V-B*`) and `docs/bridge/LRAN-Bridge_Node-Implementation-Plan` v0.7
(build). **Binding protocol:** `docs/shared/LRAN-Protocol-Specification` **v0.11**
(`ver = 2`).

**Hardware:** Heltec WiFi LoRa 32 V3. No hardware build — firmware, antenna and siting
only.

**Prose:** root `## Writing` — use the `nbj-write-clearly` skill. The target-specific
trap: **MQTT topics, discovery keys and the §14.1 counter names are exact tokens**, and
they are the interface Home Assistant sees. A topic or counter renamed for readability in
a document is a topic that no longer matches the spec, which owns both (see **Counter
names come from spec §14.1** below).

## Three properties that must survive every change

1. **No gate knowledge anywhere.** The bridge decodes GateLink's schemas because they are
   registered in a table, not because any code path knows what a gate is. The moment
   `if (node == gatelink)` appears outside the registry, **BG-2** is broken. Adding a node
   type is: a registry entry, a decoder, a discovery template. If it also requires touching
   the scheduler, the availability watchdog or the MQTT layer, the abstraction has leaked.
2. **`lora_task` is highest priority and never blocks on the network.** A status frame
   arriving during a WiFi or broker outage must still be received and queued. Publication
   is queued, never inline. This is the one place a naive "publish on receive" quietly
   loses data.
3. **Never vary `seq` on retry** (root rule 2, **BS-3**). Repeated here because this is
   the file where the retry loop lives.

## Board gotchas

- **RadioLib, version pinned in `platformio.ini`** (**D32**). Every firmware in the repo
  uses the same driver; letting the version float in one of four is how a fleet-wide
  regression arrives without a commit to blame.
- **Radio pins come from `RadioPins`, not from `#define`s.** The bridge's values are the
  `LRAN_PROFILE_HELTEC` entry in Impl Plan §10.8.1 — `nss=8 rst=12 busy=13 dio1=14
  sck=9 miso=11 mosi=10`, `rf_sw=RADIOLIB_NC`, TCXO `1.8f`, `dio2_as_rf_switch=true`.
  §10.8.1 is the only home for these; if they need correcting, correct them there.
  R-4.1b exists because GateLink's carrier shares none of these numbers.
- **TCXO is 1.8 V**, not the 3.3 V some libraries default to. Wrong value presents as a
  radio that will not calibrate, not as an obvious error.
- **OLED sits behind Vext.** Enable Vext before init. A dark display on boot is usually
  Vext, not the driver.
- **`MQTT_MAX_PACKET_SIZE` defaults to 256.** Discovery configs exceed it and fail with no
  error pointing at the cause. Set it to ≥ 1024 **in the build flags on day one** — this is
  the single most likely early time-sink on this node.

## Structure

`main` · `registry` · `scheduler` · `lora_link` · `mqtt_transport` · `discovery` ·
`publish` · `hex_proxy` · `decode/{gatelink,health,synthetic,welllink}` · `ui` · `debug`.
Task ownership is in Impl Plan §5.2/§5.3.

`MqttTransport` is an interface; PubSubClient is the first implementation. Keep the seam —
espMqttClient is the designated fallback.

## Publication rules

Publish on change for jittery values; staleness and sentinels map to `unavailable`, never
to a cached number; synthetic data stays marked; **events are never retained**, QoS 1,
deduplicated on `(src, ctx_id, event_id)`. Held-open and FIRE events drive email and SMS —
a retained event replays on HA restart and produces a 2 AM notification about something
that happened last week.

Bench nodes (`0xF0`–`0xFE`) are **received, authenticated, decoded and counted like any
other node**, then gated at publication only: diagnostic topics, `simnode_diag_enable`
default off, and never on `event` topics. Gating at the radio instead would mean bench
nodes exercise a different code path from real ones, which defeats having them.

## Development environment

Dev HA VM and dev broker until B6; production only after discovery payloads are stable.
HA's entity registry remembers every `unique_id` it has ever seen, and a retained
discovery config survives a reflash. Broker address lives in `secrets.h`.

## Counter names come from spec §14.1

§14.1 is a **normative registry** with a wire-code column, and the bridge publishes these
names to MQTT where Home Assistant will chart them — a rename after that is breaking.
Take the names from §14.1, not from the Impl Plan §10.5 fault catalogue, which predates
the registry and does not cover stages 2a, 5b or 8a. **`rx_dropped` is the sum of the
counters §14.1 marks `yes` and only those**: `rx_frag_duplicate`, `rx_frag_late` and
`rx_dup_command` are normal traffic and must not make a health metric climb during
correct operation.

Name internal identifiers after the **wire code** (§14.1's SHOULD). `REJECTED_CTX`, not
`CtxMismatch` — the wire code is the name that cannot be changed later.

## `CommandGate` exists, and on the bridge it does almost nothing

**D34** puts spec §9.4 steps 4–5 in `/lib/lran-protocol/` as `CommandGate`, one per peer,
immediately after `Reassembler`. Wire it in per node — but know what to expect: **§9.2
makes every authenticated type bridge → node**, so the bridge receives no authenticated
frames today and steps 4–6 apply to an empty set here. `rx_rejected_seq` and
`rx_dup_command` staying at zero on the bridge is **correct**, not a wiring bug.

The counters are still published per node, because §14.1 requires it and because the day a
node-originated authenticated type appears, the path must already exist. Status `seq` is
**advisory and MUST NOT reject** (§10.2) — do not route status frames through the gate.

## Milestones

B2 bring-up and OTA → B3 protocol and registry → B4 MQTT/discovery/policy → B5 HEX proxy →
B6 GateLink integration → B7 soak. B1a/B1b (range) are independent. B3 depends on simnode
B0, which depends on protocol library P6.

**Test the OTA rollback with a deliberately bad image.** An untested rollback is not a
rollback, and this is the one node where losing it costs the whole property's telemetry.
