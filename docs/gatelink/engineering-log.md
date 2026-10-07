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


## 2026-10-02 — `lib/lran-node` splits a readback; the bridge cannot yet ask for one

**The engine now answers a read in as many `CONFIG_ACK` messages as spec §7.4.1 allows.**
It collects an answer's results whole and sorts a full readback by `param_id`. It then
counts the messages and queues all of them or none. The order matters: the application
lists its rows in its own order, and the engine added the PHY group after them. For
GateLink the PHY rows (`0x0110`–`0x0115`) would have followed `0x1xxx`, against the spec's
ascending walk. A `SET` is still answered in one message. Five host tests cover the
split, the per-message `seq` rules, the four-message bound and the all-or-nothing queue.

**A repeated `GET` or `GET_ALL` was answered `DUPLICATE_CACHED`.** Spec §7.4.1 says to
walk the table again. The engine now does so for those two operations, and a repeated
write keeps the cached answer.

**The split was not shown on air, and it cannot be from this bench yet.** The XIAO Kit
ran the new image as `f1` in `ROLE_GATELINK`. The bridge's `GET_ALL` came back as one
`CONFIG_ACK` of 6 results, and the bridge closed it (`config: f1 outcome 1`). That shows
an answer that fits one frame is unchanged. Two bridge properties stop a larger one:

- The bridge names GateLink's rows only for node `0x01` (`lran::config::node_block()`).
  A simnode cannot take that ID: `is_simnode_id()` keeps it in `F0`–`F3`. Through
  `config/set`, `simnode1` takes the 4 non-PHY node-common rows and no more, so its
  readback stops at 68 bytes.
- A `config/set` of 512 bytes or more is refused before it is parsed
  (`kMaxInboundPayloadLen`). It is counted, but `config/ack` is never published, so
  Home Assistant gets no answer. All 23 of GateLink's non-PHY rows in one set came to
  about 660 bytes. A one-row set was answered `unknown_param` as expected.

The simnode's `ROLE_GATELINK` store now holds 23 rows, so a bridge that can name
GateLink's block for a simnode will draw GateLink's own 199-byte, two-message answer.

## 2026-10-05 — GL0 starts: a bring-up image, and a StamPLC the Mac cannot see

**The carrier is built.** The operator reports the 5 V and 3.3 V rails clean, the netlist
buzzed out on the board, and the Wio and the level translator seated, with the antenna
connected. The module is the header board, "Wio-SX1262 for XIAO" (p-6379), not the Kit.
Of expansion board §10's remaining items: R3 and R4 are fitted, and C10 is rated 50 V.
D2 is not fitted. The P6KE18A's maker is unknown, but its marking gives a 25.2 V maximum
clamp at an 18.9 V breakdown, which is under the AP63357's 32 V input limit. VE.Direct is
not wired, so measurement M4 and §11 step 5 wait until the MPPT comes to the bench.

**No radio test ran.** `gatelink-bringup` builds and is ready to flash. It walks §11
steps 1–4 as console commands (`firmware/gatelink/src/bringup.cpp`).

**RadioLib's `begin()` undoes the DIO1 pull-down.** `SX126x::modSetup()` in 7.7.1 sets
the IRQ pin to plain `INPUT`, which on the ESP32 clears a pull-down set before it.
Expansion board §7.1.1 and plan §4.1 both said to set `INPUT_PULLDOWN` before
`radio.begin()`. That alone leaves an open DIO1 conductor floating, so the bring-up
image sets the pull-down again after `begin()`, and both documents now say so. The
simnode's `radio.cpp` never sets a pull-down, and the Kit's DIO1 is a board trace.

**`SD.begin()` would start the shared bus on the wrong pins.** M5StamPLC's
`sd_card_init()` passes the global `SPI` to `SD.begin()`, which starts an idle `SPIClass`
on the board definition's default pins. On `esp32-s3-devkitc-1` those are G11–G13: BUSY,
the LCD's chip select and the internal I²C's SDA. The bring-up image calls
`SPI.begin(7, 9, 8)` before `M5StamPLC.begin()`. M5StamPLC leaves the SD card off by
default, which is why L6 never met this. M5GFX drives the panel in 3-wire SPI on the
same host, and its `endTransaction()` restores full-duplex for Arduino's SPI users.
Step 4 tests whether that is enough.

