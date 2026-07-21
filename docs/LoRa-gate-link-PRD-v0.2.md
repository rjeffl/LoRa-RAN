# Product Requirements Document — Remote Gate LoRa Link

**Working name:** GateLink *(placeholder)*
**Version:** 0.2 (draft, for iteration)
**Status:** Architecture settled; several implementation parameters open (see §13)
**Last updated:** 2026-07-21

---

## 1. Overview

### 1.1 Purpose
Provide bidirectional communication between a Home Assistant (HA) instance and a
remote, solar-powered driveway gate operator, over a point-to-point LoRa link.
The system lets HA send commands (open, close, stop, etc.) to the gate and pull
full status from both the gate controller and the solar charge controller.

### 1.2 System summary
Two Heltec WiFi LoRa 32 V3 nodes:

- **Gate node** (remote): interfaces to a Nice/Apollo **1050** control board via
  **BusT4** and to a Victron **MPPT 75/15** via **VE.Direct**. Runs custom,
  power-conscious firmware. Powered from the gate's 12 V FLA battery.
- **Bridge node** (house): receives/sends LoRa, connects to the LAN over WiFi,
  and acts as a **LoRa↔MQTT gateway** to the existing Mosquitto broker on the HA
  host. HA entities are created via **MQTT Discovery**.

```
+--------------------+        LoRa 915 MHz        +---------------------+
|     Gate node      |  <----------------------->  |     Bridge node     |
|  Heltec LoRa V3    |     (point-to-point)        |   Heltec LoRa V3    |
|                    |                             |                     |
|  BusT4  <-> 1050   |                             |   WiFi -> LAN       |
|  VE.Direct <-> MPPT|                             |   MQTT -> Mosquitto |
|  (opt GPIO loop)   |                             +----------+----------+
|  12V FLA -> USB    |                                        |
+--------------------+                                        v
                                                     +---------------------+
                                                     |   Home Assistant    |
                                                     | (Mosquitto broker,  |
                                                     |  MQTT Discovery)    |
                                                     +---------------------+
```

---

## 2. Goals and non-goals

### 2.1 Goals
- Reliable command path: HA → gate (open / close / stop / step-by-step / others
  BusT4 exposes), with acknowledgement.
- Full status retrieval from the 1050 (state, movement cause, loop-detector
  inputs where available) and the MPPT 75/15 (all VE.Direct fields).
- Lightweight, native-feeling HA integration (cover + sensor/binary_sensor/text
  entities via MQTT Discovery).
- Minimal gate-node power draw on the FLA battery.
- Built-in bench debug tooling (packet loopback, dummy status pushes, device
  simulators).
- Best-practice repo, build, and dev workflow (GitHub + VS Code + Claude Code).

### 2.2 Non-goals (v1)
- Strong cryptographic security / full replay & spoofing prevention. Basic
  command authentication only (see §6.4).
- Over-the-air firmware updates. Updates are USB-only.
- Persisting a sequence counter across reboots.
- Controlling multiple gates or multiple charge controllers.
- Replacing or modifying the 1050's own safety logic (loops, photocells,
  obstruction). Our system issues commands; the 1050 remains the safety
  authority. See §11.

---

## 3. System architecture

### 3.1 Nodes
| | Gate node | Bridge node |
|---|---|---|
| Board | Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) | Heltec WiFi LoRa 32 V3 |
| Firmware | Custom (power-optimized) | Custom (LoRa↔MQTT gateway) |
| Wired interfaces | BusT4 (1050), VE.Direct (MPPT), optional loop GPIO | none functional |
| Wireless | LoRa only (WiFi/BLE **disabled**) | LoRa + WiFi (BLE disabled) |
| Power | 12 V FLA → buck → 5 V USB | USB (mains); no constraint |
| Display | Off in normal op; on-demand for debug | May stay on |

### 3.2 Shared code
Both nodes link a common **LoRa protocol/packet library** (framing, message
types, HMAC, CRC, sequence handling) so the wire format is defined and tested
once. This shared library is also what the bench tools and simulators exercise.

### 3.3 Data flows
- **Command (HA → gate):** HA publishes to an MQTT command topic → bridge builds
  an authenticated LoRa command frame → gate node verifies + issues the BusT4
  command → gate node returns a command-ACK frame (received + execution result)
  → bridge publishes result/state.
- **Status (gate → HA):** gate node caches the latest VE.Direct frame and 1050
  status locally; it transmits status on (a) poll request, (b) gate-controller
  state change, or (c) a VE.Direct critical error/alert. Any **non-zero VE.Direct
  charger error code** triggers a send. Bridge republishes to MQTT state topics.
