# Bridge Node — engineering log

**Dated record, appended to and never rewritten.** Measurements, surprises and the
things that cost an hour. Where this disagrees with a document, the document is the
current statement and this is what was true on the day. Impl Plan §5.3 names this file.

---

## 2026-09-10 — BF-11: task structure, and a rule that does not reach a queue

**`firmware/bridge/` now creates seven FreeRTOS tasks and two queues.** The numbers
Impl Plan §5.2 left as bands are chosen and recorded in §5.2.1; what follows is the
part that is not a table.

**Root rule 4 does not cover a queue overflow, and it should not be stretched to.**
The rule reads: *"Never discard a frame silently. Every discard increments a named
counter and maps to one `Status` value and one §14 stage."* A frame dropped because
the RX queue is full has already passed the whole §14 ladder — it is valid, addressed
to this node and authenticated. There is no stage to map it to and no `Status` that
describes it, and inventing one would put a bridge-local condition into an
enumeration the specification owns and every node shares.

**What the rule is actually protecting is honoured:** each queue carries `sent`,
`dropped` and `high_water` in `QueueAccounting`, published as bridge diagnostics
rather than in schema `0xF0`, which is node health and normative (§14.1). **The
distinction to keep:** `lran::Counters` is the wire's registry; queue statistics are
this firmware's own. Mixing them would make a bridge's software problem read as a
node's link problem on a Home Assistant chart.

**Drop newest, not oldest, and one policy rather than two.** Dropping the oldest is
better for state, which is idempotent — but wrong for events, which are not
interchangeable and drive email and SMS. A single policy that is wrong for events
beats a per-call-site choice made before the publication policy exists; **BF-24 and
BF-25 own the refinement**, with the event dedup rules in hand.

**The never-block rule now has a check rather than a paragraph.**
`tools/checks/lora_task_never_blocks.py` fails if `portMAX_DELAY`, `delay()`, a WiFi
or publish call, or a queue call with a non-zero timeout appears in the code
`lora_task` owns. It reads one function's text, so it is a tripwire on the shape of
the mistake and not a proof — it cannot see into RadioLib, and it cannot follow an
indirect call. **It caught its own false positive during development**: the first
version flagged `xQueueSend(q, &m, 0) != pdTRUE`, which is the exact line it exists
to bless, so the timeout test parses the argument list instead of matching a regex.
The `--self-test` fixtures now include that line.

**The check does not run in CI**, because the bridge is not in CI yet (Bridge
Firmware Tasks §1.2). It is the second thing to add when the workflow is next edited,
after `pio test -d firmware/bridge -e native`.

**Two seams exist deliberately before their consumers do.** `lora_task_idle()`
returns true unconditionally so BF-13's OTA deferral (R-5.3d) has something to defer
against rather than inventing its own; and the publish queue's depth and accounting
are fixed now although its message type arrives with BF-12. Queue boundaries are the
part of a task structure that is expensive to move later, which is BF-11's whole
reason for being an Opus task.

**Stack sizes are guesses and are marked as such.** 4096 words for `lora`, 6144 for
`mqtt` and `app` because ArduinoJson serializes a discovery config on those stacks.
The way to correct one is `uxTaskGetStackHighWaterMark` through the diagnostic
topics — not a number doubled after a crash.

---

## 2026-09-10 — BF-12: WiFi, MQTT, and the bridge enters CI

**The network is configured at boot and connects nowhere.** `net_begin()` sets
credentials and the LWT and returns; association and the broker handshake happen in
`mqtt_task` on its own backoff. **A bridge with no access point still receives LoRa**,
which is R-3.2b's actual requirement and is easy to lose by calling
`WiFi.waitForConnectResult()` in `setup()`.

**Two SDK defaults are turned off deliberately.** `WiFi.persistent(false)` — the
Arduino core otherwise caches credentials in NVS and re-associates from that copy,
which produces a bridge connected to a network the build no longer names, and that
looks like a build that worked. `WiFi.setAutoReconnect(false)` — reconnect cadence
belongs in one place with host tests, not split between our backoff and the SDK's.

**`millis()` wrap is handled by subtraction, not comparison**, in both the WiFi
retry and the broker retry. The wrap is at ~49.7 days; this node is mains-powered
and expected to run for years, so `now >= deadline` stalls the reconnect for the
remainder of the epoch the first time it happens — about seven weeks after
commissioning, which is exactly late enough to be blamed on something else.

**Spec §16.3 is enforced on the path, and twice.** A retained publication on an
`lran/<node>/event/` topic is refused by `make_publish()` and again by the transport
immediately before the wire. **Refused, not corrected**: silently clearing the flag
leaves the caller believing something untrue. The match is on the topic *segment* —
`lran/eventful/gate/state` is not an event topic — because a rule that cannot tell
those apart gets switched off by whoever it first annoys.

**A failed publish loses the message, and that is the choice.** `drain_publish_queue`
stops on the first failure and leaves the rest queued, but the one already dequeued is
gone and counted. Re-queueing it would reorder it behind newer state for the same
entity, which for state is worse than losing it. **BF-25 owns events**, where the
answer is different.

