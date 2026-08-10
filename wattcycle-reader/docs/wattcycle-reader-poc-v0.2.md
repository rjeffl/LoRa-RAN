# WattCycle BLE BMS Reader — Proof of Concept

**Sub-project of:** LoRa Remote Automation Network (LRAN)
**Status:** v0.2 — BMS identified, protocol captured, ready to scaffold
**Purpose:** validate BLE reading of the gate battery's BMS and produce a reusable driver for the GateLink node.

---

## 1. Objective

Read State of Charge, pack voltage, current and temperature from a WattCycle 100 Ah mini LiFePO4 battery over BLE, using a Heltec WiFi LoRa 32 V3 as a BLE central, and stream the decoded values over USB serial to a laptop.

Success = a Heltec board, tethered to a laptop over USB, printing a live decoded battery record once every N seconds, with clean reconnect after the battery link drops.

Secondary objective (equally important): use this small, self-contained build as the vehicle for learning the full firmware workflow — repo layout, branch, PlatformIO build, flash, monitor, commit, merge.

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
| Target | WattCycle 100 Ah mini, XiangDian BMS |
| BMS name | `XDZN-001-49A1` |
| BMS MAC | `C0:D6:58:3C:A1:49` |
| Build host | M5 MacBook Pro, macOS 26 — VS Code, PlatformIO, Claude Code, git clone |
| Field host | Older MacBook Pro running Kubuntu — serial monitoring at the gate, optional reflash |

Power and data both come from the laptop's USB port. No external supply, no soldering, no wiring.

## 4. GATT layout (confirmed)

**Service `0xFFF0`** — the data path:

| Char | Properties | Role |
|---|---|---|
| `0xFFF1` | notify, read | responses arrive here |
| `0xFFF2` | write, write-no-response, read | `HiLink` handshake + requests go here |
| `0xFFFA` | write, write-no-response, read | unused |

**Service `02F00000-…-FE00`** with characteristics `FF00`–`FF05` — carries no battery data. This UUID appears on completely unrelated hardware (Govee thermometers, WLT8016 modules), which identifies it as a **chipset-level vendor service from the Telink BLE SDK**, not something the BMS vendor defined. Ignore it. Do not write to it.

Two corrections to earlier assumptions, recorded because they each cost time:

- The iOS sniff appeared to show a CCCD on `FFF2`. macOS reports `CBATTErrorDomain Code=6` when subscribing there, and the property list contains no `notify`. `FFF2` is write-only-plus-read. The iOS descriptor listing was misleading.
- The `FFF0/FFF1/FFF2` triple is **not** proof of a JBD device. TDT uses the identical layout with a completely different protocol. Service topology is a weak signal; only a successful exchange settles it.

Bench facts about the radio link: the battery advertises as `XDZN_001_49A1` (underscores), MAC `C0:D6:3C:58:49:A1`, at roughly **-80 to -84 dBm even at close range** — the antenna is inside the case. Manufacturer data in the advertisement (`b'<XI\xa1\xffXD'`) contains the MAC bytes but no usable telemetry, so passive advertisement-only monitoring is not an option; a connection is required.

## 5. Protocol — TDT smart BMS (confirmed on hardware)

**The BMS is a TDT, not a JBD.** Despite wearing JBD's `FFF0/FFF1/FFF2` service layout, it speaks an entirely different protocol. This cost a round of blind probing (20 write/protocol combinations, zero responses) and was settled by `aiobmsble`, which identified it as "TDT smart BMS" and returned a full decode.

Everything below is verified against a live capture from this battery.

### 5.1 Connection sequence

Order matters. This is why blind probing failed.

1. Connect.
2. **Write the ASCII string `HiLink` (`48 69 4C 69 6E 6B`) to `FFF2`.** Unlock handshake — a fixed magic string, not a challenge/response. Without it the BMS ignores everything and drops the link after ~3.9 s.
3. *Then* subscribe to `FFF1` (write CCCD).
4. Send requests to `FFF2`; responses notify on `FFF1`.

### 5.2 Frame format

```
request:   1E 00 01 03 00 <cmd> 00 00 <crc_hi> <crc_lo> 0D      (11 bytes)
response:  7E 00 01 03 00 <cmd> 00 <len> <payload...> <crc_hi> <crc_lo> 0D
```