- **Poll:** a programmable poll scheduler (target **1–5 min**, configurable)
  requests a full status snapshot. Scheduler lives on the bridge (see §13, D4).

---

## 4. Hardware and BOM

### 4.1 Bill of materials (per system)
| Qty | Item | Notes |
|----:|------|-------|
| 2 | Heltec WiFi LoRa 32 V3 (US 915 MHz variant) | ESP32-S3, SX1262, 0.96" OLED |
| 2 | 915 MHz antennas | one per node |
| 1 | BusT4 breakout: 6P4C modular jack + pigtail | matches the 1050 "Oview" port |
| 1 | Bidirectional logic level shifter (3.3 V ↔ 5 V) | for the BusT4 data line |
| 1 | VE.Direct cable / JST-PH 2.0 4-pin pigtail | to the MPPT VE.Direct port |
| 1 | 12 V → 5 V USB buck converter | powers gate node from FLA |
| 1 | (Optional) opto-isolated input module | loop-detector dry-contact fallback |
| — | Enclosure, strain relief, fuse on FLA tap | field hardening |

### 4.2 BusT4 electrical — **safety critical**
The 1050's "Oview" port is a **6P4C modular jack** (Nice calls it "RJ45 6/4" —
6 positions, 4 contacts; loosely the RJ11/RJ12 form factor). It carries **4
conductors**: a half-duplex data pair plus power.

- **The connector carries ~24–28 V on its VCC pin.** Mis-wiring can instantly
  destroy the 1050 board. **Measure every pin before connecting anything.**
- **BusT4 data lines are 5 V logic**; the Heltec is 3.3 V → a level shifter is
  **mandatory**.
- **Do not** power the gate node from the BusT4 VCC. The node is powered from the
  FLA via the buck converter; keep the two domains separate (share GND only as
  required by the level shifter / bus reference — confirm during bring-up).
- BusT4 is a UART serial bus (19200 baud in the community implementations), **not
  CAN** — no CAN transceiver is used.

### 4.3 VE.Direct electrical
VE.Direct is a **3.3 V TTL UART** (19200 baud) on a JST-PH 2.0 connector.
Direct connection to a Heltec 3.3 V UART; no level shifting needed. Wire TX/RX
(observe device-perspective naming) and common GND.

### 4.4 Gate-node power source
FLA (12 V) → fused tap → buck converter → 5 V USB into the Heltec. Battery is the
single source for all gate-side power. See §8 for the power budget.

---

## 5. Firmware — gate node (custom)

### 5.1 Framework & libraries
- Build: **PlatformIO** (recommended over Arduino IDE for repo/CI hygiene),
  ESP32-S3 target.
- LoRa: **RadioLib** driving the SX1262.
- BusT4: **ported** from the existing C++ reverse-engineering work (see §12) —
  extract the frame construction/parsing (Open/Close/Stop/SBS, GET/SET info
  requests) and drop the ESPHome/WiFi scaffolding.
- VE.Direct: use/port **osh-labs/VE.Direct_mppt_arduino** for text-protocol
  parsing.

### 5.2 Responsibilities
1. **BusT4 client** — send commands; poll status via GET/INF requests; parse
   asynchronous events. Target fields: gate state (open/closed/opening/closing/
   stopped), **movement cause** (e.g. loop vs remote) if exposed, and
   **loop-detector inputs** if exposed. Fallback in §5.4.
2. **VE.Direct reader** — continuously parse the ~1 Hz text frames; cache the
   latest complete snapshot; watch the charger error field.
3. **LoRa endpoint** — receive commands, transmit status/ACK/event frames using
   the shared protocol library.
4. **Trigger/cache logic** — transmit status only on poll, gate state change, or
   VE.Direct critical error (non-zero error code always sends).
5. **Command authentication** — verify HMAC + sequence before acting (§6.4).
6. **Power management** — WiFi and BLE **off**; Vext / OLED **off** in normal
   operation; SX1262 RX duty-cycling as the primary power lever (§8).
7. **Debug** — on-demand display, packet loopback, dummy status push, leveled
   serial logging (§9).

### 5.3 Peripheral power control
Explicitly disable WiFi and BLE radios at boot; gate Vext so the OLED and its
rail are off unless a debug trigger enables them.

### 5.4 Loop-detector fallback
If BusT4 does not expose loop-detector state or movement cause, wire the loop
detector's dry-contact relay output to a gate-node GPIO (pull to ground), read as
a `binary_sensor`. `TBD` pending BusT4 sniffing results (§13, D3).

