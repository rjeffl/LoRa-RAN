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

## 2026-09-14 — BF-16 on the bridge board: the radio comes up, V-B9 waits for a broker

**The SX1262 initialises on the configured PHY and `lora_task` stays healthy for 40 s.**
That is the whole of what this session proves. No frame went out or came in, and V-B9 was
not re-run, because the MQTT broker was offline and the operator was offsite.

### What ran

The flat-case Heltec, identified by its enclosure, was USB-flashed from a clean tree at
`54a9265` on `/dev/cu.usbserial-0001`. Before flashing, the four repo checks and the bridge
host suites passed (77 tests), and `bridge_partitions.py --firmware --elf` passed on the
built image: 833 360 bytes, 24.9 % of the slot, `verifyRollbackLater` strong. The boot log
was captured for 40 s with DTR held low, so opening the port did not press PRG.

Banner and radio lines, verbatim:

```
Version: 0.1.0 (54a9265)
Slot: app0
Image state: not_pending
Tasks started: 7
LoRa: radio up - 917400000 Hz, SF9, BW 125.0 kHz, CR 4/5, -4 dBm conducted, 3.0 dBi antenna
LoRa: stack high-water 6248 bytes free
```

### What it shows

- **`begin()` accepted the D1 PHY with the §10.8.1 Heltec pin map.** The TCXO and
  DIO2-as-RF-switch settings fail by leaving the radio uncalibrated, so a radio-up line is
  some evidence they are right. It is not proof the radio transmits or receives.
- **No `LoRa: radio down` in 40 s.** `lora_link` reads the IRQ register once a second, so
  the capture spans about 40 of those reads.
- **`lora_task` used 1944 of its 8192 bytes at bring-up.** That is the first stack figure
  measured on this board. It was taken after `begin()` and before any frame was received,
  so it is a floor, not a working-load figure. 8192 stays.
- **MQTT connect attempts time out and back off: 3, 3, 4, 8 and 16 s apart.** No reboot, and
  `lora_task` kept running through them. That is Impl Plan §5.2's never-block property
  holding with the broker unreachable, observed once.

### Why V-B9 did not run

**The OTA verdict needs the broker.** `ota_policy.cpp` marks an image valid only when
`tasks_started && mqtt_connected && radio_ok`. With the broker offline, §6.5.2 step 2's
good image would roll back at the deadline, and the run would record a broker outage as a
firmware failure. A USB flash leaves the image `not_pending`, so this boot reached no
verdict and was not at risk.

### Not done

- **V-B9 re-run** — still owed, and it needs the broker.
- **DIO1 waking `lora_task`, and any frame on air** — still unproven. Both need simnode B0.
- **The other six task stacks** — still unmeasured.

## 2026-09-14 — BF-15: the registry, and a discard spec §14 has no stage for

**BF-15 is built and host-tested.** `registry.{h,cpp}` holds `kNodeTable` (GateLink,
WellLink and the four simnode identities), derives each key by HKDF at load and derives
`is_bench` from the address. `registry_runtime.{h,cpp}` adds mbedTLS and a FreeRTOS mutex.
The bridge host suites went from 77 to 96 tests: 15 in the new `test_registry` and 4 in
`test_lora`. The target builds, and all four repo checks pass. Impl Plan §4.2.1 has the
design table.

### Decisions taken with the operator

- **An unregistered source is a bridge diagnostic, and the spec question is raised.** The
  alternative was a spec v0.12 stage first, which touches every node's ladder, the W4
  vectors and the library for a question that has one obvious answer on the bridge today.
- **A short mutex, any task.** The learned half of an entry is written by whichever task
  learns it, under a lock. The other option was `sched_task` as sole owner, fed by a queue,
  which delays learning a `ctx_id` by up to 1 s and adds a queue for no gain.
- **BF-15 learns a context but does no scheduling.** It records `last_seen`, RSSI, SNR,
  `proto_ver` and the §10.1 `ctx_id`, and resets `cmd_seq` on a new context (§10.2). The
  fields other tasks write exist, with sentinels and a `TODO` naming each owner.

### A specification gap, raised and not patched

**A frame from an address the bridge does not provision passes every stage spec §14
defines.** `STATUS` carries no MAC, so a transmitter using address `0x03`, or the unassigned
bench address `0xF4`, reaches stage 10 with nothing refusing it. Two consequences:

- **Reassembly.** §11.3 asks for a set "per provisioned node". BF-16 gave a slot to any
  `src`, so an unprovisioned transmitter could displace a registered node's live set.
- **Root rule 4.** A discard needs a named counter, a `Status` value and a §14 stage.
  Refusing the frame can meet only the first.

**What the bridge does:** `RxLadder` refuses the frame after stage 9 and before stage 10,
counts it as `unregistered_src`, and sets `last_unregistered_src()`. After stage 9, so the
stages §14 does define still count an unregistered sender's faults. Before stage 10, so the
frame takes no slot and no RX queue entry. The counter stays out of `rx_dropped`, and its
name is not `rx_`-prefixed, so it cannot take a name v0.12 might choose.

**The question for spec v0.12:** should §14 gain a stage — for example 5c, "`src`
provisioned", with an `rx_` counter — or should §11.3 say that an unprovisioned source is a
receiver's local policy? A node receives only from `0x00`, so the stage would be trivial
there, but it would still need a test.

### What the code found

- **The library's platform crypto is not part of its build.** `platform/esp32/mbedtls_mac.cpp`
  sits outside the library's `srcDir`, so a firmware that wants it must add it to its own
  `build_src_filter`. The bridge does, for `heltec` (and the two V-B9 environments that
  extend it); `native` adds `platform/native/` for the tests.
- **Decoding per schema belongs to no task.** Impl Plan §5.3 lists `decode/` and no `BF-*`
  row names it. `app_task`'s `TODO` now gives it to BF-24, its first consumer.
- **A `lora_link.cpp` comment still said `lora_task` had 4 KB of stack.** Corrected to 8 KB.

### On the board

The bridge board was USB-flashed from `5222b1d`, a clean tree, and boots:

```
Version: 0.1.0 (5222b1d)
Registry: 0x01 0x02 0xF0(bench) 0xF1(bench) 0xF2(bench) 0xF3(bench)
Tasks started: 7
LoRa: radio up - 917400000 Hz, SF9, BW 125.0 kHz, CR 4/5, -4 dBm conducted, 3.0 dBi antenna
LoRa: stack high-water 6496 bytes free
```

No placeholder-key warning, so the board holds the operator's real key. **`lora_task`'s
high-water reading was 6496 bytes free here and 6248 on the BF-16 boot**, two readings
taken at the same point in bring-up. The figure varies by a few hundred bytes from boot to
boot, so read it as a range, not a constant.

### Not done

- **A key verifying on air** — needs frames from simnode B0.
- **Publishing `unregistered_src`** — BF-19, with the other counters.
- **The mutex is not host-tested.** It needs FreeRTOS. Its callers are `app_task` today
  and `sched_task` from BF-17.

## 2026-09-14 — Simnode B0, first slice: two boards echo each other on 917.4 MHz

**`firmware/simnode/` exists, and two Heltecs running it complete every PING round trip
spec §6.6 defines, on the D1 PHY.** That slice is BF-2, BF-3, BF-5 and the core of BF-4.
BF-6 (`ROLE_GATELINK`), BF-7 (the patch primitive), BF-8 (the fault catalogue) and BF-9
(self-disarm and the OLED) are not started. B0 is on its own branch, `b0-simnode-bringup`,
stacked on B3's.

### Decisions taken with the operator

- **Spec §12.3 media access moved to `lib/lran-link/`**, with its seven tests, and the
  simnode uses it. The alternative was a second copy on a branch from `main`; two copies of a
  backoff rule drift, and the drift shows on air as one node starving another. The cost is a
  stacked branch: B0 cannot merge before B3. `RadioPins`, `kPhy` and the D33 EIRP assert
  moved into the same library afterwards, so the fleet has one copy of the PHY constants.
- **The simnode reads `LRAN_MASTER_KEY` from the root `secrets.h`**, the file the bridge
  reads, and nothing else from it. Both firmwares then derive the same node keys. CI builds
  both simnode profiles against the committed template, as it does the bridge.
- **The bridge board was flashed as a second simnode for the on-air check**, then flashed
  back. The XIAO was not connected, and the bridge logs nothing per frame.

### What was built

- **`identity.{h,cpp}`** — up to four identities, `0xF0`–`0xF3`. Each has its own HKDF key
  (checked against the W4 vectors), random non-zero `ctx_id`, status seq, counters,
  `CommandGate` and reassembler.
- **`node.{h,cpp}`** — every enabled identity decodes every frame, with itself as `self`.
  A frame for `0xF2` is counted `rx_not_addressed` by `0xF0`, exactly as a second board would
  count it. `ROLE_RANGE` echoes PING and answers POLL with `0xF0`; `ROLE_HEALTH` answers
  POLL only.
- **`console.{h,cpp}`** — `id`, `enable`, `disable`, `ver`, `ctx`, `ping`, `stats`, `log`,
  and `radio` from `main.cpp`. `push`, `event`, `ack`, `field` and `fault` answer `ERR not
  implemented` and name BF-6 or BF-8.
- **`radio.{h,cpp}`** — follows the bridge's `lora_link.cpp` state machine: CAD and transmit
  started, then read back from the IRQ register against a deadline.
- 31 host tests across `test_identity`, `test_node` and `test_console`.

### On the bench

The handheld Heltec (A, `/dev/cu.usbserial-3`) and the flat-case Heltec (B,
`/dev/cu.usbserial-0001`) both ran `simnode-heltec` from `cab05e8`, about 1 m apart. A kept
its boot identities, `f0 ROLE_RANGE` and `f2 ROLE_HEALTH`; B was reconfigured to
`f1 ROLE_RANGE` alone. Verbatim:

```
A| ping f0 -> f1 seq 1: echo ok, n 8, 1 frame(s) out, 1 back, rssi -19 dBm, snr 10.3 dB, 520 ms
A| ping f0 -> f1 seq 2: echo ok, n 202, 1 frame(s) out, 1 back, rssi -18 dBm, snr 10.5 dB, 2289 ms
A| ping f0 -> f1 seq 3: echo ok, n 202, 15 frame(s) out, 15 back, rssi -18 dBm, snr 10.5 dB, 8617 ms
B| ping f1 -> f0 seq 1: echo ok, n 202, 15 frame(s) out, 15 back, rssi -18 dBm, snr 10.8 dB, 8492 ms
B| ping f1 -> f0 seq 2: echo ok, n 40, 1 frame(s) out, 1 back, rssi -18 dBm, snr 11.0 dB, 812 ms
B| ping f1 -> f2 seq 3: no echo in 30000 ms
```

- **The full-size frame's round trip is 2289 ms**, about two of spec §15.1's 1107 ms SF9
  frames plus a CAD each way. That is the airtime table checked at one more point.
- **Every counter reconciles.** A's driver saw 33 `TX_DONE`s against its identities' 33
  queued frames; B's saw 34 against 34. `f0` heard B's 34 frames. `f2` counted 33 of them
  `rx_not_addressed` and the one PING addressed to it as `unhandled`. No TX error, timeout,
  forced transmission or CAD error on either board. B recorded one CAD backoff.
- **`ROLE_HEALTH` did not echo**, which is the correct result.
- **The bridge, flashed back from `cab05e8`**, boots with the lifted `lran-link` code: the
  radio comes up on D1's PHY, and `lora_task` has 6188 bytes of stack free.

### What the code found

- **Schema `0xF0` cannot carry `DEBUG_SYNTHETIC`.** Impl Plan §10.1 says every simnode
  payload sets `status_reason = DEBUG_SYNTHETIC`, but spec §7.5's health schema has no
  `status_reason`. The simnode sets `health_flags` bit 0, "any debug mode active", on every
  `0xF0`, and §10.1 now says so. A bridge that publishes `0xF0` from a bench node must read
  that bit to mark the data synthetic.
- **The bridge does not answer PING.** Spec §17.3 makes RF loopback "required of every node
  build", and no `BF-*` task gives it to the bridge. Until one does, a PING from a simnode to
  `0x00` reports no echo. That is a bridge gap, not a simnode fault.
- **`lib_extra_dirs = ..` breaks a library's own test project.** PlatformIO picks the
  project up as a library of itself, and the test build loses Unity's include path
  (`unity.h` not found). `lib/lran-link/platformio.ini` uses `lib_deps = symlink://` instead.
- **`0xF0`'s `tx_frames` never counts the frame that carries it.** The payload is built
  before the frame is queued, so each report counts the frames before it.

### Not done

- **B0's criteria not met:** "console accepts every command" (five commands are
  placeholders), and "faults arm, fire the specified count, self-disarm, and show armed
  state on the OLED" (BF-7 to BF-9). **The XIAO profile builds and has not been flashed.**
- **Four identities on one board have not been on air at the same time.** The table and its
  independence are host-tested; the bench run used three identities across two boards.
- **Identities do not persist.** Every reset returns a Heltec to `f0` and `f2`. Two Heltecs
  booted together both answer to `f0` until one is reconfigured.

## 2026-09-14 — BF-7: malformed frames through the real encoder, checked against W4

**`lib/lran-sim/` exists, and its `FramePatch` rebuilds 18 of the 21 W4 negative vectors
byte for byte from `encode()` and one stated patch.** BF-8's dependency is met. The simnode
does not call the library yet; BF-8 is its first caller. Host-tested only; there is nothing
to put on air until BF-8.

