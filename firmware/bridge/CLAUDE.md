# `lran-bridge` — node `0x00`

**Subordinate to `/CLAUDE.md`.** Everything there applies. This file adds only what is
specific to the bridge.

**Primary documents:** `docs/bridge/LRAN-Bridge_Node-PRD` v0.11 (requirements,
`R-*`/`BG-*`/`BS-*`/`V-B*`), `docs/bridge/LRAN-Bridge_Node-Implementation-Plan` v0.22
(build) and `docs/bridge/LRAN-Bridge-Firmware-Tasks` v0.10 (**the `BF-*` task order**).
**Binding protocol:** `docs/shared/LRAN-Protocol-Specification` **v0.11** (`ver = 2`).

**Hardware:** Heltec WiFi LoRa 32 V3. No hardware build — firmware, antenna and siting
only. **The antenna is decided and is not a choice to revisit here:** the same 3.0 dBi
19 cm stick the range test ran on (PRD **R-4.3a.1**). Its gain is a term in D1's EIRP
arithmetic, not a note about a part.

**Prose:** root `## Writing` — use the `nbj-write-clearly` skill. The target-specific
trap: **MQTT topics, discovery keys and the §14.1 counter names are exact tokens**, and
they are the interface Home Assistant sees. A topic or counter renamed for readability in
a document is a topic that no longer matches the spec, which owns both (see **Counter
names come from spec §14.1** below).

## What exists here today

**`BF-10` to `BF-14` — B2's code, complete.**
`platformio.ini` (`heltec` and `native`), `main.cpp` (banner, placeholder-key check,
network config, task start), `tasks.{h,cpp}` and `queues.{h,cpp}`, `task_runtime.{h,cpp}`
(every FreeRTOS call), `net_policy.{h,cpp}` (backoff, topic grammar, the retain rule),
`wifi_link.{h,cpp}`, `mqtt_transport.{h,cpp}` (the seam), `mqtt_pubsub.{h,cpp}` (D5's
first implementation), `ota_policy.{h,cpp}` (the rollback verdict, host-tested),
`ota.{h,cpp}`, `partitions.csv`, `status_page.{h,cpp}` (what the OLED says, host-tested),
`ui.{h,cpp}` and `board_ui.h`.

**`BF-16` — the radio link, built and host-tested, not yet on air.** `radio_config.h`
(pins, PHY, the EIRP and pin-collision asserts), `rx_ladder.{h,cpp}` (spec 14 stages 1–10,
per-peer reassembly), `media_access.{h,cpp}` (spec 12.3), and `lora_link.{h,cpp}`, **the
only file that includes RadioLib**. Impl Plan §5.3.1 records the choices.

**Still absent: the registry, discovery and the publication policy** — B3 and B4. Each
arrives with its own `BF-*` task; do not add one early because it is convenient.

**Stack sizes are bytes.** `TaskSpec::stack_bytes` was `stack_words` until BF-16 found
that ESP-IDF counts bytes. Correct a size from `uxTaskGetStackHighWaterMark`, not by
doubling it after a crash.

```bash
pio run  -d firmware/bridge -e heltec            # target build - NEEDS secrets.h
pio test -d firmware/bridge -e native            # host, no secrets
python3 tools/checks/lora_task_never_blocks.py   # the never-block rule, enforced
```

**All of it runs in CI** (Bridge Firmware Tasks §1.2), plus `bridge_partitions.py` on the
table, the built image and its symbol table. The workflow copies `secrets.h.example` for
the target build, so nothing secret is in it.

## OTA — two things that are easy to break and silent when broken

- **Rollback depends on `extern "C" bool verifyRollbackLater()` in `ota.cpp`.** Without
  it, Arduino-ESP32 marks every image valid before `setup()` runs. A C++ definition links
  cleanly and overrides nothing; CI's `bridge_partitions.py --elf` fails unless the symbol
  is strong. **Do not remove the `extern "C"`, and do not move the definition into a
  library** — an archive member nothing references is not linked, and the weak default
  wins again.
- **`partitions.csv` cannot change on a deployed bridge without USB** (Impl Plan §6.5).
  It is committed rather than taken from the board definition for that reason. Grow the
  image, not the table.

