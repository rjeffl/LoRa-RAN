# Product Requirements Document — LoRa Remote Automation Network (LRAN)

**Working name:** LoRa Remote Automation Network — **LRAN**
**Node projects:** `GateLink` (gate), `WellLink` (well, future), `LoRaBridge` (house)
**Version:** 0.5 (draft, for iteration)
**Status:** Architecture settled; BusT4 physical layer pending measurement (D13); PHY parameters and field-measured values open (§13)
**Last updated:** 2026-08-06
**Supersedes:** v0.4 "LoRa GateLink"

---

## 1. Overview

### 1.1 Purpose

Provide a **property-wide point-to-multipoint LoRa network** linking a Home
Assistant (HA) instance to remote, low-power monitoring and control nodes that
are outside practical WiFi range.

The first and most complex node is **GateLink**: a remote, solar-powered
driveway gate operator. HA sends commands (open, close, stop, etc.) to the gate,
pulls full status from both the gate controller and the solar charge controller,
reads and writes charge-controller configuration, and detects and classifies
vehicle traffic through the gate.

A second node, **WellLink**, is planned (§1.6). The network is designed for it
from the outset rather than retrofitted later.

### 1.2 System summary

One house-side bridge and N remote nodes:

- **LoRaBridge** (house): a **general-purpose LoRa↔MQTT gateway**. Receives and
  sends LoRa to any registered node, connects to the LAN over WiFi, and bridges
  to the existing Mosquitto broker on the HA host. HA entities are created via
  **MQTT Discovery**, one HA device per node. Mains powered; no power constraint.
  Supports OTA update (§10).
- **GateLink** (remote, ~500 ft): interfaces to a Nice/Apollo **1050** control
  board via **BusT4** and to a Victron **MPPT 75/15** via **VE.Direct** (text +
  HEX). Reads the battery BMS over **BLE**. Runs custom firmware. Powered from a
  12 V **100 Ah LiFePO4** battery.
- **WellLink** (remote, ~500 ft, different bearing): well level monitoring.
  Scoped in §1.6, specified in a later revision.

```
                       +---------------------+
   +--------------+    |                     |
   |   GateLink   |<-->|                     |
   | Heltec V3    |    |                     |
   | BusT4 <->1050|    |     LoRaBridge      |        +-------------------+
   | VE.Direct    |    |     Heltec V3       |        |  Home Assistant   |
   |   <-> MPPT   |    |                     |        | (Mosquitto broker,|
   | BLE <-> BMS  |    |  LoRa 915 MHz  <--> |        |  MQTT Discovery)  |
   | 12V LiFePO4  |    |  WiFi -> LAN        |------->|                   |
   +--------------+    |  MQTT -> Mosquitto  |  MQTT  +-------------------+
                       |  OTA capable        |
   +--------------+    |                     |
   |   WellLink   |<-->|                     |
   |  (planned)   |    |                     |
   +--------------+    +---------------------+
```

### 1.3 Physical installation context (gate)

The **1050 controller, MPPT 75/15, LiFePO4 battery, and GateLink node all share a
single controller enclosure.** The gate motors are mounted externally on the gate
itself and are driven through dedicated motor ports on the 1050 board — **no
motor resides inside the enclosure.**

This matters for three design decisions and is referenced from §4.3, §4.5, and
§11:
- All signal runs (BusT4, VE.Direct) are short — inches to a couple of feet.
- Motor current enters the enclosure only via the 1050's own supply and motor
  terminals.
- Cable capacitance on the level-shifted lines is minimal.

It also places the BLE BMS within inches of the node antenna (§5.7).

### 1.4 Naming conventions

| Thing | Value |
|---|---|
| Repo | `lran` |
| Firmware targets | `lran-bridge`, `lran-gatelink`, `lran-welllink` (future) |
| MQTT topic root | `lran/` |
| Node topic form | `lran/<node>/...` — e.g. `lran/gatelink/...` |
| HA device names | "LoRa Bridge", "GateLink", "WellLink" |
| C++ namespace | `lran` |
| Node IDs | `0x00` bridge, `0x01` gatelink, `0x02` welllink, `0xFF` broadcast |

> **Migration note.** v0.4 used `gatelink/` as the MQTT root and `gatelink` as
> the namespace. Renaming is done **now**, before any HA entity history exists,
> because discovery `unique_id`s and recorder history are painful to migrate
> later. If a shorter root is preferred, the only requirement is that it is
> fleet-neutral rather than gate-specific.

### 1.5 What changed from v0.4 — orientation

Five changes drive most of this revision. Read these before the detail sections.

1. **The bridge is now general-purpose** (§3, §6). Point-to-point assumptions are
   gone; addressing, keying, media access, fragmentation, and per-node
   availability all change.
2. **The battery is 100 Ah LiFePO4, 100% usable** (§8). Autonomy roughly triples.
   The night/low-PV power profile is **dropped** as a consequence, along with RX
   duty-cycling and the extended-preamble wake scheme on GateLink.
3. **VE.Direct gains the bidirectional HEX protocol** (§4.3, §5.2, §6.6). The
   MPPT RX line becomes mandatory, and HEX **writes** become an authenticated
   path.
4. **Vehicle detection hardware is different than v0.4 described** (§5.4). A
   Diablo DSP-7LP drives a *single* combined safety contact; the exit sensor is a
   *separate* wand far inside the gate.
5. **BusT4 sniffing moves off the node** (§9). Sniffing is a laptop bench
   procedure. In exchange, GateLink streams raw BusT4 frames to HA as an opt-in
   diagnostic, with decoding split between node and bridge (§5.8).

### 1.6 WellLink scope (forward-looking)

Not specified in this revision, but the network must accommodate it:

| Attribute | Status |
|---|---|
| Distance / bearing | ~500 ft, different direction from GateLink |
| Power | **TBD** — mains is possible; battery/solar must remain viable |
| Function | Well level monitoring. Possible expansion later. |
| Reporting | Fixed-interval poll/push, **plus** an event push on rapid level change |
| Payload | Level + **battery status** — reserve packet space for both |
| Commands | None currently anticipated |

Two design consequences carried into this revision:

- The **RX duty-cycle and extended-preamble wake design is retained in Appendix
  A** rather than deleted, because WellLink may be battery powered even though
  GateLink no longer needs it.
- The status schema is **per-node and versioned** (§6.4), so WellLink can define
  its own payload without touching GateLink's.

---

## 2. Goals and non-goals

### 2.1 Goals

**Network**
- A single house-side bridge serving **multiple** independent remote nodes over
  one LoRa channel, with per-node addressing, keying, and availability.
- Adding a node requires reflashing only the bridge and the new node — never the
  existing nodes (§6.4 version tolerance).

**GateLink**
- Reliable command path: HA → gate (open / close / stop / step-by-step / partial
  open / others BusT4 exposes), with acknowledgement.
- Full status retrieval from the 1050 (state, movement cause, detector inputs)
  and the MPPT 75/15 (all VE.Direct fields).
- **Read/write access to all MPPT configuration and status registers** via the
  VE.Direct HEX protocol, with GateLink acting as transport only (§6.6).
- **Vehicle detection and direction-of-travel classification** from the existing
  safety-loop and exit-wand contacts (§5.4).
- **Alert on vehicle detection while the gate is statically open** (§5.4.5) —
  the primary operational requirement behind §5.4.
- Battery health and state of charge (§5.7).
- Raw BusT4 frame visibility in HA as an opt-in diagnostic (§5.8).

**Integration**
- Lightweight, native-feeling HA integration (cover + sensor / binary_sensor /
  button / lock / number / switch entities via MQTT Discovery).
- Built-in bench debug tooling (packet loopback, dummy status pushes, device
  simulators, detector event injection).
- Best-practice repo, build, and dev workflow (GitHub + VS Code + Claude Code).

### 2.2 Non-goals (v1)

- Strong cryptographic security / full replay & spoofing prevention. Command and
  MPPT-write authentication only (§6.5).
- **OTA for remote nodes.** The bridge supports OTA (§10); GateLink and WellLink
  are USB-only. Rationale: the bridge is on the LAN, mains powered, and
  recoverable by hand; a bricked remote node is a walk with a laptop.
- Persisting a sequence counter across reboots.
- Controlling **multiple gates** or multiple charge controllers. Multiple *nodes*
  are explicitly in scope; multiple gate operators are not.
- **Multi-device BusT4.** The bus is point-to-point between `lran-gatelink` and
  the 1050 Oview port only. No Oview or other accessory shares the bus.
- **Gate position reporting** (percentage open). Discrete states only in v1 —
  §7.2.
- Replacing or modifying the 1050's own safety logic. §11.
- Mesh or multi-hop routing. The topology is a star with the bridge at the
  centre; every node hears the bridge directly.

---

## 3. System architecture

### 3.1 Nodes

| | LoRaBridge | GateLink | WellLink (planned) |
|---|---|---|---|
| Board | Heltec WiFi LoRa 32 V3 | Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) | TBD |
| Node ID | `0x00` | `0x01` | `0x02` |
| Firmware | LoRa↔MQTT gateway + decoders | Custom (interfaces + transport) | TBD |
| Wired interfaces | none functional | BusT4 (1050), VE.Direct (MPPT) | level sensor |
| Wireless | LoRa + WiFi (BLE off) | LoRa + **BLE (duty-cycled)**; WiFi **off** | LoRa |
| Power | USB-C (mains) | 12 V **LiFePO4** → 12 V→5 V USB-C adapter | TBD |
| Update | **OTA + USB** | USB only | USB only |
| Display | May stay on | Off in normal op; on-demand + auto in debug (§5.6) | TBD |
| Location | House, near LAN | Inside existing controller enclosure (§1.3) | At the well |

### 3.2 Shared code

All nodes link a common **LoRa protocol/packet library** (framing, addressing,
HMAC, CRC, sequence handling, fragmentation) so the wire format is defined and
tested once. This shared library is the **fleet-wide contract** and is what the
bench tools and simulators exercise.

Because remote nodes have no OTA, this library carries a **compatibility
obligation** (§6.4.5): the bridge must accept protocol version *N* and *N−1*, so
a protocol revision can be rolled out node-by-node rather than in a single
flag-day flash of every device on the property.

### 3.3 Decode placement — resolved (D14)

**Hybrid.** Neither extreme is right:

| Layer | Runs on | Why |
|---|---|---|
| Trigger-relevant decode (gate state, detector inputs, direction classification, MPPT charger error) | **GateLink** | These drive local push triggers and time-ordered classification. A LoRa round trip per decision is not viable, and some protocol/action decisions are better made locally. |
| Full BusT4 status expansion, field naming, entity mapping, VE.Direct HEX register interpretation | **LoRaBridge** | The bridge is mains powered, on WiFi, OTA-capable and in the house. Reflashing the decoder must not require a walk to the gate. |
| Raw frame passthrough | GateLink → bridge | Opaque blobs, opt-in (§5.8). Lets parsing decisions be deferred until debug data exists. |

Consequence for licensing: `/lib/bust4/` links into **both** the gate and bridge
firmwares, so GPL-3.0 covers both (§12 — acceptable under D11).

### 3.4 Data flows

- **Command (HA → node):** HA publishes to an MQTT command topic → bridge builds
  an authenticated LoRa command frame addressed to the node → node verifies HMAC
  and sequence, issues the BusT4 command → node returns a command-ACK frame →
  bridge publishes result.
- **Status (node → HA):** each node caches its latest device state and transmits
  on (a) poll request, (b) a locally significant state change, or (c) an event.
  For GateLink: gate-controller state change, non-zero VE.Direct charger error
  code, or a **vehicle detection event** (§5.4).
- **MPPT config (HA → MPPT):** HA publishes a HEX request → bridge wraps it →
  GateLink transports it verbatim to the MPPT → response transported back →
  bridge publishes it. **Writes require HMAC and an armed write-enable switch**
  (§6.6).
- **Poll:** per-node poll scheduler on the bridge, each node's interval
  runtime-configurable via its own HA `number` entity (§6.3).

---

## 4. Hardware and BOM

### 4.1 Bill of materials

#### 4.1.1 System