**With the 12 V supply on, the StamPLC's USB never enumerates.** No `/dev/cu.usbmodem*`
appears, and `ioreg -p IOUSB` lists no device at all. The schematics account for it,
though M5Stack's documentation does not mention it. The StampS3's USB-C VBUS reaches its
5 V rail through a 1 A PPTC with no diode (`Sch_StampS3_v0.3.3`), and the StamPLC feeds
that pin from `SYS_5V` through FU3 (`K141_sch_StamPLC_V10_CPU`). The board therefore
holds about 5 V on VBUS before the cable is plugged in. A USB-C host applies VBUS only
after it sees 0 V there, so the Mac never attaches. That last step is an inference from
the Type-C specification, not a measurement. **Falsified by:** a USB 2.0 hub with USB-A
ports between the Mac and the StamPLC, which has no 0 V check. If the port still does
not appear through the hub, the cause is something else. The carrier cannot be the
cause: the StampS3's USB is on G19 and G20, which reach neither the bus header nor the
Grove ports. GL0 waits for that hub.

## 2026-10-06 — GL0: the radio answers on the carrier, and R4 does not hold reset

**Through a USB 2.0 hub, the StamPLC enumerates with the 12 V supply on.** It appeared
as `/dev/cu.usbmodem1101`. That is the outcome the 2026-10-05 entry's falsification
check predicted, so the VBUS backfeed explanation stands. The bring-up image flashed
from `1e5048a`, and the banner reads `0.1.0 (1e5048a)`.

**§11 steps 2 and 3 pass.** Console output, from `tools/bench/bench.py`:

```text
reset: BUSY in reset high; after release low in 1619 us
begin: RadioLib status 0 - radio up
begin: 917400000 Hz, SF9, BW 125 kHz, CR 4/5, -4 dBm conducted, 3.0 dBi antenna
radio: version "SX1261 V2D 2D02" (status 0), sync word 0x1424 (status 0, expect 0x1424)
tx: #1 time on air 164864 us; DIO1 edge SEEN at 171385 us; IRQ 0x0001, TX_DONE set
```

The DIO1 edge arrived on the first transmit, so the PORT.A white conductor carries the
IRQ. The version string reads "SX1261" on an SX1262 as well; the part answered over SPI,
and the read-back sync word matches.

**R4 does not hold the radio in reset.** Right after boot, with G2 set to `INPUT`,
`pins` reads NRESET high and BUSY low:

```text
pins: NSS G41 high (expect high, R3)
pins: NRESET G2 high (expect low, R4)
pins: BUSY G11 low (expect high while in reset)
pins: DIO1 G1 low (expect low, pull-down)
```

A low BUSY means the radio really was out of reset, so this is not a threshold reading
near mid-rail. Something on the RST net pulls up harder than R4's 10 kΩ pulls down.
The candidates are a pull-up on the StamPLC's PORT.A or one on the Wio header board;
neither is confirmed. Firmware drives RST from `begin()` on, so bring-up is not blocked,
but expansion board §7.1's fail-loud reset at boot does not hold as built. **Falsified
by:** the RST voltage to GND at idle after a boot. A reading near 0 V means the `pins`
reading is wrong; otherwise the opposing pull-up is 10 kΩ × (3.3 − V)/V.

**Not run:** `txloop 50` with the 3.3 V rail on a meter, `sd` and `bus 60`. The board
moved to the workbench. The operator is weighing dropping the microSD card, because it
is a liability in an enclosure without climate control. That would leave NVS as the only
nonvolatile store.

## 2026-10-06 — The RST pull-up is on the Wio, and R4 cannot win against it

