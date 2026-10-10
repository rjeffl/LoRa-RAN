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

## 2026-10-07 — GL1's relay checks on a logic analyzer: pulses, power cycles and a watchdog reset pass

**The method.** A HiLetgo USB logic analyzer ran PulseView on the operator's Linux laptop,
sampling at 1 MHz. Each relay's COM went to analyzer GND, and its NO went to a channel with
10 kΩ to a pull-up supply, so a channel reads low while its contact is closed. The board ran
`main`'s image (`3beb863`). **The analyzer sees the contact, not the coil.** A coil
glitch shorter than the relay's operate time does not show, and every result below has
that limit.

**The pull-ups need their own supply for anything that powers the board down.** They were
first fed from the Wio's 3.3 V, so every channel would have read low the moment the board
lost power. A 5 V wall adapter replaced it for the power cycles.

**Pulse widths, contact to contact, at the default 500 ms:**

| Relay | Analyzer | Firmware (expander writes) |
|---|---|---|
| K1 | 499.3 ms | 499.9 ms |
| K2 | 499.3 ms | 499.9 ms |
| K3 | 499.3 ms | 499.8 ms |
| K4 | 498.2 ms | 499.9 ms |

All four are inside 500 ms ±10 ms with operate and release times included. One
single-sample low showed on K3 when K4 operated. The operator judged it noise from the
breadboard connections. A 1 µs low is far shorter than a contact can close, so it is not
K3's contact; coupling from K4's switching is likely but not shown.

**Five power cycles with USB unplugged: no channel went low.** The board came back with
`relays 0x00; boot relay-off ok`. This answers the 2026-10-06 entry's open question: the
relay expander's reset default, through M5GFX's GPIO 3 pulse inside `M5.begin()`, closes
no contact.

**A watchdog reset mid-pulse: no relay energized from the reset, but K4 held through it.**
`relay 4 2000`, then `hang` 0.57 s later. The interrupt watchdog reset the board
(`Reset: interrupt_watchdog`), and K4's contact stayed closed for 1262.46 ms. By the
console's timestamps that puts its release at about the moment `setup()` ran, so
`board_relays_off_early()` released it, not the reset: a CPU reset leaves the expander's
latch alone. No channel went low after K4's pulse. R-3.5j is met as worded, but the same
mechanism lengthens a pulse. A hang 100 ms into a 500 ms pulse would hold the relay for
the watchdog timeout plus the reboot, and a hang in `io_task` itself would hold it until
the watchdog fired. The panic output also printed `Re-entered core dump! Exception happened
during core dump!` before rebooting.

**The brownout leg was not run.** The bench supply adjusts from 5.25 V to 17 V, and at
5.25 V the StamPLC's MP4560 still regulates: no reset and no glitch. No supply on hand
reaches the board's reset point. The operator closed GL1 with the leg open. A VIN sag that
deep means the LiFePO4 BMS failed to cut the battery's output at low charge. A
catastrophic battery or BMS failure is beyond the scope of any failsafe operation.
**Falsified by:** a VIN sag through the reset point on the analyzer, if the brownout leg is
ever run. The 2026-10-06 entry's premise, that a brownout puts the relay expander into
hardware reset as a power-on does, is unmeasured.

**`bench.py run` exits when a port disappears and a command is then written to it.** USB
was unplugged for the power cycles. The read side logged `reopening` once a second, and
the next `send` raised `PortNotOpenError` at `bench.py:259`, which stopped the harness.

## 2026-10-07 — GL3's node image on the air: BOOT, the context roll, polls and PING pass

**The first node image's frames never left: every CAD hung the radio.** `lora_task` queued
the `BOOT` status and event at boot, and the console showed status `seq` 3 with no frame
transmitted. The new `radio` line gave `cad err 2`, `begin fails 2` and `last error
startReceive -707`. A CAD started cleanly, `CAD_DONE` never arrived within 500 ms, and
every command after it failed. Each failure dropped the frame, so the outbox emptied with
nothing on the air.

