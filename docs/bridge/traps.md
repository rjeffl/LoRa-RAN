# Bridge Node — traps

**Each entry here cost real time on the bridge or its bench, and most present as a
different fault from the one they are.** Read the section for the work you are about to
do. [`HANDOFF.md`](./HANDOFF.md) indexes the entries that matter for the current work;
this file keeps the full set. The engineering log has the full account of each one.

**This file is kept current, unlike the log.** Add a trap when one costs an hour. Remove
it when the code or the procedure makes it impossible, and record the removal in the
engineering log. Three board facts that code depends on live in
[`firmware/bridge/CLAUDE.md`](../../firmware/bridge/CLAUDE.md) and are not repeated here:
the Heltec V3's 1.8 V TCXO, the OLED behind Vext, and `MQTT_MAX_PACKET_SIZE`.

## Measuring frame loss

- **The bridge loses frames on a clean bench at one metre, and the spacing that protects a
  measurement is not known.** The morning of 2026-09-17 measured 5.6 % at 250 ms falling
  to 0 % at 1100 ms, twice. **The afternoon did not reproduce it**: ten bursts put 250 ms
  at 2.50 % and 2000 ms at 1.90 %, including a 2000 ms control that lost 8 % and a 250 ms
  arm that lost nothing. **So do not treat "space them above 1 s" as a safe rule** — one
  2000 ms control has broken it. **A bench measurement that counts frames needs a control
  arm in the same session**, whatever the spacing.
- **Interleave the arms; do not run one to completion and then the other.** Two sweeps on
  2026-09-21 put 250 ms at 3.91 % against 2000 ms at 0.31 % over 1280 frames, with the
  denser arm worse in all four run-by-half cells — and **run 1 also separated when pooled
  by session half**, so reading either two-way split alone gives a different answer.
  `tools/simctl/sweep_interleave.py` alternates the arms and prints the cross-tab.
- **2000 ms is not a zero-loss spacing.** It lost 2 frames of 640 on 2026-09-21, both in a
  gap holding a bridge transmission. Text that treats a wide gap as a clean control is
  correct for before that date.
- **The dense arm's rate moves by a factor of four inside one session, in either
  direction** — 1.88 % to 7.50 % in one sweep and 4.38 % to 1.88 % in the next. **A single
  burst is not a measurement**, and the variable behind that spread is unidentified.
- **`cad_backoffs` counts a *busy* CAD only.** A CAD that returns free still takes the
  radio out of receive and increments nothing. A zero in that column is not evidence the
  radio stayed in receive. **Read `cad_free` and `rx_deaf_ms` beside it since
  2026-09-17**; every run recorded before that date carries the gap.
- **`rx_deaf_ms` is read against the window it was differenced over, not on its own.**
  `per_measure` times each window and prints the fraction. The two spans are not perfectly
  aligned — the tool times the `rx` readings while the radio document is whichever arrived
  most recently — so read a fraction far below the PER as ruling the transmit path out,
  never as a figure to quote to two decimals.
- **A zero in `rx_no_interrupt` is a result, not an absence.** It means every `RX_DONE`
  the radio raised arrived with its own DIO1 edge. **What it cannot see**: a frame whose
  `RX_DONE` was cleared by a neighbouring `readData()` before any pass looked — the first
  frame's edge is real there, so the pass reads as an ordinary packet and `rx_wake_empty`
  stays zero too. Do not read a zero pair as "the receive path is clean".
- **A gap in the frame log's `seq` is not a lost frame until the type is checked.** W11: a
  `PING` responder echoes the initiator's `seq`, so a node's `PING` answers carry numbers
  from the *bridge's* sequence space. `rxlog_analyze.py` keys streams on `(peer, type)`
  for this reason. Reading the topic by hand without doing the same invents losses.
- **A frame-log record can go missing two ways and only one of them is the bridge's.** The
  ring overwrites when `log_task` falls behind, and that is reported as `lost` inside the
  payload; a record that left the bridge and never reached a subscriber is QoS 0, a broker
  restart, or a tool that started late. **`rxlog.py` reports them separately and they must
  not be added** — the second has nothing to do with the receive path.
- **A reboot of the bridge board zeroes every counter.** Read the counters, then leave that
  port alone for the rest of the run. `per_measure` refuses a window this happened in.
- **Bench cross-traffic moves the bridge's counters.** W9's pings are addressed to another
  node and the bridge still hears them: `rx_not_addressed` and `rx_dropped` both climbed by
  8. Difference a counter only across a window carrying nothing else.
- **An entry whose correct result is "nothing happens" needs a second reading.** A silent
  pass and a frame that never arrived look identical at the broker. Check `rx_frames`
  moved.

## Measuring the channel (M25)