| Qty | Item | Notes |
|----:|------|-------|
| 2+ | Heltec WiFi LoRa 32 V3 (US 915 MHz variant) | ESP32-S3, SX1262, 0.96" OLED. One per node plus the bridge. |
| 2+ | 915 MHz antennas | one per node |

#### 4.1.2 GateLink

| Qty | Item | Notes |
|----:|------|-------|
| 1 | BusT4 breakout: 6P4C modular jack + pigtail | matches the 1050 "Oview" port |
| 1 | **BSS138 4-channel bidirectional level shifter module** | Adafruit #757 / SparkFun BOB-12009 or equivalent. **Required under all branches** — serves VE.Direct always, BusT4 under Branch A (§4.2.3) |
| 1 | VE.Direct cable / JST-PH 2.0 4-pin pigtail | to the MPPT VE.Direct port. **Both data lines used** (§4.3) |
| 1 | 12 V → 5 V **USB-C** adapter (off-the-shelf) | powers GateLink from the battery, §4.4 |
| 1 | USB-C cable, short | adapter → Heltec |
| 1 | **12 V 100 Ah LiFePO4 battery** — WattCycle 100 Ah mini w/ Bluetooth | §8.1. Replaces the Group 24 FLA. |
| *0–1* | *SN65HVD230 CAN transceiver module* | **Branch B only** (§4.2.4) — on hand; used only if BusT4 proves differential |
| *0–1* | *Victron SmartShunt 500 A* | **Contingency only** (§5.7.4) — if BLE BMS access fails and true SOC is wanted |
| — | Mounting, strain relief, **inline fuse on the battery tap** | inside existing enclosure |

No separate enclosure is required — GateLink mounts inside the existing
controller enclosure (§1.3).

**Existing installed hardware, not purchased:** Nice/Apollo 1050 control board,
Victron MPPT 75/15, 50 W panel, **Diablo DSP-7LP** loop detector with two safety
loops, self-contained exit wand sensor, 2 × 2 W LED lights.

> **Purchase note.** The BSS138 module is a **certain** purchase, not a
> conditional one: VE.Direct requires level shifting regardless of how the BusT4
> physical layer resolves. Only whether BusT4 also consumes two of its channels
> is conditional.

#### 4.1.3 Bench / debug (§9)

| Qty | Item | Notes |
|----:|------|-------|
| 1 | **8-channel USB logic analyzer** (sigrok/PulseView compatible) | Primary BusT4 sniff tool. Captures both directions with timestamps, decodes UART, and — critically — **shows the 519–590 µs break**, which a USB-serial adapter cannot. |
| 1 | USB-TTL serial adapter (5 V / 3.3 V selectable) | secondary; convenient for ASCII-oriented VE.Direct work |
| — | Jumper leads, breadboard | |

**Input tolerance:** confirm the analyzer's inputs are 5 V tolerant, or run the
tap through the BSS138. Many low-cost 8-channel clones are 3.3 V parts with
5 V-tolerant inputs, but this must be verified against the specific unit, not
assumed. **Never connect the analyzer's outputs (if any) to the bus** — this is a
read-only tap.

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
and GateLink ever drive the bus.

> Note this is a statement about **BusT4**, not about the LoRa network. LoRa
> media access *is* now a multi-node concern — see §6.7.

#### 4.2.2 Pin identification and physical-layer discrimination — **complete before first connection**

Perform and record before mating anything. **This is now a laptop bench
procedure** (§9.1), performed with the logic analyzer before GateLink firmware
touches the bus. One capture answers all three questions.

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
   commanded externally is the 1050's TX → GateLink RX. The remaining data line
   is the 1050's RX → GateLink TX; confirm it idles high before driving it.
5. **Record** the result below, with a photograph of the jack, and commit both to
   `/docs`. This becomes the authoritative reference for this install.

| 6P4C position | Signal | Measured | LRAN connection |
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

- Channel 1: 1050 TX → GateLink RX
- Channel 2: GateLink TX → 1050 RX

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

##### 4.2.4.2 RS pin under GPIO control — **relaxed in v0.5, still recommended**
The part draws roughly **6–10 mA typical in normal mode** (higher while driving
dominant). Under v0.4's 37.5 Ah usable budget this was the largest single change
to node consumption. **Against 100 Ah usable (§8.1) it is ~0.19 Ah/day out of
~6.9 — about 2.8%.** It is no longer a design driver.

Retain GPIO control anyway: it costs one pin and a few lines of code, it is free
margin, and the SN65HVD230's standby mode keeps the **receiver active** while
disabling only the driver, so there is no functional penalty.

- **Wire RS to a GPIO.** Assert standby whenever not actively transmitting.
- Firmware responsibility recorded in §5.2.
- Power line item recorded in §8.2.

##### 4.2.4.3 Slope control
If RS is instead used for slope control: a 10 kΩ resistor to ground gives ~100 ns
loop delay and 100 kΩ gives ~500 ns. Bit time at 19200 baud is ~52 µs, so even
the slowest setting costs ~1% of a bit. **Prefer the slowest available slope**
for EMI margin. Note this conflicts with GPIO standby control on a single pin —
if both are wanted, the GPIO drive can be sequenced through a resistor. Given
§4.2.4.2's relaxation, **slope control may now take priority** if a single choice
must be made.

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
**Do not** power GateLink from BusT4 VCC. The node is powered from the LiFePO4
battery via the 12 V→USB-C adapter (§4.4); domains stay separate, sharing only
the GND reference required by the conditioning circuit. §13, D7 — resolved.

### 4.3 VE.Direct electrical — **RX now mandatory**

> **Correction retained from v0.3:** earlier drafts stated VE.Direct is 3.3 V TTL
> requiring no level shifting. **This is incorrect.** All Victron MPPTs are 5 V
> devices.

VE.Direct is a **5 V TTL UART, 19200 baud**, on a **JST-PH 2.0 4-pin** connector.
Pinout is vendor-documented, with signal names given from the **device's**
perspective:

| Pin | Signal (device POV) | LRAN connection |
|---|---|---|
| 1 | GND | GateLink GND |
| 2 | RX (into MPPT) | BSS138 ch 4 → GateLink TX — **required** (v0.4: optional) |
| 3 | TX (out of MPPT) | BSS138 ch 3 → GateLink RX — **required** |
| 4 | V+ | **Do not connect** |

**Change from v0.4.** v0.4 permitted leaving pin 2 unconnected, since the MPPT
emits text-protocol frames unsolicited at ~1 Hz. The HEX protocol requirement
(§6.6) removes that option: HEX is a request/response protocol and **cannot
function without the MPPT RX line**. Both BSS138 channels 3 and 4 are now
committed, and "MPPT RX optional" is deleted as a fallback.

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
- **Wire colors are actively misleading.** VE.Direct cables are crossover cables;
  red may be GND and black may be V+, and the two data conductors differ in
  meaning between the cable's ends. **Meter every conductor and confirm which pin
  it lands on.** Buying a genuine Victron cable and cutting it is the easiest
  sourcing path and makes this check mandatory.
- Community sources disagree about whether pin 4 is V+ or GND on some units. Moot
  since pin 4 is unconnected, but a further argument for metering first.
- **If a SmartShunt is added** (§5.7.4) it presents a second VE.Direct port
  requiring a second level-shifted UART pair. The ESP32-S3 has three hardware
  UARTs; BusT4 and MPPT consume two, leaving one. A second BSS138 module would be
  required. Carried as a contingency, not a v1 requirement.

### 4.4 GateLink power source

**12 V LiFePO4 (100 Ah)** → inline fuse → **off-the-shelf 12 V-to-USB-C
adapter** → USB-C into the Heltec. An automotive/marine adapter is acceptable
and preferred over a custom buck converter. Spec by requirement so it stays
sourceable:

| Requirement | Value | Rationale |
|---|---|---|
| Input range | 9–16 V | tolerate LiFePO4 absorption ~14.4 V |
| Output | 5 V @ ≥2 A | LoRa TX bursts are short but sharp |
| **Quiescent draw, no load** | **≤ 5 mA** | see note |
| Termination | screw or ring terminal | not a cigarette plug |

**The quiescent figure still matters, but less than it did.** Many inexpensive
adapters idle at 15–20 mA. Against v0.4's 37.5 Ah usable that was a meaningful
fraction of the night budget; against 100 Ah it is ~0.36 Ah/day out of ~6.9.
**Measure the actual no-load draw of the selected part** — it remains a
two-minute measurement and it feeds §8.2 — but it is no longer a selection gate.

> **LiFePO4-specific note.** The battery's BMS can disconnect the load under
> fault (low cell voltage, over-current, over-temperature). Unlike an FLA, which
> simply sags, a LiFePO4 pack goes to **zero volts at the terminals** when the
> BMS opens. GateLink will not brown out gracefully; it will drop dead and
> reboot when the BMS re-closes. Nothing must depend on a clean shutdown
> sequence, and nothing critical may live in RAM across a power event.

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

Under Branch B, BusT4 moves to the SN65HVD230 and channels 1–2 are free — which
is where a SmartShunt's VE.Direct pair would land if §5.7.4 is exercised under
Branch B. Under Branch A, a SmartShunt needs a second module.

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

- Bring GateLink ground and the MPPT signal ground to battery negative at a
  **single common point**.
- Keep the **1050's motor-terminal wiring** physically separated from signal
  grounds, so the thin signal grounds never become a parallel path for motor
  return current.

---

## 5. Firmware — GateLink

### 5.1 Framework & libraries

- Build: **PlatformIO**, ESP32-S3 target.
- LoRa: **RadioLib** driving the SX1262. Channel Activity Detection (CAD) used
  for media access (§6.7).
- BusT4: **ported** from the existing C++ reverse-engineering work (§12) —
  extract frame construction/parsing (Open/Close/Stop/SBS, GET/SET info
  requests) and drop the ESPHome/WiFi scaffolding. **See §12.2 for the licensing
  consequence.**
- VE.Direct: use/port **osh-labs/VE.Direct_mppt_arduino** (**MIT** — confirmed,
  §12.1) for **both** the text-protocol parser and the HEX protocol definitions,
  register map, and encode/decode helpers.
- BLE: **NimBLE-Arduino** (§5.7) — materially smaller flash and RAM footprint
  than Bluedroid, which matters because the BusT4, VE.Direct, LoRa and BLE
  stacks now coexist on one node.

### 5.2 Responsibilities

1. **BusT4 client** — send commands; poll status via GET/INF requests; parse
   asynchronous events. Must implement the 519–590 µs pre-burst UART break
   (§4.2.1). **Decode locally only what drives triggers** (§3.3): gate state,
   detector inputs (§5.4), movement cause. Everything else is forwarded raw
   (§5.8). No arbitration or address routing required (point-to-point, §4.2.1).
2. **VE.Direct text reader** — continuously parse the ~1 Hz text frames; cache the
   latest complete snapshot; watch the charger error field.
3. **VE.Direct HEX transport** — multiplex HEX request/response with the text
   stream on the same UART (§6.6); enforce the write-authentication rule
   (§6.6.3). **No local interpretation of register semantics.**
4. **BLE BMS client** — duty-cycled connect/read/disconnect against the battery
   BMS (§5.7).
5. **LoRa endpoint** — receive commands, transmit status/ACK/event frames; CAD +
   backoff before transmit (§6.7); fragmentation for oversized payloads (§6.4.4).
6. **Trigger/cache logic** — transmit on poll, gate state change, VE.Direct
   critical error, vehicle detection event (§5.4), or BMS alarm.
7. **Detection and direction classification** — §5.4.
8. **Authentication** — verify HMAC + sequence before acting on commands and
   before passing HEX writes to the MPPT (§6.5, §6.6.3).
9. **Power management** — WiFi **off**; BLE **duty-cycled, not disabled**
   (§5.7.2); Vext / OLED **off** in normal operation.
   **Branch B only:** drive the SN65HVD230 RS pin to standby when not
   transmitting (§4.2.4.2).
   **Removed in v0.5:** light-sleep, PV-aware profile switching, and SX1262 RX
   duty-cycling — see §8.6.
10. **Raw BusT4 frame streaming** — opt-in diagnostic (§5.8).
11. **Debug** — display control (§5.6), packet loopback, dummy status push,
    detection event injection, leveled serial logging (§9).
    **Removed in v0.5:** integrated BusT4 sniff mode — see §9.1.

