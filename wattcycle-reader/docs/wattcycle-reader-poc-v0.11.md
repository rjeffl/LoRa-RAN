# WattCycle BLE BMS Reader — Proof of Concept

**Sub-project of:** LoRa Remote Automation Network (LRAN)
**Status:** draft v0.1
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

## 4. What the nRF sniff tells us

From the attached capture, the BMS exposes two GATT services:

**Service `0xFFF0`** — this is the JBD / Xiaoxiang ("Overkill Solar") BMS profile. Characteristics:

| Char | Descriptors | Expected role |
|---|---|---|
| `0xFFF1` | has CCCD | **Notify** — BMS responses arrive here |
| `0xFFF2` | has CCCD | **Write** — commands go here |
| `0xFFFA` | none | vendor/unused |

**Service `02F00000-…-FE00`** with characteristics `FF00`–`FF05` — a vendor service, almost certainly Telink OTA / DFU. **Ignore it.** Do not write to it; writing to an OTA endpoint on a BMS is how you brick a BMS.

The `FFF0/FFF1/FFF2` triple is exactly the layout the `neilsheps/overkill-xiaoxiang-jbd-bms-ble-reader` project targets, which is the strongest signal we have that the JBD protocol will work as-is.

Two things the sniff does *not* tell us, to be confirmed on the bench:
- whether the BMS requires pairing/bonding (JBD units normally do not)
- whether it accepts a second central while the vendor phone app is connected (normally **no** — one central at a time, so keep the phone app closed during testing)

## 5. Protocol summary (JBD / Xiaoxiang)

Frames are written to `FFF2`, responses notified on `FFF1`.

**Request:** `DD A5 <reg> 00 <chk_hi> <chk_lo> 77`
Checksum = `0x10000 - sum(reg + len + payload)`, big-endian.

| Register | Meaning | Ready-made frame |
|---|---|---|
| `0x03` | Basic info (voltage, current, SOC, temps, protection, FET) | `DD A5 03 00 FF FD 77` |
| `0x04` | Individual cell voltages | `DD A5 04 00 FF FC 77` |
| `0x05` | Hardware/version string | `DD A5 05 00 FF FB 77` |

**Response:** `DD <reg> <status> <len> <payload…> <chk_hi> <chk_lo> 77`, where status `0x00` = OK.

**Basic info (0x03) payload layout:**

| Offset | Type | Field | Scaling |
|---|---|---|---|
| 0–1 | u16 | Pack voltage | ×10 mV |
| 2–3 | **i16** | Current | ×10 mA, negative = discharging |
| 4–5 | u16 | Residual capacity | ×10 mAh |
| 6–7 | u16 | Nominal capacity | ×10 mAh |
| 8–9 | u16 | Cycle count | — |
| 10–11 | u16 | Production date | packed |
| 12–15 | u32 | Balance status bitmap | per-cell |
| 16–17 | u16 | Protection status bitmap | per-fault |
| 18 | u8 | Software version | — |
| 19 | u8 | **RSOC** | percent |
| 20 | u8 | FET status | bit0 charge, bit1 discharge |
| 21 | u8 | Cell count | — |
| 22 | u8 | NTC count | — |
| 23+ | u16[] | Temperatures | °C = (raw − 2731) / 10 |

**Cell voltages (0x04):** array of u16, millivolts, one per cell.

### The gotcha that eats a day

Default BLE MTU is 23 bytes → 20 bytes of payload per notification. A basic-info response is ~34 bytes. **It will arrive split across two or three notifications.** The driver must accumulate bytes into a reassembly buffer and only parse when it sees the `0x77` terminator (and validate the checksum, and time out a partial frame after ~1 s). Every "the BMS returns garbage" report in the community repos is this bug.

Requesting a larger MTU is worth trying but many of these clone BMSs ignore it — reassembly is required regardless.

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
| M3 | Subscribe + raw | CCCD written on `FFF1`; `0x03` request sent; raw notification bytes dumped as hex |
| M4 | Reassembly | Fragments joined into a complete, checksum-valid frame |
| M5 | Decode basic info | Voltage / current / SOC / temps printed and **cross-checked against the vendor app** |
| M6 | Cell voltages | `0x04` decoded, cell count and per-cell mV printed |
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

| Risk | Likelihood | Mitigation |
|---|---|---|
| BMS is a JBD *lookalike* with a different payload layout | Medium | M3 raw dump before decode; compare against vendor app readings |
| BMS refuses connection / requires vendor app auth handshake | Low–Medium | nRF Connect on the phone to manually write `DD A5 03 00 FF FD 77` to `FFF2` and watch `FFF1` — settles it in 5 minutes without any firmware |
| BLE range from gate node to battery | Low | Both inside the same enclosure |
| BLE + LoRa RAM contention on the S3 (GateLink) | Medium | NimBLE now; measure heap in M7 so GateLink has a real number |
| Continuous BLE connection raises gate node idle draw | Low | Poll-then-disconnect is an option; measure both |

## 16. Open questions

**Resolved:**

- Repo is on GitHub, not yet cloned locally → §12 starts with a clone on the macOS build host.
- Build happens on a macOS MacBook Pro; the Kubuntu MacBook is field-only (§13).
- Both nRF Connect and the vendor app are available → §10 pre-flight is viable, and we have ground truth for M5.
- Serial output: human-readable, with the print path isolated for a later swap (see also §9).

**Still open (defaults assumed, say the word to change):**

1. **Framework** — assuming PlatformIO + Arduino. ESP-IDF is more capable but a harsher first project, and the community BMS/BusT4/VE.Direct code is all Arduino-flavoured.
2. **Poll interval** — assuming 5 s for the PoC (lively feedback while debugging); GateLink's production cadence gets decided later against measured power draw.
3. **Connection model** — assuming persistent connection with polling. The alternative (connect → read → disconnect each cycle) may be lower-power for GateLink; worth measuring at M7 rather than deciding now.
