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

---

## 2026-09-13 — B2 bench session: every criterion met, V-B9 run

**Every B2 acceptance criterion in Impl Plan §8 was met on the flat-case Heltec**, the
first time any board has run bridge firmware. V-B9 passed all four steps of §6.5.2. The
operator identified the board by its enclosure and confirmed it was the only Heltec on
USB (`/dev/cu.usbserial-0001`). What it ran before the USB flash was not read, so
whether it held the range-test build or P8's Unity image stays unrecorded. Before the
flash, the bridge host suites, the `heltec` target build, `lora_task_never_blocks.py`
and `bridge_partitions.py` all passed on the macOS build machine.

**The first boot failed on `secrets.h`, twice over, and the firmware reported it well.**
Every WiFi attempt ended in `Reason: 15 - 4WAY_HANDSHAKE_TIMEOUT`, the signature of a
wrong passphrase. The broker was also set to a Tailscale address (100.64.0.0/10), which
the build machine reaches over its tunnel and the bridge cannot reach at all. The
operator corrected both. While WiFi was failing, the bridge kept running, and the gaps
between attempts grew to 8, 16 and 30 s, then held at 30 s.

**The serial log says nothing when WiFi or MQTT connects or drops.** Only the OLED shows
the state (`MQTT up` / `MQTT --`), and the broker side had to be read with
`mosquitto_sub`. The reconnect results below are readable from the log only through
core warnings and uptime continuity. One log line per network state change would make
a remote B7 soak diagnosable from a capture. This is a proposal, not a change made here.

**The sandbox broker refuses anonymous clients** (`CONNACK 5`). A bench subscription
needs the sandbox user's credentials, so an assistant cannot watch the broker without
the operator.

### Results

| Criterion | Evidence |
|---|---|
| **OLED shows a status page** | Operator confirmed the page showed as expected, with no change needed. After V-B9 step 2 its footer read `up 2m` and `0.1.1` |
| **MQTT connects, LWT registered** | Broker showed `lran/bridge/availability online` and `lran/bridge/version {"version":"0.1.0","git":"9893d86","slot":"app0","ota_state":"verified_or_usb"}`. **LWT fired** on USB unplug: `offline` at 11:38:04 (unplug time not recorded) |
| **MQTT reconnects, no reboot** | Broker restarted: `offline` at 11:39:25; the bridge logged `Connection reset by peer` at +1, +2, +4 and +8 s and was `online` at 11:39:57, on the next retry 16 s later. No banner, so no reboot |
| **WiFi reconnects, no reboot** | Drop at the access point, 11:44:23: `Reason: 6 - NOT_AUTHED`, then `Reason: 202 - AUTH_FAIL` at +1, +2, +4 and +8 s. **LWT fired** 19 s after the drop (11:44:42). `online` again at 11:44:54 with the uptime counter continuous past 363 s |
| **Version published** | As above, retained, republished on every connect |
| **A/B partitioning** | `bridge_partitions.py`: two equal OTA slots of `0x330000`. Image 784 816 bytes, 23.5 % of a slot |
| **OTA succeeds** | V-B9 step 2 |
| **A bad image rolls back** | V-B9 steps 3 and 4 |

After replugging, the bridge was `online` 2 s after its boot banner.

### V-B9, banner lines verbatim

Step 2's image was `custom_bridge_version = 0.1.1` as a local, uncommitted edit, hence
`-dirty`. **The two bad images built from the same tree also print `0.1.1`**, so `Slot:`,
the V-B9 banner and the returning `WiFi SSID:` line are what tell the images apart, not
the version. §6.5.2 should give the bad images a version of their own.

**Step 1, USB flash:**
```
Version: 0.1.0 (9893d86)
Slot: app0
Image state: not_pending
```

**Step 2, OTA of a second good build.** The first attempt failed; the retry passed.
```
11:48:18.113 OTA: upload started
11:48:19.123 [598875][E][ArduinoOTA.cpp:297] _runUpdate(): Receive Failed
11:48:19.123 OTA: failed, error 3
```
```
11:50:33.088 OTA: upload started
11:50:41.649 OTA: upload complete, rebooting
11:50:42.061 Version: 0.1.1 (9893d86-dirty)
11:50:42.061 Slot: app1
11:50:42.061 Image state: pending_verify
11:52:41.927 OTA: image verified - marked valid, rollback cancelled
```
`pending_verify` on this boot is the bench confirmation of the BF-13 override: the core
did not bless the image before `setup()`. Verified 119.9 s after boot, against 120 s.