### 5.3 Peripheral power control

Explicitly disable the WiFi radio at boot. Gate Vext so the OLED and its rail are
off unless a display trigger enables them (§5.6). BLE is initialized and
de-initialized around each BMS poll rather than left resident (§5.7.2).

### 5.4 Vehicle detection — **REQUIRED**

> **Substantially revised in v0.5.** v0.4 described a Reno A&E BX-LP two-channel
> detector presenting two independent loop contacts (one inside, one outside).
> **That is not the installed hardware.** The corrected description below changes
> the classification logic, the timing window, and the alert semantics.

#### 5.4.1 Installed detection hardware

| Signal | Source | Position | Semantics |
|---|---|---|---|
| **SAFETY** | **Diablo DSP-7LP** loop detector | Two loops, **one outside and one inside the gate**, wired **in parallel** into one detector channel | **Single** dry contact to the 1050. Asserts when a vehicle is over **either** loop. The two loops are *not* separately observable. |
| **EXIT** | Self-contained **wand-style** sensor | **Much farther inside** the gate | Separate dry contact to the 1050. Asserts on vehicle presence at the wand. |

Both are existing, working installed hardware. Both terminate as **dry-contact
inputs on the 1050 board**, so both should — pending §13 D3 — be observable over
BusT4 without additional wiring.

The DSP-7LP draws **1 mA static** with no vehicle detected (§8.2), which is far
below v0.4's conservative 40 mA placeholder.

#### 5.4.2 Consequences of the corrected topology

1. **Direction is still derivable.** The two signals are widely separated along
   the drive, with SAFETY at the gate and EXIT far inside it. Rising-edge
   ordering discriminates:

   | First rising edge | Then | Classification |
   |---|---|---|
   | `EXIT` | `SAFETY` | `EXIT` — vehicle departing (moving outward) |
   | `SAFETY` | `EXIT` | `ENTRY` — vehicle arriving (moving inward) |
   | either | *(no second edge within window)* | `UNDETERMINED` |
   | both within `detect_debounce_ms` | — | `UNDETERMINED` |

2. **The window must be much larger than v0.4's 5 s.** A departing vehicle trips
   EXIT, then *waits for the gate to open* before reaching SAFETY. With the wand
   set well back, gap times of 10–30 s are normal. See §5.4.4.

3. **SAFETY cannot distinguish inside from outside.** Because both loops feed one
   channel in parallel, a vehicle traversing the gate produces one continuous
   SAFETY assertion spanning both loops, not two separable pulses. The
   classifier must not attempt to read structure inside a single SAFETY
   assertion.

4. **EXIT is also an actuator.** The wand's assertion causes the 1050 to open the
   gate. This makes movement-cause attribution easy: a gate-open transition
   immediately following an EXIT rising edge is self-evidently exit-triggered
   (§5.5).

5. **Wand hold behavior must be characterized.** Some self-contained wand sensors
   are motion/presence hybrids that de-assert after a hold time even with a
   vehicle stationary above them. If so, a vehicle idling at the wand may produce
   repeated edges. Record actual behavior during §9.1 sniffing and set
   `detect_debounce_ms` and the re-trigger lockout accordingly. **`TBM`.**

#### 5.4.3 Requirements

- **5.4.3.1** GateLink SHALL determine the instantaneous state of the SAFETY and
  EXIT inputs and represent each as a discrete boolean in the status payload.
- **5.4.3.2** GateLink SHALL derive direction of travel from the rising-edge
  ordering in the table in §5.4.2.
- **5.4.3.3** A detection event SHALL trigger an **immediate unsolicited status
  push**, independent of the poll schedule.
- **5.4.3.4** GateLink SHALL raise the **gate-held-open alert** defined in §5.4.5.
- **5.4.3.5** The acquisition path (BusT4 status field vs. direct GPIO) is an
  implementation detail and SHALL NOT be visible in the LoRa protocol or the HA
  entity model.
- **5.4.3.6** Direction classification SHALL be implemented as a state machine
  with an explicit idle-reset condition, not as a simple ordered pair, so that
  partial traversals (vehicle pulls up to the wand and reverses; vehicle stops on
  the loops and waits) resolve to `UNDETERMINED` rather than corrupting the next
  real traversal.

#### 5.4.4 Configuration

| Parameter | Default | Meaning |
|---|---|---|
| `detect_sequence_window_ms` | **60000** | Max gap between the first and second rising edge for the pair to count as one traversal. **Raised from v0.4's 5000** — a departing vehicle waits for a full gate-open cycle between EXIT and SAFETY. |
| `detect_sequence_idle_ms` | 10000 | Both inputs clear for this long ends the current sequence and resets the state machine to idle. |
| `detect_debounce_ms` | 50 | Debounce on each input. Edges closer together than this across the two inputs are treated as simultaneous. |
| `held_open_alert_delay_s` | **30** | See §5.4.5. Suppresses alerts during normal traversals. |
| `held_open_alert_repeat_s` | 0 | 0 = one alert per detection. Non-zero re-alerts at this interval while the condition persists. |

#### 5.4.5 Gate-held-open alert — **the primary operational requirement**

The operational need is: *the gate is sometimes deliberately left standing open
(deliveries, work on the property), and in that state any vehicle movement
through it should notify by email and SMS from HA.*

This is a narrower and more useful condition than v0.4's "detection while the
gate is not fully closed", which would have fired on **every normal traversal**,
since SAFETY asserts on every pass.

**Definition — the gate is `STATICALLY_OPEN` when all of:**

| Condition | Source |
|---|---|
| Gate state is fully open | 1050 via BusT4 |
| Gate is not moving and no motion is pending | 1050 via BusT4 |
| No auto-close timer is running | 1050 via BusT4 — **`TBM`, §9.1**. If the 1050 does not expose auto-close state, substitute "no gate state change within `held_open_alert_delay_s`". |
| It has been in that state for ≥ `held_open_alert_delay_s` | GateLink timer |

**Requirement.** While `STATICALLY_OPEN`, any rising edge on SAFETY or EXIT
SHALL:
1. Set the `vehicle_while_held_open` alert flag in an immediate `EVENT` push.
2. Include the direction classification if one is available at the time of the
   push, and a follow-up push when classification completes.

**Rationale for the delay.** A normal traversal ends with the gate reaching fully
open, then closing. `held_open_alert_delay_s` = 30 ensures the gate must sit open
and unattended before the condition arms, so ordinary passes do not notify.

**HA delivery.** Because this drives email and SMS, the event must be delivered
as a **non-retained MQTT event message**, not solely as a retained
`binary_sensor` state (§7.4). Retained-state binary sensors replay on HA restart
and on discovery refresh, which would produce spurious 2 AM notifications. The
binary sensor is provided for dashboard visibility; **the event topic is the
automation trigger.**

#### 5.4.6 Acquisition path — BusT4 vs GPIO

§5.4.3.5 decouples this from the protocol so it can be decided from sniff data
(§13, D3) without blocking protocol work.

**BusT4 path — now clearly preferred.** The `makstech` component issues an
`INF_IO` request to read the controller's I/O state, using it to confirm limit
switches — strong evidence the 1050 reports input states over the bus, with the
SAFETY and EXIT contacts plausibly in the same structure. Zero added wiring.

**The v0.4 timing objection has largely evaporated.** v0.4 warned that `INF_IO`
poll rate would set ordering resolution, and that two closely-spaced loops could
assert 0.5–3 s apart. With the corrected topology, SAFETY and EXIT are separated
by a gate cycle and a long stretch of driveway — gaps of 10–30 s. A 250–500 ms
`INF_IO` poll while either input is active or the gate is not closed has ample
resolution. **BusT4 acquisition is now the primary plan; GPIO is documented
fallback only.**

**GPIO fallback.** Deterministic and simple, but both contacts are already
committed to the 1050's inputs. Options: parallel the existing contacts
(electrically fine, but **confirm the 1050's input is not a current-sourcing
scheme** that would fight an ESP32 pull-up), or use a spare/auxiliary output on
the DSP-7LP. **Check the DSP-7LP datasheet for a second/auxiliary output before
assuming a tap is required** — it is a two-channel-capable detector being used in
a parallel single-channel configuration, so a spare channel may be available.

### 5.5 Movement cause

Where BusT4 exposes it, capture the reason for gate movement (exit wand vs.
safety vs. remote vs. Oview vs. wall button) and publish as a text sensor. `TBD`
pending sniff results (§13, D3).

Per §5.4.2 item 4, exit-wand attribution is inferable locally even if BusT4 does
not report cause directly: a gate-open transition within a short window of an
EXIT rising edge is exit-triggered. Use the BusT4-reported cause when available
and fall back to inference otherwise.

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
inactivity timer**, whenever the node is in any debug mode (packet loopback,
simulated status push, detection event injection, raw BusT4 streaming, or any
future mode registered as a debug mode). It returns to manual/timeout behavior
when the last debug mode exits.

> **v0.5:** "BusT4 sniff" is removed from the debug-mode list (§9.1). "Raw BusT4
> streaming" (§5.8) is added, since leaving that on unattended is exactly the
> condition a lit display should advertise.

**Sub-items for development:**
- **Multi-page cycling.** One page will not fit gate state + detector state +
  MPPT + battery SOC + link quality. Proposal: short-press = next page,
  long-press = off.
- **Vext sequencing.** Repeatedly powering the OLED rail down and up requires a
  settle delay and a **full SSD1306 re-init** on each power-up, not merely a wake
  command. Getting this wrong produces a display that works exactly once.

### 5.7 Battery BMS over BLE — **new in v0.5**

#### 5.7.1 Why this matters more than it looks

LiFePO4 has a famously flat discharge curve: roughly **13.2–13.3 V across
20–80% SOC**. Voltage-derived SOC is therefore close to meaningless through the
middle of the range and only usable near the endpoints.

The MPPT compounds this: it measures **charge** current only, not load current,
so coulomb counting on discharge is not available from it either.

**The BLE BMS is therefore the only good SOC source in the base BOM**, not a
convenience feature. §5.7.4 defines what happens if it is unavailable.

#### 5.7.2 Requirements

- **5.7.2.1** GateLink SHALL, on a configurable interval (`bms_poll_s`, default
  **300**), connect to the battery BMS over BLE, read state of charge, pack
  voltage, pack current, cell voltages, temperature(s) and alarm/protection
  flags, then disconnect.
- **5.7.2.2** GateLink SHALL de-initialize the BLE controller between polls
  rather than leaving the stack resident, so idle BLE standby current is not
  carried continuously.
- **5.7.2.3** BMS alarm or protection flags SHALL trigger an immediate
  unsolicited status push.
- **5.7.2.4** A failed BLE connection SHALL NOT block or delay any other node
  function. BMS data is published with a staleness timestamp; consumers treat
  absence as unknown, not as zero.
- **5.7.2.5** The connect/read/disconnect cadence SHALL leave the BMS reachable
  from a phone between polls — see §5.7.5.

#### 5.7.3 Power cost — small

A connect / read / disconnect cycle is roughly **3–6 s at 60–100 mA ≈ 0.1 mAh**.
At `bms_poll_s` = 300 that is 288 cycles/day ≈ **0.03 Ah/day** against a ~6.9
Ah/day budget — under half a percent. Even a 60 s cadence costs only ~0.15
Ah/day.

**There is no radio coexistence problem.** The SX1262 is separate silicon on SPI
with its own antenna at 915 MHz; the ESP32-S3's BLE radio is 2.4 GHz. This is
unlike WiFi+LoRa, where contention for MCU time and shared RF front ends is a
real concern. WiFi remains off on GateLink regardless (§5.3).

**Flash cost is the real budget item.** NimBLE adds roughly 200–300 KB. The
Heltec V3's 8 MB flash accommodates this comfortably, but partition sizing should
be checked once all four stacks are linked.

#### 5.7.4 Protocol identification and fallback — **D15**

The WattCycle BMS protocol is **unknown at time of writing**. Packs in this class
commonly use a JBD/Xiaoxiang-family or JK-family BLE BMS, both of which have
existing community implementations, but **this must be confirmed rather than
assumed.**

**Plan of record:**

