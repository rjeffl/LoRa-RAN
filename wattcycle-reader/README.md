# WattCycle BLE BMS Reader

Reads a WattCycle 12V 100 Ah Mini LiFePO4 battery's TDT smart BMS over BLE from
a Heltec WiFi LoRa 32 V3, and prints decoded values over USB serial.

Sub-project of the LoRa Remote Automation Network (LRAN). The `lib/bms_ble/`
directory is designed to drop into the GateLink node firmware unchanged.

**Full design, protocol reference and milestones:**
[docs/wattcycle-reader-poc_3.md](docs/wattcycle-reader-poc_3.md). That document
is the source of truth; section references below (§5.4 etc.) point into it.

## Status: M7

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
| **M7 — poll loop + resilience** | **confirmed on hardware** — polls every 5 s, survived a real out-of-range/back-in-range cycle, zero leaks |

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
platformio.ini              two envs: heltec_wifi_lora_32_V3, native
lib/bms_ble/
  BmsData.h                 decoded record — integers in fixed units, no floats
  TdtProtocol.h/.cpp        CRC, frame build, reassembly, decode. HOST-COMPILABLE
  BmsTransport.h            abstract BLE seam. Interface only; no implementer yet
src/main.cpp                M1: scan and print
test/test_tdt_protocol/     21 host tests against the §5.7 captured frames
tools/                      Python probes and the aiobmsble instrumentation
```

The one architectural rule: **`TdtProtocol` must never include `Arduino.h` or
NimBLE** (§7 rule 1). The `native` env enforces it — if a dependency creeps in,
`pio test -e native` stops compiling.

## Build, test, flash

Tests first — they need no hardware and take about a second:

```bash
cd wattcycle-reader
pio test -e native          # 21 tests against the captured frames
pio run                     # build firmware for the Heltec V3
pio run -t upload           # flash
pio device monitor          # 115200 baud
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
