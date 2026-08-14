# Product Requirements Document — LoRa Remote Automation Network (LRAN)

**Working name:** LoRa Remote Automation Network — **LRAN**
**Node projects:** `GateLink` (gate), `WellLink` (well, future), `LoRaBridge` (house)
**Version:** 0.7 (draft, for iteration)
**Status:** Architecture settled. 1050 interface rebuilt on documented I/O (v0.6); BusT4 optional Phase 2 (Appendix B). **GateLink host platform moved to M5Stack StamPLC (v0.7).** PHY parameters and field-measured values open (§13)
**Last updated:** 2026-08-14
**Supersedes:** v0.6

---

## 1. Overview

### 1.1 Purpose

Provide a **property-wide point-to-multipoint LoRa network** linking a Home
Assistant (HA) instance to remote, low-power monitoring and control nodes that
are outside practical WiFi range.

The first node is **GateLink**: a remote, solar-powered driveway gate operator.
HA opens and closes the gate, holds it open on request, reads gate and solar
state, reads and writes charge-controller configuration, and detects and
classifies vehicle traffic through the gate.

A second node, **WellLink**, is planned (§1.7). The network is designed for it
from the outset rather than retrofitted later.

### 1.2 The v0.6 pivot, in one paragraph

v0.1–v0.5 assumed GateLink would talk to the Nice/Apollo 1050 over its **BusT4**
protocol port. Bench probing (§1.5) established three things that undermine that
plan: BusT4 is dead during the 1050's low-power standby, the loop and exit
detectors are **not** readable over the bus when it matters, and a wake mechanism
would be needed regardless. At the same time, probing established that everything
LRAN actually wants is available through the 1050's **documented accessory I/O** —
programmable inputs for commands and two unused programmable relay outputs for
state. **v0.6 therefore builds GateLink on that documented I/O and moves BusT4 to
an optional Phase 2** (Appendix B). The result is simpler, lower-risk, removes a
GPL obligation, and deletes the highest-risk hardware work from the critical path.

### 1.2.1 The v0.7 platform change, in one paragraph

v0.6 rebuilt the 1050 interface on relays and discrete inputs but left GateLink on
the **Heltec LoRa V3**, which has neither. The v0.6 BOM therefore carried an
external 4-channel relay module, resistive dividers with clamp diodes for the two
voltage-sense inputs, and a 12 V→USB-C adapter — a Heltec plus three bolt-on
subassemblies to reach an I/O profile that an industrial controller provides
natively. **v0.7 moves the GateLink host to the M5Stack StamPLC** (Stamp-S3A /
ESP32-S3FN8): 4 relay outputs, 8 opto-isolated 5–36 V inputs, 6–36 V input, DIN
mount, screw terminals throughout. The relay module, the dividers and clamps, and
the USB-C adapter all leave the BOM. The cost is that StamPLC has no radio, so
**LoRa becomes an external SX1262 module on a small carrier board** (§4.7.4) —
which also has to carry a 3.3 V regulator, because StamPLC exposes no 3.3 V rail
(§4.7.3). **LoRaBridge stays on the Heltec V3**; it has no I/O requirement.
Nothing above §4 changes: the protocol, the command model, the detection logic,
the HA entity model and the power budget all carry forward intact.

### 1.3 System summary

One house-side bridge and N remote nodes:

- **LoRaBridge** (house): a **general-purpose LoRa↔MQTT gateway**. Receives and
  sends LoRa to any registered node, connects to the LAN over WiFi, and bridges
  to the existing Mosquitto broker on the HA host. HA entities are created via
  **MQTT Discovery**, one HA device per node. Mains powered; no power constraint.
  Supports OTA update (§6.2).
- **GateLink** (remote, ~500 ft): an **M5Stack StamPLC** (§4.7) with an external
  **SX1262** radio. Interfaces to a Nice/Apollo **1050** control board via
  **dry-contact relay outputs and isolated inputs** (§4.2), and to a Victron
  **MPPT 75/15** via **VE.Direct** (text + HEX). Reads the battery BMS over
  **BLE**. Runs custom firmware. Powered **directly** from the 12 V **100 Ah
  LiFePO4** battery — no 5 V adapter.
- **WellLink** (remote, ~500 ft, different bearing): well level monitoring.
  Scoped in §1.7, specified in a later revision.

```
                       +---------------------+
   +--------------+    |                     |
   |   GateLink   |<-->|                     |
   | StamPLC      |    |                     |
   | + SX1262     |    |     LoRaBridge      |        +-------------------+
   | 4x relay out |    |     Heltec V3       |        |  Home Assistant   |
   | 6x iso. in   |    |                     |        |                   |
   |    <-> 1050  |    |                     |        | (Mosquitto broker,|
   | VE.Direct    |    |  LoRa 915 MHz  <--> |        |  MQTT Discovery)  |
   |   <-> MPPT   |    |  WiFi -> LAN        |------->|                   |
   | BLE <-> BMS  |    |  MQTT -> Mosquitto  |  MQTT  +-------------------+
   | 12V LiFePO4  |    |  OTA capable        |
   +--------------+    |                     |
                       |                     |
   +--------------+    |                     |
   |   WellLink   |<-->|                     |
   |  (planned)   |    |                     |
   +--------------+    +---------------------+
```

### 1.4 Physical installation context (gate)

The **1050 controller, MPPT 75/15, LiFePO4 battery, and GateLink node all share a
single controller enclosure.** The gate motors are mounted externally on the gate
itself and are driven through dedicated motor ports on the 1050 board — **no
motor resides inside the enclosure.**

This matters for three design decisions and is referenced from §4.4, §4.5, and
§11:
- All signal runs (accessory I/O, VE.Direct) are short — inches to a couple of feet.
- Motor current enters the enclosure only via the 1050's own supply and motor
  terminals.
- Cable capacitance on the level-shifted lines is minimal.

It also places the BLE BMS within inches of the node antenna (§5.7) — with the
v0.7 caveat that the StamPLC's 2.4 GHz antenna is internal to its case and has no
external option, unlike the LoRa side (§4.7.5, **D28**).

**New in v0.7 — thermal.** The StamPLC is specified 0–40 °C. This enclosure is
outdoors and sees sub-freezing winter mornings and solar gain in summer. See
§4.7.6 and **D29**; this is the principal risk introduced by the platform change.

### 1.5 Bench findings that drove this revision

All established by direct measurement on the installed board. Recorded here
because they are the evidence base for §4.2 and for Appendix B.

**BusT4 port (6P4C "Oview" jack), measured:**

| Position | Signal | Measured |
|---|---|---|
| 1 | — | NC |
| 2 | VCC | **24 V** |
| 3 | data | 2.5 V |
| 4 | data | 2.5 V |
| 5 | GND | 0 V |
| 6 | — | NC |

Resistance: pin 3 ↔ pin 4 = **145 Ω** powered (standby), **174 Ω** unpowered;
pin 3 ↔ GND and pin 4 ↔ GND both **open** when unpowered.

**Interpretation — the physical layer is differential.** Two independent
single-ended UART lines would show a resistance *to a rail* (the pull-up) and be
effectively open *between each other*. The measurement is the exact inverse: open
to ground on both, finite between them. That is a terminated differential pair.
The 145 → 174 Ω shift across power states also indicates active silicon in
parallel with the terminator — the 1050's own transceiver, whose input impedance
drops when biased. **D13 resolves to differential (SN65HVD230), for Appendix B
purposes only.**

**Standby behavior, measured:**

- Entering standby sheds pin 2 (24 V) and both data pins. **BusT4 is entirely
  unavailable during standby.**
- The **Diablo DSP-7LP loop detector is powered from gated V+** — it is
  unpowered, not merely idle, during standby.
- The **exit wand is powered from ungated V+** and remains live.

**Input wake behavior, measured.** A consistent model emerged:

| Class | Inputs tested | Wakes? |
|---|---|---|
| **Command** — causes an action or state change | EXIT, Guard Station Open (via keypad), FIRE (via keypad), AUX=STEP, AUX=UNLOCK | **Yes** |
| **Conditioning** — qualifies motion already in progress | SAFETY, SHADOW, ENTRAPMENT (shorted directly to GND) | No |
| **Unassigned** | AUX programmed "No function" | No |

Note AUX=UNLOCK causes no motion of its own yet still woke the board, so the
predictor is *is this a command*, not *does this move the gate*.

**Existing controls, as installed:**

| Control | Drives |
|---|---|
| Remote keypad | Guard Station Open (34); secondary relay drives FIRE (32), separate codes |
| Keyswitch + pushbutton | AUX1 (16), programmed STEP — **not really used**, available for repurposing |
| Radio Open/Close (39, 40) | **Free** |
| OUT1, OUT2 relay outputs | **Free** |
| Auto-close | **Enabled**, timeout `TBM` (probably factory default) |

**Existing hold-open mechanism.** The gate is held open using **OPEN and LOCK**,
released by **UNLOCK**, after which auto-close closes the gate. This was
discovered by testing AUX2 programmed to UNLOCK, which woke the board and closed a
gate that had been held OPEN+LOCKED.

**Current draw, indicative only.** Via the battery BMS app: 0.1 A with the 1050
active, nothing displayed in standby. The app truncates or rounds, so this is
loosely consistent with ~17 mA standby and ~100–120 mA active. **`TBM`** — see
§13.1.

### 1.6 Naming conventions

| Thing | Value |
|---|---|
| Repo | `lran` |
| Firmware targets | `lran-bridge`, `lran-gatelink`, `lran-welllink` (future) |
| MQTT topic root | `lran/` |
| Node topic form | `lran/<node>/...` — e.g. `lran/gatelink/...` |
| HA device names | "LoRa Bridge", "GateLink", "WellLink" |
| C++ namespace | `lran` |
| Node IDs | `0x00` bridge, `0x01` gatelink, `0x02` welllink, `0xFF` broadcast |

### 1.7 WellLink scope (forward-looking)

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
- The status schema is **per-node and versioned** (§6.4.3), so WellLink can define
  its own payload without touching GateLink's.

---

## 2. Goals and non-goals

### 2.1 Goals

**Network**
- A single house-side bridge serving **multiple** independent remote nodes over
  one LoRa channel, with per-node addressing, keying, and availability.
- Adding a node requires reflashing only the bridge and the new node — never the
  existing nodes (§6.4.5 version tolerance).

**GateLink — gate control**
- **Momentary open** (§5.3.1, case 1): open the gate, auto-close returns it.
- **Open and hold** (§5.3.2, case 2): open the gate and hold it open until
  instructed to close. The common case — deliveries, contractors, guests.
- **Close**: release the hold, or close immediately.
- All commands issued through the 1050's documented accessory inputs, replicating
  what a human control panel does.

**GateLink — monitoring**
- Gate state (open / closed / moving) from the 1050's programmable relay outputs.
- **Vehicle detection and direction-of-travel classification** from the existing
  safety-loop and exit-wand contacts, tapped directly (§5.4).
- **Alert on vehicle detection while the gate is being held open** (§5.4.5) — the
  primary operational requirement behind §5.4.
- Hard-shutdown / entrapment alarm state.
- Full status from the MPPT 75/15 (all VE.Direct fields), plus **read/write access
  to all MPPT configuration registers** via the VE.Direct HEX protocol, with
  GateLink acting as transport only (§6.6).
- Battery health and state of charge (§5.7).

**Integration**
- Lightweight, native-feeling HA integration (cover + sensor / binary_sensor /
  button / switch entities via MQTT Discovery).
- Built-in bench debug tooling (packet loopback, dummy status pushes, device
  simulators, detector event injection).
- Best-practice repo, build, and dev workflow (GitHub + VS Code + Claude Code).

### 2.2 Non-goals (v1)

- **BusT4.** Moved to optional Phase 2 — Appendix B. Not a v1 requirement, not on
  the critical path, and not required for any goal in §2.1.
- Strong cryptographic security / full replay & spoofing prevention. Command and
  MPPT-write authentication only (§6.5).
- **OTA for remote nodes.** The bridge supports OTA (§6.2); GateLink and WellLink
  are USB-only.
- Persisting a sequence counter across reboots.
- Controlling **multiple gates** or multiple charge controllers. Multiple *nodes*
  are explicitly in scope; multiple gate operators are not.
- **Gate position reporting** (percentage open). Discrete states only — §7.3.
- Partial open, step-by-step, and block/release as HA commands. Available via
  spare 1050 inputs if wanted later (§4.2.5); not in v1.
- Replacing or modifying the 1050's own safety logic. §11.
- Mesh or multi-hop routing. The topology is a star with the bridge at the centre.

---

## 3. System architecture

### 3.1 Nodes

| | LoRaBridge | GateLink | WellLink (planned) |
|---|---|---|---|
| Board | Heltec WiFi LoRa 32 V3 | **M5Stack StamPLC** (ESP32-S3FN8) + external SX1262 (§4.7) | TBD |
| Node ID | `0x00` | `0x01` | `0x02` |
| Firmware | LoRa↔MQTT gateway + decoders | Custom (I/O + interfaces) | TBD |
| Wired interfaces | none functional | 1050 accessory I/O (relays + GPIO), VE.Direct (MPPT) | level sensor |
| Wireless | LoRa + WiFi (BLE off) | LoRa + **BLE (duty-cycled)**; WiFi **off** | LoRa |
| Power | USB-C (mains) | 12 V **LiFePO4** direct to VIN (6–36 V input) | TBD |
| Update | **OTA + USB** | USB only | USB only |
| Display | May stay on | 1.14" LCD; off in normal op; on-demand + auto in debug (§5.6) | TBD |
| Location | House, near LAN | Inside existing controller enclosure (§1.4) | At the well |

### 3.2 Shared code

All nodes link a common **LoRa protocol/packet library** (framing, addressing,
HMAC, CRC, sequence handling, fragmentation) so the wire format is defined and
tested once. This shared library is the **fleet-wide contract** and is what the
bench tools and simulators exercise.

Because remote nodes have no OTA, this library carries a **compatibility
obligation** (§6.4.5): the bridge must accept protocol version *N* and *N−1*, so
a protocol revision can be rolled out node-by-node rather than in a single
flag-day flash of every device on the property.

### 3.3 Decode placement — simplified from v0.5 (D14)

v0.5 specified a hybrid split of BusT4 decoding between node and bridge. With
BusT4 out of v1, that complexity disappears: **GateLink reads discrete GPIO
states and drives discrete relays.** There is nothing to decode.

What remains:

| Layer | Runs on | Why |
|---|---|---|
| Debounce, edge detection, direction classification, hold-state tracking | **GateLink** | Sub-second local decisions; a LoRa round trip per edge is not viable |
| VE.Direct text parsing | **GateLink** | Continuous 1 Hz stream; only the cached snapshot is transmitted |
| VE.Direct HEX register interpretation | **LoRaBridge** | Bridge is mains powered, OTA-capable, in the house. GateLink is transport only (§6.6) |
| Entity mapping, discovery config | **LoRaBridge** | Changes often; must not require a walk to the gate |

**D14 resolved: GateLink is a discrete I/O device plus a VE.Direct transport.**

### 3.4 Data flows

- **Command (HA → node):** HA publishes to an MQTT command topic → bridge builds
  an authenticated LoRa command frame addressed to the node → node verifies HMAC
  and sequence, pulses the appropriate relay → node returns a command-ACK frame →
  bridge publishes result. The relay pulse also wakes the 1050 (§4.2.3).
- **Status (node → HA):** each node caches its latest state and transmits on (a)
  poll request, (b) a locally significant state change, or (c) an event. For
  GateLink: gate state change on OUT1/OUT2, non-zero VE.Direct charger error code,
  **vehicle detection event** (§5.4), hard-shutdown alarm, or BMS alarm.
- **MPPT config (HA → MPPT):** HA publishes a HEX request → bridge wraps it →
  GateLink transports it verbatim to the MPPT → response transported back →
  bridge publishes it. **Writes require HMAC and an armed write-enable switch**
  (§6.6).
- **Poll:** per-node poll scheduler on the bridge, each node's interval
  runtime-configurable via its own HA `number` entity (§6.3).

### 3.5 Platform convergence with AquaLink — new in v0.7