1. **On battery arrival**, use **nRF Connect** on a phone to enumerate GATT
   services and characteristics and capture traffic from the vendor app.
   *(Confirmed as the intended approach.)*
2. Identify the family, then port or adapt the corresponding community client.
3. Record findings and the characteristic map in `/docs/bms-protocol.md`.

**Fallback if BLE access proves impractical: a Victron SmartShunt.** This is the
selected contingency (D15). It provides true coulomb-counted SOC, load current
(which nothing else in the system measures), and consumed-Ah — and it speaks
VE.Direct, so it reuses the existing parser and transport rather than adding a
new protocol.

Cost of the fallback: a second level-shifted UART pair (§4.3), roughly 1 mA
continuous, and shunt installation in the battery negative lead.

**Interim fallback (before either path is proven):** publish battery voltage from
the MPPT with an explicit `soc_source: voltage_coarse` marker, and track **daily
Vmin and daily yield (H19/H20/H21) trends** rather than instantaneous SOC. This is
genuinely adequate for the stated operational goal of monitoring nightly
consumption and recovery over time — the trend is the signal, not the absolute
number.

#### 5.7.5 Single-connection constraint

Many BMS BLE modules in this class accept **only one connection at a time**. A
node holding a persistent connection will lock the vendor phone app out entirely.
The connect/read/disconnect cadence in §5.7.2 is designed around this: at
`bms_poll_s` = 300 the BMS is free for roughly 98% of wall-clock time.

Provide an HA `switch` — **"BMS BLE polling"** — to suspend polling entirely
while working with the phone app. Default on.

### 5.8 Raw BusT4 frame streaming — **new in v0.5**

The requirement is full BusT4 status visibility in HA during normal operation,
with parsing decisions deferred until debug data has been collected. Two distinct
mechanisms serve this; conflating them would make the airtime-hungry one always-on.

#### 5.8.1 Mechanism 1 — decoded status (always on)

A compact **binary status struct** (fixed schema, versioned per §6.4.3,
target **40–80 bytes**) sent on poll and on trigger. This backs the HA entities
in §7.4. Comfortably inside one LoRa packet.

#### 5.8.2 Mechanism 2 — raw frame stream (opt-in)

GateLink forwards **raw BusT4 frames as opaque byte blobs** to the bridge, which
publishes them as hex strings.

| Property | Value |
|---|---|
| Control | HA `switch` — "BusT4 raw stream". **Default off.** |
| Auto-disable | `raw_stream_timeout_min`, default **60**. Prevents leaving it on indefinitely. |
| Framing | Batched; fragmented per §6.4.4 when a batch exceeds one packet. |
| Direction | Both 1050→node and node→1050 frames, tagged with direction and a monotonic timestamp. |
| MQTT | `lran/gatelink/bust4/raw`, **non-retained, no MQTT Discovery entity**. |

**Why no HA entity for the raw stream.** HA caps entity state strings at 255
characters and records every state change to the recorder database. A raw frame
stream would truncate, flood the database, and be useless for analysis anyway.
The intended consumer is `mosquitto_sub -t 'lran/gatelink/bust4/raw' | tee
capture.log`, or a small script in `/tools`. A `sensor` reporting **frames/minute
and bytes/minute** IS exposed, so stream activity is visible in HA without the
payload.

#### 5.8.3 Airtime budget

At SF9 / BW 125 kHz / CR 4/5:

| Payload | Approx. airtime |
|---|---|
| 50 bytes | ~0.28 s |
| 100 bytes | ~0.5 s |
| 235 bytes (max useful, §6.4.2) | ~1.2 s |

Mechanism 1 at 40–80 bytes on a 1–5 minute poll is negligible. Mechanism 2 at
sustained rates is not, which is why it is opt-in with a timeout — and why it
matters that WellLink shares the channel (§6.7). If the SF resolves lower after
the range test (D1), all figures improve proportionally.

**If the full status set proves not to fit** even in mechanism 1, the fallback is
explicitly to **collect data first, decide later**: run mechanism 2 to capture
what the 1050 actually emits, then select the fields worth carrying in the
mechanism-1 struct. The schema versioning in §6.4.3 exists so this can be changed
without a flag-day reflash.

---

## 6. Firmware — LoRaBridge & LoRa protocol

### 6.1 Framework & libraries — MQTT client resolved (D5)

PlatformIO, ESP32-S3. RadioLib (SX1262). WiFi. Links the same shared LoRa
protocol library as every node, plus the BusT4 and VE.Direct decoders (§3.3).

**Define a thin `MqttTransport` interface; implement first against
PubSubClient.** The decision is low-stakes *if* abstracted and high-friction if
not. Bridge traffic remains modest — a poll per node every 1–5 minutes plus
occasional commands and events — so throughput and QoS 2 are irrelevant. What
matters is reliable reconnect, LWT, and publishing discovery-config JSON.

| Option | License | Assessment |
|---|---|---|
| **PubSubClient** | MIT | Tiny, synchronous, extremely stable, ubiquitous. **Gotcha:** default max packet 256 bytes — Discovery configs exceed this and fail confusingly. Fix with `MQTT_MAX_PACKET_SIZE` ≥1024. |
| **espMqttClient** | MIT | Actively maintained, sync and async variants, large payloads, QoS 0/1/2. Best modern choice on merit; designated fallback. |
| **AsyncMqttClient** | MIT | Effectively unmaintained; AsyncTCP callbacks run in a separate task with real constraints. Avoid for new work. |
| **esp-mqtt** (IDF native) | Apache-2.0 | Most robust reconnect/TLS, but pulls the design toward IDF framework. Reserve for later migration. |

> **Watch item, raised by the raw stream.** §5.8.2 can produce sustained
> publishes. A blocking publish could overlap a LoRa RX window; the SX1262
> buffers received frames and raises an interrupt, so brief blocking is
> tolerable, but the raw stream is the one feature likely to stress this. If soak
> testing shows dropped frames while streaming, switch to espMqttClient — the
> abstraction makes it contained.

### 6.2 OTA update — **new in v0.5 (D16 resolved)**

**The bridge supports OTA; remote nodes do not.**

| | Bridge | GateLink / WellLink |
|---|---|---|
| Update path | **OTA over WiFi** + USB | USB only |
| Rationale | On the LAN, mains powered, physically accessible, and the node whose firmware changes most often — it now owns all the decoders (§3.3) | 500 ft away; a bad flash is a walk with a laptop, and there is no second radio path to recover through |

Requirements:
- ArduinoOTA or ESP-IDF `esp_https_ota`, on an authenticated endpoint with a
  password/key held in untracked config (§10).
- **Dual-partition (A/B) with rollback.** A bricked bridge takes the whole
  property's telemetry offline.
- OTA disabled during an active LoRa transaction; deferred until idle.
- Version string published to `lran/bridge/version` and exposed as a diagnostic
  sensor.

### 6.3 Responsibilities

1. LoRa RX → verify → **decode per source node** → publish to MQTT state topics.
2. MQTT command topic subscribe → build authenticated LoRa command addressed to
   the target node → TX → await ACK → publish result; retry with backoff.
3. **VE.Direct HEX proxy** (§6.6) — including write-authorization enforcement.
4. Publish **MQTT Discovery** config on boot and reconnect, **one HA device per
   registered node** plus the bridge itself.
5. **Availability** — bridge's own via MQTT LWT; **per-node via watchdog**
   (§6.4.6).
6. **Per-node poll scheduler**, each runtime-configurable via that node's HA
   `number` entity.
7. Publish per-node link diagnostics (RSSI/SNR, last-seen, missed polls).
8. Republish detection events as HA events (§7.4).
9. Raw BusT4 frame republish (§5.8.2).
10. Debug: loopback mode, dummy-status publish, per-node simulators.
11. Serve OTA (§6.2).

### 6.4 LoRa protocol — **substantially revised for multi-node**

#### 6.4.1 Topology

Star. The bridge is the centre and the only node that initiates polls. Nodes
never address each other. No mesh, no relaying (§2.2).

#### 6.4.2 Frame format

```
| ver | type | src | dst | seq | frag | schema | payload... | MAC (8B, auth only) | CRC16 |
   1     1     1     1     2      1       1        0..N            0 or 8            2
```

| Field | Size | Notes |
|---|---|---|
| `ver` | 1 | Protocol version. See §6.4.5. |
| `type` | 1 | `COMMAND`, `COMMAND_ACK`, `POLL`, `STATUS`, `EVENT`, `ERROR`, `PING`/`LOOPBACK`, `HEX_REQ`, `HEX_RSP`, `RAW` |
| `src` / `dst` | 1 each | Node IDs per §1.4. `0x00` bridge, `0xFF` broadcast. 254 usable node addresses. |
| `seq` | 2 | Monotonic per source, per boot. |
| `frag` | 1 | High nibble = fragment index, low nibble = total. `0x11` = single unfragmented frame. Max 15 fragments. |
| `schema` | 1 | Payload schema ID + version for `STATUS`/`EVENT`. See §6.4.3. |
| `MAC` | 0 or 8 | Truncated HMAC-SHA256. Present on authenticated types only (§6.5). |
| `CRC16` | 2 | Over the whole frame. |

Header is 9 bytes. Against the SX126x 255-byte PHY payload limit that leaves
**~244 bytes** unauthenticated or **~236 bytes** authenticated, before CRC.
Design target for status payloads: **≤ 200 bytes** to keep margin.

#### 6.4.3 Per-node payload schemas — **new**

The bridge can no longer assume "`STATUS` means gate status." Each node type
defines its own payload schema, identified explicitly:

| `schema` value | Meaning |
|---|---|
| `0x10` | GateLink status v1 |
| `0x11` | GateLink event v1 |
| `0x20` | WellLink status v1 (reserved) |
| `0xF0` | Generic node health (uptime, RSSI, boot count) — all nodes |

**Why explicit rather than inferred from `src`.** The bridge *could* look up node
type from its registry. Explicit schema IDs additionally survive **firmware
version skew** — GateLink running an older build than the bridge expects
announces schema `0x10` while the bridge already understands `0x12`, and the
bridge decodes it correctly instead of misparsing. Given that remote nodes have
no OTA and will drift out of sync, this is worth one byte.

Schema definitions live in `/lib/lran-protocol/schemas/` and are the versioned
contract between node and bridge.

#### 6.4.4 Fragmentation — **new**

Required by §5.8.2 raw streaming and possible for a full status set. Adding it
now, while the fleet is one node, avoids a flag-day protocol change later.

- Sender splits into ≤15 fragments, each a complete frame with its own CRC.
- Receiver reassembles by `(src, seq, schema)`; incomplete sets expire after
  `frag_reassembly_timeout_ms` (default 5000) and are discarded with an error log.
- **Fragments are individually acknowledged only for `COMMAND`.** Status and raw
  frames are fire-and-forget; a dropped fragment discards the set.
- Fragmentation is **not** used for commands in v1. Commands are small; keeping
  them single-frame keeps the authentication and replay logic simple.

#### 6.4.5 Version tolerance — **the no-OTA consequence**

Remote nodes are USB-only (§2.2). A protocol change therefore means a physical
visit to every node on the property. To make rollout incremental rather than a
flag day:

- **The bridge SHALL accept protocol version `N` and `N−1`** and decode both.
- Nodes accept only their own version from the bridge; the bridge downgrades
  per-node as needed based on the version last heard from that node.
- A node running an unsupported version is marked `unavailable` with a distinct
  reason, not silently ignored.
- Breaking changes to `/lib/lran-protocol/` require a version bump and an entry
  in `/docs/protocol-changelog.md`.

This is the single most important operational constraint the multi-node change
introduces. It is cheap now and expensive to retrofit.

#### 6.4.6 Per-node availability — **new**

MQTT LWT covers only the bridge's own connection to the broker. It says nothing
about whether GateLink is alive.

- The bridge maintains `last_seen` per node.
- A node is marked `offline` after `missed_poll_threshold` (default **3**)
  consecutive unanswered polls, and `online` on any valid frame received.
- State published to `lran/<node>/availability`, **retained**, and referenced by
  every entity belonging to that node in its discovery config.
- The bridge's own LWT marks *all* nodes unavailable implicitly, since HA loses
  the bridge's availability topic too.