**Step 3, `v_b9_no_network`:**
```
11:54:45.242 Version: 0.1.1 (9893d86-dirty)
11:54:45.242 Slot: app0
11:54:45.242 Image state: pending_verify
11:54:45.242 *** V-B9 BAD IMAGE (no network) - expect a rollback at the deadline ***
11:56:14.866 OTA: image did not prove itself in time - ROLLING BACK
11:56:15.367 Version: 0.1.1 (9893d86-dirty)
11:56:15.367 Slot: app1
11:56:15.367 Image state: not_pending
```
Rolled back 89.6 s after boot, against the environment's 90 s deadline.

**Step 4, `v_b9_panic`:**
```
11:57:47.699 Slot: app0
11:57:47.699 Image state: pending_verify
11:57:47.699 *** V-B9 BAD IMAGE (panic) - aborting; expect a rollback ***
11:57:47.699 abort() was called at PC 0x4200276f on core 1
11:57:48.199 Rebooting...
11:57:48.704 Slot: app1
11:57:48.704 Image state: not_pending
```
**The banner printed once.** The bootloader rolled back on the first reset in
`PENDING_VERIFY`, and no code of the bad image ran again.

### The failed OTA attempt, a watch item

**ArduinoOTA 2.0.17 aborts if no data arrives within 1000 ms of the TCP connect, and it
retries only after some bytes have been written.** `_ota_timeout` defaults to 1000 and
the bridge does not change it. The failure came exactly 1 s after `upload started`, so
the bridge received nothing; espota's `16%` was data in the Mac's send buffer, not data
acknowledged.