AquaLink, the water-system controller project, independently selected the StamPLC.
With v0.7 the two share a host platform. This is a **platform and framework**
convergence, not a feature one — the two nodes share almost no application
behaviour.

**Shared:** the platform HAL (relay and input abstraction over the AW9523B, LCD,
buttons, buzzer, INA226, LM75, RTC, SD logging); the `lran-protocol` library and
the SX1262 driver; configuration, persistence and logging; the debug tooling in
§9.2; the PlatformIO environment and CI.

**Not shared:** AquaLink is mains-powered indoors and publishes MQTT over WiFi
directly. GateLink publishes over LoRa and is bridged.

**Requirement.** `/lib/lran-platform/` (§10) SHALL abstract the host so that both
node applications compile against the same HAL, and the **radio pin map SHALL be
injected by configuration rather than hardcoded**, so one SX1262 driver serves both
the Heltec LoRaBridge (fixed internal pins, 1.8 V TCXO, DIO2 RF switch) and the
StamPLC carrier (§4.7.4).

**Useful side effect:** because the transport is abstracted, GateLink can be
bench-exercised over WiFi/MQTT with the radio entirely out of the loop — a
strictly easier bring-up path than v0.6 had.

---

## 4. Hardware and BOM

### 4.1 Bill of materials

#### 4.1.1 System

| Qty | Item | Notes |
|----:|------|-------|
| 1+ | Heltec WiFi LoRa 32 V3 (US 915 MHz variant) | ESP32-S3, SX1262, 0.96" OLED. **LoRaBridge and bench/simnode targets.** No longer used for GateLink — see §4.1.2 |
| 2+ | 915 MHz antennas | one per node |

#### 4.1.2 GateLink

| Qty | Item | Notes |
|----:|------|-------|
| 1 | **M5Stack StamPLC (K141)** | Host platform, §4.7. 4 relays, 8 opto-isolated inputs, 6–36 V in, DIN, screw terminals. ~$43 |
| 1 | **SX1262 module, 915 MHz, SPI** | Must use **DIO2 RF-switch control**. Waveshare Core1262-HF class. **Exclude PA modules needing separate TXEN/RXEN** — the pin budget has no room (§4.7.2) |
| 1 | SMA bulkhead pigtail + 915 MHz antenna | LoRa antenna **outside** the enclosure |
| 1 | **3.3 V LDO, ≥300 mA, dropout ≤300 mV** — AP2112K-3.3 breakout or equiv. | **Required.** StamPLC exposes no 3.3 V rail and EXT_5V sits at ~4.76 V under load (§4.7.3). **An AMS1117-3.3 will not do** — ~1.1 V dropout |
| 1 | Carrier board / perfboard + 2×8 2.54 mm header | Carries the SX1262, the LDO and the VE.Direct front end onto the StamPLC-Bus (§4.7.4) |
| 1 | **BSS138 4-channel bidirectional level shifter module** | Adafruit #757 / SparkFun BOB-12009 or equivalent. **VE.Direct only** (2 channels used, 2 spare). Retained — but see **D25**: the TX direction is contingent on measurement (§4.4) |
| *0–1* | *ADuM1201 breakout **or** 74LVC1G17 buffer* | **Only if D25 shows a weak low-side driver** on the MPPT TX line (§4.4) |
| 1 | VE.Direct cable / JST-PH 2.0 4-pin pigtail | to the MPPT VE.Direct port. **Both data lines used** (§4.4) |
| 1 | **12 V 100 Ah LiFePO4 battery** — WattCycle 100 Ah mini w/ Bluetooth | §8.1 |
| — | Mounting, strain relief, **inline fuse on the battery tap** | inside existing enclosure |
| *0–1* | *SN65HVD230 CAN transceiver module* | **Appendix B only** — already on hand; not needed for v1. Note Appendix B may not need it at all (§B.5) |
| *0–1* | *Victron SmartShunt 500 A* | **Contingency only** (§5.7.4) |

**Removed from the v0.6 BOM by the platform change:** the 4-channel relay module
(StamPLC has four relays), the divider resistors and clamp diodes for IN5/IN6
(StamPLC's inputs accept 5–36 V natively, §4.2.4), the 12 V→USB-C adapter and its
cable (StamPLC takes 6–36 V directly, §4.5), and the Heltec V3 for this node.

**Added:** the StamPLC, the SX1262 module and antenna, the LDO, and a carrier
board. Net part count is roughly flat; net *assembly* count is lower, and the
number of hand-built discrete circuits drops to zero.

No separate enclosure is required — GateLink mounts inside the existing
controller enclosure (§1.4).

**Existing installed hardware, not purchased:** Nice/Apollo 1050 control board,
Victron MPPT 75/15, 50 W panel, **Diablo DSP-7LP** loop detector with two safety
loops, self-contained exit wand sensor, remote keypad, keyswitch + pushbutton,
2 × 2 W LED lights.

> **BOM trajectory.** v0.5 required the SN65HVD230 *or* two more BSS138 channels
> for BusT4, plus a bench logic analyzer as a project-critical item. v0.6 replaced
> that with a relay module. **v0.7 absorbs the relay module, the dividers and the
> power adapter into the host** and spends the saving on a radio carrier. The logic
> analyzer remains useful for VE.Direct HEX bring-up but is not on the critical path.

#### 4.1.3 Bench / debug (§9)

| Qty | Item | Notes |
|----:|------|-------|
| 1 | USB-TTL serial adapter (5 V / 3.3 V selectable) | VE.Direct bring-up and observation |
| *1* | *8-channel USB logic analyzer (sigrok/PulseView)* | Optional. Useful for VE.Direct HEX timing; **required only if Appendix B is pursued** |
| — | Jumper leads, breadboard, DVM | |

### 4.2 1050 accessory interface — **the core change in v0.6**

GateLink drives and reads the 1050 exclusively through terminals the manufacturer
documented for third-party use. Nothing here is reverse-engineered, nothing
touches a 24 V rail, and nothing depends on the bus being awake.

#### 4.2.1 Design principle

**LRAN presents itself to the 1050 as a control panel.** Every command is a
momentary dry-contact closure on an input the 1050 already expects a human device
to drive; every status read is a dry contact the 1050 already provides for
signalling accessories. This is the interface Nice designed for exactly this
purpose.

Two consequences worth stating explicitly:

- **All GateLink outputs are momentary pulses.** No maintained assertions
  anywhere. There is no relay state that, if welded or stuck, holds the gate in an
  abnormal condition. Hold-open is a **latched state inside the 1050**, set and
  cleared by pulses, not a wire held down.
- **All existing human controls are paralleled, never replaced.** Adding a dry
  contact in parallel with an existing one costs nothing and preserves every
  manual path — keypad, keyswitch, remote, front panel.

#### 4.2.2 Output map — relays

| Relay | Terminal | Programmed as | Function |
|---|---|---|---|
| **K1** | AUX1 (16) | **OPEN and LOCK** | Case 2 — open and hold (§5.3.2) |
| **K2** | AUX2 (18) | **UNLOCK** | Release hold; auto-close then closes |
| **K3** | Guard Station Open (34) | *(fixed function)* | Case 1 — momentary open (§5.3.1) |
| **K4** | Guard Station Close (36) | *(fixed function)* | Immediate close (§5.3.3) |

Pulse width: `relay_pulse_ms`, default **300**. All contacts wired
**normally-open**, so a de-energized or unpowered GateLink asserts nothing.

**v0.7 — these are now the host's own relays.** K1–K4 are StamPLC's four onboard
relays (SPDT, COM/NO/NC, DC 5 A @ 28 V), driven through the **AW9523B I²C
expander**, not by direct GPIO. Three consequences:

- Pulse timing is generated in firmware over I²C rather than by a GPIO write. At a
  300 ms pulse width, I²C latency is irrelevant, but the platform HAL owns the
  timing and must not be interrupted mid-pulse by a blocking BLE or VE.Direct call.
- The relays are dry contacts on screw terminals, so §4.2.1's "present as a control
  panel" principle is unchanged and the opto-isolation the v0.6 relay module
  provided is no longer needed — there is no shared logic rail to isolate.
- **The de-energized-asserts-nothing property is preserved**, including through the
  LiFePO4 BMS-disconnect case in §4.5.

**AUX reprogramming.** AUX1 currently reads STEP and is wired to a keyswitch and
pushbutton that are not in use. Reprogramming AUX1 to OPEN+LOCK and AUX2 to
UNLOCK frees nothing else and consumes no additional terminals. **The Radio
Open/Close inputs (39, 40) remain free** and are held in reserve — tying them
together yields step-by-step operation if a physical SBS control is ever wanted.

**Recommended: keep the keyswitch and pushbutton, repurposed.** They are already
wired and cost nothing to retain:

| Control | Move to | Becomes |
|---|---|---|
| Pushbutton | AUX1 (16) | Manual "open and hold" — press when a contractor arrives |
| Keyswitch | AUX2 (18) | Manual release |

This also supplies the manual recovery path required by §4.2.6.

#### 4.2.3 Wake — solved by construction

The 1050 wakes on command-class inputs (§1.5). Every relay in §4.2.2 drives a
command-class input, so **the pulse that carries the command is also the pulse
that wakes the board.** No dedicated wake mechanism, no wake relay, no
fail-safe-STOP wiring, and no dependency on standby configuration.

This retires the entire wake-strategy problem that dominated v0.5's late review.
It also means **standby can stay enabled** at whatever timeout is chosen, keeping
the ~2 Ah/day that disabling it would have cost (§8.2.1).

Latency note: the board needs a short interval to wake and act. Allow
`post_wake_settle_ms` (default **500**) after a pulse before evaluating OUT1/OUT2
state, and do not conclude a command failed until at least
`command_confirm_timeout_s` (default **5**) has elapsed.

#### 4.2.4 Input map — GPIO

| GPIO | Source | Configuration | Reads |
|---|---|---|---|
| **IN1** | OUT1 relay | Programmed **OPEN** | Gate is fully open |
| **IN2** | OUT2 relay | Programmed **MOVING** | Gate is in motion |
| **IN3** | Diablo DSP-7LP contact, paralleled | — | Safety loops (either loop) |
| **IN4** | Exit wand contact, paralleled | — | Exit wand |
| **IN5** | FIRE terminal (32), sensed | — | Human held the gate open via keypad |
| **IN6** | Alarm output, **divided** | — | Hard shutdown / entrapment latch |

**Gate state derivation** from IN1 and IN2:

| IN1 (OPEN) | IN2 (MOVING) | State |
|---|---|---|
| 0 | 0 | Closed |
| 0 | 1 | Moving (closing, or opening from closed) |
| 1 | 1 | Moving (closing from open) |
| 1 | 0 | Open |

Direction of travel during motion is inferred from the previous stable state,
which GateLink tracks. This is sufficient for §7.2's `cover` entity; position
percentage remains out of scope (§7.3).

**Electrical notes — substantially simplified in v0.7.**

All six inputs land on StamPLC's **opto-isolated 5–36 V DC channels** (EL3H4,
read via the AW9523B). Eight are available; six are used.

- **IN1–IN4 are dry contacts.** Wire in the high-level configuration: `EXCOM_COM`
  to the 12 V supply negative, each `INPUT` fed from 12 V+ through the contact.
  No internal pull-ups, no GPIO-referenced grounds. Debounce in firmware
  (`input_debounce_ms`, default 50).
- **IN5 and IN6 remain voltage sense — and this is now trivial.** The v0.6 design
  required a resistive divider plus a 3.3 V clamp per channel because a bare GPIO
  cannot survive 12 V. **StamPLC's inputs are specified for 5–36 V, so both connect
  directly.** The dividers, the clamps and the "do not connect either directly"
  caution are all **withdrawn**. Metering both before wiring is still required
  (§13.1) — to establish sense polarity and idle state, not to size a divider.
- The 1050's OUT1/OUT2 provide **common, NO and NC** contacts. Using NO keeps the
  logic straightforward, but see §4.2.7 — the NC contact may be the better choice
  once standby behavior is measured.

**Acquisition is by polling, not interrupt.** The AW9523B's INT line is shared with
the other onboard I²C peripherals, and §4.7.2 spends that pin on the radio.
GateLink SHALL poll the input expander at `input_poll_ms` (default **10**, max
**20**). This is sufficient by a wide margin: §5.4.4's EXIT→SAFETY discrimination
operates on windows of hundreds of milliseconds to tens of seconds, and
`input_debounce_ms` is 50 ms — five poll intervals. The poll loop must not be
blocked by BLE or VE.Direct work; see §5.1.

#### 4.2.5 Spare capacity

Deliberately unspent, recorded so future revisions know what is available:

| Terminal | Status | Possible use |
|---|---|---|
| Radio Open (39), Radio Close (40) | Free | SBS if tied together; or two more command inputs |
| Guard Station Stop (35) | Free (jumpered to GND) | Stop command, if wanted (see §4.2.6 caution) |
| SHADOW / LOOP1 (24), ENTRAPMENT / LOOP2 (26) | Likely free — **`TBM`**, §13.1 | Additional detection inputs |
| EDGE (28) | In use or free — **`TBM`** | — |
| BSS138 channels 1–2 | Free | Second VE.Direct pair if a SmartShunt is added (§5.7.4) |
| BusT4 port | Untouched | Appendix B |

**Stop is not implemented in v1.** It is available, but Guard Station Stop is
normally-closed and asserting it disables all other inputs for the duration. If
added later, wire the relay's **NC** contacts in series with the existing
STOP–GND jumper so that a de-energized relay leaves the circuit intact and every
failure mode lands safe.

#### 4.2.6 Failure modes and manual recovery

| Failure | Result | Recovery |
|---|---|---|
| GateLink loses power or crashes | All relays de-energize. No input asserted. Gate behaves exactly as it does today. | None needed |
| GateLink crashes **while the gate is held OPEN+LOCKED** | Gate stays open and locked. Plain STEP does **not** override a lock — only STEP H does. | **A manual UNLOCK path must exist** — see below |
| Welded relay contact | A single sustained closure on a command input. Repeated open commands on an already-open gate; repeated unlock on an unlocked gate. Undesirable but not hazardous. | Pull the relay module |
| 1050 in standby, command issued | Pulse wakes the board and executes (§4.2.3) | None needed |

**Manual UNLOCK is a requirement, not a nicety.** Because LRAN will enter the
OPEN+LOCKED state far more often than a human does today, there must be a way to
clear it without GateLink. Acceptable options, at least one required:

1. The **keyswitch relocated to AUX2 = UNLOCK** (§4.2.2, recommended).
2. A remote channel programmed to **UNLOCK** or **CLOSE and UNLOCK**.
3. A spare radio channel programmed to **STEP H**, which overrides a lock.
4. Front-panel operation.

Record which is provided in `/docs/1050-config.md`.

#### 4.2.7 Open question — OUT relay behavior in standby

**`TBM`, §13.1.** Whether OUT1/OUT2 hold their state when the board sleeps is
unknown; a relay coil costs power and the board may drop it.

The exposure is narrower than it looks. Standby engages only when the gate is not
moving *and* not in an auto-close countdown. With auto-close enabled, an open gate
always has a countdown running — so **the board can normally only sleep with the
gate closed**, where OUT1 de-energized reads "not open," which is correct.

Two cases sit outside that:

- **LRAN holding the gate OPEN+LOCKED.** LOCK defeats auto-close, so the board may
  sleep with the gate open. GateLink knows it commanded the lock, so it can trust
  its own state over IN1 here.
- **A human holding the gate open via the keypad FIRE code.** Covered by IN5.

**Mitigation if the relays do drop:** program OUT1 to **CLOSE** instead of OPEN and
read the **NC** contact, inverting the sense so the de-energized state reads
"open." Decide after measurement.

### 4.3 What happened to BusT4

Not deleted — **relocated to Appendix B** as an optional Phase 2 enhancement,
with all bench findings preserved. It is not required for any v1 goal.

Removed from the critical path as a result: the safety-critical bring-up
procedure, the Branch A/B physical-layer analysis, the pin-identification gate,
raw frame streaming, the laptop sniffing phase, and decisions D3, D13, D18. See
§15 for the full list and §12.2 for the licensing consequence.