### 6.5 Authentication and keying — **revised for multi-node**

#### 6.5.1 Per-node keys

v0.4 assumed one shared key. With a fleet, a single key means **compromising the
well sensor grants gate-command authority** — an unacceptable coupling between a
low-value node and the only node that moves a large motorized object.

**Derive per-node keys from one master:**

```
node_key = HKDF-SHA256(master_key, salt = "lran-v1", info = "node-" || node_id)
```

- Only `master_key` is provisioned to the bridge.
- Each node is flashed with only its own derived key.
- Compromising a node yields that node's key alone.
- Adding a node requires no change to existing nodes.

`master_key` lives in untracked build config (§10) and is never committed.

#### 6.5.2 What is authenticated

| Frame type | MAC required | Rationale |
|---|---|---|
| `COMMAND` | **Yes** | Moves the gate. |
| `HEX_REQ` — Set (`0x8`) / Restart (`0x6`) | **Yes** | Writes MPPT config — a battery-damage path under LiFePO4 (§6.6.3). |
| `HEX_REQ` — Get (`0x7`) | No | Read-only, consistent with status. |
| `STATUS`, `EVENT`, `POLL`, `RAW` | No | Per requirements; spoofed status is a nuisance, not a hazard. |
| `COMMAND_ACK` | No | Correlated to an authenticated request by `seq`. |

#### 6.5.3 Sequence / replay

Monotonic counter, no cross-reboot persistence. Each node picks a random
`boot_id` at boot; the bridge keeps a **per-node table** of `(boot_id, last_seq)`
and accepts increasing `seq` within a `boot_id`, resyncing on a new one. This is
a table lookup in v0.5, not a scalar as in v0.4.

#### 6.5.4 Reliability

Commands are ACKed (receipt + execution result); the bridge retries with backoff.
Status is fire-and-forget except poll responses, detection events, and HEX
responses.

### 6.6 VE.Direct HEX proxy — **new in v0.5**

#### 6.6.1 Requirement

HA must be able to read and write **all** MPPT 75/15 configuration and status
registers. GateLink acts as **transport only** — it does not interpret register
semantics, hold a register cache, or replay writes.

#### 6.6.2 On-node multiplexing

The MPPT emits ~1 Hz text frames and HEX responses **on the same UART**, and they
interleave. The parser is a line-oriented state machine:

- A line beginning with `:` is a **HEX** frame — hex nibbles, checksummed,
  newline-terminated.
- Anything else belongs to the **text** protocol (`LABEL\tVALUE\r\n`, blocks
  terminated by a `Checksum` field).

Rules:
- **One outstanding HEX transaction at a time.** Correlate the response by its
  echoed register ID; discard unmatched responses with a log entry.
- `hex_timeout_ms` default **1000**; on timeout return a `HEX_RSP` carrying an
  explicit timeout status rather than silence.
- **Do not disable the text protocol** to simplify HEX handling. The 1 Hz text
  stream is the primary telemetry source (§5.2.2).
- HEX requests are transported **verbatim**. GateLink inspects the command nibble
  for §6.6.3 and otherwise does not parse the payload.

#### 6.6.3 Write protection — three independent gates

Writing MPPT charge parameters under LiFePO4 is a **battery-damage path**.
Re-enabling temperature compensation or equalization on a lithium pack is exactly
the kind of one-character mistake that is invisible until the battery is harmed.
Three gates, all required:

1. **HMAC on the frame.** Set (`0x8`) and Restart (`0x6`) commands require a
   valid MAC (§6.5.2). GateLink rejects unauthenticated writes at the transport
   layer and returns an `ERROR`.
2. **Armed write-enable switch.** An HA `switch` — **"MPPT config write
   enable"** — must be on. **Default off**, with **auto-expiry after
   `mppt_write_arm_timeout_s` (default 300)**. Enforced on the **bridge**, which
   refuses to build a write frame while disarmed. Deliberately a two-step
   operation.
3. **Audit trail.** Every write attempt — request payload, authorization
   outcome, MPPT response — published to `lran/gatelink/vedirect/hex/audit`,
   **retained**. If a charge parameter is ever wrong, the record of what changed
   it exists.

#### 6.6.4 MQTT interface

| Topic | Direction | Retained | Purpose |
|---|---|---|---|
| `lran/gatelink/vedirect/hex/request` | HA → bridge | No | Raw HEX request string |
| `lran/gatelink/vedirect/hex/response` | bridge → HA | No | Raw HEX response + status |
| `lran/gatelink/vedirect/hex/audit` | bridge → HA | Yes | §6.6.3 item 3 |
| `lran/gatelink/vedirect/write_enable/{state,set}` | both | Yes | §6.6.3 item 2 |

Raw HEX in and out is the v1 interface — it is the general case, needs no
register-by-register modelling, and matches "transport only." **Once the small
set of registers actually adjusted in practice is known, promote those to named
`number`/`select` entities** in a later revision. Do not attempt to model 100+
registers as entities.

#### 6.6.5 Sequencing — the chicken-and-egg

The MPPT must be reconfigured for LiFePO4 **before the new battery is first
charged**, and LRAN will not exist at that point.

- **Initial reconfiguration:** VictronConnect over a VE.Direct-to-USB cable, or
  the MPPT's own interface. Do this when the battery arrives. §8.1.2 lists the
  required settings.
- **LRAN's HEX path is for ongoing adjustment and verification**, and for reading
  back the configuration to confirm it has not drifted — not for initial setup.

Add a startup check: on boot, GateLink reads the charge parameters and the bridge
publishes them as diagnostic sensors so a wrong profile is visible in HA rather
than latent.

### 6.7 Media access — **new in v0.5**

Point-to-point had no contention. A shared channel with N nodes does.

**Sources of contention:**
- Unsolicited event pushes from multiple nodes (gate detection events, WellLink
  rapid-level-change pushes) can collide.
- §5.8.2 raw streaming can occupy the channel for extended periods.

**Mitigations:**

| Mechanism | Detail |
|---|---|
| **Bridge serializes polls** | Never more than one outstanding poll across the fleet. Removes the largest predictable collision source for free. |
| **CAD before TX** | Nodes use RadioLib's Channel Activity Detection before transmitting. Cheap in both time and power. |
| **Randomized backoff** | On CAD-busy, back off `random(0, backoff_max_ms)` (default 500) and retry, up to `cad_retries` (default 5), then transmit regardless — an event push must not be starved indefinitely. |
| **Raw stream yields** | §5.8.2 streaming checks CAD between batches and always yields to a pending event or ACK. |
| **Single channel** | All nodes share one frequency/SF/BW/sync word. Per-node channels would require the bridge to listen on multiple configurations, which one SX1262 cannot do. |

**Airtime awareness.** With SF and payload sizes settled after D1, compute total
expected channel occupancy across all nodes and record it. Point-to-point made
this a non-question; it is now a real budget with a real ceiling.

**FCC.** 915 MHz ISM, digital modulation under Part 15.247. Not LoRaWAN, so no
TTN duty-cycle policy applies, but transmit power and bandwidth limits do.

### 6.8 LoRa link parameters

- Band: **US 915 MHz**, point-to-multipoint star (not LoRaWAN), private sync word.
- SF / BW / CR / TX power: **TBD** after the range test (§13, D1). Starting point
  for ~150 m near-LOS: SF7–9, BW 125 kHz, CR 4/5, moderate TX power.
- **Bridge antenna placement is now a two-bearing problem.** GateLink and
  WellLink are at similar distances in **different directions**. The bridge
  antenna must serve both — favour an omnidirectional antenna in a central,
  elevated position over anything with a pattern optimized toward the gate.
  Range-test **both bearings** (§14 phase 1) before committing to a location.
- Node-address filtering in the SX126x packet handler is enabled so nodes discard
  frames not addressed to them in hardware. Low value while GateLink runs
  continuous RX; retained because it matters for any duty-cycled node
  (Appendix A).

---

## 7. Home Assistant integration (MQTT Discovery)

### 7.1 Device model — **revised for multi-node**

**One HA device per node, plus one for the bridge.** Each node's entities carry
that node's `device` block and reference that node's availability topic
(§6.4.6). This keeps the HA device page for GateLink meaningful, and lets
WellLink appear later without disturbing anything.

| HA device | Node | Availability source |
|---|---|---|
| LoRa Bridge | `0x00` | MQTT LWT |
| GateLink | `0x01` | `lran/gatelink/availability` (bridge watchdog) |
| WellLink | `0x02` | `lran/welllink/availability` (bridge watchdog) |

Discovery `unique_id`s are prefixed per node (`lran_gatelink_*`) so they are
stable and non-colliding as the fleet grows.

### 7.2 Entity model — resolved (D8): `cover` **plus** auxiliary entities

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
| Detector state, direction, movement cause | outside the model | `binary_sensor` / `sensor` |

### 7.3 Position reporting — deferred to v2

Upstream components estimate position by timing movements. **v1 will not report
position.** Time-based estimation on a gate that can be stopped, reversed, or
obstructed produces confident wrong answers, and a cover that lies about being
40% open is worse than one reporting discrete states. Revisit if the 1050 proves
to expose encoder position directly.

### 7.4 Entity table — GateLink

Topic hierarchy `lran/gatelink/...`, retained discovery configs, per-node
availability topic.

**Gate control and state**

| Entity | Type | Source | Notes |
|---|---|---|---|
| Gate | `cover` (device_class: gate) | 1050 | open/close/stop; no position (§7.3) |
| Step-by-step | `button` | 1050 | SBS command |
| Partial open 1/2/3 | `button` ×3 | 1050 | if exposed |
| Gate lock | `lock` | 1050 | BusT4 block/release |
| Gate raw state | `sensor` (text, diagnostic) | 1050 | full state incl. blocked/error |
| Movement cause | `sensor` (text) | 1050 / inferred | §5.5 |
| Gate statically open | `binary_sensor` | GateLink | §5.4.5 armed condition |

**Vehicle detection (§5.4)**

| Entity | Type | Source | Notes |
|---|---|---|---|
| Safety detector | `binary_sensor` (occupancy) | 1050 or GPIO | Combined DSP-7LP contact, both loops |
| Exit wand | `binary_sensor` (occupancy) | 1050 or GPIO | Separate wand contact |
| Last traversal direction | `sensor` (text) | GateLink | `ENTRY` / `EXIT` / `UNDETERMINED` |
| Last traversal time | `sensor` (timestamp) | GateLink | |
| Vehicle while held open | `binary_sensor` (problem) | GateLink | **Dashboard visibility only** — see below |
| **Vehicle while held open** | **`event`** | GateLink | **`lran/gatelink/event/held_open`, non-retained. This is the automation trigger for email/SMS.** |

> **Why both.** A retained `binary_sensor` replays its state when HA restarts or
> re-reads discovery, which would fire the notification automation at arbitrary
> times. The non-retained event topic fires exactly once, when it happens. Use
> the `binary_sensor` for the dashboard and the **event for the automation**
> (§5.4.5).

**Battery and solar**

| Entity | Type | Source | Notes |
|---|---|---|---|
| Battery SOC | `sensor` (%) | BMS via BLE, or SmartShunt | §5.7 |
| Battery SOC source | `sensor` (text, diagnostic) | GateLink | `bms_ble` / `smartshunt` / `voltage_coarse` — honest about provenance |
| Battery voltage | `sensor` (V) | MPPT | |
| Battery current (charge) | `sensor` (A) | MPPT | Charge only unless SmartShunt fitted |
| Battery temperature | `sensor` (°C) | BMS | §8.4 |
| Cell voltages | `sensor` ×N (diagnostic) | BMS | |
| BMS alarm flags | `binary_sensor` ×N | BMS | |
| **Charging inhibited (low temp)** | `binary_sensor` (problem) | derived | **§8.4** |
| BMS BLE polling | `switch` | GateLink | §5.7.5 |
| Panel voltage / power | `sensor` (V / W) | MPPT | |
| Yield today | `sensor` (kWh) | MPPT | |
| Charge state | `sensor` (text) | MPPT | bulk/absorption/float/off |
| Charger error | `sensor` (text/code) | MPPT | non-zero → alert + push |
| MPPT config readback | `sensor` ×N (diagnostic) | MPPT via HEX | §6.6.5 — absorption/float/temp-comp/equalization |
| MPPT config write enable | `switch` | bridge | §6.6.3, default off, auto-expiry |

