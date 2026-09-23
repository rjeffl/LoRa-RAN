# `lran-bridge` — node `0x00`

**Subordinate to `/CLAUDE.md`.** Everything there applies. This file adds only what is
specific to the bridge.

**Primary documents:** `docs/bridge/LRAN-Bridge_Node-PRD` v0.13 (requirements,
`R-*`/`BG-*`/`BS-*`/`V-B*`), `docs/bridge/LRAN-Bridge_Node-Implementation-Plan` v0.47
(build) and `docs/bridge/LRAN-Bridge-Firmware-Tasks` v0.36 (**the `BF-*` task order**).
**Binding protocol:** `docs/shared/LRAN-Protocol-Specification` **v0.13** (`ver = 2`).

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

**`BF-16` — the radio link, host-tested; the radio comes up on the board, no frame on air
yet.** `radio_config.h` (pins, PHY, the EIRP and pin-collision asserts), `rx_ladder.{h,cpp}`
(spec 14 stages 1–10, per-peer reassembly), `lib/lran-link`'s `media_access` (spec 12.3,
shared with the simnode since 2026-09-14), and
`lora_link.{h,cpp}`, **the only file that includes RadioLib**. Impl Plan §5.3.1 records the
choices.

**`BF-15` — the registry, built and host-tested.** `registry.{h,cpp}` (`kNodeTable`, HKDF
keys at load, `is_bench`, the learned fields) and `registry_runtime.{h,cpp}` (the instance,
its mutex, mbedTLS). Impl Plan §4.2.1 records the choices. **Two rules to keep:**
`registry_begin()` runs before `start_tasks()`, because `lora_task` reads keys without a
lock; and **`lora_task` never calls into `registry_runtime`**, which waits on a mutex.
**A frame from an unregistered source is `Status::UnknownSrc`** — spec §14 stage 9a,
counted `rx_unknown_src` in the codec's own `Counters` and summed into `rx_dropped`
(**BF-15a**). The ladder bumps it and returns false; **nothing is sent back**, because
§14.2 answers registered sources only. `unregistered_src` was the pre-v0.12 name for it
and exists nowhere now.

**`BF-17` — the poll scheduler, built and host-tested; no poll on air yet.**
`scheduler.{h,cpp}` decides and `sched_task` sends; Impl Plan §6.1.1. **Three things to
keep:** the scheduler's mutex in `task_runtime.cpp` is **never held across a registry call or
a queue send**, so it never nests with the registry's; a bench row is polled only after it
has been heard; and `lora_task_idle()` is false while a poll is outstanding, which an OTA
upload waits on.

**`BF-20` — the availability watchdog, built and host-tested.** `node_availability.{h,cpp}`
judges and `sched_task` publishes; Impl Plan §6.1.2. **Three things to keep:** the watchdog
belongs to `sched_task` alone, so other tasks reach it only through atomics; it detects a
frame from `NodeState::frames_heard`, never from `missed_polls == 0`; and **a bench node's
availability is not published** until BF-26's `simnode_diag_enable` exists (spec §16.6).
**Do not name a source file `availability.h`**: on macOS's case-insensitive filesystem it
shadows the SDK's `<Availability.h>` and breaks every native build.

**`BF-19` — the diagnostic publication, built and host-tested.** `diag_json.{h,cpp}` formats
and `sched_task` publishes; Impl Plan §4.3.2. **Three things to keep:** counter names come
from `lran::kCounterRegistry` and are never spelled as literals; **read `lora_task`'s
counters only through `lora_diag_snapshot()`**, never field by field; and `sched_task`
publishes from its static `g_sched_msg`, because a `PublishMessage` is ~872 bytes and its
stack is 3072. **`ERROR` replies are BF-19a**, built to spec v0.12 §14.2 and confirmed
on air 2026-09-16 (`error_reply.{h,cpp}`); §14.2's bound 1 stays host-tested, because no
unregistered source has been produced on the bench.

**`BF-18` — the command path and the MQTT receive path, built and confirmed on air
2026-09-16.** `command.{h,cpp}` decides and `sched_task` acts; Impl Plan §6.2.1.
**Four things to keep:**

- **Root rule 2 lives in `command.h`.** A retry reuses the `seq` of the attempt it
  repeats, and there is deliberately **no call on `CommandPath` that advances one**.
  `seq` is allocated by `Registry::take_cmd_seq`, which is also where spec §10.5's wrap
  lives, and the wrap skips `0`.