### 4.4 VE.Direct electrical — unchanged from v0.5

> **Correction retained from v0.3:** earlier drafts stated VE.Direct is 3.3 V TTL
> requiring no level shifting. **This is incorrect.** All Victron MPPTs are 5 V
> devices.

VE.Direct is a **5 V TTL UART, 19200 baud**, on a **JST-PH 2.0 4-pin** connector.
Pinout is vendor-documented, with signal names given from the **device's**
perspective:

| Pin | Signal (device POV) | LRAN connection |
|---|---|---|
| 1 | GND | GateLink GND |
| 2 | RX (into MPPT) | BSS138 ch 1 → GateLink TX — **required** for HEX (§6.6) |
| 3 | TX (out of MPPT) | BSS138 ch 2 → GateLink RX — **required** |
| 4 | V+ | **Do not connect** |

Both data lines are mandatory: HEX is a request/response protocol and cannot
function without the MPPT RX line.

> **v0.7 addition — the 5 V TX driver is weak, and this may invalidate the BSS138
> on the TX direction (D25).** Victron's own solar-charger documentation specifies
> the TX port as a logic 5 V signal driving **at most a 22 kΩ load, at which point
> the output has already fallen to 3.3 V**. That implies an effective source
> impedance around **10–11 kΩ**. A BSS138 translator presents a 10 kΩ pull-up to
> 5 V on the HV node; against a comparably weak driver the low level lands near
> **2.4 V**, the FET never sees a valid gate-source condition, and the LV side stays
> permanently high. The failure mode is silent — no framing errors, simply no data.
>
> This is contingent on whether the driver is symmetric. Many outputs are strong-low
> and weak-high, in which case the BSS138 is fine and this note is moot. It is
> cheap to settle:
>
> **Measurement (D25, §13.1).** Put 10 kΩ from the MPPT TX pin to GND with the port
> streaming and observe the **low** excursions, preferably on a scope.
>
> | Result | Meaning | Action |
> |---|---|---|
> | Low ≈ 0–0.5 V | Strong low side | **BSS138 as planned.** No BOM change |
> | Low ≈ 2–2.5 V | Weak symmetric driver | BSS138 cannot translate this direction. Use an **ADuM1201** (CMOS input, unloads the driver, isolation as a bonus) or a **74LVC1G17** buffer at 3.3 V |
>
> **The BSS138 stays for the RX direction regardless** — a strong 3.3 V output into
> a high-impedance MPPT input is exactly what that topology handles well. A split
> solution is acceptable and is the expected outcome if the measurement goes badly.
>
> Note also that whether a 5 V-family MPPT reliably reads 3.3 V as a logic high on
> its RX input is **not documented by Victron** and has been asked on their own
> community forum without answer. §14 phase 4 must prove the HEX round-trip, not
> assume it.

Implementation notes:

- **Galvanic isolation is not required.** Given §1.4 — no motor inside the
  enclosure, all devices sharing one box, short heavy battery leads — the
  worst-case ground offset is on the order of **16 mV** (2 ft of 12 AWG at 5 A) to
  ~40 mV for lighter wire. Against a 5 V logic threshold this is noise. **D12
  resolved: BSS138, no isolator.**
- **Wire colors are actively misleading.** VE.Direct cables are crossover cables;
  red may be GND and black may be V+, and the two data conductors differ in
  meaning between the cable's ends. **Meter every conductor.** Buying a genuine
  Victron cable and cutting it is the easiest sourcing path.
- Community sources disagree about whether pin 4 is V+ or GND on some units. Moot
  since pin 4 is unconnected, but a further argument for metering first.
- Rise-time analysis: BSS138 is open-drain with 10 kΩ pull-ups, RC-limited. At
  ~20 pF (module plus short trace, which is this install) rise time is ~440 ns
  against a 52 µs bit period — **0.85%**, three orders of magnitude of headroom.
  If a far-end edge looks lazy during bring-up, parallel additional pull-ups to
  bring the effective resistance to 2.2–4.7 kΩ.
- **Rail sourcing — changed in v0.7.** HV rail from StamPLC's **EXT_5V** (nominal
  5 V, **~4.76 V measured under load**, §4.7.3); LV rail from the **carrier board's
  3.3 V LDO**, because StamPLC exposes no 3.3 V. Both Victron device power pins stay
  unconnected. Do not attempt to power the front end from the VE.Direct V+ pin — it
  is limited to roughly 10 mA average.
- **If a SmartShunt is added** (§5.7.4) it presents a second VE.Direct port
  requiring BSS138 channels 3–4 and a third hardware UART. The ESP32-S3 has three;
  with BusT4 gone, two are free.

### 4.5 GateLink power source — simplified in v0.7

**12 V LiFePO4 (100 Ah)** → inline fuse → **StamPLC VIN terminal block**. The
intermediate 12 V→5 V USB-C adapter is **deleted**: StamPLC accepts DC 6–36 V
directly, comfortably spanning the LiFePO4 range from BMS-cutoff to ~14.6 V
absorption.

| Requirement | Value | Rationale |
|---|---|---|
| Input range | 6–36 V (spec) | LiFePO4 operating range sits mid-band |
| Termination | screw terminal (VIN/GND) | Barrel jack also available; terminal block preferred |
| Fusing | inline on the battery tap | unchanged from v0.6, §11 |
| Onboard measurement | **INA226 on VIN** | GateLink can now report its own supply voltage and current with no added hardware — see §8.5 |

**Three consequences worth noting.** One connector, one converter and one cable
leave the design. The adapter's ~0.12 Ah/day overhead leaves the power budget
(§8.2). And the INA226 makes the node's own consumption a **measured, published
telemetry value** rather than a `TBM` row — which closes several §13.1 items by
construction once the node is running.

> **LiFePO4-specific note, unchanged.** The pack BMS can disconnect the load under
> fault, taking terminal voltage to **zero** rather than sagging. GateLink will drop
> dead and reboot when the BMS re-closes. Nothing may depend on a clean shutdown or
> on RAM surviving a power event. Benign for the 1050 interface: all relays
> de-energize and assert nothing (§4.2.6).

> **LiFePO4-specific note.** The battery's BMS can disconnect the load under
> fault. Unlike an FLA, which sags, a LiFePO4 pack goes to **zero volts at the
> terminals** when the BMS opens. GateLink will not brown out gracefully; it will
> drop dead and reboot when the BMS re-closes. Nothing must depend on a clean
> shutdown sequence, and nothing critical may live in RAM across a power event.
> Note this is benign for the 1050 interface: all relays de-energize, asserting
> nothing (§4.2.6).

### 4.6 Detector power — **new requirement**

**The Diablo DSP-7LP must be moved to unswitched power.** It is currently on gated
V+ and is therefore unpowered during standby, which means:

- The safety loops provide no detection at all while the board sleeps.
- **§5.4.5 is unimplementable as things stand** — the gate sitting open is
  precisely when the board sleeps and the detector is dark.
- It matters not at all how GateLink acquires the contacts; GPIO is as dead as the
  bus if the detector has no power.

**Two acceptable sources:**

1. **Terminals 10 and 11.** Power remains present there during standby, and the
   manual recommends them for accessories in standby applications. Terminal 11
   tracks the highest incoming voltage — ~13.5 V from the battery. **Verify against
   the DSP-7LP's input range.**
2. **The fused battery tap that feeds GateLink.** Preferred: fully decoupled from
   1050 state, and unaffected by any future change to the board's configuration.

Cost: ~1 mA continuous, **0.02 Ah/day**.

**Secondary benefit, independent of LRAN.** Inductive loop detectors need time to
tune after power-up. At present the Diablo powers up cold at the same moment a
gate cycle begins. Continuous power removes that window. The exit wand is already
on ungated V+ and needs no change.

### 4.7 GateLink host platform — StamPLC (new in v0.7)

#### 4.7.1 Why this platform

v0.6 established the I/O requirement precisely: **four momentary relay outputs and
six inputs, two of which are 12 V voltage-sense.** The StamPLC meets that natively.

| v0.6 requirement | Heltec V3 | StamPLC |
|---|---|---|
| 4 × dry-contact relay out | external relay module | **native**, SPDT, screw terminals |
| 4 × dry-contact in | GPIO + pull-ups | **native**, opto-isolated |
| 2 × 12 V voltage-sense in | divider + clamp per channel | **native** (5–36 V inputs) |
| 12 V supply | 12 V→USB-C adapter | **native** (6–36 V VIN) |
| DIN mount, screw terminals | none | **native** |
| LoRa radio | **native (SX1262)** | **external module required** |

The trade is one subsystem in exchange for three. It also aligns GateLink with
AquaLink (§3.5) and eliminates every hand-built discrete circuit from the design —
a long-standing project preference.

| Item | Value |
|---|---|
| Product | M5Stack StamPLC, SKU K141 |
| Module | Stamp-S3A — **ESP32-S3FN8, 8 MB flash, no PSRAM** (same SoC family as the Heltec V3) |
| Relays | 4 × SPDT, COM/NO/NC, AC 5 A @ 250 V / DC 5 A @ 28 V, via AW9523B |
| Inputs | 8 × opto-isolated (EL3H4), DC 5–36 V, via AW9523B |
| Supply | DC 6–36 V; DC5521 barrel + VIN/GND/COM terminal block |
| Expansion | StamPLC-Bus (2×8, 2.54 mm), PORT.A and PORT.C (HY2.0-4P) |
| Fieldbus | PWR-485 (SIT3088), PWR-CAN (SIT1044) — neither used in v1; see §B.5 |
| HMI | 1.14" ST7789v2 LCD (135×240), 3 user buttons, buzzer, RGB LED (via PI4IOE5V6408) |
| Onboard sensing | **INA226** (VIN V/I), LM75 temperature, RX8130CE RTC |
| Storage | microSD |
| Mounting | DIN rail |
| Operating temp | **0 – 40 °C** — see §4.7.6 |
| Consumption | 12 V: **15.22 mA standby, 47.84 mA working** (vendor spec) |

Because relays, inputs, buttons and RGB all sit behind I²C expanders, they consume
**no general-purpose GPIO** — which is what makes §4.7.2 feasible.

#### 4.7.2 Pin budget

Documented StamPLC-Bus pinout:

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | VIN | 2 | GND |
| 3 | GND | 4 | GND |
| 5 | GND | 6 | **EXT_5V** |
| 7 | G15 (SCL) | 8 | G13 (SDA) |
| 9 | G3 (PHY_RST) | 10 | G14 (INT) |
| 11 | G9 (SPI MISO) | 12 | G7 (SPI SCK) |
| 13 | G8 (SPI MOSI) | 14 | G11 (SPI CS) |
| 15 | **G40 (custom)** | 16 | **G41 (custom)** |

HY2.0-4P: **PORT.A** = GND / 5 V / G2 / G1; **PORT.C** = GND / 5 V / G5 / G4.

**Allocation:**

| Function | Signals |
|---|---|
| SX1262 SPI | G7 SCK, G8 MOSI, G9 MISO, G11 NSS |
| SX1262 BUSY | G41 |
| SX1262 DIO1 | G40 |
| SX1262 NRESET | **G14**, freed by the polling decision in §4.2.4 |
| VE.Direct UART | PORT.A — G1, G2 |
| I²C (onboard peripherals) | G15 / G13, already in use |
| **Spare** | PORT.C (G4, G5) — reserved for a second VE.Direct pair if a SmartShunt is added (§5.7.4) |

**Reserved — do not use:**

| Signal | Reason |
|---|---|
| **G3** | Common RST for the LCD, the PI4IOE expander **and** any StamPLC-Bus expansion module. A radio driver that pulses reset here also resets the display and an expander — and therefore risks disturbing relay state. **The SX1262 reset must not be wired to G3.** M5's own PoE module documentation flags this same hazard and recommends passing `-1` for the reset pin |
| **G0** | RS485_TX and the ESP32-S3 BOOT strap. Unused by GateLink; if PWR-485 is ever used, keep the bus idle-high |
| G42 / G43 | PWR-CAN. Unused in v1; see §B.5 |

*Fallback if G14 is later needed:* pull SX1262 NRESET to 3.3 V and pass
`RADIOLIB_NC`, accepting software-reset-only. Not preferred for an outdoor node.

#### 4.7.3 Power rails — **no 3.3 V is exposed**

Confirmed across current M5 documentation:

- StamPLC-Bus power pins are **VIN, GND and EXT_5V only**.
- The StamPLC PoE accessory documentation independently republishes the same bus
  map, naming pin 6 `EXT_5V`.
- Both HY2.0-4P ports are GND / 5 V / GPIO / GPIO.
- M5's own SPI expansion module (PoE, carrying a W5500 — a 3.3 V part) is specified
  for PoE / DC 12 V / DC 5 V input and **regulates its own 3.3 V locally.**

Rail capacity, vendor-specified under load: **expansion port 4.76 V @ 700 mA**,
HY2.0-4P **4.81 V @ 700 mA**. Note the rail sits near 4.8 V, not 5.0 V.

**Requirement.** The carrier board SHALL provide a local 3.3 V regulator serving
the SX1262 and the VE.Direct LV rail. Given a 4.76 V source, an **AMS1117-3.3 is
excluded** (~1.1 V dropout). Specify ≤300 mV dropout — AP2112K-3.3 or equivalent.
Load: ~120 mA peak SX1262 TX at +22 dBm plus the level-shifter rail; 700 mA of
headroom is ample.

> **Verification status.** This is confirmed at documentation level across multiple
> official sources, **not** at netlist level. **D26** — confirm against
> `K141_sch_StamPLC_V10_IO.pdf` before the carrier board is built.

#### 4.7.4 Carrier board

A small board seating on the StamPLC-Bus header, carrying:

1. 3.3 V LDO from EXT_5V (§4.7.3)
2. SX1262 module + SMA bulkhead to the enclosure exterior
3. VE.Direct level-shift front end (BSS138, plus D25's outcome)
4. Screw or JST landing for the VE.Direct cable

Consolidating these onto one board rather than wiring three breakouts point-to-point
suits the enclosure and keeps the "no breadboarded discretes" property. **D27**:
perfboard prototype versus fabricated PCB — for a single node, perfboard is
likely sufficient, but the antenna feed and the SPI runs argue for care in layout.

**Antenna:** the LoRa antenna is external via SMA bulkhead. Keep the SPI run short
and away from the relay terminals; the relays are dry contacts carrying only the
1050's low-voltage accessory signalling, so coupling risk is low, but layout should
not invite it.

#### 4.7.5 BLE — driver portability confirmed

Stamp-S3A is an **ESP32-S3FN8**; the Heltec LoRa V3 is an **ESP32-S3FN8**. Same
core, same flash, no PSRAM on either. The BMS client (§5.7) is therefore a
**recompile, not a port** — NimBLE-Arduino behaves identically on both.

Two carry-over items, neither StamPLC-specific except the second:

- **MTU.** Reference decodes were obtained at a negotiated MTU of 512, with
  responses arriving unfragmented. NimBLE defaults lower; the client must either
  request a larger MTU or implement reassembly.
- **Antenna (D28).** The Stamp-S3A's 2.4 GHz antenna is internal to the DIN case
  with **no external option**, unlike the LoRa side. The pack's own BLE transmitter
  is already known to be weak. §1.4 puts the node within inches of the battery, so
  this should be comfortable — but **measure RSSI from the intended mounting
  position** before committing. The §5.7.4 SmartShunt fallback is unchanged and
  remains the contingency.

#### 4.7.6 Operating temperature — **the principal risk of this platform**

StamPLC is specified **0–40 °C**. The gate enclosure is outdoors in western NC:
sub-freezing winter mornings and summer solar gain that can push a closed box well
past 40 °C.

The ESP32-S3 die is rated −40/+85; the exposed parts are electrolytics, the LCD
(which will ghost badly when cold and is a debug aid, not a functional dependency),
and the RTC crystal's accuracy. None of these is obviously fatal, and the rating is
likely conservative — but it is a **specification exceedance**, and the honest
position is that it is unproven for this installation.

**D29 — mitigation and evidence.** Log enclosure temperature across a season using
the **onboard LM75**, published as a normal HA sensor. Vent and/or shade the
enclosure. Note that the LiFePO4 pack in the same box already has a low-temperature
charge inhibition regime (§8.4), so enclosure temperature is a value the system
wants to publish regardless — **D29 costs nothing extra to instrument.**

This risk does not apply to AquaLink, which is indoors.

---

## 5. Firmware — GateLink

### 5.1 Framework & libraries

- Build: **PlatformIO**, ESP32-S3 target.
- LoRa: **RadioLib** driving the SX1262. Channel Activity Detection (CAD) used
  for media access (§6.7).
- VE.Direct: use/port **osh-labs/VE.Direct_mppt_arduino** (**MIT** — confirmed,
  §12.1) for **both** the text-protocol parser and the HEX protocol definitions,
  register map, and encode/decode helpers.
- BLE: **NimBLE-Arduino** (§5.7) — materially smaller flash and RAM footprint
  than Bluedroid.
- **Platform HAL: `M5StamPLC` + `M5Unified`** (both M5Stack, MIT), wrapped behind
  `/lib/lran-platform/` (§3.5, §10) so GateLink and AquaLink compile against one
  abstraction and the Heltec targets keep building.
- 1050 interface: **no protocol library**. Debounced reads from the input expander,
  timed pulses to the relay expander. This is still the point of §4.2 — v0.7 only
  changes *where* those bits are read and written, not what they mean.

> **v0.7 scheduling constraint.** Inputs are polled at 10–20 ms (§4.2.4) and relay
> pulses are timed in firmware, both over the same I²C bus. Neither may be starved
> by a blocking call. BLE polling (§5.7), VE.Direct HEX round-trips (§6.6) and LoRa
> TX must not run inline with the I/O loop — put the I/O service on its own task or
> a strict cooperative slot. This is a real change from v0.6, where a GPIO edge was
> free and a relay pulse was a hardware timer away.

> **v0.6:** the BusT4 port is gone from v1. That removes the project's only GPL
> dependency, its only reverse-engineered protocol, and its only firmware
> component requiring a custom UART break implementation. See §12.2.

### 5.2 Responsibilities

1. **1050 command driver** — pulse K1–K4 per §4.2.2 on authenticated command;
   enforce pulse width and inter-pulse spacing; never assert two conflicting
   relays.
2. **1050 state reader** — debounce IN1–IN6; derive gate state per §4.2.4;
   maintain the authoritative hold-state (§5.3.4).
3. **Detection and direction classification** — §5.4.
4. **VE.Direct text reader** — continuously parse the ~1 Hz text frames; cache the
   latest complete snapshot; watch the charger error field.
5. **VE.Direct HEX transport** — multiplex HEX request/response with the text
   stream on the same UART (§6.6); enforce the write-authentication rule
   (§6.6.3). **No local interpretation of register semantics.**
6. **BLE BMS client** — duty-cycled connect/read/disconnect against the battery
   BMS (§5.7).
7. **LoRa endpoint** — receive commands, transmit status/ACK/event frames; CAD +
   backoff before transmit (§6.7); fragmentation for oversized payloads (§6.4.4).
8. **Trigger/cache logic** — transmit on poll, gate state change, VE.Direct
   critical error, vehicle detection event (§5.4), hard-shutdown alarm, or BMS
   alarm.
9. **Authentication** — verify HMAC + sequence before pulsing any relay and before
   passing HEX writes to the MPPT (§6.5, §6.6.3).
10. **Power management** — WiFi **off**; BLE **duty-cycled** (§5.7.2); Vext / OLED
    **off** in normal operation. No light-sleep, no RX duty-cycling (§8.6).
11. **Debug** — display control (§5.6), packet loopback, dummy status push,
    detection event injection, relay dry-run mode (§9.2), leveled serial logging.

### 5.3 Gate command model

Two operational cases, per requirement. Both are expressed as HA `cover` and
`switch` operations (§7.2).

#### 5.3.1 Case 1 — momentary open

**Pulse K3 (Guard Station Open).** The gate opens to full open with auto-close
enabled; the 1050 closes it after the auto-close timeout. Analogous to pressing
open on a remote.

- Confirmed wake input (it is what the keypad drives).
- No hold state is entered. GateLink's `hold_open` flag stays false.
- If the gate is already open and unlocked, the pulse restarts the auto-close
  countdown — harmless, and a useful way to extend the window without holding.

#### 5.3.2 Case 2 — open and hold

**Pulse K1 (AUX1 = OPEN and LOCK).** The gate opens and the 1050 latches a LOCK
state that inhibits auto-close and further commands. The gate stays open
indefinitely.

This replicates the mechanism already in use on this installation, rather than
inventing one. Three properties make it the right choice:

- **It is a latched state inside the 1050, set by a momentary pulse.** No wire is
  held down, so no stuck contact can hold the gate abnormally.
- **No safety interlock is bypassed.** FIRE was considered and rejected: it clears
  hard shutdown, which is a latched entrapment state deliberately requiring human
  intervention. A remote path that clears it would put an invisible bypass around
  a UL325 interlock.
- **No lock-out hazard.** SHADOW was considered and rejected: it also *maintains a
  closed gate closed*, so a stuck assertion with the gate shut would prevent
  anyone — keypad, wand, remote, GateLink — from opening it.

GateLink sets `hold_open = true` on successful pulse.

#### 5.3.3 Case 3 — close

Two paths, both available:

| Path | Action | Result |
|---|---|---|
| **Release** (default) | Pulse K2 (AUX2 = UNLOCK) | Lock clears, auto-close resumes and closes the gate. Matches existing behavior. |
| **Immediate** | Pulse K2, wait `unlock_settle_ms` (default 500), then pulse K4 (Guard Station Close) | Closes without waiting out the auto-close timeout |

K2 must precede K4 whenever `hold_open` is true — a locked 1050 ignores a close
command. GateLink sequences this automatically; HA sees a single "close"
operation.

GateLink sets `hold_open = false` on successful K2 pulse.

#### 5.3.4 Hold-state tracking

GateLink maintains `hold_open` as its own authoritative flag, because the 1050's
lock state is not directly readable through the accessory I/O.

| Event | `hold_open` |
|---|---|
| K1 pulsed successfully | true |
| K2 pulsed successfully | false |
| GateLink boot | **false, and K2 is pulsed once** to clear any lock left by a previous instance |
| IN5 (FIRE) asserted | unchanged — tracked separately as `held_open_by_human` |

The boot-time unlock is deliberate: after a power event GateLink cannot know
whether it left the gate locked, and an unrecoverable locked-open gate is a worse
outcome than a spurious unlock on a gate that was already unlocked.

Publish `hold_open` and `held_open_by_human` as separate HA entities (§7.4) — they
mean different things and drive different automations.

### 5.4 Vehicle detection — **REQUIRED**

#### 5.4.1 Installed detection hardware

| Signal | Source | Position | Semantics |
|---|---|---|---|
| **SAFETY** (IN3) | **Diablo DSP-7LP** | Two loops, **one outside and one inside the gate**, wired **in parallel** into one detector channel | **Single** dry contact. Asserts when a vehicle is over **either** loop. The two loops are *not* separately observable. |
| **EXIT** (IN4) | Self-contained **wand-style** sensor | **Much farther inside** the gate | Separate dry contact. Asserts on vehicle presence at the wand. |

Both contacts are tapped in parallel with their existing connections to the 1050
(§4.2.4). The Diablo requires the power change in §4.6; the wand does not.

The DSP-7LP draws **1 mA static** with no vehicle detected.

#### 5.4.2 Direction classification

The two signals are widely separated along the drive — SAFETY at the gate, EXIT
far inside it. Rising-edge ordering discriminates:

| First rising edge | Then | Classification |
|---|---|---|
| `EXIT` | `SAFETY` | `EXIT` — vehicle departing (moving outward) |
| `SAFETY` | `EXIT` | `ENTRY` — vehicle arriving (moving inward) |
| either | *(no second edge within window)* | `UNDETERMINED` |
| both within `detect_debounce_ms` | — | `UNDETERMINED` |

Four properties of the installed topology shape the logic:

1. **The window must be large.** A departing vehicle trips EXIT, then waits for
   the gate to open before reaching SAFETY. Gaps of 10–30 s are normal.
2. **SAFETY cannot distinguish inside from outside.** Both loops feed one channel
   in parallel, so a traversal produces one continuous SAFETY assertion spanning
   both loops, not two separable pulses. The classifier must not attempt to read
   structure inside a single SAFETY assertion.
3. **EXIT is also an actuator.** The wand's assertion causes the 1050 to open the
   gate. A gate-open transition on IN1/IN2 shortly after an EXIT rising edge is
   self-evidently exit-triggered.
4. **Wand hold behavior must be characterized.** Some self-contained wand sensors
   de-assert after a hold time even with a vehicle stationary above them, which
   would produce repeated edges from an idling vehicle. **`TBM`**, §13.1.

**GPIO acquisition is now the only path**, which removes the timing question
entirely: interrupts see the edge in microseconds regardless of what the 1050 is
doing. This also makes classification immune to standby, provided §4.6 is done.

#### 5.4.3 Requirements

- **5.4.3.1** GateLink SHALL represent the instantaneous state of SAFETY and EXIT
  as discrete booleans in the status payload.
- **5.4.3.2** GateLink SHALL derive direction of travel from the rising-edge
  ordering in §5.4.2.
- **5.4.3.3** A detection event SHALL trigger an **immediate unsolicited status
  push**, independent of the poll schedule.
- **5.4.3.4** GateLink SHALL raise the **held-open alert** defined in §5.4.5.
- **5.4.3.5** Direction classification SHALL be implemented as a state machine with
  an explicit idle-reset condition, so that partial traversals (vehicle pulls up to
  the wand and reverses; vehicle stops on the loops and waits) resolve to
  `UNDETERMINED` rather than corrupting the next real traversal.

#### 5.4.4 Configuration

| Parameter | Default | Meaning |
|---|---|---|
| `detect_sequence_window_ms` | **60000** | Max gap between first and second rising edge for the pair to count as one traversal |
| `detect_sequence_idle_ms` | 10000 | Both inputs clear this long ends the sequence and resets to idle |
| `detect_debounce_ms` | 50 | Debounce per input; closer than this across the two is treated as simultaneous |
| `held_open_alert_repeat_s` | 0 | 0 = one alert per detection; non-zero re-alerts at this interval while the condition persists |

Note `held_open_alert_delay_s` from v0.5 is **removed** — see §5.4.5.

#### 5.4.5 Held-open alert — **the primary operational requirement**

The need: *the gate is sometimes deliberately left standing open (deliveries,
contractors, guests), and in that state any vehicle movement through it should
notify by email and SMS from HA.*

**v0.6 defines this on explicit hold state rather than inference.** v0.5 had to
guess at "statically open" from gate state plus a timeout, because auto-close
state was not observable. That guesswork is gone: the gate is only held open
because *something asserted a hold*, and both hold mechanisms are now visible.

**The gate is `HELD_OPEN` when IN1 (OPEN) is asserted and either:**

| Condition | Source |
|---|---|
| `hold_open` is true | GateLink's own flag — LRAN issued OPEN+LOCK (§5.3.4) |
| `held_open_by_human` is true | IN5 — someone used the keypad FIRE code |

**Requirement.** While `HELD_OPEN`, any rising edge on SAFETY or EXIT SHALL:

1. Set the `vehicle_while_held_open` alert in an immediate `EVENT` push.
2. Include the direction classification if available at push time, and send a
   follow-up push when classification completes.

**Why the delay timer disappears.** With hold state known explicitly, there is no
risk of firing on a normal traversal: a routine pass never enters `HELD_OPEN`,
because nothing locked the gate and no FIRE code was entered. The v0.5 30-second
arming delay existed only to suppress false positives from an inferred condition,
and it is no longer needed. Alerts now fire on the first edge, with no dead window.

**HA delivery.** Because this drives email and SMS, the event must be delivered as
a **non-retained MQTT event message**, not solely as a retained `binary_sensor`
state (§7.4). Retained binary sensors replay on HA restart and on discovery
refresh, which would produce spurious 2 AM notifications. The binary sensor is for
dashboard visibility; **the event topic is the automation trigger.**

### 5.5 Movement cause

Derived locally rather than read from the controller:

| Observation | Inferred cause |
|---|---|
| EXIT rising edge, then gate opens within `cause_window_ms` (default 10000) | Exit wand |
| GateLink pulsed K1 or K3, then gate opens | LRAN command |
| IN5 asserts, then gate opens | Keypad FIRE code |
| Gate opens with none of the above | External — keypad, remote, or front panel |

Published as a text sensor. Coarser than a BusT4-reported cause, but sufficient
for the entity model and available with no bus.

### 5.6 Display control (D6)

Display **off by default in all normal operating modes**, with two independent
activation paths.

**Manual.** A **StamPLC user button (A, B or C)** toggles the display. On
activation it shows a status page and starts an inactivity timer
(`display_timeout_s`, default 60); on expiry the backlight is switched off via the
PI4IOE expander (`LCD_BL`, P7). A press while on cancels the timer and blanks
immediately.

> **v0.7 — this got easier.** The Heltec design had one button on GPIO0, which is
> also the boot strap, requiring press-duration filtering and a post-reset sampling
> delay. StamPLC provides **three dedicated user buttons** behind the PI4IOE
> expander, none of them strapping pins. The GPIO0 caution is **withdrawn**. Three
> buttons also make the multi-page proposal below straightforward rather than a
> gesture-overloading exercise.

**Automatic.** The display SHALL power on and remain on, **ignoring the inactivity
timer**, whenever the node is in any debug mode (packet loopback, simulated status
push, detection event injection, relay dry-run, or any future registered debug
mode). It returns to manual/timeout behavior when the last debug mode exits.

**Sub-items for development:**
- **Multi-page cycling.** One page will not fit gate state + hold state + detector
  state + MPPT + battery SOC + link quality. With three buttons: **A = next page,
  B = previous, C = off**. The 135×240 colour LCD also fits materially more per page
  than the 0.96" OLED it replaces.
- **Backlight, not rail power.** v0.6 required full SSD1306 re-initialisation after
  every Vext power cycle — a known way to build a display that works exactly once.
  StamPLC's LCD stays initialised and only its **backlight** is switched, so that
  entire failure mode is **withdrawn**. Idle cost is the panel's static draw, which
  is inside the §8.2 node allocation.
- **Buzzer — new capability, optional.** StamPLC has one. Candidate use: audible
  confirmation during §9.3 bring-up, when the operator is at the gate and not
  looking at the screen. Default **off** in normal operation; a gate that beeps at
  its own accord is not wanted.

### 5.7 Battery BMS over BLE

#### 5.7.1 Why this matters

LiFePO4 has a famously flat discharge curve — roughly **13.2–13.3 V across 20–80%
SOC**. Voltage-derived SOC is close to meaningless through the middle of the range
and only usable near the endpoints. The MPPT compounds this: it measures **charge**
current only, so coulomb counting on discharge is unavailable from it.

**The BLE BMS is therefore the only good SOC source in the base BOM.**

#### 5.7.2 Requirements

- **5.7.2.1** GateLink SHALL, on a configurable interval (`bms_poll_s`, default
  **300**), connect to the battery BMS over BLE, read state of charge, pack
  voltage, pack current, cell voltages, temperature(s) and alarm/protection flags,
  then disconnect.
- **5.7.2.2** GateLink SHALL de-initialize the BLE controller between polls rather
  than leaving the stack resident.
- **5.7.2.3** BMS alarm or protection flags SHALL trigger an immediate unsolicited
  status push.
- **5.7.2.4** A failed BLE connection SHALL NOT block or delay any other node
  function. BMS data is published with a staleness timestamp; consumers treat
  absence as unknown, not as zero.
- **5.7.2.5** The connect/read/disconnect cadence SHALL leave the BMS reachable
  from a phone between polls — §5.7.5.

#### 5.7.3 Power cost

A connect / read / disconnect cycle is roughly **3–6 s at 60–100 mA ≈ 0.1 mAh**.
At `bms_poll_s` = 300 that is ~**0.03 Ah/day** against a ~7 Ah/day budget — under
half a percent.

**No radio coexistence problem.** The SX1262 is separate silicon on SPI with its
own antenna at 915 MHz; the ESP32-S3's BLE radio is 2.4 GHz. WiFi remains off
regardless (§8.7). **This is unchanged by v0.7** — the radio is now on a carrier
board rather than the same PCB, which if anything improves isolation. The open
question is link margin, not interference: see §4.7.5 and **D28**.

**Flash is the real budget item.** NimBLE adds ~200–300 KB. The 8 MB flash
accommodates this comfortably — and with the BusT4 stack gone, there is more
headroom than v0.5 assumed.

#### 5.7.4 Protocol identification and fallback (D15)

The WattCycle BMS protocol is **unknown**. Packs in this class commonly use a
JBD/Xiaoxiang-family or JK-family BLE BMS, both with existing community
implementations, but this must be confirmed.

**Plan of record:**

1. **On battery arrival**, use **nRF Connect** to enumerate GATT services and
   characteristics and capture traffic from the vendor app.
2. Identify the family, then port or adapt the corresponding community client.
3. Record findings in `/docs/bms-protocol.md`. **Verify the source's license before
   vendoring** — a repository with no LICENSE file grants no rights.

**Fallback: a Victron SmartShunt.** Selected contingency. Provides true
coulomb-counted SOC, load current (which nothing else in the system measures), and
consumed-Ah — and it speaks VE.Direct, reusing the existing parser and transport.
Cost: a second level-shifted UART pair (BSS138 ch 3–4), ~1 mA continuous, and
shunt installation in the battery negative lead.

**Interim fallback:** publish battery voltage from the MPPT with an explicit
`soc_source: voltage_coarse` marker, and track **daily Vmin and daily yield
(H19/H20/H21) trends** rather than instantaneous SOC. Adequate for the stated goal
of monitoring nightly consumption and recovery — the trend is the signal, not the
absolute number.

#### 5.7.5 Single-connection constraint

Many BMS BLE modules accept **only one connection at a time**. A node holding a
persistent connection will lock the vendor phone app out entirely. At
`bms_poll_s` = 300 the BMS is free ~98% of wall-clock time.

Provide an HA `switch` — **"BMS BLE polling"** — to suspend polling while working
with the phone app. Default on.

---

## 6. Firmware — LoRaBridge & LoRa protocol

### 6.1 Framework & libraries (D5)

PlatformIO, ESP32-S3. RadioLib (SX1262). WiFi. Links the same shared LoRa protocol
library as every node, plus the VE.Direct HEX register model (§3.3).

**Define a thin `MqttTransport` interface; implement first against PubSubClient.**
Bridge traffic is modest — a poll per node every 1–5 minutes plus occasional
commands and events — so throughput and QoS 2 are irrelevant. What matters is
reliable reconnect, LWT, and publishing discovery-config JSON.

| Option | License | Assessment |
|---|---|---|
| **PubSubClient** | MIT | Tiny, synchronous, extremely stable, ubiquitous. **Gotcha:** default max packet 256 bytes — Discovery configs exceed this and fail confusingly. Fix with `MQTT_MAX_PACKET_SIZE` ≥1024. |
| **espMqttClient** | MIT | Actively maintained, sync and async variants, large payloads, QoS 0/1/2. Designated fallback. |
| **AsyncMqttClient** | MIT | Effectively unmaintained. Avoid for new work. |
| **esp-mqtt** (IDF native) | Apache-2.0 | Most robust reconnect/TLS, but pulls the design toward IDF. Reserve for later migration. |

> **v0.6 note:** v0.5 flagged raw BusT4 frame streaming as the feature most likely
> to stress a blocking publish. That feature is gone, so bridge MQTT load is now
> uniformly light and PubSubClient is a comfortable fit.

### 6.2 OTA update (D16)

**The bridge supports OTA; remote nodes do not.**

| | Bridge | GateLink / WellLink |
|---|---|---|
| Update path | **OTA over WiFi** + USB | USB only |
| Rationale | On the LAN, mains powered, physically accessible, and the node whose firmware changes most often | 500 ft away; a bad flash is a walk with a laptop, and there is no second radio path to recover through |

Requirements:
- ArduinoOTA or ESP-IDF `esp_https_ota`, on an authenticated endpoint with a
  password/key held in untracked config (§10).
- **Dual-partition (A/B) with rollback.** A bricked bridge takes the whole
  property's telemetry offline.
- OTA disabled during an active LoRa transaction; deferred until idle.
- Version string published to `lran/bridge/version` and exposed as a diagnostic
  sensor.

### 6.3 Responsibilities

1. LoRa RX → verify → decode per source node → publish to MQTT state topics.
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
9. Debug: loopback mode, dummy-status publish, per-node simulators.
10. Serve OTA (§6.2).

### 6.4 LoRa protocol

#### 6.4.1 Topology

Star. The bridge is the centre and the only node that initiates polls. Nodes never
address each other. No mesh, no relaying.

#### 6.4.2 Frame format

```
| ver | type | src | dst | seq | frag | schema | payload... | MAC (8B, auth only) | CRC16 |
   1     1     1     1     2      1       1        0..N            0 or 8            2
```

| Field | Size | Notes |
|---|---|---|
| `ver` | 1 | Protocol version. See §6.4.5. |
| `type` | 1 | `COMMAND`, `COMMAND_ACK`, `POLL`, `STATUS`, `EVENT`, `ERROR`, `PING`/`LOOPBACK`, `HEX_REQ`, `HEX_RSP` |
| `src` / `dst` | 1 each | Node IDs per §1.6. `0x00` bridge, `0xFF` broadcast. 254 usable addresses. |
| `seq` | 2 | Monotonic per source, per boot. |
| `frag` | 1 | High nibble = fragment index, low nibble = total. `0x11` = single unfragmented frame. |
| `schema` | 1 | Payload schema ID + version for `STATUS`/`EVENT`. See §6.4.3. |
| `MAC` | 0 or 8 | Truncated HMAC-SHA256. Present on authenticated types only (§6.5). |
| `CRC16` | 2 | Over the whole frame. |

Header is 9 bytes. Against the SX126x 255-byte PHY payload limit that leaves
**~244 bytes** unauthenticated or **~236 bytes** authenticated, before CRC.

> **v0.6:** with raw frame streaming gone, GateLink's status payload is comfortably
> **40–80 bytes** and never approaches the limit. Fragmentation is retained anyway
> (§6.4.4) — it costs one header byte and WellLink or a future node may need it.
> The `RAW` frame type is removed.

#### 6.4.3 Per-node payload schemas

Each node type defines its own payload schema, identified explicitly:

| `schema` value | Meaning |
|---|---|
| `0x10` | GateLink status v1 |
| `0x11` | GateLink event v1 |
| `0x20` | WellLink status v1 (reserved) |
| `0xF0` | Generic node health (uptime, RSSI, boot count) — all nodes |

**Why explicit rather than inferred from `src`.** The bridge *could* look up node
type from its registry. Explicit schema IDs additionally survive **firmware version
skew** — GateLink running an older build announces schema `0x10` while the bridge
already understands `0x12`, and the bridge decodes it correctly instead of
misparsing. Given that remote nodes have no OTA and will drift out of sync, this is
worth one byte.

Schema definitions live in `/lib/lran-protocol/schemas/` and are the versioned
contract between node and bridge.

#### 6.4.4 Fragmentation

- Sender splits into ≤15 fragments, each a complete frame with its own CRC.
- Receiver reassembles by `(src, seq, schema)`; incomplete sets expire after
  `frag_reassembly_timeout_ms` (default 5000) and are discarded with an error log.
- **Fragments are individually acknowledged only for `COMMAND`.** Status is
  fire-and-forget; a dropped fragment discards the set.
- Fragmentation is **not** used for commands in v1. Commands are small; keeping
  them single-frame keeps authentication and replay logic simple.

#### 6.4.5 Version tolerance — the no-OTA consequence

Remote nodes are USB-only. A protocol change means a physical visit to every node.
To make rollout incremental rather than a flag day:

- **The bridge SHALL accept protocol version `N` and `N−1`** and decode both.
- Nodes accept only their own version from the bridge; the bridge downgrades
  per-node based on the version last heard from that node.
- A node running an unsupported version is marked `unavailable` with a distinct
  reason, not silently ignored.
- Breaking changes to `/lib/lran-protocol/` require a version bump and an entry in
  `/docs/protocol-changelog.md`.

#### 6.4.6 Per-node availability

MQTT LWT covers only the bridge's connection to the broker. It says nothing about
whether GateLink is alive.

- The bridge maintains `last_seen` per node.
- A node is marked `offline` after `missed_poll_threshold` (default **3**)
  consecutive unanswered polls, and `online` on any valid frame received.
- State published to `lran/<node>/availability`, **retained**, and referenced by
  every entity belonging to that node in its discovery config.
- The bridge's own LWT marks all nodes unavailable implicitly.

### 6.5 Authentication and keying

#### 6.5.1 Per-node keys

A single fleet-wide key would mean **compromising the well sensor grants gate
command authority** — an unacceptable coupling between a low-value node and the
only node that moves a large motorized object.

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
| `STATUS`, `EVENT`, `POLL` | No | Spoofed status is a nuisance, not a hazard. |
| `COMMAND_ACK` | No | Correlated to an authenticated request by `seq`. |

#### 6.5.3 Sequence / replay

Monotonic counter, no cross-reboot persistence. Each node picks a random `boot_id`
at boot; the bridge keeps a **per-node table** of `(boot_id, last_seq)` and accepts
increasing `seq` within a `boot_id`, resyncing on a new one.

#### 6.5.4 Reliability

Commands are ACKed (receipt + execution result); the bridge retries with backoff.

> **v0.6 caution on retries.** GateLink's commands are now **physical relay
> pulses**, so a retried command is a second pulse, not an idempotent re-send. A
> duplicate K3 restarts the auto-close countdown (harmless); a duplicate K1 or K2
> re-asserts a state already held (harmless). But GateLink SHALL deduplicate on
> `(boot_id, seq)` and return the cached ACK rather than pulsing twice — the bridge
> retrying an ACK it never received must not move the gate again.

### 6.6 VE.Direct HEX proxy

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
- **Do not disable the text protocol.** The 1 Hz stream is the primary telemetry
  source.
- HEX requests are transported **verbatim**. GateLink inspects only the command
  nibble, for §6.6.3.

#### 6.6.3 Write protection — three independent gates

Writing MPPT charge parameters under LiFePO4 is a **battery-damage path**.
Re-enabling temperature compensation or equalization on a lithium pack is exactly
the kind of one-character mistake that is invisible until the battery is harmed.

1. **HMAC on the frame.** Set (`0x8`) and Restart (`0x6`) require a valid MAC
   (§6.5.2). GateLink rejects unauthenticated writes at the transport layer and
   returns an `ERROR`.
2. **Armed write-enable switch.** An HA `switch` — **"MPPT config write enable"** —
   must be on. **Default off**, with **auto-expiry after `mppt_write_arm_timeout_s`
   (default 300)**. Enforced on the **bridge**, which refuses to build a write
   frame while disarmed. Deliberately a two-step operation.
3. **Audit trail.** Every write attempt — request payload, authorization outcome,
   MPPT response — published to `lran/gatelink/vedirect/hex/audit`, **retained**.

#### 6.6.4 MQTT interface

| Topic | Direction | Retained | Purpose |
|---|---|---|---|
| `lran/gatelink/vedirect/hex/request` | HA → bridge | No | Raw HEX request string |
| `lran/gatelink/vedirect/hex/response` | bridge → HA | No | Raw HEX response + status |
| `lran/gatelink/vedirect/hex/audit` | bridge → HA | Yes | §6.6.3 item 3 |
| `lran/gatelink/vedirect/write_enable/{state,set}` | both | Yes | §6.6.3 item 2 |

Raw HEX in and out is the v1 interface — the general case, needing no
register-by-register modelling. **Once the small set of registers actually adjusted
in practice is known, promote those to named `number`/`select` entities.** Do not
attempt to model 100+ registers as entities.

#### 6.6.5 Sequencing — the chicken-and-egg

The MPPT must be reconfigured for LiFePO4 **before the new battery is first
charged**, and LRAN will not exist at that point.

- **Initial reconfiguration:** VictronConnect over a VE.Direct-to-USB cable. Do this
  when the battery arrives. §8.1.2 lists the required settings.
- **LRAN's HEX path is for ongoing adjustment and verification**, and for reading
  back configuration to confirm it has not drifted — not for initial setup.

On boot, GateLink reads the charge parameters and the bridge publishes them as
diagnostic sensors, so a wrong profile is visible in HA rather than latent.

### 6.7 Media access

Point-to-point had no contention. A shared channel with N nodes does.

| Mechanism | Detail |
|---|---|
| **Bridge serializes polls** | Never more than one outstanding poll across the fleet. Removes the largest predictable collision source for free. |
| **CAD before TX** | Nodes use RadioLib's Channel Activity Detection before transmitting. Cheap in time and power. |
| **Randomized backoff** | On CAD-busy, back off `random(0, backoff_max_ms)` (default 500), retry up to `cad_retries` (default 5), then transmit regardless — an event push must not be starved indefinitely. |
| **Single channel** | All nodes share one frequency/SF/BW/sync word. Per-node channels would require the bridge to listen on multiple configurations, which one SX1262 cannot do. |

> **v0.6:** removing raw frame streaming removes the only feature that could
> occupy the channel for extended periods. Expected occupancy is now dominated by
> short status frames on a 1–5 minute cadence, which is negligible.

**FCC.** 915 MHz ISM, digital modulation under Part 15.247. Not LoRaWAN, so no TTN
duty-cycle policy applies, but transmit power and bandwidth limits do.

### 6.8 LoRa link parameters

- Band: **US 915 MHz**, point-to-multipoint star (not LoRaWAN), private sync word.
- SF / BW / CR / TX power: **TBD** after the range test (D1). Starting point for
  ~150 m near-LOS: SF7–9, BW 125 kHz, CR 4/5, moderate TX power.
- **Bridge antenna placement is a two-bearing problem.** GateLink and WellLink are
  at similar distances in **different directions**. Favour an omnidirectional
  antenna in a central, elevated position over anything with a pattern optimized
  toward the gate. Range-test **both bearings** before committing to a location.
- Node-address filtering in the SX126x packet handler is enabled so nodes discard
  frames not addressed to them in hardware. Low value while GateLink runs
  continuous RX; retained because it matters for any duty-cycled node (Appendix A).

---

## 7. Home Assistant integration (MQTT Discovery)

### 7.1 Device model

**One HA device per node, plus one for the bridge.** Each node's entities carry
that node's `device` block and reference that node's availability topic (§6.4.6).

| HA device | Node | Availability source |
|---|---|---|
| LoRa Bridge | `0x00` | MQTT LWT |
| GateLink | `0x01` | `lran/gatelink/availability` (bridge watchdog) |
| WellLink | `0x02` | `lran/welllink/availability` (bridge watchdog) |

Discovery `unique_id`s are prefixed per node (`lran_gatelink_*`) so they are stable
and non-colliding as the fleet grows.

### 7.2 Entity model (D8): `cover` **plus** a hold switch

**`cover` (device_class: `gate`) is the primary control surface** — native
open/close/stop UI, voice-assistant support, dashboard cards, standard `cover.*`
services. Driven from real IN1/IN2 state with `optimistic: false`.

Mapping to §5.3:

| HA operation | GateLink action |
|---|---|
| `cover.open_cover` | Case 1 — pulse K3 (momentary open, auto-close returns it) |
| `cover.close_cover` | Case 3 — pulse K2, then K4 if `close_immediate` is set |
| `switch.turn_on` ("Hold gate open") | Case 2 — pulse K1 (OPEN and LOCK) |
| `switch.turn_off` ("Hold gate open") | Pulse K2 (UNLOCK); auto-close then closes |

**A `switch` rather than a second cover.** "Hold open" is a persistent mode, not a
momentary action, and a switch models a mode correctly — it shows current state,
it is togglable from a dashboard, and it can be used as an automation condition.
Modelling it as a button would lose the state.

`cover.stop_cover` is **not implemented in v1** (§4.2.5). The entity may either
omit stop or expose it as unavailable.

### 7.3 Position reporting — deferred to v2

**v1 will not report position.** Time-based estimation on a gate that can be
stopped, reversed, or obstructed produces confident wrong answers, and a cover that
lies about being 40% open is worse than one reporting discrete states.

### 7.4 Entity table — GateLink

Topic hierarchy `lran/gatelink/...`, retained discovery configs, per-node
availability topic.

**Gate control and state**

| Entity | Type | Source | Notes |
|---|---|---|---|
| Gate | `cover` (device_class: gate) | IN1/IN2 | open/close; no position (§7.3) |
| Hold gate open | `switch` | `hold_open` flag | §5.3.2 / §5.3.4 |
| Close immediately | `switch` (config) | — | When on, close uses K2+K4 rather than K2 alone |
| Gate state | `sensor` (text, diagnostic) | IN1/IN2 | open / closed / moving |
| Held open by keypad | `binary_sensor` | IN5 (FIRE sense) | A human used the FIRE code |
| Hard shutdown | `binary_sensor` (problem) | IN6 (alarm sense) | Entrapment latch — requires physical reset |
| Movement cause | `sensor` (text) | derived, §5.5 | exit wand / LRAN / keypad / external |

**Vehicle detection (§5.4)**

| Entity | Type | Source | Notes |
|---|---|---|---|
| Safety detector | `binary_sensor` (occupancy) | IN3 | Combined DSP-7LP contact, both loops |
| Exit wand | `binary_sensor` (occupancy) | IN4 | Separate wand contact |
| Last traversal direction | `sensor` (text) | derived | `ENTRY` / `EXIT` / `UNDETERMINED` |
| Last traversal time | `sensor` (timestamp) | derived | |
| Vehicle while held open | `binary_sensor` (problem) | derived | **Dashboard visibility only** |
| **Vehicle while held open** | **`event`** | derived | **`lran/gatelink/event/held_open`, non-retained. This is the automation trigger for email/SMS.** |

> **Why both.** A retained `binary_sensor` replays its state when HA restarts or
> re-reads discovery, which would fire the notification automation at arbitrary
> times. The non-retained event topic fires exactly once, when it happens. Use the
> `binary_sensor` for the dashboard and the **event for the automation** (§5.4.5).

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
| MPPT config readback | `sensor` ×N (diagnostic) | MPPT via HEX | §6.6.5 |
| MPPT config write enable | `switch` | bridge | §6.6.3, default off, auto-expiry |

**Diagnostics**

| Entity | Type | Source | Notes |
|---|---|---|---|
| LoRa RSSI / SNR | `sensor` (diagnostic) | bridge | per node |
| Missed polls | `sensor` (diagnostic) | bridge | feeds §6.4.6 |
| Node uptime | `sensor` (diagnostic) | GateLink | |
| Protocol version | `sensor` (diagnostic) | bridge | §6.4.5 — shows node/bridge skew |
| Poll interval | `number` (config) | bridge | per node, runtime-configurable |
| Relay dry-run | `switch` (config, diagnostic) | GateLink | §9.2 — log intent without pulsing |

### 7.5 Entity table — LoRaBridge

| Entity | Type | Notes |
|---|---|---|
| Bridge availability | via LWT | |
| WiFi RSSI | `sensor` (diagnostic) | |
| Bridge uptime | `sensor` (diagnostic) | |
| Firmware version | `sensor` (diagnostic) | §6.2 |
| Nodes online | `sensor` (count, diagnostic) | |

### 7.6 WellLink — reserved

Entity model to be defined with the node (§1.7). Expected: level `sensor`, battery
`sensor`s, availability, RSSI, poll interval `number`. The device and topic
namespace are reserved now so nothing else claims them.

---

## 8. Power budget (gate side)

### 8.1 Source — LiFePO4

#### 8.1.1 Capacity

**12 V 100 Ah LiFePO4** (WattCycle 100 Ah mini w/ Bluetooth), replacing the Group
24 FLA. **100% of nameplate capacity is usable.** 50 W panel via MPPT 75/15.

| | v0.4 (FLA) | v0.6 (LiFePO4) |
|---|---|---|
| Nameplate | 75 Ah | 100 Ah |
| Usable | 37.5 Ah (50% derate) | **100 Ah** |
| Daily draw (§8.2) | ~7.3 Ah | **~7.0 Ah** |
| Autonomy at zero harvest | ~5.1 days | **~14.3 days** |
| Winter harvest ratio | 1.45× | **1.51×** |

#### 8.1.2 Required MPPT reconfiguration — **before first charge**

Charging LiFePO4 on a lead profile is harmful. Do this with VictronConnect over USB
when the battery arrives, then verify via the HEX readback sensors (§6.6.5).

| Setting | Required value | Why |
|---|---|---|
| Battery type | **User-defined** | Not any lead preset |
| Absorption voltage | **14.2–14.6 V** | Per WattCycle spec; confirm against the pack datasheet |
| Absorption time | Short / fixed | LiFePO4 does not need long absorption |
| Float voltage | **~13.5 V** | Avoids holding the pack at high SOC indefinitely |
| **Equalization** | **Disabled** | Destructive to LiFePO4 |
| **Temperature compensation** | **0 mV/°C** | Default −16.2 mV/°C is a lead-acid behavior and is actively wrong here. **The single most consequential setting in this table.** |
| Low-voltage cutoff / load output | Per pack spec | The pack BMS is the real protection |

Record final values in `/docs/mppt-config.md`.

### 8.2 Load inventory

**TBM** = To Be Measured; placeholders are conservative.

| Load | Current @ 12 V | Duty | Ah/day |
|---|---:|---|---:|
| 2 × 2 W LED lights (night only) | 0.33 A | 12 h | 4.00 |
| 1050 controller, **standby retained** (§4.2.3) | 0.017 A **TBM** | ~23 h | 0.39 |
| 1050 controller, active windows | 0.110 A **TBM** | ~1 h | 0.11 |
| **Diablo DSP-7LP, now continuously powered** (§4.6) | 0.001 A | 24 h | 0.02 |
| GateLink node — continuous RX (**StamPLC**, v0.7) | 0.048 A (vendor) **TBM** | 24 h | 1.15 |
| GateLink — BLE BMS polling | — | 288 cycles/day | 0.03 |
| GateLink — relay coils | ~0.070 A | ~10 pulses × 0.3 s/day | <0.01 |
| MPPT 75/15 self-consumption | 0.010 A | 24 h | 0.24 |
| Gate motors while operating | ~5 A **TBM** | 2 cycles × ~20 s | 0.06 |
| ~~12 V→USB-C adapter overhead~~ | — | — | **0 (deleted, §4.5)** |
| *SmartShunt* — **contingency only** | *0.001 A* | *24 h* | *0.02* |
| **Total** | | | **≈ 6.9 Ah/day** |

**Harvest:** 50 W × ~3.0 winter peak-sun-hours (western NC, non-optimal tilt) ×
0.85 system efficiency ≈ 127 Wh ≈ **10.6 Ah/day**. Summer roughly doubles this.

> **v0.7 note — the platform change is power-neutral.** StamPLC's vendor-specified
> 47.84 mA @ 12 V working is within a couple of mA of the Heltec allocation it
> replaces, and deleting the USB-C adapter removes its ~0.12 Ah/day conversion
> overhead. Net effect is a marginal *improvement*, ~7.0 → ~6.9 Ah/day, and the
> harvest ratio is unchanged at ~1.5×. **No power argument exists for or against
> this platform** — which is the point: the decision rests on I/O and packaging.
>
> The relay-coil row is likewise unchanged; StamPLC's relays draw comparably and
> for the same ~0.3 s pulse. And per §4.5 the **INA226 now measures the node's
> actual draw**, so this row stops being `TBM` as soon as the node is powered.

#### 8.2.1 Keeping standby is worth ~2 Ah/day

Because every LRAN command wakes the 1050 on its own (§4.2.3), **standby stays
enabled**. That decision is worth stating in budget terms:

| Configuration | 1050 contribution | Total | Harvest ratio |
|---|---|---:|---:|
| **Standby retained** (v0.6) | ~0.50 Ah/day | 7.0 | **1.51×** |
| Standby disabled | ~2.40 Ah/day | 8.9 | 1.19× |

Disabling standby would have cost roughly **19% of winter harvest**. It would also
contradict the manufacturer's own solar guidance, which directs 1050 solar
installations to use standby mode. The §4.2 architecture buys that back for the
price of a relay module.

**Set the standby timeout deliberately** rather than leaving it at default. The
range is 5–120 s; a longer timeout gives more post-wake window at negligible cost.
Current setting **`TBM`**, §13.1.

### 8.3 Findings

1. **The LEDs are the budget.** At ~4 Ah/day they are ~57% of consumption, more
   than every other load combined. Real headroom comes from LED runtime, not
   firmware.
2. **A bigger battery does not fix a harvest deficit — it extends the
   ride-through.** Harvest vs. load is 10.6 in / 7.0 out. That ratio is a property
   of the panel and the loads. What 100 Ah buys is **cloud tolerance and time to
   notice**.
3. **Autonomy is ~14.3 days** from full at zero harvest, up from ~5.
4. **Firmware power optimization is not a design driver.** At 100 Ah usable, prefer
   the simple, always-on, low-latency design in every case. §8.6 acts on this.
5. **The system already works.** The existing install has run for years. LRAN adds
   ~1.4 Ah/day, roughly 20% of the new total, against an autonomy figure that
   nearly tripled.

### 8.4 Low-temperature charge inhibition

The pack's BMS blocks charging below approximately 0 °C. This is correct behaviour
and is accepted; the reserve covers it. But it must be **observable**, because its
symptom — a battery not recharging on a sunny day — is otherwise indistinguishable
from a failing panel or MPPT.

**Autonomy check.** ~14.3 days of reserve comfortably exceeds any realistic western
NC sub-freezing spell, where daytime highs typically rise above freezing even in a
cold snap and restore a charging window each day. This is a monitoring problem, not
a capacity problem.

**Detection without the BMS** — inferable from VE.Direct alone:

> PV power present **AND** charger state is bulk/absorption **AND** battery current
> ≈ 0 **AND** battery voltage not rising, sustained for `charge_inhibit_confirm_s`
> (default 300) ⇒ **charging inhibited**

**Detection with the BMS:** read the charge-inhibit / low-temperature protection
flag and pack temperature directly. Preferred when available.

**Requirements:**
- Publish a `binary_sensor` "Charging inhibited" (device_class `problem`).
- Publish pack temperature when the BMS is reachable.
- Maintain a **cumulative Ah drawn since charging was last active** counter, so a
  multi-day cold event shows consumed reserve rather than just a boolean.
- Note **upgrading the panel does not help while charging is inhibited**.

### 8.5 Validation and ongoing monitoring

**Before install:** log the MPPT's own yield history (`H19`/`H20`/`H21`) and battery
voltage minima for one week via VE.Direct. That yields the *actual* present margin
for free.

**After install — the plan of record.** Monitor nightly consumption and overnight
recovery over time:

| Metric | Source | What it tells you |
|---|---|---|
| Daily yield (H20) | MPPT | Harvest trend, panel/shading degradation |
| Overnight ΔSOC | BMS or SmartShunt | True nightly consumption |
| Daily Vmin | MPPT | Proxy for depth of discharge if SOC is unavailable |
| Days since full | derived | Early warning of a sustained deficit |
| Charging-inhibited hours | §8.4 | Distinguishes cold events from real faults |

**Decision rule for a panel upgrade:** upgrade only if *days-since-full* trends
upward across a season **and** the shortfall is not attributable to
charging-inhibited hours.

### 8.6 Not implemented — night profile, RX duty-cycling, extended preamble

Removed in v0.5 and confirmed removed here. The saving was ~0.36 Ah/day — **0.36%
of 100 Ah usable** — against real costs in complexity, latency, and overnight
VE.Direct data continuity. **Retires D2, D9, D10** for GateLink. The design is
preserved in **Appendix A** for a possibly battery-powered WellLink.

### 8.7 Fixed levers

- **WiFi disabled** on GateLink (largest single ESP32 saving).
- **BLE duty-cycled**, not resident (§5.7.2).
- OLED / Vext off in normal operation (§5.6).
- Status caching minimizes LoRa TX (the peak consumer).
- **1050 standby retained** (§8.2.1) — now the largest single lever in the budget.
- Relays are momentary; coil current is negligible (§8.2).

### 8.8 Where the current goes on the node

The **radio is not the constraint.** Per the SX1262 datasheet, LoRa 125 kHz receive
is **4.2 mA** (normal) or **5.3 mA** (Rx-boosted, +3 dB); TX is ~90 mA @ +14 dBm
and ~118 mA @ +22 dBm; sleep with config retained is sub-µA. Continuous RX adds only
~5 mA on top of an **ESP32-S3 that dominates** at tens of mA while awake. The floor
is set by keeping the MCU awake, and the MCU stays awake to parse the ~1 Hz
VE.Direct stream.

**D10 is retired as a power question** — 1.1 mA is 0.026 Ah/day. If the range test
shows any benefit from Rx-boosted gain, **take it**.

**v0.7 does not change this analysis.** The MCU still dominates and still stays
awake for the ~1 Hz VE.Direct stream. What v0.7 adds to the node's floor is the LCD
backlight when on (off by default, §5.6), the I²C expanders, and the INA226/LM75/RTC
— all small, all already inside the vendor's 47.84 mA working figure. What it
removes is the USB-C adapter's conversion loss. The one genuinely new continuous
cost is **10–20 ms input polling**, which is I²C traffic on an already-awake MCU:
no measurable power implication.

---

## 9. Debug and bench tooling

### 9.1 What changed in v0.6

The BusT4 sniffing programme is gone from the critical path along with BusT4
itself. No pin-identification gate, no laptop capture phase, no read-only tap
procedure, no raw frame streaming. Those are preserved in Appendix B for anyone
pursuing Phase 2.

What replaces it is much simpler: **the 1050 interface can be exercised with a
DVM, a continuity tester, and the board's own front panel.** Every signal is a dry
contact or a documented terminal.

### 9.2 On-node debug tooling

- **Relay dry-run mode — new.** A `switch` that makes GateLink log and publish
  every relay pulse it *would* have issued without energizing anything. This is the
  single most valuable new debug feature: command logic can be exercised end to end
  — HA → LoRa → bridge → node → decision — **without moving a large motorized
  gate.** Default off; enabling it also lights the display (§5.6).
- **Input injection.** Synthetic assertions on IN1–IN6 in configurable order and
  spacing, to test §5.4 direction classification, §4.2.4 state derivation, and the
  §5.4.5 held-open alert without driving a car back and forth. **Must cover the long
  cases** — 30 s EXIT→SAFETY gaps and partial traversals.
- **Packet loopback:** (a) RF loopback — a node echoes received frames to validate
  link and framing without the peer; (b) internal loopback — feed TX frames back
  into the RX parser with no radio.
- **Dummy status push:** synthetic VE.Direct and gate-state data exercising the full
  pipeline (node → LoRa → bridge → MQTT → HA) without real hardware.
- **MQTT as bench harness:** `mosquitto_sub -t 'lran/#'` to watch every decoded
  payload live; `mosquitto_pub` to inject commands or fake status, decoupled from HA
  and the RF link.
- **Device simulators:** a VE.Direct frame generator covering **both text and HEX**;
  a dummy BMS BLE peripheral.
- **Multi-node simulator:** a `lran-simnode` firmware target that registers as node
  `0x02`, answers polls, and emits events on demand — validates addressing, per-node
  keying, availability watchdog, fragmentation, and CAD/backoff (§6.7) before
  WellLink exists.
- **On-demand display:** §5.6.
- **Leveled serial logging** on all nodes.

### 9.3 1050 interface bring-up procedure

Ordered, and safe to perform incrementally.

1. **Reprogram the 1050.** AUX1 → OPEN and LOCK; AUX2 → UNLOCK; OUT1 → OPEN;
   OUT2 → MOVING. Record all settings in `/docs/1050-config.md`.
2. **Verify by hand.** With no GateLink connected, momentarily short each input
   terminal to GND and confirm the expected behavior. Meter OUT1/OUT2 contacts
   through a full open/close cycle and confirm the state table in §4.2.4.
3. **Measure IN5 and IN6 levels** to establish idle state and sense polarity
   (§4.2.4). *v0.7: no longer a prerequisite for divider design — StamPLC's inputs
   accept 5–36 V directly — but still required to get the logic sense right.*
4. **Move the Diablo to unswitched power** (§4.6) and confirm it still detects.
5. **Wire inputs only.** Connect IN1–IN6 with GateLink's relay outputs left
   physically disconnected. Validate state derivation, detection, and direction
   against real gate cycles driven by the keypad. This is a **read-only** phase and
   cannot move the gate.
6. **Wire relays with dry-run enabled** (§9.2). Exercise every command path from HA
   and confirm the logged intent matches expectations.
7. **Disable dry-run.** Test each command with a clear line of sight to the gate.

### 9.4 Bench analysis — placeholder

`TBD`. Outstanding work. Prior bench procedure exists as a starting point.

---

## 10. Dev environment, repo, and build

```
/firmware/bridge/        # LoRaBridge PlatformIO project
/firmware/gatelink/      # GateLink PlatformIO project
/firmware/welllink/      # WellLink (future)
/firmware/simnode/       # simulated node for multi-node bench testing (§9.2)
/lib/lran-platform/      # host HAL: relays, inputs, display, buttons, INA226,
                         #   LM75, RTC, SD. Shared with AquaLink (§3.5)
/lib/lran-protocol/      # shared framing/addressing/HMAC/CRC/fragmentation
    /schemas/            # versioned per-node payload schemas (§6.4.3)
/lib/vedirect/           # VE.Direct text + HEX (osh-labs port) [MIT]
/lib/bms-ble/            # BLE BMS client (§5.7)
/tools/                  # bench scripts, simulators, MQTT helpers
/ha/                     # example discovery payloads + automations
/hardware/carrier/       # GateLink carrier board: schematic, layout, BOM (§4.7.4)
/docs/                   # this PRD, design notes, 1050-config.md,
                         #   bms-protocol.md, mppt-config.md,
                         #   protocol-changelog.md, appendix-b-bust4/
LICENSE                  # D11 — REOPENED, see §12.2
THIRD_PARTY_NOTICES.md   # see §12
```

> **`/lib/bust4/` is gone.** That single removal is why §12.2 reopens.

- **PlatformIO** multi-environment build (one env per firmware target), VS Code +
  Claude Code.
- **CI:** GitHub Actions building **all** firmware targets on push. With several
  targets sharing `/lib/lran-protocol/`, CI catches a protocol change breaking a
  node nobody rebuilt locally.
- **Secrets:** LoRa `master_key` (§6.5.1), WiFi creds, MQTT creds, OTA password via
  untracked config / build flags (never committed).
- **Updates:** bridge OTA + USB (§6.2); remote nodes USB only.
- **Protocol changes** require a version bump and a `/docs/protocol-changelog.md`
  entry (§6.4.5).

---

## 11. Safety

- The **1050 remains the safety authority** — obstruction, photocells, and detector
  logic stay with the operator. LRAN issues commands through documented accessory
  inputs and *observes* detector state; it must never be relied on to prevent unsafe
  motion. §5.4 is a monitoring and notification feature, not a safety feature.
- **No safety interlock is bypassed.** FIRE was explicitly rejected as a command
  path because it clears hard shutdown — a latched entrapment state that must
  require human intervention (§5.3.2). GateLink *senses* FIRE (IN5) and *reports*
  hard shutdown (IN6); it never asserts either.
- **No maintained assertions.** Every GateLink output is a momentary pulse on a
  normally-open contact (§4.2.1). An unpowered, crashed, or removed GateLink asserts
  nothing and the gate behaves exactly as it does today.
- **A manual UNLOCK path is required** (§4.2.6). LRAN will leave the gate
  OPEN+LOCKED far more often than a human does today, and plain STEP does not
  override a lock.
- **The §5.4.5 held-open alert notifies; it does not close the gate.** No automatic
  close behavior is initiated by LRAN beyond explicit HA commands.
- **Fuse the battery tap.** A 100 Ah LiFePO4 delivers far higher short-circuit
  current than the FLA it replaces. Size and place the fuse for the new pack.
- **MPPT configuration is a remotely writable, battery-affecting path.** The three
  gates in §6.6.3 are safety requirements, not conveniences. Reconfigure for LiFePO4
  before first charge (§8.1.2).
- **Do not rely on graceful shutdown.** The pack BMS opens under fault and takes
  terminal voltage to zero (§4.5). Nothing critical may depend on an orderly
  power-down. Benign for the 1050 interface — relays simply drop out.
- **Measure IN5 and IN6 before connecting them** (§4.2.4). Both are voltage-sense,
  not dry contact, and the alarm output supplies fused 12 V. *v0.7 removes the
  damage risk — StamPLC's inputs are rated 5–36 V — but the measurement is still
  required to get sense polarity right.*
- **The host is operated outside its temperature rating** (§4.7.6, **D29**). This is
  a deliberate, instrumented exceedance, not an oversight. If seasonal logging shows
  sustained excursions, the platform decision must be revisited rather than
  rationalised.
- **The BusT4 port is not connected in v1.** Its VCC pin carries 24 V. If Appendix
  B is ever pursued, that section's cautions apply in full.
- Grounding discipline: bring GateLink ground and the MPPT signal ground to battery
  negative at a **single common point**, and keep the 1050's motor-terminal wiring
  physically separated from signal grounds.

---

## 12. Third-party code and licenses

### 12.1 Inventory

| Component | Source | License |
|---|---|---|
| VE.Direct parser + HEX | `osh-labs/VE.Direct_mppt_arduino` | **MIT — confirmed** |
| LoRa radio driver | RadioLib | MIT |
| BLE stack | NimBLE-Arduino | Apache-2.0 |
| BMS client reference | JBD/JK community implementations — **specific source TBD pending §5.7.4** | **verify before use** |
| Arduino-ESP32 core | Espressif | LGPL-2.1-or-later |
| ESP-IDF components | Espressif | Apache-2.0 |
| mbedTLS (HMAC, HKDF) | via ESP-IDF | Apache-2.0 |
| MQTT client | PubSubClient (§6.1) | MIT |
| JSON | ArduinoJson | MIT |
| Display | U8g2 (Heltec/OLED targets) | BSD-2-Clause |
| **Platform HAL** | **`m5stack/M5StamPLC` + `M5Unified`** | **MIT** — verify at the pinned commit |
| **LCD driver** | **LovyanGFX (via M5Unified)** | **FreeBSD/BSD-2-Clause** |
| NVS / Preferences | via ESP-IDF | Apache-2.0 |
| *Nice BusT4 protocol logic* | *`pruwait` / `xdanik` / `makstech` lineage* | ***GPL-3.0 — not used in v1***. Appendix B only. |

### 12.2 Project license — **D11 REOPENED**

v0.5 resolved D11 to GPL-3.0 because porting the Nice BusT4 lineage made the
firmware a derivative work of GPL-3.0 code.

**With BusT4 out of v1, that obligation disappears.** The remaining stack is MIT,
Apache-2.0, BSD-2-Clause, and LGPL-2.1-or-later (dynamically satisfied by the
Arduino core in the usual embedded way). None of these impose copyleft on the
project. **v0.7 adds M5StamPLC, M5Unified and LovyanGFX — MIT and BSD-2-Clause —
which does not change this conclusion.**

**The license is now a free choice.** Options, with the trade in one line each:

| Option | Consequence |
|---|---|
| **MIT** | Simplest; maximum reuse; imposes only attribution |
| **Apache-2.0** | Same permissiveness plus an explicit patent grant |
| **GPL-3.0** | Still available if preferred on principle; also the least-friction path *if* Appendix B is likely to be pursued |

**Recommendation: pick MIT or Apache-2.0 now**, and if Appendix B is later
pursued, isolate the BusT4 port so the copyleft scope is understood at that point
rather than assumed today. Note that a Phase 2 that links GPL-3.0 BusT4 code into
GateLink would make *that binary* GPL-3.0 regardless of the repo's stated license —
which is a reason to keep the port in its own clearly-marked subtree if it happens.

**This decision is not urgent** but should be made before the repo is first pushed
public, since pushing is distribution.

### 12.3 Repo obligations

- `LICENSE` at root — pending D11.
- `THIRD_PARTY_NOTICES.md` listing §12.1 with copyright lines. MIT and BSD
  components require attribution retention.
- Nice's own reference documents (1050 manual, TTPCI manual, DMBM integration
  protocol PDF) are Nice-copyrighted: **link them, do not vendor them.**