**Diagnostics**

| Entity | Type | Source | Notes |
|---|---|---|---|
| BusT4 raw stream | `switch` | GateLink | §5.8.2, default off |
| BusT4 stream rate | `sensor` (frames/min) | bridge | payload itself is not an entity |
| LoRa RSSI / SNR | `sensor` (diagnostic) | bridge | per node |
| Missed polls | `sensor` (diagnostic) | bridge | feeds §6.4.6 |
| Node uptime | `sensor` (diagnostic) | GateLink | |
| Protocol version | `sensor` (diagnostic) | bridge | §6.4.5 — shows node/bridge skew |
| Poll interval | `number` (config) | bridge | per node, runtime-configurable |

Exact MPPT field list is **all available VE.Direct fields**; the table is
representative, finalized once live frames are seen.

### 7.5 Entity table — LoRaBridge

| Entity | Type | Notes |
|---|---|---|
| Bridge availability | via LWT | |
| WiFi RSSI | `sensor` (diagnostic) | |
| Bridge uptime | `sensor` (diagnostic) | |
| Firmware version | `sensor` (diagnostic) | §6.2 |
| Nodes online | `sensor` (count, diagnostic) | |
| Channel occupancy | `sensor` (%, diagnostic) | §6.7 airtime awareness |

### 7.6 WellLink — reserved

Entity model to be defined with the node (§1.6). Expected: level `sensor`,
battery `sensor`s, availability, RSSI, poll interval `number`. The device and
topic namespace are reserved now so nothing else claims them.

---

## 8. Power budget (gate side) — **substantially revised**

### 8.1 Source — LiFePO4

#### 8.1.1 Capacity

**12 V 100 Ah LiFePO4** (WattCycle 100 Ah mini w/ Bluetooth), replacing the Group
24 FLA. **100% of nameplate capacity is usable** — no 50% derate. 50 W panel via
MPPT 75/15.

| | v0.4 (FLA) | v0.5 (LiFePO4) |
|---|---|---|
| Nameplate | 75 Ah | 100 Ah |
| Usable | 37.5 Ah (50% derate) | **100 Ah** |
| Daily draw (§8.2) | ~7.3 Ah | **~6.9 Ah** |
| Autonomy at zero harvest | ~5.1 days | **~14.5 days** |
| Winter harvest ratio | 1.45× | **1.54×** |

#### 8.1.2 Required MPPT reconfiguration — **do this before first charge**

The MPPT 75/15 is presumably still on a lead-acid profile. Charging LiFePO4 on a
lead profile is harmful. Per §6.6.5, do this with VictronConnect over USB when
the battery arrives, then verify via the HEX readback sensors.

| Setting | Required value | Why |
|---|---|---|
| Battery type | **User-defined** | Not any lead preset |
| Absorption voltage | **14.2–14.6 V** | Per WattCycle spec; confirm against the pack datasheet |
| Absorption time | Short / fixed | LiFePO4 does not need long absorption |
| Float voltage | **~13.5 V** | Avoids holding the pack at high SOC indefinitely |
| **Equalization** | **Disabled** | Destructive to LiFePO4 |
| **Temperature compensation** | **0 mV/°C** | Default −16.2 mV/°C is a lead-acid behavior and is actively wrong here. **The single most consequential setting in this table.** |
| Low-voltage cutoff / load output | Per pack spec | The pack BMS is the real protection; do not rely on the MPPT |

Record the final values in `/docs/mppt-config.md` and confirm them against the
§7.4 readback sensors.

### 8.2 Load inventory

**TBM** = To Be Measured; placeholders are conservative and should be replaced
with meter readings at bring-up.

| Load | Current @ 12 V | Duty | Ah/day |
|---|---:|---|---:|
| 2 × 2 W LED lights (night only) | 0.33 A | 12 h | 4.00 |
| 1050 controller static | 0.05 A **TBM** | 24 h | 1.20 |
| **Diablo DSP-7LP loop detector** | **0.001 A** (no detect) | 24 h | **0.02** |
| GateLink node — continuous RX, no night mode | 0.050 A **TBM** | 24 h | 1.20 |
| GateLink — BLE BMS polling | — | 288 cycles/day | 0.03 |
| MPPT 75/15 self-consumption | 0.010 A | 24 h | 0.24 |
| Gate motors while operating | ~5 A **TBM** | 2 cycles × ~20 s | 0.06 |
| 12 V→USB-C adapter overhead | 0.005 A **TBM** | 24 h | 0.12 |
| *SN65HVD230, standby except TX* — **Branch B only** | *~0.008 A* | *TX only* | *~0.02* |
| *SmartShunt* — **contingency only (§5.7.4)** | *~0.001 A* | *24 h* | *~0.02* |
| **Total (Branch A, BLE BMS, no shunt)** | | | **≈ 6.9 Ah/day** |

Note the BSS138 module adds negligible current — its pull-ups draw only while a
line is held low, averaging well under 1 mA at UART duty cycles.

**Harvest:** 50 W × ~3.0 winter peak-sun-hours (western NC, non-optimal tilt) ×
0.85 system efficiency ≈ 127 Wh ≈ **10.6 Ah/day**. Summer roughly doubles this.

#### 8.2.1 Why the total went *down* despite dropping night mode

Two corrections more than offset it:

| Change | Δ Ah/day |
|---|---:|
| Loop detector: 40 mA placeholder (Reno BX-LP) → **1 mA measured** (Diablo DSP-7LP) | **−0.94** |
| GateLink: day/night split (0.54 + 0.18) → continuous (1.20) | +0.48 |
| BLE BMS polling added | +0.03 |
| **Net** | **−0.43** |

The night profile was buying ~0.36 Ah/day. The detector correction alone is worth
more than twice that. **Dropping the complexity cost nothing.**

### 8.3 Findings — these should shape the design

1. **The LEDs are still the budget.** At ~4 Ah/day they are ~58% of consumption,
   more than every other load combined. Real headroom comes from LED runtime, not
   firmware.
2. **A bigger battery does not fix a harvest deficit — it extends the
   ride-through.** Harvest vs. load is still 10.6 in / 6.9 out, a 1.54× ratio;
   that ratio is a property of the panel and the loads, and the battery does not
   change it. What 100 Ah buys is **cloud tolerance and time to notice**. If
   average winter harvest ever fell below load, a larger battery would only delay
   the failure. State this plainly so the monitoring plan (§8.5) is understood as
   the actual safeguard.
3. **Autonomy is now ~14.5 days** from full at zero harvest, up from ~5. Two
   consecutive overcast days no longer erase the surplus in any meaningful sense.
4. **Firmware power optimization is no longer a design driver.** v0.4 correctly
   warned against buying 5% savings with architectural complexity. At 100 Ah
   usable that argument is decisive: **prefer the simple, always-on, low-latency
   design in every case.** §8.6 acts on this.
5. **The system already works.** The existing install has run for years without
   the gate node. LRAN adds ~1.25 Ah/day, roughly 18% of the new total, against
   an autonomy figure that nearly tripled.

### 8.4 Low-temperature charge inhibition — **new in v0.5**

The pack's BMS blocks charging below approximately 0 °C. This is correct
behaviour and is accepted; the reserve covers it. But it must be **observable**,
because its symptom — a battery that is not recharging on a sunny day — is
otherwise indistinguishable from a failing panel or a failing MPPT.

**Autonomy check.** ~14.5 days of reserve comfortably exceeds any realistic
western-NC sub-freezing spell, where daytime highs typically rise above freezing
even in a cold snap and restore a charging window each day. This is a monitoring
problem, not a capacity problem.

**Detection without the BMS.** Inferable from VE.Direct alone:

> PV power present **AND** charger state is bulk/absorption **AND** battery
> current ≈ 0 **AND** battery voltage not rising, sustained for
> `charge_inhibit_confirm_s` (default 300)
> ⇒ **charging inhibited**

**Detection with the BMS** (§5.7): read the charge-inhibit / low-temperature
protection flag and the pack temperature directly. Preferred when available.

**Requirements:**
- Publish a `binary_sensor` "Charging inhibited" (device_class `problem`).
- Publish pack temperature when the BMS is reachable.
- Maintain a **cumulative Ah drawn since charging was last active** counter, so a
  multi-day cold event shows consumed reserve rather than just a boolean.
- Note that **upgrading the panel does not help while charging is inhibited** —
  relevant to the §8.5 decision path.

### 8.5 Validation and ongoing monitoring

**Before install:** log the MPPT's own yield history (`H19`/`H20`/`H21`) and
battery voltage minima for one week via VE.Direct. That yields the *actual*
present margin for free.

**After install — this is the plan of record.** Monitor nightly consumption and
overnight recovery over time. The metrics that matter:

| Metric | Source | What it tells you |
|---|---|---|
| Daily yield (H20) | MPPT | Harvest trend, panel/shading degradation |
| Overnight ΔSOC | BMS or SmartShunt | True nightly consumption |
| Daily Vmin | MPPT | Proxy for depth of discharge if SOC is unavailable |
| Days since full | derived | Early warning of a sustained deficit |
| Charging-inhibited hours | §8.4 | Distinguishes cold events from real faults |

**Decision rule for a panel upgrade:** upgrade only if *days-since-full* trends
upward across a season **and** the shortfall is not attributable to
charging-inhibited hours. The system being built is also the instrument that
validates it.

### 8.6 Removed in v0.5 — night profile, RX duty-cycling, extended preamble

v0.4 §8.7 and §8.8 specified a PV-aware day/night profile with MCU light-sleep,
SX1262 hardware RX duty-cycling, and an extended-preamble command wake to keep
latency at ≤2 s. **All of this is removed from GateLink.**

**Rationale:**

| Argument | Detail |
|---|---|
| The saving is noise | ~0.36 Ah/day is **0.36% of 100 Ah usable**. |
| It costs data | Light-sleep means giving up continuous VE.Direct parsing overnight — precisely the nightly consumption data §8.5 now depends on. |
| It costs latency | Continuous RX gives sub-second command latency instead of ≤2 s. |
| It costs complexity | Profile switching, hysteresis thresholds, preamble sizing, and wake-window arithmetic all disappear. |
| The MCU was awake anyway | Daytime already required continuous VE.Direct parsing; the delta from always-on is smaller than it appears. |

**Retires D2, D9, and D10** for GateLink.

**Not deleted — relocated.** The design is preserved in **Appendix A** because
WellLink may be battery powered without a 100 Ah pack behind it (§1.6). The
SX126x node-address filtering in §6.8 is retained for the same reason.

### 8.7 Fixed levers (retained)

- **WiFi disabled** on GateLink (largest single ESP32 saving).
- **BLE duty-cycled**, not resident (§5.7.2).
- OLED / Vext off in normal operation (§5.6).
- Status caching minimizes LoRa TX (the peak consumer).
- Raw BusT4 streaming off by default with auto-expiry (§5.8.2) — this is now the
  largest *discretionary* power and airtime consumer on the node.
- **Branch B:** SN65HVD230 held in standby except while transmitting (§4.2.4.2).

### 8.8 Where the current goes on the node

The **radio is not the constraint.** Per the SX1262 datasheet, LoRa 125 kHz
receive is **4.2 mA** (normal) or **5.3 mA** (Rx-boosted, +3 dB); TX is ~90 mA
@ +14 dBm and ~118 mA @ +22 dBm; sleep with config retained is sub-µA. Continuous
RX adds only ~5 mA on top of an **ESP32-S3 that dominates** at tens of mA while
awake. The floor is set by keeping the MCU awake, and the MCU stays awake to parse
the ~1 Hz VE.Direct stream — which §8.6 now makes a 24-hour condition rather than
a daytime one.

**D10 (Rx-boosted gain) is retired as a power question** — 1.1 mA is 0.026 Ah/day.
If the range test shows any benefit from the +3 dB, **take it**; the power cost is
irrelevant at 100 Ah.

---

## 9. Debug and bench tooling (requirement)

### 9.1 BusT4 sniffing — **moved off the node (v0.5)**