**The bridge is in CI as of this task.** The workflow copies `secrets.h.example`, so
nothing secret enters it; the `native` job builds only the Arduino-free translation
units, and the repo root is off that include path so a host test cannot quietly depend
on `secrets.h`. **What CI cannot cover is the whole of what BF-12 does at runtime** —
the reconnect reconnecting, the LWT landing, a real broker at all. That is B2 and B4
bench work, and the host tests are arithmetic and string handling precisely so the
bench is spent on what only the bench can answer.

**`setBufferSize()` is called as well as the build flag.** `MQTT_MAX_PACKET_SIZE` at
compile time does not reach PubSubClient when the library is built as a separate
archive, which is the shape this trap takes in PlatformIO — and its failure mode is
discovery configs that never appear, with no error pointing anywhere.

---

## 2026-09-10 — BF-13: OTA, and the default that would have defeated rollback

**The A/B partition table would not have been enough, and nothing would have said so
until V-B9.** The prebuilt bootloader for this board has
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` set — confirmed in the installed framework's
`tools/sdk/esp32s3/qio_qspi/include/sdkconfig.h`, the variant this board's `qio` flash
and absent PSRAM select. But Arduino-ESP32 2.0.17's `initArduino()`
(`cores/esp32/esp32-hal-misc.c`) checks a weak `verifyRollbackLater()`, which returns
false, then calls a weak `verifyOta()`, which returns true, and **marks the image valid
before `setup()` runs**. Every image that reaches `initArduino()` is kept. The case that
matters here — an image that boots and never finds the LAN, and so can never be OTA'd
again — is exactly the one it keeps.

**The override needs `extern "C"`, and getting that wrong is silent.** The weak default
is in a `.c` file and no header declares it. A C++ definition is mangled, overrides
nothing, links without a diagnostic, and leaves the core in charge. Checked on the first
build: `nm firmware.elf` shows `T verifyRollbackLater` (ours, strong) beside
`W verifyOta` (the core's, now never consulted). That check is now permanent —
`bridge_partitions.py --elf` in CI's firmware job, with a self-test fixture for the
mangled name `_Z19verifyRollbackLaterv` it must not accept.

**The verdict: all tasks started and the broker connected, after 120 s; otherwise
rolled back at 600 s.** Reachability is the criterion because reachability is what makes
a bad image *recoverable* — a bridge on the broker can be OTA'd again. When the deadline
fires on a good image because the broker was down during the update, the bridge returns
to the previous known-good image, which is the safe direction. The radio joins the
verdict with BF-16.

**Spec §16.2 names `lran/bridge/version` and says nothing about its payload.** The
bridge publishes `{"version","git","slot","ota_state"}` — the last two because they are
what V-B9 reads from Home Assistant after a rollback. **This is a gap in the
specification, not a definition by this firmware**, and it belongs in the next
substantive spec revision alongside whatever discovery (BF-23) needs from the same topic.

**First image: 748 816 bytes, 22.4 % of a 3.2 MB slot.** The CI check fails past 90 %.

**V-B9 has not been run.** Two bad-image environments exist for it — `v_b9_no_network`
tests this firmware's verdict, `v_b9_panic` tests the bootloader — and Impl Plan §6.5.2 is
the procedure. Both are built by CI so the harness cannot rot; neither has been flashed.

---

## 2026-09-10 — BF-14: the status page, and two overruns caught at a desk

**B2's code is complete with this task.** What is left is the bench, and the bench is
waiting on the sandbox broker.

**The page's text is built Arduino-free and held to a character budget per font by a host
test** — ~21 characters for ArialMT_Plain_10 and ~12 for _16 on a 128 px panel. The range
test's panel truncated two strings on hardware (`RESPONDE`) before anyone noticed. **This
one caught two overruns before it was ever flashed:** the footer at the widest possible
uptime (`QUEUE DROPS  up 49710d06h`), which GCC's `-Wformat-truncation` flagged in the same
build, and a three-digit node count (`nodes 254/254`) in the large font. The footer words
were shortened; counts past 99 render as `nodes >99`.

**The widest-uptime test exposed a real defect, not just a long string.** The page took its
uptime from `millis()`, which wraps at ~49.7 days — so the display would have returned to
zero every seven weeks and read as a reboot that never happened. It uses `esp_timer` now.
`millis()` remains correct where it is used for intervals (the reconnect and OTA timers,
which use wrap-safe subtraction).

**Burn-in is designed against.** R-4.1c lets the panel stay on, and it will, for years. An
SSD1306 driven with the same static layout that long keeps a ghost of it. Contrast is 96
rather than the range test's outdoor 255, and every element shifts 0–3 px on a five-minute
cycle, the right-aligned slot label moving opposite so nothing leaves the panel.

**Unknown is `--`.** The node count is not known until BF-15/BF-20, and `nodes 0` would read
as every node down (root rule 6).

**The page has not been seen.** Vext, orientation and whether the fonts measure as the
budgets assume are bench questions, and B2's "OLED shows a status page" is not met until
someone has looked at it.