- **A capture taken with a simnode powered up is not a channel measurement.** M25's
  sampler skips a reception only once a **valid LoRa header** is seen, and the preamble
  arrives first — roughly 33 ms of every one of our own frames, at about −37 dBm, lands in
  the samples as a large excursion. **Power both simnodes down for a baseline.** A capture
  with them running answers a different question and must not be pooled with one without.
- **`−127.5 dBm` in a capture is the encoding's rail, not a reading.** RadioLib returns
  `rssiRaw / -2.0`, so a raw `0xFF` reads exactly that; it appears in the first buckets
  after boot. The firmware counts it as a skip since 2026-09-17, but **a capture taken
  before that date carries it** and one sample drags a window's floor 12 dB below the
  campaign floor.
- **A `CHAN` line is not the denominator.** The firmware writes one only for a bucket that
  saw something. Occupancy is `above / samples`, and the samples live in the quiet buckets
  the `CHANSUM` rollups carry. Summing the `CHAN` lines divides the excursions by
  themselves and reports a nearly-silent channel as almost fully occupied.
- **A bucket that sampled nothing is unobserved, not quiet.** The sampler does not look
  while the radio is transmitting, doing a CAD, or down. `skipped` and `blind` say how much
  of a window was never seen.

## Bench boards and serial ports

- **Opening *either* board's serial port reboots it, the XIAO included.** Hold one port
  open for a whole run rather than reconnecting per command.
- **Opening the serial port can press PRG.** GPIO 0 is on the CP2102's DTR. Construct the
  port unopened and set `dtr = False` before opening.
- **A disabled identity re-enables itself on the next boot**, and opening a simnode's
  serial port reboots it. **Quiet both boards in the same session that runs the
  measurement**, and hold the ports open.
- **Two simnode boards boot with the same identities** (`f0`, `f2`), and opening either
  serial port resets its board to them. Reconfigure one in the same session that runs the
  test.
- **Opening the simnode's port changes its `ctx_id`**, so the bridge's learned context goes
  stale on every reconnect. Useful for reaching the resync deliberately; announce with
  `push f1` afterwards for everything else.
- **An identity in `ROLE_FAULT` answers no `POLL`** (`node.cpp`), which is exactly what a
  PER measurement wants and exactly what makes it go `offline` after three missed polls.
  Use `ROLE_HEALTH` for an identity that must answer.
- **A bench identity is polled only after the bridge has heard it.** `push` works for
  `ROLE_GATELINK`; `fault <id> hdr_rsv` announces any role and moves no counter.
- **A background serial capture piped into `tail` writes an empty file**, because the pipe
  buffers until the process exits. Redirect to a file instead, and run Python with `-u`.
- **`pio` is a shell alias on the macOS build machine.** A script that does not source the
  user's profile must call `~/.platformio/penv/bin/pio` by path, or every step fails as
  `command not found` while looking like a build failure. **The same applies to
  `python3`** — `pyserial` and `paho-mqtt` live in PlatformIO's environment, so a bench
  tool runs under `~/.platformio/penv/bin/python`, not the system interpreter.
- **`pio test -e esp32s3` overwrites whatever the board was running.**
- **The vendor header's `DIO0` on GPIO 14 is the SX1262's DIO1** on the Heltec V3. It
  fails without an error.

## Commands, the broker and MQTT

- **A command takes 4-9 s from the MQTT publish to the node**, not the ~1 s the radio alone
  suggests: `sched_task`'s 1 s tick, the TX queue behind the poll scheduler, and media
  access each add to it. Three of BF-18's bench attempts were lost to assuming ~2 s.
- **The bridge's ERROR reply and the sender's next frame deafen each other.** Half duplex:
  the bridge cannot receive while it answers, and the sender cannot hear the answer while
  it transmits. A multi-frame §10.5 row loses a frame and its reply to this, and it is not
  a defect at either end. **`gap` spaces injections, not the frames inside one injection.**