v0.4 specified an integrated BusT4 sniff/monitor mode in the gate firmware. **This
is removed.** Sniffing is an **interactive laptop bench procedure** using the
logic analyzer in §4.1.3 connected through the appropriate signaling interface.

**Why this is better:**

| | Integrated sniffer | Laptop bench sniffer |
|---|---|---|
| Timing fidelity | LoRa-limited, buffered | Microsecond, timestamped |
| **Break visibility** | Hard — UART peripheral abstracts it | **PulseView shows the 519–590 µs break directly** |
| Availability | Requires gate firmware to exist | **Works before any firmware is written** |
| Both directions | Two UARTs on a busy node | Two analyzer channels, trivially |
| Node complexity | Carried forever | None |

**Scheduling consequence — this is a real win.** D3 (BusT4 detector/movement-cause
coverage) can now be resolved **before GateLink firmware exists**. The §5.4
acquisition-path decision and the §5.8.1 status schema both stop blocking on
firmware, and firmware stops blocking on them.

**Procedure:**

1. Complete §4.2.2 pin identification and physical-layer discrimination.
2. Tap the data line(s) **read-only** through the appropriate conditioning
   (BSS138 under Branch A; SN65HVD230 under Branch B). **The analyzer's outputs,
   if any, are never connected to the bus.**
3. Capture while operating the gate by: remote, wall button, Oview if available,
   **exit wand** (drive a vehicle over it), and **safety loops** (vehicle on the
   outside loop, then the inside loop, then both).
4. **Specifically capture `INF_IO` responses with a vehicle on the exit wand and
   on the safety loops, and check whether bits move.** This is the single most
   important capture in the project — it resolves D3.
5. Capture during a full open cycle with the gate then left standing open, to
   determine whether auto-close state is observable (§5.4.5).
6. Characterize wand hold behavior (§5.4.2 item 5).
7. Commit captures and decoded findings to `/docs/bust4-captures/`.

Existing bench procedure notes from prior work are the starting point for this.

### 9.2 Retained on-node debug tooling

- **Packet loopback:** (a) RF loopback — a node echoes received frames to
  validate link + framing without the peer; (b) wired/internal loopback — feed TX
  frames back into the RX parser with no radio.
- **Dummy status push:** synthetic VE.Direct and 1050 status frames exercising
  the full pipeline (node → LoRa → bridge → MQTT → HA) without real hardware.
- **Detection event injection:** synthetic SAFETY and EXIT assertions in
  configurable order and spacing, to test §5.4 direction classification and the
  §5.4.5 held-open alert without driving a car back and forth. **Must cover the
  new long-gap cases** — 30 s EXIT→SAFETY gaps and partial traversals.
- **MQTT as bench harness:** `mosquitto_sub -t 'lran/#'` to watch every decoded
  payload live; `mosquitto_pub` to inject commands or fake status, decoupled from
  HA and the RF link.
- **Device simulators:** a "dummy 1050" mode answering BusT4 commands and
  emitting events, so command logic is exercised **without risking the real
  board**; a VE.Direct frame generator covering **both text and HEX**; a dummy
  BMS BLE peripheral.
- **Multi-node simulator — new:** the bridge must be exercisable with a simulated
  second node before WellLink exists. A `lran-simnode` firmware target that
  registers as node `0x02`, answers polls, and emits events on demand validates
  addressing, per-node keying, availability watchdog, and CAD/backoff (§6.7).
- **Raw BusT4 streaming:** §5.8.2 — the on-node replacement for what the
  integrated sniffer would have provided during *normal operation*, as distinct
  from bench characterization.
- **On-demand gate-node display:** §5.6.
- **Leveled serial logging** on all nodes.

### 9.3 Bench analysis — placeholder

`TBD`. Outstanding work. Prior bench procedure exists as a starting point and
will be folded into a later revision.

---

## 10. Dev environment, repo, and build

```
/firmware/bridge/        # LoRaBridge PlatformIO project
/firmware/gatelink/      # GateLink PlatformIO project
/firmware/welllink/      # WellLink (future)
/firmware/simnode/       # simulated node for multi-node bench testing (§9.2)
/lib/lran-protocol/      # shared framing/addressing/HMAC/CRC/fragmentation
    /schemas/            # versioned per-node payload schemas (§6.4.3)
/lib/bust4/              # ported BusT4 protocol  [GPL-3.0 — see §12]
/lib/vedirect/           # VE.Direct text + HEX (osh-labs port) [MIT]
/lib/bms-ble/            # BLE BMS client (§5.7)
/tools/                  # bench scripts, simulators, MQTT helpers, capture parsers
/ha/                     # example discovery payloads + automations
/docs/                   # this PRD, design notes, measured pinouts,
                         #   bust4-captures/, bms-protocol.md, mppt-config.md,
                         #   protocol-changelog.md
LICENSE                  # GPL-3.0 — D11 resolved, see §12
THIRD_PARTY_NOTICES.md   # see §12
```

- **PlatformIO** multi-environment build (one env per firmware target), VS Code +
  Claude Code.
- **CI:** GitHub Actions building **all** firmware targets on push. With three-plus
  targets sharing `/lib/lran-protocol/`, CI is now the mechanism that catches a
  protocol change breaking a node nobody rebuilt locally.
- **Secrets:** LoRa `master_key` (§6.5.1), WiFi creds, MQTT creds, OTA password
  via untracked config / build flags (never committed).
- **Updates:** bridge OTA + USB (§6.2); remote nodes USB only. Document both
  procedures.
- **Protocol changes** require a version bump and a `/docs/protocol-changelog.md`
  entry (§6.4.5).

---

## 11. Safety

- The **1050 remains the safety authority** — obstruction, photocells, and
  detector logic stay with the operator. LRAN issues commands and *observes*
  detector state; it must never be relied on to prevent unsafe motion. §5.4 is a
  monitoring and notification feature, not a safety feature.
- No automatic close behavior initiated by LRAN beyond explicit HA commands. In
  particular, **the §5.4.5 held-open alert notifies; it does not close the gate.**
- **BusT4 bring-up carries hardware-destruction risk (§4.2).** Complete the pin
  identification procedure (§4.2.2), condition the lines per the selected branch,
  and sniff read-only with the laptop harness (§9.1) before sending anything.
  **GateLink's BusT4 TX line stays physically unwired until §9.1 sniffing has
  validated the pinout, the physical layer, and the frame format.**
- The BusT4 VCC pin carries **24–28 V**. This is why §4.2.2 is a gate on the work
  rather than a suggestion.
- **Fuse the battery tap.** A 100 Ah LiFePO4 delivers far higher short-circuit
  current than the FLA it replaces. Size and place the fuse for the new pack, not
  the old one.
- **MPPT configuration is now a remotely writable, battery-affecting path.** The
  three gates in §6.6.3 are safety requirements, not conveniences. Reconfigure for
  LiFePO4 before first charge (§8.1.2).
- **Do not rely on graceful shutdown.** The pack BMS opens under fault and takes
  terminal voltage to zero (§4.4). Nothing critical may depend on an orderly
  power-down.
- Observe the grounding discipline in §4.5.4 — motor current enters the enclosure
  through the 1050's terminals and should never share a path with signal grounds.

---

## 12. Third-party code and licenses

### 12.1 Inventory

| Component | Source | License |
|---|---|---|
| Nice BusT4 protocol logic | `pruwait/Nice_BusT4`, `xdanik/Nice_BusT4`, `makstech/esphome-BusT4` | **GPL-3.0** |
| BusT4 wiring/quirk references | `karol27/Nice_BusT4_WT32-ETH01`, `bpietroiu/esphome-nice-bidiwifi` | GPL-3.0 (verify each) |
| VE.Direct parser + HEX | `osh-labs/VE.Direct_mppt_arduino` | **MIT — confirmed** |
| LoRa radio driver | RadioLib | MIT |
| BLE stack | NimBLE-Arduino | Apache-2.0 |
| BMS client reference | JBD/JK community implementations — **specific source TBD pending §5.7.4** | **verify before use** |
| Arduino-ESP32 core | Espressif | LGPL-2.1-or-later |
| ESP-IDF components | Espressif | Apache-2.0 |
| mbedTLS (HMAC, HKDF) | via ESP-IDF | Apache-2.0 |
| MQTT client | PubSubClient (§6.1) | MIT |
| JSON | ArduinoJson | MIT |
| Display | U8g2 | BSD-2-Clause |
| Display (alt) | Adafruit GFX / SSD1306 | BSD |
| NVS / Preferences | via ESP-IDF | Apache-2.0 |

> **v0.5:** the VE.Direct library's license was `TBD` in v0.4 and is now
> confirmed **MIT** — compatible with GPL-3.0 and imposing only attribution.
> This matters more than it did, since §6.6 depends on much more of that library.

> **New open item:** the BMS client source is unknown until §5.7.4 identifies the
> protocol family. **Verify its license before vendoring.** A repository with no
> LICENSE file grants no rights, regardless of how permissive the author intended
> to be.

### 12.2 Project license — **D11 RESOLVED: GPL-3.0**

The **entire Nice BusT4 lineage is GPL-3.0.** Porting that protocol logic into
LRAN makes the firmware a derivative work, which must then be distributed under
GPL-3.0. Since the repo will be public on GitHub, "distributed" is true the moment
it is pushed.

**Resolution: license the whole repository GPL-3.0** (v0.4 option 1). For a
personal project this costs nothing real, honors the upstream authors' terms, and
removes the question permanently. The isolated-submodule variant is explicitly
**not** taken — it would preserve only the LoRa protocol library's license for
reuse elsewhere while the combined binary remains GPL-3.0 regardless.

**Scope note from §3.3:** because the bridge now performs BusT4 decoding, `/lib/bust4/`
links into **both** the gate and bridge firmwares. Under a whole-repo GPL-3.0
license this is a non-issue. Under the rejected submodule approach it would have
been an additional complication — a further argument for the resolution taken.

### 12.3 Repo obligations

- `LICENSE` at root: **GPL-3.0**.
- `THIRD_PARTY_NOTICES.md` listing §12.1 with copyright lines. MIT and BSD
  components require attribution retention.
- Nice's own reference documents (TTPCI manual, DMBM integration protocol PDF)
  are Nice-copyrighted: **link them, do not vendor them.**
- Victron VE.Direct protocol documents: link, do not vendor.

---

## 13. Open decisions register

| # | Decision | Status / notes | Resolve by |
|---|---|---|---|
| D1 | LoRa PHY params (SF/BW/CR/TX power) | open — pick after range test at ~500 ft **on both bearings** (§6.8) | Phase 1 |
| D2 | RX duty-cycle period + preamble length | **retired for GateLink** — night mode dropped (§8.6). Moves to Appendix A; reopens only if WellLink is battery powered | — |
| D3 | BusT4 detector/movement-cause coverage vs. GPIO fallback | open — **now resolvable before firmware exists** via laptop sniffing (§9.1). BusT4 path strongly favoured after the §5.4 topology correction | Phase 2 |
| D4 | Poll scheduler location | **resolved** — bridge firmware, **per node**, runtime-configurable via HA `number` | done |
| D5 | MQTT client library | **resolved** — `MqttTransport` abstraction, PubSubClient first, `MQTT_MAX_PACKET_SIZE` ≥1024; espMqttClient as fallback (§6.1) | done |
| D6 | Gate-node display trigger | **resolved** — button toggle + auto-on in any debug mode (§5.6) | done |
| D7 | BusT4 VCC handling | **resolved** — VCC not connected; TX/RX/GND only. Pin identification (§4.2.2) remains a hard prerequisite | done |
| D8 | Entity modeling | **resolved** — `cover` primary + auxiliary entities; position deferred to v2 (§7.2–7.3) | done |
| D9 | PV-aware profile thresholds + hysteresis | **retired** — profile removed (§8.6) | — |
| D10 | Rx-boosted gain on/off | **retired as a power question** (§8.8). Take the +3 dB if the range test shows any benefit | Phase 1 |
| D11 | Project license | **RESOLVED — GPL-3.0**, whole repo (§12.2) | done |
| D12 | VE.Direct isolation vs. level shifting | **resolved** — BSS138 module, no isolator (§4.3) | done |
| **D13** | **BusT4 physical layer: single-ended (Branch A) vs. differential (Branch B)** | **open — resolved by measurement, §4.2.2 step 3.** Branch A expected. Both designs fully specified; hardware for both on hand | Phase 4, before wiring |
| **D14** | **Decode placement: node vs. bridge** | **RESOLVED — hybrid** (§3.3). Trigger-relevant decode local; full expansion on the bridge; raw passthrough opt-in | done |
| **D15** | **Battery SOC source** | **open — BLE BMS preferred; SmartShunt is the selected fallback** (§5.7.4). Sniff with nRF Connect on battery arrival | Before install |
| **D16** | **OTA policy** | **RESOLVED — bridge yes (WiFi, mains, LAN-accessible), remote nodes no** (§6.2) | done |
| **D17** | **Naming** | **RESOLVED** — LRAN umbrella; `lran/` MQTT root; GateLink / WellLink / LoRaBridge nodes (§1.4). Renamed now, before HA entity history exists | done |
| **D18** | **Auto-close observability** | **open** — does the 1050 expose auto-close timer state over BusT4? Determines whether §5.4.5's `STATICALLY_OPEN` definition uses the real signal or the timeout substitute | Phase 2 (§9.1 capture) |
| **D19** | **WellLink power source** | **open** — mains vs. battery/solar. Determines whether Appendix A duty-cycling is needed and whether battery telemetry is required in the WellLink schema (§1.6) | Before WellLink design |