- Victron VE.Direct protocol documents: link, do not vendor.

---

## 13. Open decisions register

| # | Decision | Status / notes | Resolve by |
|---|---|---|---|
| D1 | LoRa PHY params (SF/BW/CR/TX power) | open — pick after range test at ~500 ft **on both bearings** (§6.8) | Phase 1 |
| D2 | RX duty-cycle period + preamble length | **retired for GateLink** (§8.6). Appendix A; reopens only if WellLink is battery powered | — |
| D3 | BusT4 detector/movement-cause coverage vs. GPIO | **RESOLVED — GPIO** (§4.2.4, §5.4.2). BusT4 acquisition is unavailable during standby and is out of v1 regardless | done |
| D4 | Poll scheduler location | **resolved** — bridge firmware, per node, runtime-configurable via HA `number` | done |
| D5 | MQTT client library | **resolved** — `MqttTransport` abstraction, PubSubClient first, `MQTT_MAX_PACKET_SIZE` ≥1024 (§6.1) | done |
| D6 | Gate-node display trigger | **resolved** — button toggle + auto-on in any debug mode (§5.6) | done |
| D7 | BusT4 VCC handling | **superseded** — the BusT4 port is not connected in v1 (§4.3) | done |
| D8 | Entity modeling | **resolved** — `cover` primary + "Hold gate open" `switch`; position deferred (§7.2–7.3) | done |
| D9 | PV-aware profile thresholds | **retired** (§8.6) | — |
| D10 | Rx-boosted gain on/off | **retired as a power question** (§8.8). Take the +3 dB if the range test shows benefit | Phase 1 |
| **D11** | **Project license** | **REOPENED — free choice now that the GPL dependency is gone** (§12.2). MIT or Apache-2.0 recommended | Before first public push |
| D12 | VE.Direct isolation vs. level shifting | **resolved for isolation — none needed** (§4.4). **Level-shifter choice partially reopened by v0.7 as D25** | done (see D25) |
| D13 | BusT4 physical layer | **RESOLVED — differential** (§1.5). SN65HVD230 with its onboard 120 Ω terminator lifted. **Appendix B only** | done |
| D14 | Decode placement | **RESOLVED — moot.** GateLink reads discrete GPIO and drives discrete relays; there is nothing to decode (§3.3) | done |
| D15 | Battery SOC source | **open — BLE BMS preferred; SmartShunt is the selected fallback** (§5.7.4). Sniff with nRF Connect on battery arrival | Before install |
| D16 | OTA policy | **resolved** — bridge yes, remote nodes no (§6.2) | done |
| D17 | Naming | **resolved** — LRAN umbrella; `lran/` MQTT root; GateLink / WellLink / LoRaBridge (§1.6) | done |
| D18 | Auto-close observability | **RESOLVED — not needed.** §5.4.5 keys on explicit hold state (`hold_open` + IN5), not inferred auto-close | done |
| D19 | WellLink power source | **open** — mains vs. battery/solar. Determines whether Appendix A duty-cycling is needed and whether battery telemetry is required in the WellLink schema (§1.7) | Before WellLink design |
| **D20** | **1050 standby policy** | **RESOLVED — standby retained.** Command relays wake the board on their own (§4.2.3), so disabling it buys nothing and costs ~2 Ah/day (§8.2.1). Timeout value still to be set | done (value `TBM`) |
| **D21** | **Wake mechanism** | **RESOLVED — none needed.** Every command relay drives a command-class input, which wakes the board (§4.2.3). Retires the fail-safe STOP relay concept entirely | done |
| **D22** | **Hold-open mechanism** | **RESOLVED — OPEN+LOCK / UNLOCK** (§5.3.2), replicating the installation's existing mechanism. FIRE rejected (clears hard shutdown); SHADOW rejected (would block a closed gate from opening) | done |
| **D23** | **OUT1/OUT2 sense polarity** | **open** — depends on whether the relays hold state in standby (§4.2.7). If they drop, program OUT1 to CLOSE and read NC to invert the sense | Phase 2 bring-up |
| **D24** | **Manual UNLOCK path** | **open** — which of the four options in §4.2.6 is provided. Keyswitch relocated to AUX2 recommended | Before first hold-open use |
| **D25** | **VE.Direct TX translator** | **open** — BSS138 is retained by default but may fail against a weak symmetric 5 V driver. Settled by one measurement (§4.4); fallback ADuM1201 or 74LVC1G17. BSS138 stays on the RX direction either way | Before carrier board build |
| **D26** | **StamPLC 3.3 V rail** | **open (expected: confirms absent)** — documentation across multiple official sources says no 3.3 V is exposed (§4.7.3). Confirm against `K141_sch_StamPLC_V10_IO.pdf`. If somehow present, the carrier LDO drops from the BOM | Before carrier board build |
| **D27** | **Carrier board fabrication** | **open** — perfboard prototype vs. fabricated PCB (§4.7.4). Single node argues perfboard; the antenna feed and SPI runs argue for layout care | Phase 1 |
| **D28** | **BLE link margin from StamPLC position** | **open** — the Stamp-S3A 2.4 GHz antenna is internal with no external option (§4.7.5). Measure RSSI to the BMS from the intended mounting position. Fallback is the existing D15 SmartShunt path | Phase 5 |
| **D29** | **Enclosure thermal envelope** | **open** — StamPLC is rated 0–40 °C and the enclosure is outdoors (§4.7.6). **The principal risk introduced by v0.7.** Instrument with the onboard LM75 and publish; vent/shade the enclosure | Phase 9 / ongoing |