### The surface

`FramePatch` holds a caller-owned 255-byte buffer. It starts from `encode()` or
`encode_fragment()`, patches single-byte header fields by name, resizes the payload
(moving the MAC and CRC), strips or flips the MAC, or truncates the body. Then
`seal(Seal::Crc)` or `seal(Seal::MacAndCrc)` reseals it, and `flip_crc()` can corrupt it
afterwards. Impl Plan §10.5.2 maps each catalogue entry to its operation.

Two choices, both about rule 1 (never a second serializer):

- **Every patch unseals, and `frame()` is `nullptr` until `seal()`.** Without that, a
  forgotten reseal sends a frame that also fails its CRC. The receiver counts it at stage 3,
  and the fault reads as tested while the targeted stage never ran.
- **No `seq` or `ctx_id` patch.** `ctx_jump`, `seq_jump` and `seq_wrap` are correct frames,
  so `encode()` emits them from a `Header`. Adding multi-byte patches would be the first step
  towards a serializer here.

### How it is checked

- **The W4 negative vectors are an independent witness.** `tools/vectors/generate.py` built
  them the way §10.6 asks the simnode to, by editing a correct frame and resealing it, in
  Python that never read this code. `test_frame_patch` starts from the C++ encoder instead
  and must produce the same bytes. The three it skips are `command_ctx_mismatch` and
  `command_signed_with_wrong_node_key`, both correct frames, and `frag_index_equals_total`,
  which takes the same path as `frag_index_ge_total`.
- **`HdrByte`'s offsets are the one layout the library states**, so a test patches each
  offset and reads it back through `decode_header()`, checking that no other field moved.
- **The comparison catches a wrong offset.** With `HdrByte::Dst` changed from 3 to 4, two
  tests failed: `wrong_dst_not_addressed` differed at byte 3, and the offset test failed.
  Reverted.

16 tests. All passed the first time the suite compiled, and that result is why the mutation
check was run.

### What the work found

- **Impl Plan §5.4 said `/tools/vectors/` shares `lran-sim`**, "so the on-air fault
  injector and the host vectors agree byte-for-byte". It cannot and should not. The
  generator is Python, and Impl Plan §9.3 requires it to stay independent of the C++ code.
  Shared code would let the simnode and the vectors be wrong in the same way. v0.25
  corrects §5.4: the two stay separate, and `lran-sim`'s tests compare them.
- **`oversize` reaches 255 bytes**, the SX1262's 8-bit length maximum, and the codec counts
  it `rx_oversize` at stage 2a. The W4 vector is 223 bytes, one past `LRAN_MAX_FRAME`;
  §10.5's entry says 255. Both are now producible.

## 2026-09-14 — BF-8: the fault catalogue, each entry checked against the receive ladder

**`firmware/simnode/fault.{h,cpp}` and the `fault` console command are built**, and the host
suite `test/test_fault` (33 tests) proves each fault moves the spec §14 counter its §10.5 row
names. B0's fault criterion is met bar the OLED (BF-9). Host-tested only; nothing new has been
on air.

### The model, chosen with the operator

`fault <hex> <name> [count] [gap <ms>] [to <hex>] [ctx <hex32>]` **arms** a fault on one
identity. The first injection fires on arm; the rest fire as the outbox drains and the gap
allows, then it self-disarms (§10.6 rule 2). An injection is the whole frame sequence a row
describes — `bad_ver` two frames, `set_displaced` two, `single_frame_interleave` four. The
alternative considered was arming every fault against the identity's *next* answers; it was
rejected because it cannot be driven until the bridge polls (BF-17) and is harder to script.
`silent` is the one behaviour fault in this slice and does work that way: it withholds the
identity's next `count` answers.

### What holds it to the rules

- **Every malformed frame comes from `lran::sim::FramePatch`** (BF-7): a real `0xF0` health
  status, one patch, an explicit reseal. No second serializer (§10.6 rule 1). The carrier is
  the status the node really sends, so a fault differs from an accepted frame in exactly the
  way its row states.
- **Authenticated faults carry `COMMAND(NOP)`.** If a receiver defect ever accepts one,
  nothing moves at a gate.
- **The counter column is the assertion.** `test_fault` feeds each fault into the codec's own
  `decode_header` / `decode_payload` / `Reassembler` — the ladder the bridge runs — and checks
  the named counter, and only it, moves. `hdr_rsv`, `seq_wrap` and `single_frame_interleave`
  assert the opposite: `rx_dropped` does not move. A fault malformed the wrong way lands on a
  different stage and fails.
- **`oversize` is 255 bytes**, the PHY ceiling, not the W4 vector's 223.

### Deferred, and why

- **The five command-path faults** — `ack_suppress`, `ack_dup`, `event_replay` and §10.5.1's
  `cmd_replay`, `cmd_stale_seq` — need `ROLE_GATELINK`'s command path. `arm()` refuses them
  with `WaitsForTask` and the console names **BF-6**. `ctx_jump` sends its node-side half (a
  status from a fresh context); the `REJECTED_CTX` reply half is also BF-6.
- **`bad_phy_crc` is refused as uninjectable.** The SX1262 computes the PHY CRC in hardware;
  §14 stage 1 is closed only at the far edge of a real link (§10.5.2).
- **The OLED (BF-9)** is not started. The bounded-count self-disarm it shares with BF-9 is
  built and tested here; only the display is left.

## 2026-09-14 — BF-9: the simnode's OLED page

**The Heltec simnode now draws its identity table, the last frame it heard, and every armed
fault as an inverted bar**, and the bar clears when the fault disarms itself. The page is
host-tested (`test/test_oled`, 14 tests, run against the real injector and node). **Both
target images build. Neither has been flashed, and nobody has seen the page on a panel.**

### The page

Five rows of ArialMT_Plain_10, 13 px apart:

```
f1>f0 PING -42        12s     last frame: src>dst, type, RSSI, age
f0 bad_crc              2     inverted: armed fault, injections left
f2 ROLE_HEALTH                identity, exact role token
```

- **Row 0 is the last frame the board received**, whichever identity it was for. A frame no
  identity decoded shows as `<len>B <rssi> drop`, a PHY CRC failure as `phy crc error`, and
  a radio that is not up as an inverted `RADIO DOWN` over everything else.
- **`Node` records the last frame only from a successful `decode_header`.** Reading `src` and
  `type` from raw offsets would be a second parser, which §10.6 rule 1 forbids for writing.
  `on_phy_crc_error()` now takes `now_ms` so its row has an age.
- **`silent` is read from `Identity::silent_left`**, not from the injector, because that is
  where BF-8 keeps it.
- **A fault name too long for the row is cut and ends in `~`.** `single_frame_interleave`
  with a three-digit count is the widest case. A cut token has to look cut, or an operator
  types the fragment and gets `unknown fault`.
- **Role tokens are shown whole** (`ROLE_GATELINK`, not `GATELINK`), per the simnode's rule
  on exact tokens. The `ctx_id` did not fit beside them, and `id list` has it.
- **The panel redraws only when the text changes**, checked every 200 ms. Ages tick once a
  second, so it redraws at about 1 Hz. A redraw is about 1 KB over I2C, well inside
  `radio.cpp`'s 500 ms CAD deadline, and DIO1 is latched by its ISR during it. **That
  reasoning is unmeasured**: no `radio` counter has been read with the panel running.

### What the tests caught

`test_the_top_row_fits_at_its_widest` failed on the first run. An RSSI of −32767 pushed the
row past the 21-character budget. No SX1262 reading lands there, but the budget has to hold
for any input, so a reading outside −199…99 dBm now shows as `?`.

### A gap in B0's criterion

**The XIAO + Wio-SX1262 Kit has no display.** Impl Plan §8's B0 criterion says armed state
shows "on the OLED", and §10.8.1 assigns `0xF1 ROLE_GATELINK` to the XIAO, which is the board
that will carry the command-path faults once BF-6 lands. On that board an armed fault is
visible only from the console. Its boot banner now says so (`OLED: none on this board`).
This is not patched in the criterion. The operator decides whether the Heltec's panel
discharges it, or whether the XIAO needs another indicator such as its user LED.

## 2026-09-14 — Correction to the BF-9 entry: the XIAO has a panel

> **Supersedes "A gap in B0's criterion" in the BF-9 entry above.** That section is wrong.

**The XIAO + Wio-SX1262 Kit is mounted on a Seeeduino XIAO expansion board, and that board
has an SSD1306.** The range test already drives it: `firmware/range-test/src/board_config.h`
`kXiaoWioKitUi` has SDA 5, SCL 6, no reset line, no Vext, and `flip_vertically` set because
the enclosure holds the stack upside down. BF-9 was written from the Kit's description and
never checked against the range test's board config, which already covered this. The
operator caught it.

**Fixed:** `profiles.h` gains `kXiaoExpansionPanel`, and `ui_begin()` skips the Vext and reset
steps when a pin is `kPinNone`, as the range test's `ui_oled.cpp` does. The pin-collision
`static_assert` now covers both profiles. Both images build, and the 79 host tests pass.
**The XIAO image has still never been flashed.** B0's OLED criterion needs no decision.

### The Heltec simnode, flashed from `c3ef4ca`

Flashed from `c3ef4ca` on the handheld Heltec at `/dev/cu.usbserial-3` (MAC
`44:1b:f6:fa:bc:2c`). Boot log:

```
Board: heltec_wifi_lora_32_V3
id f0 ROLE_RANGE ctx 0x1bd0fe32
id f2 ROLE_HEALTH ctx 0xeff12a5e
[  1784][W][Wire.cpp:301] begin(): Bus already started in Master Mode.
OLED: up
radio: up - 917400000 Hz, SF9, BW 125.0 kHz, CR 4/5, -4 dBm conducted, 3.0 dBi antenna
```

- **`OLED: up` means the panel ACKed its address.** It does not show what the panel draws.
- **The `Wire` warning is harmless.** `ui_begin()` starts I2C to probe the panel, and
  ThingPulse's `init()` starts it again.
- **Opening the port with DTR and RTS held low still rebooted the board**, so faults armed
  over a fresh connection land on a fresh boot. `fault f0 silent 5` and
  `fault f2 bad_crc 3 gap 30000` were armed after it, for the panel check.

### The XIAO is mounted the other way up as a simnode

**The simnode's XIAO profile does not flip the panel, although the range test's does.** The
operator reports the board is rotated 180° from its range-test mounting. The range test set
`flip_vertically` only because its enclosure held the stack inverted. ThingPulse's
`flipScreenVertically()` is a 180° rotation (`SEGREMAP | 0x01` with `COMSCANDEC`), not a
mirror, so a board turned 180° reads upright without it. The pins still come from the range
test; the orientation does not. Unverified on the panel: the XIAO image has not been flashed.

### The Heltec's page, confirmed by eye

**The operator confirmed every staged element on the handheld Heltec's panel**, running
`c3ef4ca`. One serial connection drove four stages over 166 s, and every command answered
`OK`:

1. Idle: `rx: nothing yet`, `f0 ROLE_RANGE`, `f2 ROLE_HEALTH`.
2. `id add f1 ROLE_GATELINK`, `id add f3 ROLE_FAULT`, `disable f1`. `f1` showed `off`.
3. `fault f0 silent 5`, `fault f2 bad_crc 3 gap 20000` and
   `fault f3 single_frame_interleave 999 gap 60000`, all as inverted bars. `f2` counted down
   and cleared when the console logged `3 injection(s) done, disarmed` at 99 s. `f3`'s name
   was cut with `~`, and its count stayed visible.
4. `fault f0 off`, `fault f3 off`, `enable f1`. Every row returned to plain.

- **Rows follow slot order, not identity order.** `f1`, added after `f2`, drew below it, which
  matches `id list`.
- **Not shown:** row 0's frame format and `RADIO DOWN`. The Heltec does not hear its own
  transmissions, and the bridge board sends nothing, so a received frame needs a second
  transmitting simnode.
- **The XIAO is not with the operator offsite**, so its panel, pins and orientation wait for
  its first flash.

## 2026-09-14 — BF-6: ROLE_GATELINK, and the five command-path faults

**`ROLE_GATELINK` is built** in `firmware/simnode/gatelink.{h,cpp}`. It answers `POLL` with
schema `0xFE`, `COMMAND` with `COMMAND_ACK` through the identity's `CommandGate`, and
`CONFIG` with `CONFIG_ACK`, and it sends `0x11` events on request. The console's `push`,
`event`, `ack` and `field` exist, and all five command-path faults arm. That completes every
Impl Plan §10.4 command. The host suites pass: 108 tests, 23 of them in the new
`test_gatelink`. Both images build. **Nothing from this task has been on air.**

### Four choices, made with the operator