---

## 6. Firmware — bridge node (custom) & LoRa protocol

### 6.1 Framework & libraries
PlatformIO, ESP32-S3. RadioLib (SX1262). WiFi. MQTT client
(`PubSubClient` or an async MQTT client — `TBD`, §13 D5). Links the same shared
LoRa protocol library as the gate node.

### 6.2 Responsibilities
1. LoRa RX → decode → publish to MQTT **state** topics.
2. MQTT **command** topic subscribe → build authenticated LoRa command → TX →
   await ACK → publish result; retry with backoff on no ACK.
3. Publish **MQTT Discovery** config on boot (and on reconnect) for all entities.
4. **Availability** via MQTT Last-Will-and-Testament (marks device offline).
5. **Poll scheduler** issuing status requests to the gate. Runtime-configurable:
   the interval is exposed as an HA `number` entity (via MQTT Discovery) and
   applied live, so it can be changed from HA without reflashing.
6. Publish link diagnostics (RSSI/SNR, last-seen).
7. Debug: loopback mode, dummy-status publish.

### 6.3 LoRa link parameters
- Band: **US 915 MHz**, point-to-point (not LoRaWAN — no TTN duty cycle, but
  respect FCC part 15 limits).
- SF / BW / CR / TX power: **TBD**, chosen after the range test (§13, D1). Starting
  point for ~150 m near-LOS: SF7–9, BW 125 kHz, CR 4/5, moderate TX power, private
  sync word.

### 6.4 Message format & authentication
Frame (shared library):

```
| ver | msg_type | src | dst | seq | payload... | MAC(4-8B, cmd only) | CRC |
```

- **Message types:** `COMMAND`, `COMMAND_ACK`, `POLL`, `STATUS`, `EVENT`,
  `ERROR`, `PING/LOOPBACK`.
- **Command auth (commands only):** truncated **HMAC-SHA256** over
  `(boot_id, seq, command)` using a shared key. Status/poll frames are
  unauthenticated per requirements.
- **Sequence / replay:** monotonic counter, no cross-reboot persistence. On boot
  the gate node picks a random `boot_id`; the bridge accepts increasing `seq`
  within a `boot_id` and resyncs on a new `boot_id`. This meets the "basic
  protection" bar without persistence.
- **Reliability:** commands are ACKed (receipt + execution result); bridge
  retries with backoff. Status is fire-and-forget except poll responses.

---

## 7. Home Assistant integration (MQTT Discovery)

Native entities, published under a stable topic hierarchy (e.g.
`gatelink/<node>/...`) with retained discovery configs and an availability topic.

| Entity | Type | Source | Notes |
|---|---|---|---|
| Gate | `cover` (device_class: gate) | 1050 | open/close/stop; state from BusT4 |
| Movement cause | `sensor` (text) | 1050 | if exposed via BusT4 (§5.4) |
| Loop A / Loop B | `binary_sensor` | 1050 or GPIO | presence loops |
| Battery voltage | `sensor` (V) | MPPT | |
| Battery current | `sensor` (A) | MPPT | |
| Panel voltage / power | `sensor` (V / W) | MPPT | |
| Yield today | `sensor` (kWh) | MPPT | |
| Charge state | `sensor` (text) | MPPT | bulk/absorption/float/off |
| Charger error | `sensor` (text/code) | MPPT | non-zero → alert + push |
| LoRa RSSI / SNR | `sensor` (diagnostic) | bridge | link quality |
| Gate node uptime | `sensor` (diagnostic) | gate | |
| Link availability | via LWT | bridge | |
| Poll interval | `number` (config) | bridge | runtime-configurable from HA (§6.2) |

Exact field list from the MPPT is **all available VE.Direct fields**; the table
above is representative and finalized once we see live frames.

---

## 8. Power budget (gate node)

The FLA (group 24, ~70–85 Ah) + 50 W panel is generous for a single ESP-class
node — the node's overnight draw is on the order of ≤1 Ah, negligible against that
and trivial next to the gate motor. So the objective is disciplined minimization
as insurance for multi-day low-sun stretches, not a tight daily budget.

### 8.1 Where the current actually goes
The **radio is not the constraint.** Per the Semtech SX1262 datasheet, LoRa
125 kHz receive is **4.2 mA** (normal) or **5.3 mA** (Rx-boosted gain, +3 dB
sensitivity); TX is ~90 mA @ +14 dBm and ~118 mA @ +22 dBm; sleep with config
retained is sub-µA. Continuous RX therefore adds only ~5 mA on top of an
**ESP32-S3 that dominates** at tens of mA while awake. The floor is set by keeping
the MCU awake, and the reason it must stay awake is parsing the ~1 Hz VE.Direct
stream. That is precisely what the PV-aware scheme below targets.

