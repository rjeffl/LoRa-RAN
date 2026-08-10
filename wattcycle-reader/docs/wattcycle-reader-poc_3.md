# WattCycle BLE BMS Reader — Proof of Concept

**Sub-project of:** LoRa Remote Automation Network (LRAN)
**Status:** v0.3 — protocol fully characterized and independently reproduced; ready to scaffold
**Purpose:** validate BLE reading of the gate battery's BMS and produce a reusable driver for the GateLink node.

---

## 1. Objective

Read State of Charge, pack voltage, current and temperature from a WattCycle 100 Ah mini LiFePO4 battery over BLE, using a Heltec WiFi LoRa 32 V3 as a BLE central, and stream the decoded values over USB serial to a laptop.

Success = a Heltec board, tethered to a laptop over USB, printing a live decoded battery record once every N seconds, with clean reconnect after the battery link drops.

Secondary objective (equally important): use this small, self-contained build as the vehicle for learning the full firmware workflow — repo layout, branch, PlatformIO build, flash, monitor, commit, merge.

As of v0.2 the protocol is captured and a working Python reference exists (§5, §10), so this is now a **port**, not a reverse-engineering exercise. Every milestone has an oracle to check against.

## 2. Why this is worth doing separately

- The GateLink node is a big build. BLE-to-BMS is the one piece with real unknowns (undocumented clone protocol, unclear connection behaviour, unknown RAM cost of a BLE stack alongside LoRa).
- Failing fast here is cheap. If the BMS turns out to be uncooperative, the PRD falls back to MPPT voltage + SmartShunt with no schedule damage.
- The output is a library, not a demo. `lib/bms_ble/` drops into the GateLink firmware unchanged.

## 3. Hardware

| Item | Detail |
|---|---|
| MCU board | Heltec WiFi LoRa 32 V3 (ESP32-S3FN8, 8 MB flash, 512 KB SRAM + PSRAM variant, SX1262) |
| Radio used | BLE only for this PoC (ESP32-S3 is BLE 5.0; **no** Bluetooth Classic — fine, the BMS is BLE) |
| USB | USB-C, onboard USB-serial bridge (CP2102 or CH9102 depending on batch) |
| Target | WattCycle 12V 100 Ah Mini LiFePO4, 4S |
| BMS | TDT smart BMS (confirmed via `aiobmsble`) |
| Advertised name | `XDZN_001_49A1` (underscores) |
| BMS MAC | `C0:D6:3C:58:49:A1` |
| Vendor app | BMS Meta (iOS/Android) |
| Build host | M5 MacBook Pro, macOS 26 — VS Code, PlatformIO, Claude Code, git clone |
| Field host | Older MacBook Pro running Kubuntu — serial monitoring at the gate, optional reflash |

Power and data both come from the laptop's USB port. No external supply, no soldering, no wiring.

## 4. GATT layout (confirmed)

**Service `0xFFF0`** — the data path:

| Char | Properties | Role |
|---|---|---|
| `0xFFF1` | notify, read | responses arrive here |
| `0xFFF2` | write, write-no-response, read | `HiLink` handshake + requests go here |
| `0xFFFA` | write, write-no-response, read | **handshake channel** — `HiLink` is written here, and read back for the `0x01` ack |

**Service `02F00000-…-FE00`** with characteristics `FF00`–`FF05` — carries no battery data. This UUID appears on completely unrelated hardware (Govee thermometers, WLT8016 modules), which identifies it as a **chipset-level vendor service from the Telink BLE SDK**, not something the BMS vendor defined. Ignore it. Do not write to it.

Two corrections to earlier assumptions, recorded because they each cost time:

- The iOS sniff appeared to show a CCCD on `FFF2`. macOS reports `CBATTErrorDomain Code=6` when subscribing there, and the property list contains no `notify`. `FFF2` is write-only-plus-read. The iOS descriptor listing was misleading.
- The `FFF0/FFF1/FFF2` triple is **not** proof of a JBD device. TDT uses the identical layout with a completely different protocol. Service topology is a weak signal; only a successful exchange settles it.

Bench facts about the radio link: the battery advertises as `XDZN_001_49A1` (underscores), MAC `C0:D6:3C:58:49:A1`, at roughly **-80 to -84 dBm even at close range** — the antenna is inside the case. Manufacturer data in the advertisement (`b'<XI\xa1\xffXD'`) contains the MAC bytes but no usable telemetry, so passive advertisement-only monitoring is not an option; a connection is required.