**With the board powered and G2 not driven, the RST net reads 2.3 V.** The operator
measured it on the carrier. It still reads 2.3 V with the PORT.A cable unplugged, so the
pull-up is not on the StamPLC. Only R4 and the Wio remain on the net, so the Wio pulls
RST up through about 10 kΩ × (3.3 − 2.3) / 2.3 ≈ 4.3 kΩ. Whether the resistor is on
Seeed's header board or inside the module is not known. Finding out means taking the
carrier out of the enclosure to pull the Wio, and that was not done. R4 could not be
measured in circuit with the Wio seated.

**R4 stays fitted at 10 kΩ.** Winning against 4.3 kΩ cleanly needs about 470 Ω, which
draws 7 mA whenever G2 drives high. Instead, the GateLink driver checks RST at boot: it
drives RST low and requires BUSY to rise, then releases RST and requires BUSY to fall.
Expansion board §7.1 and plan §4.1 now say so.

**SD testing is deferred**, along with the decision on whether to keep the card. Dropping
it would move persistence to NVS, which changes Protocol Spec §8.11's meaning of
`APPLIED_NOT_PERSISTED` for GateLink (D49). It would also leave the M14 baseline log and
D29's seasonal temperature log without a local store.

## 2026-10-06 — The 3.3 V rail holds through transmit at −4 dBm

**The 3.3 V rail read 3.32–3.33 V through `txloop 50` and `txloop 200`.** The operator
may have seen one brief dip to 3.31 V, possibly from poor probe contact. GL0 requires
≥3.2 V. All 250 transmits completed with a DIO1 edge and `TX_DONE`, at Envelope A's
−4 dBm conducted.

The meter was a DMM, which averages, so it cannot show the dip at each PA turn-on.
Each loop is about 172 ms, of which 165 ms is time on air, so the reading is close to
the rail voltage during transmit. A scope on the rail at the start of a transmit would
show the turn-on dip. The operator notes that the Wio is the only switching load on the
rail, and reads the result as the 100 µF capacitor doing its job.

**`bus` runs without a card now.** It required `sd` first. With the card's future
undecided, it runs the LCD and radio tasks alone when no card is mounted.

## 2026-10-06 — The LCD and the radio share the bus cleanly, without a card

**`bus 60` ran with no microSD card mounted**, on image `1d9f746`:

```text
bus: 60 s; LCD 4000 frames; SD off 0 ok 0 bad; radio 7989 ok 0 bad; tx after ok
```

The operator watched the panel throughout. The text stayed legible with no corruption.
The panel flashes because the test alternates navy and black fills at about 67 frames a
second, which is intended. This passes §11 step 4 for the LCD and the radio only. The
microSD leg waits on the decision whether to keep the card.

## 2026-10-06 — §11 step 4 passes with the microSD card, once the test stops starving IDLE0

**The 128 GB card would not mount until it was formatted on the board.** `SD.begin()`
failed with FatFs `(13) There is no valid FAT volume`. That error comes after the card
has initialised and its first sectors have been read, so the wiring was good. The core
builds FatFs with `FF_FS_EXFAT 0` (`ffconf.h`), and a card over 32 GB ships as exFAT.
`sd format` passes `format_if_empty` to `SD.begin()`. FatFs made a FAT32 volume of
121,942 MB in 61 s over SPI at 4 MHz, with the operator's permission to erase the card.

**The first two runs with the card tripped the task watchdog on IDLE0**, at 5 s and at
9 s. Each time `bus_lcd` was running on CPU 0, busy-waiting in M5GFX's
`Bus_SPI::writeDataRepeat()`. The suspected cause, an LCD clock that an SD transaction
left at 4 MHz, is ruled out: the longest LCD hold stayed at 14.3–14.6 ms throughout. Every
driver busy-waits on its own transfer, and the lock passes straight from one task to
the next, so the bus never idles. Three unpinned tasks at priority 2 kept IDLE0 off
CPU 0. No transfer failed in either run. The bus tasks now run on core 1 at priority 1.

**With that change, all three legs pass:**

