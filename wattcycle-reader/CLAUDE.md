# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**This sub-project sits inside the LRAN repository but outside its build.** The root
`/CLAUDE.md` governs the repository; this file governs `wattcycle-reader/`. Where they
overlap, the root wins.

LRAN (LoRa Remote Automation Network): bidirectional communication between Home Assistant
and remote automation nodes over a point-to-multipoint LoRa link. Nodes are **LoRaBridge**
(`0x00`, WiFi↔LoRa gateway), **GateLink** (`0x01`, driveway gate controller + battery
monitor) and **WellLink** (`0x02`, placeholder). Start at
[`docs/README.md`](../docs/README.md); the parent document is
[`docs/LRAN-System-PRD.md`](../docs/LRAN-System-PRD.md), and
`docs/shared/LRAN-Protocol-Specification.md` is authoritative for anything on the wire.

This is a standalone proof of concept for the BLE BMS read that GateLink needs —
**`docs/gatelink/LRAN-GateLink_Node-PRD` §3.4**, requirements `R-3.4a`–`R-3.4d`. It is
recorded as complete at System PRD §7.1. (This file used to cite **§5.7 of
`lran-prd-v0_5`**, which is superseded and now lives under `docs/archive/`, retained
deliberately so old references still land somewhere. Do not confuse it with the **§5.7**
in code comments and the README here — those point at
`docs/wattcycle-reader-poc_3.md`, this workspace's own design doc.)

`lib/bms_ble/` is written to drop into GateLink's firmware unchanged once GateLink exists
— treat it as a library being developed in place, not throwaway PoC code. **`src/` is
not**, and one difference is already known: this PoC holds a persistent connection and
polls on an interval, while `R-3.4a`/`R-3.4b` require GateLink to connect, read,
disconnect and de-initialize the BLE controller between polls. See Architecture below.

Two other sub-projects have code now — `lib/lran-protocol/` and `firmware/range-test/`.
Neither builds against anything here.

The TDT protocol write-up still needs lifting out of this workspace into
`docs/gatelink/bms-protocol.md` (System PRD §5 and §7.1). That move is a prerequisite for
GateLink's BMS work.

## Writing

This sub-project is outside the LRAN build, but **the repo's prose standard still applies
to it**: root `## Writing` — use the `nbj-write-clearly` skill for the README, the design
docs under `wattcycle-reader/docs/`, commit messages and code comments.

The target-specific trap is the **known-unverified** material. README §"Known-unverified"
records the current sign convention (§5.8) as the one open protocol question, and the
design docs carry similar qualifications. **A hedge here is a measurement that was never
taken**, not cautious phrasing to be tightened up — it must survive every revision, and it
must survive the move when the TDT protocol write-up is lifted out into
`docs/gatelink/bms-protocol.md`.

Section numbers (§5.8 and the rest) are cited from code comments. Renumbering a design doc
breaks those references silently.

## wattcycle-reader — build, test, flash

All commands run from `wattcycle-reader/`. `pio` is PlatformIO; if not on
PATH it's at `~/.platformio/penv/bin/pio`.

```bash
pio test -e native                        # host-only: 21 protocol tests, no hardware
pio run                                    # build default env (Heltec V3)
pio run -e m5stack_stamplc                 # build for the StamPLC instead
pio run -t upload                          # flash default env
pio run -e m5stack_stamplc -t upload       # flash StamPLC
pio device monitor                         # serial monitor, 115200 baud
```

Three PlatformIO envs in `platformio.ini`:
- `heltec_wifi_lora_32_V3` (default) — the board this PoC was originally built on.
- `m5stack_stamplc` — the board actually chosen for GateLink. Same ESP32-S3
  chip family, so `lib/bms_ble/` and `src/main.cpp`'s BLE logic need no
  changes between boards; only the display implementation, board-specific
  pins/flags, and a couple of build-time library swaps differ (see below).
- `native` — compiles `lib/bms_ble/`'s protocol layer and
  `test/test_tdt_protocol/` on the host, no Arduino/NimBLE/hardware involved.
  This is the fast test loop; run it before every hardware flash.

Each hardware env's `build_src_filter` excludes the other board's display
`.cpp` (`BmsDisplay.cpp` needs the SSD1306 lib and only builds for the Heltec
env; `TftDisplay.cpp` needs the M5StamPLC lib and only builds for the StamPLC
env) — see Architecture below for how `src/main.cpp` picks between them.

**Hardware-specific gotchas** (see `wattcycle-reader/README.md` for full
detail — it has a running list, e.g. macOS's "Allow accessory to connect?"
prompt, the `intelhex` module that PlatformIO's bundled `esptool.py` needs
and that self-upgrades silently drop, and always using `/dev/cu.*` not
`/dev/tty.*`):
- StamPLC has no external USB-UART bridge — its native USB *is* the serial
  port (`/dev/cu.usbmodemXXXX`, not `/dev/cu.usbserial-XXXX`). Without
  `-DARDUINO_USB_CDC_ON_BOOT=1` (already set in its env), Arduino's `Serial`
  silently binds to unconnected UART0 pins instead of the USB CDC everyone is
  listening on — boot ROM lines and NimBLE's internal logs still show up
  (separate console path), so it looks like it's working while every
  `Serial.print()` in the app is lost.
- StamPLC's screen is landscape 240x135 (M5GFX autodetects `rotation=1`), not
  the portrait 135x240 its "1.14-inch (135x240)" spec suggests out of
  context — lay out `TftDisplay` rows as fractions of `Display.height()`, not
  fixed pixel offsets, or it'll clip.
- Check the *installed* `M5StamPLC.h` (`.pio/libdeps/.../M5StamPLC/src/`),
  not GitHub `main`, before relying on its API — the published PlatformIO
  package (`M5StamPLC.Display` is a member reference) has drifted from `main`
  (`Display()` is a method there) at least once already.

## Architecture

**Layering rule (the one that matters most):** `lib/bms_ble/TdtProtocol.h/.cpp`
and `BmsData.h` must never include `Arduino.h` or NimBLE headers — they compile
standalone on the host, which is what lets `test/test_tdt_protocol` run 21
tests against captured protocol frames with no board attached. The `native`
PlatformIO env enforces this: if a dependency creeps in, that env stops
compiling. `NimBleTransport.h/.cpp` is the *only file in `lib/bms_ble/`*
allowed to include NimBLE headers — it's the concrete implementation of the
abstract `BmsTransport` interface, and is guarded `#ifdef ARDUINO` so the
native env's library scan (which picks up the whole `lib/bms_ble/` folder)
doesn't choke on it.

That rule stops at the library boundary, though: `src/main.cpp` also
includes NimBLE headers directly, for scanning and connection-state
management. This is intentional and documented (`BmsTransport.h`'s scope
note) — `BmsTransport` only covers post-connection I/O (write/read/
subscribe/rssi); `main.cpp` is this PoC's wiring, meant to be replaced by
GateLink's own client, not dropped into GateLink verbatim the way
`lib/bms_ble/` is.

```
lib/bms_ble/
  BmsData.h              decoded record — integers in fixed units, no floats
  TdtProtocol.h/.cpp     CRC, frame build, reassembly, decode — HOST-COMPILABLE
  BmsTransport.h         abstract BLE seam (write/read/subscribe/rssi)
  NimBleTransport.h/.cpp the only file allowed to touch NimBLE
src/
  main.cpp               state machine (Scanning <-> Polling) + serial wiring
  DisplayBase.h/.cpp     state/setters shared by both display implementations
  BmsDisplay.h/.cpp      Heltec V3: SSD1306 OLED presentation
  TftDisplay.h/.cpp      StamPLC: ST7789 TFT presentation, via m5stack/M5StamPLC
```

Decode produces a plain struct (`BmsData`); presentation layers (serial
printer, `BmsDisplay`/`TftDisplay`) consume it and know nothing about BLE or
the wire protocol. This split is deliberate so GateLink can reuse
`lib/bms_ble/` regardless of what UI it ends up with. `BmsDisplay` and
`TftDisplay` both derive from `DisplayBase`, which owns the state/staleness
logic they'd otherwise duplicate; each subclass owns only what's genuinely
hardware-specific (`begin()`, `render()`, `showMessage()`, the link-indicator
shape). `src/main.cpp` picks between them via a type alias behind
`#if defined(BOARD_STAMPLC)` — no other call sites change between boards.

`BmsNotifyHandler` (in `main.cpp`) feeds BLE notifications into the
reassembler from `onNotify()`, which NimBLE calls on its own host task — a
different FreeRTOS task from the one running `loop()`. Every touch point of
its shared state (the reassembler, the last decoded frame, the frame
counter) is behind a `portMUX_TYPE` critical section for exactly that reason;
don't add a new field there without extending the lock to cover it.

`src/main.cpp`'s `loop()` is a small state machine, not a linear script:
`Scanning` (active BLE scan, same as a plain central-scanner sketch) hands off
to `Polling` (holds one persistent connection, sends a request on a fixed
interval, falls back to `Scanning` the moment the link drops). This is the
shape to extend for new commands or a different poll cadence — don't bolt a
one-shot connect/disconnect cycle on next to it.

**That rule is about this PoC, and GateLink does not inherit it.** `R-3.4a`/`R-3.4b`
require GateLink to connect, read, disconnect and de-initialize the BLE controller between
polls, on `bms_poll_s` (default 300) — the opposite of a held connection, for power
reasons this mains-adjacent PoC never had to face. The persistent link stays here because
it makes protocol work fast; GateLink's client is written against the PRD, not copied from
`src/main.cpp`. `lib/bms_ble/` is unaffected either way: it is transport-agnostic
post-connection I/O.

The protocol itself (TDT smart BMS, wearing a JBD-style
`FFF0`/`FFF1`/`FFF2` GATT service layout but speaking a different protocol
underneath) is documented in detail in
`wattcycle-reader/docs/wattcycle-reader-poc_3.md`, which `wattcycle-reader/README.md`
treats as the source of truth (§-numbered references throughout the code
comments point back into it). Read the README first for current status and
hardware notes; consult the design doc for wire-level protocol facts (frame
format, CRC, handshake sequence, known-unverified items like current sign
convention).