## 5. Protocol — TDT smart BMS (confirmed on hardware)

**The BMS is a TDT, not a JBD.** Despite wearing JBD's `FFF0/FFF1/FFF2` service layout, it speaks an entirely different protocol. This cost a round of blind probing (20 write/protocol combinations, zero responses) and was settled by `aiobmsble`, which identified it as "TDT smart BMS" and returned a full decode.

Everything below is verified against a live capture from this battery.

### 5.1 Connection sequence

Order and target both matter. Verified by instrumenting `aiobmsble`'s transport layer against this battery.

1. Connect. Request the largest MTU the stack allows (see §5.5).
2. **Write ASCII `HiLink` (`48 69 4C 69 6E 6B`) to `FFFA`, with response.**
3. **Read `FFFA` back.** It returns `0x01` — the handshake acknowledgement. Use it as a gate: if it isn't `0x01`, don't proceed.
4. Subscribe to `FFF1` (write CCCD).
5. Send requests to `FFF2`; responses notify on `FFF1`.

> **The handshake goes to `FFFA`, not `FFF2`.** This is the single most important line in this document. Every request written to `FFF2` before a successful `FFFA` handshake is acknowledged at the ATT layer and silently ignored at the application layer, and the BMS drops the link about 4 s after connecting. That failure mode is indistinguishable from "wrong protocol", and it cost roughly twenty blind probe combinations to find. `FFFA` looked like an unused vendor characteristic because it has no counterpart in the JBD layout this device superficially resembles.

### 5.2 Frame format

```
request:   1E 00 01 03 00 <cmd> 00 00 <crc_hi> <crc_lo> 0D      (11 bytes)
response:  7E 00 01 03 00 <cmd> 00 <len> <payload...> <crc_hi> <crc_lo> 0D
```

- **Request head is `0x1E`; response head is `0x7E`.** `aiobmsble` probes `0x7E` first — three writes with response, three without, all ignored — before finding `0x1E`. That costs ~10 s per session. **Go straight to `0x1E`.** Keep `0x7E` as a fallback only if you ever meet a second TDT unit; this battery never answers it.
- **All requests are written with response** (`response=True`). Confirmed on every successful exchange.
- `<len>` is the payload byte count, exclusive of CRC and terminator.
- Terminator is `0x0D`.

**Checksum: CRC-16/MODBUS** (poly 0x8005, init 0xFFFF, reflected in/out, no final XOR), computed over every byte from the head up to but excluding the CRC. Verified against five distinct captured frames. Note it is transmitted **big-endian (hi byte first)**, which is the opposite of Modbus convention — an easy bug to write.

### 5.3 Commands

| Cmd | Returns | Request frame |
|---|---|---|
| `0x8C` | Cells, temps, pack V/I, SOC, capacity, cycles | `1E 00 01 03 00 8C 00 00 B1 44 0D` |
| `0x8D` | Alarm/protection bitmaps, MOSFET status | `1E 00 01 03 00 8D 00 00 71 15 0D` |
| `0x92` | SW version, manufacturer, serial number | `1E 00 01 03 00 92 00 00 B7 24 0D` |

### 5.4 Payload layout, command 0x8C

Variable-length, driven by two inline counts. All multi-byte fields are **big-endian**.

| Offset | Field | Scaling |
|---|---|---|
| 0 | cell count `N` | — |
| 1 | `N` × u16 cell voltage | mV |
| 1+2N | temp sensor count `M` | — |
| 2+2N | `M` × u16 temperature | 0.1 K; °C = (raw − 2731)/10 |
| then | u16 current | **bit 0x4000 = discharge flag**; magnitude = (raw & 0x3FFF) × 10 mA |
| +2 | u16 pack voltage | ×10 mV |
| +4 | u16 remaining capacity | ×0.1 Ah |
| +6 | u16 nominal capacity | ×0.1 Ah |
| +8 | u16 cycle count | — |
| +10 | u16 state of health | ×0.1 % |
| +12 | u16 **SOC** | % |

Worked example from the live capture — payload len 0x20, N=4, M=4:

```
04 0D89 0DA1 0D9C 0D9B  04 0B82 0B9D 0B7F 0B7E
4000 0570 03E7 03E8 0001 03E8 0064
```
→ cells 3.465/3.489/3.484/3.483 V, temps 21.5/24.2/21.2/21.1 °C, current 0.0 A,
pack 13.92 V, 99.9 of 100.0 Ah, 1 cycle, SOH 100.0 %, **SOC 100 %**.

The current encoding is the one field worth extra care: a plain signed-int16 reading gives 16384 instead of zero. Confirm the sign convention against the app under real load before trusting it, since a resting battery can't disambiguate charge from discharge.

### 5.5 MTU, fragmentation, and framing

**Negotiate a large MTU.** On macOS the link came up at **MTU 512**, and a 71-byte response arrived as a *single* notification — no fragmentation whatsoever. On ESP32/NimBLE the default is 23 (20 bytes of payload), so the driver must explicitly request a larger MTU. Do that; it removes a whole class of bug.

**Implement reassembly anyway.** MTU negotiation can fail or be refused, and the driver must not be silently dependent on it. Treat single-notification frames as the happy path, not the contract.

**Frame on the length byte, never on the terminator.** Total frame length is `8 + payload_len + 3` (header + payload + CRC + `0x0D`). Accumulating until `0x0D` is wrong and silently corrupts data: a `0x8C` response contains **four literal `0x0D` bytes before the real terminator**, because cell voltages near 3.4 V encode as `0x0D86`, `0x0D9D` and so on.

Reassembly rules:

1. Resync by scanning for `0x7E`; discard anything before it.
2. Wait for 8 bytes, then read `payload_len` from offset 7.
3. Wait for `8 + payload_len + 3` bytes total.
4. Validate CRC-16/MODBUS over everything preceding the CRC; confirm the last byte is `0x0D`.
5. Time out and reset a partial frame after ~1 s.

Verified against captured frames at 20-, 7- and 1-byte chunk sizes.

### 5.6 Idle timeout and connection lifetime

**Before a successful handshake:** the BMS drops the connection ~4 s after connecting, regardless of what you write to `FFF2`. Writes it doesn't accept do not reset the timer.

**After a successful handshake:** the connection is stable. Observed holding across a 3 s idle gap with no reconnect and no drop, servicing further polls normally.

This resolves the connection-model question for GateLink: **a persistent connection is viable.** Whether it's *preferable* is now purely a power question — hold the link open versus connect-per-poll — to be measured at M7, not a protocol constraint.

### 5.7 Reference capture (ground truth for M5)

Taken from `aiobmsble` 0.27.0 against this battery, at rest, fully charged. **Use these frames as unit-test fixtures** — the decoder can be developed and tested on the build host with no hardware attached.

```
req  0x92: 1e 00 01 03 00 92 00 00 b7 24 0d
rsp  0x92: 7e 00 01 03 00 92 00 3c "WT30_10004SW14_L_01" ... 2e b7 0d

req  0x8C: 1e 00 01 03 00 8c 00 00 b1 44 0d
rsp  0x8C: 7e 00 01 03 00 8c 00 20
           04 0d89 0da1 0d9c 0d9b 04 0b82 0b9d 0b7f 0b7e
           4000 0570 03e7 03e8 0001 03e8 0064 55 a3 0d

req  0x8D: 1e 00 01 03 00 8d 00 00 71 15 0d
rsp  0x8D: 7e 00 01 03 00 8d 00 18
           04 00000000 04 0000000000000000 0000 0629 00000000 0000 bd 3f 0d
```

Decoded values these must reproduce:

| Field | Value |
|---|---|
| cell count / temp sensors | 4 / 4 |
| cell voltages | 3.465, 3.489, 3.484, 3.483 V (delta 24 mV) |
| temperatures | 21.5 (ambient), 24.2 (MOSFET), 21.2, 21.1 (cells) °C |
| pack voltage | 13.92 V |
| current | 0.0 A |
| SOC | 100 % |
| remaining / nominal | 99.9 / 100.0 Ah |
| cycles | 1 |
| charge & discharge MOSFET | both on |
| problem code | 0 |

Device info from `0x92`:

| Field | Value |
|---|---|
| sw_version | `WT30_10004SW14_L_01` |
| manufacturer | `11112222333344445555` |
| serial_number | `IKKKK0000AII00000000` |