- **A `PING`'s `n` is payload and caps at 202.** `ping f0 222` answers `ERR ping f0: n
  above 202`. The 222 in B3a's criterion is the frame: `kMaxFrame` 222 − `kHdrLen` 16 −
  `kCrcLen` 2 leaves 204, and the ping header takes two.
- **`flood` sends one frame unless you give it a count.** `fault f1 flood 50 gap 0` is the
  row §10.5 describes; `fault f1 flood` is one frame and proves nothing.
- **A simnode PING to `00` reports no echo.** The bridge does not answer PING yet.
- **A bench node's `lran/<node>/diag/state` is not published at all**, so
  `unsupported_ver`, `proto_ver` and the per-node link are invisible at the broker for
  `f0`-`f3`. Spec §16.6 gates them on `simnode_diag_enable`, which **BF-26** has not built.
  Read the bridge's serial instead.
- **A bench counter check must be read at the broker**, not from the console: the bridge
  has no console, `diag/state` publishes every 60 s, and a reflash resets every counter to
  zero.
- **A retained message read at subscribe time is not evidence of this boot.** Compare the
  `version` payload, or wait for a live publication. `diag/state` is retained.
- **A wrong broker address reads as `Error: Bad file descriptor` from `mosquitto_sub`**,
  not as a connect failure. The bridge's banner prints `MQTT broker:`; believe it over a
  shell variable.
- **`mosquitto_sub` block-buffers into a pipe**, so `| tee` shows an empty file for minutes
  while it is working. Subscribe with a client that line-buffers.
- **The sandbox broker refuses anonymous clients** (`CONNACK 5`).
- **HA's entity registry remembers every `unique_id`, and a retained discovery config
  survives a reflash.** Develop against the dev HA VM and dev broker until **B6**.

## Boot, OTA and V-B9

- **`AUTH_FAIL` (reason 202) on the bridge's first WiFi attempt at every boot** is expected
  as of 2026-09-15, five boots out of five; a later attempt connects. Unexplained, harmless
  so far, and invisible except in the serial log.
- **V-B9 needs the broker.** The verdict requires `mqtt_connected`, so with the broker down
  a good image rolls back and reads as a firmware failure.
- **V-B9's bad images print the same version as the good image.** Read `Slot:`, the V-B9
  banner and the `WiFi SSID:` line to tell them apart.
- **An OTA upload can fail 1 s in with `Receive Failed`** while espota's progress bar
  climbs. Retry once before debugging.
- **A C++ `verifyRollbackLater()` links cleanly and does nothing.** It must be
  `extern "C"`.

## Code and build

- **`registry_begin()` must run before `start_tasks()`**, and **`lora_task` must never call
  `registry_runtime`**, which waits on a mutex.
- **The codec returns `Ok` for an authenticated frame it had no key to check.** Test
  `mac_verified`, never the status alone.
- **An `RxLadder` with no `PeerKeys` refuses every frame**, as `rx_unknown_src`. The
  handoff called this counter `unregistered_src` until 2026-09-18; BF-15a renamed it.
- **The library's platform crypto is not in its build.** `platform/esp32/` and
  `platform/native/` are added by each firmware's `build_src_filter`.
- **`lib_extra_dirs = ..` in a library's own test project loses `unity.h`.** Use
  `lib_deps = symlink://../<dep>`, as `lib/lran-link/platformio.ini` does.
- **ESP-IDF stack depth is bytes.** Size a stack from `uxTaskGetStackHighWaterMark`, and
  read it as a range: two boots differed by 248 bytes.
- **RadioLib's `scanChannel()` has no timeout and `transmit()` busy-waits.** Start the
  operation and read the IRQ register against a deadline, as `lora_link` does.
- **Receive routes only `RX_DONE` to DIO1.** `HEADER_VALID` and `HEADER_ERR` never wake the
  task; `lora_link` reads them on a 1 s poll and before a CAD.
- **Never ask `getPacketLength()` whether a packet arrived.** Gate on `RX_DONE`.
- **RadioLib's `SPIClass` `Module` constructor allocates on the heap.** Construct an
  `ArduinoHal` in static storage.

## Behaviour that changed before 2026-09-16

These changes make older artifacts read differently. **An older artifact is correct for
when it was made**; do not re-stamp it. Changes from 2026-09-16 on are in the handoff.

- **The simnode logs a received ERROR's `err_code` since 2026-09-16.** Before that it
  routed `MsgType::Error` to `default: ++unhandled`.
- **The bridge prints `poll: <node> answered in N ms (window N ms)` since `28ffd82`.** Text
  saying no poll timing is observable is correct for before it.
- **The XIAO simnode boots as `0xF1 ROLE_GATELINK` since BF-6**, not `ROLE_RANGE`.
- **`media_access` and the PHY constants moved to `lib/lran-link/` on the B0 branch.**
- **The handheld Heltec is a simnode from 2026-09-14.** Older text calls it the range
  test's board and says it never runs a simnode build.
- **The ladder refuses unregistered sources since BF-15.** It was counted as
  `unregistered_src` until **BF-15a renamed it `rx_unknown_src`** and moved it inside
  `rx_dropped`; the old name exists nowhere now.
- **`RxMessage` carries a decoded header and complete payload since BF-16**, not raw bytes.
- **`TaskSpec::stack_words` is `stack_bytes` since BF-16**; only `lora` changed,
  4096 → 8192.
- **The OTA verdict requires `radio_ok` since BF-16.**
- **`LoRaBridge` is retired in favour of `Bridge Node`, 2026-09-10** (**D17** amended).
  `lran-bridge` remains the firmware target and **"LoRa Bridge" remains the HA device
  name**.
- **The PHY parameters are stated rather than deferred, 2026-09-10.** A document citing
  Protocol Spec v0.9 or earlier reads §12.1 as "per D1" and `backoff_max_ms` as 500.
