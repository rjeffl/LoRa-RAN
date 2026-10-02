# GateLink Node — engineering log

**Dated record, appended to and never rewritten.** Measurements, surprises and the
things that cost an hour. Where this disagrees with a document, the document is the
current statement and this is what was true on the day. Implementation Plan §5.3 names
this file.

[`docs/README.md`](../README.md#conventions) says when the log is split.

---

## 2026-10-01 — L1: `lib/lran-node/` extracted from the simnode, host-proven, not yet on air

**The simnode's protocol engine is now `lib/lran-node/`, and the simnode runs on it.** The
simnode's 144 host tests pass, as they did before the move, and both of its target
environments build. The library's own suite adds 12. **No bench check has run**: plan §8.1
asks for `ROLE_GATELINK` back on air against the bridge, and for the BF-34 roll, the §10.7
reboot and the §12.4.2 PHY trial each to pass its bench check again. Until they do, L1 is
not closed.

**The boundary, as built.** `lran::node::Engine` holds every rule the specification
decides: the receive ladder after the codec, the `REJECTED_CTX` and `REJECTED_MAC`
refusals, the roll, both `CommandGate` calls, the `CONFIG` path through the PHY trial, the
HEX transport with `BUSY` and `TIMEOUT`, and the `BOOT` announcement.
`lran::node::Context` holds what one node owns. `lran::node::Application` is what the
node supplies: its capabilities, what a command does, its status and event bodies, its
parameters beyond the PHY group, and the device behind its HEX UART. The simnode's
`Identity` extends `Context`, and `Node::App` implements `Application` for all four roles.
`phy_trial.{h,cpp}`, the `Outbox`, the `Sink` and the spec's token names moved with it.

**`check()` and `record()` are separate steps**, as plan §5.2 requires. An application's
`execute()` may return `deferred`. The command then stays in flight, a retry gets no
answer, a roll answers `ACTUATOR_BUSY`, and `Engine::finish_command()` later records it and
sends the ACK. The simnode's `ack <hex> delay <ms>` now runs through that path rather than
its own `PendingAck`. `test_deferred_command_holds_the_execution_window` covers it on the
host. The engine holds no lock, so GateLink must call `finish_command()` on the task that
calls `receive()`, or guard both; `engine.h` says so.

**Four things changed beyond the move, and each is deliberate:**

- **The engine has no VE.Direct dependency.** The HEX response buffer is sized to what a
  `HEX_RSP` can carry, not to `vedirect::kMaxChars`. A node without an MPPT should not
  link a VE.Direct header to answer a HEX request.
- **The fault hooks are `Application` methods with no-op defaults.** `withhold()` carries
  the `silent` fault, `force_reject_ctx()` carries `ctx_reject` and still acts before the
  gate, and `fresh_ack()` carries `ack_suppress` and `ack_dup`. GateLink leaves them alone.
- **Four log lines read differently.** A deferred command logs `executing, ACK deferred`,
  not `ACK in <n> ms`. A HEX timeout logs `no answer by its deadline`, not `no MPPT answer
  in <n> ms`. `EVENT PHY_REVERTED` no longer prints the `event_id`. An unqueued status names
  its schema in place of the literal `0xFE`. The engine knows none of those values.
  `tools/simctl/` parses none of these lines; checked with a search on the day.
- **Five lines of the simnode's tests changed, and only their field paths.** `executions`,
  `hex_requests`, `hex_pending` and `phy_revert_detail` moved from `GateLinkState` to the
  `Context`, so `e.gl.executions` is now `e.executions`. No expected value changed.

**What the move did not settle.** `Engine::complete_hex()` exists for a device that
answers after `hex_forward()` returns, which is what a real UART does. Only the library's
test calls it: the simulated MPPT always answers at once or not at all. The `BOOT` and
`PHY_REVERTED` event bodies come from the application, because schema `0x11` is GateLink's.
WellLink will need its own when it is built.

## 2026-10-01 — L1's bench checks pass: the simnode on `lib/lran-node/` behaves as before on air

**All four of plan §8.1's L1 checks passed against the bridge.** The XIAO Kit ran
`simnode-xiao-wio` from `f223700`, flashed over USB on `/dev/cu.usbmodem1101`, MAC
`68:ee:8f:4b:85:f4`. It held `f1` in `ROLE_GATELINK` alone. The bridge ran `d8e45c3` on
`/dev/cu.usbserial-0001`, and the Heltec simnode was not connected. A harness held both
ports open and logged them, with `lran/#` from the sandbox broker, to two traces:
[`l1-bench-roll-reboot-2026-10-01.log`](./data/l1-bench-roll-reboot-2026-10-01.log) and
[`l1-bench-phy-2026-10-01.log`](./data/l1-bench-phy-2026-10-01.log). Times below are
seconds from the start of each trace.

| Check | What was done | What happened |
|---|---|---|
| `ROLE_GATELINK` on air | `simnode_diag_enable` and f1's `deployed` set to 1, then `PRESS` on `lran/simnode1/cmd/open/set` | f1 answered polls, and the bridge's charge readback drew `HEX_RSP`s. The node logged `OPEN ACCEPTED`. `cmd/ack` read `acked`, one attempt, 1.4 s after the publish |
| BF-34 roll | The bridge reset alone by RTS, then `push f1` so the bridge heard f1 | The bridge polled f1, then rolled it: `ctx 0xc97be29d -> 0x6bd68373, ACCEPTED`, and `roll: f1 rolled ... after 1 attempt(s)`. The traces hold three more bridge resets, and each rolled f1 in one attempt as well |
| Spec §10.7 reboot | `165` on `lran/simnode1/cmd/reboot/set` | The node logged `REBOOT ACCEPTED`, then `ACK on the air, restarting` 1.6 s later, then `rst:0xc (RTC_SW_CPU_RST)`. The bridge received the ACK and published `acked`, one attempt. The node booted as `REBOOT_COMMAND`, `boot_count` 154. Its `BOOT` status (`seq` 1) and then its `BOOT` event (`seq` 2) reached the bridge 210 ms apart |
| Spec §12.4.2, commit | `{"set":{"freq_hz":917000000}}` on `lran/bridge/config/set` | One node in the fleet. The bridge committed 6.9 s after it started the change, and `config/ack` read `ok`, `persisted`. `phy` on the node read 917.0 MHz committed, `trials 1 committed 1` |
| Spec §12.4.2, revert | 917.0 → 917.4 MHz. The harness held the bridge's EN low from 160 ms after `every node accepted - retuning` until 26.7 s after the node's window closed | f1 retuned at 89.10 and logged `REVERTED (window expired)` at 208.98, 119.9 s later. The bridge came back on 917.0 and published `phy_reverted` `restart`. Its boot `POLL` drew f1's `EVENT PHY_REVERTED detail 0x0001` as `seq` 6, ahead of the status at `seq` 7. The bridge counted the event in `event_frames` and `bench_withheld` |

**What the traces do not show.** No check ran with `ack <hex> delay <ms>`, so the engine's
deferred-command path has run on the host only. `Engine::complete_hex()` stays untested on
a board, as the extraction entry says. The `PHY_REVERTED` event reached the bridge's
`rxlog` and counters, not MQTT, because spec §16.6 withholds a bench node's events.

**Opening the bridge's port with pyserial reset it**, `rst:0x1 (POWERON)`. The harness set
DTR and RTS false before the open, and it reset the bridge both times it started. This
contradicts the simnode trap that recorded no reset on 2026-09-24, and that trap now says
to expect one. The XIAO reset on each open, as its own trap already said.

**The bridge's boot banner prints 917.4 MHz after a commit to 917.0.** The banner prints a
fixed string. The `LoRa: radio up` line two lines later reads the group the bridge runs on,
which was 917.0. Read that line, not the banner.

**The bridge logged `AUTH_FAIL` on its first WiFi attempt at every boot**, then joined.
At the session's start the sandbox broker was down, and the bridge's MQTT connects failed
with `Connection reset by peer`. Both cleared once the operator brought the broker up.

The bench was left as it was found. The fleet is back on 917.4 MHz, committed on the bridge
and on the XIAO, and `simnode_diag_enable` and f1's `deployed` are 0.

---

## 2026-10-01 — L3: the VE.Direct text parser, ported from osh-labs, host-proven on a synthesized block

`lib/vedirect/` now holds `include/vedirect/text.h` beside `hex.h`. `TextParser::feed()`
takes one UART byte and reports a delivered block, a dropped one or a finished HEX line.
`decode_mppt()` reads the 75/15's labels from a block. The suite has 12 new tests beside
the 12 for HEX, and the bridge's `heltec` and both simnode targets compile the new file
under `-Werror`. The port follows osh-labs at `fadcc4e` (2026-07-08).

**No real MPPT output has passed through it.** The repository holds no capture, and the
operator chose a synthesized block over waiting for one. That block takes osh-labs' sample
fields, adds `H19` and `H21` from spec §7.2.2, and adds the `FW` and `SER#` labels that
osh-labs names but does not decode. `OR` and `H23` are left out because neither source
defines them. L3's "captured block" criterion is therefore not met. GL4's first capture
should replace the block in `test_text.cpp`.

**osh-labs' specification and code disagree on a HEX frame inside a text block.** Its
specification (§6.4) says the text state is preserved across the interruption. Its code
abandons the block. This port follows the specification. The text checksum still guards
every block delivered, so resuming cannot deliver a corrupt one; at worst it fails one that
abandoning would also have lost. Each interrupted block that fails counts in `interrupted`,
apart from `bad_checksum`. **If `interrupted` grows while `bad_checksum` stays at zero, the
MPPT counts HEX bytes in its text checksum, and resuming is wrong.**

**osh-labs is not field-proven on a 75/15.** Its specification lists that controller's
firmware as "TBD at bench test". Plan v0.18 said the library was "proven in the field";
v0.19 drops the claim. Its §3.1 also shows the frame as `:Label\t<value>\r\n` lines. The
code reads `\r\n`-opened records and a bare checksum byte, and this port follows the code.

**Two changes to osh-labs' behaviour, both for root rule 4.** A block that fails its
checksum before the parser has seen a block boundary counts as `unsynced`, not
`bad_checksum`, so a reboot mid-block does not look like line noise. A block whose label,
value or field count overflows is dropped as `overflow` even when its checksum passes;
osh-labs folds that case into an invalid frame.

**Checksum bytes that look like delimiters are tested.** A checksum byte of `:`, `\t`,
`\r` or `\n` is read as the checksum and nothing else. The test finds each one by varying
`H19`, which reaches all four by 1029.

---

## 2026-10-01 — L4: GateLink's parameter block, and the two places it did not fit

`lib/lran-config/` now declares `kGateLinkParams`, 19 rows at `0x1000`–`0x1052`. Fifteen
are the parameters the PRD and plan §4.4 name. The operator chose four more, all rows the
PRD requires without naming: `relay_min_spacing_ms` (R-3.1.2b), `vedirect_stale_s`
(R-3.3f), `inject_spacing_ms` (R-5.4b) and `buzzer_enable`. Every default is a document's.
Every range is a proposal. The bridge discovers the rows from the table, and
`docs/gatelink/gatelink-config.md` is generated from it and checked in CI.

**Finding 2 is settled by renaming the PRD's term.** R-4.3i now says `tx_power_dbm`. Its
other half asks for `antenna_gain_dbi` and the envelope as runtime parameters. M21's handoff
lists the same obligation for every firmware. The operator kept those rows out of L4,
because they belong to every node and to the bridge, not to GateLink's block.

**The block does not fit one `CONFIG_ACK`.** A full readback is 131 bytes of GateLink rows
plus 68 of node-common and PHY, 199 against 193. Library Plan §4 had predicted a fit with
three `uint16` rows to spare. The four added rows and a `u32` for
`detect_sequence_window_ms` used that margin and six bytes more. A `u16` of milliseconds
stops at 65.5 s, against a 60 s default. The `Store`'s readback already splits into two
messages marked `MORE_FOLLOWS`, and a test shows it. **`lib/lran-node`'s engine does not
split.** Its `AckBuilder` drops the entries past one message and counts them. GateLink's
firmware needs the split before it answers `GET_ALL`.

**It did not fit one MQTT publication either.** `test_config` builds the widest document
a node topic can produce. With GateLink's block, `config/state` counts to about 1.9 KB
with every value at its widest. That is over the bridge's 1536-byte `kMaxPayloadLen`, and
a publication over the cap is refused. The operator chose to raise the cap to 2048. The
bridge log's entry for today has the RAM cost.

---

## 2026-10-01 — L5: node key provisioning, and the half that waits for L6

GateLink is the first firmware flashed with a derived key and not the master (plan §6.8).
`secrets.h.example` now carries `LRAN_GATELINK_NODE_KEY`, 32 zero bytes, and
`tools/provision/node_key.py` derives the real value from the master in the root
`secrets.h`. The tool imports `generate.py`'s HKDF, and its self-test reproduces all six
W4 `kdf` vectors through the tool's own parser and derivation.

**The tool refuses an all-zero master.** HKDF of 32 zero bytes is not zeros, so a key
derived from the template's master would pass the placeholder check on GateLink's banner.
The node would announce itself provisioned with a key no bridge holds, and every command
would fail its MAC with nothing pointing at the cause. The W4 test master is refused for
the fixture's own reason: it is never flashed.

**The boot check is half built.** `lran::key_is_placeholder()` is in `lib/lran-protocol/`
and host-tested, and the bridge and the simnode now call it in place of their own copies.
GateLink's banner and display cannot call it until `firmware/gatelink/` exists, so that
half is now an L6 acceptance criterion.

**Nothing in the build stops a node reading the master.** Every firmware includes the
one root `secrets.h`, which defines `LRAN_MASTER_KEY`. `tools/checks/node_holds_no_master.py`
fails when a firmware other than the bridge and the simnode names it, in code or in a
build flag, and runs in CI's `checks` job. It reads text, so it is a tripwire and not a
proof that the image is clean.

**An existing `secrets.h` does not get the new field.** The template's completeness block
requires it, but each copy carries its own block, so a copy made before today has no
check for it. The bridge and the simnode do not read the field and build unchanged.
GateLink's `main.cpp` must test for the field itself (L6).


## 2026-10-02 — L6: the firmware skeleton, built but not yet booted

`firmware/gatelink/` exists. It builds for the StamPLC against the committed template
(RAM 19.7 %, flash 558 KB of a 4 MB factory partition), and its 13 host tests pass. **No
StamPLC was attached this session**, so L6's "boots on a bare StamPLC" is not yet shown.
The bench check is a flash and a serial read: the banner, `Tasks: 7 of 7 started`, an
`alive:` line every 30 s, and the boot page on the panel.

**The environment pins the M5Stack libraries `wattcycle-reader` resolved, not the newest.**
Those are M5StamPLC 1.2.0, M5Unified 0.2.20 and M5GFX 0.2.27, with NimBLE-Arduino 1.4.3.
The PoC floated them on `espressif32@^6.9.0`; this build moves them to the fleet's 6.13.0,
and nothing has run them on that platform yet.

**RadioLib's `#warning` did not fire.** `-Wno-error=cpp` is set as plan §5.1 says, but
nothing includes RadioLib until GL0 adds `radio.cpp`, so the library is not compiled.

**The watchdog is not armed.** The bridge fixed its timeout at 10 s and argued the
exception to root rule 8 from having OTA. GateLink has no OTA, so that argument does not
carry over. Plan §5.2 now leaves the timeout to GL3.

**This machine's `secrets.h` lacks `LRAN_GATELINK_NODE_KEY`.** The local target build
stops at `main.cpp`'s `#error`, which names `tools/provision/node_key.py`. That error is
the behaviour plan §6.8 asks for. The target build above ran in a scratch copy with the
template as `secrets.h`, as CI builds it.

## 2026-10-02 — L6 boots on a bare StamPLC, after two fixes the bench found

**The skeleton passes the bench check in *L6 on the bench*.** On a StamPLC with nothing
attached, the image prints its banner and `Tasks: 7 of 7 started`, then an `alive:` line
every 30 s. Over five minutes, `io`, `app` and `ui` gained 300 a line, `vedirect` and
`lora` 30, and `log` 1. `bms` reached 2 at 300 s. The panel showed its four lines. Two
defects surfaced first, and the operator asked for two changes to the panel.

**Every `alive:` line lost bytes, its newline included.** A line read
`alive: io=301 vedirect=31 lora= ui=301`, and the next line ran on from it. `log_task`
wrote each line as eight `Serial` calls. The installed core, Arduino-ESP32 2.0.17, drives
the USB-serial FIFO from an ISR that ignores how many bytes
`usb_serial_jtag_ll_write_txfifo()` accepted. Small writes racing that ISR are the likely
loss; the fix supports that, but nothing here proves it. `log_task` now formats the line
into one buffer and writes it once. Ten lines in a row came through whole.

**The first `alive:` line interleaved with the banner.** `log_task` printed before
`setup()` had written `Tasks: 7 of 7 started`, so two writers shared `Serial` for a moment.
The first report now waits one period.

**`Reset: unknown` is correct for a reset from the USB-serial port.** The ROM reports
`rst:0x15 (USB_UART_CHIP_RESET)`, and ESP-IDF 4.4's `esp_reset_reason_t` has no value for
it. The code was left as it was.

**The panel turns 180° and insets each line 6 pixels.** GateLink's StamPLC mounts upside
down, and the case's bezel covered part of each line's first character. `board_begin()`
turns the display from the library's default rotation, so it keeps that default's panel
offset. The inset costs a column: a line holds 19 characters, not 20. A dirty build's
version line, `v0.1.0 abc1234-dirty`, was exactly 20, so the panel drops the `v`. The
serial banner never printed one.

**The M5Stack libraries run on `espressif32@6.13.0`.** This was their first run on that
platform. The display, the backlight and `M5StamPLC.begin()` showed no fault.

## 2026-10-02 — L2: `lib/bms-ble/` and `bms-protocol.md`

**The TDT protocol layer is a repository library now, and its 21 host tests pass under
`-Werror`.** `wattcycle-reader/lib/bms_ble/` moved to `lib/bms-ble/` with `git mv`, so its
history follows. Files are `snake_case` under `include/bms_ble/` and `src/`, and so are the
functions, methods and fields: `crc16_modbus()`, `decode_cells_and_pack()`, `cell_mv`,
`current_ma`. Enumerators became `kCamelCase`. No logic changed. The suite needed no edit
beyond the names and the header comment, and CI's `native` job runs it.

**wattcycle-reader builds against the moved library, and it lost its M5StamPLC pin on the
way.** Both targets now reach `../lib` through `lib_extra_dirs`. Changing `lib_deps` made
PlatformIO resolve the libraries again, and `m5stack/M5StamPLC@^1.2.0` fetched 1.2.1, with
M5Unified 0.2.24 and M5GFX 0.2.31. In that release `Display` is a method, so
`TftDisplay.cpp` stopped compiling. This is the drift the handoff's traps warn about. The
StamPLC env now pins M5StamPLC 1.2.0, M5Unified 0.2.20 and M5GFX 0.2.27, the set
`firmware/gatelink` pins, and NimBLE-Arduino is pinned at 1.4.3 in both envs. Both targets
build. Neither was flashed in this session, so the move is checked by compilation only.

**`bms-protocol.md` adds three things the PoC document did not have.** The full `0x92`
response is there, from the raw `aiobmsble` log; the PoC document elides it. NimBLE's MTU
of 512 on both boards is recorded, from the README. And which temperature sensor is which
is now an open item, because the PoC document labels them while `BmsData::max_temp_dc()`
says the labels are not established. Every other unverified item kept its wording, the
`pack_ma` sign (M7, W6) and `0x8D` included.

**`instrument.py` is not in the repository.** The PoC document's §10.2b describes it as
kept, and `bms-protocol` §12 says it is missing.

**The specification cites `/docs/bms-protocol.md`**, a path that never existed. That is
`doc-findings` 10, left for a specification revision.

**Task L7 joins the plan**, a Heltec V3 emulating the BMS from the §9 capture, by operator
decision 2026-10-02. The pack is not on the bench, and the emulator lets `bms_task` meet the
handshake gate, the 4 s drop and fragmented responses before GL5 reaches the pack. It
replays the captured bytes rather than encoding them with `lib/bms-ble/`, so a codec defect
cannot pass on both ends.

## 2026-10-02 — L7 moves into the simnode, and `instrument.py` is found

**L7 is a BMS peripheral inside the simnode, not a separate firmware**, by operator decision.
It runs on the `simnode-heltec` board. It is not a simnode role: a role is LoRa behaviour
assigned to one identity at bench addresses `0xF0`–`0xF3` (`identity.h`), and a BLE
peripheral has no address and is one per board. It is switched from the console instead,
off at boot, so the bridge bench sees the same simnode until someone turns it on.

**Starting Bluetooth on the simnode has one known interaction.** `main.cpp` keeps
`bootloader_random_enable()` on for `ctx_id` entropy, because the simnode ran neither WiFi
nor Bluetooth. That ADC source must be disabled before the radio starts. The L7 row says
so. Flash is not a constraint: the image is 403 KB in a 3.2 MB app partition
(`default_8MB.csv`).

**The operator's probe-development folder had the two missing scripts.** `instrument.py`
and `scan.py` are now in `wattcycle-reader/tools/`, with license headers added and nothing
else changed. That folder's `bms_probe_v1_0.py` is identical to the repository's copy. It
also holds `bms_probe_v0_7.py` to `v0_9.py`, which the repository does not have and this
session did not add.

---

## 2026-10-02 — L7: the BMS emulator runs on air, and its first run found a client defect

**The simnode Heltec emulated the BMS, and `wattcycle-reader`'s StamPLC target decoded it.**
The Heltec (MAC `44:1b:f6:fa:bc:2c`, `/dev/cu.usbserial-4`) ran `simnode-heltec` from this
branch. The StamPLC (MAC `50:78:7d:cd:c9:94`, `/dev/cu.usbmodem101`) ran `wattcycle-reader -e
m5stack_stamplc`, flashed over the L6 skeleton by operator decision, then flashed back to
`firmware/gatelink -e gatelink`. A harness held both ports and typed the simnode's console.

| Check | What happened |
|---|---|
| Discovery | `bms on 49A1`. The StamPLC found `XDZN_001_49A1` at −54 dBm, about 1 m away, and connected. MTU 512 was negotiated, and `FFF1`, `FFF2` and `FFFA` resolved with the §2 properties |
| Handshake | `HiLink -> FFFA: read-back 0x01 (ACK)`, then `subscribe FFF1: OK` |
| `0x8C` decode | Cells 3465, 3489, 3484, 3483 mV; temperatures 215, 242, 212, 211 (0.1 °C); 13920 mV; 0 mA with the discharge flag; SOC 100 %; 999/1000; 1 cycle; SOH 1000. These are `bms-protocol` §9's values. 18 polls over 90 s, `notify_failures 0` |
| `bad_crc` | `reassembler: CRC ERROR`, twice for a count of 2, then a clean decode |
| `bad_term` | `reassembler: BAD TERMINATOR` |
| `no_response` | The poll was sent and nothing came back. The next poll decoded |
| `drop_mid` | The link dropped after 20 bytes. The StamPLC logged `link dropped, resuming scan`, reconnected, handshook again and decoded |
| `split` | **Failed first, as `BAD TERMINATOR` on every split response.** Passes after the fix below |
| `bms off`, `bms on` | The StamPLC saw the link drop, and reconnected after `bms on`. `bms on` with no suffix advertised `XDZN_001_BC2D` |
| LoRa alongside BLE | Two `ping f0 16` sends with BLE connected gave `tx_frames 4`, with no TX errors or timeouts. Reception by the bridge was not checked |

**`split`'s failure was in the client, and the bytes proved it.** The server sent all three
notifications with return code 0. Bridge Impl Plan §10.9.4 explains why the peripheral calls
`ble_gattc_notify_custom` itself. A dump recorded inside `wattcycle-reader`'s `on_notify()`
showed 20, 20 and 3 bytes, correct and in order, 1 ms apart. The reassembler still reported
`BAD TERMINATOR`. With a `Serial.printf` inside the callback the same response decoded,
because the print slowed it.

**The cause was `FrameReassembler::tick()`'s unsigned age.** `wattcycle-reader`'s `loop()`
reads `now = millis()`, then blocks in `g_transport.write()` while NimBLE's host task feeds
the response, stamping its first byte with a later `millis()`. The loop then calls
`tick(now)`, and `(uint32_t)(now - started_ms_)` wraps to about 2³² ms. The partial frame
was dropped as timed out. The reassembler then resynced on the literal `0x7E` in cell 4,
and its bogus header's length ended on a non-`0x0D` byte. A frame in one notification
completes inside the callback and is never partial, so only a fragmented response failed.
**GateLink's `bms_task` would have met this whenever MTU negotiation failed**, at the gate.
`tick()` now computes a signed age and treats a clock older than the stamp as no timeout.
A host test reproduces the defect, and both split responses decode on the same hardware.

**What L7 does not show yet.** The pre-handshake drop at 4 s and a refused handshake are
host-tested only, because `wattcycle-reader` always handshakes. It polls `0x8C` alone, so
`0x8D` and `0x92` are decoded on the host and not on a board. The four L1 bench checks were
not run again: they need the XIAO, which could not be connected for lack of a USB port.
The Heltec image with the emulator off differs from the old one by an early return in
`loop()` and an OLED row that is empty while the emulator is off.

**The StamPLC's USB CDC console drops and splices lines** under this load, so several
results above were read from the counters on the simnode, not the StamPLC's log.