| Question | Decided |
|---|---|
| What marks a `ROLE_GATELINK` status synthetic, given `push <hex> [reason]` sets the reason | **Schema `0xFE` itself** (spec §7.1, bench only). `push` defaults to `DEBUG_SYNTHETIC`, and a given spec §8.7 name overrides it. The simnode `CLAUDE.md` rule that said every status carries `DEBUG_SYNTHETIC` is corrected |
| `CONFIG` needs parameter IDs, which only the unbuilt `/lib/lran-config/` may declare | **A generic RAM store.** `SET` holds any `param_id` whose `ptype` and `len` agree; `persist_status` is always `APPLIED_NOT_PERSISTED`. `GET` of an unset id is `UNKNOWN_PARAM`. No id is invented. `CLAMPED` and `READ_ONLY` cannot occur until the library exists |
| Impl Plan §10.4's `ack` modes read as persistent; simnode rule 3 bounds every fault | **`suppress` and `dup` arm the bounded `ack_suppress` / `ack_dup` faults**, which self-disarm and show on the OLED. **`delay <ms>` is a setting**, kept until `ack <hex> normal` and shown in `id list` |
| `cmd_replay` and `cmd_stale_seq` impersonate the bridge; where may the frames go | **A target on the same board is fed through `Node::on_rx` and never transmitted**, so the node's own gate can be tested with one board and in host tests. A target on another board is reached over the air with `to <hex> ctx <hex32>` |

### How the command path holds to spec §9.4 and D34

- **Steps 2 and 3 answer.** A `COMMAND` or `CONFIG` failing its context check or its MAC gets
  `COMMAND_ACK(REJECTED_CTX)` or `(REJECTED_MAC)`, from the node's own `ctx_id`, and moves
  nothing, the high-water mark included. `ctx_jump`'s missing half, the `REJECTED_CTX` reply,
  exists as a result.
- **`check()` before dispatch, `record()` before the ACK.** `ack <hex> delay <ms>` holds a
  command in flight. A retry landing in that window is counted in `rx_dup_command` and
  answered with nothing; the retry after it gets `DUPLICATE_CACHED`
  (`test_a_retry_inside_the_execution_window_receives_nothing`). A new command during the
  window is `ACTUATOR_BUSY`, its `seq` consumed.
- **`actuations` counts what a relay would have pulsed.** `cmd_replay` sends `OPEN`, not `NOP`,
  so its assertion is that this count stays at 1.
- **`REBOOT` with the `0xA5` guard** sends its ACK under the old context, then a `BOOT` status
  under a new one. The config store, event ids and any in-flight command go with it.
- **The mutation check:** making a cached retry increment `actuations` failed five tests, three
  in `test_gatelink` and two in `test_fault`. Reverted.

### Three questions for spec v0.12, not settled here

1. **How a `DUPLICATE_CACHED` ACK carries the cached result.** §9.4 step 4 says
   "`COMMAND_ACK(DUPLICATE_CACHED)` with the cached result", and §6.3 has one `result` byte and
   one `detail` byte. The simnode sends `result = DUPLICATE_CACHED` with the cached result in
   `detail`, so the bridge can see the dedup hit (V-B5). The gate's cached `detail` is lost.
   BF-18 will read whatever v0.12 decides.
2. **What answers a repeated `CONFIG`.** §9.4 applies steps 4–6 to every authenticated type
   and words the answers as `COMMAND_ACK`. The simnode follows that wording. §7.4 says a lost
   `CONFIG_ACK` is recovered by readback, so a bridge should never repeat a `CONFIG`, but the
   specification does not say what a node sends if one does.
3. **§7.4 expects fragmentation to carry config sets that §3.1 forbids.** §7.4 says 24
   `u32` entries fill a `CONFIG` and 21 results fill a `CONFIG_ACK`, "the first fragmented
   frames the system is expected to produce." But 24 `u32` entries are 194 bytes, which fits one
   authenticated frame, and §3.1 caps a *reassembled* set at the same 196 bytes, so
   fragmentation cannot carry a 22nd result. The simnode cuts the `CONFIG_ACK` at what fits and
   logs the cut (`test_a_config_ack_that_cannot_fit_is_cut_and_logged`). Its store holds 21
   entries so that a `GET_ALL` always fits.

### Other changes

- **The XIAO now boots as `0xF1 ROLE_GATELINK`**, §10.8.1's assignment. The Heltec is unchanged.
- **A loopback command fault shows on the OLED's top row** as `00>f1 CMD --`: it goes through
  the same receive path as a frame off the air. The target's `COMMAND_ACK`s do go on air, to
  `00`.
- **`field` names are the `GateLinkStatusV1` member names** (`batt_mv`, `cell_mv2`), and `na`
  writes the width's sentinel where the specification has one. `status_reason` is not a
  field: `push` owns it.

**Not supported by anything here:** a bridge that retries on its own (BF-18), and any of it on
air.

## 2026-09-14 — BF-17: the poll scheduler

**The bridge now polls.** `firmware/bridge/src/scheduler.{h,cpp}` decides which node to poll
and when; `sched_task` builds the `POLL` and queues it each 1 s tick. **Never more than one
poll is outstanding across the fleet** (R-3.1d). An unanswered poll increments the node's
`missed_polls`, and any valid frame from the node clears it. The host suite passes: 103
tests, 14 of them the new `test_scheduler`. The target builds, and the never-block check is
clean. **No poll has been transmitted**: the bridge board was not flashed.

Built on a new branch, `b3-poll-scheduler`, stacked on B0's. B3's own branch carries
documents several versions older than B0's, and every doc change there would have conflicted
on the way back up the stack (decided with the operator).

### Who is polled — decided with the operator

**Production rows from boot; a bench row once the bridge has heard any frame from it.**
GateLink and WellLink are polled whether or not they answer, so a GateLink that never comes
up still counts missed polls for BF-20. `f0`–`f3` cost no airtime until a simnode speaks, and
a simnode's `push` enrols it. Nothing removes a row once enrolled; going offline is BF-20's.

### Choices the documents left open

- **The reply window is 10 s, and no document gives the number.** A node that finds the
  channel busy may wait `cad_retries` × `backoff_max_ms` = 5 × 1500 = 7.5 s before it
  transmits regardless (spec §12.3), and its `0xFE` answer is about 0.6 s at SF9. A shorter
  window would count a node that obeyed media access as missing. It is runtime-settable;
  **BF-23 takes it from Home Assistant.** The Impl Plan should name the parameter.
- **The next poll falls one interval after the send**, not after the due time, so a poll
  that waited behind another does not pull the next one early.
- **A `POLL` takes its `seq` from a scheduler counter, not the command `seq`.** `POLL` is not
  authenticated (spec §9.2), so no node checks its `seq`, and spending command seqs on it
  would muddle the space BF-18 owns. Spec §10.2 names two sequence spaces and does not place
  bridge-originated unauthenticated frames in either.
- **An OTA upload holds new polls** (`ota_in_progress()`), and **an outstanding poll holds an
  upload**: `lora_task_idle()` now also requires no poll outstanding (R-5.3d).
- **A zero interval is held to 1 s.** Refusing it belongs where the value is set, BF-23.

### Two things the tests found before any board did

- **A due time of 0 fails after about 24.8 days.** The first version marked a never-polled
  row due at time 0. After `millis()` passes half its range, 0 reads as the future, so a bench
  row enrolled that late would never be polled. Rows now carry a "not yet polled" flag, and
  `test_a_row_enrolled_late_in_uptime_is_polled` holds it.
- **The mutation check:** letting `next()` start a poll while one was outstanding failed
  three tests. Reverted.

**Not supported by anything here:** a poll on air, a simnode answering one, and the reply
window measured against a real exchange. B3's bench run needs the XIAO or the bridge board
flashed with this build.

## 2026-09-14 — CI's GCC crashes on BF-6's `gatelink.cpp`

**The simnode's `native` suite did not build in CI after BF-6.** GCC 13 on the `ubuntu-24.04`
runner stops with an internal compiler error (`in gimple_add_tmp_var, at gimplify.cc:774`) at
`cfg_ack_ = lran::schema::GateLinkConfigAckV1{};`. The macOS host build uses Clang and the
target builds use Xtensa GCC, and both compile it, so every local check passed. The PR checks on
#60 and #61 were the first place it showed.

The pattern is assigning a braced temporary of an aggregate whose array member has a default
member initializer (`entries[kMaxConfigAckEntries] = {}`). `gatelink.cpp` now copies from
file-scope empty constants instead, at all four places it reset such a struct. The bytes are the
same. **Unverified locally:** no Linux GCC is installed on this machine, so the next CI run is
the check. `identity.cpp` and the bridge's `registry.cpp` use the same idiom on structs without
an initialized array member, and CI compiled both.

**Trap:** a clean local `pio test -e native` on macOS does not show that CI's GCC will compile
the code. Read the PR's `Host Unity suites` job before calling a change verified.

## 2026-09-14 — BF-20: the availability watchdog

**The bridge now judges each watched node `online` or `offline` and publishes the result,
retained, to `lran/<node>/availability`** (PRD R-3.4a–c, spec §16.5). It is host-tested: 13
tests in `test_availability`, 116 bridge host tests in all. Nothing has been published to a
broker, and no node on air has been judged. Impl Plan §6.1.2 has the rules.

The fix in the entry above passed CI's host suites at `10e3d6c`.

### What it does

- `sched_task` runs the watchdog once a tick, after the scheduler. A node goes `offline` when
  `missed_polls` reaches `missed_poll_threshold` (default 3, runtime-settable, 0 held to 1) and
  `online` on any valid frame.
- **A node that has neither answered nor missed enough polls since boot is Unknown, and
  nothing is published for it.** Whatever the broker retained from before the reboot stands
  until the node settles it. The alternative, `offline` for every node at boot, would flap a
  live GateLink off and on at every bridge restart.
- Every broker connect publishes each judged node again: `mqtt_task` sets a flag and
  `sched_task` publishes through the queue on its next tick. A publication the queue refuses
  leaves the node pending, and the next tick retries it.
- **The watched nodes are the ones BF-17 polls.** The status page's `nodes n/m` now reads
  online out of watched, so a bridge with no WellLink shows `nodes 1/2` once GateLink answers.
- R-3.4d needs no code here. BF-23's discovery configs must list the bridge's LWT topic and the
  node's availability topic together.

### Decisions made while building it

1. **A simnode's availability is not published yet.** Spec §16.6 publishes bench availability
   only while `simnode_diag_enable` is set, default `false`, and BF-26 owns that flag. Until
   BF-26, the bridge judges a simnode and prints each change on the serial console, for
   example `availability: simnode1 offline (missed_polls 3, threshold 3)`. **V-B3 reads that
   line on the bench** until the flag exists.
2. **The watchdog detects a frame from a new registry count, `frames_heard`**, not from
   `missed_polls == 0`. A frame followed by a closed reply window inside one 1 s tick would
   leave `missed_polls` at 1 and hide the frame. BF-17's scheduler makes that unlikely, since
   any frame clears the node's outstanding poll, but the watchdog should not depend on it. A
   mutation back to `missed_polls == 0` failed one test; changing `>=` to `>` at the threshold
   failed six.

### Trap

**A source file named `availability.h` breaks the macOS native build.** macOS's filesystem is
case-insensitive, `src/` is on the include path, and the SDK's `stdio.h` includes
`<Availability.h>`, so the project's header replaced the SDK's in every translation unit. The
errors appear inside `string.h` and name nothing in the project. The files are
`node_availability.{h,cpp}`.

## 2026-09-14 — BF-19: every §14.1 counter published, and BF-26 deferred

**The bridge now publishes all 21 §14.1 discard counters by their normative names**, with
`rx_dropped`, retained, on `lran/bridge/diag/state`. The radio's and queues' diagnostics go
to `lran/bridge/diag/radio/state`, and each watched node's link to `lran/<node>/diag/state`.
It is host-tested: 10 tests in `test_diag`, 126 bridge host tests in all. Nothing has been
seen at a broker, and no counter has been moved by a frame on air. Impl Plan §4.3.2 has the
topics and rules.

### BF-26 first, and why it stopped

BF-26 was asked for first and deferred with the operator. Impl Plan §4.2a makes
`simnode_diag_enable` a `/lib/lran-config/` parameter set over `lran/bridge/config/set`, and
three pieces of that do not exist:

1. **`/lib/lran-config/`.** System PRD §9.4 describes it; no plan section builds it and no
   task owns it.
2. **An MQTT receive path.** The bridge can subscribe but installs no message callback and
   has no inbound queue. BF-18's command topics need the same path, and no task owns it
   either.
3. **A `config/set` and `config/ack` payload.** Spec §16.2 names the topics and defines no
   payload, and the specification owns every MQTT topic.

**Decided with the operator for when BF-26 is built:** until Home Assistant can set the flag,
the bench toggle is a serial `diag on|off` command, RAM only, off at every boot. It keeps
one binary, which is §16.6's argument against a build-time switch.

### Decisions

1. **The discard counters are the bridge's, not per node** (operator). §14.1 says every
   counter is "published: per node by the bridge", but stages 1–2a have no header to read,
   and until stage 9 checks the MAC the `src` byte may be corrupt or forged. Charging a
   stranger's frame to GateLink would make a healthy node look sick. **Raised for spec
   v0.12:** how a pre-MAC discard is attributed, if at all.
2. **`ERROR` replies are not built** (operator). §14 has a receiver answer stages 5a–10 with
   `ERROR`, and stages 3–4 optionally. On the bridge, each reply goes to a solar node's `src`
   taken from an unauthenticated header, addressed to a `ctx_id` the frame has not proved.
   **Raised for spec v0.12:** whether the bridge must answer, and to which `src` and
   `ctx_id`. The work is the new BF-19a.
3. **The payloads are the bridge's choice.** Spec §16.2 defines no `diag/state` payload, as
   it defines none for `lran/bridge/version`. The keys are the §14.1 names from
   `kCounterRegistry`, and a sentinel is `null`. **Raised for spec v0.12** with the other
   payload gaps.