**The bring-up image's new `cad` step placed it outside the firmware.** With no tasks and no
panel traffic, `begin` succeeded and read back the version and sync word, and then a CAD
let BUSY fall at 5.6 ms with no DIO1 edge and IRQ `0x0000`. A `tx` straight after a fresh
`begin` failed the same way, though GL0 recorded 250 clean transmits on 2026-10-06.
`startChannelScan` afterwards returned `WRONG_MODEM`: the chip read back packet type GFSK,
its power-on default, so the radio was resetting when its RF chain powered up. The
operator found a power problem at the expansion board. With it fixed, `tx`, three CADs
from standby and from receive, and `txloop 5` all completed, each CAD with `CAD_DONE` at
20–25 ms.

**-707 is `SPI_CMD_FAILED`, not `SPI_CMD_TIMEOUT`**, which is -705. `radio.h` and
`bringup.cpp` said otherwise from GL1, and are corrected.

**On the air with the bridge, no broker.** The bridge restarted when the harness opened its
port, at 0.0 s below:

| t (s) | Bridge | Reads as |
|---|---|---|
| 1.44 | `rx peer=0x01 type=4 schema=16 seq=1`, `config: 01 rebooted` | The `BOOT` `STATUS` (spec 10.7) |
| 1.85 | `rx type=5 schema=17 seq=2`, `gatelink online` | The `BOOT` event |
| 2.05–2.86 | `tx type=1`, `rx type=2`, `roll: 01 rolled to ctx 0xe9f955c7 after 1 attempt(s)` | `ROLL_CONTEXT` and its ACK (spec 10.6) |
| 23.78–24.80 | `tx type=3`, `poll: 01 answered in 966 ms` | A scheduled `POLL`, answered with schema `0x10` |

GateLink's `lran` line read the same `ctx_id`, `0xe9f955c7`, and no refusal. The bridge's
type 8 frame at 18.92 s went unanswered, as expected: GateLink does not take `CONFIG` yet.

**PING from the simnode, identity `0xF0`, all echoed:** 32 bytes in 901 ms; 202 bytes
`pattern` in one 222-byte frame, 2460 ms; 202 bytes `pattern` in five 48-byte fragments,
five back, 4130 ms. RSSI −59 dBm, SNR 11.0 dB. This is GL0's ping and loopback item.

**Not run: every check that needs a `COMMAND`.** The broker at 192.168.2.52 was down, and
the bridge takes commands from MQTT alone. The ACK after the pulse, dedup of a retry and
the context resync wait for it.

**`Reset: unknown` after the harness opens the StamPLC's port.** The USB-serial-JTAG reset
is not among `main.cpp`'s cases, and `reset_cause()` reports it as `UNKNOWN` in the `BOOT`
event. GL3's reset-cause slice should name it.

## 2026-10-08 — M4 by DVM: the MPPT's TX has a weak high side; its low side is unmeasured

**The MPPT is the gate's 75/15, brought to the bench** and powered from a bench supply
with no PV connected. The gate runs from its battery meanwhile. The operator saved the
charger's settings from VictronConnect beforehand.

**The operator measured J4 pin 3, the MPPT's TX, with a DVM, not a scope:**

| Load on pin 3 | Reading |
|---|---|
| None | about 3.2 V |
| 10 kΩ to GND | 1.1 V |

The line idles high between 1 Hz text blocks, so both readings are the high level. They
put the high side's source impedance near 19 kΩ, from 2.1 V dropped at 0.11 mA. That is
weaker than plan §4.2.2's 10–11 kΩ, which came from Victron's 22 kΩ load figure. The
unloaded 3.2 V agrees with expansion board §7.4's 3.25 V.

**D25 is still open.** It turns on the low side, which a DVM cannot see on a 1 Hz stream.
A strong low with this weak high is the case plan §4.2.2 calls moot: the BSS138 works. A
low as weak as this high would sit near 3.3 V against the converter's 10 kΩ pull-up to
5 V, and no data would arrive. The bring-up image's `ved raw` tests the carrier's own
path instead, which is the question D25 exists to answer. Checksummed blocks through
J4 settle it for this carrier; nothing, with the cable metered, is D25's failure case.

## 2026-10-08 — VE.Direct works both ways on the carrier; the BSS138 passes the MPPT's lows