| Run | LCD | SD | Radio | Transmit after |
|---|---|---|---|---|
| `bus 60` | 1,592 frames, max hold 14.6 ms | 1,588 ok, 0 bad, max 59.2 ms | 1,596 reads, 0 bad, max 0.23 ms | ok |
| `bus 5` | 135 frames | 133 ok, 0 bad | 134 reads, 0 bad | ok |
| `bus 20` | 532 frames, max 14.6 ms | 530 ok, 0 bad, max 51.1 ms | 531 reads, 0 bad, max 0.23 ms | ok |

The operator watched the panel through `bus 20` and saw no corruption.

**An SD write holds the bus for up to 59 ms**, and a radio operation waits behind it.
That bears on plan §5.2's ACK-timing question at GL3, if the card stays.

**The console holds back the last line of a long command.** After the 60 s and 20 s
runs, the summary line appeared only when the next command produced output. The 5 s
run's summary arrived at once. The cause is not established; it looks like the USB CDC
transmit buffer not being flushed. Send `stat` to flush it.

## 2026-10-06 — GateLink keeps the microSD card; the Wio's 10 kΩ does not explain 2.3 V

**The operator has decided to keep the microSD card.** Nothing in the plan or the
specification changes, since both already assume it. The bring-up `bus` test still runs
without a card, for a bench with none fitted.

**The Wio's schematic shows a 10 kΩ pull-up on RST.** Against R4's 10 kΩ, that alone
would hold RST at 1.65 V, not the 2.3 V measured. The measured 2.3 V implies about
4.3 kΩ of pull-up in total, so something else supplies about 7.7 kΩ in parallel. Two
candidates fit, and neither is checked: a pull-up inside the SX1262 on NRESET, or an R4
that is not 10 kΩ. An R4 of about 23 kΩ against the Wio's 10 kΩ alone would also give
2.3 V. **Falsified by:** R4 measured out of circuit, or RST measured on the carrier with
the Wio pulled. Neither changes the conclusion that R4 cannot hold reset. Expansion board §7.1
now cites the Wio's 10 kΩ.

## 2026-10-06 — GL1 starts: the relay expander does not reset with the ESP32

**A reset in the middle of a relay pulse lengthens it.** The four relays are P0_0–P0_3 of
the AW9523B at 0x59 on the internal I²C bus (M5StamPLC 1.2.0, `io_expander_b_init()`).
That chip sits outside the ESP32's reset domain, so a watchdog reset or a panic leaves its
output latch as it was. The relay stays energized through the boot ROM, `setup()`'s 200 ms
banner delay and `M5.begin()`, until `io_expander_b_init()` makes every pin an input.
PRD R-3.5j asks that no relay energize from reset; this is a relay that never
de-energized. Found by reading the installed library, not on the bench.

**The library's own sequence would also close a relay briefly.** For each relay pin,
`io_expander_b_init()` calls `pinMode(OUTPUT)` before `digitalWrite(false)`. With a 1 in
the latch, the relay is driven between those two I²C transactions.

**`board_relays_off_early()` is now the first call in `setup()`**, in the node image and
the bring-up image. It writes 0 to the output latch over a raw I²C write before anything
else runs. That ends the stretched pulse at the first instruction `setup()` executes, and
leaves a 0 in the latch for the library's sequence. It does not cover the boot ROM and
the core's start-up, before `setup()`. **Falsified by:** the GL1 scope check, a relay
output observed through a watchdog reset fired during a pulse.

**The library names the INA226's current the "IO socket output current"**
(`getIoSocketOutputCurrent()`, `M5StamPLC.h`). That names neither of the two readings
plan §3.4 weighs, the bank's current or the node's. M12 is still open.

## 2026-10-06 — GPIO 3 holds the IO expanders in reset after a chip reset

**The first bench run of GL1's image found the boot-time relay-off write unacknowledged**
after the upload's reset (`rst:0x15`, `USB_UART_CHIP_RESET`) and after a power-on with
USB unplugged. It was acknowledged after a software reset and after four
interrupt-watchdog resets (`rst:0xc`, `RTC_SW_CPU_RST`), three of them during a 2 s pulse
on K4. Clocking SCL to free the bus did not change it, and neither did the core's `Wire`
in place of M5Unified's `In_I2C`.