4. **`diag_publish_interval_s` is 60 s**, runtime-settable. No document gave a cadence.

### Found on the way

- **The counter document does not fit the old queue payload.** With every counter at
  `UINT32_MAX` it is 681 bytes, and `kMaxPayloadLen` was 512. A counter document refused
  months into uptime would have been counted and silent on the broker. `kMaxPayloadLen` is
  768 and a test asserts the worst case fits; the publish queue's static RAM grows from ~19 KB
  to ~28 KB. The Heltec build reports 38.3 % RAM.
- **`sched_task`'s 3072-byte stack could not hold a publication as a local.** A
  `PublishMessage` is now ~872 bytes and the JSON buffer 768. `sched_task` publishes from
  one static message, BF-20's availability included.
- **`lora_task`'s counters were readable only field by field**, so a reader could combine
  values from two moments and publish an `rx_dropped` that disagreed with its parts.
  `lora_task` now copies them under a spinlock once a second. A spinlock rather than a
  mutex, because `lora_task` never waits on another task.

Mutation checks: skipping the first registry counter failed two tests; publishing an
unknown RSSI as a number failed one.

## 2026-09-14 — B3 split into B3a and B3b, so the stack can merge

**Milestone B3 is now two milestones** (Impl Plan v0.32 §8, decided with the operator). B3a
covers what is built: the radio link, the registry, the poll scheduler, the availability
watchdog and the counter publication (BF-15, BF-16, BF-17, BF-19, BF-20). B3b covers the command
path, `ERROR` replies, version tolerance, the `simctl` catalogue, CAD under real contention
and V-B12 (BF-18, BF-19a, BF-21, BF-22).

**Why.** Three draft PRs are stacked, and they merge bottom-up from B3's. B3 as written could
not be accepted before spec v0.12, BF-18, BF-21 and BF-22, so nothing could merge and every
new task made the stack taller. B1a/B1b is the precedent for splitting a milestone at the
point where a bench can prove the first half.

**What moved and what did not.** No criterion was dropped. Two were narrowed in B3a and
completed in B3b: keys are proven by a command round-trip in B3b, because a `STATUS` carries
no MAC; and the §10.5 catalogue runs by hand in B3a and from `simctl` in B3b. B3a gained one
criterion B3 lacked, a measured poll-to-answer time, because `poll_reply_timeout_ms` = 10 000
is still a derived number. B4 follows B3a.

**The next session has every board and the broker.** The handoff orders it so that B0 and
B3a can be accepted and the stack merged in one sitting.

## 2026-09-15 — V-B9 re-run, B0 accepted, and B3a on air

**The bench session the B3 split was made for.** Every board and the dev broker were on the
desk: the bridge board, the simnode Heltec, and the XIAO with the Wio-SX1262 Kit, which had
never run a simnode image. Everything ran from the tip of `b3-poll-scheduler`, so the bench
tested what `main` will run.

Desk check first, from a clean tree: 127 protocol, 7 link, 16 sim, 126 bridge and 108 simnode
host tests pass, the three targets build, and the five repository checks pass.

### V-B9 — all four steps passed

Owed since BF-16 added `radio_ok` to the verdict (Impl Plan §6.5.2). Step 2's image carried
`custom_bridge_version = 0.1.1-vb9`, so its banner reads `-dirty`; the bump was reverted and
the board reflashed from a clean tree afterwards.

1. **USB flash.** `Version: 0.1.0 (c5f021f)`, `Slot: app0`, `Image state: not_pending`.
2. **OTA a second good build.** `Slot: app1`, `pending_verify`, then 120 s after boot
   `OTA: image verified - marked valid, rollback cancelled`.
3. **The no-network image.** `Slot: app0`, `pending_verify`, the V-B9 banner, then at 90 s
   `OTA: image did not prove itself in time - ROLLING BACK`, and a reboot into `Slot: app1`
   carrying step 2's version.
4. **The panic image.** The V-B9 banner once, `abort() was called at PC 0x4200284b`, and a
   reboot into step 2's image. One banner, so the bootloader rolled back.

**What this run did not test.** The no-network image's radio came up, so its rollback proves
the network half of the verdict, not the `radio_ok` half BF-16 added. No image with a dead
radio was built.

**`AUTH_FAIL` (reason 202) on the first WiFi attempt of every good boot**, five boots out of
five, with a later attempt connecting each time. Nothing here depends on it. Recorded because
the serial log otherwise says nothing about WiFi state.

### B0 — accepted by the operator

The XIAO ran `simnode-xiao-wio` for the first time: `Board: xiao_esp32s3+wio_sx1262_kit`,
`id f1 ROLE_GATELINK`, `OLED: up`, radio up on 917.4 MHz. The operator confirmed the
expansion board's panel reads the right way up, and watched `f3`'s inverted fault bar count
down and clear.

```
H| ping f2 -> f0 seq 1: echo ok, n 8, 1 frame(s) out, 1 back, rssi -34 dBm, snr 11.8 dB, 524 ms
X| fault f3 bad_crc: 5 injection(s) done, disarmed
X| OK event f1 -> 00 event_id 1 (repeat)
```

`H` is the handheld Heltec, `X` the XIAO. Four identities `f0`–`f3` were loaded on the XIAO at
once, each with its own `ctx_id`, and every command in `help` was typed on a board. #60 carries
the clause-by-clause record.

### B3a — what went on air

- **The bridge polls and the simnodes answer.** All four identities on the XIAO were enrolled,
  polled and `online` at once: `simnode0` through `simnode3`, each with its own `ctx_id`.
- **Poll-to-answer times, 21 measurements** against `poll_reply_timeout_ms` = 10 000, in ms:

  | Identity | Role | Answers |
  |---|---|---|
  | `f0` | `ROLE_RANGE` | 522, 525, 528, 529, 531, 533, 606 |
  | `f1` | `ROLE_GATELINK` | 788, 793, 795, 796, 798, 987, 1013 |
  | `f2` | `ROLE_HEALTH` | 522, 527, 527, 531 |
  | `f3` | `ROLE_HEALTH` | 523, 524, **1686** |

  Minimum 522, mean 694, maximum 1686. A schema `0xF0` answer sits near 525 ms and
  `ROLE_GATELINK`'s larger `0xFE` near 795 ms, which is the airtime difference. **The single
  1686 ms is the interesting one**: about 1100 ms longer than that identity's other answers,
  which is the shape of one media-access backoff (`backoff_max_ms` = 1500) rather than a lost
  frame. `cad_backoffs` stood at 2 on the XIAO's radio counters. **Even so, the window is
  nearly six times the slowest answer measured.**

  These are one-hop, about 1 m apart, with four identities on one board and no other traffic,
  so they are a floor for the margin rather than a worst case. Impl Plan §6.1.1's derivation
  stands; nothing here argues for changing the number.
- **V-B3 passed.** `disable f0` gave `availability: simnode0 offline (missed_polls 3,
  threshold 3)` 2 min 46 s later; `enable f0` gave `simnode0 online` on the next poll, about
  40 s after.
- **Retained `offline` at the broker for both production nodes.** `lran/gatelink/availability`
  and `lran/welllink/availability` arrived live while subscribed, about 130 s and 140 s after
  boot, and neither node exists.
- **Discard counters, read at the broker.** `bad_crc` ×5 moved `rx_bad_crc` to 5 and
  `rx_dropped` to 5, and nothing else. Two `PING` frames between simnodes, which the bridge
  also hears, moved `rx_not_addressed` to 2 and `rx_dropped` to 7. **`hdr_rsv` moved no
  counter and was delivered** — the frame enrolled `f0`, which is how it shows as accepted.
  The rest of the §10.5 catalogue was not run.
- **No simnode topic reached the broker**, as spec §16.6 requires until BF-26. Only
  `lran/bridge/*`, `lran/gatelink/*` and `lran/welllink/*` appeared.

### The poll-to-answer measurement needed an instrument (`28ffd82`)

B3a requires poll-to-answer times recorded, and **nothing logged one**: the bridge printed
nothing per poll, and the simnode's debug log timestamps the `POLL` it receives but not the
answer it sends. Decided with the operator: add the instrument rather than estimate.

`PollScheduler::on_heard()` now returns the time from `on_sent()` to the answering frame's
receive time, or `kNotAnAnswer`, and `sched_on_heard()` prints
`poll: <node> answered in N ms (window N ms)` after releasing the scheduler lock. The time
includes the POLL's queue wait and its own media access, because the reply window starts at
`on_sent()` too. One new host test; a mutation returning a time for every frame failed it.

### `ROLE_FAULT` answers no POLL, by design

`f3` was added as `ROLE_FAULT` for the four-identity check and never answered a poll.
`node.cpp` answers `POLL` for `ROLE_RANGE`, `ROLE_HEALTH` and `ROLE_GATELINK` only, so a
`ROLE_FAULT` identity that is polled will always go `offline` after three misses. It is the
role's purpose, not a defect, but it reads as a node failure on the bridge's console. `f3` was
re-created as `ROLE_HEALTH` for the check. **A bench operator arming faults on a polled
identity should expect that node to go offline.**

---

## 2026-09-16 — B3a's §10.5 catalogue at the broker, and W9 between two boards

**Every §10.5 entry B3a owns was injected and read back, and one row cannot pass as
written.** The bench carried the bridge board on `28ffd82`, the XIAO running
`simnode-xiao-wio` as the injecting node, and the handheld Heltec as the second
transmitter for W9. Counters were read from `lran/bridge/diag/state` at the sandbox
broker, which publishes every 60 s — measured across 24 consecutive publications, so the
`kDiagPublishIntervalDefaultS` default is what runs.

Desk check first: 127 protocol, 7 link, 16 sim, 127 bridge, 108 simnode and 191 range-test
host tests pass, and the six repository checks pass.

**The counters started from zero because opening the bridge's serial port rebooted it.**
That is the documented trap doing what it does; it happened before the first injection, so
every number below is from one uninterrupted image.

### The catalogue, one entry per publication window

An entry was armed on identity `f1`, then the next window's counters were differenced
against the previous window's. `rx_dropped` is shown where it moved.

| `fault` | Counter movement | Against §10.5 |
|---|---|---|
| `runt` | `rx_runt` +1, `rx_dropped` +1 | as specified |
| `oversize` | `rx_oversize` +1, `rx_dropped` +1 | as specified |
| `bad_ver` | `rx_bad_ver` **+2**, `rx_dropped` +2 | **both frames rejected** — see below |
| `crit_ext` | `rx_unknown_hdr_ext` +1, `rx_dropped` +1 | as specified |
| `frag_zero` | `rx_bad_frag` +1, `rx_dropped` +1 | as specified |
| `unknown_type` | `rx_unknown_type` +1, `rx_dropped` +1 | as specified |
| `unknown_schema` | `rx_unknown_schema` +1, `rx_dropped` +1 | as specified |
| `bad_length` | `rx_bad_length` +2, `rx_dropped` +2 | as specified — the row sends one short frame and one long |
| `frag_command` | `rx_not_fragmentable` +1, `rx_dropped` +1 | as specified |
| `bad_mac` | `rx_rejected_mac` +1, `rx_dropped` +1 | as specified |
| `frag_timeout` | `rx_reassembly_timeout` +1, `rx_dropped` +1 | as specified, and it fired from the tick with nothing sent after the fragment |
| `frag_overflow` | `rx_fragment_overflow` +1, `rx_dropped` +1 | as specified |
| `frag_oversize` | `rx_fragment_overflow` +1, `rx_dropped` +1 | as specified — 15 frames, one discard, at the point the set exceeds the cap |
| `frag_dup` | `rx_frag_duplicate` +1, **`rx_dropped` still** | as specified |
| `frag_late` | `rx_frag_late` +1, **`rx_dropped` still** | as specified |
| `single_frame_interleave` | **nothing moved**, `rx_frames` +5 | as specified |
| `set_displaced` | `rx_reassembly_abandoned` +1 **and `rx_reassembly_timeout` +1**, `rx_dropped` +2 | **a second counter moves** — see below |
| `seq_jump` | nothing moved, `rx_frames` +2 | as specified |
| `seq_wrap` | nothing moved, `rx_frames` +3 | as specified |
| `flood` | nothing dropped, `rx_frames` +51 | as specified |

**A silent pass and a frame that never arrived look identical, so the four rows whose
correct result is "nothing happens" were checked against `rx_frames` as well.** Each shows
the injected frames arriving in the window it was armed in: `single_frame_interleave` +5,
`seq_jump` +2, `seq_wrap` +3, and the set completing with `rx_dropped` unmoved. Without
that second reading, a dead radio would have passed three entries.

**`flood` needs an explicit count.** `fault f1 flood` sends one frame, because the row is
one correct frame per injection and the count carries the burst. `fault f1 flood 50 gap 0`
delivered all 50 in a single window with none dropped: `q_rx_dropped` 0, receive-queue
high-water 1, and `diag/state` kept its 60 s tick. The queue never builds because SF9
airtime paces arrivals far below the drain rate, so this run says the bridge stays
responsive at the rate one simnode can transmit — not that the queue has headroom under a
faster source.

### `set_displaced` moves two counters, and §10.5's row cannot hold