### 8.2 Fixed levers (always on)
- WiFi + BLE disabled (largest ESP32 saving).
- OLED / Vext off in normal operation.
- Status caching minimizes LoRa TX (the peak consumer).

### 8.3 PV-aware adaptive profile
The node already reads the MPPT, so it uses PV output (panel power / charge state
from VE.Direct) to select a power profile, with **hysteresis** to avoid
dawn/dusk flapping (thresholds `TBD`, §13 D9):

| Profile | Trigger | MCU | VE.Direct | LoRa RX | Poll |
|---|---|---|---|---|---|
| **Daytime** | PV charging | awake | full ~1 Hz | near-continuous (fast) | normal |
| **Night/low-PV** | PV fallen off | light-sleep | occasional | SX1262 hardware RX duty-cycle | reduced |

### 8.4 Keeping night mode responsive
RX duty-cycling normally trades latency for power. We avoid that: the **bridge
sends commands with an extended preamble** long enough to span the gate radio's
sleep window, so the sleeping receiver still detects the preamble on its next
wake. Worst-case command latency ≈ one duty-cycle period, independent of sleep
depth — so night mode is low-power **and** responsive when a vehicle arrives after
dark (the case that matters). Preamble length is tuned to the chosen duty-cycle
period (§13 D2).

**Target:** night-mode worst-case command latency **≤ 2 s**. The night RX
duty-cycle period is therefore set at ≈2 s (with margin), and the bridge's command
preamble is sized to ≥ that period so a command is always caught within one cycle.
Daytime, with near-continuous RX, is effectively sub-second.

### 8.5 Budget table (to be measured on the bench — placeholders)
| State | Current (mA) | Notes |
|---|---:|---|
| ESP32-S3 active, radios managed | `TBD` | dominant term |
| SX1262 RX (normal / boosted) | 4.2 / 5.3 | datasheet typ. |
| SX1262 TX burst (+14 / +22 dBm) | ~90 / ~118 | rare |
| ESP32-S3 light sleep (night) | `TBD` | with VE.Direct/RX wake |
| **Average — daytime / night** | `TBD` / `TBD` | → mAh/day each |

Fill from bench measurement; compare daytime vs. night averages against FLA
capacity and worst-case (winter / shaded) harvest.

---

## 9. Debug and bench tooling (requirement)

- **BusT4 sniff/monitor mode:** a read-only diagnostic that captures and logs raw
  BusT4 frames (to serial, and on demand forwarded over LoRa → MQTT for
  house-side capture). Run it while operating the gate by remote, by loop, and by
  Oview to characterize what the 1050 actually emits — the primary tool for
  pinning down loop-detector state and movement-cause encoding (feeds §13 D3).
  Kept as a permanent diagnostic, not just a bring-up step.
- **Packet loopback:** (a) RF loopback — a node echoes received frames to validate
  link + framing without the peer; (b) wired/internal loopback — feed TX frames
  back into the RX parser with no radio, for pure protocol testing.
- **Dummy status push:** synthetic VE.Direct and 1050 status frames emitted to
  exercise the full pipeline (gate → LoRa → bridge → MQTT → HA) without the real
  hardware attached.
- **MQTT as bench harness:** `mosquitto_sub -t '#'` to watch every decoded payload
  live; `mosquitto_pub` to inject commands or fake status, decoupled from HA and
  the RF link. (Primary reason MQTT was chosen over ESPHome for the bridge.)
- **Device simulators:** a "dummy 1050" firmware/mode that answers BusT4 commands
  and emits events, so command logic is exercised on the bench **without risking
  the real board**; likewise a VE.Direct frame generator.
- **On-demand gate-node display:** enable OLED via button/serial to show state,
  RSSI, last frame. Trigger mechanism `TBD` (§13, D6).
- **Leveled serial logging** on both nodes.

---

## 10. Dev environment, repo, and build