### 13.1 Measurement backlog

| Item | Section | Blocks |
|---|---|---|
| **Auto-close timeout setting** | §8.2.1, §5.3.3 | Close-path timing, held-open semantics |
| **Standby timeout setting** | §8.2.1 | D20 value |
| **OUT1/OUT2 behavior during standby** | §4.2.7 | **D23** |
| **IN5 (FIRE) and IN6 (alarm) idle/asserted voltages** | §4.2.4 | Sense polarity and idle state. *No longer a divider-sizing exercise — StamPLC's inputs take 5–36 V directly (v0.7)* |
| **Which loop input the Diablo occupies** | §4.2.5 | Spare-capacity record |
| **Whether EDGE (28) is in use** | §4.2.5 | Spare-capacity record |
| **Exit wand hold/de-assert behavior** | §5.4.2 | `detect_debounce_ms`, re-trigger lockout |
| **Real EXIT→SAFETY gap times (drive the vehicle)** | §5.4.4 | `detect_sequence_window_ms` default |
| **DSP-7LP input voltage range** | §4.6 | Terminal 10/11 vs. battery tap |
| **BMS BLE GATT map (nRF Connect)** | §5.7.4 | **D15** |
| 1050 current, standby and active, inline meter | §8.2 | Budget confidence |
| ~~12 V→USB-C adapter no-load draw~~ | — | *Retired — adapter deleted in v0.7 (§4.5)* |
| **MPPT VE.Direct TX low-excursion under 10 kΩ** | §4.4 | **D25**, carrier BOM |
| **StamPLC IO schematic — confirm no 3.3 V rail** | §4.7.3 | **D26**, carrier design |
| **BLE RSSI to BMS from StamPLC mounting position** | §4.7.5 | **D28** |
| **Enclosure temperature, seasonal (LM75)** | §4.7.6 | **D29** |
| Gate node average current, continuous RX | §8.2 | Budget |
| ~~Relay module 3.3 V trigger reliability~~ | — | *Retired — relay module deleted in v0.7; relays are onboard (§4.2.2)* |
| **Node current via onboard INA226** | §4.5, §8.2 | Budget confidence — now self-measuring |
| **Range/RSSI on both bearings** | §6.8 | D1, bridge antenna siting |
| One week MPPT yield + Vmin baseline | §8.5 | install go/no-go |