**Run twice, an hour apart, with the same result: `rx_reassembly_abandoned` +1 and
`rx_reassembly_timeout` +1, both inside the window the fault was armed in.** §10.5 asks
that exactly the named counter move, and the table's introduction makes that the standard
for every row.

The cause looks like the row's construction rather than a receiver defect. The injection
sends a live set, then fragment 0 of a *different* set from the same peer. The first set is
displaced, which is the `rx_reassembly_abandoned` the row asks for. The displacing set is
then left incomplete by design, so it expires on the tick and counts a timeout. Nothing in
the receiver had a choice about the second counter.

**Raised, not patched.** Either §10.5's row states that a timeout necessarily follows, or
the fault completes the displacing set so only the displacement is counted. The second
changes `fault.cpp` and belongs with BF-21, which owns the scripted catalogue.

### `bad_ver` rejects both frames, and cannot pass until BF-22

The row expects `ver` N−1 accepted and N−2 rejected with a distinct reason (**V-B10**).
Both frames were rejected: `rx_bad_ver` +2. **This is version tolerance, which is BF-22 and
sits in B3b** — the Impl Plan §8 row for B3b names it. The row is not wrong and the bridge
is not defective; the entry simply has nothing to pass against until BF-22 lands.

### W9 — full-size and fragmented `PING` between two boards

The handheld Heltec sent from `f0` to a `ROLE_RANGE` identity `f3` added on the XIAO, so
the round trip crossed two radios rather than two identities sharing one.

```
H| ping f0 -> f3 seq 1: echo ok, n 202, 1 frame(s) out, 1 back, rssi -37 dBm, snr 11.0 dB, 2283 ms
H| ping f0 -> f3 seq 2: echo ok, n 202, 4 frame(s) out, 4 back, rssi -35 dBm, snr 11.3 dB, 3587 ms
```

Both identities ended with no reassembly errors, no CRC errors and no CAD backoffs.

**`n` is payload and caps at 202; the 222 in B3a's criterion is the frame.**
`ping f0 222` answers `ERR ping f0: n above 202`. With `kMaxFrame` 222, `kHdrLen` 16 and
`kCrcLen` 2, `kMaxPayloadPlain` is 204, and the ping header takes the last two bytes. A
full-size frame on the wire is `n` = 202.

### Two things that cost time, neither of them the firmware

**The broker address was wrong by one octet, and `mosquitto_sub` reported it as
`Error: Bad file descriptor`.** The bridge's banner prints `MQTT broker:` and settled it.
An unreachable host reads as a file-descriptor fault rather than a connect failure, which
sends the reader looking at the wrong layer.

**`mosquitto_sub` block-buffers into a pipe**, so `| tee` showed an empty file for minutes
while the subscriber was working. A Python subscriber with line buffering replaced it, and
is what produced the log this entry is built from.

**Bench cross-traffic moves the bridge's counters.** The W9 pings are addressed to `f3`, and
the bridge heard them: `rx_not_addressed` and `rx_dropped` both climbed by 8 in that window.
A catalogue entry read across a window that carries other traffic will not difference
cleanly.

### How this was driven, and what it is not

The injections were armed over the simnode console by a script holding the port open, with
counters differenced from the broker log. That is the same evidence a typed run produces,
and it is **not** BF-21: nothing is committed, the harness lives in this session's
scratchpad, and B3b still owes the catalogue as a `simctl` script.

---

## 2026-09-16 — spec v0.12: the nine questions, answered

**Every question B3a and B0 raised and left unpatched is decided, and B3b's gate is
clear.** Eight are **D35–D42** in the Decision Register; the ninth was a fact rather than
a choice and is **M24**. `LRAN-Spec-v0.12-Brief` carries the options and is superseded.

**One property was checked across the whole set rather than asserted.** No answer changes
a frame layout, a header field, an enumeration value, a schema or the authentication
scope, so `ver` stays `2`. **`generate.py` re-run against v0.12 reproduced all 72
committed vectors byte for byte**, and `check.py` re-derived them: 62 distinct valid
frames, 21 negative, no overlap. On a fleet with no OTA this is the difference between a
document change and a walk to every node.

### Two answers that changed shape while being written

**The obvious answer to the `ERROR` question is a reflection vector.** §14's `ERROR`
replies all fire before the sender is authenticated. Answering whoever asked means one
spoofed frame produces one transmission, at a rate the sender chooses, on a channel the
whole fleet shares. Silence was the other candidate, and it costs the field diagnosis
these counters exist to provide. **§14.2 takes the middle**: registered sources only,
rate-limited per source by `error_min_interval_ms` (default 1000), `ctx_id` `0`, and a
receiver must never adopt that zero as a context.

**`CONFIG` fragmentation was a contradiction, not a gap.** §11.4 called `CONFIG`
fragmentable and noted it exceeds one frame at 24 `uint32` entries. §3.1 caps a
reassembled schema-bearing set at 196 bytes — **exactly what one authenticated frame
already carries**. A fragmented config set could never carry one byte more than an
unfragmented one, so v0.4 through v0.11 described an encoding no conforming sender could
produce. Both sections had been read many times; neither had been read against the other
until BF-6 asked what a repeated `CONFIG` does. **v0.12 makes both types single-frame**,
and §11.5's argument for why fragmentation exists is rewritten, because that argument
rested on the same claim.

### M24 — the SX126x cannot filter node addresses in LoRa mode

BF-16 suspected this during bridge radio bring-up and recorded it as **unverified against
the datasheet**, where it sat through two revisions. It is now read:

- **`AddrComp` is GFSK `PacketParam5`** — SX1261/2 Rev 1.1, `DS.SX1261-2.W.APP`,
  December 2017, Table 13-56, under §13.4.6.1 *GFSK Packet Parameters*. `NodeAddrReg` is
  `0x06CD` and `BroadcastReg` `0x06CE` (Tables 13-57, 13-58).
- **The LoRa packet parameters are preamble length, header type, payload length, CRC type
  and invert-IQ** — §13.4.6.2, Tables 13-66 to 13-70. No address parameter, no address
  register.

LoRa discriminates by sync word, which the whole fleet shares. **§12.1's requirement is
withdrawn** and addressing is §14 stage 5, in software, which is what every firmware here
already does.

**The cost lands on §17.1**, which assumed a duty-cycled node could let the silicon drop a
frame addressed elsewhere. It cannot: every frame on the channel wakes the receiver and is
judged in software, so the power model that justified the design is unquantified. That is
**W14**, owed before WellLink is built on the profile and moot if **D19** makes WellLink
mains-powered. **The premise named no check for two revisions** — the falsification rule in
root `CLAUDE.md` exists for exactly this, and this is the second time it has caught
something after the fact rather than before.

### Two document defects found while editing

- **§13.2 required a changelog entry in `/docs/protocol-changelog.md`, a file that has
  never existed.** The changelog has always been §20. Eleven revisions were recorded
  correctly while the rule pointed elsewhere, so the requirement was met by practice and
  not by the text.
- **System PRD §12's version column was wrong in eight rows**, the Bridge Implementation
  Plan by fourteen revisions. The protocol citation is enforced by
  `tools/checks/spec_citation_version.py`; that column is enforced by nothing, and it
  drifts the moment any document is edited. Corrected, with a note saying which half is
  checked.

### The code now owes the specification one rename

**`unregistered_src` becomes `rx_unknown_src`** (§14 stage 9a), inside `rx_dropped`, which
makes `kCounterRegistry` 22 rows and changes a published number. It is **BF-15a**, and it
should land before B4 builds Home Assistant discovery on the old name. Until then the
bridge publishes the old name beside the registry rather than in it — deliberately, since
BF-15 chose a non-`rx_` name so it would not squat whatever the specification picked.

---

## 2026-09-16 — BF-15a: the bridge's own counter becomes the specification's

**`unregistered_src` is gone and `rx_unknown_src` has taken its place**, inside
`rx_dropped`, as spec v0.12 §14 stage 9a requires. Host-tested on both sides; **not yet
flashed**, so the bench board still publishes the old document.

**What moved.** `lran::Status::UnknownSrc` is new, and `Counters` gains `rx_unknown_src`
between `rx_rejected_mac` and `rx_reassembly_timeout` — ladder order, which is
`kCounterRegistry` order, which is the order the bridge publishes in. The registry is 22
rows and `sizeof(Counters)` is 25 words. On the bridge, `RxLadder` now bumps the codec's
counter and records `Status::UnknownSrc` as `last_status()`, so three pieces of
bridge-local plumbing were deleted rather than renamed: `unregistered_src_`,
`last_unregistered_`, the third output of `lora_diag_snapshot()` and `diag_rx_json()`'s
second argument.

**Four tests failed the moment the field was added, which is the whole design.**
`sizeof(Counters)`'s `static_assert`, the registry-length check, the independent
spec-name list in `test_framing` and the `in_dropped` column count all refused the
half-made change. The `-Werror=switch` on `Counters::bump` would have caught a missing
case as well. **None of these had to be looked for** — the build named them.

**Both vector registries needed the row and neither produced a diff.**
`tools/vectors/generate.py` and `check.py` each carry their own copy of §14.1, and
`check.py` refuses a vector naming a counter outside it. No vector names `rx_unknown_src`
— the bridge's registry raises it, not the codec — so all 72 vectors re-derived byte for
byte.

**The published document changes shape, and that is the reason this ran before B4.**
`lran/bridge/diag/state` loses the `unregistered_src` key, gains `rx_unknown_src` among
the counters, and **`rx_dropped` now includes it**. A Home Assistant sensor built on the
old key would have broken silently at whatever later date this landed; nothing is built on
it yet.

**What is not done.** No frame from an unknown source has crossed the air against this
build. The bench proof is BF-19a's to take, since the same path decides what is answered
and what is discarded in silence.

---

## 2026-09-16 — BF-19a: the bridge answers, under §14.2's two bounds

**The bridge sends §14's `ERROR` replies now**, built to spec v0.12 §14.2 and host-tested.
Nothing has gone on air: the §10.5 catalogue is the test, and six of its entries — 
`crit_ext`, `frag_zero`, `unknown_type`, `unknown_schema`, `bad_length` and `frag_command` —
should each produce a reply at the simnode.

**`error_reply.{h,cpp}` decides and `lora_task` sends.** The policy is Arduino-free and
I/O-free like `scheduler` and `node_availability`, because the bounds are what needs
testing and they are all decisions: which status maps to which `err_code`, whether the
source is registered, and whether the limit has elapsed. `lora_task` builds the frame and
posts it to the TX queue with no wait, so a reply takes its turn at media access behind
whatever is already waiting — an `ERROR` competes with polls for airtime, as it should.

**The stage-10 reply needed the ladder to say *what* expired.** `RxLadder::tick()` returned
nothing, and an `ERROR(REASSEMBLY_TIMEOUT)` needs a `dst`. It now reports each peer whose
set expired, read **before** the tick: `reassembly.h` documents `src()` and `seq()` as valid
"while `active()` or a set has completed", and after an expiry neither holds.

**A mutation test that passed, and what it taught.** Moving that read to *after* the tick
changed nothing — every test still passed. `Reassembler::reset()` clears the set's state and
**not** its key, so the accessors keep returning the expired set's values. The order is
still correct by contract and the wrong order is still a defect waiting on a library change
nobody would connect to it; the comment now says that, rather than claiming the code reads
zeros.

**What the mutations did catch**, each with the tests that bit:

| Mutation | Result |
|---|---|
| Drop the registered-source check | 2 failed |
| Rate limit never fires | 4 failed |
| Rate-limit the first reply of a boot | 8 failed |

**The first-reply case is the one worth keeping.** A table of zeroed timestamps against a
plain `now - last >= interval` refuses every peer's first reply for the first second after
boot — invisible, and exactly when a misconfigured node is most likely to be shouting. The
table seeds each new peer at one interval in the past instead.

**`BAD_CRC` and `BAD_VERSION` stay unbuilt**, as §14 marks them optional. A frame that
failed CRC has a `src` field that cannot be trusted to name its sender, so the reply would
go to an address chosen by corruption. An unreadable `ver` is **BF-22**'s to answer, with
the per-node downgrade in hand.

**Replies the limit withholds are counted** as `errors_suppressed` on
`lran/bridge/diag/radio/state`, beside the queue statistics rather than among the §14.1
counters. It is not a discard — the frame that provoked it is already counted by the stage
that discarded it, and counting the silence again would double it.

**`error_reply.cpp` joined `tools/checks/lora_task_never_blocks.py`'s file list**, because
it runs in `lora_task` for the same reason `rx_ladder.cpp` does. Five regions now, four
before.

## 2026-09-16 — BF-15a and BF-19a on air: the §10.5 ERROR rows, and what a half-duplex reply costs

**Both ends reflashed from `afd2178`, counters from zero.** Bridge banner `Version: 0.1.0
(afd2178)`, `Registry: 0x01 0x02 0xF0(bench) 0xF1(bench) 0xF2(bench) 0xF3(bench)`; the
simnode Heltec was reflashed in the same session because its running image still cited spec
v0.11. Every counter below is a cumulative total from that boot, read at the broker.

**BF-15a landed as a shape change in `diag/state`.** `rx_unknown_src` is present and
`unregistered_src` is gone. A counter document captured before this flash carries the old
key and the old `rx_dropped` sum; compare `lran/bridge/version` before trusting either.

### The eight §14 stages that name an ERROR, answered on air

Each row is one `fault f2 <name>` on the simnode Heltec's `ROLE_HEALTH` identity, read from
the simnode's own receive log.