**The first build of the harness crossed TX and RX.** J4 is a 2×2 header and socket, not
the JST of expansion board §2, and it was wired to §6's pin numbers through the factory
VE.Direct cable, which is a crossover. The MPPT's TX landed on HV3, our TX channel. `ved
raw` saw no bytes. `ved edges`, which samples G4 and G5 as inputs with the UART released,
counted 1623 falling edges in 3 s on G5 and none on G4. The operator swapped the crimps of
pins 2 and 3, and §6 now names J4's pins for the MPPT pin each reaches.

**D25 is answered for this unit: the BSS138 stays.** Over the crossed cable, the MPPT's
lows passed channel 3 against its 10 kΩ pull-up, and over the corrected one they pass
channel 4 and R2. Both reached the ESP32 as clean lows, the shortest 45 µs against a 52 µs
bit. The weak 19 kΩ high side of the earlier entry pairs with a strong low side, which is
the case plan §4.2.2 calls moot. M4 was not run as specified, with a scope on a 10 kΩ
load; this is the carrier's own path doing the job instead.

**Text.** One block every 1000 ms, 19 fields, every block read continuously passed its
checksum, and `decode_mppt()` left none unparsed. The block carries `OR` and `H23`, which
neither osh-labs nor spec 7.2.2 defines. PID `0xA075`, FW `175`, battery 13.36 V at
−40 mA, `CS` 0 and `OR` `0x00000001` with no PV. One block, byte for byte, is now
`kCaptured` in `lib/vedirect`'s text tests.

**HEX round-trip.** Ping answered `0x4175`, AppVersion the same, and ProductId `0xA075`,
which agrees with the text block's PID. Each answered in 8–13 ms. Gets of BF-30's ten
registers and of `0x0201` answered in 14–15 ms, one in 111 ms with a text block in the
way:

| Register | Raw | Reads as |
|---|---|---|
| `0xEDF7` absorption | 1420 | 14.20 V |
| `0xEDF6` float | 1350 | 13.50 V |
| `0xEDF4` equalisation | 0 | 0.00 V |
| `0xEDFD` auto equalisation | 0 | off |
| `0xEDF2` temperature compensation | 0 | off |
| `0xEDF1` battery type | `0xFF` | user defined |
| `0xEDF0` maximum charge current | 150 | 15.0 A |
| `0xEDFB` absorption time limit | 200 | 2.00 h |
| `0xEDEA` system voltage | 12 | 12 V |
| `0xEDE0` low-temperature cut-off | 500 | 5.00 °C |
| `0x0201` device state | 0 | off |

The simnode's simulated MPPT holds 1420 in `0xEDF4`; this unit reads 0.

**The first HEX contact sets off an Async burst.** The scan's first Get, of `0x0100`,
drew about 25 unsolicited `:A` frames and no reply inside 1000 ms. The same Get answered
`0xA075` on the next try, and the second scan answered 12 of 12. The node's VE.Direct
task should expect the burst and retry a timed-out Get. Async frames also follow single
requests; the console discarded each as unmatched without losing its transaction.

**Three blocks failed their checksum, and the console caused them.** Nothing reads
Serial1 between console commands, so its 1 KB buffer overflows and loses bytes. Every
failure followed such a gap.

## 2026-10-08 — The readback matches VictronConnect, and M4 closes in circuit

**The operator compared the scan with the VictronConnect iOS app** on the Battery settings
page, and every value the app shows matches. The app showed no equalisation voltage, so
`0xEDF4`'s 0 has no second reading.

**M4 and D25 closed, by the operator's decision**, on the in-circuit result above rather
than a scope reading. The BSS138 stays in both directions. Decision Register v0.29.

## 2026-10-08 — `vedirect_task` on the carrier: readback over the air, and staleness

**Image `cb92ff6`, then `177b8e3`, on the carrier at the gate's MPPT, with the bridge on
the air and no broker.** The bridge's BF-30 readback starts once it hears the node, so it
ran without MQTT and exercised the whole `HEX_REQ` path.

**All ten BF-30 registers came back through the node, each on its first attempt.** Every
`HEX_RSP` carried status 0 and the MPPT's reply verbatim. The values are the scan's
above: `0xEDF7` 1420, `0xEDF6` 1350, `0xEDF4` 0, `0xEDFD` 0, `0xEDF2` 0, `0xEDF1` `0xFF`,
`0xEDF0` 150, `0xEDFB` 200, `0xEDEA` 12 and `0xEDE0` 500. The bridge paced them over
about 54 s.

**The first Get met the Async burst and still answered.** The node counted 8 Async frames
before the reply, and no retry was needed. So the retry at half of `hex_timeout_ms` has
not yet run on the board; the native suite is all that covers it. After the readback the
MPPT kept sending Async frames: the next boot counted 97 in about 150 s with no request
sent.

**Staleness, PRD R-3.3f.** With the console sampling `ved` every 2 s, the operator pulled
the VE.Direct cable for about 15 s. The last block arrived at 122.8 s. Bit 1 was clear at
126.9 s and set at 129.1 s, 6.2 s after it, against `vedirect_stale_s` of 5. The values
held at the last block's while the bit was set. The bit cleared by 143.3 s, after the
cable went back in. No block failed its checksum across the pull: 108 good, 0 bad.

**The USB console loses bytes from the middle of a line.** The 190-byte `ved` line lost
about 60 bytes, so `177b8e3` split it in two. The shorter lines then lost bytes too, one
in about ten, so line length is not the cause. The loss is in the console, not in
`vedirect_task`: the counters it prints agree with each other.

**`vedirect_task` passes about 240 times a second**, against the 50 its 20 ms wait alone
would give. Serial1's receive callback wakes it for each burst of bytes. Nothing has been
starved yet.

## 2026-10-08 — Writes through the broker, and 30 days of MPPT history

**Bridge, simnode Heltec and the StamPLC on the bench, with the sandbox broker; the MPPT on
its bench supply with no PV.** Traces: [`data/gl4-write-gates-2026-10-08.log`](data/gl4-write-gates-2026-10-08.log)
and [`data/m14-mppt-history-2026-10-08.log`](data/m14-mppt-history-2026-10-08.log).

**The StamPLC was silent until it was replugged.** For 135 s after the bridge booted,
`lran/gatelink/availability` stayed `offline`, and macOS gave the StamPLC no
`/dev/cu.usbmodem*`. After a replug it booted `177b8e3`, rolled its context and came
online. What it was doing before the replug is not known; its console was unreachable.

**The bridge refused a Set while disarmed, and built no frame for it.** A Set of `0xEDF0`
to 14.0 A (`:8F0ED008C00E4`) drew `refused_disarmed` on `hex/response` and a retained
`hex/audit` entry with `authorization` `disarmed`. The bridge's frame log shows no
transmission between the request and the refusal. PRD R-3.5b's second gate passes on the
bench.

**Armed, the same Set reached the MPPT and read back.** `write_enable/set ON` armed it,
and `write_enable/state` followed. The MPPT echoed the Set with flags 0. A Get of
`0xEDF0` returned `0x008C`, and the bridge's own readback after the write published
`charge_max_current_a` 14.0. A second Set restored 15.0 A, `OFF` disarmed it, and the
readback showed 15.0 again. Each write's audit entry carries `authorization` `armed` and
the MPPT's reply.

**A history Get timed out at the node, though the MPPT answered it.** The first Get of
`0x1050`, today's history record, came back `status` timeout. The node's counters showed
one retry and two unmatched replies. `VedLink::answers()` matched a Get through
`vedirect::reg_reply()`, which accepts at most 4 value bytes, and a history record carries
34. `cbacd3f` matches on the echoed register alone. On that image every Get from `0x104F`
to `0x106E` answered on its first attempt. The first history Get was also the first time
the Get retry ran on the board.

**The history gives M14 a starting point, not its result.** M14 asks for a week logged
at the gate before install, and the MPPT is off the gate's PV, so it stays open. The
records are the MPPT's own, kept while it was on the gate:

| | Days 1–30 |
|---|---|
| Daily yield | 0.03–0.14 kWh, most days 0.08–0.10 |
| Daily Vbat min | 12.44–13.19 V |
| Daily Vbat max | 13.64–14.23 V |
| Daily Pmax | 19–60 W |
| Daily Vpv max | 19.13–21.79 V |
| Errors | none recorded |

Day 0 is the bench: no yield and a Vpv max of 0.01 V. Day 1 has 312 minutes of bulk and
no absorption, so it may be the partial day the MPPT came off the gate. The record does
not say. The totals record's yield, 467, matches the text block's `H19` of 4.67 kWh. The
layout used is Victron's history record: yield and consumption in 0.01 kWh, voltages in
0.01 V, Imax in 0.1 A, and bulk, absorption and float in minutes. The rest of the
totals record is not decoded.

## 2026-10-08 — GL3's command path on the air: ACK after the pulse, the in-flight retry, the dedup hit and both resyncs

**Bridge and StamPLC on the bench, with the sandbox broker; no simnode.** Every command
went in on `lran/gatelink/cmd/<action>/set`. Trace:
[`data/gl3-command-path-2026-10-08.log`](data/gl3-command-path-2026-10-08.log). Times
below are seconds from the start of each run.

**Every command on the air acked in one attempt.** Run 1, image `4fac560`:

| Command | `cmd/ack` | At GateLink |
|---|---|---|
| `nop` | `acked`, result 0 | `NOP ACCEPTED` |
| `open` | `acked`, result 0 | K3 for 499.8 ms, then the ACK |
| `hold_open` | `acked`, result 0 | `HOLD_OPEN ACCEPTED`; the K1 line lost its middle on the console |
| `release_hold` | `acked`, result 0 | K2 for 499.8 ms |
| `close 0` | `acked`, result 0 | K2 for 499.8 ms |
| `close 1` | `acked`, result 0 | K2, then K4 1.03 s later; the ACK after K4's trailing edge |
| `request_status` | `acked`, result 0 | A `STATUS` followed the ACK |
| `close 5` | `acked`, **result 5** | `REJECTED_ARG`, nothing dispatched |

The `lran` line after them read `exec 8, pulsed 5`: five sequences handed to `io_task`, and
the three commands that pulse nothing did not. `close 1` arrived at 175.13 and its ACK left
at about 176.6, inside the bridge's 3 s timeout, as plan §5.2's decision of 2026-10-07
predicted for the defaults.

**A retry inside the execution window goes unanswered** (spec §9.4). With the bridge's
`command_ack_timeout_ms` at 500, `close 1` went out at 211.95 and its retry, same `seq`
10, at 212.76, while K2 was still timing. GateLink logged `retry in flight, not answered
(spec 9.4)`, K4 followed, and the one ACK reached the bridge at 213.78. `cmd/ack` read
`acked`, two attempts. The `lran` line went from `dup 0, exec 9` to `dup 1, exec 10`:
one execution for two transmissions. The timeout went back to 3000 after the check.

**The real ACK always beat the next retry, so `DUPLICATE_CACHED` needed a fault.**
`75726f1` adds `lran ack drop`, which withholds one fresh ACK through lran-node's
`fresh_ack()` hook. In run 2, `open` at `seq` 1 pulsed K3 and its ACK was withheld. The
bridge's retry at 25.83 drew `dedup hit, DUPLICATE_CACHED (ACCEPTED), not executed`, and
`cmd/ack` read `acked`, two attempts, result 7, detail 0. `exec 1, pulsed 1`: no second
pulse. That is root rule 2 on GateLink's own relays.

**Both resyncs behave as spec §10.3 and D70 say.** `4fac560` adds `lran ctx new`, a new
`ctx_id` that GateLink does not announce. A reboot cannot stand in for it, because the
bridge hears the `BOOT` frames and adopts the new context from them.

- After `ctx new`, `request_status` drew `REJECTED_CTX (frame ctx 0xee1103fa, own
  0x66f5e7c8)`. The bridge adopted the context and retried once at `seq` 1, which was
  accepted. `cmd/ack` read `acked`, attempts 1, because a resync restarts the attempt
  count, as the bridge log of 2026-09-16 found.
- After a second `ctx new`, `open` drew `REJECTED_CTX` and the bridge published
  `unconfirmed`, result 3, with no retry. Nothing pulsed. A `nop` that followed went at
  `seq` 1 in the adopted context and acked.

**Not reached on the air: `REJECTED_MAC` and `REJECTED_SEQ`.** The bridge always sends a
valid MAC, and it resets `seq` with every roll and resync, so neither occurs without a
fault. Both stay host-tested in `lib/lran-node` and `lib/lran-protocol`. Every frame here
passed GateLink's MAC check, and `refused 2` counts the two `REJECTED_CTX` replies alone.

**The console loses the middle of a line under load.** `GL q 1: OPEN ACCEPTED` and
`GL (expander writes)` each lost about 25 bytes. The log queue dropped nothing (`log 0`),
so the loss is past the queue, in the USB CDC path the 2026-10-02 and 2026-10-08 entries
already found. It cost one pulse width, K1's, which this run has as dispatched but not
measured.

## 2026-10-08 — GL3's task watchdog fires on a stalled `app_task` at the configured 10 s

**`app_task` is the only subscriber, and the watchdog caught it.** `8fae69e` arms the task
watchdog in `start_tasks()` from `watchdog_timeout_s`'s default, and `app_task` feeds it
once a 100 ms pass (Impl Plan §5.2). On the StamPLC at `8fae69e`, `wdt` read
`timeout 10 s`. `wdt stall` parks `app_task` unfed at 9.56 s; the watchdog fired at 19.54 s
and named `app (CPU 0/1)` alone. The next boot's banner read `Reset: task_watchdog`.
`esp_reset_reason()` reports the watchdog rather than the panic it aborts through, so spec
§8.14's reset cause reads `Watchdog`.

**Nothing else tripped it.** After that reboot the board ran 110 s idle, with `vedirect_task`
reading the MPPT and `lora_task` listening, and no `task_wdt` line appeared. The GL1 bus
test was not run against it, and BLE does not run until GL5, which is where the BLE window
first sits beside the watchdog.

**Not built here: applying a `SET`.** GateLink has no `Store` yet, so the boot value is the
row's default. `apply_watchdog_timeout()` is the call the CONFIG slice makes; ESP-IDF 4.4's
`esp_task_wdt_init()` reconfigures a running watchdog, as the bridge found on 2026-09-26.

## 2026-10-08 — GL3's CONFIG on the air, with the card and without it

**V-10 passes on the bench, both legs** (Impl Plan §8.3, PRD R-4.2c). The StamPLC ran
`ff2c092`, the bench bridge ran the same commit, and every request went through
`lran/gatelink/config/set`.

| Step | GateLink | `config/ack` |
|---|---|---|
| `SET relay_pulse_ms 700`, card in | 1 write, 0 failed | `persisted`, `ok` 700 |
| Restart, `GET_ALL` | `1 restored` | `persisted`; `config/state` shows 700, `source` `override` |
| Card pulled, `SET relay_pulse_ms 800` | write failed, `DIRTY` | `applied_not_persisted`, `ok` 800 |
| Restart with no card | `NOT MOUNTED - defaults` | — |
| `SET relay_pulse_ms 800, watchdog_timeout_s 20` | `wdt: timeout 20 s` at once | `applied_not_persisted`, both `ok` |
| Card back, `GET_ALL` | `card back, config.json rewritten` | `applied_not_persisted`, as found |
| `GET_ALL` again | `overrides persisted` | `persisted` |
| Restart | `2 restored`, `wdt: timeout 20 s` from boot | — |
| `RESTORE_DEFAULTS` | `wdt: timeout 10 s`, file emptied | two `CONFIG_ACK` frames on the bridge; harness stopped before the MQTT line |

`node/state` followed: `config_persisted` and `sd_ok` were both `false` with the card out
and both `true` once it was back (spec §7.2.8 bits 0 and 1). Before this slice, bit 0 was
always set and bit 1 always clear.

**The first `SET` deadlocked `lora_task`.** `radio_service()` held the SpiLock while it
called `on_frame()`, so the engine ran inside the lock, and the card write took the same
non-recursive mutex. `lora`'s pass count stopped at 1275 and `ui_task` stalled behind the
LCD's take. The bridge reported every later `CONFIG` as outcome unknown. `91579e3` hands the
frame over after the lock is released.

**A remount with no card holds the SPI bus for about 1 s, and a failed write for about
1.8 s.** The SD library retries `GO_IDLE_STATE` and prints each retry through the core's
`log_w`, from `lora_task`. The first build retried every 30 s on a timer, which would stall
the radio at a gate with no card and was a fixed timing constant besides. `ff2c092` retries
once after a `CONFIG` or a readback finds the card unusable, so only configuration traffic
pays. The bridge's 8 s `config_ack_timeout_ms` covers the 2.5 s the slowest `SET` took.

**The bench bridge at `28dc204` answered `watchdog_timeout_s` `UNKNOWN_PARAM`.** It predates
the row, and its `CONFIG` came back outcome unknown as well. That second result was the
deadlock, not the bridge. Reflashed at `ff2c092` with the operator's agreement, it carries
the row.

**Still not reached: the PHY group.** It answers `READ_ONLY` because spec §12.4.2 is not
built on GateLink, and `config.json` never holds a PHY row.

## 2026-10-10 — The MPPT's LiFePO4 settings read back as configured

**Bridge, simnode Heltec and the StamPLC on the bench at `ff2c092`, with the sandbox
broker; the MPPT on its bench supply with no PV, the WattCycle pack beside it.** Trace:
[`data/mppt-readback-2026-10-10.log`](data/mppt-readback-2026-10-10.log).

**The bridge's boot readback matches the operator's VictronConnect page on every setting
the app shows.** The harness opening the bridge's port reset it, and its boot pass sent
ten HEX Gets through GateLink. Each answered on its first attempt, about 2 s apart.
Decoding the raw answers on GateLink's console by hand gives BF-30's published values.
[`mppt-config.md`](mppt-config.md) has the table and starts R-6.1b's record.

**Nothing changed since 2026-10-08.** Every value equals that day's readback after the
15 A restore. So the operator's LiFePO4 setup predates GL4's write test, and the readback
on 2026-10-08 was already reading it.

**`0xEDF4` and `0xEDF2` read 0 again**, so their scales are still not exercised. The
LiFePO4 settings disable both, and no readback of this configuration will exercise them.

**`0xEDF1` reads `0xFF` while VictronConnect names the preset *Smart Lithium (LiFePo4)*.**
The preset name is not in the register.

**The pack's absorption specification contradicts itself**, as `mppt-config.md` records:
"14.2V – 14.6V (14.6V ± 0.2V)". 14.20 V meets the first range and not the second. Which
one the vendor means is the operator's to settle.

## 2026-10-10 — Absorption stays at 14.20 V

**The operator settled the pack's absorption figure.** WattCycle gives 14.6 V, with
14.2–14.6 V the acceptable range. The MPPT stays at 14.20 V, because the operator expects
the lower absorption voltage to increase cell life. [`mppt-config.md`](mppt-config.md)
records the choice.

## 2026-10-10 — GL5: `bms_task` reads the pack, and the window is measured

**The StamPLC and the MPPT both on the WattCycle pack, the PV panel indoors, the sandbox
bridge running.** Trace of the final image:
[`data/bms-windows-2026-10-10.log`](data/bms-windows-2026-10-10.log). `bms now [abort_ms]`
on the console runs a window at once and, with `abort_ms`, asks it to end that far in, which
stands in for a reply queued at that moment.

**The decode agrees with `wattcycle-reader`'s for this pack.** Four cells at 3330–3333 mV,
four temperatures at 18.7–22.1 °C, 13320 mV, 0 mA with the raw discharge flag, SOC 96 %,
95.9 of 100.0 Ah, 3 cycles, SOH 100.0 %, link RSSI −66 to −67 dBm. A reading at rest settles
nothing about the current's sign, so M7 is still open.

**The first image read the pack, then panicked in the FreeRTOS timer task**, PC 0 in
`prvProcessReceivedCommands`: a timer whose callback was gone. With NimBLE's debug log on,
the window read cleanly and showed why it usually did not. Straight after the FFF1
subscription the pack asks to update the connection parameters, and our disconnect lands
while that update is pending. The debug log's own delay was enough to avoid it. Two changes
cleared it, each tested apart:

- **The client refuses the update and opens at the parameters the pack asks for**: 15 ms,
  latency 0, 4 s supervision. Panics fell from every window to 1 in 22 without aborts.
- **NimBLE's callouts run on `esp_timer`** (`CONFIG_BT_NIMBLE_USE_ESP_TIMER=1`). With
  FreeRTOS timers, a series of 26 windows with bench aborts panicked 3 times, early in a
  window and once on the first window after a reset. On `esp_timer` the same series and a
  second one of 26 ran clean. Why a FreeRTOS timer outlives its callout here is not
  established. The 24–72 h soak is the check that would show the cure is not complete.

**Discovering the FFF0 characteristics in one pass cut the window by more than half.**
Asked for one by one, NimBLE discovered each separately: 1.4 s of a 1.9 s connect. After:

| | Before | After, 15 reads |
|---|---|---|
| Window, interlock to release | 2652–3653 ms | 992–2237 ms, median 1217 |
| Connect, with discovery | 1905–2495 ms | 625–1145 ms |
| Scan to the pack's name | 41–461 ms | 11–701 ms |
| Abort latency, worst | 1352 ms | 617 ms |

**The abort latency is the connect.** Every other step checks for its end every 10 ms;
NimBLE's connect is one call that cannot be interrupted. An abort asked during it waits it
out, up to about 1.1 s. A `COMMAND_ACK` queued at the worst moment therefore leaves about
1.9 s of the bridge's 3 s ACK timeout for media access and airtime.

**`bms_window_max_ms` is in the table at `0x1042`: default 5000 ms, range 2000–30000.**
The default is more than twice the slowest window seen. The floor sits above every window
measured, and a cap set too low costs only BMS reads: a reply the bridge waits on still
ends a window early. 30 s is NimBLE's own connect timeout. No overrun was counted in any
series.

**The interlock ran against real traffic.** In two earlier series a reply the bridge was
waiting on was queued during a window: lora_task counted `asked 1`, the window ended as
`aborted`, and the reply went out after it. A frame that is not awaited waited instead
(`tx waits`).

**The heap returns to the same figure after every window**, 231376 B on the final image,
and the lowest free heap since boot stays near 182 KB. The controller cycle leaks nothing
measurable over 26 windows.

**NimBLE's C++ log is off.** At `CORE_DEBUG_LEVEL` 2 it printed `E NimBLEClient:` lines
from its host task straight to the USB port, around `log_task`. `bms_task`'s own line names
the step a failed window ended in.

## 2026-10-10 — M7: the pack current's sign is bit 15, and its unit is 0.1 A

**`lib/bms-ble` read the pack current at a tenth of its size, and on charge with the wrong
sign.** The bench had the WattCycle pack on the MPPT's battery terminals, the StamPLC on
the same terminals, and a load on the MPPT's load output. Under load, `bms data` read
−380 mA while the WattCycle app read −4.00 A and the MPPT 4.05 A out of the battery.
`aiobmsble` 0.27's TDT decoder scales the field in 0.1 A and takes the sign from bit 15.
`bms data` now prints the raw field (`853ba8f`), and the raw values settled it:

| Pack state | Raw | Old decode | New decode | WattCycle app |
|---|---|---|---|---|
| At rest (`bms-protocol` §9) | `0x4000` | 0 mA | 0 mA | 0.0 A |
| Load on the MPPT's load output | `0xC028` | −400 mA | −4000 mA | −4.00 A |
| LiFePO4 charger on the pack, load off | `0x4012` | −180 mA | +1800 mA | +1.8 A |

Bit 14 was set in all three states, so it is not the direction; what it means is still
open (`bms-protocol` §10). The decode now takes the sign from bit 15 and the magnitude in
0.1 A, and the host test holds both raw values. M7 is closed in the register. W6 stays
open in the spec, which still calls `pack_ma`'s convention pending.

**The load was 4 A.** The operator turned it down from 11 A to keep the load board cool,
and the MPPT reported `load 4000 mA` throughout.

**The MPPT read the battery 0.9–1.5 V below the pack.** At 4 A the MPPT reported 11.74
and later 12.31 V while the BMS read 13.21–13.24 V. The operator measured 12.3 V at the
MPPT's terminals and 13.18 V at the pack's, and found one section of the harness warm.
That is about 0.22 Ω, or 3.5 W at 4 A. At the controller's 15 A it would drop about 3.3 V
and dissipate about 50 W, and the MPPT would regulate on a voltage well below the pack's.

**The first window after a boot ended `aborted` at about 45 ms, twice,** with `abort
asked` and nothing yet connected. The window after it read normally. It is consistent
with the boot's LoRa traffic asking for the radio, but that was not checked.