---

## 14. Test plan / bring-up phases

0. **Carrier board bring-up — new in v0.7.** Confirm **D26**, build the carrier
   (LDO + SX1262 + VE.Direct front end), verify the 3.3 V rail under SX1262 TX load,
   and prove RadioLib talks to the radio on the §4.7.2 pin map. Resolve **D27**.
   *Do this before phase 1* — until it passes, GateLink has no radio.
1. **RF link only** — StamPLC+carrier and a Heltec bridge; ping + loopback; RSSI/SNR
   at ~500 ft **on both the gate bearing and the well bearing**. Resolve D1, D10.
   Site the bridge antenna. **Two Heltecs remain the faster way to characterise the
   link alone** if the carrier is not ready — the PHY result is host-independent.
2. **1050 configuration and manual validation** — §9.3 steps 1–4. Reprogram AUX and
   OUT terminals, verify by hand with a DVM and jumper, measure IN5/IN6 levels, move
   the Diablo to unswitched power. **No LRAN hardware required.** Runs in parallel
   with phase 1.
3. **Protocol/framing** — input injection to validate §5.4 direction logic and
   §5.4.5 held-open alerting, including 30 s gaps and partial traversals. **Run
   `simnode` alongside GateLink** to validate addressing, per-node keys, availability
   watchdog, fragmentation, and CAD/backoff (§6.7).