| Entry | `err_code` | Spec 14 stage |
|---|---|---|
| `crit_ext` | `0x0A` UNKNOWN_HDR_EXT | 5a |
| `frag_zero` | `0x02` BAD_LENGTH | 5b |
| `unknown_type` | `0x03` UNKNOWN_TYPE | 6 |
| `unknown_schema` | `0x07` UNKNOWN_SCHEMA | 7 |
| `bad_length` | `0x02` BAD_LENGTH | 8 |
| `frag_command` | `0x02` BAD_LENGTH | 8a |
| `frag_overflow`, `frag_oversize` | `0x09` FRAGMENT_OVERFLOW | 10 |
| `frag_timeout` | `0x08` REASSEMBLY_TIMEOUT | 10, from the tick |

Every reply carried `detail` 0 and a `ref_seq` matching the offending frame's `seq`.

**`frag_timeout` is the one that needed new code.** It arrived 8 s after the fragment, from
the periodic tick and not from a later arrival — the `RxLadder::tick()` → `ExpiredSet` →
`reply_error` path BF-19a added, and the path whose capture-order mutation did **not** fail
a host test. It fires on air.

**The four silent rows stayed silent** and moved their counters, so each frame did arrive:
`runt` 1, `oversize` 1, `wrong_dst` 1 (`rx_not_addressed`), `bad_crc` 1. No ERROR for any.

### Bound 2 fired: `errors_suppressed` 2

`fault f2 unknown_type 4 gap 300` put four frames on air inside a second. The bridge counted
**three** of them, answered the first and suppressed two — `errors_suppressed` 2 exactly.
The rate limit had read 0 through every earlier entry, so this is the first time spec
§14.2's second bound has done anything outside a host test.

### The finding: an immediate reply and the sender's next frame deafen each other

**Two entries looked like defects and are one radio property.** `bad_length` queues two
frames (§10.5), and across two runs the bridge counted `rx_bad_length` **once** per arming,
while the reply it did send never reached the simnode. The burst above lost one frame the
same way.

The mechanism accounts for all three, and the bridge's own numbers confirm it rather than
merely allowing it: in the burst, 4 sent → 3 counted → 1 replied + 2 suppressed.

- The bridge answers at once, and **cannot receive while it transmits**. The sender's next
  frame arrives into a deaf receiver.
- The sender is transmitting that frame, so **it cannot hear the reply** either.

**Neither end is at fault and no counter is wrong.** Spaced single frames are clean:
`fault f2 unknown_type 2 gap 4000` produced two discards and two replies, `ref_seq` 18 and
19. **`gap` spaces injections, not the frames inside one injection**, so a multi-frame row
cannot be spread out this way.

**What this costs BF-21.** A catalogue row that emits more than one frame **cannot confirm
its own ERROR from the same board** — the reply and the row's later frames collide by
construction. A scripted catalogue has to either read those rows' results from the bridge's
counters alone, or drive the row from one board and listen on a second.

**It is not an argument for delaying the reply.** The bridge answers a frame it has not
authenticated; holding it in a queue to dodge the sender's own traffic would mean state kept
on behalf of an unauthenticated peer, which is what §14.2's bounds exist to avoid.

### What this run did not cover

- **Bound 1 is untested on air.** Answering only a registered source is host-tested in
  `test_error`, and `rx_unknown_src` stayed 0 for the whole run because every frame came
  from a provisioned bench address. Producing a stranger needs an identity outside
  `kNodeTable`, and `id add 05 health` did not take.
- **`bad_ver`, and every command-path row**, remain BF-22's and BF-18's.
- **The `set_displaced` question is still BF-21's.** Nothing here touched it.

### An instrument was missing and is now committed

The simnode routed `MsgType::Error` to `default: ++unhandled`, so its log recorded that an
ERROR arrived and nothing about what it said. Spec §14 maps eight stages onto six wire
codes, so without the code the catalogue cannot tell `crit_ext` from `unknown_schema` on
air — the whole point of the run. `Node::on_error` now logs `err_code`, `detail` and
`ref_seq`, raw: `lran::ErrCode` has no `to_string` and a bench instrument is the wrong
reason to grow one. The simnode still acts on none of it.

### Final counters, cumulative from the `afd2178` boot

`rx_frames` 49, `rx_dropped` 21, `q_rx_dropped` 0, `q_rx_high_water` 1, `tx_frames` 60,
`errors_suppressed` 2. `rx_unknown_type` reads 6 because that row was used for the spacing
and burst experiments as well as its own catalogue entry.

---

## 2026-09-16 — BF-18: the first authenticated frame the bridge has ever sent

**A command from Home Assistant reached a simnode, executed, and its `COMMAND_ACK`
came back.** Every authenticated type is bridge → node (spec §9.2), so until today the
bridge had sent none: `bad_mac` proved the rejection path on air this morning, and
nothing had proved the accepting one. Bridge flashed from `5f8e3f7`, XIAO simnode from
the same tree.

### What ran, and what each entry proves

The bridge published to `lran/<node>/cmd/ack` and the simnode's own log was read at
`log debug`. Times are the ack's arrival at the broker.

| Published | Ack | Proves |
|---|---|---|
| `cmd/nop/set` | `acked`, seq 1, attempts 1, result 0 | The derived key verifies at the node. **B3b's first criterion** |
| `cmd/set_relay_dry_run/set` `1` | `acked`, seq 1, **attempts 2**, result 7 | A lost ACK is retried with the SAME seq |
| `cmd/open/set` | `acked`, seq 2, attempts 1, **result 16** | The node's own `DRY_RUN` reaches Home Assistant unchanged |
| `cmd/close/set` `5` | `acked`, attempts 2, result 7, **detail 5** | Spec §6.3 — `DUPLICATE_CACHED` carries the CACHED result |
| `cmd/request_status/set` | `acked`, seq 1, attempts 1 | Spec §10.3 — the resync, below |

**The retry does not execute a second time, and the simnode says so in those words:**

```
cmd f1 <- 00 seq 1: OPEN ACCEPTED
fault f1 ack_suppress: ACK for seq 1 withheld, 0 left
rx  f1 <- 00 type 0x01 seq 1, 4 B in 1 frame(s)
cmd f1 <- 00 seq 1: dedup hit, DUPLICATE_CACHED (ACCEPTED), not executed
```

That is root rule 2 and **BS-3** on air, and **B3b's third criterion**. The retry
carries `seq 1` exactly as the first attempt did; at the gate the difference is a
second relay pulse.

**`detail 5` is the one worth keeping.** `close` with `arg 5` is out of range, so the
node cached `REJECTED_ARG` and the dedup hit replayed it in `detail` — a non-zero
cached result, travelling as spec §6.3 v0.12 requires. Read against a cached
`ACCEPTED`, `detail 0` is correct and indistinguishable from the field being unset,
which is why this entry was run with a rejection rather than a success.

### The resync happened, and it is invisible in the ack topic

`ctx f1 new` on the simnode, then a command, with the bridge still holding the old
context:

```
cmd f1 <- 00 seq 1: REJECTED_CTX (frame ctx 0x424b192b, own 0x1b0cadf8)
rx  f1 <- 00 type 0x01 seq 1, 4 B in 1 frame(s)
cmd f1 <- 00 seq 1: REQUEST_STATUS ACCEPTED
```

Spec §10.3 steps 1 and 2: the node rejected with its own `ctx_id`, the bridge adopted
it, reset the command seq to 1 and retried once. **The published ack read `attempts 1`**
— the resync restarts the attempt budget, which is a decision recorded at the code and
not a spec requirement. The consequence found here: *a resync and a command that never
resynced publish the same ack*, so **`lran/bridge/diag/cmd/state` was added in the same
session** rather than leaving the only evidence on a serial cable. It read
`cmd_submitted 2, cmd_sent 3, cmd_acked 2, cmd_resyncs 1` across two commands — the
third transmission is the resync, and `cmd_retries 0` is what separates it from a
timeout retry.

### A second REJECTED_CTX was not forced, after three attempts

**`ResyncFailed` stays host-tested.** Spec §10.3 step 3 stops the command rather than
resyncing again, and `test_a_second_rejected_ctx_stops_rather_than_looping` covers it,
but the bench could not produce it. The window between the node's rejection and the
bridge's retry is **under one second**, and the method available — racing `ctx f1 new`
from a second process against a command whose flight time varied between 4 and 9
seconds — has no resolution at that scale. Three attempts, all of which landed both
context changes on the same side of the exchange.

**What would make it deterministic is a simnode fault**, along the lines of
`ctx_reject <count>`: arm it and the identity answers the next N `COMMAND`s with
`REJECTED_CTX` whatever context they carry. That is a console race replaced by an
armed behaviour, which is what every other entry in the §10.5 catalogue already is.
Raised for **BF-21**, which owns the catalogue.

### Traps this run cost time on

- **A simnode command's flight time is 4–9 s from the MQTT publish**, not the ~1 s the
  radio alone suggests. `sched_task`'s 1 s tick, the TX queue behind the poll
  scheduler, and media access each add to it. Any bench step timed against a command
  needs that budget, and three of this session's attempts were lost to assuming ~2 s.
- **The XIAO simnode was still running a pre-v0.12 image**, banner `v0.11`. The
  2026-09-16 reflash that added `Node::on_error` went to the simnode Heltec only, and
  the hardware table's per-board rows are what say so. Reflashed before the run.
- **Opening the simnode's serial port reboots it and changes its `ctx_id`**, so the
  bridge's learned context goes stale on every reconnect. Useful for reaching the
  resync, and a nuisance for everything else: announce with `push f1` after each open.
- **A background capture piped into `tail` produced an empty file**, because the pipe
  buffers until the process exits. Redirect to a file instead.

### Counters at the end of the run

`rx_frames` 33, `tx_frames` 58, `cad_backoffs` 1, and **every §14.1 counter zero** —
`rx_dropped` 0. The command traffic produced no discards at either end.
`q_command_dropped` 0, `q_command_high_water` 1.

---

## 2026-09-16 — BF-21: the catalogue runs itself, and §10.3 step 3 finally fired

**`tools/simctl/` runs the §10.5 catalogue against the bridge's published counters**, and
the two decisions §10.5 left to BF-21 are made. Bridge unchanged on `5f8e3f7`; XIAO
simnode reflashed with the two firmware changes below.

### `ctx_reject` closed the criterion BF-18 could not

Earlier today BF-18 failed three times to force a second `REJECTED_CTX` by racing
`ctx f1 new` against a command. With `fault f1 ctx_reject 2` armed it fired first try:

```
fault f1 ctx_reject: seq 1 answered REJECTED_CTX (own ctx 0x7441d4d7), 1 left
rx    f1 <- 00 type 0x01 seq 1, 4 B in 1 frame(s)
fault f1 ctx_reject: seq 1 answered REJECTED_CTX (own ctx 0x7441d4d7), 0 left
```

and the bridge published `{"outcome":"resync_failed","seq":1,"attempts":1,"result":3}`.
**Exactly two `COMMAND`s, then nothing** — spec §10.3 step 3 stops rather than resyncing
again, which on a shared channel is the difference between one failed command and a
transmit storm. **B3b's "resync retries once and then faults" is now met in both halves.**

**The fault acts before the dedup gate**, and that is what makes it work rather than an
implementation detail. Spec §9.4 puts the context check at step 2 and the gate at steps
4–6: a node refusing on context has not looked at the sequence space, so no `seq` is
consumed and no result is cached. Cached, the bridge's resync retry would have met a
`DUPLICATE_CACHED` instead of a second rejection — the very path the fault exists to
produce.

### `set_displaced` now moves one counter, and the bench says so

The row sent a lone fragment 0 as its displacing set, so that set expired on the tick and
the row moved `rx_reassembly_abandoned` **and** `rx_reassembly_timeout` (measured twice,
this morning). It was the one row breaking the table's own invariant — the host suite
states it as *"the counter its row names, and only that counter, moves"* — and a row that
moves two cannot tell a displacement defect from a timeout defect. `fault.cpp` now
completes the displacing set. Read at the broker:

| Row | Result |
|---|---|
| `set_displaced` | **PASS** — `rx_reassembly_abandoned +1`, `rx_dropped +1`, `rx_reassembly_timeout` **flat** |
| `single_frame_interleave` | **PASS** — nothing discarded, `rx_frames +6` |
| `frag_dup` | **PASS** — `rx_frag_duplicate +1`, `rx_dropped +0` |
| `runt` | **PASS** — `rx_runt +1`, `rx_dropped +1` |
| `hdr_rsv` | **PASS** — nothing discarded, `rx_frames +3` |
| `bad_ver` | **DIVERGED** — `rx_bad_ver +2`, expected 1. Known, and BF-22's |

### What the tool enforces that a hand-run pass did not

**`rx_frames` must have moved before any other check is believed.** A silent pass and a
frame that never arrived are identical in every counter a forward-compatibility row cares
about; the handoff has carried that trap as prose since B3a, and it is now a condition in
code. Every row above reports its frame delta, and the deltas are **larger than the
injection** — `+3` where one frame was sent, `+6` where four were. The surplus is poll
answers arriving in the same window, which is why the check is a floor and not equality.

**Only the row's own counter may move.** This is what caught `set_displaced` in the first
place and what now proves the fix.

