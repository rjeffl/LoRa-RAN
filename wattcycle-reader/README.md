# WattCycle BLE BMS Reader

Reads a WattCycle 12V 100 Ah Mini LiFePO4 battery's TDT smart BMS over BLE, and
prints decoded values over USB serial. Builds for two boards: the Heltec WiFi
LoRa 32 V3 this PoC was developed on, and the M5Stack StamPLC — the board
actually chosen for GateLink after the PoC was already under way (see
[Second board: M5Stack StamPLC](#second-board-m5stack-stamplc)).

Sub-project of the LoRa Remote Automation Network (LRAN). The `lib/bms_ble/`
directory is designed to drop into the GateLink node firmware unchanged.

**Full design, protocol reference and milestones:**
[docs/wattcycle-reader-poc_3.md](docs/wattcycle-reader-poc_3.md). That document
is the source of truth; section references below (§5.4 etc.) point into it.

## Status: M8

| Milestone | State |
|---|---|
| M0 — toolchain up | builds clean |
| M0b — display alive | **confirmed on hardware** — panel ACKs at 0x3C, layout renders |
| M1 — BLE scan | **confirmed on hardware** — target found; see RSSI below |
| M2 — connect + discover | **confirmed on hardware** — connects, FFF0 enumerated, FFF1/FFF2/FFFA handles confirmed |
| M3 — handshake + raw | **confirmed on hardware** — HiLink ACKed, subscribed, 0x8C answered in one frame at MTU 512 |
| M4 — reassembly + CRC | **confirmed on hardware** — notify bytes reassembled, CRC validated |
| M5 — decode 0x8C | **confirmed on hardware** — full field-by-field decode, values sane |
| M6 — alarms (0x8D) | deliberately not decoded — see below |
| M6b — display live data | **confirmed on hardware** — real SOC/V/A/temp on the OLED, link dot fills, staleness verified |
| M7 — poll loop + resilience | **confirmed on hardware** — polls every 5 s, survived a real out-of-range/back-in-range cycle, zero leaks |
| M7a — StamPLC display port | **confirmed on hardware** — same live data on the StamPLC's TFT via a second `TftDisplay` implementation |
| **M8 — library extraction** | protocol under test (21 host tests), transport abstracted, code-reviewed and fixed, both boards re-confirmed — see [Ready to merge (M8)](#ready-to-merge-m8) |

M0-M7 above were all run on the Heltec V3. M7 (scan, connect, handshake,
subscribe, poll, decode) was then **also confirmed on the StamPLC** — see
[Second board: M5Stack StamPLC](#second-board-m5stack-stamplc) — with no
changes to any file under `lib/bms_ble/` and no code changes to `src/main.cpp`'s
BLE logic either; only `platformio.ini` gained a second env. M7a then closed
the one remaining gap: `TftDisplay` (`src/TftDisplay.h/.cpp`) ports M6b's
display behavior onto the StamPLC's own screen — see
[StamPLC display port (M7a)](#stamplc-display-port-m7a).

The protocol layer got built ahead of the radio layer on purpose: it is testable
on the laptop against captured frames, so there is no reason to debug it over a
serial cable. Wiring the already-tested reassembler and decoder onto the notify
stream `NimBleTransport` delivers turned out to be exactly that — no decode bugs
found on hardware; the host-test suite's captured-frame coverage held.

**0x8D is intentionally not decoded.** It is only partially understood (§5.7):
`0629` sits where MOSFET and status bits appear to live, but mapping the bitmaps
needs a capture taken during a real protection event. A speculative bit map would
produce confident-looking wrong alarms, so the decoder is deferred to M6. Its
framing and CRC are already covered by tests.

## Layout

```
platformio.ini                three envs: heltec_wifi_lora_32_V3, m5stack_stamplc, native
lib/bms_ble/
  BmsData.h                  decoded record — integers in fixed units, no floats
  TdtProtocol.h/.cpp         CRC, frame build, reassembly, decode. HOST-COMPILABLE
  BmsTransport.h             abstract BLE seam (write/read/subscribe/rssi)
  NimBleTransport.h/.cpp     the only file allowed to touch NimBLE (#ifdef ARDUINO)
src/
  main.cpp                   Scanning<->Polling state machine, serial wiring
  LinkState.h                connection-state enum shared by both displays
  DisplayBase.h/.cpp         shared display state/setters (M8)
  BmsDisplay.h/.cpp          Heltec V3: SSD1306 OLED presentation
  TftDisplay.h/.cpp          StamPLC: ST7789 TFT presentation (M7a)
test/test_tdt_protocol/      21 host tests against the §5.7 captured frames
tools/                       Python probes and the aiobmsble instrumentation
```

The one architectural rule: **`TdtProtocol` must never include `Arduino.h` or
NimBLE** (§7 rule 1). The `native` env enforces it — if a dependency creeps in,
`pio test -e native` stops compiling.

## Build, test, flash

Tests first — they need no hardware and take about a second:

```bash
cd wattcycle-reader
pio test -e native                        # 21 tests against the captured frames
pio run                                   # build for the default env (Heltec V3)
pio run -t upload                         # flash the default env
pio run -e m5stack_stamplc -t upload      # flash the StamPLC instead
pio device monitor                        # 115200 baud
```

If `pio` isn't on your PATH, it's at `~/.platformio/penv/bin/pio`.

Notes for this build host (§13.1):

- `platform = espressif32@^6.9.0` is pinned deliberately. Don't float it.
- Use `/dev/cu.*`, never `/dev/tty.*` — the `tty.` variant blocks on carrier
  detect and hangs uploads with no useful error.
- macOS may prompt *"Allow accessory to connect?"* on first plug-in. Dismiss it
  and the port never enumerates, which looks exactly like a driver problem.
- If auto-reset into the bootloader fails: hold **PRG/BOOT**, tap **RST**,
  release **PRG**, upload, then press **RST** to run.
- **`ModuleNotFoundError: No module named 'intelhex'`** — the build dies at
  `bootloader.bin` while the compile itself looks fine. PlatformIO's bundled
  `esptool.py` needs `intelhex`, which isn't in its venv:

  ```bash
  ~/.platformio/penv/bin/python -m pip install intelhex
  ```

  **This comes back every time PlatformIO Core self-upgrades** — the upgrade
  rebuilds the venv and drops the package. Seen twice already, once on the
  6.1.16 → 6.1.19 upgrade. If a build that worked yesterday fails at
  `bootloader.bin` today, this is why; the fix is the same line again.

- `pio` is aliased in `~/.zshrc` to `~/.platformio/penv/bin/pio`. It is not on
  PATH, because that directory also holds a `python`/`pip` that would shadow
  the Homebrew ones.

## Expected M1 output

```
WattCycle BLE BMS reader — M1 (scan only)
looking for: XDZN_001_49A1 / c0:d6:3c:58:49:a1

=== sweep 1 — 7 devices ===
  RSSI  ADDRESS            NAME
  ----  -----------------  --------------------------
   -62  4c:1d:96:aa:bb:cc  (no name)
>>  -81  c0:d6:3c:58:49:a1  XDZN_001_49A1
     mfr data: 3c5849a1ff5844

  TARGET FOUND: XDZN_001_49A1 @ -81 dBm
```

### Measured RSSI

| Position | RSSI |
|---|---|
| Desk range, board and battery on a bench | −77 to −88 dBm |
| **Heltec at the approximate mounting position** | **−60 to −65 dBm** |

The mounting-position figure was measured at M1 and matches the §15 prediction
of −55 to −65 dBm. This closes the "weak BLE transmitter" risk for the gate
install: link margin at the real distance is comfortable.

**−77 to −88 dBm at desk range is normal**, not a fault — the battery's antenna
appears shielded by the BMS heat sink, confirmed independently on two radios
(§5.8). The ~20 dB improvement at the mounting position says the loss is
geometry, not a failing radio, so preserve the tested orientation when the
enclosure is built.

Keep publishing RSSI as a diagnostic regardless (§5.8): it is the early-warning
signal for a mount degrading from moisture, corrosion, or a shifted bracket.

## Connect, discover, handshake, decode (M2-M5, M6b)

`NimBleTransport` (`lib/bms_ble/NimBleTransport.h/.cpp`) is the only file that
includes NimBLE headers (§7 rule 2) — it implements `BmsTransport` against
service `0xFFF0`, and is guarded `#ifdef ARDUINO` so `pio test -e native`
still builds without it. `BmsNotifyHandler` (`src/main.cpp`) feeds every
notification through `TdtProtocol::FrameReassembler` (M4) and decodes complete
`0x8C` frames with `decodeCellsAndPack()` (M5) — the same reassembler and
decoder already covered by the 21 host tests in `test/test_tdt_protocol`.

M2-M5 run as one block, once per boot, on the first sweep that finds the
target: stop scanning, connect, discover, handshake, subscribe, request,
decode whatever comes back, disconnect, then resume scanning. It is a
capability check, not the persistent connection or poll loop — those are M7.

```
--- M2: connect + discover ---
  MTU negotiated: 512
  FFF1 (rx/notify)   handle 0x0010  read=1 write=0 writeNR=0 notify=1
  FFF2 (tx)          handle 0x0014  read=1 write=1 writeNR=1 notify=0
  FFFA (handshake)   handle 0x0018  read=1 write=1 writeNR=1 notify=0
--- M3: handshake ---
  HiLink -> FFFA: written, read-back: 0x01 (ACK)
  subscribe FFF1: OK
  0x8C request sent: OK
--- M4/M5: reassembly + decode ---
  decode 0x8C: OK — 4 cells, 4 temps
    cells (mV): 3333 3333 3334 3334  (delta 1 mV)
    temps (0.1C): 225 249 220 219
    pack: 13330 mV   current: 0 mA (discharge flag)   SOC: 99%
    remaining/nominal: 989/1000 (0.1 Ah)   cycles: 2   SOH: 1000 (0.1%)
--- M2-M5 block complete, disconnected ---
```

Confirmed on hardware, all in one pass: handles resolve and properties match
the §4 GATT layout table; a device missing any of the three characteristics is
treated as "not this BMS" even if it shares the JBD-style `FFF0`/`FFF1`/`FFF2`
service topology (§4 note); the `0x8C` response arrived as a single
notification at the full negotiated MTU 512 (§5.5's happy path — fragmentation
still untested on hardware, only against captured frames); and the decoder
produced the same sane values by-eye-checked at M3 (4 cells ~3.33 V, 13.33 V
pack, SOC 99%, SOH 100%, 2 cycles, 0.0 A at rest) with zero decode bugs found —
the host-test suite's captured-frame coverage held on live hardware.

**M6b** rides on the same block: once connected, `BmsDisplay::setLink()` fills
the link indicator for real (rather than waiting for the next scan-driven
render), and once a frame decodes, `BmsDisplay::setData()` pushes it straight
to the OLED. Confirmed on hardware, at the time this ran as a one-shot probe
(before M7 replaced it with a persistent connection below): real SOC/pack
voltage/current/temp appeared, the dot filled while connected and went hollow
again after disconnect, and the numbers dashed back out ~15 s later
(`kStaleAfterMs`) with nothing refreshing them.

## Poll loop + resilience (M7)

`loop()` is a small state machine, `Scanning <-> Polling` (`src/main.cpp`):

- **Scanning** — the M1 active-scan sweeps, unchanged; the display's aiming
  behaviour still works exactly as before. When the target is seen,
  `connectAndHandshake()` runs the M2+M3 sequence. Success moves to Polling;
  failure stays in Scanning and counts against a `g_consecutive_failures`
  total.
- **Polling** — the connection is held open per §5.6 ("a persistent
  connection is viable"), rather than connect/round-trip/disconnect each
  time. Every 5 s it sends `0x8C`; `BmsNotifyHandler` decodes whatever comes
  back the same way as M4/M5, and only a genuinely *new* frame is pushed to
  the display, so `BmsDisplay`'s staleness timer isn't re-stamped by nothing.
  If `NimBleTransport::isConnected()` ever goes false, that's the drop
  detector — it falls back to Scanning, which reconnects once the target is
  seen again.

Confirmed on hardware with a real out-of-range/back-in-range cycle, battery
walked away and back:

```
--- poll: 0x8C sent=OK  rssi=-92 dBm  heap=284348  failures=0 ---
--- link dropped, resuming scan ---
  target XDZN_001_49A1 NOT seen this sweep      (repeated ~21 s while away)
  TARGET FOUND: XDZN_001_49A1 @ -89 dBm
--- connect + discover ---
  HiLink -> FFFA: read-back 0x01 (ACK)
  subscribe FFF1: OK — polling
--- poll: 0x8C sent=OK  rssi=-87 dBm  heap=284348  failures=0 ---
--- poll: 0x8C sent=OK  rssi=-59 dBm  heap=284328  failures=0 ---
```

RSSI degraded as the battery moved away (down to -92 dBm), the link dropped
and was logged, Scanning correctly reported "NOT seen" the whole time it was
gone, reconnect fired automatically the moment it was seen again, and
`consecutive_failures` reset to 0 on that successful reconnect. Free heap held
flat (284348 -> 284328, a one-time ~20-byte NimBLE bookkeeping shift on
reconnect, not a leak trend) across seven pre-drop poll cycles, the drop, and
six more after reconnecting.

## Second board: M5Stack StamPLC

GateLink's actual node hardware was decided to be the [M5Stack
StamPLC](https://docs.m5stack.com/en/core/StamPLC) after this PoC's Heltec V3
was already in hand and M0-M7 already confirmed on it. StamPLC is built
around a Stamp-S3A module — same ESP32-S3 chip family — so none of
`lib/bms_ble/` or `src/main.cpp` needed to change; only `platformio.ini`
gained an `[env:m5stack_stamplc]`.

**Board id:** `esp32-s3-devkitc-1`. There's no StamPLC-specific board JSON in
this platform version; StamPLC's SoC is an ESP32-S3FN8 (8 MB flash, no
PSRAM), and `esp32-s3-devkitc-1` ("8 MB QD, No PSRAM") is an exact match —
also what M5Stack's own Arduino docs point to.

**No external USB-UART bridge — the ESP32-S3's native USB *is* the serial
port.** It enumerates as `/dev/cu.usbmodemXXXX`, not `/dev/cu.usbserial-XXXX`
like the Heltec V3's CP2102/CH9102 bridge. This has a real consequence: the
board default sets `ARDUINO_USB_MODE=1` but not `ARDUINO_USB_CDC_ON_BOOT`, so
without adding that flag, Arduino's `Serial` silently binds to UART0 — pins
that go nowhere on this board — instead of the native USB CDC everyone is
actually listening on. **Confirmed on hardware**: without the flag, the boot
ROM lines and NimBLE's internal `ESP_LOG` output (a separate console path)
still arrive, so it looks like it's working, but every `Serial.print()` /
`printf()` in this codebase is silently lost — the exact kind of "half the
output is there" symptom that wastes an hour before you check the flag. Fixed
with `-DARDUINO_USB_CDC_ON_BOOT=1` in `m5stack_stamplc`'s `build_flags`.

**M7 (scan -> connect -> handshake -> subscribe -> poll -> decode) confirmed
on hardware**, same as the Heltec V3: target found, connected, MTU 512
negotiated, HiLink ACKed, subscribed, then polling every ~5 s with stable
RSSI (-55 to -60 dBm at bench range) and flat heap (285884 bytes) across
repeated cycles. At that point in the session `BmsDisplay::begin()` (the
Heltec's SSD1306 code, still unconditionally compiled in at the time)
correctly reported `OLED at 0x3c: NOT FOUND` and the program continued
normally — expected, since `kPinOledSda` (17), `kPinOledScl` (18) and
`kPinVext` (36) aren't wired to anything on this board. M7a (below) replaced
that with a real display for this board.

StamPLC's onboard I2C bus (SCL G15, SDA G13) carries an LM75B temp sensor
(0x48), INA226 voltage/current sensor (0x40), and an RX8130CE RTC (0x32) —
none of which this firmware touches, but worth knowing before wiring
anything else onto that bus later.

## StamPLC display port (M7a)

StamPLC's screen is a 1.14" SPI ST7789v2 TFT — MOSI G8, SCK G7, CS G12,
RS/DC G6, RST G3 — not an I2C SSD1306: different bus, different driver, and
the backlight sits behind a PI4IOE5V6408 IO expander (P7) rather than a
plain GPIO. Rather than hand-roll the ST7789 init sequence and the IO
expander's register protocol, `TftDisplay` (`src/TftDisplay.h/.cpp`) drives
it through the official `m5stack/M5StamPLC` Arduino library (pulling in
M5Unified + M5GFX transitively) — M5Stack publishes a tested example for
exactly this board and screen, and getting an I2C expander's register map
wrong by hand isn't worth the risk when a working implementation already
exists.

`TftDisplay` implements the same public API as `BmsDisplay`
(`begin`/`setDeviceName`/`setLink`/`setData`/`render`/`showMessage`), so
`src/main.cpp` only swaps a type alias — `ActiveDisplay` — behind
`#if defined(BOARD_STAMPLC)`; no call site changes between boards. Each env's
`build_src_filter` in `platformio.ini` excludes the other board's display
`.cpp` (`TftDisplay.cpp` on the Heltec env, `BmsDisplay.cpp` on the StamPLC
env), so neither board pulls in a display library it doesn't use.

**Three things only showed up on real hardware:**

1. **`M5StamPLC.Display` is a member reference in the published v1.2.0
   package (`LGFX_Device& Display = M5.Display;`), not a method.** The
   library's GitHub `main` branch (fetched while researching this) declares
   it as `inline LGFX_Device& Display()` — a method — which is what
   `pio pkg search` actually installs from PlatformIO's registry disagreed
   with. Writing `M5StamPLC.Display()` against the real installed header
   fails to compile (`no match for call to '(LGFX_Device) ()'`): the code
   evaluates `Display` as a `LGFX_Device&` first, then tries to call *that*
   with `()`. Fix was mechanical — drop the parens — but the lesson is to
   check the package that actually installs (`.pio/libdeps/.../M5StamPLC.h`)
   over `main` on GitHub when the two might have drifted.
2. **The panel is landscape, 240x135 — not the 135x240 portrait the "1.14-inch
   (135x240)" spec implies out of context.** `M5GFX` auto-detects this and
   sets `rotation=1` (confirmed via a one-line diagnostic:
   `M5StamPLC.Display.width()/height()/getRotation()`). The first layout used
   fixed pixel row offsets sized for a 240-tall portrait screen; on the real
   135-tall landscape panel every row past the top third drew off-screen —
   confirmed on hardware as clipped/missing text. Fixed by computing every
   row as a fraction of `Display.height()` instead of a hardcoded pixel
   count, which is also what makes the layout not care which orientation a
   future panel is actually wired in.
3. **Drawing straight to the panel flickered visibly on every refresh.**
   `render()` opened with `fillScreen(TFT_BLACK)` on `M5StamPLC.Display`
   itself, so the whole panel briefly went black before each element redrew
   over SPI — confirmed on hardware as a visible flash every poll cycle.
   Fixed with the same off-screen-sprite pattern the library's own
   `DashboardUI` example uses: `TftDisplay` now owns a full-screen
   `LGFX_Sprite` (`canvas_`), every draw call in `render()`/`showMessage()`
   targets that (invisibly, in RAM), and a single `pushSprite(0, 0)` at the
   end blits the finished frame to the panel in one SPI burst. Costs ~63 KB
   of heap (240x135 RGB565, allocated once in `begin()` — this board has no
   PSRAM to put it in instead) but eliminated the flicker entirely, confirmed
   on hardware.

**Confirmed on hardware, final layout**: device name + link indicator (fills
green when connected) top-left/top-right, RSSI top-right below that, SOC
large and centered, pack voltage (left) and current (right, colour-coded —
green charging, red discharging, white idle) on one row, temperature and FET
state on the bottom row. Same staleness behavior as M6b: values dash out
after `kStaleAfterMs` (15 s) with no connection refreshing them.

`M5StamPLC.begin()` also brings up the board's onboard LM75B/INA226/RX8130
sensors and I2C IO expanders — that's the library's own design, there's no
display-only init path — but `Config_t`'s defaults leave Modbus, CAN and the
SD card disabled, so nothing in `TftDisplay` ever touches the RS485/CAN
transceivers or actuates a PLC relay.

## Display (M0b)

```
XDZN_001_49A1        o     name + link indicator (filled once connected)
                 -62dBm    RSSI — live while scanning, not just when connected
  87%                      SOC, largest font
13.42 V      -4.2 A        pack voltage, current
24.1C        DSG           hottest sensor, FET state
```

The RSSI line is an addition to the §9 layout. It makes the board an aiming
instrument: you can find a mounting position by watching the panel, with no
laptop attached. That is why the scan sweep is 3 s with no pause — a 7 s
refresh is too slow to position a board by.

Values dash out until a frame decodes, and again after 15 s without one, so a
dropped link never looks like a live reading.

Two settings established on hardware, both easy to get wrong:

- `flipScreenVertically()` is **required** on the V3 — without it the panel is
  upside down.
- **No degree symbol.** `0xB0` renders as nothing on this panel despite being
  in the fonts' nominal range, so the label is a plain `C`.

`BmsDisplay` takes a `const BmsData&` and a link state and knows nothing about
BLE or the protocol — same rule as the serial printer. `displayOn()/displayOff()`
exist from the start so GateLink inherits the §9 power behaviour rather than
having it retrofitted.

**The FET field is derived from current sign, not read from the BMS.** Real
MOSFET status is in `0x8D`, which isn't decoded yet. A pack can sit idle with
its discharge FET open, and those are not the same statement — this becomes
honest at M6.

Baseline resource use at M1, for the §15 RAM-contention question: 29.1 KB RAM
(8.9%), 520 KB flash (15.6%).

## Protocol facts most likely to bite

All verified against a live capture; details in §5.

1. **The handshake goes to `FFFA`, not `FFF2`.** Write ASCII `HiLink`, then read
   `FFFA` back and require `0x01` before proceeding. Requests sent to `FFF2`
   before that are ACKed at the ATT layer and silently ignored, and the BMS drops
   the link ~4 s after connecting. That failure is indistinguishable from "wrong
   protocol" and cost ~20 blind probe combinations to find.
2. **Request head is `0x1E`.** This battery never answers `0x7E`.
3. **CRC-16/MODBUS, transmitted big-endian** — the opposite of Modbus
   convention.
4. **Frame on the length byte, never the terminator.** A `0x8C` response contains
   four literal `0x0D` bytes and a literal `0x7E` in its payload. Both are
   covered by tests.
5. **Current is not a signed int16.** Bit `0x4000` is a flag; magnitude is the
   low 14 bits in units of 10 mA. Read naively, a resting pack reports 16384.

## Known-unverified

**The current sign convention** (§5.8) is the one open protocol question. The
pack has only ever been observed at rest, where current reads raw `0x4000` —
the discharge flag with zero magnitude — which cannot disambiguate charge from
discharge. `BmsData::discharging` carries the raw flag; `current_mA` applies the
assumed convention. Capture `0x8C` under charge and again under load before
trusting the polarity.

## Ready to merge (M8)

M8's criteria (§11): protocol code under test (21 host tests, unchanged
throughout M2-M7a — the reassembler/decoder never needed a fix once real
hardware started exercising them), transport abstracted (`BmsTransport` +
`NimBleTransport`), README written. What was still open going into M8 was the
review pass the design doc calls out as part of "ready to merge" (§12):
"review the diff yourself... it's the last time you'll see the whole change
at once." A full-branch code review turned up seven findings; all were
triaged, and the ones inside the actual merge boundary were fixed:

**Fixed, in `lib/bms_ble/` (the library GateLink drops in unchanged):**
- `NimBleTransport::read()` returned `0` on a genuine read failure instead of
  the `-1` `BmsTransport.h` documents — indistinguishable from "read
  succeeded with zero bytes," a case this protocol never actually produces.
  Now returns `-1` for empty reads too.
- `kHandshakeMagicLen` (`6`) was hand-maintained separately from
  `kHandshakeMagic` (`"HiLink"`), with nothing catching the two drifting
  apart if either is ever edited alone. Added a `static_assert` next to the
  definition.

**Fixed, in `src/` (PoC wiring/presentation — not part of the library
boundary, but real bugs in code this session hardware-tested extensively):**
- `BmsNotifyHandler`'s reassembler and decoded-frame state were touched from
  two different FreeRTOS tasks (NimBLE's host task via `onNotify()`, the
  Arduino loop task via `tick()`/`frameCount()`/`lastData()`) with no
  synchronization — a real race, just one that never happened to manifest
  during this session's testing. Added a `portMUX_TYPE` critical section
  around every touch point; `lastData()` now returns a copy rather than a
  reference, since a reference into locked state defeats the lock the moment
  the caller keeps reading through it afterward.
- `TftDisplay::begin()` discarded `createSprite()`'s return value and always
  reported success; a heap-allocation failure for the ~63 KB back-buffer
  (this board has no PSRAM) would have left `canvas_` with no pixel buffer
  while `main.cpp` still logged `display: OK`. Now checks the return value
  and fails `begin()` properly — this board's actual "not found" case, since
  the panel itself can't fail an I2C probe the way the OLED can.
- `BmsDisplay` and `TftDisplay` independently declared the same eight fields
  and byte-for-byte identical `setDeviceName()`/`setLink()`/`setData()`/
  `dataFresh()` bodies — exactly the kind of duplication that drifts silently
  when one gets edited and the other doesn't. Extracted into `DisplayBase`
  (`src/DisplayBase.h/.cpp`); each subclass now owns only what's genuinely
  hardware-specific (`begin()`, `displayOn()`/`Off()`, `render()`,
  `showMessage()`, the link-indicator shape).

**Documented, not restructured (real, but outside what "ready to merge"
needs to touch):**
- `src/main.cpp` talks to NimBLE directly for scanning/connecting, not just
  through `BmsTransport` — `BmsTransport.h`'s scope note now says explicitly
  that this interface covers post-connection I/O only, and `main.cpp` is PoC
  wiring meant to be replaced by GateLink's own client, not part of the
  library boundary. Building a scan-aware `TdtBmsClient` behind the
  transport interface is real future work, not a fix owed by this PoC.
- RSSI `0` doubles as "not connected"/"read failed" rather than a distinct
  sentinel, in `NimBleTransport::rssi()` and `src/main.cpp`'s scan handling.
  Documented on `BmsTransport::rssi()` — a real 0 dBm reading would be
  misread as "no signal," but that's not a practical concern at BLE ranges.

**Re-confirmed on hardware after the fixes**, both boards: full
scan→connect→handshake→poll→decode cycle, no crashes, heap flat across
repeated poll cycles, StamPLC's `createSprite` succeeding and the OLED
rendering unchanged (the refactor moved *where* state lives, not the
drawing code itself).

## Reference implementation

`aiobmsble` (Apache-2.0) is the oracle. At each milestone, run the C++ decode
and the Python decode against the same battery minutes apart and compare field
by field:

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install aiobmsble aiooui
aiobmsble -v
```

The raw capture these tests are built from is in
`tools/results_from_aiobmsble.txt`.

**Not safety-grade.** The upstream author warns explicitly against
safety-relevant use. BMS data here is for monitoring and alerting only — never
let it gate a charge decision. The MPPT stays authoritative.