The manufacturer and serial strings are obviously unprogrammed placeholder patterns. Don't build anything that keys off them — in particular, don't use serial number to distinguish batteries if a second one is ever added.

**Command `0x8D` is only partially decoded.** The leading `04 … 04 …` mirrors the cell/temp counts, and `0629` sits where MOSFET and status bits appear to live (both MOSFETs read on, problem code 0 in this capture). A capture during an actual protection event would be needed to map the bitmaps properly, which isn't worth engineering. Treat `0x8D` as: nonzero anywhere in the alarm region means "something is wrong, raise it to HA" — and let the human look at the app for detail. That is honest about what we know and still delivers the alert the PRD asks for.

### 5.8 Still to verify on hardware

- **RSSI at the real mounting distance.** Desk testing gives -77 to -88 dBm; the battery's transmitter is weak and its antenna appears shielded by the BMS heat sink (confirmed on two independent radios). At the gate the node sits ~12" away, which should be comfortable, but measure it at M7 and expose it as a diagnostic — it is the early-warning signal for a link degrading from moisture, corrosion, or a shifted mount.
- **Current sign convention.** Still the only open protocol question. Only ever observed at 0.0 A (raw `0x4000`). Capture `0x8C` while charging and again under load, and confirm bit `0x4000` is the discharge flag.

Resolved since v0.2: handshake target (`FFFA`, §5.1), write mode (with response), command head (`0x1E` only), MTU behaviour (§5.5), and connection lifetime (§5.6).

### 5.9 How this was determined

Worth recording, because the method generalizes to the BusT4 work.

Black-box probing found nothing across ~30 combinations of characteristic, protocol, write mode, and timing — because it was searching the wrong space entirely (the handshake target was never a variable). What worked was **instrumenting the known-good implementation**: monkey-patching `BleakClient.write_gatt_char` / `read_gatt_char` / `start_notify` to log every call with arguments and timing, then running `aiobmsble` through it. The complete answer appeared in the first eight lines of output.

`tools/instrument.py` is kept in the repo for exactly this purpose. When a working reference implementation exists, watching it is strictly better than hypothesizing about it — reach for that first, not after the guesses are exhausted.

## 6. Software stack

| Layer | Choice | Rationale |
|---|---|---|
| Build system | PlatformIO (VS Code extension) | Same toolchain the rest of LRAN will use; handles the S3 toolchain and board defs |
| Framework | Arduino-ESP32 | Community BMS/BusT4/VE.Direct code is all Arduino-flavoured |
| BLE stack | NimBLE-Arduino | ~40 % of Bluedroid's RAM/flash — matters when LoRa + BusT4 + VE.Direct share the S3 later |
| Board id | `heltec_wifi_lora_32_V3` | — |

`platformio.ini` starting point:

```ini
[env:heltec_wifi_lora_32_V3]
platform = espressif32
board = heltec_wifi_lora_32_V3
framework = arduino
monitor_speed = 115200
monitor_filters = time, esp32_exception_decoder
lib_deps =
    h2zero/NimBLE-Arduino@^1.4.2
build_flags =
    -DCORE_DEBUG_LEVEL=3
```

## 7. Code architecture (designed for reuse)

The whole point is that GateLink inherits this. So the protocol logic must not know that BLE or Arduino exist.

```
wattcycle-reader/
├── platformio.ini
├── README.md
├── docs/
│   ├── poc-plan.md              <- this document
│   └── xdzn-001-sniff.txt       <- the nRF capture
├── include/
├── lib/
│   └── bms_ble/
│       ├── BmsData.h            struct BmsData { voltage_mV, current_mA, soc_pct, ... }
│       ├── TdtProtocol.h/.cpp   frame build, CRC, reassembly, decode — pure C++, no deps
│       ├── BmsTransport.h       abstract: write(), onNotify(), isConnected()
│       ├── NimBleTransport.h/.cpp   the only file that touches NimBLE
│       └── TdtBmsClient.h/.cpp  state machine: scan -> connect -> HiLink -> subscribe -> poll
├── src/
│   ├── BmsDisplay.h/.cpp        SSD1306 rendering; takes BmsData + link state
│   └── main.cpp                 thin: construct client, poll, print, render
└── test/
    └── test_tdt_protocol/       native tests against the captured frames in §5.7
```