**Suggested monorepo layout (GitHub):**
```
/firmware/gate/          # gate-node PlatformIO project
/firmware/bridge/        # bridge-node PlatformIO project
/lib/lora-protocol/      # shared framing/HMAC/CRC/message types
/lib/bust4/              # ported BusT4 protocol
/lib/vedirect/           # VE.Direct parser (osh-labs port)
/tools/                  # bench scripts, simulators, MQTT helpers
/ha/                     # example discovery payloads + automations
/docs/                   # this PRD and design notes
```
- **PlatformIO** multi-environment build (two targets), VS Code + Claude Code.
- **CI:** GitHub Actions building both firmwares on push.
- **Secrets:** LoRa shared key, WiFi creds, MQTT creds via untracked config /
  build flags (never committed).
- **Updates:** USB flashing only; document the procedure. No OTA.

---

## 11. Safety

- The **1050 remains the safety authority** — obstruction, photocells, and loop
  logic stay with the operator. GateLink issues commands; it must never be relied
  on to prevent unsafe motion.
- No automatic close behavior initiated by GateLink beyond explicit HA commands
  (any auto-close should be the operator's or HA's deliberate automation, clearly
  documented).
- BusT4 bring-up carries a hardware-destruction risk (§4.2): measure first,
  level-shift, and sniff read-only before sending.
- Fuse the FLA tap.

---

## 12. Reference implementations to port from

BusT4 protocol has been reverse-engineered by several projects (validated on
Nice Opera units such as Robus/Road/Walky; the 1050 is in the same BusT4/Opera
family). We port the framing/parsing, not the whole component. **Confirm each
project's license before reuse.**

- `pruwait/Nice_BusT4` (original) and its English fork `xdanik/Nice_BusT4`
- `makstech/esphome-BusT4`
- `bpietroiu/esphome-nice-bidiwifi` (position polling, device-specific quirks)
- `karol27/Nice_BusT4_WT32-ETH01` (documents wiring hazards, raw command frames)
- VE.Direct: `osh-labs/VE.Direct_mppt_arduino`

---

## 13. Open decisions register

| # | Decision | Status / notes | Resolve by |
|---|---|---|---|
| D1 | LoRa PHY params (SF/BW/CR/TX power) | open — pick after range test at 500 ft | Phase 1 |
| D2 | RX duty-cycle period + preamble length | latency target **resolved: ≤ 2 s night mode** (§8.4); exact duty period/preamble/SF finalized against link margin at range test | Phase 1 |
| D3 | BusT4 status coverage (loops, movement cause) vs. GPIO fallback | resolved approach — add retained BusT4 sniff mode (§9) to characterize; GPIO fallback stays as plan B | Phase 2/4 |
| D4 | Poll scheduler location | resolved — bridge firmware, runtime-configurable via HA `number` (§6.2, §7) | done |
| D5 | MQTT client library (PubSubClient vs. async) | open | Next iter |
| D6 | Gate-node display trigger mechanism | open — button vs. serial vs. LoRa command | Dev |
| D7 | Confirm exact BusT4 VCC voltage / pinout on the actual 1050 jack | open — measure on unit | Before wiring |
| D8 | Entity modeling: `cover` vs. buttons+state sensor | open — leaning `cover` | Next iter |
| D9 | PV-aware profile thresholds + hysteresis (day↔night switch) | open — set from bench/field VE.Direct data | Phase 3/6 |
| D10 | Rx-boosted gain on/off (5.3 mA +3 dB vs 4.2 mA) | open — decide with link margin from range test | Phase 1 |

---

## 14. Test plan / bring-up phases

1. **RF link only** — two Heltecs; ping + loopback; RSSI/SNR at 500 ft.
2. **Protocol/framing** — using dummy-gate and VE.Direct simulators (no real HW).
3. **VE.Direct** — real MPPT 75/15 on the bench; verify full field parsing.
4. **BusT4** — measure the Oview jack, add level shifter, **sniff read-only**,
   then commands; validate state/event parsing.
5. **HA integration** — MQTT Discovery entities; command round-trip; availability.
6. **Field** — install, range/power soak, error-path validation.

---

## 15. Changelog
- **v0.2** — added BusT4 sniff/monitor as a retained diagnostic (D3); rewrote §8
  around SX1262 RX figures (4.2/5.3 mA) and a PV-aware day/night power profile
  with extended-preamble command wake to keep night mode responsive; poll
  scheduler fixed to bridge firmware and made runtime-configurable via an HA
  `number` entity (D4); added D9 (PV thresholds/hysteresis) and D10 (Rx-boost).
- **v0.1** — initial draft. Architecture settled: two custom Heltec V3 nodes,
  BusT4 (1050) + VE.Direct (MPPT 75/15) on the gate node, LoRa 915 MHz link,
  bridge as LoRa↔MQTT gateway to existing Mosquitto via MQTT Discovery. Open
  items captured in §13.