- **Request head is `0x1E`; response head is `0x7E`.** The library probes `0x7E` first, times out three times, then finds `0x1E` works ("detected command head: 0x1E"). Skip straight to `0x1E` and save six seconds per connection — but keep the probe as a fallback, since other TDT units evidently use `0x7E`.
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

### 5.5 Fragmentation

Responses run 40+ bytes against a 23-byte default MTU, so **frames arrive split across notifications**. Accumulate until the `0x0D` terminator, then validate the CRC, and time out partial frames after ~1 s. This was always going to be required; it is not TDT-specific.

### 5.6 Idle timeout

The BMS drops the connection ~3.9 s after the last activity it recognizes. Poll faster than that to hold the link, or accept a connect-per-poll cycle. Measure both for GateLink — see §15.

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
│       ├── JbdProtocol.h/.cpp   frame build, checksum, reassembly, decode — pure C++, no deps
│       ├── BmsTransport.h       abstract: write(), onNotify(), isConnected()
│       ├── NimBleTransport.h/.cpp   the only file that touches NimBLE
│       └── JbdBmsClient.h/.cpp  state machine: scan -> connect -> subscribe -> poll -> reconnect
├── src/
│   ├── BmsDisplay.h/.cpp        SSD1306 rendering; takes BmsData + link state
│   └── main.cpp                 thin: construct client, poll, print, render
└── test/
    └── test_jbd_protocol/       native tests against captured frames
```

Two rules that make this reusable:

1. **`JbdProtocol` compiles on the host.** No `Arduino.h`, no NimBLE. That means you can unit-test the decoder on the laptop against a captured hex frame, with no hardware in the loop — which is enormously faster than flash-and-squint.
2. **BLE lives behind `BmsTransport`.** When GateLink needs the BMS read, it supplies its own transport (or the same one) and the state machine and decode come along free.

State machine states: `IDLE → SCANNING → CONNECTING → DISCOVERING → SUBSCRIBED → POLLING`, with `DISCONNECTED` falling back to `SCANNING` after a backoff. Non-blocking throughout — no `delay()` in the connect path, because GateLink will be servicing LoRa and BusT4 on the same core.

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
│ XDZN-001-49A1        ●     │  name + link indicator
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

## 10. Bench pre-flight (do this before writing any firmware)

The single biggest risk in §15 — "is this actually a JBD-compatible BMS?" — can be settled before any firmware exists. But **not from an iPhone.**

### 10.1 Why the iPhone attempt fails

Look again at the timing in the nRF capture: connect at `:08.138`, discovery complete at `:09.066`, disconnect at `:13.045`. Roughly four seconds after discovery finished, **with no write ever attempted**.

That is the signature of a JBD idle-disconnect timeout, not necessarily an auth handshake. These BMS units drop any central that hasn't sent a valid command within a few seconds of connecting. On iOS you cannot tap through *enable notifications → navigate to FFF2 → enter hex → send* inside that window. It's a UI-speed problem.

An authentication requirement is still possible, but it's the second hypothesis, not the first, and the test below distinguishes them cleanly.

### 10.2 Scripted probe (macOS or Linux)

`bms_probe.py` uses **bleak**, which speaks CoreBluetooth on macOS and BlueZ on Linux. It connects, subscribes, and fires the request in one uninterrupted sequence — comfortably inside the timeout.

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install bleak
python3 bms_probe.py --dump
```

The script prints timestamped raw notification bytes as they arrive, reassembles fragments, validates the checksum, and decodes the result. Flags: `--reg 0x04` for cell voltages, `--with-response` to switch the write type, `--dump` for the full GATT table.

**Which host:** either works. The **Kubuntu laptop is slightly better for this one test** because BlueZ exposes real MAC addresses, so you can confirm you're talking to `C0:D6:58:3C:A1:49` and not a neighbour's battery. macOS CoreBluetooth hides MACs behind opaque per-host UUIDs, so the script matches on advertised name there. If you have both on the bench, use Linux.

Close the vendor phone app first — one central at a time.

### 10.3 Reading the result