Ruled out: the path MTU (1472-byte pings with DF set pass), the macOS firewall (stealth
off, PlatformIO's python allowed) and packet loss (none). **Suspected, not proven:** WiFi
modem sleep. The bridge never calls `WiFi.setSleep`, so it runs the core's default power
save, and LAN round-trip times to it ran 4 to 143 ms. The retry uploaded in 8.5 s with no
change.

**Nothing was changed for one failure in two attempts.** If it recurs, the candidates
are `ArduinoOTA.setTimeout()` at several seconds and `WiFi.setSleep(false)`. The bridge is
mains-powered, so sleep saves nothing, but disabling it changes the WiFi radio's duty
cycle, which is exactly what **M22 / V-B12** measure. That change is a decision to record,
not a bench fix.

### Not done, and a document discrepancy

- **The OTA upload was not tested during a LoRa transaction** (R-5.3d). There is no
  radio yet; BF-16 brings one.
- **V-B12 is listed under B2** in Impl Plan §7.1's coverage matrix, but §8's B2 row does
  not include it and it cannot run without the radio. It belongs with B3, where BF-16
  lands. Moved in Impl Plan v0.21, committed with this entry.
- **The board is left on `app1` running the `0.1.1-dirty` image.** A USB flash of a
  committed build puts it back in a known state before B3.

---

## 2026-09-13 — BF-16: the radio link, and three things the documents had wrong

**BF-16 is built and host-tested, and none of it has run on a board.** `lora_link.cpp` is
the only file that includes RadioLib. The two decisions `lora_task` makes are
Arduino-free and covered by 27 host tests in `test_lora`: `rx_ladder.{h,cpp}` runs spec 14
stages 1 to 10, and `media_access.{h,cpp}` runs spec 12.3's CAD and backoff.
`radio_config.h` holds the pin map and the D1 PHY, with a `static_assert` on the D33 EIRP
ceiling and another on radio and panel pin collisions. The operator chose four design
points before code was written; each is recorded below with the reason.

### Decisions taken with the operator

- **`lora_task` decodes and reassembles, per Impl Plan §5.2.** BF-11's `queues.h` had
  `lora_task` queue raw frame bytes for `app_task` to decode, which contradicted §5.2's
  table. `RxMessage` now carries the decoded header and the complete payload, and a frame
  the ladder rejects no longer costs a queue slot.
- **Keys arrive through a `PeerKeys` seam.** BF-15's registry supplies them. Until it
  does, every authenticated frame is refused and counted `rx_rejected_mac`.
- **A backoff is state, not a delay.** A busy channel here is usually a node talking, often
  to the bridge. A `lora_task` that slept through a backoff would be deaf for up to 7.5 s
  at the defaults, to the frame that caused it.
- **Discards are counted, not answered.** ERROR replies need the node's `ctx_id`, which the
  registry tracks. `TODO(BF-19)`.

### What the code found

**The codec's MAC check fails open without a key.** `decode_payload` verifies a MAC only
when it holds both an `IMac` and a key. Without either it returns `Ok` with
`mac_verified = false`. That is correct for the bench and the vector generator, and it is
a forged-COMMAND hole in a production receiver. `RxLadder` refuses any frame that should
carry a MAC and was not verified. The library is unchanged; its `DecodeCtx` comment
already says a null `IMac` is for the bench.

**RadioLib's `scanChannel()` has no timeout.** It loops on DIO1 until the radio raises
`CAD_DONE` (`SX126x.cpp`, 7.7.1), so a radio that never answered would hang the
highest-priority task for good. Blocking `transmit()` busy-waits for up to five times the
airtime at priority 6. `lora_link` therefore starts a CAD or a transmission and reads its
completion from the IRQ register on later passes, each against a deadline. `lora_task`'s
one wait is `ulTaskNotifyTake`, bounded at 10 ms and woken by DIO1.

**Receive routes only `RX_DONE` to DIO1, but `HEADER_VALID` is still recorded.** RadioLib's
receive defaults enable `HEADER_VALID` and `HEADER_ERR` in the IRQ register and route only
`RX_DONE` to the pin. Two consequences:
- Before a CAD, `lora_link` reads the register. A valid header seen within the last
  1500 ms means a frame is arriving, and a CAD would take the radio out of receive and
  destroy it. That case counts as a busy CAD (`cad_deferred` records the cause).
- A LoRa header that fails its own CRC never reaches DIO1. It is found on the 1 s
  register read and counted `rx_crc_err`, stage 1.

**RadioLib's `Module` allocates on the heap.** The `Module(cs, irq, rst, gpio, SPIClass&)`
constructor runs `new ArduinoHal`. The range test used that constructor. The bridge builds
the `ArduinoHal` in static storage and passes it to the constructor that takes a HAL, so
the radio path does not allocate (root rule 3).

**BF-11's task stacks are in bytes, not words, a quarter of what was intended.** On the
ESP32-S3, ESP-IDF's `xTaskCreateStaticPinnedToCore` takes the depth in bytes and
`StackType_t` is `uint8_t` (`portmacro.h`); upstream FreeRTOS counts words. BF-11's
comments, the field name `stack_words` and Impl Plan §5.2.1's column all said words. B2's
bench session ran on those sizes without fault, but it had no radio. The field is now
`stack_bytes`, and a `static_assert` checks `sizeof(StackType_t) == 1`. **`lora_task`
goes from 4096 to 8192 bytes**, because it now runs RadioLib's `begin()` and a
`Serial.printf`. `lora_link` logs `lora_task`'s high-water mark after bring-up; that
number is the check on 8192. **The other six sizes are unchanged and unmeasured.**

### A specification discrepancy, raised and not patched

**Spec §12.1 requires node-address filtering "in the SX126x packet handler", and in LoRa
mode there appears to be none.** RadioLib 7.7.1 exposes `setNodeAddress()` for the SX127x,
RF69, LR11x0, CC1101 and LR2021, and for no SX126x class. My reading is that the SX126x's
address field is a GFSK packet parameter only. **That reading is unverified against the
datasheet.** Nothing is implemented for it, and the spec is unchanged. §12.1's own note
says the filtering is "low value while nodes run continuous RX", so no current node loses
anything; the question matters for a duty-cycled node (§17.1).

### A consequence to know

**The OTA verdict now requires `radio_ok`**, the TODO BF-13 left under BF-16's name. An image
whose radio never initialises rolls back at the deadline, even on the broker. `radio_ok`
means the radio initialised, not that it hears nodes. **V-B9 is owed again**: this
changes `ota_policy.cpp`, which Impl Plan §6.5.2 names as a re-run trigger.

### Not done

- **Nothing has been received or sent over the air.** `begin()` succeeding proves nothing
  about a pin map. B3 needs frames out and echoes back, which needs a second transmitter
  at 917.4 MHz, SF9. The range-test firmware sits on 915.0 MHz.
- **N−1 acceptance** (`TODO(BF-22)`), **slots for registered nodes only**
  (`TODO(BF-15)`), **runtime timing from Home Assistant** (`lora_configure()` exists,
  `TODO(BF-23)`), **the raw frame log** (`TODO(BF-27)`).
- **`lora_task_idle()` does not yet see a poll or command awaiting its reply.**
  `TODO(BF-17)`, BF-18.
