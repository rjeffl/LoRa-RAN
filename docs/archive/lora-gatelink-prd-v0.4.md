# Product Requirements Document — LoRa GateLink

**Working name:** LoRa GateLink
**Version:** 0.4 (draft, for iteration)
**Status:** Architecture settled; BusT4 physical layer pending measurement (D13); PHY parameters and field-measured values open (§13)
**Last updated:** 2026-07-23

---

## 1. Overview

### 1.1 Purpose
Provide bidirectional communication between a Home Assistant (HA) instance and a
remote, solar-powered driveway gate operator, over a point-to-point LoRa link.
The system lets HA send commands (open, close, stop, etc.) to the gate, pull
full status from both the gate controller and the solar charge controller, and
detect and classify vehicle traffic through the gate.

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
|  Loop detect       |                             +----------+----------+
|  12V FLA -> USB-C  |                                        |
+--------------------+                                        v
                                                     +---------------------+
                                                     |   Home Assistant    |
                                                     | (Mosquitto broker,  |
                                                     |  MQTT Discovery)    |
                                                     +---------------------+
```

### 1.3 Physical installation context
The **1050 controller, MPPT 75/15, FLA battery, and gate node all share a single
controller enclosure.** The gate motors are mounted externally on the gate itself
and are driven through dedicated motor ports on the 1050 board — **no motor
resides inside the enclosure.**

This matters for three design decisions and is referenced from §4.3, §4.5, and
§11:
- All signal runs (BusT4, VE.Direct) are short — inches to a couple of feet.
- Motor current enters the enclosure only via the 1050's own supply and motor
  terminals.
- Cable capacitance on the level-shifted lines is minimal.

### 1.4 Naming conventions
| Thing | Value |
|---|---|
| Repo | `lora-gatelink` |
| Firmware targets | `gatelink-gate`, `gatelink-bridge` |
| MQTT topic root | `gatelink/` |
| HA device names | "GateLink Gate", "GateLink Bridge" |
| C++ namespace | `gatelink` |

---

## 2. Goals and non-goals

### 2.1 Goals
- Reliable command path: HA → gate (open / close / stop / step-by-step / partial
  open / others BusT4 exposes), with acknowledgement.
- Full status retrieval from the 1050 (state, movement cause, loop-detector
  inputs) and the MPPT 75/15 (all VE.Direct fields).
- **Vehicle detection and direction-of-travel classification** from the two
  existing inductive loops (§5.4).
- Lightweight, native-feeling HA integration (cover + sensor/binary_sensor/text
  entities via MQTT Discovery).
- Minimal gate-node power draw on the FLA battery.
- Built-in bench debug tooling (packet loopback, dummy status pushes, device
  simulators).
- Best-practice repo, build, and dev workflow (GitHub + VS Code + Claude Code).

### 2.2 Non-goals (v1)
- Strong cryptographic security / full replay & spoofing prevention. Basic
  command authentication only (§6.4).
- Over-the-air firmware updates. Updates are USB-only.
- Persisting a sequence counter across reboots.
- Controlling multiple gates or multiple charge controllers.
- **Multi-device BusT4.** The bus is point-to-point between `gatelink-gate` and
  the 1050 Oview port only. No Oview or other accessory shares the bus.
- **Gate position reporting** (percentage open). Discrete states only in v1 —
  §7.2.
- Replacing or modifying the 1050's own safety logic. §11.

---

## 3. System architecture

### 3.1 Nodes
| | Gate node | Bridge node |
|---|---|---|
| Board | Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) | Heltec WiFi LoRa 32 V3 |
| Firmware | Custom (power-optimized) | Custom (LoRa↔MQTT gateway) |
| Wired interfaces | BusT4 (1050), VE.Direct (MPPT), loop inputs | none functional |
| Wireless | LoRa only (WiFi/BLE **disabled**) | LoRa + WiFi (BLE disabled) |
| Power | 12 V FLA → 12 V→5 V USB-C adapter | USB-C (mains); no constraint |
| Display | Off in normal op; on-demand + auto in debug (§5.6) | May stay on |
| Location | Inside existing controller enclosure (§1.3) | House, near LAN |

### 3.2 Shared code
Both nodes link a common **LoRa protocol/packet library** (framing, message
types, HMAC, CRC, sequence handling) so the wire format is defined and tested
once. This shared library is also what the bench tools and simulators exercise.

### 3.3 Data flows
- **Command (HA → gate):** HA publishes to an MQTT command topic → bridge builds
  an authenticated LoRa command frame → gate node verifies + issues the BusT4
  command → gate node returns a command-ACK frame → bridge publishes result.
- **Status (gate → HA):** gate node caches the latest VE.Direct frame and 1050
  status; it transmits on (a) poll request, (b) gate-controller state change,
  (c) a VE.Direct critical error/alert, or (d) **a vehicle loop detection
  event** (§5.4). Any **non-zero VE.Direct charger error code** triggers a send.
- **Poll:** programmable poll scheduler (target **1–5 min**, configurable) on the
  bridge (§13, D4).

---

## 4. Hardware and BOM

### 4.1 Bill of materials (per system)
| Qty | Item | Notes |
|----:|------|-------|
| 2 | Heltec WiFi LoRa 32 V3 (US 915 MHz variant) | ESP32-S3, SX1262, 0.96" OLED |
| 2 | 915 MHz antennas | one per node |
| 1 | BusT4 breakout: 6P4C modular jack + pigtail | matches the 1050 "Oview" port |
| 1 | **BSS138 4-channel bidirectional level shifter module** | Adafruit #757 / SparkFun BOB-12009 or equivalent. **Required under all branches** — serves VE.Direct always, BusT4 under Branch A (§4.2.3) |
| 1 | VE.Direct cable / JST-PH 2.0 4-pin pigtail | to the MPPT VE.Direct port |
| 1 | 12 V → 5 V **USB-C** adapter (off-the-shelf) | powers gate node from FLA, §4.4 |
| 1 | USB-C cable, short | adapter → Heltec |
| *0–1* | *SN65HVD230 CAN transceiver module* | **Branch B only** (§4.2.4) — on hand; used only if BusT4 proves differential |
| — | Mounting, strain relief, inline fuse on FLA tap | inside existing enclosure |

No separate enclosure is required — the gate node mounts inside the existing
controller enclosure (§1.3).

> **Purchase note.** The BSS138 module is a **certain** purchase, not a
> conditional one: VE.Direct requires level shifting regardless of how the BusT4
> physical layer resolves. Only whether BusT4 also consumes two of its channels
> is conditional.

### 4.2 BusT4 electrical — **safety critical**

The 1050's "Oview" port is a **6P4C modular jack** (Nice calls it "RJ45 6/4" —
6 positions, 4 contacts; loosely the RJ11/RJ12 form factor). It carries **4
conductors**: data plus power.

**Documentation status.** The electrical layer is community-documented; no
reverse engineering is required. The `pruwait` → `xdanik` → `makstech` lineage
(§12) has characterized it, including a photographed connector pinout at
`github.com/xdanik/Nice_BusT4/blob/main/img/connector.jpg`. **Caveat:** that
photo documents the **10-pin** BusT4 header, not the 6P4C Oview jack. Same bus,
same signaling, different pin ordering — the electrical characterization
transfers, the pinout does not. §4.2.2 exists for that reason.

#### 4.2.1 Established electrical characteristics
- **VCC pin carries 24–28 V.** Not 12 V, not 5 V. Mis-wiring can instantly
  destroy the 1050 board. Highest-consequence pin in the project.
- **Data is 5 V logic.** The Heltec V3 (ESP32-S3) is 3.3 V and **not** 5 V
  tolerant. Signal conditioning is mandatory under every branch.
- **Signaling is a modified UART: 19200 baud, 8N1, with a UART break of
  519–590 µs preceding each burst.** This is a *firmware* requirement, not just a
  wiring note — a stock `HardwareSerial` configuration will not interoperate.
  The ESP32-S3 UART peripheral can generate and detect breaks, but requires
  explicit handling (§5.2).
- **Physical layer is unconfirmed for this jack.** Upstream sources describe both
  single-ended 5 V logic (with level shifters, which is what the working
  implementations use) and CAN transceivers on the physical layer in multi-device
  installations, with no CAN framing in either case. §4.2.2 discriminates; §4.2.3
  and §4.2.4 give the two resulting designs. Tracked as **D13**.

Because this install is **point-to-point** (§2.2), several multi-drop concerns
are permanently out of scope: bus arbitration, address routing for third
parties, termination-for-loading analysis, and collision backoff. Only the 1050
and the gate node ever drive the bus.

#### 4.2.2 Pin identification and physical-layer discrimination — **complete before first connection**

Perform and record before mating anything. One scope capture answers all three
questions.

1. **Find VCC.** Power the board, gate idle. Meter each pin against battery
   negative. **The pin reading 24–28 V is VCC.** Mark it. Do not touch it again.
2. **Count the data lines.** Scope the remaining pins against GND while
   operating the gate from a wall button or remote. Note whether activity appears
   on **one** pin (shared bidirectional) or **two** (separate TX/RX).
3. **Determine the physical layer.** From the same capture:

   | Observation | Physical layer | Design |
   |---|---|---|
   | Normal 0–5 V UART waveform referenced to GND | **Single-ended** | Branch A (§4.2.3) |
   | Two pins swinging oppositely around a ~2.5 V common mode; both idle near 2.5 V | **Differential** | Branch B (§4.2.4) |

4. **Assign direction.** The line carrying 19200-baud traffic when the gate is
   commanded externally is the 1050's TX → gate node RX. The remaining data line
   is the 1050's RX → gate node TX; confirm it idles high before driving it.
5. **Record** the result below, with a photograph of the jack, and commit both to
   `/docs`. This becomes the authoritative reference for this install.

| 6P4C position | Signal | Measured | GateLink connection |
|---|---|---|---|
| 1 | `TBM` | | |
| 2 | `TBM` | | |
| 3 | `TBM` | | |
| 4 | `TBM` | | |

**Physical layer:** `TBM` — single-ended / differential
**Data lines:** `TBM` — one (shared) / two (separate)

#### 4.2.3 Branch A — single-ended (expected; primary design)

Two channels of the **BSS138 4-channel level shifter module** (§4.5). HV rail
5 V, LV rail 3.3 V, both sourced from the Heltec (§4.5.2).

- Channel 1: 1050 TX → gate node RX
- Channel 2: gate node TX → 1050 RX

Suitability at this bit rate is analyzed in §4.5.3. Relevant properties:
- Idle-high at rest on both sides, matching UART idle. If the Heltec is
  unpowered while the 1050 is live, the HV pull-up holds the line idle — no
  spurious framing.
- Sustained lows pass unimpeded, so the 519–590 µs break transmits intact.
- Open-drain topology means contention is non-destructive. **If step 2 of §4.2.2
  finds a single shared data line, this is a material advantage** — a push-pull
  buffer would require an output-enable GPIO to avoid fighting the 1050, and the
  BSS138 arrangement does not.

#### 4.2.4 Branch B — differential (alternative; hardware on hand)

Retained as a fully specified alternative pending D13. Uses the **SN65HVD230**
module already in inventory. Do **not** substitute the SN65HVD231 — it disables
the receiver in standby, which breaks §4.2.4.2.

##### 4.2.4.1 Why this part specifically
The SN65HVD230 is a **3.3 V supply, 3.3 V logic** transceiver. Its D and R pins
connect directly to the Heltec UART with no level shifting at all. Common 5 V
transceivers (MCP2551, TJA1050) would *reintroduce* the level-shifting problem
and must not be used here.

##### 4.2.4.2 RS pin must be under GPIO control — not a fixed jumper
Supply current is the dominant consideration. The part draws roughly **6–10 mA
typical in normal mode** (higher while driving dominant). Against the §8 gate
node budget of ~45 mA daytime and ~15 mA night, leaving it continuously enabled
would increase the **night** figure by 50–70% — the largest single change to the
node's own consumption in the whole design.

The SN65HVD230 enters a low-current standby when a high logic level is applied
to RS, in which **the driver is switched off but the receiver remains active**,
exiting standby when a dominant state appears on the bus. That is exactly the
required behavior.

- **Wire RS to a GPIO.** Assert standby whenever not actively transmitting, and
  throughout the night profile (§8.7).
- Firmware responsibility recorded in §5.2.
- Power line item recorded in §8.2.

##### 4.2.4.3 Slope control
If RS is instead used for slope control: a 10 kΩ resistor to ground gives ~100 ns
loop delay and 100 kΩ gives ~500 ns. Bit time at 19200 baud is ~52 µs, so even
the slowest setting costs ~1% of a bit. **Prefer the slowest available slope**
for EMI margin. Note this conflicts with GPIO standby control on a single pin —
if both are wanted, the GPIO drive can be sequenced through a resistor, or
standby prioritized. **Standby takes priority** given §4.2.4.2.

##### 4.2.4.4 Dominant timeout — checked, appears clear
TXD dominant timeout would truncate the 519–590 µs break and silently break all
transmission. This feature belongs to the later SN65HVD25x parts (255/257), whose
block diagrams include a dedicated DTO stage. The SN65HVD230/231/232 datasheet
documents only normal and standby/sleep modes selected via RS, with no DTO in the
mode tables. **Confirm against the datasheet timing table before relying on it**,
but the 230 appears safe for sustained dominant.

##### 4.2.4.5 Onboard termination
Common VP230 breakout modules carry a 120 Ω resistor across CANH/CANL. In CAN the
termination resistors, not the driver, establish the recessive state — so at this
speed and distance termination is about biasing, not reflections. If the 1050 end
is already terminated, the module's resistor gives the standard two-end
arrangement and is correct. If not, it may be loading the bus. **Identify it on
the board and be prepared to lift it.**

#### 4.2.5 Power domain rule
**Do not** power the gate node from BusT4 VCC. The node is powered from the FLA
via the 12 V→USB-C adapter (§4.4); domains stay separate, sharing only the GND
reference required by the conditioning circuit. §13, D7 — resolved.

### 4.3 VE.Direct electrical

> **Correction retained from v0.3:** earlier drafts stated VE.Direct is 3.3 V TTL
> requiring no level shifting. **This is incorrect.** All Victron MPPTs are 5 V
> devices.

VE.Direct is a **5 V TTL UART, 19200 baud**, on a **JST-PH 2.0 4-pin** connector.
Pinout is vendor-documented, with signal names given from the **device's**
perspective:

| Pin | Signal (device POV) | GateLink connection |
|---|---|---|
| 1 | GND | Gate node GND |
| 2 | RX (into MPPT) | BSS138 ch 4 → gate node TX — optional, see below |
| 3 | TX (out of MPPT) | BSS138 ch 3 → gate node RX — **required** |
| 4 | V+ | **Do not connect** |

Implementation notes:

- **Conditioning: BSS138 channels 3 and 4** (§4.5). Level shifting is required
  here under **every** BusT4 branch.
- **Galvanic isolation is not required.** Earlier drafts specified an ADUM1201
  on this interface to guard against ground offset under motor load. Given §1.3 —
  no motor inside the enclosure, all devices sharing one box, short heavy battery
  leads — the worst-case offset is on the order of **16 mV** (2 ft of 12 AWG at
  5 A) to ~40 mV for lighter wire. Against a 5 V logic threshold this is noise.
  **D12 resolved: BSS138, no isolator.** Grounding discipline in §4.5.4 is
  retained as hygiene.
- **MPPT RX is optional.** The MPPT emits its text-protocol frames unsolicited at
  ~1 Hz, so pin 2 may be left unconnected; the cost is losing HEX-mode queries
  and advanced fields. Since the requirement is *all* available fields, wire it —
  but keep unwired as a documented fallback if the port misbehaves.
- **Wire colors are actively misleading.** VE.Direct cables are crossover cables;
  red may be GND and black may be V+, and the two data conductors differ in
  meaning between the cable's ends. **Meter every conductor and confirm which pin
  it lands on.** Buying a genuine Victron cable and cutting it is the easiest
  sourcing path and makes this check mandatory.
- Community sources disagree about whether pin 4 is V+ or GND on some units. Moot
  since pin 4 is unconnected, but a further argument for metering first.

### 4.4 Gate-node power source

FLA (12 V) → inline fuse → **off-the-shelf 12 V-to-USB-C adapter** → USB-C into
the Heltec. An automotive/marine adapter is acceptable and preferred over a
custom buck converter. Spec by requirement so it stays sourceable:

| Requirement | Value | Rationale |
|---|---|---|
| Input range | 9–16 V | tolerate FLA float ~14.4 V and equalize excursions |
| Output | 5 V @ ≥2 A | LoRa TX bursts are short but sharp |
| **Quiescent draw, no load** | **≤ 5 mA** | see note |
| Termination | screw or ring terminal | not a cigarette plug |

**The quiescent figure is the one that matters.** Many inexpensive adapters idle
at 15–20 mA, a meaningful fraction of the gate node's night budget. **Measure the
actual no-load draw of the selected part before committing it** — a two-minute
measurement that protects the whole §8 budget.

### 4.5 Signal conditioning — consolidated

#### 4.5.1 Channel allocation
A single BSS138 module serves both interfaces because **BusT4 and VE.Direct are
both 5 V** — one HV rail covers all four channels.

| Channel | Under Branch A | Under Branch B |
|---|---|---|
| 1 | BusT4: 1050 TX → node RX | *spare* |
| 2 | BusT4: node TX → 1050 RX | *spare* |
| 3 | VE.Direct: MPPT TX → node RX | VE.Direct: MPPT TX → node RX |
| 4 | VE.Direct: node TX → MPPT RX | VE.Direct: node TX → MPPT RX |

Under Branch B, BusT4 moves to the SN65HVD230 and channels 1–2 are unused.

#### 4.5.2 Rail sourcing — note this is non-obvious
Both device connectors have their power pins **deliberately unconnected** —
BusT4 VCC is 24–28 V (§4.2.5) and VE.Direct pin 4 is unused (§4.3). Therefore:

- **HV rail (5 V):** from the **Heltec's 5 V pin**, derived from the 12 V→USB-C
  adapter.
- **LV rail (3.3 V):** from the Heltec's 3.3 V pin.

This is standard practice and cleaner than tapping device power, since it
preserves the domain separation §4.2.5 requires.

#### 4.5.3 Suitability analysis at 19200 baud
The BSS138 topology is open-drain with passive 10 kΩ pull-ups, so rise time is
RC-limited rather than actively driven. This is the basis of the conventional
advice against these modules for UART — advice aimed at high-speed links.

| Condition | Capacitance | Rise time (2.2·RC) | % of bit period |
|---|---|---|---|
| Module + short trace *(this install, §1.3)* | ~20 pF | ~440 ns | 0.85% |
| Plus ~3 ft of cable *(not applicable here)* | ~150 pF | ~3.3 µs | 6.3% |

Bit period at 19200 baud is **52 µs**. With all devices in one enclosure the
relevant row is the first: three orders of magnitude of headroom. Falling edges
are actively driven through the MOSFET and are fast in both cases.

**Contingency (unlikely to be needed):** if a far-end rising edge looks lazy on
the scope during bring-up, parallel additional pull-ups on the HV side to bring
the effective resistance to 2.2–4.7 kΩ. The module's 10 kΩ values are chosen for
I²C bus-loading rules that do not constrain this application. Two-resistor fix,
not a redesign.

#### 4.5.4 Grounding discipline
Retained as good practice rather than as mitigation (§4.3):

- Bring the gate node ground and the MPPT signal ground to battery negative at a
  **single common point**.
- Keep the **1050's motor-terminal wiring** physically separated from signal
  grounds, so the thin signal grounds never become a parallel path for motor
  return current.

---

## 5. Firmware — gate node (custom)

### 5.1 Framework & libraries
- Build: **PlatformIO**, ESP32-S3 target.
- LoRa: **RadioLib** driving the SX1262.
- BusT4: **ported** from the existing C++ reverse-engineering work (§12) —
  extract frame construction/parsing (Open/Close/Stop/SBS, GET/SET info
  requests) and drop the ESPHome/WiFi scaffolding. **See §12.2 for the licensing
  consequence.**
- VE.Direct: use/port **osh-labs/VE.Direct_mppt_arduino** for text-protocol
  parsing.

### 5.2 Responsibilities
1. **BusT4 client** — send commands; poll status via GET/INF requests; parse
   asynchronous events. Must implement the 519–590 µs pre-burst UART break
   (§4.2.1). Target fields: gate state, **movement cause** if exposed, and
   **loop-detector inputs** if exposed (§5.4). No arbitration or address routing
   required (point-to-point, §4.2.1).
2. **VE.Direct reader** — continuously parse the ~1 Hz text frames; cache the
   latest complete snapshot; watch the charger error field.
3. **LoRa endpoint** — receive commands, transmit status/ACK/event frames.
4. **Trigger/cache logic** — transmit on poll, gate state change, VE.Direct
   critical error, or vehicle detection event (§5.4.3).
5. **Loop detection and direction classification** — §5.4.
6. **Command authentication** — verify HMAC + sequence before acting (§6.4).
7. **Power management** — WiFi and BLE **off**; Vext / OLED **off** in normal
   operation; SX1262 RX duty-cycling as the primary power lever (§8).
   **Branch B only:** drive the SN65HVD230 RS pin to standby whenever not
   transmitting, and for the duration of the night profile (§4.2.4.2).
8. **Debug** — display control (§5.6), packet loopback, dummy status push, loop
   event injection, leveled serial logging (§9).

### 5.3 Peripheral power control
Explicitly disable WiFi and BLE radios at boot; gate Vext so the OLED and its
rail are off unless a display trigger enables them (§5.6).

### 5.4 Vehicle loop detection — **REQUIRED**

The installation uses a **Reno A&E BX-LP** two-channel loop detector driving
**two of the three** available dry-contact loop inputs on the 1050. One loop is
**inside** the gate, one **outside**. Detector and loops are existing, working
installed hardware.

**Purpose:** detect a vehicle entering or leaving while the gate is open, raise
an alert, and determine direction of travel.

#### 5.4.1 Requirements
- **5.4.1** The gate node SHALL determine the instantaneous detect state of both
  loops and represent each as a discrete boolean in the status payload.
- **5.4.2** The gate node SHALL derive direction of travel from the temporal
  ordering of loop assertions:

  | First asserted | Then | Classification |
  |---|---|---|
  | outside | inside | `ENTRY` |
  | inside | outside | `EXIT` |
  | either | *(no second within window)* | `UNDETERMINED` |
  | both simultaneously | — | `UNDETERMINED` |

- **5.4.3** A vehicle detection event SHALL trigger an **immediate unsolicited
  status push**, independent of the poll schedule.
- **5.4.4** When a detection event occurs while the gate is anything other than
  fully closed, the gate node SHALL set an **alert flag** in the pushed status.
- **5.4.5** The acquisition path (BusT4 status field vs. direct GPIO) is an
  implementation detail and SHALL NOT be visible in the LoRa protocol or the HA
  entity model.

#### 5.4.2 Configuration
| Parameter | Default | Meaning |
|---|---|---|
| `loop_direction_window_ms` | 5000 | Max gap between first and second assertion for the pair to count as one traversal |
| `loop_debounce_ms` | 50 | Debounce on each loop input |

#### 5.4.3 Acquisition path — BusT4 vs GPIO
5.4.5 decouples this from the protocol so it can be decided from sniff data
(§13, D3) without blocking protocol work.

**BusT4 path.** The `makstech` component issues an `INF_IO` request to read the
controller's I/O state, using it to confirm limit switches — strong evidence the
1050 reports input states over the bus, with loop inputs plausibly in the same
structure. Zero added wiring if it works.

**GPIO path.** Deterministic and simple, but the BX-LP's dry contacts are already
committed to the 1050's inputs. Options: parallel the existing contacts
(electrically fine, but **confirm the 1050's input is not a current-sourcing
scheme** that would fight an ESP32 pull-up), or use a spare/auxiliary output on
the BX-LP. **Check the BX-LP datasheet for spare or open-collector outputs before
assuming a tap is required.**

**Timing asymmetry — a real point in GPIO's favor.** The loops are within a car
length or two, so assertion gaps may be 0.5–3 s and a long vehicle may assert
both at once. On the BusT4 path the `INF_IO` poll rate sets ordering resolution —
a 15 s refresh misses ordering entirely, requiring fast local polling
(250–500 ms) whenever either loop is active or the gate is not closed. **GPIO
with interrupts sidesteps this completely.** Weigh this against the wiring cost
rather than treating BusT4 as automatically preferable.

### 5.5 Movement cause
Where BusT4 exposes it, capture the reason for gate movement (loop vs. remote vs.
Oview vs. wall button) and publish as a text sensor. `TBD` pending sniff results
(§13, D3). Independent of §5.4 — loop detection is required regardless.

### 5.6 Display control — resolved (D6)

Display **off by default in all normal operating modes**, with two independent
activation paths:

**Manual.** The onboard user button (GPIO0) toggles the display. On activation it
shows a status page and starts an inactivity timer (`display_timeout_s`, default
60); on expiry the display and Vext rail power down. A press while on cancels the
timer and powers down immediately.

> GPIO0 is also the boot-strap pin. Use a press-duration check to distinguish a
> UI press from a boot event, and do not sample it until well after reset.

**Automatic.** The display SHALL power on and remain on, **ignoring the
inactivity timer**, whenever the node is in any debug mode (BusT4 sniff, packet
loopback, simulated status push, loop event injection, or any future mode
registered as a debug mode). It returns to manual/timeout behavior when the last
debug mode exits.

**Sub-items for development:**
- **Multi-page cycling.** One page will not fit gate state + loop state + MPPT +
  link quality. Proposal: short-press = next page, long-press = off.
- **Vext sequencing.** Repeatedly powering the OLED rail down and up requires a
  settle delay and a **full SSD1306 re-init** on each power-up, not merely a wake
  command. Getting this wrong produces a display that works exactly once.

---

## 6. Firmware — bridge node (custom) & LoRa protocol

### 6.1 Framework & libraries — MQTT client resolved (D5)
PlatformIO, ESP32-S3. RadioLib (SX1262). WiFi. Links the same shared LoRa
protocol library as the gate node.

**Define a thin `MqttTransport` interface; implement first against
PubSubClient.** The decision is low-stakes *if* abstracted and high-friction if
not. Bridge traffic is trivial — one poll every 1–5 minutes plus occasional
commands and events — so throughput and QoS 2 are irrelevant. What matters is
reliable reconnect, LWT, and publishing discovery-config JSON.

| Option | License | Assessment |
|---|---|---|
| **PubSubClient** | MIT | Tiny, synchronous, extremely stable, ubiquitous. **Gotcha:** default max packet 256 bytes — Discovery configs exceed this and fail confusingly. Fix with `MQTT_MAX_PACKET_SIZE` ≥1024. |
| **espMqttClient** | MIT | Actively maintained, sync and async variants, large payloads, QoS 0/1/2. Best modern choice on merit; designated fallback. |
| **AsyncMqttClient** | MIT | Effectively unmaintained; AsyncTCP callbacks run in a separate task with real constraints. Avoid for new work. |
| **esp-mqtt** (IDF native) | Apache-2.0 | Most robust reconnect/TLS, but pulls the design toward IDF framework. Reserve for later migration. |

A blocking publish could in principle overlap a LoRa RX window, but the SX1262
buffers received frames and raises an interrupt, so brief blocking is tolerable.
If soak testing shows dropped frames, the abstraction makes the swap contained.

### 6.2 Responsibilities
1. LoRa RX → decode → publish to MQTT **state** topics.
2. MQTT **command** topic subscribe → build authenticated LoRa command → TX →
   await ACK → publish result; retry with backoff.
3. Publish **MQTT Discovery** config on boot and reconnect.
4. **Availability** via MQTT Last-Will-and-Testament.
5. **Poll scheduler**, runtime-configurable via an HA `number` entity.
6. Publish link diagnostics (RSSI/SNR, last-seen).
7. Republish vehicle detection events as HA events (§7.3).
8. Debug: loopback mode, dummy-status publish.

### 6.3 LoRa link parameters
- Band: **US 915 MHz**, point-to-point (not LoRaWAN — no TTN duty cycle, but
  respect FCC Part 15 limits).
- SF / BW / CR / TX power: **TBD** after the range test (§13, D1). Starting point
  for ~150 m near-LOS: SF7–9, BW 125 kHz, CR 4/5, moderate TX power, private sync
  word.

### 6.4 Message format & authentication
```
| ver | msg_type | src | dst | seq | payload... | MAC(4-8B, cmd only) | CRC |
```
- **Message types:** `COMMAND`, `COMMAND_ACK`, `POLL`, `STATUS`, `EVENT`,
  `ERROR`, `PING/LOOPBACK`.
- **Command auth:** truncated **HMAC-SHA256** over `(boot_id, seq, command)`
  using a shared key. Status/poll frames unauthenticated per requirements.
- **Sequence / replay:** monotonic counter, no cross-reboot persistence. Gate
  node picks a random `boot_id` at boot; bridge accepts increasing `seq` within a
  `boot_id` and resyncs on a new one.
- **Reliability:** commands ACKed (receipt + execution result); bridge retries
  with backoff. Status fire-and-forget except poll responses and vehicle events.

---

## 7. Home Assistant integration (MQTT Discovery)

### 7.1 Entity model — resolved (D8): `cover` **plus** auxiliary entities

`cover` is correct as the primary control surface and wrong as the *only*
surface, because the 1050 has states and commands the cover model cannot express.

**Use `cover` (device_class: `gate`) as primary** — native open/close/stop UI,
voice-assistant support, dashboard cards, standard `cover.*` services. Drive from
real BusT4 state with `optimistic: false`; do not use assumed state.

**Auxiliary entities for what `cover` cannot represent:**

| Need | Why `cover` can't | Entity |
|---|---|---|
| Step-by-step (SBS) | no equivalent service | `button` |
| Partial open 1/2/3 | no partial-preset concept | `button` ×3 |
| BusT4 block/release | not a cover concept | `lock` |
| Raw 1050 state incl. blocked/error | cover flattens to 5 states | `sensor` (diagnostic) |
| Loop state, direction, movement cause | outside the model | `binary_sensor` / `sensor` |

### 7.2 Position reporting — deferred to v2
Upstream components estimate position by timing movements. **v1 will not report
position.** Time-based estimation on a gate that can be stopped, reversed, or
obstructed produces confident wrong answers, and a cover that lies about being
40% open is worse than one reporting discrete states. Revisit if the 1050 proves
to expose encoder position directly.

### 7.3 Entity table
Topic hierarchy `gatelink/<node>/...`, retained discovery configs, availability
topic.

| Entity | Type | Source | Notes |
|---|---|---|---|
| Gate | `cover` (device_class: gate) | 1050 | open/close/stop; no position (§7.2) |
| Step-by-step | `button` | 1050 | SBS command |
| Partial open 1/2/3 | `button` ×3 | 1050 | if exposed |
| Gate lock | `lock` | 1050 | BusT4 block/release |
| Gate raw state | `sensor` (text, diagnostic) | 1050 | full state incl. blocked/error |
| Movement cause | `sensor` (text) | 1050 | if exposed (§5.5) |
| Loop — inside | `binary_sensor` (occupancy) | 1050 or GPIO | §5.4 |
| Loop — outside | `binary_sensor` (occupancy) | 1050 or GPIO | §5.4 |
| Last traversal direction | `sensor` (text) | gate | `ENTRY`/`EXIT`/`UNDETERMINED` |
| Vehicle detected while open | `binary_sensor` / event | gate | alert per §5.4.4 |
| Battery voltage | `sensor` (V) | MPPT | |
| Battery current | `sensor` (A) | MPPT | |
| Panel voltage / power | `sensor` (V / W) | MPPT | |
| Yield today | `sensor` (kWh) | MPPT | |
| Charge state | `sensor` (text) | MPPT | bulk/absorption/float/off |
| Charger error | `sensor` (text/code) | MPPT | non-zero → alert + push |
| LoRa RSSI / SNR | `sensor` (diagnostic) | bridge | |
| Gate node uptime | `sensor` (diagnostic) | gate | |
| Link availability | via LWT | bridge | |
| Poll interval | `number` (config) | bridge | runtime-configurable |

Exact MPPT field list is **all available VE.Direct fields**; the table is
representative, finalized once live frames are seen.

---

## 8. Power budget (gate side)

### 8.1 Source
Group 24 deep-cycle FLA, **75 Ah nominal**. Budget against **50% usable ≈ 37 Ah**
to preserve cycle life — FLA rated capacity is not usable capacity. 50 W panel
via MPPT 75/15.

### 8.2 Load inventory
**TBM** = To Be Measured; placeholders are conservative and should be replaced
with meter readings at bring-up.

| Load | Current @ 12 V | Duty | Ah/day |
|---|---:|---|---:|
| 2 × 2 W LED lights (night only) | 0.33 A | 12 h | 4.00 |
| 1050 controller static | 0.05 A **TBM** | 24 h | 1.20 |
| Reno A&E BX-LP loop detector | 0.04 A **TBM** | 24 h | 0.96 |
| Gate node — daytime profile | 0.045 A **TBM** | 12 h | 0.54 |
| Gate node — night profile | 0.015 A **TBM** | 12 h | 0.18 |
| MPPT 75/15 self-consumption | 0.010 A | 24 h | 0.24 |
| Gate motors while operating | ~5 A **TBM** | 2 cycles × ~20 s | 0.06 |
| 12 V→USB-C adapter overhead | 0.005 A **TBM** | 24 h | 0.12 |
| *SN65HVD230, normal mode* — **Branch B only** | *~0.008 A* | *TX only, standby otherwise* | *~0.02* |
| *SN65HVD230, if left enabled* — **Branch B failure mode** | *~0.008 A* | *24 h* | *~0.19* |
| **Total (Branch A)** | | | **≈ 7.3 Ah/day** |

Note the BSS138 module adds negligible current — its pull-ups draw only while a
line is held low, averaging well under 1 mA at UART duty cycles.

**Harvest:** 50 W × ~3.0 winter peak-sun-hours (western NC, non-optimal tilt) ×
0.85 system efficiency ≈ 127 Wh ≈ **10.6 Ah/day**. Summer roughly doubles this.

### 8.3 Findings — these should shape the design
1. **The LEDs are the budget.** At ~4 Ah/day they are ~55% of consumption, more
   than every other load combined. Real headroom comes from LED runtime, not
   firmware.
2. **The gate node is ~10% of the budget, and the night profile saves ~0.36
   Ah/day — about 5%.** Keep the adaptive scheme; it is designed and cheap. But
   **do not accept architectural complexity or sacrifice the 2 s night latency
   target** for further savings that vanish into the LED load's noise.
3. **Winter margin is thin.** 10.6 Ah in vs 7.3 Ah out is a 1.45× ratio. Two
   consecutive overcast days erase the surplus. Autonomy from full at zero
   harvest is ~5 days.
4. **The system already works** — the strongest available data point. The
   existing install has run for years on this battery without the gate node.
   GateLink adds ~0.7 Ah/day, roughly 10%. Frame this section as *"characterize
   the existing margin, then confirm GateLink fits inside it."*

### 8.4 Validation task — do this before install
Log the MPPT's own yield history (`H19`/`H20`/`H21`) and battery voltage minima
for one week via VE.Direct. That yields the *actual* present margin for free, and
once GateLink is live the same telemetry becomes ongoing early warning of battery
ageing. The system being built is also the instrument that validates it.

### 8.5 Where the current goes on the node
The **radio is not the constraint.** Per the SX1262 datasheet, LoRa 125 kHz
receive is **4.2 mA** (normal) or **5.3 mA** (Rx-boosted, +3 dB); TX is ~90 mA
@ +14 dBm and ~118 mA @ +22 dBm; sleep with config retained is sub-µA. Continuous
RX adds only ~5 mA on top of an **ESP32-S3 that dominates** at tens of mA while
awake. The floor is set by keeping the MCU awake, and the reason it must stay
awake is parsing the ~1 Hz VE.Direct stream.

### 8.6 Fixed levers (always on)
- WiFi + BLE disabled (largest ESP32 saving).
- OLED / Vext off in normal operation (§5.6).
- Status caching minimizes LoRa TX (the peak consumer).
- **Branch B:** SN65HVD230 held in standby except while transmitting (§4.2.4.2).

### 8.7 PV-aware adaptive profile
The node reads the MPPT, so it uses PV output to select a power profile, with
**hysteresis** to avoid dawn/dusk flapping (thresholds `TBD`, §13 D9):

| Profile | Trigger | MCU | VE.Direct | LoRa RX | Poll |
|---|---|---|---|---|---|
| **Daytime** | PV charging | awake | full ~1 Hz | near-continuous | normal |
| **Night/low-PV** | PV fallen off | light-sleep | occasional | SX1262 hardware RX duty-cycle | reduced |

> Per §8.3, night mode buys ~5% of the daily budget. Worth having as insurance
> for multi-day low-sun stretches, but not the load-bearing element of the design.

### 8.8 Keeping night mode responsive
RX duty-cycling normally trades latency for power. We avoid that: the **bridge
sends commands with an extended preamble** long enough to span the gate radio's
sleep window, so the sleeping receiver detects the preamble on its next wake.
Worst-case latency ≈ one duty-cycle period, independent of sleep depth.

**Target:** night-mode worst-case command latency **≤ 2 s**. Night RX duty-cycle
period ≈2 s (with margin); the bridge's command preamble is sized to ≥ that
period. Daytime, with near-continuous RX, is sub-second.

### 8.9 Future: lithium upgrade
A future consideration, not a requirement. A 50 Ah LiFePO4 would give ~45 Ah
usable (vs 37) at half the weight with deeper cycling tolerance. **Two conditions
to carry forward:** the MPPT 75/15 charge profile must be reconfigured, and the
BMS **must** have low-temperature charge cutoff — sub-0 °C charging does occur
here. Revisit at battery replacement time.

---

## 9. Debug and bench tooling (requirement)

- **BusT4 sniff/monitor mode:** read-only diagnostic capturing raw BusT4 frames
  (to serial, and on demand forwarded over LoRa → MQTT for house-side capture).
  Run while operating the gate by remote, by loop, and by Oview to characterize
  what the 1050 emits — the primary tool for pinning down loop-detector state and
  movement-cause encoding (§13 D3). **Specifically: capture `INF_IO` responses
  with a vehicle on each loop and check whether bits move.** Retained as a
  permanent diagnostic.
- **Packet loopback:** (a) RF loopback — a node echoes received frames to
  validate link + framing without the peer; (b) wired/internal loopback — feed TX
  frames back into the RX parser with no radio.
- **Dummy status push:** synthetic VE.Direct and 1050 status frames exercising
  the full pipeline (gate → LoRa → bridge → MQTT → HA) without real hardware.
- **Loop event injection:** synthetic loop assertions in configurable order and
  spacing, to test §5.4 direction classification without driving a car back and
  forth.
- **MQTT as bench harness:** `mosquitto_sub -t '#'` to watch every decoded
  payload live; `mosquitto_pub` to inject commands or fake status, decoupled from
  HA and the RF link.
- **Device simulators:** a "dummy 1050" mode answering BusT4 commands and
  emitting events, so command logic is exercised **without risking the real
  board**; likewise a VE.Direct frame generator.
- **On-demand gate-node display:** §5.6.
- **Leveled serial logging** on both nodes.

---

## 10. Dev environment, repo, and build

```
/firmware/gate/          # gate-node PlatformIO project
/firmware/bridge/        # bridge-node PlatformIO project
/lib/lora-protocol/      # shared framing/HMAC/CRC/message types
/lib/bust4/              # ported BusT4 protocol  [GPL-3.0 — see §12]
/lib/vedirect/           # VE.Direct parser (osh-labs port)
/tools/                  # bench scripts, simulators, MQTT helpers
/ha/                     # example discovery payloads + automations
/docs/                   # this PRD, design notes, measured pinouts
LICENSE                  # see §12 / D11
THIRD_PARTY_NOTICES.md   # see §12
```
- **PlatformIO** multi-environment build (two targets), VS Code + Claude Code.
- **CI:** GitHub Actions building both firmwares on push.
- **Secrets:** LoRa shared key, WiFi creds, MQTT creds via untracked config /
  build flags (never committed).
- **Updates:** USB flashing only; document the procedure. No OTA.

---

## 11. Safety

- The **1050 remains the safety authority** — obstruction, photocells, and loop
  logic stay with the operator. GateLink issues commands and *observes* loop
  state; it must never be relied on to prevent unsafe motion. §5.4 is a
  monitoring feature, not a safety feature.
- No automatic close behavior initiated by GateLink beyond explicit HA commands.
- BusT4 bring-up carries hardware-destruction risk (§4.2): complete the pin
  identification procedure (§4.2.2), condition the lines per the selected branch,
  and sniff read-only before sending anything.
- The BusT4 VCC pin carries **24–28 V**. This is why §4.2.2 is a gate on the work
  rather than a suggestion.
- Fuse the FLA tap.
- Observe the grounding discipline in §4.5.4 — motor current enters the enclosure
  through the 1050's terminals and should never share a path with signal grounds.

---

## 12. Third-party code and licenses

### 12.1 Inventory
| Component | Source | License |
|---|---|---|
| Nice BusT4 protocol logic | `pruwait/Nice_BusT4`, `xdanik/Nice_BusT4`, `makstech/esphome-BusT4` | **GPL-3.0** |
| BusT4 wiring/quirk references | `karol27/Nice_BusT4_WT32-ETH01`, `bpietroiu/esphome-nice-bidiwifi` | GPL-3.0 (verify each) |
| VE.Direct parser | `osh-labs/VE.Direct_mppt_arduino` | **`TBD` — verify before use** |
| LoRa radio driver | RadioLib | MIT |
| Arduino-ESP32 core | Espressif | LGPL-2.1-or-later |
| ESP-IDF components | Espressif | Apache-2.0 |
| mbedTLS (command HMAC) | via ESP-IDF | Apache-2.0 |
| MQTT client | PubSubClient (§6.1) | MIT |
| JSON | ArduinoJson | MIT |
| Display | U8g2 | BSD-2-Clause |
| Display (alt) | Adafruit GFX / SSD1306 | BSD |
| NVS / Preferences | via ESP-IDF | Apache-2.0 |

### 12.2 The GPL-3.0 consequence — decision required (D11)
The **entire Nice BusT4 lineage is GPL-3.0.** Porting that protocol logic into
GateLink makes the gate firmware a derivative work, which **must** then be
distributed under GPL-3.0. Since the repo is public on GitHub, "distributed" is
true the moment it is pushed.

1. **License GateLink GPL-3.0 and move on. — recommended.** For a personal
   project this costs nothing real, honors the upstream authors' terms, and
   removes the question permanently.
2. **Isolate BusT4** in a separate GPL-3.0 repo/submodule, keeping the rest
   permissive. Defensible, but the combined gate binary is still GPL-3.0 — the
   only thing preserved is the license of the *LoRa protocol library* for reuse
   elsewhere.
3. **Clean-room** from protocol documentation only. More work, murkier, and
   disrespects the spirit of the upstream contribution. Not recommended.

### 12.3 Repo obligations
- `LICENSE` at root matching the D11 outcome.
- `THIRD_PARTY_NOTICES.md` listing §12.1 with copyright lines.
- Nice's own reference documents (TTPCI manual, DMBM integration protocol PDF)
  are Nice-copyrighted: **link them, do not vendor them.**

---

## 13. Open decisions register

| # | Decision | Status / notes | Resolve by |
|---|---|---|---|
| D1 | LoRa PHY params (SF/BW/CR/TX power) | open — pick after range test at 500 ft | Phase 1 |
| D2 | RX duty-cycle period + preamble length | latency target **resolved: ≤ 2 s**; exact duty period/preamble/SF finalized at range test | Phase 1 |
| D3 | BusT4 loop/movement-cause coverage vs. GPIO fallback | approach resolved — sniff mode characterizes; note the timing asymmetry favoring GPIO (§5.4.3) | Phase 2/4 |
| D4 | Poll scheduler location | **resolved** — bridge firmware, runtime-configurable via HA `number` | done |
| D5 | MQTT client library | **resolved** — `MqttTransport` abstraction, PubSubClient first, `MQTT_MAX_PACKET_SIZE` ≥1024; espMqttClient as fallback (§6.1) | done |
| D6 | Gate-node display trigger | **resolved** — button toggle + auto-on in any debug mode (§5.6) | done |
| D7 | BusT4 VCC handling | **resolved** — VCC not connected; TX/RX/GND only. Pin identification (§4.2.2) remains a hard prerequisite | done |
| D8 | Entity modeling | **resolved** — `cover` primary + auxiliary entities; position deferred to v2 (§7.1–7.2) | done |
| D9 | PV-aware profile thresholds + hysteresis | open — set from bench/field VE.Direct data | Phase 3/6 |
| D10 | Rx-boosted gain on/off (5.3 mA +3 dB vs 4.2 mA) | open — decide with link margin from range test | Phase 1 |
| D11 | **Project license** | **open — recommend GPL-3.0** (forced by BusT4 port, §12.2) | Before repo goes public |
| D12 | VE.Direct isolation vs. level shifting | **resolved** — BSS138 module, no isolator. Ground offset is ~16–40 mV given §1.3; grounding discipline §4.5.4 retained as hygiene | done |
| **D13** | **BusT4 physical layer: single-ended (Branch A) vs. differential (Branch B)** | **open — resolved by measurement, §4.2.2 step 3.** Branch A expected. Both designs fully specified (§4.2.3, §4.2.4); hardware for both on hand | Phase 4, before wiring |

### 13.1 Measurement backlog (tasks, not decisions)
| Item | Section | Blocks |
|---|---|---|
| 1050 Oview jack pinout | §4.2.2 step 1/4 | Phase 4 |
| Data line count (one shared vs. two) | §4.2.2 step 2 | Branch A design detail |
| **Physical layer: single-ended vs. differential** | §4.2.2 step 3 | **D13 — selects Branch A or B** |
| SN65HVD230 DTO confirmation (Branch B only) | §4.2.4.4 | Branch B viability |
| Module onboard 120 Ω termination (Branch B only) | §4.2.4.5 | Branch B wiring |
| 12 V→USB-C adapter no-load draw | §4.4 | §8 budget |
| 1050 static current | §8.2 | §8 budget |
| BX-LP loop detector current | §8.2 | §8 budget |
| BX-LP spare/aux output availability | §5.4.3 | D3 |
| Gate node day/night average current | §8.2 | §8 budget |
| One week MPPT yield + Vmin baseline | §8.4 | install go/no-go |

---

## 14. Test plan / bring-up phases

1. **RF link only** — two Heltecs; ping + loopback; RSSI/SNR at 500 ft.
2. **Protocol/framing** — dummy-gate and VE.Direct simulators; loop event
   injection to validate §5.4 direction logic.
3. **VE.Direct** — real MPPT 75/15 on the bench through BSS138 channels 3–4;
   verify full field parsing. Start the §8.4 one-week baseline log.
4. **BusT4** — complete §4.2.2 (pinout, line count, physical layer); **resolve
   D13**; wire the selected branch; **sniff read-only** (including `INF_IO` with
   a vehicle on each loop); then commands.
5. **HA integration** — MQTT Discovery entities; command round-trip;
   availability; loop/direction entities.
6. **Field** — install, range/power soak, error-path validation, confirm measured
   daily Ah against §8.2.

---

## 15. Changelog

- **v0.4** — Added §1.3 physical installation context: 1050, MPPT, battery, and
  gate node share one enclosure with **no motor inside**; motors are external via
  dedicated 1050 ports. **Resolved D12 on that basis** — worst-case ground offset
  is ~16–40 mV, so the ADUM1201 isolator is dropped; grounding discipline retained
  as hygiene (§4.5.4). Consolidated signal conditioning into new **§4.5** around a
  single **BSS138 4-channel level shifter module** serving BusT4 (2 ch) and
  VE.Direct (2 ch) on one shared 5 V HV rail, with rise-time analysis at 19200
  baud showing three orders of magnitude of headroom, explicit rail-sourcing from
  the Heltec (both device power pins being deliberately unconnected), and a
  pull-up contingency. Restructured §4.2 into **Branch A (single-ended, BSS138,
  expected)** and **Branch B (differential, SN65HVD230, hardware on hand)**, with
  Branch B fully documented: 3.3 V-native rationale, RS-pin standby under GPIO
  control as a power requirement, slope control, dominant-timeout finding, and
  onboard termination check. Expanded §4.2.2 into a three-question measurement
  that also discriminates physical layer and data-line count. Added **D13** for
  the branch selection and dropped multi-drop concerns as out of scope per the
  point-to-point constraint (§2.2). Added Branch B current lines to §8.2 and
  §8.6, and Branch B firmware duty to §5.2.
- **v0.3** — renamed to **LoRa GateLink**; added naming conventions.
  **Corrected §4.3: VE.Direct is 5 V, not 3.3 V.** Added documented BusT4
  electrical characteristics incl. 519–590 µs UART break and 24–28 V VCC, plus a
  pin identification procedure. Added VE.Direct pinout table and crossover-cable
  warning. Changed power to 12 V→5 V **USB-C** with an off-the-shelf adapter and a
  ≤5 mA quiescent requirement. **Promoted loop detection to a hard requirement**
  with direction-of-travel classification, event-triggered push, and open-gate
  alerting; added loop event injection to bench tooling. Expanded §8 to a full
  gate-side power budget — finding: LEDs are ~55% of load, gate node ~10%, night
  mode buys ~5%. Added pre-install validation and lithium notes. Added full
  license inventory and the GPL-3.0 consequence (D11). Resolved D5, D6, D7, D8.
  Added D12 and a measurement backlog.
- **v0.2** — added BusT4 sniff/monitor as a retained diagnostic (D3); rewrote §8
  around SX1262 RX figures and a PV-aware day/night power profile with
  extended-preamble command wake; poll scheduler fixed to bridge firmware and made
  runtime-configurable via an HA `number` entity (D4); added D9 and D10.
- **v0.1** — initial draft. Architecture settled: two custom Heltec V3 nodes,
  BusT4 (1050) + VE.Direct (MPPT 75/15) on the gate node, LoRa 915 MHz link,
  bridge as LoRa↔MQTT gateway to existing Mosquitto via MQTT Discovery.