**V-B9 was run on the bridge board on 2026-09-13 and passed** (engineering log), and **is
owed again since BF-16 added `radio_ok` to the verdict**. **Re-run
Impl Plan §6.5.2 after any change to `ota.cpp`, `ota_policy.cpp`, `partitions.csv` or the
Arduino-ESP32 version**; CI's symbol check catches a lost `extern "C"`, but only a board
proves a rollback. The two bad-image environments, `v_b9_no_network` and `v_b9_panic`,
exist for it and are never a production build.

## Two network rules that are enforced, not remembered

- **Event topics are never retained** (spec §16.3). `make_publish()` refuses a retained
  publication on `lran/<node>/event/`, and the transport refuses it again before the
  wire. **Refused, not silently corrected** — a caller that set the flag believes
  something untrue. These events drive email and SMS; a retained one replays on every HA
  restart.
- **Nothing is truncated.** An oversized topic or payload is refused and counted.
  Truncated JSON is worse than absent: HA logs a parse error against a topic that looks
  alive while the entity keeps a stale value.

**Credentials live in `main.cpp` and nowhere else.** Every other file takes what it needs
as an argument. The SSID and broker address are printed at boot because they make a
failure diagnosable; **no password, no key, is printed anywhere in this firmware.**

**The Arduino-free/Arduino split is load-bearing, and `build_src_filter` in
`[env:native]` is where it is declared.** `tasks.cpp` and `queues.cpp` build on the host
so §5.2's rules can be *asserted*; a file that needs to move into that group has to be
added to the filter, which is a visible edit. Keep new policy on the host side.

## The never-block rule is checked, not just stated

**`lora_task` is highest priority and never blocks on the network** — and there is no
blocking queue send in `task_runtime.h` to reach for. Every send is zero-tick and counts
its drop. `tools/checks/lora_task_never_blocks.py` fails on `portMAX_DELAY`, `delay()`, a
WiFi or publish call, or a queue call with a non-zero timeout in the code `lora_task`
owns. **It reads one function's text** — a tripwire on the shape of the mistake, not a
proof, and it cannot see into RadioLib.

**A full queue drops the newest item and counts it** (`QueueAccounting`). Those counters
are **bridge diagnostics and not schema `0xF0`**: §14.1 is the wire's normative registry
of receive-ladder discards, and a queue overflow happens *after* a frame has passed the
whole ladder. Root rule 4 is honoured in substance — no silent discard — and the
engineering log's 2026-09-10 entry says why it is not stretched further.

**`secrets.h` is required to build the target, and it is gitignored.** Copy
`secrets.h.example` from the repo root and fill it in; the build fails with a message
naming that step rather than a file-not-found. **The template's `LRAN_MASTER_KEY` is 32
zero bytes and it compiles**, so `main.cpp` checks at boot and says so loudly on a
placeholder build — a build that cannot authenticate anything must not look healthy in a
log. Never commit, echo or log the real values.

## The PHY is fixed — D1, closed 2026-09-10

**917.4 MHz, SF9, BW 125 kHz, CR 4/5, −4 dBm conducted** with the 3.0 dBi antenna, under
§15.249 Envelope A. Protocol Spec §12.1 states them; Decision Register §3.4 records why.

- **`backoff_max_ms` defaults to 1500**, not the 500 older material shows. A maximum
  `PING` at SF9 runs 1107 ms and a window shorter than the frame cannot outlast it.
- **PHY parameters are not runtime-configurable** (§12.1). They belong in the injected
  radio config beside the pin map, never in the HA-visible config set — a node that boots
  on the wrong channel is a walk to the gate with a laptop.
- **`cad_backoffs` is the instrument to watch** once frames are moving. M20 sampled
  125 kHz every 200 kHz, so 37.5 % of the band was never looked at.

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
B6 GateLink integration → B7 soak. B1a/B1b (range) are done. B3 depends on simnode B0,
which depends on protocol library **P6 and P8** — both met; P8 (`CommandGate`, D34) landed
2026-09-11. Impl Plan §8 gated B0 on P6 alone until an audit corrected it.

**Test the OTA rollback with a deliberately bad image.** An untested rollback is not a
rollback, and this is the one node where losing it costs the whole property's telemetry.