Two rules that make this reusable:

1. **`TdtProtocol` compiles on the host.** No `Arduino.h`, no NimBLE. That means you can unit-test the decoder on the laptop against a captured hex frame, with no hardware in the loop — which is enormously faster than flash-and-squint.
2. **BLE lives behind `BmsTransport`.** When GateLink needs the BMS read, it supplies its own transport (or the same one) and the state machine and decode come along free.

State machine states: `IDLE → SCANNING → CONNECTING → MTU → HANDSHAKE(FFFA) → ACK_CHECK → SUBSCRIBED → POLLING`, with `DISCONNECTED` falling back to `SCANNING` after a backoff. Non-blocking throughout — no `delay()` in the connect path, because GateLink will be servicing LoRa and BusT4 on the same core.

## 8. Serial output

**Decision: human-readable.** One aligned key/value block per poll — the point of the PoC is reading values with your eyes and comparing them to the phone app, and JSON adds noise to that.

Keep the print function isolated in `main.cpp` (`printBmsData(const BmsData&)`) so a JSON or MQTT emitter is a drop-in swap later. The decode never produces text; it produces a struct.

Also useful during bringup: a raw-hex dump mode that prints every notification as it arrives, before any parsing. That's what you look at when the decode disagrees with the phone app.

## 9. On-board display

The Heltec V3 carries a 128×64 SSD1306 OLED on I²C. Showing SOC, voltage and temperature on it makes the PoC self-contained — you can walk the board over to the battery and see live values without a laptop attached, which is exactly what you'll want when this moves to the gate enclosure.

**Heltec V3 pin map (this is the part that trips everyone up):**

| Signal | GPIO |
|---|---|
| OLED SDA | 17 |
| OLED SCL | 18 |
| OLED RST | 21 |
| Vext control | 36 (**active LOW**) |

The OLED is powered through Vext, not directly. If you skip the Vext step the display simply stays dark and the I²C scan finds nothing — it looks like a dead panel or a wrong address. Bring-up order:

```cpp
pinMode(VEXT, OUTPUT);
digitalWrite(VEXT, LOW);        // LOW = Vext ON
delay(100);
pinMode(OLED_RST, OUTPUT);
digitalWrite(OLED_RST, LOW);    // pulse reset
delay(20);
digitalWrite(OLED_RST, HIGH);
delay(50);
// then Wire.begin(17, 18) / display.init()
```

I²C address is `0x3C`.

**Library:** ThingPulse `ESP8266 and ESP32 OLED driver for SSD1306 displays` — despite the name it's the driver Heltec's own examples use, and it has a clean text/layout API.

```ini
lib_deps =
    h2zero/NimBLE-Arduino@^1.4.2
    thingpulse/ESP8266 and ESP32 OLED driver for SSD1306 displays@^4.4.0
```

U8g2 is the alternative if you want more font control; heavier, and unnecessary here.

**Screen layout (128×64):**

```
┌────────────────────────────┐
│ XDZN_001_49A1        ●     │  name + link indicator
│                            │
│   87%                      │  SOC, largest font
│                            │
│ 13.42 V      -4.2 A        │  pack voltage, current
│ 24.1 °C      DSG           │  temp, FET state
└────────────────────────────┘
```

The link indicator matters more than it sounds: a filled dot when connected and subscribed, hollow when scanning/reconnecting. Without it, stale numbers on a dropped connection look identical to live ones — and that misreading is exactly the failure mode you don't want inherited into GateLink.

Add a "stale after N seconds" rule too: if no valid frame has arrived in, say, 15 s, dim or dash out the values rather than showing old data.

**Keep it decoupled.** `BmsDisplay.h/.cpp` takes a `const BmsData&` and a connection state enum, and knows nothing about BLE or the protocol. Same rule as the serial printer: decode produces a struct, presentation layers consume it.

**Note for GateLink:** the PRD has the gate node holding Vext off during normal operation with the display on a button and auto-on during debug modes. That's fine — this code is the display half of that, and the Vext control shown above is the same switch the power strategy uses. Building the display behind a `displayOn()/displayOff()` pair here means GateLink inherits both behaviours.

## 10. Bench tooling and the Python reference