| What you see | Meaning | Next step |
|---|---|---|
| `DD 03 00 …` frame, checksum OK, sane values | Confirmed JBD. §5 decode table applies. | Build with confidence |
| Response arrives in 2–3 chunks | Expected — confirms the reassembly requirement is real | Note it; the driver must handle it |
| `still connected at t+8s` after the write | **The write reset the idle timer.** Proves the timeout hypothesis and that the BMS accepts our commands | Firmware just needs to poll promptly after connecting |
| Disconnect at ~t+4s *even after* the write | Write was rejected or ignored | Retry with `--with-response`; if still dropping, auth is likely |
| Status byte non-zero (`DD 03 80 …`) | Command understood but rejected | Register or framing differs — bring bytes back to chat |
| Nothing, no drop | Notifications enabled but no reply | Check `--dump` output: does `FFF2` list WRITE, WRITE NO RESPONSE, or both? |

### 10.4 If it really is an auth handshake

Then the next move is capturing what the *vendor app* sends, since it evidently gets in. On Android that's straightforward — enable Bluetooth HCI snoop log in developer options, run the app, pull the log, open it in Wireshark, and read the first few writes after connect. If you have any Android device available, that's the definitive answer. iOS has no equivalent without a dedicated sniffer.

Worth noting: the unfiltered `--dump` GATT output is itself evidence. A BMS with a real auth gate usually exposes a distinct challenge/response characteristic beyond the standard `FFF1`/`FFF2` pair. Your capture shows only the standard pair plus a Telink OTA service — which argues *against* an auth scheme.

### 10.5 Ground truth

Once a read succeeds, open the vendor app and record SOC, pack voltage, current and temperature at that moment. Save it in the repo next to the raw hex. That's your check for M5, and the battery will have moved on by the time you get there.

## 11. Milestones

| # | Milestone | Done when |
|---|---|---|
| M0 | Toolchain up | `pio run` builds, board flashes, serial monitor shows boot log |
| M0b | Display alive | Vext on, OLED reset pulsed, "hello" text renders — do this at M0 so the panel is proven before it has to show real data |
| M1 | BLE scan | Serial lists nearby BLE devices; `XDZN-001-49A1` / `C0:D6:58:3C:A1:49` appears with RSSI |
| M2 | Connect + discover | Connects, enumerates `FFF0`, confirms `FFF1`/`FFF2` handles |
| M3 | Handshake + raw | `HiLink` written to `FFF2`, then subscribe `FFF1`, then send `0x8C`; raw bytes dumped as hex |
| M4 | Reassembly + CRC | Fragments joined to `0x0D`; CRC-16/MODBUS validates |
| M5 | Decode 0x8C | Voltage / current / SOC / temps printed, **matching `aiobmsble` output field for field** |
| M6 | Alarms | `0x8D` decoded for protection bitmaps and MOSFET state |
| M6b | Display live data | SOC / voltage / current / temp on the OLED, with link indicator and stale-data handling |
| M7 | Poll loop + resilience | Polls on interval; survives battery going out of range and coming back |
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

   > Read `wattcycle-reader/docs/poc-plan.md`. Scaffold the PoC exactly as described in §7: platformio.ini, the `bms_ble` library skeleton with `JbdProtocol` free of Arduino dependencies, and a `main.cpp` that reaches milestone M1 (BLE scan, print discovered devices). Don't implement past M1 yet. Then walk me through building and flashing it.

4. Work milestone by milestone with Claude Code, coming back here when something surprising happens on the wire.

Keep this doc in the repo. It's the shared context between the two tools — Claude Code reading a spec file is far more reliable than re-explaining the protocol each session.

## 15. Risks

| Risk | Status | Mitigation |
|---|---|---|
| ~~BMS is a JBD lookalike~~ | **Confirmed TDT** | Protocol documented in §5 against live capture |
| ~~Auth handshake required~~ | **Resolved** | Fixed `HiLink` magic string, no challenge/response |
| Weak BLE signal (-80 dBm at close range) | Open | Gate node sits in the same enclosure; measure link quality at M7 |
| 3.9 s idle disconnect forces fast polling or reconnect churn | Open | Measure hold-open vs connect-per-poll power draw at M7 |
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