**A known divergence is neither a pass nor a failure.** `bad_ver` is reported as
`DIVERGED` with BF-22 named, and the run's exit status ignores it. A tool that scored it
as a failure would train its reader to ignore failures, and one that scored it as a pass
would hide the thing BF-22 exists to fix.

### Two things worth keeping

- **The 60 s diagnostic cadence sets the pace, and that was a deliberate choice.** One
  publication window per row, against the handoff's rule that a counter is differenced
  only across a window carrying nothing else. A full 23-row run is therefore about half
  an hour, unattended. The alternative considered and declined was a force-publish MQTT
  topic: it would have put a control surface on the receive path that `/lib/lran-config/`
  and BF-26 should own properly.
- **`test_console`'s transcript buffer held exactly 32 lines against a 32-row catalogue
  plus a header.** Adding `ctx_reject` pushed the last row off the end, and the failure
  read as a missing fault rather than as a full buffer. Raised to 64 with the reason
  written at the constant.

---

## 2026-09-16 — BF-22: version tolerance, and B3b's last criterion

**The bridge accepts N and N−1 and downgrades per node.** Spec §13.1, R-3.1e/f, **V-B10**.
Flashed from `24f7993`.

### The row that was red is green

`simctl --only bad_ver`, the same command that reported `DIVERGED` earlier today:

```
--- bad_ver ---
    stage 4 - N-1 ACCEPTED, N-2 rejected (V-B10). Two frames, one discard
    PASS  rx_bad_ver +1
          rx_frames +2
```

**`+1` where it read `+2` this morning**, with both frames arriving — so N−1 was decoded
and only N−2 refused. **V-B10 is met.**

### R-3.1f took the most thought, and nearly did not work

*"A node running an unsupported version SHALL be marked unavailable with a distinct
reason, never silently ignored."* The obvious reading fails: **a frame refused at §14
stage 4 never reaches the registry**, so the version that caused it is lost and the node
simply stops being heard and goes offline after three missed polls — indistinguishable
from a flat battery, which is exactly the silent ignoring the requirement forbids.

The version has to survive the discard. The ladder keeps the offending `ver` the same way
BF-19a kept `src` and `seq`: read from the buffer at a fixed offset, because a frame
rejected that early has no guarantee of a filled header.

**Carrying it between tasks is the part with a rule attached.** `lora_task` must never
call `registry_runtime`, which waits on a mutex. So the `(src, ver)` pair is published as
one atomic word that `sched_task` collects and clears each tick. On air:

```
ver: f0 speaks v0, this bridge accepts 1-2
```

**The reason is published on `lran/<node>/diag/state` as `unsupported_ver`, not on the
availability topic.** Spec §16.5 fixes that topic's payloads at `online` and `offline` and
Home Assistant depends on both tokens, so an unsupported node goes offline like any other
and its diagnostics say why.

**A bench node's per-node diagnostics are still gated** by `simnode_diag_enable` (spec
§16.6, BF-26), so `unsupported_ver` was read from the bridge's serial line above rather
than at the broker. The field is host-tested; the broker path arrives with BF-26.

### Two decisions worth keeping

- **N−1 only, never best-effort.** Spec §13.2 allows a field to change meaning across two
  versions, so parsing N−2 would decode a frame into the wrong shape and publish it as a
  plausible wrong number. A rejection is recoverable; a wrong number that looks right is
  not.
- **A node never heard is addressed in N, not N−1.** It is likelier to be new than old,
  and its first frame is a `POLL` it answers — after which the real version is known.
  Guessing N−1 would address every fresh node in a version it may not have.

---

## 2026-09-17 — M22's idle arm: 5.6 % PER at one metre, and no explanation that fits

**B3b is accepted and V-B12 moved to B4** (Impl Plan §8.1). Before it moved, its idle arm
was measured, because it is the baseline the saturated arm is compared against and nothing
blocked it. **The number is worse than expected and the cause is not established.** Both
facts are the point of this entry.

### The run

`tools/simctl/per_measure.py`, new today. Bridge on `24f7993`, XIAO + Wio simnode sending,
Heltec simnode quieted first. Five bursts of 50 frames, 250 ms apart, from **`0xF3` in
`ROLE_FAULT`** — a role that answers no `POLL`, so the only frames that identity sends are
the burst. Every other identity on both boards was disabled, so nothing else was on air.

```
PER 5.60 % over 250 frames in 5 valid window(s); worst burst 10.00 %
  never heard 14, corrupt 0, bridge transmissions in window 19
```

| burst | sent | accepted | never heard | bridge TX | bridge `cad_backoffs` |
|---|---|---|---|---|---|
| 1 | 50 | 45 | 5 | 3 | 8 |
| 2 | 50 | 46 | **4** | **0** | **0** |
| 3 | 50 | 49 | 1 | 7 | 5 |
| 4 | 50 | 49 | 1 | 3 | 0 |
| 5 | 50 | 47 | 3 | 6 | 5 |

Committed as `docs/bridge/data/m22-idle-2026-09-17.json`.

### What the numbers rule out

**`corrupt 0` across all 250 frames.** Not one frame arrived and failed its CRC:
`rx_crc_err` never moved, and neither did any other §14 counter. Every loss is a frame the
radio never delivered at all. At one metre, with −4 dBm into a 3.0 dBi antenna, an RF
explanation does not fit — and there is no marginal-link story that produces zero corrupt
frames alongside 14 missing ones.

**Burst 2 rules out the bridge's own media access as a complete explanation.** The obvious
first theory was half duplex: the SX1262 cannot hear a frame arriving while it answers a
`POLL`, and a CAD takes the radio out of receive (Impl Plan §747, and the 2026-09-10 entry
above). Burst 2 has **zero transmissions and zero CAD backoffs** and still lost 4 frames of
50. Across the five bursts the losses do not track either column.

**What `cad_backoffs` does not count is worth knowing before the next attempt.** It counts
a *busy* CAD result only (`media_access.cpp`). A CAD that returns free still took the radio
out of receive and increments nothing, and `cad_deferred` covers only the
reception-in-progress case. So a zero in that column is not a claim that the radio stayed
in receive — it is a claim that no CAD found the channel busy.

### What is left, and not tested today

**Receive turnaround is the remaining candidate.** At SF9 a frame of this size runs roughly
250–330 ms, and the injector's gap is measured from when it fired, so the frames are close
to back to back. Whether the bridge can read one frame and be listening again before the
next one starts is untested. A control at a 2000 ms gap was started at the end of the
session to separate that from link PER; **its result is not in this entry.**

**This is not a WiFi measurement and must not be read as one.** The saturated arm does not
exist on this firmware — nothing reaches `g_diag_interval_s` at runtime — so today's figure
says nothing about R-4.4 either way. **M22 stays open.** What it now has is a baseline, an
instrument, and a narrowed question.

### Two decisions worth keeping

- **PER is pooled over frames, never averaged over bursts.** Averaging these five bursts
  gives 4.4 %; pooling gives 5.6 %. The difference is small here and would not be with
  uneven burst sizes, where a short burst's single loss becomes the headline.
- **A window that cannot carry a figure returns a reason instead of one.** Three guards, all
  host-tested: a counter that went backwards (the bridge rebooted and zeroed everything),
  more frames accepted than were sent (something else transmitted into the window), and
  nothing on the air. The second one is the handoff's *"difference a counter only across a
  window carrying nothing else"* made mechanical.

---

## 2026-09-17 — the control: 0 % at a 2000 ms gap, so the 5.6 % is spacing, not the link

**The wide-gap control the entry above left open has run, and it answers the question.**
Same instrument, same identity, same bridge image:

```
PER 0.00 % over 40 frames in 2 valid window(s); worst burst 0.00 %
  never heard 0, corrupt 0, bridge transmissions in window 6, bridge CAD backoffs 0
```

| gap | frames | never heard | PER |
|---|---|---|---|
| 250 ms | 250 | 14 | **5.60 %** |
| 2000 ms | 40 | 0 | **0.00 %** |

Committed as `docs/bridge/data/m22-idle-control-gap2000-2026-09-17.json`. The bridge
transmitted in both control windows (6 times across the two), so this is not a quiet-bench
artefact — it lost nothing while doing the same work it was doing during the 250 ms run.

**Two things differed from the 250 ms run, not one, and the second is worth stating rather
than glossing.** The control was run with `--keep-others`, so `0xF1` stayed enabled on the
XIAO instead of being disabled. It contributed nothing: `accepted` equalled `sent` exactly
in both windows, and a single frame from another identity would have made `accepted` exceed
`sent` and tripped the guard that refuses the window. So the comparison holds, but it holds
because of a guard rather than because the runs were identical. **A repeat should disable
the others in both arms.**

**The losses are a function of inter-frame spacing.** At SF9 a frame of this size runs
roughly 250–330 ms, and the injector measures its gap from when it fired, so at `gap 250`
the frames are close to back to back and at `gap 2000` they are not. Nothing else differs.

**What this closes.** The previous entry listed three candidates and ruled out two by
measurement. The third — that the bridge cannot read one frame and be listening again
before the next one starts — is the one left standing, and the control is consistent with
it. **It is consistent with, not proof of**: this measures the bridge's behaviour at two
spacings and does not instrument the turnaround itself. What would prove it is a capture
that shows where the second frame goes, and that is not built.

**What this changes for M22, and it is the practical part.** The saturated arm must run at
a spacing whose idle PER is zero, or a WiFi effect cannot be told from this one. **`gap
2000` is a measured zero and `gap 250` is not**, so the saturated arm inherits the wide
gap and a longer run rather than the dense one. Written into the instrument's defaults is
deliberately *not* the answer — the dense case is worth keeping runnable, because it is the
only thing that has made this visible.

**What it does not change.** The bridge still drops frames offered back to back on a clean
bench at one metre, and nothing in the protocol prevents a node from sending that way — a
fragmented `STATUS` is exactly that pattern. **That is a real question about the receive
path, and it is not M22's.** It is recorded in the handoff's *Open* section rather than
being folded into a WiFi measurement that would obscure it.

---

## 2026-09-17 — the threshold is at 1 s, not at frame airtime, and that names a constant

**Backing the gap off in steps changed the conclusion**, which is why the sweep was worth
running rather than stopping at a spacing that happened to give zero. Same instrument, same
identity, same bridge image, 100 frames per point:

| gap | frames | never heard | corrupt | PER | bridge `cad_backoffs` |
|---|---|---|---|---|---|
| 250 ms | 250 | 14 | 0 | **5.60 %** | 18 |
| 400 ms | 100 | 5 | 0 | **5.00 %** | 11 |
| 700 ms | 100 | 1 | 0 | **1.00 %** | 8 |
| 1100 ms | 100 | 0 | 0 | **0.00 %** | 0 |
| 2000 ms | 40 | 0 | 0 | **0.00 %** | 0 |

Committed as `docs/bridge/data/m22-idle-sweep-gap{400,700,1100}-2026-09-17.json`.

### What the shape rules out

**It is not frame airtime.** At SF9 a frame this size runs roughly 250–330 ms, so the
obvious theory was that the bridge needs about one frame time to turn around. **At 400 ms
the frames are no longer back to back and the loss rate did not move** — 5.00 % against
5.60 %. A turnaround cost of one frame time would have collapsed there, and it did not.

**The losses fall away between 700 ms and 1100 ms.** That brackets **`kIrqReadMs`, which
is 1000** (`lora_link.cpp`). `service_receive` runs when DIO1 has fired **or** when that
poll interval has elapsed:

```cpp
if (!g_dio1 && elapsed(now_ms, g_last_irq_read_ms) < kIrqReadMs) return;
g_dio1 = false;
```

**`g_dio1` is a single `bool`, not a count.** Two frames whose interrupts both land before
one `service_receive` pass collapse into one pass, which reads one packet — and `readData()`
clears the SX1262's IRQ register, so the second frame's `RX_DONE` goes with it. The flag is
false again, so the next opportunity is the next DIO1 or the 1 s poll. Above 1100 ms every
frame gets its own pass regardless; below it, consecutive frames can share one.

### What this is, and what it is not

**It is a correlation with a named constant, not a proof.** The measurement shows where the
losses stop; it does not show a frame being dropped at that line. **What would prove it:**
change `kIrqReadMs` and re-run the sweep — if the knee moves with the constant, the
mechanism is this one. A counter on the path would settle it outright, and none exists.
**Neither is built, and this is recorded as unproved on purpose.**

**Media access is not the explanation, and the `cad_backoffs` column should not be read as
one.** It tracks offered load, because denser traffic makes a CAD more likely to find the
channel busy — so it falls with the gap for reasons that have nothing to do with the losses.
The discriminator remains the 250 ms run's burst 2: **zero transmissions, zero CAD backoffs,
four frames lost of fifty.**

### What it changes

- **M22's saturated arm runs at `--gap 2000`**, unchanged. 1100 ms is the measured knee and
  2000 ms is the margin either side of it; there is no reason to sit on the edge.
- **The receive path has a real question against it**, and it now has a number and a
  suspect rather than a shrug. A fragmented `STATUS` is exactly this traffic pattern, and
  **BF-24's decode work is what will meet it first**.
- **`gap` below about 1 s is not a valid bench configuration for anything that counts
  frames.** Two of the §10.5 catalogue's rows drive multi-frame injections; they judge
  counters rather than totals and are unaffected, but a future row that counts arrivals
  would be measuring this instead.

## 2026-09-17 — DIO1 is a level output read on its rising edge, and that is a mechanism, not a correlation