Pre-flight is **complete** — the protocol is captured (§5) and the central risk is closed. What remains from that effort is tooling worth keeping.

### 10.1 `aiobmsble` as the reference implementation

Install on the build host and keep it available for the life of the sub-project:

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install aiobmsble aiooui     # aiooui is needed by the CLI entry point
aiobmsble -v                     # autodetect and dump every reachable BMS
```

This is the oracle. At every milestone, run the C++ decode and the Python decode against the same battery minutes apart and compare field by field. A firmware bug then looks like a disagreement rather than a plausible-looking wrong number — which is the difference between finding it in minutes and shipping it to the gate.

Also useful as a live check when the gate node eventually misbehaves in the field: run it from the Kubuntu laptop next to the enclosure and see whether the BMS or the firmware is at fault.

### 10.2 `tools/bms_probe.py`

The probe script from the discovery phase still earns its place, once updated for the real protocol: send `HiLink`, subscribe `FFF1`, send an arbitrary command, dump raw frames with timestamps. That's the tool for §5.8 — capturing `0x8C` under charge and under load to settle the current sign convention, which `aiobmsble` will happily decode but won't show you the raw bytes for.

Move it into the repo under `wattcycle-reader/tools/` rather than leaving it in `~/Downloads`.

It carries a `--selftest` mode that validates framing, CRC and decode against the captured frames in §5.7 with no hardware attached. Those same fixtures and assertions port directly to `test/test_tdt_protocol/` as the C++ unit tests.

### 10.2b `tools/instrument.py`

Logs every BLE call `aiobmsble` makes — connect, MTU, subscribe, write (with its `response` flag), read, and every notification, all timestamped. This is what found the `FFFA` handshake after black-box probing failed. Keep it: when the C++ driver misbehaves in a way that isn't obvious, running this alongside gives you a byte-exact reference for the same exchange.

### 10.3 What was learned (kept deliberately)

Recorded because the reasoning generalizes to the other LRAN nodes:

- **Service topology is a weak signal.** `FFF0/FFF1/FFF2` is used by JBD, Daly and TDT with mutually incomprehensible protocols. Never infer a protocol from a UUID.
- **A successful ATT write proves nothing.** Twenty writes were acknowledged at the transport layer and ignored at the application layer. "No error" is not "understood".
- **Look for the community implementation before reverse engineering.** One targeted search found a maintained library covering this exact battery. That search should have come first — it would have saved the entire probing exercise. For WellLink and the BusT4 work, search first.
- **A phone app is a poor exploration tool.** The iOS nRF workflow can't act inside a 3.9 s window, and its descriptor listing was actively misleading.

## 11. Milestones

| # | Milestone | Done when |
|---|---|---|
| M0 | Toolchain up | `pio run` builds, board flashes, serial monitor shows boot log |
| M0b | Display alive | Vext on, OLED reset pulsed, "hello" text renders — do this at M0 so the panel is proven before it has to show real data |
| M1 | BLE scan | Serial lists nearby BLE devices; `XDZN_001_49A1` / `C0:D6:3C:58:49:A1` appears with RSSI |
| M2 | Connect + discover | Connects, enumerates `FFF0`, confirms `FFF1`/`FFF2` handles |
| M3 | Handshake + raw | MTU requested; `HiLink` → `FFFA`; read-back `0x01` verified; subscribe `FFF1`; send `0x8C`; raw bytes dumped |
| M4 | Reassembly + CRC | Fragments joined to `0x0D`; CRC-16/MODBUS validates |
| M5 | Decode 0x8C | Voltage / current / SOC / temps printed, **matching `aiobmsble` output field for field** |
| M6 | Alarms | `0x8D` decoded for protection bitmaps and MOSFET state |
| M6b | Display live data | SOC / voltage / current / temp on the OLED, with link indicator and stale-data handling |
| M7 | Poll loop + resilience | Polls on interval; survives battery going out of range and coming back; logs RSSI, consecutive-failure count, and free heap |
| M8 | Library extraction | Protocol code under test, transport abstracted, README written, ready to merge |

M0–M3 is one sitting. M4–M5 is where the real debugging is.

## 12. Repo and git workflow

**Recommendation: a branch in the existing LRAN repo, not a clone.**

A clone gives you a second remote to keep in sync and a merge that git treats as unrelated history. A branch gives you the same isolation, a real pull request, and a one-command merge. Clone only if you wanted this to become a separately published project, which you don't — it's a component.

The repo exists on GitHub but isn't on the build MacBook yet, so the first step is a clone **on that machine** (§13.1). Use SSH if you already have a key there; otherwise HTTPS with a personal access token, or `brew install gh && gh auth login` once — easiest of the three.

The field laptop gets its own clone later, only if you want to reflash in the field. Git is the transfer mechanism between the two; don't copy the folder around by hand.

```bash
cd ~/projects                       # or wherever you keep work
git clone git@github.com:<you>/<lran-repo>.git
cd <lran-repo>