- **One command is in flight across the fleet.** Not for airtime — because spec §10.3's
  resync resets a node's command `seq` to 1, and a second command in flight during one
  would be refused as a replay and read here as a node fault.
- **The inbound topic's action tokens are this firmware's to choose**, spec §8.1's `cmd`
  names lowercased (`net_policy.h`). They are HA-visible, so **renaming one after B4 is
  breaking**, like every other exact token on this node.
- **An inbound payload is refused, never defaulted to `0`.** `arg` carries `REBOOT`'s
  `0xA5` guard.

**`MqttTransport::set_inbound` is the seam's one concession to PubSubClient**, whose
callback is a bare function pointer with no user context, so the implementation keeps
the sink in a file static. A second `PubSubTransport` is refused at `begin()` rather
than allowed to steal the first's callbacks. The reasoning is written at the
declaration; do not remove it and do not widen the concession.

**`BF-22` — version tolerance, built and on air 2026-09-16.** The ladder accepts
**N and N−1** and refuses N−2; `node_tx_ver()` (`registry.h`) picks the version each node
is addressed in. **Three things to keep:**

- **N−1 only, never best-effort.** Spec §13.2 lets a field change meaning across two
  versions, so parsing N−2 publishes a plausible wrong number. Widening the range is a
  discussion, not a convenience.
- **A node never heard is addressed in N.** It is likelier new than old, and its first
  frame is a `POLL` it answers.
- **R-3.1f's reason is on `diag/state` as `unsupported_ver`, never on the availability
  topic** — spec §16.5 fixes that at `online`/`offline` and HA depends on both tokens.
  The version survives the stage-4 discard because the ladder reads it from the buffer,
  and reaches `sched_task` as one atomic word, since **`lora_task` cannot call
  `registry_runtime`**.

**`BF-27` — the raw frame log, built 2026-09-17; the rest of §6.6 is untouched.**
`frame_log.{h,cpp}` is the ring, `log_task` drains it to serial and to
`lran/bridge/diag/rxlog/state`, and `tools/simctl/rxlog.py` reads it. Impl Plan §6.6.1
records the choices. **Four things to keep:**

- **`log_task` is the only consumer**, because the ring is single-consumer. A second
  reader takes records the first never sees, and nothing will report that it happened.
- **The ring overwrites its oldest record**, against the queues' DropNewest. A log that
  stops recording when the load arrives throws away the passage worth reading.
- **The topic is not retained, and §16.2's table says a `/state` leaf is.** Deliberate,
  and **raised against the specification** rather than settled here: a retained frame log
  replays a finished burst as though it were arriving now.
- **There is no runtime enable and adding one is not a small change.** It needs the
  HA-visible configuration path, whose route is an open operator decision.

**`M25` — the channel monitor, built 2026-09-17.** `chan_monitor.{h,cpp}` has lived in
`lib/lran-link/` since 2026-09-19, shared with the listen-only `firmware/chan-capture/`. The
bridge samples raw RSSI from `lora_task` (~100/s, one per `kLoraMaxWaitMs` wake) and `log_task` writes it to
serial; `tools/simctl/rssi_capture.py` captures a long unattended run and
`rssi_report.py` reads it. **Four things to keep:**

- **Raw RSSI, not CAD, and that is the whole point.** A LoRa CAD detects a LoRa preamble
  at the configured SF, so it cannot see the property's Z-Wave and Insteon FSK at any
  level. Decision Register §3.4 names `cad_backoffs` as the channel's instrument; M25
  exists because it cannot do that job.
- **Two tiers, and the quiet tier is not optional.** `CHAN` lines carry only buckets that
  saw something; `CHANSUM` carries every bucket once a minute. Occupancy is
  `above / samples` and **the samples live in the quiet buckets** — drop them and the
  capture has no denominator.
- **A skipped opportunity is not a quiet one.** The sampler does not look while the radio
  is transmitting or one of our frames is arriving, and `-127.5 dBm` is the encoding's
  rail rather than a reading. All of them are counted as skips.
- **Run a baseline with the simnodes powered down.** The sampler skips a reception only
  after a valid LoRa header, so ~33 ms of each of our own frames' preambles would land in
  the samples as a large excursion.