### 13.1 Measurement backlog (tasks, not decisions)

| Item | Section | Blocks |
|---|---|---|
| 1050 Oview jack pinout | §4.2.2 step 1/4 | Phase 4 |
| Data line count (one shared vs. two) | §4.2.2 step 2 | Branch A design detail |
| **Physical layer: single-ended vs. differential** | §4.2.2 step 3 | **D13 — selects Branch A or B** |
| **`INF_IO` bit movement with vehicle on wand / on loops** | §9.1 step 4 | **D3** |
| **Auto-close state observability over BusT4** | §9.1 step 5 | **D18** |
| **Exit wand hold/de-assert behavior** | §5.4.2 item 5, §9.1 step 6 | §5.4.4 timing constants |
| **Real EXIT→SAFETY gap times (drive the vehicle)** | §5.4.4 | `detect_sequence_window_ms` default |
| **BMS BLE GATT map (nRF Connect)** | §5.7.4 | **D15** |
| SN65HVD230 DTO confirmation (Branch B only) | §4.2.4.4 | Branch B viability |
| Module onboard 120 Ω termination (Branch B only) | §4.2.4.5 | Branch B wiring |
| 12 V→USB-C adapter no-load draw | §4.4 | §8 budget |
| 1050 static current | §8.2 | §8 budget |
| DSP-7LP current while detecting | §8.2 | §8 budget (idle 1 mA known) |
| Gate node average current, continuous RX | §8.2 | §8 budget |
| **Range/RSSI on the WellLink bearing** | §6.8 | D1, bridge antenna siting |
| One week MPPT yield + Vmin baseline | §8.5 | install go/no-go |
| **Full BusT4 status set size in bytes** | §5.8.3 | mechanism-1 schema design |

---

## 14. Test plan / bring-up phases

1. **RF link only** — two Heltecs; ping + loopback; RSSI/SNR at ~500 ft **on both
   the gate bearing and the well bearing**. Resolve D1, D10. Site the bridge
   antenna.
2. **BusT4 characterization (bench, laptop)** — §4.2.2 pin identification and
   physical-layer discrimination; **resolve D13**; §9.1 sniffing to resolve **D3**
   and **D18**, characterize the wand, and size the status set. **No LRAN firmware
   required.** This phase can run in parallel with phase 1.
3. **Protocol/framing** — dummy-1050 and VE.Direct simulators (text + HEX);
   detection event injection to validate §5.4 direction logic and §5.4.5 held-open
   alerting, including 30 s gaps and partial traversals. **Multi-node: run
   `simnode` alongside GateLink** to validate addressing, per-node keys,
   availability watchdog, fragmentation, and CAD/backoff (§6.7).
4. **VE.Direct** — real MPPT 75/15 on the bench through BSS138 channels 3–4;
   verify full text field parsing **and HEX request/response round-trip**,
   including write rejection when disarmed or unauthenticated (§6.6.3). Start the
   §8.5 one-week baseline log.
5. **Battery and BMS** — on battery arrival: nRF Connect enumeration (**D15**);
   MPPT reconfiguration for LiFePO4 (§8.1.2) and readback verification; BLE client
   bring-up; low-temp inhibition detection (§8.4).
6. **BusT4 live** — wire the selected branch; **sniff read-only first** (TX still
   unwired, §11); then commands against the dummy 1050; then the real board.
7. **HA integration** — MQTT Discovery entities per device; command round-trip;
   per-node availability; detection/direction entities; **verify the held-open
   event fires exactly once and does not replay on HA restart** (§7.4).
8. **Field** — install, range/power soak, error-path validation, confirm measured
   daily Ah against §8.2, begin §8.5 ongoing monitoring.

---

## Appendix A — Low-power RX duty-cycling (retained design, not used by GateLink)

Removed from GateLink in v0.5 (§8.6), preserved here because **WellLink may be
battery powered** (D19) without a 100 Ah pack behind it.

**PV-aware adaptive profile.** A node reads its charge controller and uses PV
output to select a power profile, with **hysteresis** to avoid dawn/dusk
flapping:

| Profile | Trigger | MCU | Peripherals | LoRa RX | Poll |
|---|---|---|---|---|---|
| Daytime | PV charging | awake | full rate | near-continuous | normal |
| Night/low-PV | PV fallen off | light-sleep | occasional | SX1262 hardware RX duty-cycle | reduced |

**Keeping duty-cycled RX responsive.** RX duty-cycling normally trades latency
for power. Avoid that: the **bridge sends commands with an extended preamble**
long enough to span the node radio's sleep window, so the sleeping receiver
detects the preamble on its next wake. Worst-case latency ≈ one duty-cycle
period, independent of sleep depth.

Reference target from v0.4: worst-case command latency ≤ 2 s with a ~2 s RX
duty-cycle period and a preamble sized to ≥ that period.

**Multi-node caveat, new in v0.5.** An extended preamble is address-agnostic —
**every** duty-cycled node on the channel wakes and receives the header before
discarding a frame not addressed to it. Enable **SX126x hardware node-address
filtering** (§6.8) so the discard happens in silicon rather than costing MCU wake
time on every other node's traffic.

**Do not adopt this design for a node that does not need it.** The v0.5 finding
stands: measured against adequate storage, it buys single-digit percentages of
the budget at a real cost in complexity, latency, and overnight data continuity.

---

## 15. Changelog

- **v0.5** — **Renamed the project to LoRa Remote Automation Network (LRAN)**;
  MQTT root `gatelink/` → `lran/`, one HA device per node, node projects
  GateLink / WellLink / LoRaBridge (**D17**). **Expanded the bridge to a
  general-purpose multi-node LoRa↔MQTT gateway** — per-node addressing and static
  registry, **per-node HMAC keys derived by HKDF from one master** (§6.5.1),
  per-node sequence/`boot_id` tables, **per-node availability watchdog** (§6.4.6),
  per-node poll schedulers, explicit **payload schema IDs** (§6.4.3),
  **fragmentation** (§6.4.4), **protocol version tolerance N/N−1** to make no-OTA
  rollout incremental (§6.4.5), and **CAD + randomized backoff media access**
  (§6.7). Scoped **WellLink** (§1.6) and added a `simnode` bench target (§9.2).
  **Corrected the vehicle detection topology** (§5.4): the installed hardware is a
  **Diablo DSP-7LP** presenting a **single combined safety contact** from two
  parallel loops, plus a **separate exit wand** far inside the gate — not two
  independent loop contacts. Rewrote the classification table, raised
  `detect_sequence_window_ms` from 5 s to **60 s**, added a state machine
  requirement, and **replaced the alert semantics** with a **gate-statically-open**
  condition (§5.4.5) delivered as a **non-retained event** for email/SMS
  automation. BusT4 acquisition is now the primary path; the v0.4 timing objection
  no longer applies. **Added the VE.Direct HEX protocol** (§6.6) — MPPT RX line
  becomes **mandatory** (§4.3), GateLink is transport-only, and writes are gated by
  **HMAC + an armed write-enable switch with auto-expiry + a retained audit trail**
  (§6.6.3). **Replaced the FLA with a 100 Ah LiFePO4** (§8.1): 100% usable,
  autonomy ~5 → **~14.5 days**, daily draw **down** to ~6.9 Ah/day (the DSP-7LP's
  measured 1 mA more than offsets dropping night mode). Added required **MPPT
  LiFePO4 reconfiguration** (§8.1.2) and **low-temperature charge-inhibition
  detection** (§8.4). **Dropped the night/low-PV profile, RX duty-cycling, and
  extended-preamble wake** from GateLink (§8.6), retiring **D2, D9, D10** and
  relocating the design to **Appendix A**; latency improves to sub-second and
  overnight VE.Direct logging becomes continuous. **Enabled BLE on GateLink**,
  duty-cycled, to read the WattCycle BMS (§5.7), with a **SmartShunt** as the
  selected fallback (**D15**) and honest SOC-provenance reporting. **Moved BusT4
  sniffing off the node** to a laptop + logic-analyzer bench procedure (§9.1),
  which unblocks D3 before firmware exists; in exchange added **opt-in raw BusT4
  frame streaming** (§5.8) alongside a compact decoded status struct, with
  **hybrid decode placement** (**D14**, §3.3). **Enabled OTA for the bridge only**
  (**D16**, §6.2). **Resolved D11 — GPL-3.0 for the whole repo**; confirmed the
  VE.Direct library is **MIT**. Added **D18** (auto-close observability) and
  **D19** (WellLink power). Restructured §14 into eight phases, with BusT4
  characterization moved early and parallelizable.
- **v0.4** — Added §1.3 physical installation context: 1050, MPPT, battery, and
  gate node share one enclosure with **no motor inside**; motors are external via
  dedicated 1050 ports. **Resolved D12 on that basis** — worst-case ground offset
  is ~16–40 mV, so the ADUM1201 isolator is dropped; grounding discipline retained
  as hygiene (§4.5.4). Consolidated signal conditioning into new **§4.5** around a
  single **BSS138 4-channel level shifter module** serving BusT4 (2 ch) and
  VE.Direct (2 ch) on one shared 5 V HV rail, with rise-time analysis at 19200
  baud showing three orders of magnitude of headroom, explicit rail-sourcing from
  the Heltec, and a pull-up contingency. Restructured §4.2 into **Branch A
  (single-ended, BSS138, expected)** and **Branch B (differential, SN65HVD230,
  hardware on hand)**. Expanded §4.2.2 into a three-question measurement. Added
  **D13** for the branch selection and dropped multi-drop concerns as out of scope.
- **v0.3** — renamed to **LoRa GateLink**; added naming conventions.
  **Corrected §4.3: VE.Direct is 5 V, not 3.3 V.** Added documented BusT4
  electrical characteristics incl. 519–590 µs UART break and 24–28 V VCC, plus a
  pin identification procedure. Added VE.Direct pinout table and crossover-cable
  warning. Changed power to 12 V→5 V **USB-C** with an off-the-shelf adapter and a
  ≤5 mA quiescent requirement. **Promoted loop detection to a hard requirement**
  with direction-of-travel classification, event-triggered push, and open-gate
  alerting. Expanded §8 to a full gate-side power budget. Added full license
  inventory and the GPL-3.0 consequence (D11). Resolved D5, D6, D7, D8. Added D12
  and a measurement backlog.
- **v0.2** — added BusT4 sniff/monitor as a retained diagnostic (D3); rewrote §8
  around SX1262 RX figures and a PV-aware day/night power profile with
  extended-preamble command wake; poll scheduler fixed to bridge firmware and made
  runtime-configurable via an HA `number` entity (D4); added D9 and D10.
- **v0.1** — initial draft. Architecture settled: two custom Heltec V3 nodes,
  BusT4 (1050) + VE.Direct (MPPT 75/15) on the gate node, LoRa 915 MHz link,
  bridge as LoRa↔MQTT gateway to existing Mosquitto via MQTT Discovery.