git switch -c feature/wattcycle-bms-reader
mkdir -p wattcycle-reader/{src,lib/bms_ble,docs,test}
# ... scaffold, build, commit as you go ...
git push -u origin feature/wattcycle-bms-reader
```

When M8 lands, open a PR on GitHub, review the diff yourself (a genuinely good habit — it's the last time you'll see the whole change at once), and merge to `main`.

If you ever want the PoC and main checked out simultaneously, `git worktree add ../lran-bms feature/wattcycle-bms-reader` gives you a second working directory sharing one repo. Not needed yet.

Commit granularity for learning purposes: commit at each milestone, with a message saying what now works. `M3: subscribe to FFF1 and dump raw notifications`.

## 13. Host machines

Two machines, two roles. Both need a serial monitor; only one needs a toolchain.

### 13.1 Build host — M5 MacBook Pro, macOS 26

This is where VS Code, PlatformIO, Claude Code and the git clone live. All development and flashing happens here.

1. **VS Code + PlatformIO IDE extension.** PlatformIO pulls the entire ESP32-S3 toolchain itself on first build — no Homebrew, no manual ESP-IDF install. Apple Silicon is well-trodden ground now; the espressif32 platform ships native `darwin_arm64` toolchains, so no Rosetta. First `pio run` takes several minutes downloading, then it's cached.
2. **Pin the platform version** rather than floating, so a mid-project platform release can't change your toolchain underneath you:
   ```ini
   platform = espressif32@^6.9.0
   ```
   If the toolchain ever misbehaves on very new silicon, that pin is the first thing to bisect on.
3. **USB-serial driver: probably nothing to install.** Current macOS ships in-kernel drivers for both CP2102 and CH34x/CH9102, which covers every Heltec V3 batch. Plug the board in and check:
   ```bash
   ls /dev/cu.*
   ```
   Expect `/dev/cu.usbserial-*` (CP2102) or `/dev/cu.usbmodem*` / `/dev/cu.wchusbserial*` (CH9102). If nothing new appears, install the WCH vendor driver — but try bare first.
4. **Approve the accessory.** Apple Silicon Macs prompt *"Allow accessory to connect?"* the first time a USB device is attached. Miss the dialog and the port never enumerates, which looks exactly like a driver problem. System Settings → Privacy & Security if you dismissed it.
5. **Use `/dev/cu.*`, never `/dev/tty.*`.** Both appear for the same device. The `tty.` variant blocks waiting for carrier detect and hangs uploads with no useful error. PlatformIO usually picks correctly; pin it if not:
   ```ini
   upload_port = /dev/cu.usbserial-0001
   monitor_port = /dev/cu.usbserial-0001
   ```

No permissions setup is required on macOS — the `dialout` group and `brltty` problems below are Linux-only.

### 13.2 Field host — Kubuntu MacBook Pro

Field use is serial monitoring and, occasionally, reflashing a build you already have. You don't need the full toolchain here, but you do need port access.

```bash
# 1. Serial port permissions — without this you get "permission denied"
sudo usermod -aG dialout $USER      # then log out and back in

# 2. brltty hijacks CH340/CP2102 adapters on Ubuntu. Check first:
sudo systemctl status brltty
sudo apt remove brltty              # if present and you don't use a braille display