**`BF-23` — Home Assistant discovery, built and host-tested.** `discovery.{h,cpp}` builds
the configs and `mqtt_task` publishes them, retained, on boot and on every broker
reconnect; Impl Plan §4.4.1. `json_writer.h` is BF-19's JSON writer, lifted out of
`diag_json.cpp` when discovery became its second user. **Four things to keep:**

- **There is no "first time" flag, and adding one would break R-3.3b.** A boot and a
  reconnect take the same path, so the reconnect case cannot be the one that rots.
- **A button's existence is `command_allowed()`'s answer**, never a table per node type.
  The moment a node type gets its own button list, **BG-2** is broken.
- **A bench node produces no discovery** until BF-26 builds `simnode_diag_enable`. Spec
  §16.6, through the same `bench_publication_allowed()` the availability watchdog uses.
- **`ha/discovery/` is generated, and CI checks it.** After any change to a discovery
  table, run `python3 tools/checks/ha_examples.py --write` and commit the diff — it is
  what Home Assistant will see differently.

**`BF-23` — the runtime levers, built and host-tested 2026-09-23; not yet on air.**
`levers.{h,cpp}` carries each bridge row's effective value from `ConfigStore` to the task
that owns its consumer; Impl Plan §4.4.2. **Four things to keep:**

- **Publish after the store changes, never before.** `config_begin()` publishes after the
  NVS restore, and `handle_config_set()` publishes before a node half's job is queued. Move
  either and a reboot, or a set with a node half, leaves a lever on its default.
- **`LeverBoard` has one writer.** `setup()` before the tasks start, then `mqtt_task`.
  A second writer can interleave two publishes under one generation.
- **`lora_configure()` is called from `lora_task` only.** It writes state
  `lora_service()` reads without a lock.
- **A consumer's compile-time default must equal its row's default.** `test_levers` checks
  it. Change one without the other and the bridge runs two values across a boot.

**`BF-32` — the configuration path, built and confirmed on air 2026-09-21.**
`config_json.{h,cpp}` reads spec §16.7.2's payload and writes `config/ack` and
`config/state`; `config_store.{h,cpp}` holds what the bridge owns; `config_path.{h,cpp}`
is the node half's state machine; `nvs_persist.{h,cpp}` is the store behind it (D49).
**Five things to keep:**

- **The topic decides which row a name means.** `cad_retries`, `backoff_max_ms` and
  `frag_reassembly_timeout_ms` are in BOTH blocks of the table, because the bridge and
  every node each have their own. A lookup that searched both would answer the bridge's
  row for a set aimed at a node and the write would land on the wrong radio. `find_param`
  takes a `ConfigScope`; a name the scope does not hold is `unknown_param`, never a
  fall-through.
- **A missing `CONFIG_ACK` is `unknown` and is resolved by READBACK, not retransmission**
  (spec §7.4). This is the opposite of the command path's rule, and it is deliberate: a
  configuration write is not idempotently repeatable, so repeating one cannot tell you
  whether the first took effect.
- **The bridge accepts more than one `CONFIG_ACK` per `seq`** and closes on the message
  whose `MORE_FOLLOWS` is clear (spec §7.4.1, **D57**). Closing on the first strands the
  rest of the answer and reports a configuration it did not finish reading. **Nothing is
  published from an incomplete answer.**
- **A readback REPLACES the state mirror; a set's ACK MERGES into it.** Confusing them
  blanks every row the set did not name, which the bench showed on 2026-09-21.
- **NVS restores through `Store::apply()`**, so a value stored before a range changed is
  clamped on the way back in and a row that has since become `READ_ONLY` is refused.

**`BF-34` — the context roll after a bridge restart, built 2026-09-23 and
confirmed on air the same day.** `context_roll.{h,cpp}` decides and `sched_task` acts; Impl Plan §6.2.2
and spec §10.6. **Four things to keep:**

- **No node leaves the pending state except through a completed roll.** A failed roll
  stays pending and runs again when the node is next heard. Falling back to commanding
  the node is how a command gets acknowledged and never run.
- **The roll claims its `COMMAND_ACK` before the command path sees it**, in
  `cmd_on_ack()`. An ACK neither claims is counted by `CommandPath` alone.
- **The roll and the command path serialize**, because the roll resets the node's `seq`
  space. That is the same reason there is one command in flight.
- **A `config/set` is refused on `mqtt_task`, before either half applies.**
  `config_set_reaches_node()` must agree with `ConfigStore::apply()`'s split, and
  `test_config_store` checks that. `mqtt_task` reads the pending bits from an atomic
  that only `sched_task` writes.