4. **VE.Direct** — **resolve D25 by measurement first** (§4.4), then real MPPT 75/15
   on the bench through the selected translator; verify full text field parsing
   **and HEX request/response round-trip** — the round-trip is the proof that the
   MPPT accepts our RX drive level, which is undocumented. Include write rejection
   when disarmed or unauthenticated (§6.6.3). Start the §8.5 one-week baseline log.
5. **Battery and BMS** — on battery arrival: nRF Connect enumeration (**D15**); MPPT
   reconfiguration for LiFePO4 (§8.1.2) and readback verification; BLE client
   bring-up; low-temp inhibition detection (§8.4). **Measure BLE RSSI from the
   intended StamPLC mounting position (D28)** before declaring the BMS path viable.
6. **1050 inputs live** — §9.3 step 5. Wire IN1–IN6 to the opto-isolated input
   terminals, relays still disconnected. Read-only; cannot move the gate. Resolve
   **D23**. Confirm the 10–20 ms poll loop meets §5.4 timing under real gate cycles.
7. **1050 relays live** — §9.3 steps 6–7. Dry-run first, then real pulses with a
   clear line of sight. Confirm **D24** manual UNLOCK path works before the first
   real hold-open.
8. **HA integration** — MQTT Discovery entities per device; command round-trip;
   per-node availability; detection/direction entities; **verify the held-open event
   fires exactly once and does not replay on HA restart** (§7.4).
9. **Field** — install, range/power soak, error-path validation, confirm measured
   daily Ah against §8.2 **using the onboard INA226**, begin §8.5 ongoing monitoring,
   and **start the §4.7.6 seasonal enclosure-temperature log (D29)**.

---

## Appendix A — Low-power RX duty-cycling (retained design, not used by GateLink)

Removed from GateLink in v0.5, preserved because **WellLink may be battery
powered** (D19) without a 100 Ah pack behind it.

**PV-aware adaptive profile.** A node reads its charge controller and uses PV
output to select a power profile, with **hysteresis** to avoid dawn/dusk flapping:

| Profile | Trigger | MCU | Peripherals | LoRa RX | Poll |
|---|---|---|---|---|---|
| Daytime | PV charging | awake | full rate | near-continuous | normal |
| Night/low-PV | PV fallen off | light-sleep | occasional | SX1262 hardware RX duty-cycle | reduced |

**Keeping duty-cycled RX responsive.** The **bridge sends commands with an extended
preamble** long enough to span the node radio's sleep window, so the sleeping
receiver detects the preamble on its next wake. Worst-case latency ≈ one duty-cycle
period, independent of sleep depth. Reference target: ≤ 2 s with a ~2 s RX
duty-cycle period.

**Multi-node caveat.** An extended preamble is address-agnostic — **every**
duty-cycled node on the channel wakes and receives the header before discarding a
frame not addressed to it. Enable **SX126x hardware node-address filtering** (§6.8)
so the discard happens in silicon.

**Do not adopt this design for a node that does not need it.** Measured against
adequate storage it buys single-digit percentages of the budget at real cost in
complexity, latency, and overnight data continuity.

---

## Appendix B — BusT4 (optional Phase 2)

Not required for any v1 goal. Preserved because the bench work is done, the
findings are solid, and the hardware is on hand.

### B.1 What it would add

Everything in §2.1 is already met without it. BusT4 would additionally provide:

- **1050 configuration read/write** — auto-close time, force, speed, standby
  timeout, and the rest, from HA instead of the front panel.
- **Specific error and diagnostic codes**, where v1 sees only the hard-shutdown
  latch (IN6).
- **Cycle counters and service interval.**
- **Encoder position**, on controllers that expose it — the only route to the
  position reporting deferred in §7.3.
- **Movement cause as reported by the board**, rather than inferred (§5.5).

### B.2 What it costs

- A GPL-3.0 dependency, with the licensing consequence in §12.2.
- A differential transceiver wired to a connector whose adjacent pin carries 24 V.
- A custom UART implementation: 19200 8N1 with a **519–590 µs break** preceding each
  burst, which a stock `HardwareSerial` configuration will not produce.
- A characterization phase — laptop, logic analyzer, read-only tap — before any
  frame is transmitted.
- **It is unavailable during standby.** Any BusT4 feature must either tolerate that
  or be preceded by a wake pulse on a command input (§4.2.3).

### B.3 Bench findings, preserved

Measured pinout, resistances, and the differential determination are in §1.5.
Summary: **6P4C jack, position 2 = 24 V, positions 3 and 4 = differential data pair
(~145–174 Ω between them, open to ground), position 5 = GND. Use the SN65HVD230
with its onboard 120 Ω terminator lifted**, since the 1050 end already presents a
defined differential impedance and doubling the load buys nothing at 19200 baud
over inches of cable.

Do **not** substitute the SN65HVD231 — it disables the receiver in standby. Confirm
the '230 has no TXD dominant timeout before relying on sustained dominant for the
break; the feature belongs to the later SN65HVD25x parts, but verify against the
datasheet timing table.

### B.4 Community sources

`pruwait/Nice_BusT4` (original), `xdanik/Nice_BusT4` (English), `makstech/esphome-BusT4`
(ESP-IDF, most complete), `karol27/Nice_BusT4_WT32-ETH01`, `bpietroiu/esphome-nice-bidiwifi`
(clean-room rewrite), `gashtaan/nice-bidiwifi-firmware` (BiDi-WiFi schematics).
All GPL-3.0 except the last two — verify each before use.

Known command frames and the `INF_IO` request used for limit-switch confirmation
are documented in those repos. Note that `makstech` exposes `set_standby(bool)` and
`set_auto_close(bool)` as SET commands, which is the natural Phase 2 entry point:
1050 configuration from HA.

### B.5 If pursued — v0.7 note on the physical layer

**The StamPLC may make Appendix B cheaper.** BusT4 is UART framing on a differential
pair, and the ESP32-S3 GPIO matrix permits routing a UART to **G42/G43**, which are
wired to StamPLC's onboard **SIT1044 CAN transceiver** on the PWR-CAN port. If that
works, the SN65HVD230 is not needed and PORT.C stays free.

Two things must be verified before relying on it, neither of which is worth doing
unless Phase 2 is actually pursued:

1. **Termination.** Whether M5 fitted a 120 Ω terminator on the PWR-CAN pair. The
   §1.5 measurement of 145 Ω across the 1050's data pins indicates that side is
   already terminated; a second terminator on a point-to-point link is undesirable.
   D13's note about lifting the SN65HVD230's terminator applies equally here.
2. **XT30 pinout.** The PWR-CAN connector's power pins are tied **directly to VIN**.
   Per §11 and D7, the 1050's Oview VCC pin carries 24 V and is not connected —
   under no circumstances may these meet.

### B.5.1 If pursued

Read §4.2 of PRD v0.5 for the full bring-up procedure, branch analysis, and safety
gating. That document remains the authoritative reference for the BusT4 physical
layer and should be preserved in `/docs/appendix-b-bust4/`.

---

## 15. Changelog

- **v0.7** — **Moved the GateLink host from the Heltec LoRa V3 to the M5Stack
  StamPLC** (§4.7). v0.6 defined the I/O requirement — four momentary relays and six
  inputs, two of them 12 V voltage-sense — and met it with an external relay module,
  per-channel dividers and clamps, and a 12 V→USB-C adapter bolted to a Heltec.
  StamPLC provides all of it natively behind screw terminals on a DIN rail, so
  **the relay module, the dividers and clamps, and the power adapter all leave the
  BOM** (§4.1.2), along with every hand-built discrete circuit in the design. The
  cost is the radio: **LoRa becomes an external SX1262 on a carrier board** (§4.7.4),
  which must also carry a **3.3 V LDO because StamPLC exposes no 3.3 V rail**
  (§4.7.3, **D26**) and whose EXT_5V sits at ~4.76 V — excluding an AMS1117.
  §4.7.2 sets out the pin budget: SPI plus G40/G41 for BUSY and DIO1, with **G14
  freed for the radio reset by moving input acquisition to a 10–20 ms poll**
  (§4.2.4), and **G3 explicitly reserved** because it resets the LCD and the
  expanders together. **The BLE BMS work ports unchanged** — Stamp-S3A and Heltec V3
  are both ESP32-S3FN8, so §5.7 is a recompile (§4.7.5) — but the 2.4 GHz antenna is
  internal with no external option, giving **D28**. **Power is a wash**: ~7.0 →
  ~6.9 Ah/day, since the vendor's 47.84 mA working figure lands where the Heltec
  allocation was and the adapter's conversion overhead disappears; the **onboard
  INA226 now measures the node's own draw**, closing several `TBM` rows by
  construction (§4.5, §8.2). The display gets simpler: **three dedicated buttons
  instead of one strapping pin, and backlight switching instead of Vext power
  cycling**, retiring the SSD1306 re-init failure mode (§5.6). Added **§3.5**, the
  platform convergence with **AquaLink**, requiring a `/lib/lran-platform/` HAL and
  an **injected radio pin map** so one SX1262 driver serves both the StamPLC carrier
  and the Heltec bridge. **LoRaBridge stays on the Heltec V3.** Reopened the
  VE.Direct translator question as **D25**: Victron specifies the MPPT TX port as a
  5 V logic signal driving at most 22 kΩ, implying a ~10 kΩ source impedance that a
  BSS138's 10 kΩ pull-up may not overcome — one measurement decides between keeping
  the BSS138 and falling back to an ADuM1201 or 74LVC1G17, with the BSS138 retained
  for the RX direction regardless (§4.4). Added **D27** (carrier fabrication) and
  **D29** (**operating temperature — the principal risk of this revision**, since
  StamPLC is rated 0–40 °C and the enclosure is outdoors; instrumented via the
  onboard LM75). Added a **phase 0 carrier bring-up** to the test plan. Noted in
  **Appendix B** that StamPLC's onboard PWR-CAN transceiver may serve as the BusT4
  differential driver, potentially retiring the SN65HVD230 from Phase 2 as well.
  **Nothing above §4 changed**: protocol, command model, detection logic, HA entity
  model, keying and safety posture all carry forward from v0.6 intact.
- **v0.6** — **Rebuilt the 1050 interface on documented accessory I/O and moved
  BusT4 to an optional Phase 2 (Appendix B).** Bench probing established that BusT4
  is dead during 1050 standby, that the loop detector is unpowered during standby,
  and that a wake mechanism would be needed regardless — while also establishing
  that every v1 goal is reachable through terminals Nice documented for third-party
  use. GateLink now drives **four momentary relays** (AUX1 = OPEN and LOCK,
  AUX2 = UNLOCK, Guard Station Open, Guard Station Close) and reads **six GPIO
  inputs** (OUT1 = OPEN, OUT2 = MOVING, Diablo contact, exit wand contact, FIRE
  sense, alarm sense) — §4.2. **The wake problem dissolves** (**D21**): every
  command relay drives a command-class input, which wakes the board, so **standby is
  retained** (**D20**) and the ~2 Ah/day it saves is kept (§8.2.1). **Hold-open
  resolves to OPEN+LOCK / UNLOCK** (**D22**), replicating the mechanism already in
  use on this installation; FIRE was rejected because it clears hard shutdown and
  SHADOW because it would block a closed gate from opening (§5.3.2). **§5.4.5 is
  redefined on explicit hold state** rather than inferred "statically open",
  retiring **D18** and removing the 30 s arming delay — alerts now fire on the first
  edge with no dead window. **D3 resolves to GPIO**; **D14 becomes moot** since
  there is nothing left to decode; **D13 resolves to differential** and is preserved
  for Appendix B only; **D7 is superseded**. **D11 is REOPENED** — with `/lib/bust4/`
  gone the project has no copyleft dependency and the license is a free choice
  (§12.2). Added a **new requirement to move the Diablo DSP-7LP to unswitched
  power** (§4.6), without which §5.4.5 is unimplementable. Added **relay dry-run
  mode** and **input injection** as debug features (§9.2) and a staged 1050 bring-up
  procedure with a read-only phase (§9.3). Added **D23** (OUT relay sense polarity)
  and **D24** (manual UNLOCK path — now a safety requirement, since plain STEP does
  not override a lock). Added command **deduplication on `(boot_id, seq)`** because
  relay pulses are not idempotent (§6.5.4). BOM: relay module in, SN65HVD230 and the
  logic analyzer out of the critical path; BSS138 demoted to VE.Direct only.
  Removed: raw BusT4 frame streaming, the `RAW` frame type, the laptop sniffing
  phase, and the BusT4 pin-identification gate.
- **v0.5** — Renamed to LoRa Remote Automation Network (LRAN); MQTT root `gatelink/`
  → `lran/`; one HA device per node (**D17**). Expanded the bridge to a
  general-purpose multi-node LoRa↔MQTT gateway — per-node addressing, per-node HMAC
  keys via HKDF, per-node sequence tables, availability watchdog, explicit payload
  schema IDs, fragmentation, protocol version tolerance N/N−1, and CAD + randomized
  backoff media access. Scoped **WellLink** and added a `simnode` bench target.
  Corrected the vehicle detection topology to a **Diablo DSP-7LP** with a single
  combined safety contact plus a separate exit wand. Added the **VE.Direct HEX
  protocol** with HMAC + armed write-enable + audit trail. Replaced the FLA with a
  **100 Ah LiFePO4**; added LiFePO4 MPPT reconfiguration and low-temperature
  charge-inhibition detection. Dropped the night/low-PV profile (**D2, D9, D10**).
  Enabled **BLE** for the WattCycle BMS with a SmartShunt fallback (**D15**). Moved
  BusT4 sniffing off the node; added opt-in raw frame streaming with hybrid decode
  (**D14**). Enabled **OTA for the bridge only** (**D16**). Resolved **D11** to
  GPL-3.0.
- **v0.4** — Added physical installation context (single enclosure, no motor
  inside); resolved **D12** on that basis. Consolidated signal conditioning around a
  single BSS138 module with rise-time analysis. Restructured the BusT4 electrical
  section into Branch A (single-ended) and Branch B (differential); added **D13**.
- **v0.3** — Renamed to LoRa GateLink. **Corrected VE.Direct to 5 V.** Added BusT4
  electrical characteristics and a pin identification procedure. Promoted loop
  detection to a hard requirement with direction classification. Expanded the power
  budget. Added the license inventory and **D11**. Resolved **D5–D8**.
- **v0.2** — Added BusT4 sniff/monitor as a retained diagnostic (**D3**); PV-aware
  day/night power profile with extended-preamble wake; poll scheduler fixed to the
  bridge (**D4**); added **D9**, **D10**.
- **v0.1** — Initial draft. Two custom Heltec V3 nodes, BusT4 + VE.Direct on the
  gate node, LoRa 915 MHz link, bridge as LoRa↔MQTT gateway via MQTT Discovery.