# 3. Plug the board in, confirm it enumerates
dmesg | tail -20
ls /dev/ttyUSB* /dev/ttyACM*
```

The board needs no drivers on Linux; both CP2102 and CH9102 are in-kernel.

For monitoring only, a terminal program is enough — `minicom -D /dev/ttyUSB0 -b 115200`, or `picocom`, or `tio`. If you want to reflash in the field, `pipx install platformio` plus a `git pull` of the branch gives you the same `pio run -t upload` without touching VS Code. Worth doing once at the bench, before you need it at the gate.

### 13.3 Flashing, either host

PlatformIO auto-resets the board into the bootloader via DTR/RTS. If that fails on a Heltec V3, hold **PRG/BOOT**, tap **RST**, release **PRG**, then upload. After a manual-bootloader upload, press **RST** to run the new firmware.

## 14. When to hand off to Claude Code

Rough division of labour, since this is your first pass through the workflow:

**Here in chat** — anything where you want to think out loud: protocol questions, decode disagreements ("the app says 62 % and I'm reading 0x3E, is that right?"), architecture decisions, reviewing a design before it's built.

**Claude Code, in VS Code** — anything that touches the repo or the toolchain: creating files, editing them, running `pio run`, reading build errors, git operations. It can see your actual error output, which is worth more than you pasting it.

**The handoff point is now-ish**, once you've answered the remaining questions below. The natural sequence:

1. Finish this doc (answer §16, I revise).
2. Create the branch yourself with the commands in §12 — you should do the first one by hand, once.
3. Open the LRAN repo folder in VS Code, launch Claude Code, and paste it a starter prompt like:

   > Read `wattcycle-reader/docs/poc-plan.md`. Scaffold the PoC as described in
   > §7: platformio.ini, the `bms_ble` library skeleton with `TdtProtocol` free
   > of Arduino dependencies, and a `main.cpp` that reaches milestone M1 (BLE
   > scan, print discovered devices). Also add the native unit tests in
   > `test/test_tdt_protocol/` using the captured frames in §5.7 — those run on
   > the host and should pass before any hardware is involved. Don't implement
   > past M1 otherwise. Then walk me through building and flashing it.

4. Work milestone by milestone with Claude Code, coming back here when something surprising happens on the wire.

Keep this doc in the repo. It's the shared context between the two tools — Claude Code reading a spec file is far more reliable than re-explaining the protocol each session.

## 15. Risks

| Risk | Status | Mitigation |
|---|---|---|
| ~~BMS is a JBD lookalike~~ | **Confirmed TDT** | Protocol documented in §5 against live capture |
| ~~Auth handshake required~~ | **Resolved** | Fixed `HiLink` magic string, no challenge/response |
| Weak BLE transmitter in the battery | **Characterized, low concern** | -77 to -88 dBm at desk range, confirmed independently on a second radio (nRF/iPhone); needs the phone resting on the battery to beat -60 dBm. Antenna is likely shielded by the BMS heat sink. Gate node sits ~12" away, so expect -55 to -65 dBm. Mitigations: publish RSSI as a diagnostic sensor, and mount the node so its own chip antenna isn't also shielded |
| ~~Idle disconnect forces reconnect churn~~ | **Resolved** | Link is stable after handshake (§5.6); persistent vs per-poll is now a power question only |
| Current sign convention unverified | Open | Only observed at 0 A; check against the app under charge and under load |
| BLE + LoRa RAM contention on the S3 (GateLink) | Open | NimBLE; measure free heap at M7 |
| Library values are not safety-grade | Accepted | Author warns explicitly against safety-relevant use; BMS data is for monitoring and alerting only — never let it gate a charge decision, the MPPT stays authoritative |

## 16. Open questions

**Resolved:**

- Repo is on GitHub → §12 starts with a clone on the macOS build host.
- Build on the M5 MacBook (macOS 26); Kubuntu MacBook is field-only (§13).
- Serial output human-readable, print path isolated for later swap.
- **BMS identified and protocol captured** — §5. The PoC's central unknown is closed before any firmware was written, which was the entire point of §2.

**Still open (defaults assumed):**

1. **Framework** — PlatformIO + Arduino.
2. **Poll interval** — 5 s for the PoC. Note this interacts with the 3.9 s idle timeout: polling faster than that holds the link open for free.
3. **Connection model** — persistent vs connect-per-poll, decided at M7 on measured draw.
4. **Port vs wrap** — porting the TDT protocol to C++ is the plan, since the gate node has no Python. `aiobmsble` (Apache-2.0) is the reference; record it in PRD §12 alongside the other third-party licenses.
