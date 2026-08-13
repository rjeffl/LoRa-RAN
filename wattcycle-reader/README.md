# WattCycle BLE BMS Reader

Reads a WattCycle 12V 100 Ah Mini LiFePO4 battery's TDT smart BMS over BLE from
a Heltec WiFi LoRa 32 V3, and prints decoded values over USB serial.

Sub-project of the LoRa Remote Automation Network (LRAN). The `lib/bms_ble/`
directory is designed to drop into the GateLink node firmware unchanged.

**Full design, protocol reference and milestones:**
[docs/wattcycle-reader-poc_3.md](docs/wattcycle-reader-poc_3.md). That document
is the source of truth; section references below (§5.4 etc.) point into it.

## Status: M1

| Milestone | State |
|---|---|
| M0 — toolchain up | builds clean |
| M0b — display alive | not started (§9) |
| **M1 — BLE scan** | **confirmed on hardware** — target found; see RSSI below |
| M2 — connect + discover | not started |
| M3 — handshake + raw | not started |
| M4 — reassembly + CRC | **decoder done and host-tested**, not yet run on hardware |
| M5 — decode 0x8C | **decoder done and host-tested**, not yet run on hardware |
| M6 — alarms (0x8D) | deliberately not decoded — see below |

The protocol layer got built ahead of the radio layer on purpose: it is testable
on the laptop against captured frames, so there is no reason to debug it over a
serial cable. What remains for M2–M5 is BLE plumbing behind `BmsTransport`, not
decode work.

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