**Read out of the pinned driver, not measured.** The 1 s knee entry above named `kIrqReadMs`
as a suspect on the strength of where the losses stop. Reading RadioLib 7.7.1 turns the
suspicion into a described mechanism — one that is still unconfirmed on hardware, and that
now has a counter pointed at it.

### What the driver does

**`SX126x::setDio1Action` attaches on the rising edge** (`SX126x_config.cpp`:
`GpioInterruptRising`). **DIO1 itself is a level output**: the SX1262 holds it asserted for
as long as a masked interrupt is set and drops it only when `clearIrqStatus()` runs. The
mask `startReceive()` installs is `RADIOLIB_IRQ_RX_DEFAULT_MASK` — **`RX_DONE` alone**.

So a second `RX_DONE` raised while the first is still pending produces **no edge**. The line
never went low, so there is nothing to rise, and `on_dio1` never runs for that frame. The
frame sits in the radio's buffer with nothing to announce it.

**`readData()` clears the whole register** (`SX126x.cpp`), which is what eventually drops the
line — and takes any second frame's `RX_DONE` with it.

### What that makes `kIrqReadMs`

**The timed read is not belt and braces here; it is the only thing that finds such a frame.**
That reframes the constant: it is not a redundant poll behind a working interrupt, it is the
deadline a frame has to beat. A frame whose edge was swallowed survives if the timed read
reaches it before the next `readData()` clears the register, and is lost if it does not.

**This predicts the measured curve** — losses below a spacing of about 1 s, none above it —
**from the constant rather than from the constant's neighbourhood.** It also explains why the
knee is not at frame airtime, which was the hypothesis the 400 ms sweep point refuted.

**One thing the timed read is not optional for.** `HEADER_ERR` is not in the DIO1 mask, so
spec §14 stage 1's header half can only ever be found by the timed read. Removing the read
would stop counting those discards entirely, which root rule 4 forbids. **Any fix has to keep
a path that reads the register without an edge.**

### The counters, and what would falsify this

Two, in `LoraStats` on `lran/bridge/diag/radio/state`, with the classification lifted into
`rx_wake.h` so it has host tests:

- **`rx_no_interrupt`** — `RX_DONE` found by the timed read with no edge behind it. **A frame
  the interrupt path missed and the timer recovered.**
- **`rx_wake_empty`** — an edge arrived and the register held neither `RX_DONE` nor
  `HEADER_ERR`. The frame that raised it was cleared by an earlier `readData()`.

**Bridge-local, not spec §14.1**, decided rather than left open: §14.1 is a normative
registry of receive-ladder *discards* carried on the wire by schema `0xF0`. Neither of these
is a discard, neither has a `Status` or a stage, and both describe a driver no node need
share. `queues.h` reasons the same way about queue overflow.

**What falsifies the mechanism: `rx_no_interrupt` at zero during a 250 ms burst that still
loses frames.** That would say every frame got its own interrupt and the losses are
somewhere else entirely — and it would kill the `kIrqReadMs` story outright rather than
weakening it.

**What confirms it: `rx_no_interrupt` non-zero at *every* spacing, including the 2000 ms
control where PER is 0.** Missed edges would then be constant and the *recovery* interval
would be what separates a run that loses frames from one that does not. That is a sharper
claim than "the knee moved", and it is readable at the broker in one burst.

**Neither reading has been taken.** The bridge board is running `24f7993` and this needs a
USB reflash.

## 2026-09-17 — the falsifier fired: `kIrqReadMs` is not the mechanism

**`rx_no_interrupt` read zero across 190 frames spanning both arms, including the arm that
lost frames.** The entry above named the reading that would kill its own hypothesis, and
that is the reading that came back. **The `kIrqReadMs` story is dead as stated**, and this
entry supersedes the two above it on the mechanism while leaving their measurements intact.

The bridge board was reflashed from a clean tree at `4c83f3d` for these runs.

### The two runs

| Arm | Frames | Lost | PER | Bridge TX | CAD backoffs | `rx_no_interrupt` | `rx_wake_empty` |
|---|---|---|---|---|---|---|---|
| `--gap 250`, 3 × 50 | 150 | 4 | **2.67 %** | 10 | 11 | **0** | **0** |
| `--gap 2000`, 2 × 20 | 40 | 0 | **0 %** | 9 | 2 | **0** | **0** |

`rx_crc_err`, `rx_dropped` and `rx_driver_errors` were zero in both. **The spacing effect
reproduced** — it is not an artefact of the 2026-09-16 session.

### What that rules out

**The interrupt path delivered every `RX_DONE` the radio raised, at both spacings.** Not one
frame was found by the timed read. So the timed read never had to recover anything, which
means it was never the deadline a frame had to beat — the prediction the entry above made
from the driver's edge-versus-level reading.

**The predicted confirming signature did not appear either.** That entry said missed edges
would show as `rx_no_interrupt` non-zero at *every* spacing, with the recovery interval
separating a lossy run from a clean one. Zero at both spacings says there were no missed
edges to recover.

**Bridge transmissions do not predict losses.** Across the eight `--gap 250` bursts now on
record, transmissions against frames lost run 3/5, 0/4, 7/1, 3/1, 6/3, 4/2, 3/1, 3/1 — the
burst with no transmissions lost the most, and the burst with seven lost one. The control's
second burst transmitted six times and lost nothing.

### What it does not rule out, and this is the honest limit of the instrument

**A frame whose `RX_DONE` was cleared by a neighbouring `readData()` before any pass looked
is invisible to both counters.** In that case the first frame's edge is real, so the pass is
classified `Packet` and `rx_wake_empty` stays zero too. **The counters were built to catch
the recovery path and they cannot see this one.**

**It is too small to be the whole story.** That window is one `readBuffer` SPI transaction —
of order 1–2 ms against a 250–550 ms spacing, so roughly 0.3 % against a measured 2.67 %.
It is an order of magnitude short. **Stated as arithmetic, not as a dismissal:** it could be
a component.

### The rest of the radio document, read once at the end of both runs

`cad_deferred` **15**, `cad_backoffs` **18**, `tx_forced` **2**, `tx_frames` **32**,
`q_rx_high_water` **1**, every queue drop **0**, every §14.1 counter **0**, `rx_frames`
**186** against 190 sent.

Three things follow. **The RX queue was never under pressure** — depth 1 at its worst, so
nothing was lost after the ladder. **The media-access path was busy even though few
transmissions completed**: 15 CADs were not started because a reception was in progress,
which is the guard working. And **`tx_forced` fired twice** — spec §12.3's transmit-regardless,
the one path on which the bridge deliberately transmits into a channel it has not cleared,
and therefore the one that can destroy an arriving frame. Two of them against four lost
frames is not an explanation, but it is the only counter on record that describes the
bridge knowingly deafening itself.

### What is left

**A frame the radio never reported at all.** Not corrupt, not discarded, not queued and
dropped, not missed by the interrupt — and more likely the closer the frames are spaced.
Nothing in the firmware currently sees below `RX_DONE`.

**Two things would separate the remaining candidates, and neither is built:**

1. **The raw frame log — BF-27**, already a `TODO` in `service_receive`. Flood frames carry
   an incrementing status `seq` (`fault.cpp`), so a log of arrivals says **which** frames go
   missing. Scattered points at the chip or the air; clustered after a bridge action points
   at the firmware.
2. **A burst with the bridge provably never leaving receive.** Every transmit is preceded by
   a CAD, and **`cad_backoffs` counts only a *busy* CAD** — a free one still takes the radio
   out of receive and increments nothing. Running the flood with no node for the bridge to
   poll removes the whole path rather than measuring around it.

**Run 2 first.** It is one bench session with no firmware change, and it settles whether the
answer is above or below the driver.

### What stands from the entries above

- **The measurements.** 5.6 % at 250 ms falling to 0 % at 1100 ms, five spacings, nothing
  corrupt at any of them. Today's runs reproduce the endpoints.
- **The bench rule.** A measurement that counts frames still has to space them above 1 s.
- **The `HEADER_ERR` constraint.** It is not in the DIO1 mask, so the timed read is the only
  path that finds spec §14 stage 1's header half. That is unaffected by any of this, and it
  still forbids removing the read.
- **The counters stay.** They are cheap, they are published, and they turned a suspicion
  into a closed question in one bench session. Root rule 4 wanted them regardless.

## 2026-09-17 — the transmit path is ruled out: the same deaf time loses 6 % and 0 %

**The radio spends 1.07–1.10 % of every window out of receive, and that figure does not
move when the PER moves from 6 % to 0 %.** Media access does not explain the knee. This
entry closes the second of the two steps the entry above it named, and it supersedes
nothing: the measurements there stand.

The bridge board was reflashed from a clean tree at `d2212c9`.

### What was missing, and what was built

**`cad_backoffs` counts a *busy* CAD only** — spec §12.3 made it the channel's instrument,
not the radio's. A CAD that returns free still takes the radio out of receive and
incremented nothing, so no run on record could say whether the bridge was listening when a
frame went missing.

Two numbers now sit on `lran/bridge/diag/radio/state`, bridge-local for the reason
`lora_stats.h` already gives for `rx_wake.h`'s pair:

- **`cad_free`** — the CAD outcome nothing recorded.
- **`rx_deaf_ms`** — milliseconds outside receive, CAD and transmission together, measured
  from leaving receive to `startReceive()` re-arming it.

**`rx_deaf_ms` is the one that decides it, and a count would not have.** Bridge
transmissions were already tested against losses burst by burst and predicted nothing
(the entry above). A count of CADs is the same kind of correlation; a duration can be set
against the window it was differenced over and compared with the PER measured over that
same window. The interval also carries `lora_task`'s own latency in noticing `CAD_DONE`
and re-arming, which no count reaches.

### The two runs

| Arm | Burst | Sent | Lost | PER | Bridge TX | `cad_backoffs` | `cad_free` | `rx_deaf_ms` | Window | Deaf |
|---|---|---|---|---|---|---|---|---|---|---|
| `--gap 250` | 1 | 50 | 3 | **6.00 %** | 3 | 5 | 2 | 659 ms | 59 904 ms | **1.100 %** |
| | 2 | 50 | 1 | **2.00 %** | 3 | 4 | 3 | 643 ms | 59 950 ms | **1.073 %** |
| | 3 | 50 | 1 | **2.00 %** | 3 | 4 | 3 | 647 ms | 60 179 ms | **1.075 %** |
| `--gap 2000` | 1 | 20 | 0 | **0 %** | 3 | 0 | 3 | 643 ms | 60 099 ms | **1.070 %** |
| | 2 | 20 | 0 | **0 %** | 3 | 0 | 3 | 639 ms | 59 859 ms | **1.068 %** |

`rx_no_interrupt` and `rx_wake_empty` read zero in all five, as they did on 2026-09-17's
earlier runs. Nothing was corrupt and no §14.1 counter moved.

### Why this is decisive, and it is not the fraction

**The deaf time spans 639–659 ms — a 3 % spread — while the PER spans 6 % to 0 %.** The
same three polls, the same ~1.07 % of the window, in the burst that lost three frames and
in the burst that lost none. Whatever separates a lossy burst from a clean one, the
transmit path holds the radio out of receive for the same length of time in both.

That argument does not depend on comparing 1.08 % with 3.33 %, and it is worth saying why
the comparison is avoided. **The window is 60 s and the burst occupies only part of it**, so
a deaf fraction measured over the window understates the fraction during the burst if the
deafness is concentrated there. It is not — the polls are spread across the window — but the
constancy across five bursts settles the question without needing that assumption.

### The instrument agrees with the spec, which is the check that it works

**Three `POLL`s at SF9 are 555 ms of airtime** (§15.1: 185 ms each). Measured deaf time in
the same windows is 639–659 ms. **The 84–104 ms excess is three CADs and three re-arms**,
so roughly 28–35 ms per transmit cycle, of which a SF9 CAD is a few 4.1 ms symbols. The
counter reproduces an independently computed number and the remainder is the thing it was
built to see. **`lora_task` is not slow to re-arm receive** — that candidate is answered in
passing.

### The hole `cad_free` closed, in one row

**The control's `cad_backoffs` is 0 and its `cad_free` is 3, per burst.** On the old
instrument that window read as a bridge that never contended for the channel at all, while
the radio in fact left receive three times and stayed out for 643 ms. Every control run
before today carries the same blind spot.

**A second reading follows from it.** The `--gap 250` arm shows 13 backoffs against the
control's 0 and **the same deaf time**, which says those backoffs mostly cost no receive
time — consistent with `cad_deferred`, a CAD never started because a frame was arriving.
Stated as consistency, not as measurement: the per-window `cad_deferred` deltas were not
captured.

### What is left

**A frame the radio never reported at all**, and now with one more candidate gone. Not
corrupt, not discarded, not queued and dropped, not missed by the interrupt, and **not lost
to a radio that was busy transmitting**. More likely the closer the frames are spaced.

**BF-27's raw frame log is the next step and it is now the only one on the list.** Flood
frames carry an incrementing status `seq` (`fault.cpp`), so a log of arrivals says *which*
frames go missing: scattered points at the chip or the air, clustered after a bridge action
points at the firmware. The two cheap instruments are spent, and both came back clean.

**It still matters beyond M22.** A fragmented `STATUS` is exactly this traffic pattern, and
**BF-24's decode work meets it first**.