**Still absent: the publication policy** — BF-24. It arrives with its own `BF-*` task; do
not add one early because it is convenient.

**Stack sizes are bytes.** `TaskSpec::stack_bytes` was `stack_words` until BF-16 found
that ESP-IDF counts bytes. Correct a size from `uxTaskGetStackHighWaterMark`, not by
doubling it after a crash.

```bash
pio run  -d firmware/bridge -e heltec            # target build - NEEDS secrets.h
pio test -d firmware/bridge -e native            # host, no secrets
python3 tools/checks/lora_task_never_blocks.py   # the never-block rule, enforced
python3 tools/simctl/test_rxlog_analyze.py       # BF-27's frame-log arithmetic, no board
python3 tools/simctl/test_rssi_analyze.py        # M25's channel-capture arithmetic
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

**`v_b12_blaster` is V-B12's bench image, and it is never deployed either.** It adds
`blaster.{h,cpp}`, a UDP transmitter driven from the USB serial port, which loads WiFi for
the saturated arm (Impl Plan §8.1.2). Outside that environment `blaster.cpp` compiles to
nothing and `loop()` is unchanged.

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
- **PHY parameters ARE runtime-configurable since spec v0.13** (**D56**, §12.4), and the
  six rows are in `/lib/lran-config/`'s table — `READ_ONLY` until **BF-33** builds
  §12.4's commit-and-revert, so Home Assistant can read the working point before it can
  change it. The pin map, TCXO voltage and RF-switch flag stay in the injected radio
  config (§12.2) and are not parameters. **Text saying the PHY belongs nowhere near the
  HA-visible config set is correct for before v0.13**; the hazard it named is real and is
  what the revert window exists for — a node that boots on the wrong channel is a walk to
  the gate with a laptop.
- **`cad_backoffs` counts a *busy* CAD and nothing else** (spec §12.3 — it is the
  channel's instrument, not the radio's). **Read `cad_free` and `rx_deaf_ms` beside it**
  (`rx_deaf.h`): a free CAD takes the radio out of receive and moves `cad_backoffs` not at
  all, so a zero there is not evidence the bridge held receive. M20 sampled 125 kHz every
  200 kHz, so 37.5 % of the band was never looked at.
- **Do not assume frame spacing controls the bench loss rate.** It did on 2026-09-17
  morning and it did not that afternoon: ten bursts put a 250 ms gap at 2.50 % and a
  2000 ms gap at 1.90 %, with every loss falling in four of the ten runs regardless of
  spacing. **Every sweep so far ran one spacing to completion before the next**, so a slow
  change in the environment and an effect of spacing are not separated in any data on
  record. The engineering log's last 2026-09-17 entry has the runs.

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
- **Radio pins come from `RadioPins` in `radio_config.h`, not from `#define`s.** The
  bridge's values are the `LRAN_PROFILE_HELTEC` entry in Impl Plan §10.8.1 — `nss=8
  rst=12 busy=13 dio1=14 sck=9 miso=11 mosi=10`, `rf_sw=kPinNone`, `tcxo_mv=1800`,
  `dio2_as_rf_switch=true`. TCXO is millivolts, not §10.8.1's float, for the range test's
  reason: a float that prints as 1.8 can compare unequal to 1.8f.
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

`main` · `registry` · `scheduler` · `lora_link` (with `rx_ladder`, `radio_config`, and
`lib/lran-link`'s `media_access`) · `mqtt_transport` · `discovery` (with `json_writer`) ·
`publish` · `hex_proxy` · `decode/{gatelink,health,synthetic,welllink}` · `ui` ·
`debug` (BF-27 built its frame-log half as `frame_log`).
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

B2 bring-up and OTA → B3a radio, registry, polling, availability, counters → B3b command
path, version tolerance, scripted catalogue → B4 MQTT/discovery/policy (after B3a) → B5 HEX
proxy → B6 GateLink integration → B7 soak. B3 was split into B3a/B3b on 2026-09-14 (Impl
Plan v0.32). B1a/B1b (range) are done. B3a depends on simnode B0,
which depends on protocol library **P6 and P8** — both met; P8 (`CommandGate`, D34) landed
2026-09-11. Impl Plan §8 gated B0 on P6 alone until an audit corrected it.

**Test the OTA rollback with a deliberately bad image.** An untested rollback is not a
rollback, and this is the one node where losing it costs the whole property's telemetry.