**A bus scan at the start of `setup()` found the cause.** After a chip reset, only 0x32
(RX8130), 0x40 (INA226) and 0x48 (LM75) answer. With GPIO 3 driven high, 0x43 (expander
A, a PI4IOE5V6408 by M5Unified's board table) and 0x59 (the relay expander) answer too,
and the write is acknowledged at once. GPIO 3 is the LCD's reset (`STAMPLC_PIN_LCD_RST`),
and M5GFX drives it inside `M5.begin()`. A CPU reset leaves it high, which is why those
resets behaved.

**What this means for PRD R-3.5j:**

- After a power-on, brownout or USB reset, the relay expander sits in hardware reset until
  GPIO 3 rises. Its outputs are then whatever its reset state gives, so the boot ROM is
  covered by hardware if that state leaves the relays off.
- After a CPU reset, the expander keeps its latch, and `board_relays_off_early()` clears
  it at the start of `setup()`. The boot ROM and the core's start-up remain uncovered.
- M5GFX pulses GPIO 3 again inside `M5.begin()`, which resets the expander to its
  defaults before `io_expander_b_init()` turns each relay pin to an output and then writes
  it low. The latch's reset default therefore decides whether a relay closes briefly
  there. The datasheet's default was not checked. **Falsified by:** the scope through a
  power cycle, which is GL1's R-3.5j check.

`board_relays_off_early()` now drives GPIO 3 high before the write. After the upload's
chip reset and after an interrupt-watchdog reset during a 2 s K4 pulse, the console
reported `boot relay-off ok` and `relays 0x00`.

**Pulse widths timed from the expander writes:** 499.9 ms at the default 500 ms and
99.9 ms at 100 ms. A pulse sent 0.2 s after K1's trailing edge was refused, as
`relay_min_spacing_ms` requires. The scope measurement is still to come.

**A 12 V power cycle with USB attached resets nothing.** USB VBUS keeps the StamPLC
running, and the operator saw the carrier's 3.3 V LED stay lit. The carrier draws only
from Bus pin 1, so something reaches that pin from USB, or the supply was not fully off.
Not investigated.

**The INA226 read 12,144 mV and 0 mA** with the StamPLC and carrier running and USB
attached. That fits the library's "IO socket output current", but the board may have
been drawing from USB, so it does not settle M12.

**`beep` printed `ledc_get_duty(745): LEDC is not initialized`** on its first use.
Whether the buzzer sounded was not recorded.

## 2026-10-06 — The buzzer sounds

**The operator heard a clearly audible beep during the session** but wasn't watching the
bench. The only `beep` sent was the one that logged `LEDC is not initialized`, and no
button was pressed, so that command most likely made it. The message looks like a log
line from the first `tone()` call, not a failure. Not yet confirmed by a `beep` sent
while someone is listening.

## 2026-10-06 — The node image shares the SPI bus cleanly from its own tasks

**GL1's concurrency criterion passes on the node image** (plan §8.2). `bus <s>` drives
the LCD from `ui_task`, the microSD from `log_task` and the radio from `lora_task`, each
at its plan §5.2 priority and unpinned, all under the `SpiLock`. Each device checks its
own transfer. The radio reads its sync word back every 5 ms, and the microSD takes one
append on each 20 ms `log_task` pass.

| Run | LCD frames, longest call | microSD ok / bad, longest call | Radio ok / bad, longest call |
|---|---|---|---|
| 60 s | 600, 25.0 ms | 2100 / 0, 23.8 ms | 7790 / 0, 15.5 ms |
| 300 s | 3000, 36.2 ms | 9007 / 0, 55.9 ms | 35764 / 0, 49.1 ms |

Neither run reset the chip or tripped the task watchdog, and `io_task` kept its 100 ms
period through both: its count in the 30 s `alive:` lines rose by 300 each time. A longest call includes the wait for
the lock, so the radio's 49 ms is mostly a microSD write ahead of it. The bring-up
image's IDLE0 starvation did not appear, because each task here sleeps between
operations rather than taking the lock straight back.

`radio.cpp` brings the SX1262 up in `lora_task` at boot, with RadioLib status 0.

## 2026-10-06 — M12: the INA226 sees neither the node's current nor Bus pin 1's

**The INA226 reads VIN, but its shunt carries neither the node's own supply nor the
carrier's draw through Bus pin 1.** The operator put a DVM in series with the 12 V feed
to VIN, with USB unplugged, and read the panel's INA226 line alongside it:

| Condition | DVM, 12 V feed | INA226 |
|---|---|---|
| StamPLC and carrier | 43.6 mA | 11,870 mV, 0 mA |
| Carrier unplugged | 41.2 mA | — |
| Carrier, plus 2.2 kΩ from Bus pin 1 to GND | 49.2 mA | 0 mA |

The carrier draws 2.4 mA, and the resistor adds 5.6 mA, close to the 5.5 mA that 12 V
across 2.2 kΩ predicts. The INA226 did not move off 0 mA for either. The library's
calibration (10 mΩ, 2 A full scale) resolves about 61 µA, and the part's offset is about
1 mA at most, so a 5.6 mA load through its shunt could not read 0. The panel showed whole
milliamps at the time.

M5StamPLC 1.2.0 calls the reading the current of "the right side io socket." The StamPLC
has one expansion socket, and the carrier is in it, so that name points at some output
on the socket other than Bus pin 1. Which one was not established.

**What this settles.** Plan §9.8, V-11 and PRD R-4.4b read the INA226 as the node's own
supply current, and expansion board §7.7 as the bank's current. Both are wrong. The bus
voltage, which reads VIN, is still usable as the bank's voltage behind the inline fuse.
The node's consumption needs another source, and choosing it is a requirement change, not
a fix made here.

**With USB attached, the 12 V feed carries 3.1 mA.** USB supplies almost all of the
StamPLC's load even with VIN at 12 V. That is why the first INA226 reading, 0 mA with
USB attached, settled nothing, and it fits the 12 V power cycle that reset nothing.

## 2026-10-06 — The inputs debounce against a bench switch

**`io_task` debounces a bench switch as plan §8.2 asks.** The switch fed 12 V+ to IN8,
with `EXCOM_COM` on 12 V−, which is the high-level wiring plan §3.2 gives IN1–IN4. At
lran-config's defaults, `input_poll_ms` 100 and `input_debounce_samples` 2, the debounced
value followed each raw change one poll later.

`in` now counts bit changes before and after the debouncer. Ten taps, as short as the
operator could make them, gave **20 raw edges and 16 debounced**. The poll sampled every
tap, and the two that lasted a single sample were rejected. A bounce shorter than
`input_poll_ms` falls between samples and never reaches the debouncer at all.

**IN8 is bit 7 and IN1 is bit 0**, matching M5StamPLC 1.2.0's `_in_pin_list`. The switch
was first wired to IN8 while meant for IN1. With the board mounted upside down, its
terminal labels read the other way round.

## 2026-10-07 — The StamPLC schematic answers M12, and R-4.4b drops node current

**The schematic settles what M12 measured.** VIN reaches the StamPLC's system rail
through a power MOSFET and a Zener, which protect against reverse voltage and transients,
and nothing on that path passes through the INA226's shunt. The shunt sits on the 5 V
output rail alone. GateLink does not use that rail, so the reading is 0 mA in this
application, which is what M12 saw.

**Reading the schematic first would have saved the bench time.** Five minutes with it
answers M12. The bench session took over half an hour and ended in the same place. A
measurement that asks what a host's own sensor sees starts from that host's schematic.

**The operator amended PRD R-4.4b (v0.17).** The node publishes VIN from the INA226 and
`node_ma` as its unavailable sentinel. No other current sensor is planned: node current
was a nice-to-have, never a requirement. V-11 now compares the BMS's overnight ΔSOC, which
is the whole bank's consumption, gate operator included, against the budget.
